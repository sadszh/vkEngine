#ifndef EDITOR_ENTITY_PICKER_H
#define EDITOR_ENTITY_PICKER_H

#include <render/pipeline.hpp>
#include <render/camera.hpp>
#include <entt/entity/entity.hpp>
#include <glm/vec2.hpp>
#include <map>
#include <memory>

namespace vke_common { class SceneManager; }

namespace vke_editor
{
    class EntityPicker
    {
    public:
        explicit EntityPicker(VkFormat depthFormat);
        entt::entity Pick(vke_common::SceneManager &scene, const vke_render::CameraInfo &camera,
                          glm::uvec2 extent, glm::uvec2 pixel);

    private:
        VkFormat depthFormat;
        std::shared_ptr<vke_render::ShaderModuleSet> shaders[2];
        std::map<std::pair<bool, uint32_t>, std::unique_ptr<vke_render::GraphicsPipeline>> pipelines;

        vke_render::GraphicsPipeline &getPipeline(bool skinned, uint32_t stride);
    };
}

#endif
