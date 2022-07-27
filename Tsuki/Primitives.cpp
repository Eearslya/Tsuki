#include "Primitives.hpp"

#include <Vulkan/Device.hpp>

namespace Primitives {
Luna::IntrusivePtr<Luna::Mesh> Plane(Luna::Vulkan::Device& device) {
	Luna::Mesh* mesh = new Luna::Mesh();

	const glm::vec3 positions[] = {glm::vec3(-0.5f, 0.0f, -0.5f),
	                               glm::vec3(-0.5f, 0.0f, 0.5f),
	                               glm::vec3(0.5f, 0.0f, 0.5f),
	                               glm::vec3(0.5f, 0.0f, -0.5f)};
	const glm::vec3 normals[]   = {glm::vec3(0, 1, 0), glm::vec3(0, 1, 0), glm::vec3(0, 1, 0), glm::vec3(0, 1, 0)};
	const glm::vec2 uvs[]       = {glm::vec2(0, 1), glm::vec2(0, 0), glm::vec2(1, 1), glm::vec2(1, 0)};
	const uint32_t indices[]    = {0, 1, 2, 2, 3, 0};

	mesh->Submeshes.resize(1);
	mesh->Submeshes[0].VertexCount   = 4;
	mesh->Submeshes[0].IndexCount    = 6;
	mesh->Submeshes[0].FirstVertex   = 0;
	mesh->Submeshes[0].FirstIndex    = 0;
	mesh->Submeshes[0].MaterialIndex = 0;

	const size_t bufferSize = sizeof(positions) + sizeof(normals) + sizeof(uvs) + sizeof(indices);
	std::vector<uint8_t> buffer(bufferSize);
	mesh->PositionOffset  = 0;
	mesh->NormalOffset    = sizeof(positions);
	mesh->Texcoord0Offset = sizeof(positions) + sizeof(normals);
	mesh->IndexOffset     = sizeof(positions) + sizeof(normals) + sizeof(uvs);
	memcpy(buffer.data() + mesh->PositionOffset, positions, sizeof(positions));
	memcpy(buffer.data() + mesh->NormalOffset, normals, sizeof(normals));
	memcpy(buffer.data() + mesh->Texcoord0Offset, uvs, sizeof(uvs));
	memcpy(buffer.data() + mesh->IndexOffset, indices, sizeof(indices));

	mesh->Buffer = device.CreateBuffer(
		Luna::Vulkan::BufferCreateInfo(Luna::Vulkan::BufferDomain::Device,
	                                 bufferSize,
	                                 vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer),
		buffer.data());

	return Luna::IntrusivePtr<Luna::Mesh>(mesh);
}
}  // namespace Primitives
