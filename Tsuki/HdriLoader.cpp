#include "HdriLoader.hpp"

#include <stb_image.h>

#include <Utility/Files.hpp>
#include <Vulkan/CommandBuffer.hpp>
#include <Vulkan/Device.hpp>
#include <Vulkan/Image.hpp>
#include <Vulkan/RenderPass.hpp>
#include <Vulkan/Shader.hpp>
#include <Vulkan/TextureFormat.hpp>
#include <Vulkan/WSI.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "SkyboxComponent.hpp"

using namespace Luna;

HdriLoader::HdriLoader(Vulkan::WSI& wsi) : _wsi(&wsi) {
	_cubemapProgram = _wsi->GetDevice().RequestProgram(ReadFile("Assets/Shaders/Cubemap.vert.glsl"),
	                                                   ReadFile("Assets/Shaders/Cubemap.frag.glsl"));
}

HdriLoader::~HdriLoader() noexcept {}

Entity HdriLoader::Load(const std::filesystem::path& hdriPath, Scene& scene) {
	const std::string hdriPathStr = hdriPath.string();
	const char* hdriPathC         = hdriPathStr.c_str();
	auto bytes                    = ReadFileBinary(hdriPath);

	int width, height, components;
	stbi_set_flip_vertically_on_load(1);
	auto pixels = stbi_loadf_from_memory(bytes.data(), bytes.size(), &width, &height, &components, STBI_rgb_alpha);

	const Vulkan::ImageInitialData initialData{.Data = pixels};
	const auto imageCI = Vulkan::ImageCreateInfo::Immutable2D(width, height, vk::Format::eR32G32B32A32Sfloat);
	auto baseHdr       = _wsi->GetDevice().CreateImage(imageCI, &initialData);

	Vulkan::ImageCreateInfo cubeImageCI{.Domain        = Vulkan::ImageDomain::Physical,
	                                    .Format        = vk::Format::eR16G16B16A16Sfloat,
	                                    .InitialLayout = vk::ImageLayout::eTransferDstOptimal,
	                                    .Samples       = vk::SampleCountFlagBits::e1,
	                                    .Type          = vk::ImageType::e2D,
	                                    .Usage  = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
	                                    .Width  = 1,
	                                    .Height = 1,
	                                    .Depth  = 1,
	                                    .ArrayLayers = 6,
	                                    .MipLevels   = 1,
	                                    .Flags       = vk::ImageCreateFlagBits::eCubeCompatible};
	cubeImageCI.Width  = 1024;
	cubeImageCI.Height = 1024;
	cubeImageCI.MipLevels =
		Vulkan::TextureFormatLayout::MipLevels(cubeImageCI.Width, cubeImageCI.Height, cubeImageCI.Depth);
	auto skybox = _wsi->GetDevice().CreateImage(cubeImageCI);

	Vulkan::ImageHandle renderTarget;
	{
		auto imageCI = Vulkan::ImageCreateInfo::RenderTarget(
			skybox->GetCreateInfo().Width, skybox->GetCreateInfo().Height, vk::Format::eR16G16B16A16Sfloat);
		imageCI.Usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;
		renderTarget  = _wsi->GetDevice().CreateImage(imageCI);
	}

	auto cmdBuf = _wsi->GetDevice().RequestCommandBuffer(Vulkan::CommandBufferType::AsyncGraphics);

	glm::mat4 captureProjection    = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
	const glm::mat4 captureViews[] = {
		glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
		glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
		glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
		glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)),
		glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
		glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f))};
	struct PushConstant {
		glm::mat4 ViewProjection;
		float Roughness;
	};

	const auto ProcessCubeMap = [&](Vulkan::Program* program,
	                                Vulkan::ImageHandle& srcCubeMap,
	                                Vulkan::ImageHandle& dstCubeMap) {
		auto rpInfo                 = Vulkan::RenderPassInfo{};
		rpInfo.ColorAttachmentCount = 1;
		rpInfo.ColorAttachments[0]  = &renderTarget->GetView();
		rpInfo.StoreAttachments     = 1 << 0;

		const uint32_t mips = dstCubeMap->GetCreateInfo().MipLevels;
		const uint32_t dim  = dstCubeMap->GetCreateInfo().Width;

		for (uint32_t mip = 0; mip < mips; ++mip) {
			const uint32_t mipDim = static_cast<float>(dim * std::pow(0.5f, mip));

			for (uint32_t i = 0; i < 6; ++i) {
				const PushConstant pc{.ViewProjection = captureProjection * captureViews[i],
				                      .Roughness      = static_cast<float>(mip) / static_cast<float>(mips - 1)};
				rpInfo.RenderArea = vk::Rect2D{{0, 0}, {mipDim, mipDim}};
				cmdBuf->BeginRenderPass(rpInfo);
				cmdBuf->SetProgram(program);
				cmdBuf->SetCullMode(vk::CullModeFlagBits::eNone);
				cmdBuf->SetTexture(0, 0, srcCubeMap->GetView(), Vulkan::StockSampler::LinearClamp);
				cmdBuf->PushConstants(&pc, 0, sizeof(pc));
				cmdBuf->Draw(36);
				cmdBuf->EndRenderPass();

				const vk::ImageMemoryBarrier barrier(vk::AccessFlagBits::eColorAttachmentWrite,
				                                     vk::AccessFlagBits::eTransferRead,
				                                     vk::ImageLayout::eColorAttachmentOptimal,
				                                     vk::ImageLayout::eTransferSrcOptimal,
				                                     VK_QUEUE_FAMILY_IGNORED,
				                                     VK_QUEUE_FAMILY_IGNORED,
				                                     renderTarget->GetImage(),
				                                     vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
				cmdBuf->Barrier(
					vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eTransfer, {}, {}, {barrier});

				cmdBuf->CopyImage(*dstCubeMap,
				                  *renderTarget,
				                  {},
				                  {},
				                  vk::Extent3D(mipDim, mipDim, 1),
				                  vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, mip, i, 1),
				                  vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1));

				const vk::ImageMemoryBarrier barrier2(vk::AccessFlagBits::eTransferWrite,
				                                      vk::AccessFlagBits::eColorAttachmentWrite,
				                                      vk::ImageLayout::eTransferSrcOptimal,
				                                      vk::ImageLayout::eColorAttachmentOptimal,
				                                      VK_QUEUE_FAMILY_IGNORED,
				                                      VK_QUEUE_FAMILY_IGNORED,
				                                      renderTarget->GetImage(),
				                                      vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
				cmdBuf->Barrier(
					vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, {}, {barrier2});
			}
		}

		const vk::ImageMemoryBarrier barrier(
			vk::AccessFlagBits::eTransferWrite,
			vk::AccessFlagBits::eShaderRead,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			dstCubeMap->GetImage(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, dstCubeMap->GetCreateInfo().MipLevels, 0, 6));
		cmdBuf->Barrier(
			vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {barrier});
	};

	ProcessCubeMap(_cubemapProgram, baseHdr, skybox);

	renderTarget.Reset();

	_wsi->GetDevice().Submit(cmdBuf);

	auto sky       = scene.CreateEntity("Sky");
	auto& cSkybox  = sky.AddComponent<SkyboxComponent>();
	cSkybox.Skybox = skybox;

	return sky;
}
