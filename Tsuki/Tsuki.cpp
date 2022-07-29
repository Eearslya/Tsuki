#include "Tsuki.hpp"

#include <Application/Application.hpp>
#include <Application/EntryPoint.hpp>
#include <Application/Input.hpp>
#include <ImGui/ImGuiRenderer.hpp>
#include <Scene/CameraComponent.hpp>
#include <Scene/MeshComponent.hpp>
#include <Scene/Scene.hpp>
#include <Scene/TransformComponent.hpp>
#include <Vulkan/CommandBuffer.hpp>
#include <Vulkan/Device.hpp>
#include <Vulkan/RenderPass.hpp>
#include <Vulkan/WSI.hpp>
#include <glm/gtx/euler_angles.hpp>

#include "DirectionalLightComponent.hpp"
#include "GltfLoader.hpp"
#include "Primitives.hpp"
#include "SceneHierarchyPanel.hpp"
#include "SceneRenderer.hpp"
#include "UI.hpp"

void Tsuki::Start() {
	_imguiRenderer = std::make_unique<Luna::ImGuiRenderer>(*_wsi);
	_scene         = std::make_shared<Luna::Scene>();
	_gltfLoader    = std::make_unique<GltfLoader>(*_wsi);
	_sceneRenderer = std::make_unique<SceneRenderer>(*_wsi);
	_scenePanel    = std::make_unique<SceneHierarchyPanel>(_scene);
	StyleImGui();

	Luna::Input::OnKey += [this](Luna::Key key, Luna::InputAction action, Luna::InputMods mods) {
		if (action == Luna::InputAction::Press) {
			if (key == Luna::Key::F5) { _sceneRenderer->ReloadShaders(); }
		}
	};

	_camera       = _scene->CreateEntity("Camera");
	auto& cCamera = _camera.AddComponent<Luna::CameraComponent>();
	cCamera.Camera.SetPerspective(cCamera.Camera.GetFovDegrees(), cCamera.Camera.GetZNear(), 500.0f);
	auto& cameraTransform       = _camera.Transform();
	cameraTransform.Translation = glm::vec3(-5, 1.5, 0);
	cameraTransform.Rotation    = glm::vec3(0, 270, 0);
	Luna::Input::OnMoved += [this](glm::dvec2 pos) {
		const float sensitivity = 0.1f;
		if (_mouseControl) {
			auto& cTransform = _camera.GetComponent<Luna::TransformComponent>();
			auto& cCamera    = _camera.GetComponent<Luna::CameraComponent>();

			cTransform.Rotation += sensitivity * glm::vec3(pos.y, pos.x, 0.0f);
			cTransform.Rotation.x = glm::clamp(cTransform.Rotation.x, -89.0f, 89.0f);
			if (cTransform.Rotation.y < 0.0f) { cTransform.Rotation.y += 360.0f; }
			if (cTransform.Rotation.y >= 360.0f) { cTransform.Rotation.y -= 360.0f; }
		}
	};

	{
		auto light         = _scene->CreateEntity("Light");
		auto& xfLight      = light.Transform();
		auto& cLight       = light.AddComponent<DirectionalLightComponent>();
		xfLight.Rotation   = glm::vec3(85.0f, 20.0f, 0.0f);
		cLight.CastShadows = false;
		cLight.SoftShadows = false;
	}

	if (false) {
		auto model = _gltfLoader->Load("Assets/Models/DeccerCubes/SM_Deccer_Cubes_Textured.gltf", *_scene);
		model.Rotate(glm::vec3(15.0f, -30.0f, 0.0f));
		model.Scale(0.2f);
	}

	if (false) {
		auto model = _gltfLoader->Load("Assets/Models/DamagedHelmet/DamagedHelmet.gltf", *_scene);
		model.Translate(glm::vec3(-2.0f, 0.0f, 0.0f));
		model.Scale(0.5f);
	}

	if (false) {
		auto model = _gltfLoader->Load("Assets/Models/BoomBox/BoomBox.gltf", *_scene);
		model.Translate(glm::vec3(2.0f, 0.0f, 0.0f));
		model.Scale(50.0f);
	}

	{
		auto model = _gltfLoader->Load("Assets/Models/Sponza/Sponza.gltf", *_scene);
		model.Translate(glm::vec3(0.0f, -1.0f, 0.0f));
	}

	if (false) {
		auto plane = _scene->CreateEntity("Plane");
		plane.Translate(glm::vec3(0, -2.0f, 0));
		plane.Scale(10.0f);
		auto& planeMesh = plane.AddComponent<Luna::MeshComponent>();
		planeMesh.Mesh  = Primitives::Plane(_wsi->GetDevice());
	}
}

void Tsuki::Stop() {}

void Tsuki::Update(float dt) {
	auto& device = _wsi->GetDevice();

	ImGuiIO& io = ImGui::GetIO();

	auto camera = _scene->GetMainCamera();

	// Handle mouse camera control.
	if (_mouseControl) {
		// Note: We cannot use ImGui to determine if the mouse button is released here, because setting the cursor as
		// hidden disables all ImGui mouse input.
		if (!Luna::Input::GetButton(Luna::MouseButton::Right)) {
			_mouseControl = false;
			Luna::Input::SetCursorHidden(false);
		}

		if (camera) {
			const glm::mat3 transform = camera.GetLocalTransform();
			const glm::vec3 right     = glm::normalize(transform[0]);
			const glm::vec3 up        = glm::normalize(transform[1]);
			const glm::vec3 forward   = glm::normalize(-transform[2]);

			const float moveSpeed = 5.0f * dt;
			glm::vec3 movement    = glm::vec3(0.0f);
			if (Luna::Input::GetKey(Luna::Key::W)) { movement += moveSpeed * forward; }
			if (Luna::Input::GetKey(Luna::Key::S)) { movement -= moveSpeed * forward; }
			if (Luna::Input::GetKey(Luna::Key::D)) { movement += moveSpeed * right; }
			if (Luna::Input::GetKey(Luna::Key::A)) { movement -= moveSpeed * right; }
			camera.Translate(movement);
		}
	} else {
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !io.WantCaptureMouse) {
			_mouseControl = true;
			Luna::Input::SetCursorHidden(true);
		}
	}

	_wsi->BeginFrame();
	auto cmd = device.RequestCommandBuffer();
	_imguiRenderer->BeginFrame();

	ImGui::ShowDemoWindow();

	_sceneRenderer->SetImageSize(_wsi->GetFramebufferSize());
	_sceneRenderer->ShowSettings();
	_sceneRenderer->Render(cmd, *_scene, _wsi->GetAcquiredIndex());
	_scenePanel->Render();

	_imguiRenderer->Render(cmd, _wsi->GetAcquiredIndex(), false);
	device.Submit(cmd);
	_wsi->EndFrame();
}

void Tsuki::StyleImGui() {
	ImGuiIO& io = ImGui::GetIO();

	// Fonts
	{
		io.Fonts->Clear();

		io.Fonts->AddFontFromFileTTF("Assets/Fonts/Roboto-SemiMedium.ttf", 16.0f);

		ImFontConfig jpConfig;
		jpConfig.MergeMode = true;
		io.Fonts->AddFontFromFileTTF(
			"Assets/Fonts/NotoSansJP-Medium.otf", 18.0f, &jpConfig, io.Fonts->GetGlyphRangesJapanese());

		ImFontConfig faConfig;
		faConfig.MergeMode                 = true;
		faConfig.PixelSnapH                = true;
		static const ImWchar fontAwesome[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};
		io.Fonts->AddFontFromFileTTF("Assets/Fonts/FontAwesome6Free-Regular-400.otf", 16.0f, &faConfig, fontAwesome);
		io.Fonts->AddFontFromFileTTF("Assets/Fonts/FontAwesome6Free-Solid-900.otf", 16.0f, &faConfig, fontAwesome);
	}

	_imguiRenderer->UpdateFontAtlas();
}

namespace Luna {
std::unique_ptr<Application> CreateApplication(int argc, const char** argv) {
	return std::make_unique<Tsuki>();
}
}  // namespace Luna
