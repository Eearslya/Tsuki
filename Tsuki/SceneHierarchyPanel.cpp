#include "SceneHierarchyPanel.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <ImGui/ImGuiRenderer.hpp>
#include <Scene/CameraComponent.hpp>
#include <Scene/Entity.hpp>
#include <Scene/IdComponent.hpp>
#include <Scene/MeshComponent.hpp>
#include <Scene/NameComponent.hpp>
#include <Scene/RelationshipComponent.hpp>
#include <Scene/Scene.hpp>
#include <Scene/TransformComponent.hpp>
#include <Utility/Log.hpp>
#include <optional>

#include "DirectionalLightComponent.hpp"
#include "IconsFontAwesome6.h"
#include "SkyboxComponent.hpp"

using namespace Luna;

SceneHierarchyPanel::SceneHierarchyPanel(const std::shared_ptr<Scene>& scene) : _scene(scene) {}

template <typename T, typename... Args>
static bool AddComponentMenu(Entity entity, const char* label, Args&&... args) {
	const bool showItem = !entity.HasComponent<T>();
	if (showItem && ImGui::MenuItem(label)) { entity.AddComponent<T>(std::forward<Args>(args)...); }

	return showItem;
}

void SceneHierarchyPanel::Render() {
	auto scene = _scene.lock();
	if (!scene) {
		_scene.reset();
		_selected = {};
		SelectionChanged();
		return;
	}

	auto& registry = scene->GetRegistry();

	if (ImGui::Begin("Hierarchy")) {
		if (!_selected) {
			_selected = {};
			SelectionChanged();
		}

		auto rootEntities = scene->GetRootEntities();
		for (auto entity : rootEntities) { DrawEntity(entity); }

		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() && ImGui::IsWindowFocused()) {
			_selected = {};
			SelectionChanged();
		}

		if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight, false)) {
			if (ImGui::MenuItem(ICON_FA_PLUS " Create Entity")) {
				_selected = scene->CreateEntity();
				SelectionChanged();
			}

			ImGui::EndPopup();
		}
	}
	ImGui::End();

	ImGui::SetNextWindowSizeConstraints(ImVec2(350.0f, -1.0f), ImVec2(std::numeric_limits<float>::infinity(), -1.0f));
	if (ImGui::Begin("Properties") && _selected) {
		DrawComponents(_selected);
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Button(ICON_FA_PLUS " Add Component");
		if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonLeft)) {
			bool anyShown = false;

			anyShown |= AddComponentMenu<CameraComponent>(_selected, ICON_FA_CAMERA " Camera");
			anyShown |= AddComponentMenu<DirectionalLightComponent>(_selected, ICON_FA_SUN " Directional Light");
			anyShown |= AddComponentMenu<MeshComponent>(_selected, ICON_FA_CIRCLE_NODES " Mesh");
			anyShown |= AddComponentMenu<SkyboxComponent>(_selected, ICON_FA_GLOBE " Skybox");

			if (!anyShown) {
				ImGui::BeginDisabled();
				ImGui::MenuItem(ICON_FA_X " No Components Available");
				ImGui::EndDisabled();
			}

			ImGui::EndPopup();
		}
	}
	ImGui::End();
}

void SceneHierarchyPanel::DrawEntity(Entity entity) {
	auto scene = _scene.lock();
	if (!scene) {
		_scene.reset();
		return;
	}

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (entity == _selected) { flags |= ImGuiTreeNodeFlags_Selected; }

	const entt::entity entityId = entity;
	const void* nodeId          = reinterpret_cast<void*>(uint64_t(entityId));
	bool deleted                = false;

	const auto& cName         = entity.GetComponent<NameComponent>();
	const auto& cRelationship = entity.GetComponent<RelationshipComponent>();

	if (cRelationship.FirstChild == entt::null) { flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet; }

	const bool open = ImGui::TreeNodeEx(nodeId, flags, "%s", cName.Name.c_str());
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
		_selected = entity;
		SelectionChanged();
	}

	if (ImGui::BeginPopupContextItem()) {
		if (ImGui::MenuItem(ICON_FA_TRASH_CAN " Delete")) { deleted = true; }
		ImGui::EndPopup();
	}

	if (open) {
		auto child = Entity(cRelationship.FirstChild, *scene);
		while (child) {
			DrawEntity(child);
			auto& childRel = child.GetComponent<RelationshipComponent>();
			child          = Entity(childRel.Next, *scene);
		}

		ImGui::TreePop();
	}

	if (deleted) { scene->DestroyEntity(entity); }
}

// Custom CollapsingHeader implementation that allows for a special button on the right side of the header.
static bool CollapsingHeader(const char* label, bool* specialClick, ImGuiTreeNodeFlags flags, const char* buttonLabel) {
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->SkipItems) { return false; }
	const auto& style = ImGui::GetStyle();

	const ImVec2 buttonSize(GImGui->FontSize + style.FramePadding.x * 2.0f,
	                        GImGui->FontSize + style.FramePadding.y * 2.0f);

	flags |= ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_AllowItemOverlap |
	         static_cast<ImGuiTreeNodeFlags>(ImGuiTreeNodeFlags_ClipLabelForTrailingButton);

	ImGuiID id  = window->GetID(label);
	bool isOpen = ImGui::TreeNodeBehavior(id, flags, label);
	ImGui::SameLine(ImGui::GetItemRectSize().x - buttonSize.x);
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	if (ImGui::Button(buttonLabel, buttonSize)) { *specialClick = true; }
	ImGui::PopStyleColor();

	return isOpen;
}

template <typename T>
static void DrawComponent(Entity entity,
                          const std::string& label,
                          std::function<bool(Entity, T&)> drawFn,
                          std::optional<std::function<bool(Entity, T&)>> propsFn = std::nullopt) {
	bool hasPropertyMenu = propsFn.has_value();
	bool propertyMenu    = false;
	bool deleted         = false;

	if (entity.HasComponent<T>()) {
		const std::string compId = label + "##Properties";
		ImGui::PushID(compId.c_str());
		if (hasPropertyMenu) {
			if (CollapsingHeader(
						label.c_str(), hasPropertyMenu ? &propertyMenu : nullptr, ImGuiTreeNodeFlags_DefaultOpen, ICON_FA_WRENCH)) {
				auto& component = entity.GetComponent<T>();
				deleted |= drawFn(entity, component);

				if (propertyMenu) { ImGui::OpenPopup(compId.c_str()); }

				if (ImGui::BeginPopup(compId.c_str())) {
					deleted |= propsFn.value()(entity, component);
					ImGui::EndPopup();
				}
			}
		} else if (ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
			auto& component = entity.GetComponent<T>();
			deleted |= drawFn(entity, component);
		}

		if (deleted) { entity.RemoveComponent<T>(); }

		ImGui::PopID();
	}
}

void SceneHierarchyPanel::DrawComponents(Entity entity) {
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

	// Id
	if (entity.HasComponent<IdComponent>()) {
		auto& cId = entity.GetComponent<IdComponent>();

		if (ImGui::BeginTable("IdComponent_Properties", 2, ImGuiTableFlags_BordersInnerV)) {
			const std::string idString = fmt::format("{:x}", cId.Id);
			const char* idCString      = idString.c_str();
			const size_t idCStringLen  = strlen(idCString);

			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_WidthFixed, 85.0f);
			ImGui::TableNextColumn();
			ImGui::Text("ID");
			ImGui::TableNextColumn();
			ImGui::InputText("##ID", const_cast<char*>(idCString), idCStringLen, ImGuiInputTextFlags_ReadOnly);
			ImGui::EndTable();
		}
	}

	// Name
	if (entity.HasComponent<NameComponent>()) {
		auto& cName = entity.GetComponent<NameComponent>();
		char nameBuffer[256];
#ifdef _MSC_VER
		strcpy_s(nameBuffer, cName.Name.data());
#else
		strcpy(nameBuffer, cName.Name.data());
#endif

		if (ImGui::BeginTable("NameComponent_Properties", 2, ImGuiTableFlags_BordersInnerV)) {
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_WidthFixed, 85.0f);
			ImGui::TableNextColumn();
			ImGui::Text("Name");
			ImGui::TableNextColumn();
			if (ImGui::InputText("##Name", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
				if (strlen(nameBuffer) > 0) { cName.Name = nameBuffer; }
			}
			ImGui::EndTable();
		}
	}

	ImGui::PopStyleVar();
	ImGui::Spacing();
	ImGui::Separator();

	// Transform
	DrawComponent<TransformComponent>(
		entity,
		ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT " Transform",
		[](Entity entity, TransformComponent& cTransform) {
			const auto EditVec3 = [](const std::string& label,
		                           glm::vec3& value,
		                           float speed      = 0.1f,
		                           float resetValue = 0.0f,
		                           bool* lock       = nullptr) {
				auto& io    = ImGui::GetIO();
				auto& style = ImGui::GetStyle();

				const float lineHeight = io.Fonts->Fonts[0]->FontSize + style.FramePadding.y * 2.0f;
				const ImVec2 buttonSize(lineHeight + 3.0f, lineHeight);
				const bool locked = lock && *lock;

				ImGui::TableNextColumn();
				ImGui::Text("%s", label.c_str());

				ImGui::TableNextColumn();
				ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
				ImGui::PushID(label.c_str());

				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.61f, 0.006f, 0.015f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.79f, 0.03f, 0.03f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, style.Colors[ImGuiCol_Button]);
				if (ImGui::Button("X", buttonSize)) {
					if (locked) {
						value = glm::vec3(resetValue);
					} else {
						value.x = resetValue;
					}
				}
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					value = glm::vec3(resetValue);
				}
				ImGui::SameLine();
				if (ImGui::DragFloat("##XValue", &value.x, speed, 0.0f, 0.0f, "%.2f")) {
					if (locked) { value = glm::vec3(value.x); }
				}
				ImGui::PopItemWidth();
				ImGui::SameLine();
				ImGui::PopStyleColor(3);

				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.03f, 0.45f, 0.03f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.1f, 0.55f, 0.1f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, style.Colors[ImGuiCol_Button]);
				if (ImGui::Button("Y", buttonSize)) {
					if (locked) {
						value = glm::vec3(resetValue);
					} else {
						value.y = resetValue;
					}
				}
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					value = glm::vec3(resetValue);
				}
				ImGui::SameLine();
				if (ImGui::DragFloat("##YValue", &value.y, speed, 0.0f, 0.0f, "%.2f")) {
					if (locked) { value = glm::vec3(value.y); }
				}
				ImGui::PopItemWidth();
				ImGui::SameLine();
				ImGui::PopStyleColor(3);

				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.006f, 0.25f, 0.61f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.03f, 0.35f, 0.79f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, style.Colors[ImGuiCol_Button]);
				if (ImGui::Button("Z", buttonSize)) {
					if (locked) {
						value = glm::vec3(resetValue);
					} else {
						value.z = resetValue;
					}
				}
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					value = glm::vec3(resetValue);
				}
				ImGui::SameLine();
				if (ImGui::DragFloat("##ZValue", &value.z, speed, 0.0f, 0.0f, "%.2f")) {
					if (locked) { value = glm::vec3(value.z); }
				}
				ImGui::PopItemWidth();
				ImGui::PopStyleColor(3);

				if (lock) {
					ImGui::SameLine();
					ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
					if (ImGui::Button(*lock ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN)) { *lock = !*lock; }
					ImGui::PopStyleColor();
				}

				ImGui::PopID();
				ImGui::PopStyleVar();
			};

			if (ImGui::BeginTable("TransformComponent_Properties", 2, ImGuiTableFlags_BordersInnerV)) {
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_WidthFixed, 75.0f);
				ImGui::TableNextRow();
				EditVec3("Translation", cTransform.Translation, 0.1f);
				ImGui::TableNextRow();
				EditVec3("Rotation", cTransform.Rotation, 0.5f);
				ImGui::TableNextRow();
				EditVec3("Scale", cTransform.Scale, 0.1f, 1.0f, &cTransform.LockScale);
				ImGui::EndTable();
			}

			ImGui::Spacing();

			return false;
		},
		[](Entity entity, auto& cTransform) {
			if (ImGui::MenuItem(ICON_FA_ARROW_ROTATE_LEFT " Reset to Identity")) {
				cTransform.Translation = glm::vec3(0.0f);
				cTransform.Rotation    = glm::vec3(0.0f);
				cTransform.Scale       = glm::vec3(1.0f);
			}

			return false;
		});

	// Camera
	DrawComponent<CameraComponent>(
		entity,
		ICON_FA_CAMERA " Camera",
		[](Entity entity, auto& cCamera) {
			auto& camera     = cCamera.Camera;
			float fovDegrees = camera.GetFovDegrees();
			float zNear      = camera.GetZNear();
			float zFar       = camera.GetZFar();

			if (ImGui::BeginTable("CameraComponent_Properties", 2, ImGuiTableFlags_BordersInnerV)) {
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_WidthFixed, 100.0f);

				ImGui::TableNextColumn();
				ImGui::Text("Primary Camera");
				ImGui::TableNextColumn();
				ImGui::Checkbox("##PrimaryCamera", &cCamera.Primary);

				ImGui::TableNextColumn();
				ImGui::Text("Field of View");
				ImGui::TableNextColumn();
				ImGui::DragFloat("##FieldOfView", &fovDegrees, 0.5f, 30.0f, 90.0f, "%.1f deg");

				ImGui::TableNextColumn();
				ImGui::Text("Near Plane");
				ImGui::TableNextColumn();
				ImGui::DragFloat("##NearPlane", &zNear, 0.01f, 0.001f, 10.0f, "%.3f");

				ImGui::TableNextColumn();
				ImGui::Text("Far Plane");
				ImGui::TableNextColumn();
				ImGui::DragFloat("##FarPlane", &zFar, 1.0f, 1.0f, 100'000.0f, "%.2f");

				camera.SetPerspective(fovDegrees, zNear, zFar);

				ImGui::EndTable();
			}

			return false;
		},
		[](Entity entity, auto& cCamera) {
			bool deleted = false;
			if (ImGui::MenuItem(ICON_FA_TRASH_CAN " Remove Component")) { deleted = true; }

			return deleted;
		});

	// Directional Light
	DrawComponent<DirectionalLightComponent>(
		entity,
		ICON_FA_SUN " Directional Light",
		[this](Entity entity, auto& cLight) {
			if (ImGui::BeginTable("DirectionalLightComponent_Properties", 2, ImGuiTableFlags_BordersInnerV)) {
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_WidthFixed, 125.0f);

				ImGui::TableNextColumn();
				ImGui::Text("Radiance");
				ImGui::TableNextColumn();
				ImGui::ColorEdit3("##Radiance", glm::value_ptr(cLight.Radiance));

				ImGui::TableNextColumn();
				ImGui::Text("Intensity");
				ImGui::TableNextColumn();
				ImGui::DragFloat("##Intensity", &cLight.Intensity, 0.5f, 0.01f, 1000.0f, "%.2f");

				ImGui::TableNextColumn();
				ImGui::Text("Cast Shadows");
				ImGui::TableNextColumn();
				ImGui::Checkbox("##CastShadows", &cLight.CastShadows);

				if (cLight.CastShadows) {
					ImGui::TableNextColumn();
					ImGui::Text("Soft Shadows");
					ImGui::TableNextColumn();
					ImGui::Checkbox("##SoftShadows", &cLight.SoftShadows);

					ImGui::TableNextColumn();
					ImGui::Text("Light Size");
					ImGui::TableNextColumn();
					ImGui::DragFloat("##LightSize", &cLight.LightSize, 0.1f, 0.1f, 100.0f, "%.2f");

					ImGui::TableNextColumn();
					ImGui::Text("Shadow Strength");
					ImGui::TableNextColumn();
					float shadowAmount = std::floor(cLight.ShadowAmount * 100.0f);
					if (ImGui::DragFloat("##ShadowAmount", &shadowAmount, 1.0f, 1.0f, 100.0f, "%.0f%%")) {
						cLight.ShadowAmount = std::clamp(shadowAmount, 0.0f, 100.0f) / 100.0f;
					}
				}

				ImGui::EndTable();
			}

			return false;
		},
		[](Entity entity, auto& cCamera) {
			bool deleted = false;
			if (ImGui::MenuItem(ICON_FA_TRASH_CAN " Remove Component")) { deleted = true; }

			return deleted;
		});

	// Mesh
	DrawComponent<MeshComponent>(
		entity,
		ICON_FA_CIRCLE_NODES " Mesh",
		[](Entity entity, auto& cMesh) {
			if (ImGui::BeginTable("MeshComponent_Properties", 2, ImGuiTableFlags_BordersInnerV)) {
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_WidthFixed, 85.0f);
				ImGui::EndTable();
			}

			return false;
		},
		[](Entity entity, auto& cMesh) {
			bool deleted = false;
			if (ImGui::MenuItem(ICON_FA_TRASH_CAN " Remove Component")) { deleted = true; }

			return deleted;
		});

	// Skybox
	DrawComponent<SkyboxComponent>(
		entity,
		ICON_FA_GLOBE " Skybox",
		[this](Entity entity, auto& cSkybox) {
			if (ImGui::BeginTable("SkyboxComponent_Properties", 2, ImGuiTableFlags_BordersInnerV)) {
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_WidthFixed, 125.0f);

				ImGui::EndTable();
			}

			return false;
		},
		[](Entity entity, auto& cCamera) {
			bool deleted = false;
			if (ImGui::MenuItem(ICON_FA_TRASH_CAN " Remove Component")) { deleted = true; }

			return deleted;
		});
}

void SceneHierarchyPanel::SelectionChanged() {
	_debugCSM = false;
}
