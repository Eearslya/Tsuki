#pragma once

#include <Assets/Material.hpp>
#include <Assets/Texture.hpp>
#include <Scene/Entity.hpp>
#include <Vulkan/Common.hpp>
#include <glm/glm.hpp>

namespace Luna {
class Scene;
}

class SceneRenderer {
 public:
	SceneRenderer(Luna::Vulkan::WSI& wsi);
	~SceneRenderer() noexcept;

	Luna::Vulkan::ImageHandle& GetImage(uint32_t frameIndex);
	void ReloadShaders();
	void Render(Luna::Vulkan::CommandBufferHandle& cmd, Luna::Scene& scene, uint32_t frameIndex);
	void SetDrawToSwapchain(bool drawToSwapchain);
	void SetImageSize(const glm::uvec2& size);
	void ShowSettings();

 private:
	void PrepareCascades(Luna::Scene& scene, Luna::Entity& cameraEntity, Luna::Entity& sunEntity, uint32_t frameIndex);
	void RenderMeshes(Luna::Vulkan::CommandBufferHandle& cmd,
	                  Luna::Scene& scene,
	                  uint32_t frameIndex,
	                  bool shadows = false);
	void SetTexture(Luna::Vulkan::CommandBufferHandle& cmd,
	                uint32_t set,
	                uint32_t binding,
	                const Luna::TextureHandle& texture,
	                const Luna::Vulkan::ImageHandle& fallback = {});

	struct SceneData {
		glm::mat4 Projection;
		glm::mat4 View;
		glm::mat4 SunMatrices[4];
		glm::vec4 CascadeSplits;
		glm::vec4 CameraPos;
		glm::vec4 SunDirection;
		int ShadowCascadeCount;
		int ShadowPCF;

		int DebugShowCascades;
	};

	struct PushConstant {
		glm::mat4 Model;
	};

	struct DefaultImages {
		Luna::Vulkan::ImageHandle Black2D;
		Luna::Vulkan::ImageHandle Gray2D;
		Luna::Vulkan::ImageHandle Normal2D;
		Luna::Vulkan::ImageHandle White2D;
		Luna::Vulkan::ImageHandle BlackCube;
		Luna::Vulkan::ImageHandle GrayCube;
		Luna::Vulkan::ImageHandle WhiteCube;
		Luna::Vulkan::ImageHandle WhiteCSM;
	};

	Luna::Vulkan::WSI& _wsi;
	DefaultImages _defaultImages;
	Luna::MaterialHandle _nullMaterial;
	Luna::Vulkan::Program* _program = nullptr;
	Luna::Vulkan::Program* _shadows = nullptr;
	bool _drawToSwapchain           = true;
	glm::uvec2 _imageSize           = glm::uvec2(0);
	std::vector<Luna::Vulkan::BufferHandle> _sceneBuffers;
	std::vector<Luna::Vulkan::ImageHandle> _sceneImages;
	Luna::Vulkan::ImageHandle _shadowMap;
	std::vector<Luna::Vulkan::ImageViewHandle> _shadowCascades;

	int _shadowCascadeCount = 1;
	bool _shadowPCF         = false;

	bool _debugCSM      = false;
	bool _debugCSMSplit = false;
};
