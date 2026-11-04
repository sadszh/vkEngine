#include <editor/editor.hpp>
#include <editor/gizmo.hpp>
#include <component/camera.hpp>
#include <imgui_internal.h>
#include <ImGuizmo/ImGuizmo.h>
#include <engine.hpp>
#include <asset/asset_db_sqlite.hpp>
#include <audio/audio_manager.hpp>
#include <glm/common.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/trigonometric.hpp>
#include <cstring>
#include <fstream>

namespace vke_editor
{
    static void BuildDefaultDockLayout(ImGuiID dockspaceId)
    {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        ImGuiID mainDock = dockspaceId;
        ImGuiID leftDock = 0;
        ImGuiID rightDock = 0;
        ImGuiID bottomDock = 0;

        ImGui::DockBuilderSplitNode(mainDock, ImGuiDir_Left, 0.20f, &leftDock, &mainDock);
        ImGui::DockBuilderSplitNode(mainDock, ImGuiDir_Right, 0.25f, &rightDock, &mainDock);
        ImGui::DockBuilderSplitNode(mainDock, ImGuiDir_Down, 0.25f, &bottomDock, &mainDock);

        ImGui::DockBuilderDockWindow("Hierarchy", leftDock);
        ImGui::DockBuilderDockWindow("Inspector", rightDock);
        ImGui::DockBuilderDockWindow("Assets", bottomDock);
        ImGui::DockBuilderDockWindow("Log", bottomDock);
        ImGui::DockBuilderDockWindow("Scene", mainDock);
        ImGui::DockBuilderFinish(dockspaceId);
    }

    EditorConfig *EditorConfig::instance = nullptr;
    EditorStateManager *EditorStateManager::instance;
    Editor *Editor::instance = nullptr;

    Editor *Editor::GetInstance()
    {
        VKE_FATAL_IF(instance == nullptr, "Editor not initialized!")
        return instance;
    }

    Editor *Editor::Init(GLFWwindow *window,
                         const EditorConfig &editorConfig,
                         uint32_t sceneViewportWidth,
                         uint32_t sceneViewportHeight,
                         std::vector<vke_render::PassType> &passes,
                         std::vector<std::unique_ptr<vke_render::RenderPassBase>> &customPasses)
    {
        instance = new Editor();
        instance->selectedEntity = entt::null;
        instance->selectedAssetType = vke_common::ASSET_CNT_FLAG;
        instance->selectedAsset = 0;
        instance->assetBrowserMode = AssetBrowserMode::ByType;
        vke_common::EventSystem::Init();
        vke_common::TimeManager::Init();
        vke_common::InputManager::Init(window);
        vke_common::EngineStateManager::Init();
        vke_common::EngineStateManager::SetState(vke_common::EngineState::Paused);
        vke_audio::AudioManager::Init();
        vke_editor::EditorStateManager::Init(vke_editor::EditorState::Edit);
        vke_render::RenderEnvironment::Init(window, editorConfig.gameConfig->enableVulkanValidationLayers);
        vke_render::VulkanResourceManager::Init();
        vke_common::AssetManager::Init(
            std::make_unique<vke_common::AssetDBSQLite>(
                EditorConfig::GetFullPath(editorConfig.assetDBPath),
                vke_common::CUSTOM_ASSET_ID_ST),
            editorConfig.workingDirectory);
        VKE_FATAL_IF(!vke_common::AssetManager::BulkLoad(EditorAssetLUTPath, true),
                     "Failed to load editor assets from {}", EditorAssetLUTPath.string())
        vke_render::RenderContext *ctx = &(vke_render::RenderEnvironment::GetInstance()->rootRenderContext);
        EditorRenderer::Init(window, ctx, sceneViewportWidth, sceneViewportHeight);

        vke_physics::PhysicsManager::Init(editorConfig.gameConfig->physicsConfig);
        vke_render::Renderer::Init(EditorRenderer::GetInstance()->GetSceneRenderContext(),
                                   passes, customPasses, editorConfig.gameConfig->renderConfig);
        vke_common::ScriptManager::Init();
        instance->sceneManager = vke_common::SceneManager::Init();
        vke_ui::UIManager::Init(instance->sceneManager->registry);
        vke_editor::EditorStateManager::SetState(vke_editor::EditorState::Edit);

        return instance;
    }

    void Editor::Shutdown()
    {
        vke_render::Renderer::Shutdown();
    }

    void Editor::WaitIdle()
    {
        vke_render::Renderer::WaitIdle();
    }

    void Editor::Dispose()
    {
        instance->editorCamera.Deactivate();
        instance->entityPicker.reset();
        instance->disposeTexturePreviewDescriptorSets();
        instance->editSnapshot.reset();
        vke_ui::UIManager::Dispose();
        vke_common::SceneManager::Dispose();
        vke_common::ScriptManager::Dispose();
        vke_render::Renderer::Dispose();
        vke_physics::PhysicsManager::Dispose();
        EditorRenderer::Dispose();
        vke_common::AssetManager::Dispose();
        vke_render::VulkanResourceManager::Dispose();
        vke_render::RenderEnvironment::Dispose();
        vke_audio::AudioManager::Dispose();
        vke_editor::EditorStateManager::Dispose();
        vke_common::EngineStateManager::Dispose();
        vke_common::InputManager::Dispose();
        vke_common::TimeManager::Dispose();
        vke_common::EventSystem::Dispose();
        delete instance;
    }

    void Editor::OnWindowResize(GLFWwindow *window, int width, int height)
    {
        vke_common::EventSystem::DispatchEvent(vke_common::EVENT_WINDOW_RESIZE, nullptr);
    }

    vke_common::SceneResult<void> Editor::startRun()
    {
        if (EditorStateManager::GetState() != EditorState::Edit || editSnapshot)
            return std::unexpected("editor is not ready to start");
        sceneManager->ProcessDestroyRequests();
        cancelGizmo();
        auto snapshot = sceneManager->ExportAllEntities();
        if (!snapshot)
            return std::unexpected(snapshot.error());
        editSnapshot.emplace(std::move(*snapshot));
        editorCamera.Deactivate();
        fixedUpdateAccumulator = 0.0f;
        vke_common::InputManager::Reset();
        vke_common::TimeManager::Reset();
        EditorStateManager::SetState(EditorState::Run);
        vke_common::EngineStateManager::SetState(vke_common::EngineState::Running);
        sceneManager->Start();
        return {};
    }

    vke_common::SceneResult<void> Editor::stopRun()
    {
        if (EditorStateManager::GetState() != EditorState::Run || !editSnapshot)
            return std::unexpected("no edit snapshot to restore");
        vke_common::SceneManager::Reset();
        selectedEntity = entt::null; // Runtime handles may now refer to different entities.
        fixedUpdateAccumulator = 0.0f;
        vke_common::InputManager::Reset();
        vke_common::InputManager::SetCursorMode(GLFW_CURSOR_NORMAL);
        auto restored = vke_common::SceneManager::Instantiate(*editSnapshot);
        vke_common::TimeManager::Reset();
        // Keep the snapshot and remain read-only on failure; Stop can retry restoration.
        if (!restored)
            return std::unexpected(restored.error());
        editSnapshot.reset();
        EditorStateManager::SetState(EditorState::Edit);
        return {};
    }

    bool Editor::Update()
    {
        if (vke_common::InputManager::IsKeyPressed(GLFW_KEY_F6))
            toggleRunRequested = true;
        if (toggleRunRequested)
        {
            toggleRunRequested = false;
            auto result = EditorStateManager::GetState() == EditorState::Edit ? startRun() : stopRun();
            if (!result)
                VKE_LOG_ERROR("Cannot switch editor state: {}", result.error());
        }
        const vke_common::EngineState engineState = vke_common::EngineStateManager::GetState();
        if (engineState == vke_common::EngineState::Terminated)
        {
            Shutdown();
            return false;
        }

        vke_common::TimeManager::Update();
        sceneManager->ProcessDestroyRequests();

        if (vke_editor::EditorStateManager::GetState() == vke_editor::EditorState::Run &&
            engineState == vke_common::EngineState::Running)
        {
            vke_common::Engine::UpdateSimulation(
                vke_common::TimeManager::GetDeltaTime(), fixedUpdateAccumulator);
        }

        auto *editorRenderer = EditorRenderer::GetInstance();
        vke_ui::UIPointerInput pointer;
        editorRenderer->BeginFrame();
        drawGUI(pointer);
        using vke_common::InputManager;
        auto *window = vke_render::RenderEnvironment::GetInstance()->window;
        pointer.active = EditorStateManager::GetState() == EditorState::Run &&
                         vke_common::EngineStateManager::GetState() != vke_common::EngineState::Terminated &&
                         glfwGetWindowAttrib(window, GLFW_FOCUSED) && !glfwGetWindowAttrib(window, GLFW_ICONIFIED) &&
                         !InputManager::IsFocusLostThisFrame() && InputManager::GetCursorMode() != GLFW_CURSOR_DISABLED;
        pointer.down = InputManager::IsMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT);
        pointer.pressed = InputManager::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
        pointer.released = InputManager::IsMouseButtonReleased(GLFW_MOUSE_BUTTON_LEFT);
        vke_common::Engine::UpdatePresentation(&pointer);
        std::vector<glm::vec3> gizmoLines;
        std::vector<GizmoBatch> gizmoBatches;
        extractCollisionLines(*sceneManager, selectedEntity, gizmoLines);
        if (!gizmoLines.empty())
            gizmoBatches.push_back({0, static_cast<uint32_t>(gizmoLines.size()), glm::vec4(0.0f, 1.0f, 0.0f, 1.0f)});
        if (EditorStateManager::GetState() == EditorState::Edit)
        {
            const auto firstVertex = static_cast<uint32_t>(gizmoLines.size());
            if (const auto *sceneCamera = sceneManager->registry.try_get<vke_component::Camera>(selectedEntity);
                sceneCamera && !sceneManager->IsPendingDestroy(selectedEntity))
                appendFrustumLines(sceneCamera->cameraInfo, gizmoLines);
            extractLightLines(*sceneManager, *vke_render::Renderer::GetInstance()->lightManager, selectedEntity, gizmoLines);
            const auto vertexCount = static_cast<uint32_t>(gizmoLines.size()) - firstVertex;
            if (vertexCount != 0)
                gizmoBatches.push_back({firstVertex, vertexCount, glm::vec4(1.0f, 0.75f, 0.2f, 1.0f)});
        }
        const auto &camera = vke_render::Renderer::GetCameraInfo();
        editorRenderer->Update(camera.projection * camera.view, gizmoLines, gizmoBatches);
        vke_common::InputManager::EndFrame();
        return true;
    }

    void Editor::drawGUI(vke_ui::UIPointerInput &pointer)
    {
        ImGuizmo::BeginFrame();
        ImGuiID dockspaceId = ImGui::DockSpaceOverViewport();
        static bool defaultDockLayoutBuilt = false;
        if (!defaultDockLayoutBuilt)
        {
            BuildDefaultDockLayout(dockspaceId);
            defaultDockLayoutBuilt = true;
        }

        if (EditorStateManager::GetState() == EditorState::Edit || EditorStateManager::GetState() == EditorState::Run)
            drawSceneViewport(pointer);

        ensureSelectedEntityValid();
        showMainMenuBar();
        showProjectSettings();
        if (EditorStateManager::GetState() == EditorState::Edit)
            showAssetImportDialog();
        showHierarchy();
        showInspector();
        showAssets();
        showLog();
    }

    void Editor::showMainMenuBar()
    {
        if (!ImGui::BeginMainMenuBar())
            return;

        const bool canEdit = EditorStateManager::GetState() == EditorState::Edit;
        if (ImGui::MenuItem("Project Settings"))
        {
            projectSettingsOpen = true;
            ImGui::SetWindowFocus("Project Settings");
        }
        if (ImGui::BeginMenu("Scene", canEdit))
        {
            if (ImGui::MenuItem("Save Scene"))
            {
                const std::string &scenePath =
                    EditorConfig::GetInstance()->gameConfig->defaultScenePath;
                if (!scenePath.empty())
                {
                    auto data = sceneManager->ExportAllEntities();
                    if (data)
                    {
                        auto json = data->ToJSON();
                        if (json)
                        {
                            std::ofstream ofs(scenePath);
                            ofs << json->dump(4);
                        }
                        else
                            VKE_LOG_ERROR("Cannot save scene: {}", json.error());
                    }
                    else
                        VKE_LOG_ERROR("Cannot save scene: {}", data.error());
                }
            }

            if (ImGui::BeginMenu("New Object"))
            {
                if (ImGui::MenuItem("Empty"))
                    createEmptyObject();
                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Asset", canEdit))
        {
            if (ImGui::MenuItem("Import Mesh"))
                openAssetImport(vke_common::ASSET_MESH);
            if (ImGui::MenuItem("Import VF Shader"))
                openAssetImport(vke_common::ASSET_VF_SHADER);
            if (ImGui::MenuItem("Import Compute Shader"))
                openAssetImport(vke_common::ASSET_COMPUTE_SHADER);
            if (ImGui::MenuItem("Import Skeleton"))
                openAssetImport(vke_common::ASSET_SKELETON);
            if (ImGui::MenuItem("Import Audio Clip"))
                openAssetImport(vke_common::ASSET_AUDIO_CLIP);
            if (ImGui::MenuItem("Import Texture"))
                openAssetImport(vke_common::ASSET_TEXTURE);
            if (ImGui::MenuItem("Import Animation"))
                openAssetImport(vke_common::ASSET_ANIMATION);
            if (ImGui::MenuItem("Import Font"))
                openAssetImport(vke_common::ASSET_FONT);
            ImGui::EndMenu();
        }

        const vke_editor::EditorState editorState = vke_editor::EditorStateManager::GetState();
        if (editorState == vke_editor::EditorState::Edit || editorState == vke_editor::EditorState::Run)
        {
            const bool currentIsEdit = editorState == vke_editor::EditorState::Edit;
            const char *buttonLabel = currentIsEdit ? "Start (F6)" : "Stop (F6)";
            const ImGuiStyle &style = ImGui::GetStyle();
            const float buttonWidth = ImGui::CalcTextSize(buttonLabel).x + style.FramePadding.x * 2.0f;
            const float centeredX = (ImGui::GetWindowWidth() - buttonWidth) * 0.5f;
            const float nextX = glm::max(ImGui::GetCursorPosX() + style.ItemSpacing.x, centeredX);

            ImGui::SameLine(nextX);
            if (ImGui::Button(buttonLabel, ImVec2(buttonWidth, 0.0f)))
                toggleRunRequested = true;
        }

        ImGui::EndMainMenuBar();
    }

    void Editor::showProjectSettings()
    {
        if (!projectSettingsOpen)
            return;

        ImGui::SetNextWindowSize(ImVec2(680.0f, 420.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Project Settings", &projectSettingsOpen))
        {
            if (ImGui::BeginTabBar("ProjectSettingsTabs"))
            {
                if (ImGui::BeginTabItem("Physics"))
                {
                    ImGui::SeparatorText("Physics Layers");
                    const bool canEdit = EditorStateManager::GetState() == EditorState::Edit;
                    if (!canEdit)
                        ImGui::TextWrapped("Stop the run to edit physics layers.");

                    const auto &config = vke_physics::PhysicsManager::GetConfig();
                    ImGui::BeginDisabled(!canEdit);
                    if (ImGui::BeginTable("PhysicsLayerMappings", 2,
                                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchSame))
                    {
                        ImGui::TableSetupColumn("Object Layer");
                        ImGui::TableSetupColumn("Broad Phase Layer");
                        ImGui::TableHeadersRow();
                        for (uint32_t objectLayer = 0; objectLayer < config.objectLayerCount; ++objectLayer)
                        {
                            ImGui::PushID(static_cast<int>(objectLayer));
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::AlignTextToFramePadding();
                            ImGui::Text("%u: %s", objectLayer, config.objectLayerNames[objectLayer].c_str());
                            ImGui::TableNextColumn();
                            const auto currentLayer = config.objectToBroadPhase[objectLayer].GetValue();
                            const auto preview = std::to_string(currentLayer) + ": " + config.broadPhaseLayerNames[currentLayer];
                            ImGui::SetNextItemWidth(-FLT_MIN);
                            if (ImGui::BeginCombo("##BroadPhaseLayer", preview.c_str()))
                            {
                                for (uint32_t broadPhaseLayer = 0; broadPhaseLayer < config.broadPhaseLayerCount; ++broadPhaseLayer)
                                {
                                    const auto target = JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(broadPhaseLayer));
                                    const auto label = std::to_string(broadPhaseLayer) + ": " + config.broadPhaseLayerNames[broadPhaseLayer];
                                    const bool selected = config.objectToBroadPhase[objectLayer] == target;
                                    if (ImGui::Selectable(label.c_str(), selected))
                                        vke_physics::PhysicsManager::SetObjectLayerBroadPhaseLayer(objectLayer, target);
                                    if (selected)
                                        ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                    ImGui::EndDisabled();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }

    void Editor::showHierarchy()
    {
        ImGui::Begin("Hierarchy");

        ImGuiTreeNodeFlags commonFlags = ImGuiTreeNodeFlags_OpenOnArrow |
                                         ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                         ImGuiTreeNodeFlags_SpanAvailWidth;

        auto hierarchyView = sceneManager->registry.view<vke_common::GameObject, vke_common::Transform>();
        for (const entt::entity entity : hierarchyView)
        {
            const vke_common::Transform &transform = sceneManager->registry.get<vke_common::Transform>(entity);
            if (transform.parent == entt::null)
                drawHierarchyEntity(entity, commonFlags);
        }

        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
        {
            clearSelectedAsset();
            selectedEntity = entt::null;
        }

        ImGui::End();
    }

    void Editor::drawHierarchyEntity(entt::entity entity, ImGuiTreeNodeFlags commonFlags)
    {
        if (!sceneManager->registry.valid(entity))
            return;

        auto [object, transform] = sceneManager->registry.get<vke_common::GameObject, vke_common::Transform>(entity);
        ImGuiTreeNodeFlags flags = commonFlags;
        if (entity == selectedEntity)
            flags |= ImGuiTreeNodeFlags_Selected;
        if (transform.children.empty())
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
        const bool opened = ImGui::TreeNodeEx(object.name.c_str(), flags);
        if (ImGui::IsItemClicked())
        {
            clearSelectedAsset();
            selectedEntity = entity;
        }

        if (opened && !transform.children.empty())
        {
            for (entt::entity child : transform.children)
                drawHierarchyEntity(child, commonFlags);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void Editor::showInspector()
    {
        if (!ImGui::Begin("Inspector"))
        {
            ImGui::End();
            return;
        }
        const bool canEdit = EditorStateManager::GetState() == EditorState::Edit;
        if (!canEdit)
            ImGui::TextDisabled("Running - read only");
        ImGui::BeginDisabled(!canEdit);

        if (selectedAsset != 0)
        {
            if (selectedAssetType == vke_common::ASSET_TEXTURE)
                showSelectedTextureInspector();
            else if (selectedAssetType == vke_common::ASSET_MATERIAL)
                showSelectedMaterialInspector();
            else
                showSelectedAssetInspector();
            ImGui::EndDisabled();
            ImGui::End();
            return;
        }

        if (selectedEntity == entt::null || !sceneManager->registry.valid(selectedEntity) ||
            !sceneManager->registry.all_of<vke_common::GameObject, vke_common::Transform>(selectedEntity))
        {
            ImGui::TextUnformatted("No object selected");
            ImGui::EndDisabled();
            ImGui::End();
            return;
        }

        vke_common::GameObject &object = sceneManager->registry.get<vke_common::GameObject>(selectedEntity);
        vke_common::Transform &transform = sceneManager->registry.get<vke_common::Transform>(selectedEntity);

        char nameBuffer[128]{};
        std::strncpy(nameBuffer, object.name.c_str(), sizeof(nameBuffer) - 1);
        if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
            object.name = nameBuffer;

        ImGui::Checkbox("Static", &object.isStatic);

        if (ImGui::TreeNodeEx("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            glm::vec3 position = transform.localPosition;
            glm::vec3 rotation = glm::degrees(glm::eulerAngles(transform.localRotation));
            glm::vec3 scale = transform.localScale;
            const bool updatePhysicsComponents =
                vke_editor::EditorStateManager::GetState() == vke_editor::EditorState::Edit;

            if (ImGui::InputFloat3("Position", glm::value_ptr(position)))
                sceneManager->transformSystem.SetLocalPosition(selectedEntity, position, updatePhysicsComponents);
            if (ImGui::InputFloat3("Rotation", glm::value_ptr(rotation)))
                sceneManager->transformSystem.SetLocalRotation(selectedEntity, glm::quat(glm::radians(rotation)), updatePhysicsComponents);
            if (ImGui::InputFloat3("Scale", glm::value_ptr(scale)))
                sceneManager->transformSystem.SetLocalScale(selectedEntity, scale, updatePhysicsComponents);

            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Components", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (sceneManager->registry.all_of<vke_component::CharacterController>(selectedEntity))
                ImGui::BulletText("CharacterController");
            ImGui::TreePop();
        }

        drawCameraComponent();
        drawLightComponents();
        drawRenderableObjectComponent();
        drawSkeletonAnimatorComponent();
        drawRigidBodyComponent();
        drawSensorComponent();
        drawCanvasComponent();
        drawAudioSourceComponent();
        drawAudioListenerComponent();
        showComponentMenu();
        ImGui::EndDisabled();
        // Script trees remain navigable while their values are read-only.
        drawScriptComponents();
        ImGui::End();
    }

    void Editor::showComponentMenu()
    {
        ImGui::Separator();
        struct ComponentMenuItem
        {
            const char *name;
            vke_common::ComponentType type;
        };
        static const ComponentMenuItem items[] = {
            {"Camera", vke_common::ComponentType::Camera},
            {"RenderableObject", vke_common::ComponentType::RenderableObject},
            {"SkeletonAnimator", vke_common::ComponentType::SkeletonAnimator},
            {"RigidBody", vke_common::ComponentType::RigidBody},
            {"Sensor", vke_common::ComponentType::Sensor},
            {"CharacterController", vke_common::ComponentType::CharacterController},
            {"DirectionalLight", vke_common::ComponentType::DirectionalLight},
            {"PointLight", vke_common::ComponentType::PointLight},
            {"SpotLight", vke_common::ComponentType::SpotLight},
            {"Canvas", vke_common::ComponentType::Canvas},
            {"AudioSource", vke_common::ComponentType::AudioSource},
            {"AudioListener", vke_common::ComponentType::AudioListener}};

        ImGui::BeginDisabled(sceneManager->IsPendingDestroy(selectedEntity));
        if (ImGui::BeginCombo("Add Component", "Select component"))
        {
            bool available = false;
            for (const auto &item : items)
            {
                if (sceneManager->HasComponent(selectedEntity, item.type))
                    continue;
                available = true;
                const bool needsAssets = item.type == vke_common::ComponentType::SkeletonAnimator;
                ImGui::BeginDisabled(needsAssets);
                if (ImGui::Selectable(item.name))
                {
                    if (auto result = sceneManager->AddComponent(selectedEntity, item.type); !result)
                        VKE_LOG_ERROR("Failed to add {}: {}", item.name, result.error());
                }
                ImGui::EndDisabled();
                if (needsAssets && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Requires material, mesh and skeleton; use the SceneManager data overload.");
            }
            if (!available)
                ImGui::TextDisabled("No components available");
            ImGui::EndCombo();
        }
        if (ImGui::BeginCombo("Remove Component", "Select component"))
        {
            bool available = false;
            for (const auto &item : items)
            {
                if (!sceneManager->HasComponent(selectedEntity, item.type))
                    continue;
                available = true;
                if (ImGui::Selectable(item.name))
                {
                    if (auto result = sceneManager->RemoveComponent(selectedEntity, item.type); !result)
                        VKE_LOG_ERROR("Failed to remove {}: {}", item.name, result.error());
                }
            }
            if (!available)
                ImGui::TextDisabled("No removable components");
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
    }

    void Editor::createEmptyObject()
    {
        std::string name = "GameObject";
        clearSelectedAsset();
        selectedEntity = sceneManager->CreateEntity(name, glm::vec3(0.0f), glm::vec3(1.0f), glm::quat(glm::vec3(0.0f)), false);
    }

    void Editor::ensureSelectedEntityValid()
    {
        if (selectedEntity == entt::null || !sceneManager->registry.valid(selectedEntity))
            selectedEntity = entt::null;
    }
}
