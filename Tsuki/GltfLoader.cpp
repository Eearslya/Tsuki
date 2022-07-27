#include "GltfLoader.hpp"

#include <stb_image.h>
#include <tiny_gltf.h>

#include <Scene/Entity.hpp>
#include <Scene/MeshComponent.hpp>
#include <Scene/Scene.hpp>
#include <Scene/TransformComponent.hpp>
#include <Utility/Files.hpp>
#include <Utility/Log.hpp>
#include <Vulkan/Buffer.hpp>
#include <Vulkan/Device.hpp>
#include <Vulkan/Image.hpp>
#include <Vulkan/WSI.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

using namespace Luna;

GltfLoader::GltfLoader(Luna::Vulkan::WSI& wsi) {
	_wsi = &wsi;
}

GltfLoader::~GltfLoader() noexcept {
	_wsi = nullptr;
}

Entity GltfLoader::Load(const std::filesystem::path& meshAssetPath, Scene& scene) {
	const auto gltfPath      = meshAssetPath;
	const auto gltfFile      = gltfPath.string();
	const auto gltfFolder    = gltfPath.parent_path().string();
	const auto gltfFileName  = gltfPath.filename().string();
	const auto gltfFileNameC = gltfFileName.c_str();

	tinygltf::Model gltfModel;
	tinygltf::TinyGLTF loader;
	std::string gltfError;
	std::string gltfWarning;
	bool loaded;
	const auto gltfExt = gltfPath.extension().string();
	if (gltfExt == ".gltf") {
		loaded = loader.LoadASCIIFromFile(&gltfModel, &gltfError, &gltfWarning, gltfFile);
	} else if (gltfExt == ".glb") {
		loaded = loader.LoadBinaryFromFile(&gltfModel, &gltfError, &gltfWarning, gltfFile);
	} else {
		Log::Error("GltfLoader", "Mesh asset file {} is not supported!", gltfFile);
		return {};
	}

	if (!gltfError.empty()) { Log::Error("GltfLoader", "Error loading mesh asset {}: {}", gltfFile, gltfError); }
	if (!gltfWarning.empty()) { Log::Warning("GltfLoader", "Warning loading mesh asset {}: {}", gltfFile, gltfWarning); }
	if (!loaded) {
		Log::Error("GltfLoader", "Failed to load mesh asset file {}.", gltfFile);
		return {};
	}

	// Quickly iterate over materials to find what format each image should be, Srgb or Unorm.
	std::vector<vk::Format> textureFormats(gltfModel.images.size(), vk::Format::eUndefined);
	const auto EnsureFormat = [&](uint32_t index, vk::Format expected) -> void {
		auto& format = textureFormats[index];
		if (format != vk::Format::eUndefined && format != expected) {
			Log::Error(
				"GltfLoader", "For asset '{}', texture index {} is used in both Srgb and Unorm contexts!", gltfFile, index);
		}
		format = expected;
	};
	for (size_t i = 0; i < gltfModel.materials.size(); ++i) {
		const auto& gltfMaterial = gltfModel.materials[i];

		if (gltfMaterial.pbrMetallicRoughness.baseColorTexture.index >= 0) {
			EnsureFormat(gltfMaterial.pbrMetallicRoughness.baseColorTexture.index, vk::Format::eR8G8B8A8Srgb);
		}
		if (gltfMaterial.normalTexture.index >= 0) {
			EnsureFormat(gltfMaterial.normalTexture.index, vk::Format::eR8G8B8A8Unorm);
		}
		if (gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) {
			EnsureFormat(gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index, vk::Format::eR8G8B8A8Unorm);
		}
		if (gltfMaterial.emissiveTexture.index >= 0) {
			EnsureFormat(gltfMaterial.emissiveTexture.index, vk::Format::eR8G8B8A8Srgb);
		}
	}

	std::vector<Vulkan::ImageHandle> images;
	for (size_t i = 0; i < gltfModel.images.size(); ++i) {
		const auto& gltfImage = gltfModel.images[i];

		if (textureFormats[i] == vk::Format::eUndefined) {
			// Image is unused in any materials.
			images.push_back({});
			continue;
		}

		bool loaded = false;
		std::vector<uint8_t> bytes;
		const auto uri = gltfImage.uri;
		if (!uri.empty()) {
			const std::filesystem::path imagePath = std::filesystem::path(gltfFolder) / uri;
			const std::string imagePathStr        = imagePath.string();
			const char* imagePathC                = imagePathStr.c_str();
			try {
				bytes = ReadFileBinary(imagePath);
			} catch (const std::exception& e) {
				Log::Error("GltfLoader", "Failed to load texture for {}, {}\n\t{}", gltfFile, uri, e.what());
				images.push_back({});
				continue;
			}

			loaded = true;
		}
		const int bufferView = gltfImage.bufferView;
		if (bufferView >= 0) {
			const tinygltf::BufferView& gltfBufferView = gltfModel.bufferViews[bufferView];
			const tinygltf::Buffer& gltfBuffer         = gltfModel.buffers[gltfBufferView.buffer];
			const uint8_t* data                        = gltfBuffer.data.data() + gltfBufferView.byteOffset;
			const size_t dataSize                      = gltfBufferView.byteLength;
			bytes.resize(dataSize);
			memcpy(bytes.data(), data, dataSize);

			loaded = true;
		}
		if (!loaded) {
			Log::Error("GltfLoader", "Failed to find data source for texture for {}, image '{}'!", gltfFile, gltfImage.name);
			images.push_back({});
			continue;
		}

		int width, height, components;
		stbi_uc* pixels = stbi_load_from_memory(bytes.data(), bytes.size(), &width, &height, &components, STBI_rgb_alpha);
		if (pixels == nullptr) {
			Log::Error("AssetManager", "Failed to read texture data for {}, {}: {}", gltfFile, uri, stbi_failure_reason());
			images.push_back({});
			continue;
		}

		const Vulkan::ImageInitialData initialData{.Data = pixels};
		const auto imageCI = Vulkan::ImageCreateInfo::Immutable2D(width, height, textureFormats[i], true);
		images.push_back(_wsi->GetDevice().CreateImage(imageCI, &initialData));
	}

	std::vector<Vulkan::Sampler*> samplers;
	for (size_t i = 0; i < gltfModel.samplers.size(); ++i) {
		const auto& gltfSampler = gltfModel.samplers[i];

		const auto& gpuInfo    = _wsi->GetDevice().GetGPUInfo();
		const float anisotropy = gpuInfo.EnabledFeatures.Features.samplerAnisotropy
		                           ? gpuInfo.Properties.Properties.limits.maxSamplerAnisotropy
		                           : 0.0f;

		Vulkan::SamplerCreateInfo samplerCI{
			.AnisotropyEnable = anisotropy > 0.0f, .MaxAnisotropy = anisotropy, .MaxLod = 11.0f};
		switch (gltfSampler.magFilter) {
			case 9728:  // NEAREST
				samplerCI.MagFilter = vk::Filter::eNearest;
				break;
			case 9729:  // LINEAR
				samplerCI.MagFilter = vk::Filter::eLinear;
				break;
		}
		switch (gltfSampler.minFilter) {
			case 9728:  // NEAREST
				samplerCI.MinFilter = vk::Filter::eNearest;
				break;
			case 9729:  // LINEAR
				samplerCI.MinFilter = vk::Filter::eLinear;
				break;
			case 9984:  // NEAREST_MIPMAP_NEAREST
				samplerCI.MinFilter  = vk::Filter::eNearest;
				samplerCI.MipmapMode = vk::SamplerMipmapMode::eNearest;
				break;
			case 9985:  // LINEAR_MIPMAP_NEAREST
				samplerCI.MinFilter  = vk::Filter::eLinear;
				samplerCI.MipmapMode = vk::SamplerMipmapMode::eNearest;
				break;
			case 9986:  // NEAREST_MIPMAP_LINEAR
				samplerCI.MinFilter  = vk::Filter::eNearest;
				samplerCI.MipmapMode = vk::SamplerMipmapMode::eLinear;
				break;
			case 9987:  // LINEAR_MIPMAP_LINEAR
				samplerCI.MinFilter  = vk::Filter::eLinear;
				samplerCI.MipmapMode = vk::SamplerMipmapMode::eLinear;
				break;
		}
		switch (gltfSampler.wrapS) {
			case 33071:  // CLAMP_TO_EDGE
				samplerCI.AddressModeU = vk::SamplerAddressMode::eClampToEdge;
				break;
			case 33648:  // MIRRORED_REPEAT
				samplerCI.AddressModeU = vk::SamplerAddressMode::eMirroredRepeat;
				break;
			case 10497:  // REPEAT
				samplerCI.AddressModeU = vk::SamplerAddressMode::eRepeat;
				break;
		}
		switch (gltfSampler.wrapT) {
			case 33071:  // CLAMP_TO_EDGE
				samplerCI.AddressModeV = vk::SamplerAddressMode::eClampToEdge;
				break;
			case 33648:  // MIRRORED_REPEAT
				samplerCI.AddressModeV = vk::SamplerAddressMode::eMirroredRepeat;
				break;
			case 10497:  // REPEAT
				samplerCI.AddressModeV = vk::SamplerAddressMode::eRepeat;
				break;
		}
		samplers.push_back(_wsi->GetDevice().RequestSampler(samplerCI));
	}

	std::vector<TextureHandle> textures;
	for (size_t i = 0; i < gltfModel.textures.size(); ++i) {
		const auto& gltfTexture = gltfModel.textures[i];

		auto& image              = images[gltfTexture.source];
		Vulkan::Sampler* sampler = gltfTexture.sampler >= 0
		                             ? samplers[gltfTexture.sampler]
		                             : _wsi->GetDevice().RequestSampler(Vulkan::StockSampler::DefaultGeometryFilterClamp);
		auto handle              = TextureHandle(new Texture());
		handle->Image            = image;
		handle->Sampler          = sampler;
		textures.push_back(handle);
	}

	std::vector<MaterialHandle> materials;
	for (size_t i = 0; i < gltfModel.materials.size(); ++i) {
		const auto& gltfMaterial = gltfModel.materials[i];
		Material* material       = new Material();

		material->DualSided = gltfMaterial.doubleSided;
		if (gltfMaterial.pbrMetallicRoughness.baseColorFactor.size() == 4) {
			material->BaseColorFactor = glm::make_vec4(gltfMaterial.pbrMetallicRoughness.baseColorFactor.data());
		}
		if (gltfMaterial.emissiveFactor.size() == 3) {
			material->EmissiveFactor = glm::make_vec3(gltfMaterial.emissiveFactor.data());
		}
		if (gltfMaterial.alphaMode.compare("OPAQUE") == 0) {
			material->Alpha = AlphaMode::Opaque;
		} else if (gltfMaterial.alphaMode.compare("MASK") == 0) {
			material->Alpha = AlphaMode::Mask;
		} else if (gltfMaterial.alphaMode.compare("BLEND") == 0) {
			material->Alpha = AlphaMode::Blend;
		}
		material->AlphaCutoff     = gltfMaterial.alphaCutoff;
		material->MetallicFactor  = gltfMaterial.pbrMetallicRoughness.metallicFactor;
		material->RoughnessFactor = gltfMaterial.pbrMetallicRoughness.roughnessFactor;

		if (gltfMaterial.pbrMetallicRoughness.baseColorTexture.index >= 0) {
			material->Albedo = textures[gltfMaterial.pbrMetallicRoughness.baseColorTexture.index];
		}
		if (gltfMaterial.normalTexture.index >= 0) { material->Normal = textures[gltfMaterial.normalTexture.index]; }
		if (gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) {
			material->PBR = textures[gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index];
		}
		if (gltfMaterial.emissiveTexture.index >= 0) { material->Emissive = textures[gltfMaterial.emissiveTexture.index]; }

		materials.emplace_back(material);
	}

	std::vector<IntrusivePtr<Mesh>> meshes;
	for (size_t i = 0; i < gltfModel.meshes.size(); ++i) {
		const auto& gltfMesh = gltfModel.meshes[i];
		Mesh mesh;

		struct PrimitiveContext {
			AABB Bounds                = {};
			uint64_t VertexCount       = 0;
			uint64_t IndexCount        = 0;
			vk::DeviceSize FirstVertex = 0;
			vk::DeviceSize FirstIndex  = 0;
			int IndexStride            = 0;
			int MaterialIndex          = 0;
			const void* PositionData   = nullptr;
			const void* NormalData     = nullptr;
			const void* Texcoord0Data  = nullptr;
			const void* IndexData      = nullptr;
		};

		vk::DeviceSize totalVertexCount = 0;
		vk::DeviceSize totalIndexCount  = 0;
		std::vector<PrimitiveContext> primData(gltfMesh.primitives.size());
		{
			mesh.Submeshes.resize(gltfMesh.primitives.size());
			for (size_t prim = 0; prim < gltfMesh.primitives.size(); ++prim) {
				const auto& gltfPrimitive = gltfMesh.primitives[prim];
				if (gltfPrimitive.mode != 4) {
					Log::Warning("GltfLoader",
					             "{} mesh {} contains a primitive with mode {}. Only mode 4 (triangle list) is supported.",
					             gltfFile,
					             i,
					             gltfPrimitive.mode);
					continue;
				}

				auto& data         = primData[prim];
				data.MaterialIndex = gltfPrimitive.material;

				for (const auto [attributeName, attributeId] : gltfPrimitive.attributes) {
					const auto& gltfAccessor   = gltfModel.accessors[attributeId];
					const auto& gltfBufferView = gltfModel.bufferViews[gltfAccessor.bufferView];
					const auto& gltfBuffer     = gltfModel.buffers[gltfBufferView.buffer];
					const void* bufferData     = gltfBuffer.data.data() + gltfAccessor.byteOffset + gltfBufferView.byteOffset;

					if (attributeName.compare("POSITION") == 0) {
						data.Bounds =
							AABB(glm::make_vec3(gltfAccessor.minValues.data()), glm::make_vec3(gltfAccessor.maxValues.data()));
						mesh.Bounds.Contain(data.Bounds);
						data.VertexCount  = gltfAccessor.count;
						data.PositionData = bufferData;
					} else if (attributeName.compare("NORMAL") == 0) {
						data.NormalData = bufferData;
					} else if (attributeName.compare("TEXCOORD_0") == 0) {
						data.Texcoord0Data = bufferData;
					}
				}

				if (gltfPrimitive.indices >= 0) {
					const auto& gltfAccessor   = gltfModel.accessors[gltfPrimitive.indices];
					const auto& gltfBufferView = gltfModel.bufferViews[gltfAccessor.bufferView];
					const auto& gltfBuffer     = gltfModel.buffers[gltfBufferView.buffer];
					const void* bufferData     = gltfBuffer.data.data() + gltfAccessor.byteOffset + gltfBufferView.byteOffset;
					const auto bufferStride    = gltfAccessor.ByteStride(gltfBufferView);

					data.IndexCount  = gltfAccessor.count;
					data.IndexData   = bufferData;
					data.IndexStride = bufferStride;
				}

				data.FirstVertex = totalVertexCount;
				data.FirstIndex  = totalIndexCount;
				totalVertexCount += data.VertexCount;
				totalIndexCount += data.IndexCount;
			}
		}

		const vk::DeviceSize totalPositionSize  = ((totalVertexCount * sizeof(glm::vec3)) + 16llu) & ~16llu;
		const vk::DeviceSize totalNormalSize    = ((totalVertexCount * sizeof(glm::vec3)) + 16llu) & ~16llu;
		const vk::DeviceSize totalTexcoord0Size = ((totalVertexCount * sizeof(glm::vec2)) + 16llu) & ~16llu;
		const vk::DeviceSize totalIndexSize     = ((totalIndexCount * sizeof(uint32_t)) + 16llu) & ~16llu;
		const vk::DeviceSize bufferSize         = totalPositionSize + totalNormalSize + totalTexcoord0Size + totalIndexSize;

		mesh.PositionOffset   = 0;
		mesh.NormalOffset     = totalPositionSize;
		mesh.Texcoord0Offset  = totalPositionSize + totalNormalSize;
		mesh.IndexOffset      = totalPositionSize + totalNormalSize + totalTexcoord0Size;
		mesh.TotalVertexCount = totalVertexCount;
		mesh.TotalIndexCount  = totalIndexCount;

		std::unique_ptr<uint8_t[]> bufferData;
		bufferData.reset(new uint8_t[bufferSize]);
		uint8_t* positionCursor  = bufferData.get();
		uint8_t* normalCursor    = bufferData.get() + totalPositionSize;
		uint8_t* texcoord0Cursor = bufferData.get() + totalPositionSize + totalNormalSize;
		uint8_t* indexCursor     = bufferData.get() + totalPositionSize + totalNormalSize + totalTexcoord0Size;

		{
			for (size_t prim = 0; prim < gltfMesh.primitives.size(); ++prim) {
				const auto& data = primData[prim];
				auto& submesh    = mesh.Submeshes[prim];

				submesh.VertexCount   = data.VertexCount;
				submesh.IndexCount    = data.IndexCount;
				submesh.FirstVertex   = data.FirstVertex;
				submesh.FirstIndex    = data.FirstIndex;
				submesh.MaterialIndex = data.MaterialIndex;

				const size_t positionSize  = data.VertexCount * sizeof(glm::vec3);
				const size_t normalSize    = data.VertexCount * sizeof(glm::vec3);
				const size_t texcoord0Size = data.VertexCount * sizeof(glm::vec2);
				const size_t indexSize     = data.IndexCount * sizeof(uint32_t);

				memcpy(positionCursor, data.PositionData, positionSize);
				positionCursor += positionSize;

				if (data.NormalData) {
					memcpy(normalCursor, data.NormalData, normalSize);
				} else {
					memset(normalCursor, 0, normalSize);
				}
				normalCursor += normalSize;

				if (data.Texcoord0Data) {
					memcpy(texcoord0Cursor, data.Texcoord0Data, texcoord0Size);
				} else {
					memset(texcoord0Cursor, 0, texcoord0Size);
				}
				texcoord0Cursor += texcoord0Size;

				if (data.IndexData) {
					if (data.IndexStride == 1) {
						uint32_t* dst      = reinterpret_cast<uint32_t*>(indexCursor);
						const uint8_t* src = reinterpret_cast<const uint8_t*>(data.IndexData);
						for (size_t i = 0; i < data.IndexCount; ++i) { dst[i] = src[i]; }
					} else if (data.IndexStride == 2) {
						uint32_t* dst       = reinterpret_cast<uint32_t*>(indexCursor);
						const uint16_t* src = reinterpret_cast<const uint16_t*>(data.IndexData);
						for (size_t i = 0; i < data.IndexCount; ++i) { dst[i] = src[i]; }
					} else if (data.IndexStride == 4) {
						memcpy(indexCursor, data.IndexData, indexSize);
					}
				} else {
					memset(indexCursor, 0, indexSize);
				}
				indexCursor += indexSize;
			}
		}

		auto& device = _wsi->GetDevice();
		mesh.Buffer  = device.CreateBuffer(
      Vulkan::BufferCreateInfo(Vulkan::BufferDomain::Device,
                               bufferSize,
                               vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer),
      bufferData.get());

		meshes.emplace_back(new Mesh(mesh));
	}

	auto rootNode = scene.CreateEntity(gltfFileName);

	std::function<void(const tinygltf::Node&, Luna::Entity&)> AddNode = [&](const tinygltf::Node& gltfNode,
	                                                                        Luna::Entity& parentEntity) {
		auto entity     = scene.CreateChildEntity(parentEntity, gltfNode.name);
		auto& transform = entity.Transform();

		if (gltfNode.matrix.size() > 0) {
			const glm::mat4 matrix = glm::make_mat4(gltfNode.matrix.data());
			glm::vec3 scale;
			glm::quat rotation;
			glm::vec3 translation;
			glm::vec3 skew;
			glm::vec4 perspective;
			glm::decompose(matrix, scale, rotation, translation, skew, perspective);
			transform.Translation = translation;
			transform.Rotation    = glm::degrees(glm::eulerAngles(rotation));
			transform.Scale       = scale;
		} else {
			if (gltfNode.translation.size() > 0) { transform.Translation = glm::make_vec3(gltfNode.translation.data()); }
			if (gltfNode.rotation.size() > 0) {
				transform.Rotation = glm::degrees(glm::eulerAngles(
					glm::quat(gltfNode.rotation[3], gltfNode.rotation[0], gltfNode.rotation[1], gltfNode.rotation[2])));
			}
			if (gltfNode.scale.size() > 0) { transform.Scale = glm::make_vec3(gltfNode.scale.data()); }
		}

		if (gltfNode.mesh >= 0) {
			auto& cMesh     = entity.AddComponent<MeshComponent>();
			cMesh.Bounds    = meshes[gltfNode.mesh]->Bounds;
			cMesh.Mesh      = meshes[gltfNode.mesh];
			cMesh.Materials = materials;
		}

		for (auto gltfNodeIndex : gltfNode.children) { AddNode(gltfModel.nodes[gltfNodeIndex], entity); }
	};
	auto& gltfScene = gltfModel.scenes[gltfModel.defaultScene];
	for (auto gltfNodeIndex : gltfScene.nodes) {
		auto& gltfNode = gltfModel.nodes[gltfNodeIndex];
		AddNode(gltfNode, rootNode);
	}

	return rootNode;
}
