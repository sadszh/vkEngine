#include <editor/camera.hpp>
#include <render/render.hpp>
#include <input.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace vke_editor
{
    void EditorCamera::stopNavigation()
    {
        if (navigating)
        {
            vke_common::InputManager::SetCursorMode(GLFW_CURSOR_NORMAL);
            navigating = false;
        }
    }

    void EditorCamera::Deactivate()
    {
        stopNavigation();
        if (cameraId == 0)
            return;
        vke_render::Renderer::RemoveCamera(cameraId);
        vke_render::Renderer::SetCurrentCamera(previousCameraId);
        cameraId = 0;
    }

    void EditorCamera::Update(uint32_t width, uint32_t height, bool viewportHovered, float deltaTime)
    {
        using vke_common::InputManager;
        using vke_render::Renderer;
        auto *window = vke_render::RenderEnvironment::GetInstance()->window;
        const bool focused = glfwGetWindowAttrib(window, GLFW_FOCUSED) &&
                             !glfwGetWindowAttrib(window, GLFW_ICONIFIED) &&
                             !InputManager::IsFocusLostThisFrame();
        bool changed = cameraId == 0;
        if (!focused || !InputManager::IsMouseButtonDown(GLFW_MOUSE_BUTTON_RIGHT) ||
            InputManager::IsKeyPressed(GLFW_KEY_ESCAPE))
            stopNavigation();
        else if (!navigating && viewportHovered && InputManager::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT))
        {
            InputManager::SetCursorMode(GLFW_CURSOR_DISABLED);
            navigating = true;
        }
        else if (navigating)
        {
            const glm::vec2 delta = InputManager::GetMouseDelta();
            yaw -= delta.x * 0.003f;
            pitch = glm::clamp(pitch - delta.y * 0.003f, glm::radians(-89.0f), glm::radians(89.0f));
            changed |= delta.x != 0.0f || delta.y != 0.0f;
        }

        const glm::vec3 forward(-std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                                -std::cos(yaw) * std::cos(pitch));
        const glm::vec3 right(std::cos(yaw), 0.0f, -std::sin(yaw));
        if (navigating)
        {
            moveSpeed = glm::clamp(moveSpeed * std::pow(1.2f, InputManager::GetScrollDelta().y), 0.1f, 1000.0f);
            glm::vec3 movement(0.0f);
            if (InputManager::IsKeyDown(GLFW_KEY_W)) movement += forward;
            if (InputManager::IsKeyDown(GLFW_KEY_S)) movement -= forward;
            if (InputManager::IsKeyDown(GLFW_KEY_D)) movement += right;
            if (InputManager::IsKeyDown(GLFW_KEY_A)) movement -= right;
            if (InputManager::IsKeyDown(GLFW_KEY_E)) movement.y += 1.0f;
            if (InputManager::IsKeyDown(GLFW_KEY_Q)) movement.y -= 1.0f;
            if (glm::dot(movement, movement) > 0.0f)
            {
                const float boost = InputManager::IsKeyDown(GLFW_KEY_LEFT_SHIFT) ||
                                    InputManager::IsKeyDown(GLFW_KEY_RIGHT_SHIFT) ? 4.0f : 1.0f;
                // Avoid a large jump after a stalled frame while navigating.
                position += glm::normalize(movement) * moveSpeed * boost * std::min(deltaTime, 0.1f);
                changed = true;
            }
        }

        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        if (changed || cameraInfo.aspect != aspect)
        {
            glm::mat4 projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 1000.0f);
            projection[1][1] *= -1.0f;
            cameraInfo = vke_render::CameraInfo(0.1f, 1000.0f, glm::radians(60.0f), aspect,
                glm::lookAt(position, position + forward, glm::vec3(0.0f, 1.0f, 0.0f)), projection, position);
            if (cameraId != 0)
                Renderer::UpdateCameraInfo(cameraInfo);
        }
        if (cameraId == 0)
        {
            previousCameraId = Renderer::GetInstance()->currentCamera;
            cameraId = Renderer::RegisterCamera([this]() { Renderer::UpdateCameraInfo(cameraInfo); });
        }
        Renderer::SetCurrentCamera(cameraId);
    }
}
