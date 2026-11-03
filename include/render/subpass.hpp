#ifndef SUBPASS_H
#define SUBPASS_H

#include <render/frame_graph.hpp>

namespace vke_render
{
    enum PassType
    {
        CUSTOM_RENDERER,
        GBUFFER_PASS,
        SHADOW_PASS,
        SSAO_PASS,
        DEFERRED_LIGHTING_PASS,
        SKYBOX_RENDERER,
        ATMOSPHERE_PASS,
        TRANSPARENT_PASS,
        BLOOM_PASS,
        TONE_MAPPING_PASS,
        LAYERED_2D_RENDERER
    };

    class RenderPassBase
    {
    public:
        PassType type;
        int subpassID;
        RenderContext *context;
        VkDescriptorSet *globalDescriptorSets;

        RenderPassBase(PassType type, RenderContext *ctx, VkDescriptorSet *globalDescriptorSets)
            : type(type), context(ctx), globalDescriptorSets(globalDescriptorSets) {}

        virtual ~RenderPassBase() {}

        virtual void Reset() {}

        // virtual void RegisterCamera(VkBuffer buffer) = 0;
        virtual void Render(TaskNode &node, FrameGraph &frameGraph, VkCommandBuffer commandBuffer, uint32_t currentFrame, uint32_t imageIndex) = 0;

        virtual void Init(int subpassID,
                          FrameGraph &frameGraph,
                          std::map<std::string, vke_ds::id32_t> &blackboard,
                          ResourceNodeIDMap &currentResourceNodeID)
        {
            this->subpassID = subpassID;
        }

        virtual void OnWindowResize(FrameGraph &frameGraph, RenderContext *ctx) {}
    };
}

#endif
