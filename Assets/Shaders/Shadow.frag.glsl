#version 450 core

layout(location = 0) in vec2 inUV0;

layout(set = 1, binding = 0) uniform MaterialData {
	vec4 BaseColorFactor;
	vec4 EmissiveFactor;
	bool HasAlbedo;
	bool HasNormal;
	bool HasPBR;
	bool HasEmissive;
	bool AlphaMode;
	float AlphaCutoff;
	float Metallic;
	float Roughness;
} Material;
layout(set = 1, binding = 1) uniform sampler2D TexAlbedo;

void main() {}
