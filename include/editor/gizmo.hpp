#ifndef EDITOR_GIZMO_H
#define EDITOR_GIZMO_H

#include <render/camera.hpp>
#include <entt/entity/entity.hpp>
#include <vector>

namespace vke_common
{
    class SceneManager;
}

namespace vke_render
{
    class LightManager;
}

namespace vke_editor
{
    void appendFrustumLines(const vke_render::CameraInfo &camera, std::vector<glm::vec3> &lines);
    void extractLightLines(const vke_common::SceneManager &scene, const vke_render::LightManager &lights,
                           entt::entity entity, std::vector<glm::vec3> &lines);
    void extractCollisionLines(const vke_common::SceneManager &scene, entt::entity entity,
                               std::vector<glm::vec3> &outVertices);
}

#endif
