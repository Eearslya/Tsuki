#pragma once

#include <Vulkan/Image.hpp>

enum class LightType { Directional = 0, Point = 1, Spot = 2 };

struct LightComponent {
	LightComponent()                      = default;
	LightComponent(const LightComponent&) = default;

	LightType Type = LightType::Directional;

	bool CastShadows             = true;
	uint32_t ShadowMapResolution = 2048;
	float CascadeSplitLambda     = 0.95f;
	float DepthBiasConstant      = 1.25f;
	float DepthBiasScale         = 1.75f;
};
