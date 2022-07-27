#include "SceneRenderer.hpp"

#include <imgui.h>

#include <ImGui/ImGuiRenderer.hpp>
#include <Scene/CameraComponent.hpp>
#include <Scene/Entity.hpp>
#include <Scene/MeshComponent.hpp>
#include <Scene/Scene.hpp>
#include <Scene/TransformComponent.hpp>
#include <Utility/Files.hpp>
#include <Utility/Log.hpp>
#include <Vulkan/Buffer.hpp>
#include <Vulkan/CommandBuffer.hpp>
#include <Vulkan/Device.hpp>
#include <Vulkan/Image.hpp>
#include <Vulkan/RenderPass.hpp>
#include <Vulkan/WSI.hpp>

#include "IconsFontAwesome6.h"
#include "LightComponent.hpp"

using namespace Luna;

SceneRenderer::SceneRenderer(Vulkan::WSI& wsi) : _wsi(wsi) {
	ReloadShaders();
	_sceneImages.resize(wsi.GetImageCount());

	// Create placeholder textures.
	{
		// All textures will be 4x4 to allow for minimum texel size.
		constexpr uint32_t width    = 4;
		constexpr uint32_t height   = 4;
		constexpr size_t pixelCount = width * height;
		uint32_t pixels[pixelCount];

		const Vulkan::ImageCreateInfo imageCI2D = {
			.Domain        = Vulkan::ImageDomain::Physical,
			.Format        = vk::Format::eR8G8B8A8Unorm,
			.InitialLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
			.Samples       = vk::SampleCountFlagBits::e1,
			.Type          = vk::ImageType::e2D,
			.Usage         = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eInputAttachment,
			.Width         = width,
			.Height        = height,
			.Depth         = 1,
			.ArrayLayers   = 1,
			.MipLevels     = 1,
		};
		const Vulkan::ImageCreateInfo imageCICSM = {
			.Domain        = Vulkan::ImageDomain::Physical,
			.Format        = _wsi.GetDevice().GetDefaultDepthFormat(),
			.InitialLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
			.Samples       = vk::SampleCountFlagBits::e1,
			.Type          = vk::ImageType::e2D,
			.Usage         = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eDepthStencilAttachment,
			.Width         = width,
			.Height        = height,
			.Depth         = 1,
			.ArrayLayers   = 4,
			.MipLevels     = 1,
			.MiscFlags     = Vulkan::ImageCreateFlagBits::ForceArray,
		};
		const Vulkan::ImageCreateInfo imageCICube = {
			.Domain        = Vulkan::ImageDomain::Physical,
			.Format        = vk::Format::eR8G8B8A8Unorm,
			.InitialLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
			.Samples       = vk::SampleCountFlagBits::e1,
			.Type          = vk::ImageType::e2D,
			.Usage         = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eInputAttachment,
			.Width         = width,
			.Height        = height,
			.Depth         = 1,
			.ArrayLayers   = 6,
			.MipLevels     = 1,
			.Flags         = vk::ImageCreateFlagBits::eCubeCompatible};

		Vulkan::ImageInitialData initialImages[6];
		for (int i = 0; i < 6; ++i) { initialImages[i] = Vulkan::ImageInitialData{.Data = &pixels}; }

		// Black images
		std::fill(pixels, pixels + pixelCount, 0xff000000);
		_defaultImages.Black2D   = _wsi.GetDevice().CreateImage(imageCI2D, initialImages);
		_defaultImages.BlackCube = _wsi.GetDevice().CreateImage(imageCICube, initialImages);

		// Gray images
		std::fill(pixels, pixels + pixelCount, 0xff808080);
		_defaultImages.Gray2D   = _wsi.GetDevice().CreateImage(imageCI2D, initialImages);
		_defaultImages.GrayCube = _wsi.GetDevice().CreateImage(imageCICube, initialImages);

		// Normal images
		std::fill(pixels, pixels + pixelCount, 0xffff8080);
		_defaultImages.Normal2D = _wsi.GetDevice().CreateImage(imageCI2D, initialImages);

		// White images
		std::fill(pixels, pixels + pixelCount, 0xffffffff);
		_defaultImages.White2D   = _wsi.GetDevice().CreateImage(imageCI2D, initialImages);
		_defaultImages.WhiteCube = _wsi.GetDevice().CreateImage(imageCICube, initialImages);
		_defaultImages.WhiteCSM  = _wsi.GetDevice().CreateImage(imageCICSM, initialImages);
	}

	_nullMaterial = MakeHandle<Material>();
}

SceneRenderer::~SceneRenderer() noexcept {}

Luna::Vulkan::ImageHandle& SceneRenderer::GetImage(uint32_t frameIndex) {
	return _sceneImages[frameIndex];
}

void SceneRenderer::ReloadShaders() {
	auto* program =
		_wsi.GetDevice().RequestProgram(ReadFile("Assets/Shaders/PBR.vert.glsl"), ReadFile("Assets/Shaders/PBR.frag.glsl"));
	if (program) { _program = program; }

	auto* shadows = _wsi.GetDevice().RequestProgram(ReadFile("Assets/Shaders/Shadow.vert.glsl"),
	                                                ReadFile("Assets/Shaders/Shadow.frag.glsl"));
	if (shadows) { _shadows = shadows; }
}

void SceneRenderer::Render(Vulkan::CommandBufferHandle& cmd, Luna::Scene& scene, uint32_t frameIndex) {
	if (frameIndex >= _sceneImages.size()) { return; }
	Luna::Vulkan::ImageHandle image;
	if (!_drawToSwapchain && _imageSize != glm::uvec2(0)) {
		image = _sceneImages[frameIndex];
		if (!image) { return; }
	}

	if (_sceneBuffers.size() <= frameIndex || !_sceneBuffers[frameIndex]) {
		Vulkan::BufferCreateInfo bufferCI(
			Vulkan::BufferDomain::Host, sizeof(SceneData), vk::BufferUsageFlagBits::eUniformBuffer);
		_sceneBuffers.push_back(_wsi.GetDevice().CreateBuffer(bufferCI));
	}
	auto& sceneBuffer = _sceneBuffers[frameIndex];
	auto cameraEntity = scene.GetMainCamera();

	// Directional Light Shadow Pass
	Entity sun;
	if (cameraEntity) {
		auto lights = scene.GetRegistry().view<LightComponent>();
		for (auto entityId : lights) {
			Entity entity(entityId, scene);
			auto& cLight = lights.get<LightComponent>(entityId);
			if (cLight.Type == LightType::Directional && cLight.CastShadows) {
				sun = entity;
				break;
			}
		}

		if (sun) {
			auto& cLight = sun.GetComponent<LightComponent>();

			if (!_shadowMap || cLight.ShadowMapResolution != _shadowMap->GetCreateInfo().Width ||
			    _shadowMap->GetCreateInfo().ArrayLayers != _shadowCascadeCount) {
				Vulkan::ImageCreateInfo imageCI = Vulkan::ImageCreateInfo::RenderTarget(
					cLight.ShadowMapResolution, cLight.ShadowMapResolution, _wsi.GetDevice().GetDefaultDepthFormat());
				imageCI.ArrayLayers = _shadowCascadeCount;
				imageCI.Usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
				imageCI.Usage |= vk::ImageUsageFlagBits::eSampled;
				imageCI.MiscFlags = Vulkan::ImageCreateFlagBits::ForceArray;
				_shadowMap        = _wsi.GetDevice().CreateImage(imageCI);

				_shadowCascades.clear();
				Vulkan::ImageViewCreateInfo viewCI{.Image          = _shadowMap.Get(),
				                                   .Format         = imageCI.Format,
				                                   .BaseMipLevel   = 0,
				                                   .MipLevels      = VK_REMAINING_MIP_LEVELS,
				                                   .BaseArrayLayer = 0,
				                                   .ArrayLayers    = 1,
				                                   .Type           = vk::ImageViewType::e2D};
				for (int i = 0; i < _shadowCascadeCount; ++i) {
					viewCI.BaseArrayLayer = i;
					_shadowCascades.push_back(_wsi.GetDevice().CreateImageView(viewCI));
				}
			}

			PrepareCascades(scene, cameraEntity, sun, frameIndex);

			cmd->ImageBarrier(*_shadowMap,
			                  vk::ImageLayout::eUndefined,
			                  vk::ImageLayout::eDepthStencilAttachmentOptimal,
			                  vk::PipelineStageFlagBits::eTopOfPipe,
			                  {},
			                  vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
			                  vk::AccessFlagBits::eDepthStencilAttachmentWrite);

			Vulkan::RenderPassInfo rpInfo;
			rpInfo.ColorAttachmentCount   = 0;
			rpInfo.DepthStencilAttachment = &(_shadowMap->GetView());
			rpInfo.ClearAttachments       = 1 << 0;
			rpInfo.StoreAttachments       = 1 << 0;
			rpInfo.DSOps = Vulkan::DepthStencilOpBits::ClearDepthStencil | Vulkan::DepthStencilOpBits::StoreDepthStencil;
			rpInfo.ClearDepthStencil = vk::ClearDepthStencilValue(1.0f, 0);

			for (int i = 0; i < _shadowCascadeCount; ++i) {
				rpInfo.BaseArrayLayer = i;

				cmd->BeginRenderPass(rpInfo);
				cmd->SetOpaqueState();
				cmd->SetDepthClamp(true);
				// cmd->SetDepthBiasEnabled(true);
				// cmd->SetDepthBias(cLight.DepthBiasConstant, cLight.DepthBiasScale);
				cmd->SetProgram(_shadows);
				cmd->PushConstants(&i, sizeof(PushConstant), sizeof(i));
				RenderMeshes(cmd, scene, frameIndex, true);
				cmd->EndRenderPass();
			}

			cmd->ImageBarrier(*_shadowMap,
			                  vk::ImageLayout::eDepthStencilAttachmentOptimal,
			                  vk::ImageLayout::eShaderReadOnlyOptimal,
			                  vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
			                  vk::AccessFlagBits::eDepthStencilAttachmentWrite,
			                  vk::PipelineStageFlagBits::eFragmentShader,
			                  vk::AccessFlagBits::eShaderRead);
		}
	}

	Vulkan::RenderPassInfo rpInfo;
	if (_drawToSwapchain) {
		rpInfo = _wsi.GetDevice().GetStockRenderPass(Vulkan::StockRenderPass::Depth);
	} else {
		cmd->ImageBarrier(*image,
		                  vk::ImageLayout::eUndefined,
		                  vk::ImageLayout::eColorAttachmentOptimal,
		                  vk::PipelineStageFlagBits::eTopOfPipe,
		                  {},
		                  vk::PipelineStageFlagBits::eColorAttachmentOutput,
		                  vk::AccessFlagBits::eColorAttachmentWrite);

		auto depth = _wsi.GetDevice().RequestTransientAttachment(vk::Extent2D(_imageSize.x, _imageSize.y),
		                                                         _wsi.GetDevice().GetDefaultDepthFormat());

		rpInfo.ColorAttachmentCount   = 1;
		rpInfo.ColorAttachments[0]    = &image->GetView();
		rpInfo.DepthStencilAttachment = &(depth->GetView());
		rpInfo.ClearAttachments       = 1 << 0 | 1 << 1;
		rpInfo.StoreAttachments       = 1 << 0;
		rpInfo.ClearColors[0]         = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
		rpInfo.ClearDepthStencil      = vk::ClearDepthStencilValue(1.0f, 0);
	}

	cmd->BeginRenderPass(rpInfo);

	if (cameraEntity) {
		auto& cCameraTransform = cameraEntity.Transform();
		auto& cCamera          = cameraEntity.GetComponent<Luna::CameraComponent>();
		if (_drawToSwapchain) {
			cCamera.Camera.SetViewport(_wsi.GetFramebufferSize());
		} else {
			cCamera.Camera.SetViewport(_imageSize);
		}

		const auto& cameraProj = cCamera.Camera.GetProjection();
		const auto& cameraView = glm::inverse(cameraEntity.GetGlobalTransform());
		SceneData* sceneData   = reinterpret_cast<SceneData*>(sceneBuffer->Map());
		sceneData->Projection  = cameraProj;
		sceneData->View        = cameraView;
		sceneData->CameraPos   = glm::vec4(cCameraTransform.Translation, 1.0f);

		const Vulkan::SamplerCreateInfo samplerCI{.MagFilter        = vk::Filter::eLinear,
		                                          .MinFilter        = vk::Filter::eLinear,
		                                          .MipmapMode       = vk::SamplerMipmapMode::eLinear,
		                                          .AddressModeU     = vk::SamplerAddressMode::eClampToEdge,
		                                          .AddressModeV     = vk::SamplerAddressMode::eClampToEdge,
		                                          .AddressModeW     = vk::SamplerAddressMode::eClampToEdge,
		                                          .MipLodBias       = 0.0f,
		                                          .AnisotropyEnable = VK_FALSE,
		                                          .MaxAnisotropy    = 1.0f,
		                                          .MinLod           = 0.0f,
		                                          .MaxLod           = 1.0f,
		                                          .BorderColor      = vk::BorderColor::eFloatOpaqueWhite};
		auto* sampler = _wsi.GetDevice().RequestSampler(samplerCI);

		cmd->SetOpaqueState();
		cmd->SetProgram(_program);
		if (sun) {
			cmd->SetTexture(0, 1, _shadowMap->GetView(), sampler);
		} else {
			_shadowMap.Reset();
			_shadowCascades.clear();
			cmd->SetTexture(0, 1, _defaultImages.WhiteCSM->GetView(), sampler);
		}
		RenderMeshes(cmd, scene, frameIndex);
	}

	cmd->EndRenderPass();

	// Transition scene image to shader read-only
	if (!_drawToSwapchain) {
		cmd->ImageBarrier(*image,
		                  vk::ImageLayout::eColorAttachmentOptimal,
		                  vk::ImageLayout::eShaderReadOnlyOptimal,
		                  vk::PipelineStageFlagBits::eColorAttachmentOutput,
		                  vk::AccessFlagBits::eColorAttachmentWrite,
		                  vk::PipelineStageFlagBits::eFragmentShader,
		                  vk::AccessFlagBits::eShaderRead);
	}
}

void SceneRenderer::PrepareCascades(Luna::Scene& scene,
                                    Luna::Entity& cameraEntity,
                                    Luna::Entity& sunEntity,
                                    uint32_t frameIndex) {
	// Fetch information about our scene camera and light.
	const auto& cCamera = cameraEntity.GetComponent<CameraComponent>();
	const auto& cLight  = sunEntity.GetComponent<LightComponent>();

	// Determine the AABB encompassing the entire scene.
	const auto rootEntities = scene.GetRootEntities();
	Luna::AABB sceneBounds  = {};
	for (const auto entity : rootEntities) { sceneBounds.Contain(entity.GetGlobalBounds()); }

	// Set a few initial variables to help with creating the cascades.
	const float zNear  = cCamera.Camera.GetZNear();
	const float zFar   = cCamera.Camera.GetZFar();
	const float zRange = zFar - zNear;
	const float zRatio = zFar / zNear;

	// Determine our directional light's direction.
	const glm::vec3 lightDir = glm::vec3(0, 0, -1) * glm::quat(glm::radians(sunEntity.Transform().Rotation));

	// Map our scene's uniform buffer, and upload some of the shadow data.
	auto& sceneBuffer             = _sceneBuffers[frameIndex];
	SceneData* sceneData          = reinterpret_cast<SceneData*>(sceneBuffer->Map());
	sceneData->SunDirection       = glm::vec4(lightDir, 0.0f);
	sceneData->ShadowCascadeCount = _shadowCascadeCount;
	sceneData->ShadowPCF          = _shadowPCF ? 1 : 0;
	sceneData->DebugShowCascades  = _debugCSMSplit ? 1 : 0;

	// Determine where each cascade will stop.
	std::vector<float> cascadeSplits(_shadowCascadeCount, 0.0f);
	for (int i = 0; i < _shadowCascadeCount; ++i) {
		const float p       = (i + 1) / static_cast<float>(_shadowCascadeCount);
		const float log     = zNear * std::pow(zRatio, p);
		const float uniform = zNear + zRange * p;
		const float d       = cLight.CascadeSplitLambda * (log - uniform) + uniform;
		cascadeSplits[i]    = (d - zNear) / zRange;
	}

	// Determine our camera's inverse matrix, to convert our frustum to world coordinates.
	const auto& cameraProj = cCamera.Camera.GetProjection();
	const auto& cameraView = glm::inverse(cameraEntity.GetGlobalTransform());
	const glm::mat4 invCam = glm::inverse(cameraProj * cameraView);

	// Set up each cascade's transformation matrix.
	float lastSplitDist = 0.0f;
	for (int i = 0; i < _shadowCascadeCount; ++i) {
		const float splitDist = cascadeSplits[i];

		// Define our light frustum corners.
		glm::vec3 frustumCorners[8] = {glm::vec3(-1, 1, -1),
		                               glm::vec3(1, 1, -1),
		                               glm::vec3(1, -1, -1),
		                               glm::vec3(-1, -1, -1),
		                               glm::vec3(-1, 1, 1),
		                               glm::vec3(1, 1, 1),
		                               glm::vec3(1, -1, 1),
		                               glm::vec3(-1, -1, 1)};

		// Transform the frustum corners to world space.
		for (int i = 0; i < 8; ++i) {
			const glm::vec4 invCorner = invCam * glm::vec4(frustumCorners[i], 1.0f);
			frustumCorners[i]         = invCorner / invCorner.w;
		}

		// Shrink the frustum to fit this cascade.
		for (int i = 0; i < 4; ++i) {
			const glm::vec3 dist  = frustumCorners[i + 4] - frustumCorners[i];
			frustumCorners[i + 4] = frustumCorners[i] + (dist * splitDist);
			frustumCorners[i]     = frustumCorners[i] + (dist * lastSplitDist);
		}

		// Bound the frustum to the scene to avoid unnecessarily large views.
		if (sceneBounds.Valid()) {
			for (int i = 0; i < 8; ++i) {
				frustumCorners[i] = glm::clamp(frustumCorners[i], sceneBounds.GetMin(), sceneBounds.GetMax());
			}
		}

		// Determine the center of the frustum.
		glm::vec3 frustumCenter = glm::vec3(0.0f);
		for (int i = 0; i < 8; ++i) { frustumCenter += frustumCorners[i]; }
		frustumCenter /= 8.0f;

		// Determine the radius of the frustum.
		float radius = 0.0f;
		for (int i = 0; i < 8; ++i) {
			const float distance = glm::length(frustumCorners[i] - frustumCenter);
			radius               = glm::max(radius, distance);
		}

		// Round the radius to the nearest 16, to give padding and allow for off-screen shadow casters.
		radius = std::ceil(radius * 16.0f) / 16.0f;

		// Determine the extents of our orthographic projection.
		const glm::vec3 maxExtents = glm::vec3(radius);
		const glm::vec3 minExtents = -maxExtents;

		// Create our light-view matrices.
		const glm::mat4 lightViewMatrix =
			glm::lookAt(frustumCenter - lightDir * -minExtents.z, frustumCenter, glm::vec3(0.0f, 1.0f, 0.0f));
		const glm::mat4 lightProjMatrix =
			glm::ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, 0.0f, maxExtents.z - minExtents.z);

		// Set our scene data for this cascade.
		sceneData->SunMatrices[i]   = lightProjMatrix * lightViewMatrix;
		sceneData->CascadeSplits[i] = (zNear + splitDist * zRange) * -1.0f;

		// Prepare for the next cascade.
		lastSplitDist = splitDist;
	}
}

void SceneRenderer::SetDrawToSwapchain(bool drawToSwapchain) {
	_drawToSwapchain = drawToSwapchain;
	if (!_drawToSwapchain) { _sceneImages.clear(); }
}

void SceneRenderer::SetImageSize(const glm::uvec2& size) {
	if (_drawToSwapchain) { return; }

	if (size != _imageSize) {
		_imageSize = size;
		_sceneBuffers.clear();
		_sceneImages.clear();

		Vulkan::ImageCreateInfo imageCI =
			Vulkan::ImageCreateInfo::RenderTarget(_imageSize.x, _imageSize.y, vk::Format::eB8G8R8A8Unorm);
		imageCI.Usage |= vk::ImageUsageFlagBits::eSampled;

		const auto imageCount = _wsi.GetImageCount();
		for (int i = 0; i < imageCount; ++i) { _sceneImages.push_back(_wsi.GetDevice().CreateImage(imageCI)); }
	}
}

void SceneRenderer::RenderMeshes(Luna::Vulkan::CommandBufferHandle& cmd,
                                 Luna::Scene& scene,
                                 uint32_t frameIndex,
                                 bool shadows) {
	auto& sceneBuffer = _sceneBuffers[frameIndex];

	cmd->SetVertexAttribute(0, 0, vk::Format::eR32G32B32Sfloat, 0);
	cmd->SetVertexAttribute(1, 1, vk::Format::eR32G32B32Sfloat, 0);
	cmd->SetVertexAttribute(2, 2, vk::Format::eR32G32Sfloat, 0);
	cmd->SetUniformBuffer(0, 0, *sceneBuffer);
	auto renderables = scene.GetRegistry().view<MeshComponent>();
	for (auto entityId : renderables) {
		Entity entity(entityId, scene);
		const auto& cMesh = renderables.get<MeshComponent>(entityId);
		const PushConstant pc{.Model = entity.GetGlobalTransform()};
		cmd->PushConstants(&pc, 0, sizeof(PushConstant));

		auto& mesh = cMesh.Mesh;
		if (mesh) {
			cmd->SetVertexBinding(0, *mesh->Buffer, mesh->PositionOffset, sizeof(glm::vec3), vk::VertexInputRate::eVertex);
			cmd->SetVertexBinding(1, *mesh->Buffer, mesh->NormalOffset, sizeof(glm::vec3), vk::VertexInputRate::eVertex);
			cmd->SetVertexBinding(2, *mesh->Buffer, mesh->Texcoord0Offset, sizeof(glm::vec2), vk::VertexInputRate::eVertex);
			cmd->SetIndexBuffer(*mesh->Buffer, mesh->IndexOffset, vk::IndexType::eUint32);

			for (auto& submesh : mesh->Submeshes) {
				const bool hasMaterial =
					submesh.MaterialIndex < cMesh.Materials.size() && cMesh.Materials[submesh.MaterialIndex];
				auto& material = hasMaterial ? cMesh.Materials[submesh.MaterialIndex] : _nullMaterial;
				material->Update(_wsi.GetDevice());

				if (shadows) {
					cmd->SetCullMode(vk::CullModeFlagBits::eBack);
				} else {
					cmd->SetCullMode(material->DualSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack);
				}

				cmd->SetUniformBuffer(1, 0, *material->DataBuffer);
				SetTexture(cmd, 1, 1, material->Albedo, _defaultImages.White2D);
				SetTexture(cmd, 1, 2, material->Normal, _defaultImages.Normal2D);
				SetTexture(cmd, 1, 3, material->PBR, _defaultImages.White2D);
				SetTexture(cmd, 1, 4, material->Emissive, _defaultImages.Black2D);

				if (submesh.IndexCount > 0) {
					cmd->DrawIndexed(submesh.IndexCount, 1, submesh.FirstIndex, submesh.FirstVertex, 0);
				} else {
					cmd->Draw(submesh.VertexCount, 1, submesh.FirstVertex, 0);
				}
			}
		}
	}
}

void SceneRenderer::SetTexture(Luna::Vulkan::CommandBufferHandle& cmd,
                               uint32_t set,
                               uint32_t binding,
                               const TextureHandle& texture,
                               const Vulkan::ImageHandle& fallback) {
	const auto& fallbackView = fallback ? fallback : _defaultImages.White2D;
	const bool ready         = bool(texture);
	const bool hasTexture    = ready && texture->Image;
	const bool hasSampler    = ready && texture->Sampler;
	const auto& view         = hasTexture ? texture->Image->GetView() : fallbackView->GetView();
	const auto sampler =
		hasSampler ? texture->Sampler : _wsi.GetDevice().RequestSampler(Vulkan::StockSampler::DefaultGeometryFilterWrap);
	cmd->SetTexture(set, binding, view, sampler);
}

void SceneRenderer::ShowSettings() {
	if (ImGui::Begin("Renderer")) {
		if (ImGui::CollapsingHeader(ICON_FA_MOON " Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (ImGui::BeginTable("LightComponent_Properties", 2, ImGuiTableFlags_BordersInnerV)) {
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_WidthFixed, 125.0f);

				ImGui::TableNextColumn();
				ImGui::Text("Shadow PCF");
				ImGui::TableNextColumn();
				ImGui::Checkbox("##ShadowPCF", &_shadowPCF);

				ImGui::TableNextColumn();
				ImGui::Text("Shadow Cascades");
				ImGui::TableNextColumn();
				ImGui::SliderInt("##ShadowCascades", &_shadowCascadeCount, 1, 4);

				ImGui::TableNextColumn();
				ImGui::Text("Show Splits");
				ImGui::TableNextColumn();
				ImGui::Checkbox("##ShowCSMSPlit", &_debugCSMSplit);

				if (_shadowMap) {
					if (ImGui::Button("View Shadow Map")) { _debugCSM = !_debugCSM; }
				}

				ImGui::EndTable();
			}
		}
	}
	ImGui::End();

	if (_debugCSM) {
		if (ImGui::Begin("Cascaded Shadow Map Debug", &_debugCSM)) {
			if (ImGui::BeginTable("CSMDebug", 2)) {
				auto ui = Luna::ImGuiRenderer::Get();

				const ImVec4 cascadeColors[4] = {ImVec4(1.0f, 0.25f, 0.25f, 1.0f),
				                                 ImVec4(0.25f, 1.0f, 0.25f, 1.0f),
				                                 ImVec4(0.25f, 0.25f, 1.0f, 1.0f),
				                                 ImVec4(1.0f, 1.0f, 0.25f, 1.0f)};
				const auto cascades           = _shadowCascades.size();
				for (int i = 0; i < cascades; ++i) {
					auto& cascade = _shadowCascades[i];

					ImGui::TableNextColumn();
					if (cascade) {
						ImGui::Image(ui->Texture(cascade, Vulkan::StockSampler::LinearClamp),
						             ImVec2(256, 256),
						             ImVec2(0, 0),
						             ImVec2(1, 1),
						             _debugCSMSplit ? cascadeColors[i] : ImVec4(1, 1, 1, 1));
					}
				}

				ImGui::EndTable();
			}
		}
		ImGui::End();
	}
}
