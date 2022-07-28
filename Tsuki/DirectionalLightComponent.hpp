#pragma once

#include <glm/glm.hpp>

struct DirectionalLightComponent {
	DirectionalLightComponent()                                 = default;
	DirectionalLightComponent(const DirectionalLightComponent&) = default;

	glm::vec3 Radiance = glm::vec3(1.0f);
	float Intensity    = 1.0f;
	float LightSize    = 0.5f;
	float ShadowAmount = 1.0f;
	bool CastShadows   = true;
	bool SoftShadows   = true;
};
