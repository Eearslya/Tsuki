#version 450 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV0;

layout(set = 0, binding = 0) uniform SceneData {
	mat4 Projection;
	mat4 View;
	mat4[4] SunMatrices;
} Scene;

layout(push_constant) uniform PushConstant {
	mat4 Model;
	int ShadowCascade;
} PC;

layout(location = 0) out vec2 outUV0;

void main() {
	outUV0 = inUV0;

	gl_Position = Scene.SunMatrices[PC.ShadowCascade] * PC.Model * vec4(inPosition, 1.0);
}
