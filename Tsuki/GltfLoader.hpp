#pragma once

#include <Assets/Mesh.hpp>
#include <Scene/Entity.hpp>
#include <Utility/IntrusivePtr.hpp>
#include <Utility/ObjectPool.hpp>
#include <Vulkan/Common.hpp>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

class GltfLoader {
 public:
	GltfLoader(Luna::Vulkan::WSI& wsi);
	~GltfLoader() noexcept;

	Luna::Entity Load(const std::filesystem::path& meshAssetPath, Luna::Scene& scene);

 private:
	Luna::Vulkan::WSI* _wsi;
};
