#version 450 core

const float Epsilon = 0.00001;
const float Pi = 3.141592;
const int ShadowCascadeCount = 4;
const float TwoPi = 2 * Pi;

const vec2 PoissonDistribution[64] = vec2[](
	vec2(-0.884081, 0.124488),
	vec2(-0.714377, 0.027940),
	vec2(-0.747945, 0.227922),
	vec2(-0.939609, 0.243634),
	vec2(-0.985465, 0.045534),
	vec2(-0.861367, -0.136222),
	vec2(-0.881934, 0.396908),
	vec2(-0.466938, 0.014526),
	vec2(-0.558207, 0.212662),
	vec2(-0.578447, -0.095822),
	vec2(-0.740266, -0.095631),
	vec2(-0.751681, 0.472604),
	vec2(-0.553147, -0.243177),
	vec2(-0.674762, -0.330730),
	vec2(-0.402765, -0.122087),
	vec2(-0.319776, -0.312166),
	vec2(-0.413923, -0.439757),
	vec2(-0.979153, -0.201245),
	vec2(-0.865579, -0.288695),
	vec2(-0.243704, -0.186378),
	vec2(-0.294920, -0.055748),
	vec2(-0.604452, -0.544251),
	vec2(-0.418056, -0.587679),
	vec2(-0.549156, -0.415877),
	vec2(-0.238080, -0.611761),
	vec2(-0.267004, -0.459702),
	vec2(-0.100006, -0.229116),
	vec2(-0.101928, -0.380382),
	vec2(-0.681467, -0.700773),
	vec2(-0.763488, -0.543386),
	vec2(-0.549030, -0.750749),
	vec2(-0.809045, -0.408738),
	vec2(-0.388134, -0.773448),
	vec2(-0.429392, -0.894892),
	vec2(-0.131597, 0.065058),
	vec2(-0.275002, 0.102922),
	vec2(-0.106117, -0.068327),
	vec2(-0.294586, -0.891515),
	vec2(-0.629418, 0.379387),
	vec2(-0.407257, 0.339748),
	vec2(0.071650, -0.384284),
	vec2(0.022018, -0.263793),
	vec2(0.003879, -0.136073),
	vec2(-0.137533, -0.767844),
	vec2(-0.050874, -0.906068),
	vec2(0.114133, -0.070053),
	vec2(0.163314, -0.217231),
	vec2(-0.100262, -0.587992),
	vec2(-0.004942, 0.125368),
	vec2(0.035302, -0.619310),
	vec2(0.195646, -0.459022),
	vec2(0.303969, -0.346362),
	vec2(-0.678118, 0.685099),
	vec2(-0.628418, 0.507978),
	vec2(-0.508473, 0.458753),
	vec2(0.032134, -0.782030),
	vec2(0.122595, 0.280353),
	vec2(-0.043643, 0.312119),
	vec2(0.132993, 0.085170),
	vec2(-0.192106, 0.285848),
	vec2(0.183621, -0.713242),
	vec2(0.265220, -0.596716),
	vec2(-0.009628, -0.483058),
	vec2(-0.018516, 0.435703)
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
	float ShadowFade;
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
	return PoissonDistribution[index % 64];
}

float PCFDirectional(sampler2DArray shadowMap, uint cascade, vec3 shadowCoords, float uvRadius) {
	float bias = GetShadowBias();

	int samples = 64;
	float sum = 0;
	for (int i = 0; i < samples; ++i) {
		vec2 offset = SamplePoisson(i) * uvRadius;
		float z = textureLod(shadowMap, vec3((shadowCoords.st) + offset, cascade), 0).r;
		sum += step(shadowCoords.z - bias, z);
	}

	return sum / float(samples);
}

float PCSSDirectional(sampler2DArray shadowMap, uint cascade, vec3 shadowCoords, float uvLightSize) {
	float bias = GetShadowBias();

	// Blocker Search Radius UV
	// const float lightZNear = 0.0f;
	// const float lightRadiusUV = 0.05f;
	// float searchWidth = lightRadiusUV * (shadowCoords.z - lightZNear) / shadowCoords.z;
	float searchWidth = 0.05f;

	// Blocker Search
	int blockers = 0;
	float blockerDistance = 0.0f;
	{
		int blockerSearchSamples = 64;

		for (int i = 0; i < blockerSearchSamples; ++i) {
			float z = textureLod(shadowMap, vec3((shadowCoords.st) + SamplePoisson(i) * searchWidth, cascade), 0).r;
			if (z < (shadowCoords.z - bias)) {
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

	//return blockerDistance;
	return PCFDirectional(shadowMap, cascade, shadowCoords, uvRadius) * PBR.ShadowFade;
}

float HardShadowsDirectional(sampler2DArray shadowMap, uint cascade, vec3 shadowCoords) {
	float bias = GetShadowBias();
	float shadowMapDepth = texture(shadowMap, vec3(shadowCoords.st, cascade)).r;
	return step(shadowCoords.z, shadowMapDepth + bias) * PBR.ShadowFade;
}

void main() {
	vec4 baseColor = texture(TexAlbedo, In.UV0) * Material.BaseColorFactor;
	PBR.Albedo = baseColor.rgb;
	float alpha = baseColor.a;

	if (Material.AlphaMode == 1 && alpha < Material.AlphaCutoff) { discard; }

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
	if (Scene.CastShadows) {
		for (uint i = 0; i < ShadowCascadeCount - 1; ++i) {
			if (In.ViewPos.z < Scene.CascadeSplits[i]) { cascadeIndex = i + 1; }
		}
		float shadowDistance = 200.0f;
		float transitionDistance = 1.0f;
		float distance = length(In.ViewPos);
		PBR.ShadowFade = distance - (shadowDistance - transitionDistance);
		PBR.ShadowFade /= transitionDistance;
		PBR.ShadowFade = clamp(1.0 - PBR.ShadowFade, 0.0, 1.0);
		bool fadeCascades = false;
		if (fadeCascades) {
			float cascadeTransitionFade = 1.0f;

			float c0 = smoothstep(Scene.CascadeSplits[0] + cascadeTransitionFade * 0.5f, Scene.CascadeSplits[0] - cascadeTransitionFade * 0.5f, In.ViewPos.z);
			float c1 = smoothstep(Scene.CascadeSplits[1] + cascadeTransitionFade * 0.5f, Scene.CascadeSplits[1] - cascadeTransitionFade * 0.5f, In.ViewPos.z);
			float c2 = smoothstep(Scene.CascadeSplits[2] + cascadeTransitionFade * 0.5f, Scene.CascadeSplits[2] - cascadeTransitionFade * 0.5f, In.ViewPos.z);

			if (c0 > 0.0 && c0 < 1.0) {
			}
		} else {
			vec3 shadowCoords = In.ShadowCoords[cascadeIndex].xyz / In.ShadowCoords[cascadeIndex].w;
			shadowScale = Scene.SoftShadows ? PCSSDirectional(TexShadowMap, cascadeIndex, shadowCoords, Scene.LightSize) : HardShadowsDirectional(TexShadowMap, cascadeIndex, shadowCoords);
		}
	}

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
