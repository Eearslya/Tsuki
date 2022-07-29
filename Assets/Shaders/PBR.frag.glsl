#version 450 core
#extension GL_EXT_control_flow_attributes : require

const float Epsilon = 0.00001;
const float Pi = 3.141592;
const int ShadowCascadeCount = 4;
const float TwoPi = 2 * Pi;

const vec2 PoissonDistribution[16] = vec2[](
	vec2(0.48646277630703977f, -0.2450624721017817f),
	vec2(-0.9359107460538643f, -0.07084375594778117f),
	vec2(-0.03936694297339079f, 0.8519686452854027f),
	vec2(-0.3187392232073286f, -0.8890256837250079f),
	vec2(0.690340312231677f, 0.4440538237500569f),
	vec2(-0.723615309068909f, 0.6574892537563221f),
	vec2(-0.21505542429778174f, 0.03562234361636762f),
	vec2(0.4147737640156338f, -0.8329994021477604f),
	vec2(0.14228583114990603f, 0.3723855249238431f),
	vec2(-0.7571249781327348f, -0.6276207998302987f),
	vec2(0.9614531330983537f, -0.2643538584355873f),
	vec2(0.0698258251802725f, -0.48768314679241004f),
	vec2(0.40046580523553643f, 0.8185779516111343f),
	vec2(-0.6577826954105431f, 0.2591112608603247f),
	vec2(-0.5423834808805303f, -0.25569834604146363f),
	vec2(0.9753810680829711f, 0.140832128003699f)
);

struct DirectionalLight {
	vec3 Direction;
	float ShadowAmount;
	vec3 Radiance;
	float Intensity;
};

struct VertexIn {
	vec3 WorldPos;
	vec3 ViewPos;
	vec3 Normal;
	vec2 UV0;
	mat3 NormalMat;
	vec4 ShadowCoords[ShadowCascadeCount];
};

layout(location = 0) in VertexIn In;

layout(set = 0, binding = 0) uniform SceneData {
	mat4 ViewProjection;
	mat4 View;
	mat4 LightMatrices[ShadowCascadeCount];
	vec4 CascadeSplits;
	vec4 CameraPosition;
	DirectionalLight Light;
	float LightSize;
	bool CastShadows;
	bool SoftShadows;
	bool ShowCascades;
} Scene;
layout(set = 0, binding = 1) uniform sampler2DArray TexShadowMap;

layout(set = 1, binding = 0) uniform MaterialData {
	vec4 BaseColorFactor;
	vec4 EmissiveFactor;
	bool HasAlbedo;
	bool HasNormal;
	bool HasPBR;
	bool HasEmissive;
	int AlphaMode;
	float AlphaCutoff;
	float Metallic;
	float Roughness;
} Material;
layout(set = 1, binding = 1) uniform sampler2D TexAlbedo;
layout(set = 1, binding = 2) uniform sampler2D TexNormal;
layout(set = 1, binding = 3) uniform sampler2D TexPBR;
layout(set = 1, binding = 4) uniform sampler2D TexEmissive;

layout(location = 0) out vec4 outColor;

struct PBRInfo {
	vec3 Albedo;
	vec3 Normal;
	vec3 View;
	float Metallic;
	float Roughness;
	float NdotV;
} PBR;

vec3 FresnelSchlickRoughness(vec3 F0, float cosTheta, float roughness) {
	return F0 + (max(vec3(1.0 - roughness), F0), - F0) * pow(1.0 - cosTheta, 5.0);
}

float GaSchlickG1(float cosTheta, float k) {
	return cosTheta / (cosTheta * (1.0 - k) + k);
}

float GaSchlickGGX(float cosLi, float NdotV, float roughness) {
	float r = roughness + 1.0;
	float k = (r * r) / 8.0;
	return GaSchlickG1(cosLi, k) * GaSchlickG1(NdotV, k);
}

float NdfGGX(float cosLh, float roughness) {
	float alpha = roughness * roughness;
	float alphaSq = alpha * alpha;

	float denom = (cosLh * cosLh) * (alphaSq - 1.0) + 1.0;
	return alphaSq / (Pi * denom * denom);
}

vec3 DirectionalLights(vec3 F0) {
	vec3 result = vec3(0.0f);

	vec3 Li = -Scene.Light.Direction;
	vec3 Lradiance = Scene.Light.Radiance * Scene.Light.Intensity;
	vec3 Lh = normalize(Li + PBR.View);

	float cosLi = max(0.0, dot(PBR.Normal, Li));
	float cosLh = max(0.0, dot(PBR.Normal, Lh));

	vec3 F = FresnelSchlickRoughness(F0, max(0.0, dot(Lh, PBR.View)), PBR.Roughness);
	float D = NdfGGX(cosLh, PBR.Roughness);
	float G = GaSchlickGGX(cosLi, PBR.NdotV, PBR.Roughness);

	vec3 kD = (1.0 - F) * (1.0 - PBR.Metallic);
	vec3 diffuseBRDF = kD * PBR.Albedo;

	vec3 specularBRDF = (F * D * G) / max(Epsilon, 4.0 * cosLi * PBR.NdotV);
	specularBRDF = clamp(specularBRDF, vec3(0.0f), vec3(10.0f));

	result += (diffuseBRDF + specularBRDF) * Lradiance * cosLi;

	return result;
}

float GetShadowBias() {
	const float MinimumShadowBias = 0.002f;
	float bias = max(MinimumShadowBias * (1.0 - dot(PBR.Normal, Scene.Light.Direction)), MinimumShadowBias);
	return bias;
}

vec2 SamplePoisson(int index) {
	return PoissonDistribution[index % 16];
}

float PCFDirectional(sampler2DArray shadowMap, uint cascade, vec3 shadowCoords, float uvRadius) {
	float bias = GetShadowBias();

	const int samples = 16;
	float sum = 0;
	float currentDepth = shadowCoords.z - bias;
	for (int i = 0; i < samples; ++i) {
		vec2 offset = SamplePoisson(i) * uvRadius;
		float z = textureLod(shadowMap, vec3((shadowCoords.st) + offset, cascade), 0).r;
		sum += step(currentDepth, z);
	}

	return sum / float(samples);
}

float PCSSDirectional(sampler2DArray shadowMap, uint cascade, vec3 shadowCoords, float uvLightSize) {
	float bias = GetShadowBias();

	// Blocker Search Radius UV
	const float searchWidth = 0.05f;

	// Blocker Search
	int blockers = 0;
	float blockerDistance = 0.0f;
	{
		int blockerSearchSamples = 16;
		float currentDepth = shadowCoords.z - bias;

		for (int i = 0; i < blockerSearchSamples; ++i) {
			float z = textureLod(shadowMap, vec3((shadowCoords.st) + SamplePoisson(i) * searchWidth, cascade), 0).r;
			if (z < currentDepth) {
				blockers++;
				blockerDistance += z;
			}
		}

		if (blockers > 0) { blockerDistance /= float(blockers); }
	}
	if (blockers == 0) { return 1.0f; }

	// Determine PCF kernel based on blocker distance
	float penumbraWidth = (shadowCoords.z - blockerDistance) / blockerDistance;
	const float near = 0.01f;
	float uvRadius = penumbraWidth * uvLightSize * near / shadowCoords.z;
	uvRadius = min(uvRadius, 0.002f);

	return PCFDirectional(shadowMap, cascade, shadowCoords, uvRadius);
}

float HardShadowsDirectional(sampler2DArray shadowMap, uint cascade, vec3 shadowCoords) {
	float bias = GetShadowBias();
	float shadowMapDepth = texture(shadowMap, vec3(shadowCoords.st, cascade)).r;
	return step(shadowCoords.z, shadowMapDepth + bias);
}

void main() {
	vec4 baseColor = texture(TexAlbedo, In.UV0) * Material.BaseColorFactor;
	PBR.Albedo = baseColor.rgb;
	if (Material.AlphaMode == 1 && baseColor.a < Material.AlphaCutoff) { discard; }

	vec4 metalRough = texture(TexPBR, In.UV0);
	PBR.Metallic = metalRough.b * Material.Metallic;
	PBR.Roughness = metalRough.g * Material.Roughness;
	PBR.Roughness = max(PBR.Roughness, 0.05f);

	PBR.Normal = normalize(In.Normal);
	if (Material.HasNormal) {
		PBR.Normal = normalize(texture(TexNormal, In.UV0).rgb * 2.0f - 1.0f);
		PBR.Normal = normalize(In.NormalMat * PBR.Normal);
	}

	PBR.View = normalize(Scene.CameraPosition.xyz - In.WorldPos);
	PBR.NdotV = max(dot(PBR.Normal, PBR.View), 0.0);

	vec3 Lr = 2.0 * PBR.NdotV * PBR.Normal - PBR.View;
	vec3 F0 = vec3(0.04f);
	F0 = mix(F0, PBR.Albedo, PBR.Metallic);

	uint cascadeIndex = 0;
	float shadowScale = 1.0f;
#define CAST_SHADOWS
#ifdef CAST_SHADOWS
	if (Scene.CastShadows) {
		for (uint i = 0; i < ShadowCascadeCount - 1; ++i) {
			if (In.ViewPos.z < Scene.CascadeSplits[i]) { cascadeIndex = i + 1; }
		}
		vec3 shadowCoords = In.ShadowCoords[cascadeIndex].xyz / In.ShadowCoords[cascadeIndex].w;
		shadowScale = Scene.SoftShadows ? PCSSDirectional(TexShadowMap, cascadeIndex, shadowCoords, Scene.LightSize) : HardShadowsDirectional(TexShadowMap, cascadeIndex, shadowCoords);
	}
#endif

	vec3 lightContrib = DirectionalLights(F0) * shadowScale;

	outColor = vec4(lightContrib, 1.0f);

	if (Scene.ShowCascades) {
		switch(cascadeIndex) {
			case 0: outColor.rgb *= vec3(1.0f, 0.25f, 0.25f); break;
			case 1: outColor.rgb *= vec3(0.25f, 1.0f, 0.25f); break;
			case 2: outColor.rgb *= vec3(0.25f, 0.25f, 1.0f); break;
			case 3: outColor.rgb *= vec3(1.0f, 1.0f, 0.25f); break;
		}
	}
}
