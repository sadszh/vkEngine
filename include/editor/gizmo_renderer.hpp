#ifndef EDITOR_GIZMO_RENDERER_H
#define EDITOR_GIZMO_RENDERER_H

#include <render/buffer.hpp>
#include <render/pipeline.hpp>
#include <span>

namespace vke_editor
{
    struct GizmoBatch
    {
        uint32_t firstVertex;
        uint32_t vertexCount;
        glm::vec4 color;
    };

    class GizmoRenderer
    {
    public:
        explicit GizmoRenderer(vke_render::RenderContext *ctx);

        void Render(VkCommandBuffer commandBuffer, uint32_t currentFrame,
                    const glm::mat4 &viewProj, std::span<const glm::vec3> lines,
                    std::span<const GizmoBatch> batches);

    private:
        struct PushConstants
        {
            glm::mat4 viewProj;
            glm::vec4 color;
        };

        vke_render::RenderContext *context;
        std::shared_ptr<vke_render::ShaderModuleSet> shader;
        std::unique_ptr<vke_render::GraphicsPipeline> pipeline;
        std::unique_ptr<vke_render::HostCoherentBuffer> vertexBuffers[vke_render::MAX_FRAMES_IN_FLIGHT];

        void createGraphicsPipeline();
    };
}

#endif
