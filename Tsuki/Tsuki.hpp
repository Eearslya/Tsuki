#pragma once

#include <Application/Application.hpp>
#include <Scene/Entity.hpp>
#include <Scene/Scene.hpp>
#include <memory>

class GltfLoader;
class SceneHierarchyPanel;
class SceneRenderer;

namespace Luna {
class ImGuiRenderer;
}

class Tsuki : public Luna::Application {
 public:
	virtual void Start() override;
	virtual void Stop() override;
	virtual void Update(float dt) override;

 private:
	void StyleImGui();

	std::unique_ptr<Luna::ImGuiRenderer> _imguiRenderer;
	std::shared_ptr<Luna::Scene> _scene;
	std::unique_ptr<GltfLoader> _gltfLoader;
	std::unique_ptr<SceneRenderer> _sceneRenderer;
	std::unique_ptr<SceneHierarchyPanel> _scenePanel;

	bool _mouseControl = false;
};
