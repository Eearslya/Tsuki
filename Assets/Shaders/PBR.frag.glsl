#version 450 core

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inViewPos;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV0;

layout(set = 0, binding = 0) uniform SceneData {
	mat4 Projection;
	mat4 View;
	mat4[4] LightMatrices;
	vec4 CascadeSplits;
	vec4 CameraPos;
	vec4 SunDirection;
	int ShadowCascadeCount;
	int ShadowPCF;

	int DebugShadowCascades;
} Scene;
layout(set = 0, binding = 1) uniform sampler2DArray texShadowMap;

layout(set = 1, binding = 0) uniform MaterialData {
	vec4 BaseColorFactor;
	vec4 EmissiveFactor;
	int HasAlbedo;
	int HasNormal;
	int HasPBR;
	int HasEmissive;
	int AlphaMode;
	float AlphaCutoff;
	float Metallic;
	float Roughness;
} Material;
layout(set = 1, binding = 1) uniform sampler2D texAlbedo;
layout(set = 1, binding = 2) uniform sampler2D texNormal;
layout(set = 1, binding = 3) uniform sampler2D texPBR;
layout(set = 1, binding = 4) uniform sampler2D texEmissive;

layout(location = 0) out vec4 outColor;

struct PBRInfo {
	float NdotL;
	float NdotV;
	float NdotH;
	float LdotH;
	float VdotH;
	float PerceptualRoughness;
	float Metallic;
	vec3 Reflectance0;
	vec3 Reflectance90;
	float AlphaRoughness;
	vec3 DiffuseColor;
	vec3 SpecularColor;
};

const float Ambient = 0.15f;
const mat4 BiasMat = mat4(
	0.5, 0.0, 0.0, 0.0,
	0.0, 0.5, 0.0, 0.0,
	0.0, 0.0, 1.0, 0.0,
	0.5, 0.5, 0.0, 1.0
);
const float Pi = 3.141592653589793;

vec3 Diffuse(PBRInfo pbr) {
	return pbr.DiffuseColor / Pi;
}

float GeometricOcclusion(PBRInfo pbr) {
	float NdotL = pbr.NdotL;
	float NdotV = pbr.NdotV;
	float r = pbr.AlphaRoughness;

	float attenuationL = 2.0 * NdotL / (NdotL + sqrt(r * r + (1.0 - r * r) * (NdotL * NdotL)));
	float attenuationV = 2.0 * NdotV / (NdotV + sqrt(r * r + (1.0 - r * r) * (NdotV * NdotV)));
	return attenuationL * attenuationV;
}

vec3 GetNormal() {
	if (Material.HasNormal == 0) { return normalize(inNormal); }

	vec3 tangentNormal = texture(texNormal, inUV0).xyz * 2.0 - 1.0;
	vec3 q1 = dFdx(inWorldPos);
	vec3 q2 = dFdy(inWorldPos);
	vec2 st1 = dFdx(inUV0);
	vec2 st2 = dFdy(inUV0);

	vec3 N = normalize(inNormal);
	vec3 T = normalize(q1 * st2.t - q2 * st1.t);
	vec3 B = -normalize(cross(N, T));
	mat3 TBN = mat3(T, B, N);

	return normalize(TBN * tangentNormal);
}

vec3 ImageBasedLighting(PBRInfo pbr, vec3 n, vec3 reflection) {
	vec3 diffuse = pbr.DiffuseColor * vec3(0.15);

	return diffuse;
}

float MicrofacetDistribution(PBRInfo pbr) {
	float roughnessSq = pbr.AlphaRoughness * pbr.AlphaRoughness;
	float f = (pbr.NdotH * roughnessSq - pbr.NdotH) * pbr.NdotH + 1.0;
	return roughnessSq / (Pi * f * f);
}

float Shadow(PBRInfo pbr, vec4 shadowCoord, vec2 offset, uint cascadeIndex) {
	float shadow = 1.0;
	float ShadowBias = 0.00025f;
	float bias = ShadowBias + mix(ShadowBias, 0.0f, pbr.NdotL);
	if (shadowCoord.z > -1.0 && shadowCoord.z < 1.0) {
		float dist = texture(texShadowMap, vec3(shadowCoord.st + offset, cascadeIndex)).r;
		if (shadowCoord.w > 0.0 && dist < shadowCoord.z - bias) {
			shadow = Ambient;
		}
	}
	return shadow;
}

float ShadowPCF(PBRInfo pbr, vec4 shadowCoord, uint cascadeIndex) {
	ivec2 texDim = textureSize(texShadowMap, 0).xy;
	float scale = 0.75;
	float dx = scale * 1.0 / float(texDim.x);
	float dy = scale * 1.0 / float(texDim.y);

	float shadowFactor = 0.0;
	int count = 0;
	int range = 2;

	for (int x = -range; x <= range; x++) {
		for (int y = -range; y <= range; y++) {
			shadowFactor += Shadow(pbr, shadowCoord, vec2(dx * x, dy * y), cascadeIndex);
			count++;
		}
	}

	return shadowFactor / count;
}

vec3 SpecularReflection(PBRInfo pbr) {
	return pbr.Reflectance0 + (pbr.Reflectance90 - pbr.Reflectance0) * pow(clamp(1.0 - pbr.VdotH, 0.0, 1.0), 5.0);
}

void main() {
	// Base Color
	vec4 baseColor = Material.HasAlbedo == 1 ? texture(texAlbedo, inUV0) : vec4(1, 1, 1, 1);
	baseColor *= Material.BaseColorFactor;

	// Alpha Cutoff
	if (Material.AlphaMode == 1) {
		if (baseColor.a < Material.AlphaCutoff) { discard; }
	}

	// Metallic/Roughness
	float perceptualRoughness = Material.Roughness;
	float metallic = Material.Metallic;
	if (Material.HasPBR == 1) {
		vec4 pbr = texture(texPBR, inUV0);
		perceptualRoughness = pbr.g * perceptualRoughness;
		metallic = pbr.b * metallic;
	}
	perceptualRoughness = clamp(perceptualRoughness, 0.04, 1.0);
	metallic = clamp(metallic, 0.0, 1.0);

	// Diffuse
	vec3 f0 = vec3(0.04f);
	vec3 diffuseColor = baseColor.rgb * (vec3(1.0) - f0);
	diffuseColor *= 1.0 - metallic;

	// PBR Info
	vec3 N = GetNormal();
	vec3 V = normalize(Scene.CameraPos.xyz - inWorldPos);
	vec3 L = normalize(-Scene.SunDirection.xyz);
	vec3 H = normalize(L + V);
	vec3 specularColor = mix(f0, baseColor.rgb, metallic);
	float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);
	float reflectance90 = clamp(reflectance * 25.0, 0.0, 1.0);
	vec3 reflection = -normalize(reflect(V, N));
	reflection.y *= -1.0f;
	PBRInfo pbr;
	pbr.NdotL = clamp(dot(N, L), 0.001, 1.0);
	pbr.NdotV = clamp(abs(dot(N, V)), 0.001, 1.0);
	pbr.NdotH = clamp(dot(N, H), 0.0, 1.0);
	pbr.LdotH = clamp(dot(L, H), 0.0, 1.0);
	pbr.VdotH = clamp(dot(V, H), 0.0, 1.0);
	pbr.PerceptualRoughness = perceptualRoughness;
	pbr.Metallic = metallic;
	pbr.Reflectance0 = specularColor.rgb;
	pbr.Reflectance90 = vec3(1.0) * reflectance90;
	pbr.AlphaRoughness = perceptualRoughness * perceptualRoughness;
	pbr.DiffuseColor = diffuseColor;
	pbr.SpecularColor = specularColor;

	// Microfacet Model
	vec3 F = SpecularReflection(pbr);
	float G = GeometricOcclusion(pbr);
	float D = MicrofacetDistribution(pbr);

	// Lighting
	vec3 lightColor = vec3(1.0f);
	vec3 diffuseContrib = (1.0 - F) * Diffuse(pbr);
	vec3 specContrib = F * G * D / (4.0 * pbr.NdotL * pbr.NdotV);
	vec3 color = pbr.NdotL * lightColor * (diffuseContrib + specContrib);
	color += ImageBasedLighting(pbr, N, reflection);

	// Emission
	if (Material.HasEmissive == 1) {
		color += texture(texEmissive, inUV0).rgb;
	}

	// Shadowing
	uint cascadeIndex = 0;
	for (uint i = 0; i < Scene.ShadowCascadeCount - 1; ++i) {
		if (inViewPos.z < Scene.CascadeSplits[i]) {
			cascadeIndex = i + 1;
		}
	}
	vec4 shadowCoord = (BiasMat * Scene.LightMatrices[cascadeIndex]) * vec4(inWorldPos, 1.0f);
	float shadow = 0.0f;
	if (Scene.ShadowPCF == 1) {
		shadow = ShadowPCF(pbr, shadowCoord / shadowCoord.w, cascadeIndex);
	} else {
		shadow = Shadow(pbr, shadowCoord / shadowCoord.w, vec2(0, 0), cascadeIndex);
	}

	// Final Color
	outColor.rgb = color * shadow;
	outColor.a = baseColor.a;

	//outColor.rgb = (F + 1.0) * 0.5;
	//outColor.rgb = F;
	//outColor.rgb = vec3(G);

	if (Scene.DebugShadowCascades == 1) {
		switch(cascadeIndex) {
			case 0: outColor.rgb *= vec3(1.0f, 0.25f, 0.25f); break;
			case 1: outColor.rgb *= vec3(0.25f, 1.0f, 0.25f); break;
			case 2: outColor.rgb *= vec3(0.25f, 0.25f, 1.0f); break;
			case 3: outColor.rgb *= vec3(1.0f, 1.0f, 0.25f); break;
		}
	}
}
