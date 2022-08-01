#pragma once

#include <Scene/Entity.hpp>
#include <Vulkan/Common.hpp>
#include <filesystem>

class HdriLoader {
 public:
	HdriLoader(Luna::Vulkan::WSI& wsi);
	~HdriLoader() noexcept;

	Luna::Entity Load(const std::filesystem::path& hdriPath, Luna::Scene& scene);

 private:
	Luna::Vulkan::WSI* _wsi;

	Luna::Vulkan::Program* _cubemapProgram = nullptr;
};
