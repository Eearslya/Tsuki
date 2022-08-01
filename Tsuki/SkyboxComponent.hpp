#pragma once

#include <Vulkan/Common.hpp>

struct SkyboxComponent {
	SkyboxComponent()                       = default;
	SkyboxComponent(const SkyboxComponent&) = default;

	Luna::Vulkan::ImageHandle Skybox;
};
