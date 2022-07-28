#version 450 core

const int ShadowCascadeCount = 4;

struct DirectionalLight {
	vec3 Direction;
	float ShadowAmount;
	vec3 Radiance;
	float Intensity;
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV0;

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
	int ShadowCascade;
} PC;

layout(location = 0) out vec2 outUV0;

void main() {
	outUV0 = inUV0;

	gl_Position = Scene.LightMatrices[PC.ShadowCascade] * PC.Model * vec4(inPosition, 1.0);
}
