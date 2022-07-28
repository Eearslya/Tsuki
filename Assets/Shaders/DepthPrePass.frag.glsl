#version 450 core

layout(location = 0) in vec2 inUV0;

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

void main() {
	float alpha = texture(TexAlbedo, inUV0).a * Material.BaseColorFactor.a;
	if (Material.AlphaMode == 1 && alpha < Material.AlphaCutoff) { discard; }
}
