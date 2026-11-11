#ifndef TONE_MAPPING_H
#define TONE_MAPPING_H

#include <render/pipeline.hpp>
#include <render/subpass.hpp>
#include <render/hdr_color.hpp>
#include <render/render_config.hpp>
#include <asset/asset_manager.hpp>
#include <functional>

namespace vke_render
{
    class ToneMappingPass : public RenderPassBase
    {
    public:
        ToneMappingPass(RenderContext *ctx, VkDescriptorSet *globalDescriptorSets, HDRColorManager *hdrColorManager, const nlohmann::json &configJSON)
            : RenderPassBase(TONE_MAPPING_PASS, ctx, globalDescriptorSets), hdrColorManager(hdrColorManager), constants(configJSON) {}

        void Init(int subpassID,
                  FrameGraph &frameGraph,
                  std::map<std::string, vke_ds::id32_t> &blackboard,
                  ResourceNodeIDMap &currentResourceNodeID) override
        {
            RenderPassBase::Init(subpassID, frameGraph, blackboard, currentResourceNodeID);
            auto toneMappingShaderResult = vke_common::AssetManager::LoadVertFragShaderUnique(vke_common::BUILTIN_VFSHADER_TONE_MAPPING_ID);
            VKE_FATAL_IF(!toneMappingShaderResult, "{}", toneMappingShaderResult.error())
            toneMappingShader = std::move(*toneMappingShaderResult);
            constructFrameGraph(frameGraph, blackboard, currentResourceNodeID);
            allocateDescriptorSet();
            createGraphicsPipeline();
        }

        void Render(TaskNode &node, FrameGraph &frameGraph, VkCommandBuffer commandBuffer, uint32_t currentFrame, uint32_t imageIndex) override;
        void OnWindowResize(FrameGraph &frameGraph, RenderContext *ctx) override {}

    private:
        VkDescriptorSet toneMappingDescriptorSets[MAX_FRAMES_IN_FLIGHT];
        std::unique_ptr<GraphicsPipeline> renderPipeline;
        std::shared_ptr<ShaderModuleSet> toneMappingShader;
        HDRColorManager *hdrColorManager;
        vke_ds::id32_t toneMappingTaskNodeID;
        uint32_t inputHDRColorImageIndex;
        ToneMappingConfig constants;

        void constructFrameGraph(FrameGraph &frameGraph,
                                 std::map<std::string, vke_ds::id32_t> &blackboard,
                                 ResourceNodeIDMap &currentResourceNodeID);
        void allocateDescriptorSet();
        void createGraphicsPipeline();
        void onTransientResourcesReady(FrameGraph &frameGraph, uint32_t currentFrame);
    };
}

#endif
