#pragma once

#include <Assets/Material.hpp>
#include <Assets/Texture.hpp>
#include <Scene/Entity.hpp>
#include <Utility/AABB.hpp>
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
	static constexpr int ShadowCascadeCount = 4;

	enum class RenderStage { CascadedShadowMap, DepthPrePass, Lighting };

	struct DirectionalLight {
		glm::vec3 Direction;
		float ShadowAmount;
		glm::vec3 Radiance;
		float Intensity;
	};

	struct SceneData {
		glm::mat4 ViewProjection;
		glm::mat4 View;
		glm::mat4 LightMatrices[ShadowCascadeCount];
		glm::vec4 CascadeSplits;
		glm::vec4 Position;
		DirectionalLight Light;
		float LightSize;
		int CastShadows;
		int SoftShadows;
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

	struct RendererUniforms {
		Luna::Vulkan::BufferHandle Scene;

		SceneData* SceneData = nullptr;
	};

	void BindUniforms(Luna::Vulkan::CommandBufferHandle& cmd, uint32_t frameIndex);
	Luna::AABB GetCameraFrustum(Luna::Entity& cameraEntity);
	void PrepareCascades(Luna::Scene& scene, Luna::Entity& cameraEntity, Luna::Entity& sunEntity, uint32_t frameIndex);
	void RenderMeshes(Luna::Vulkan::CommandBufferHandle& cmd,
	                  Luna::Scene& scene,
	                  Luna::Entity& cameraEntity,
	                  uint32_t frameIndex,
	                  RenderStage stage);
	void SetTexture(Luna::Vulkan::CommandBufferHandle& cmd,
	                uint32_t set,
	                uint32_t binding,
	                const Luna::TextureHandle& texture,
	                const Luna::Vulkan::ImageHandle& fallback = {});
	RendererUniforms& Uniforms(uint32_t frameIndex);

	Luna::Vulkan::WSI& _wsi;
	DefaultImages _defaultImages;
	Luna::MaterialHandle _nullMaterial;
	Luna::Vulkan::Program* _depthPre = nullptr;
	Luna::Vulkan::Program* _program  = nullptr;
	Luna::Vulkan::Program* _shadows  = nullptr;
	bool _drawToSwapchain            = true;
	glm::uvec2 _imageSize            = glm::uvec2(0);
	std::vector<Luna::Vulkan::ImageHandle> _sceneImages;
	Luna::Vulkan::ImageHandle _shadowMap;
	std::vector<Luna::Vulkan::ImageViewHandle> _shadowCascades;
	std::vector<RendererUniforms> _uniforms;

	bool _shadowPCF = false;

	bool _debugCSM         = false;
	bool _debugCSMSplit    = false;
	bool _debugFrustumCull = false;
};
