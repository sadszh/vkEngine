#include <editor/editor.hpp>
#include <editor/builtin_assets.hpp>
#include <component/camera.hpp>
#include <component/light.hpp>
#include <cfloat>
#include <ImGuizmo/ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

namespace vke_editor
{
    namespace
    {
        glm::quat worldOrientation(const entt::registry &registry, entt::entity entity)
        {
            glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
            while (entity != entt::null)
            {
                const auto &transform = registry.get<vke_common::Transform>(entity);
                rotation = transform.localRotation * rotation;
                entity = transform.parent;
            }
            return glm::normalize(rotation);
        }
    }

    void Editor::drawSceneViewport(vke_ui::UIPointerInput &pointer)
    {
        auto *renderer = EditorRenderer::GetInstance();
        const auto *context = &vke_render::RenderEnvironment::GetInstance()->rootRenderContext;
        const auto &sceneRenderContext = *renderer->GetSceneRenderContext();
        ImGui::SetNextWindowSize(
            ImVec2(static_cast<float>(context->width) * 0.75f,
                   static_cast<float>(context->height) * 0.75f),
            ImGuiCond_Once);
        ImGui::SetNextWindowSizeConstraints(ImVec2(256.0f, 144.0f), ImVec2(FLT_MAX, FLT_MAX));
        const bool visible = ImGui::Begin("Scene");
        if (visible && EditorStateManager::GetState() == EditorState::Edit)
            drawSceneToolbar();
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        if (visible && viewportSize.x > 1.0f && viewportSize.y > 1.0f)
        {
            uint32_t width = static_cast<uint32_t>(viewportSize.x);
            uint32_t height = static_cast<uint32_t>(viewportSize.y);
            renderer->ResizeSceneViewport(width, height);
            ImGui::Image(
                ImTextureRef((ImTextureID)renderer->GetSceneTexture()),
                viewportSize);
            const auto origin = ImGui::GetItemRectMin();
            const bool hovered = ImGui::IsItemHovered();
            const bool handled = EditorStateManager::GetState() == EditorState::Edit &&
                                 drawSceneTools(glm::vec2(origin.x, origin.y), glm::vec2(viewportSize.x, viewportSize.y), hovered);
            if (!handled)
            {
                ImGui::SetCursorScreenPos(origin);
                ImGui::InvisibleButton("ScenePointer", viewportSize,
                                       ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            }
            const auto mouse = ImGui::GetMousePos();
            pointer.position = glm::vec2(mouse.x - origin.x, mouse.y - origin.y) *
                               glm::vec2(sceneRenderContext.width, sceneRenderContext.height) / glm::vec2(viewportSize.x, viewportSize.y);
            pointer.inside = hovered && !handled;
        }
        else if (EditorStateManager::GetState() == EditorState::Edit)
            drawSceneTools(glm::vec2(0.0f), glm::vec2(0.0f), false);
        ImGui::End();
    }

    void Editor::drawSceneToolbar()
    {
        ImGui::BeginDisabled(gizmoEntity != entt::null);
        ImGui::SetNextItemWidth(150.0f);
        ImGui::Combo("##TransformTool", &transformTool, "Move (W)\0Rotate (E)\0Scale (R)\0");
        ImGui::SameLine();
        if (transformTool == 2)
            ImGui::TextUnformatted("Local");
        else if (ImGui::Button(localGizmo ? "Local" : "World"))
            localGizmo = !localGizmo;
        ImGui::EndDisabled();
    }

    entt::entity Editor::drawSceneIcons(glm::vec2 origin, glm::vec2 size)
    {
        const auto &camera = vke_render::Renderer::GetCameraInfo();
        const glm::mat4 viewProjection = camera.projection * camera.view;
        const float halfSize = 16.0f * ImGui::GetStyle().FontScaleDpi;
        auto *drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(ImVec2(origin.x, origin.y), ImVec2(origin.x + size.x, origin.y + size.y), true);
        entt::entity hoveredEntity = entt::null;
        const auto mouse = ImGui::GetMousePos();
        const auto drawIcon = [&](entt::entity entity, glm::vec3 position, VkDescriptorSet texture, ImU32 color)
        {
            if (sceneManager->IsPendingDestroy(entity))
                return;
            const glm::vec4 clip = viewProjection * glm::vec4(position, 1.0f);
            if (clip.w <= 0.0f || clip.z < 0.0f || clip.z > clip.w)
                return;
            const glm::vec2 center = origin + (glm::vec2(clip) / clip.w * 0.5f + 0.5f) * size;
            const ImVec2 min(center.x - halfSize, center.y - halfSize);
            const ImVec2 max(center.x + halfSize, center.y + halfSize);
            const ImU32 tint = entity == selectedEntity ? IM_COL32(255, 191, 51, 255) : color;
            drawList->AddImage(ImTextureRef((ImTextureID)texture), min, max, ImVec2(0, 0), ImVec2(1, 1), tint);
            if (mouse.x >= min.x && mouse.x <= max.x && mouse.y >= min.y && mouse.y <= max.y)
                hoveredEntity = entity;
        };
        const auto cameraTexture = getTexturePreviewDescriptorSet(BUILTIN_TEXTURE_CAMERA_ICON_ID);
        for (auto [entity, sceneCamera] : sceneManager->registry.view<vke_component::Camera>().each())
            drawIcon(entity, glm::vec3(sceneCamera.cameraInfo.invView[3]), cameraTexture, IM_COL32(102, 204, 255, 255));
        const auto drawLights = [&]<typename T>(vke_common::AssetHandle icon)
        {
            const auto texture = getTexturePreviewDescriptorSet(icon);
            for (auto [entity, transform] : sceneManager->registry.view<T, vke_common::Transform>().each())
                drawIcon(entity, transform.GetGlobalPosition(), texture, IM_COL32(255, 240, 160, 255));
        };
        drawLights.operator()<vke_component::DirectionalLight>(BUILTIN_TEXTURE_DIRECTIONAL_LIGHT_ICON_ID);
        drawLights.operator()<vke_component::PointLight>(BUILTIN_TEXTURE_POINT_LIGHT_ICON_ID);
        drawLights.operator()<vke_component::SpotLight>(BUILTIN_TEXTURE_SPOT_LIGHT_ICON_ID);
        drawList->PopClipRect();
        return hoveredEntity;
    }

    void Editor::pickEntity(glm::vec2 position, glm::vec2 size)
    {
        const auto *context = EditorRenderer::GetInstance()->GetSceneRenderContext();
        const glm::uvec2 extent(context->width, context->height);
        const glm::uvec2 pixel = glm::min(glm::uvec2(position / size * glm::vec2(extent)), extent - 1u);
        if (!entityPicker)
            entityPicker = std::make_unique<EntityPicker>(context->depthFormat);
        clearSelectedAsset();
        selectedEntity = entityPicker->Pick(*sceneManager, vke_render::Renderer::GetCameraInfo(), extent, pixel);
    }

    void Editor::cancelGizmo()
    {
        ImGuizmo::Enable(false);
        if (gizmoEntity == entt::null)
            return;
        if (sceneManager->registry.valid(gizmoEntity) && !sceneManager->IsPendingDestroy(gizmoEntity))
        {
            auto &transforms = sceneManager->transformSystem;
            if (transformTool == 0)
                transforms.SetLocalPosition(gizmoEntity, gizmoStart.localPosition, true);
            else if (transformTool == 1)
                transforms.SetLocalRotation(gizmoEntity, gizmoStart.localRotation, true);
            else
                transforms.SetLocalScale(gizmoEntity, gizmoStart.localScale, true);
        }
        gizmoEntity = entt::null;
    }

    bool Editor::drawSceneTools(glm::vec2 origin, glm::vec2 size, bool hovered)
    {
        using vke_common::InputManager;
        const auto *context = EditorRenderer::GetInstance()->GetSceneRenderContext();
        editorCamera.Update(context->width, context->height,
                            hovered && !ImGuizmo::IsOver() && gizmoEntity == entt::null,
                            vke_common::TimeManager::GetDeltaTime());
        ensureSelectedEntityValid();
        const bool visible = size.x > 1.0f && size.y > 1.0f;
        auto *window = vke_render::RenderEnvironment::GetInstance()->window;
        const bool focused = glfwGetWindowAttrib(window, GLFW_FOCUSED) &&
                             !InputManager::IsFocusLostThisFrame();
        const auto hoveredIcon = visible ? drawSceneIcons(origin, size) : entt::null;
        if (!visible || !focused || InputManager::IsKeyPressed(GLFW_KEY_ESCAPE) ||
            (gizmoEntity != entt::null && gizmoEntity != selectedEntity))
        {
            cancelGizmo();
            return false;
        }
        const bool navigating = gizmoEntity == entt::null &&
                                (InputManager::IsMouseButtonDown(GLFW_MOUSE_BUTTON_RIGHT) ||
                                 InputManager::GetCursorMode() == GLFW_CURSOR_DISABLED);
        if (hovered && !navigating && gizmoEntity == entt::null &&
            !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive())
        {
            if (InputManager::IsKeyPressed(GLFW_KEY_W))
                transformTool = 0;
            if (InputManager::IsKeyPressed(GLFW_KEY_E))
                transformTool = 1;
            if (InputManager::IsKeyPressed(GLFW_KEY_R))
                transformTool = 2;
        }

        bool handled = false;
        if (selectedEntity != entt::null && !sceneManager->IsPendingDestroy(selectedEntity))
        {
            auto &transform = sceneManager->registry.get<vke_common::Transform>(selectedEntity);
            const glm::quat parentRotation = worldOrientation(sceneManager->registry, transform.parent);
            const bool singularParent = transform.parent != entt::null &&
                                        glm::determinant(sceneManager->registry.get<vke_common::Transform>(transform.parent).model) == 0.0f;
            if (gizmoEntity == entt::null)
            {
                gizmoMatrix = glm::translate(glm::mat4(1.0f), transform.GetGlobalPosition()) *
                              glm::mat4_cast(parentRotation * transform.localRotation);
            }
            const auto &camera = vke_render::Renderer::GetCameraInfo();
            glm::mat4 projection = camera.projection;
            projection[1][1] *= -1.0f;
            const ImGuizmo::OPERATION operation = transformTool == 0 ? ImGuizmo::TRANSLATE : transformTool == 1 ? ImGuizmo::ROTATE
                                                                                                                : ImGuizmo::SCALE;
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::AllowAxisFlip(false);
            ImGuizmo::Enable(!navigating && !singularParent && (hovered || gizmoEntity != entt::null));
            vke_common::TransformData before;
            transform.FillData(before);
            auto *drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(ImVec2(origin.x, origin.y), ImVec2(origin.x + size.x, origin.y + size.y), true);
            const bool changed = ImGuizmo::Manipulate(glm::value_ptr(camera.view), glm::value_ptr(projection),
                                                      operation, transformTool == 2 || localGizmo ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
                                                      glm::value_ptr(gizmoMatrix));
            drawList->PopClipRect();
            const bool usingGizmo = ImGuizmo::IsUsing();
            handled = !navigating && (ImGuizmo::IsOver(operation) || usingGizmo);
            if (usingGizmo && gizmoEntity == entt::null)
            {
                gizmoEntity = selectedEntity;
                gizmoStart = before;
            }
            if (changed && gizmoEntity != entt::null)
            {
                auto &transforms = sceneManager->transformSystem;
                if (transformTool == 0)
                    transforms.SetGlobalPosition(gizmoEntity, glm::vec3(gizmoMatrix[3]), true);
                else if (transformTool == 1)
                    transforms.SetLocalRotation(gizmoEntity,
                                                glm::inverse(parentRotation) * glm::normalize(glm::quat_cast(glm::mat3(gizmoMatrix))), true);
                else
                {
                    const glm::vec3 factor(glm::length(glm::vec3(gizmoMatrix[0])),
                                           glm::length(glm::vec3(gizmoMatrix[1])),
                                           glm::length(glm::vec3(gizmoMatrix[2])));
                    transforms.SetLocalScale(gizmoEntity, gizmoStart.localScale * factor, true);
                }
            }
            if (!usingGizmo)
                gizmoEntity = entt::null;
            if (singularParent)
                ImGui::GetWindowDrawList()->AddText(ImVec2(origin.x + 8, origin.y + 8),
                                                    IM_COL32(255, 200, 80, 255), "Gizmo unavailable: parent has zero scale");
        }
        else
            cancelGizmo();

        if (hovered && !navigating && !handled && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const auto mouse = ImGui::GetMousePos();
            if (hoveredIcon != entt::null)
            {
                clearSelectedAsset();
                selectedEntity = hoveredIcon;
            }
            else
                pickEntity(glm::vec2(mouse.x, mouse.y) - origin, size);
        }
        return handled;
    }
}
