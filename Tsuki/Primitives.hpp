#pragma once

#include <Assets/Mesh.hpp>
#include <glm/glm.hpp>

namespace Primitives {
Luna::IntrusivePtr<Luna::Mesh> Plane(Luna::Vulkan::Device& device);
}
