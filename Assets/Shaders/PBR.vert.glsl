#version 450 core

const mat4 BiasMat = mat4(
	0.5, 0.0, 0.0, 0.0,
	0.0, 0.5, 0.0, 0.0,
	0.0, 0.0, 1.0, 0.0,
	0.5, 0.5, 0.0, 1.0
);
const int ShadowCascadeCount = 4;

struct DirectionalLight {
	vec3 Direction;
	float ShadowAmount;
	vec3 Radiance;
	float Intensity;
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV0;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;

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

layout(push_constant) uniform PushConstant {
	mat4 Model;
} PC;

struct VertexOut {
	vec3 WorldPos;
	vec3 ViewPos;
	vec3 Normal;
	vec2 UV0;
	mat3 NormalMat;
	vec4 ShadowCoords[ShadowCascadeCount];
};

layout(location = 0) out VertexOut Out;

void main() {
	vec4 locPos;
	locPos = PC.Model * vec4(inPosition, 1.0);
	Out.WorldPos = locPos.xyz / locPos.w;

	Out.Normal = normalize(transpose(inverse(mat3(PC.Model))) * inNormal);
	Out.ViewPos = (Scene.View * locPos).xyz;
	Out.UV0 = inUV0;

	Out.NormalMat = mat3(PC.Model) * mat3(inTangent, inBitangent, inNormal);

	Out.ShadowCoords[0] = Scene.CastShadows ? (BiasMat * Scene.LightMatrices[0]) * vec4(Out.WorldPos, 1.0f) : vec4(0);
	Out.ShadowCoords[1] = Scene.CastShadows ? (BiasMat * Scene.LightMatrices[1]) * vec4(Out.WorldPos, 1.0f) : vec4(0);
	Out.ShadowCoords[2] = Scene.CastShadows ? (BiasMat * Scene.LightMatrices[2]) * vec4(Out.WorldPos, 1.0f) : vec4(0);
	Out.ShadowCoords[3] = Scene.CastShadows ? (BiasMat * Scene.LightMatrices[3]) * vec4(Out.WorldPos, 1.0f) : vec4(0);

	gl_Position = Scene.ViewProjection * vec4(Out.WorldPos, 1.0);
}
