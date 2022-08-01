#version 450 core

const vec3 CubePositions[36] = vec3[36](
	vec3(-1.0f, -1.0f, -1.0f),
	vec3( 1.0f,  1.0f, -1.0f),
	vec3( 1.0f, -1.0f, -1.0f),
	vec3( 1.0f,  1.0f, -1.0f),
	vec3(-1.0f, -1.0f, -1.0f),
	vec3(-1.0f,  1.0f, -1.0f),
	vec3(-1.0f, -1.0f,  1.0f),
	vec3( 1.0f, -1.0f,  1.0f),
	vec3( 1.0f,  1.0f,  1.0f),
	vec3( 1.0f,  1.0f,  1.0f),
	vec3(-1.0f,  1.0f,  1.0f),
	vec3(-1.0f, -1.0f,  1.0f),
	vec3(-1.0f,  1.0f,  1.0f),
	vec3(-1.0f,  1.0f, -1.0f),
	vec3(-1.0f, -1.0f, -1.0f),
	vec3(-1.0f, -1.0f, -1.0f),
	vec3(-1.0f, -1.0f,  1.0f),
	vec3(-1.0f,  1.0f,  1.0f),
	vec3( 1.0f,  1.0f,  1.0f),
	vec3( 1.0f, -1.0f, -1.0f),
	vec3( 1.0f,  1.0f, -1.0f),
	vec3( 1.0f, -1.0f, -1.0f),
	vec3( 1.0f,  1.0f,  1.0f),
	vec3( 1.0f, -1.0f,  1.0f),
	vec3(-1.0f, -1.0f, -1.0f),
	vec3( 1.0f, -1.0f, -1.0f),
	vec3( 1.0f, -1.0f,  1.0f),
	vec3( 1.0f, -1.0f,  1.0f),
	vec3(-1.0f, -1.0f,  1.0f),
	vec3(-1.0f, -1.0f, -1.0f),
	vec3(-1.0f,  1.0f, -1.0f),
	vec3( 1.0f,  1.0f , 1.0f),
	vec3( 1.0f,  1.0f, -1.0f),
	vec3( 1.0f,  1.0f,  1.0f),
	vec3(-1.0f,  1.0f, -1.0f),
	vec3(-1.0f,  1.0f,  1.0f)
);

layout(set = 0, binding = 0) uniform SceneData {
	mat4 ViewProjection;
	mat4 View;
	mat4 Projection;
} Scene;

layout(location = 0) out vec3 outLocalPos;

void main() {
	const vec3 position = CubePositions[gl_VertexIndex];
	outLocalPos = position;

	const mat4 rotView = mat4(mat3(Scene.View));
	const vec4 clipPos = Scene.Projection * rotView * vec4(position, 1.0);
	gl_Position = clipPos.xyww;
}
