#ifndef EDITOR_CAMERA_H
#define EDITOR_CAMERA_H

#include <common.hpp>
#include <ds/id_allocator.hpp>
#include <render/camera.hpp>
#include <glm/trigonometric.hpp>

namespace vke_editor
{
    class EditorCamera
    {
    public:
        EditorCamera() = default;
        EditorCamera(const EditorCamera &) = delete;
        EditorCamera &operator=(const EditorCamera &) = delete;

        void Update(uint32_t width, uint32_t height, bool viewportHovered, float deltaTime);
        void Deactivate();

    private:
        vke_ds::id32_t cameraId = 0;
        vke_ds::id32_t previousCameraId = 0;
        vke_render::CameraInfo cameraInfo;
        glm::vec3 position = glm::vec3(0.0f, 2.0f, 5.0f);
        float yaw = 0.0f;
        float pitch = glm::radians(-15.0f);
        float moveSpeed = 5.0f;
        bool navigating = false;

        void stopNavigation();
    };
}

#endif
