#ifndef BLOOM_H
#define BLOOM_H

#include <render/pipeline.hpp>
#include <render/subpass.hpp>
#include <render/hdr_color.hpp>
#include <render/render_config.hpp>
#include <asset/asset_manager.hpp>
#include <functional>

namespace vke_render
{
    class BloomPass : public RenderPassBase
    {
    public:
        BloomPass(RenderContext *ctx, VkDescriptorSet *globalDescriptorSets, HDRColorManager *hdrColorManager, const nlohmann::json &configJSON)
            : RenderPassBase(BLOOM_PASS, ctx, globalDescriptorSets),
              hdrColorManager(hdrColorManager),
              constants(configJSON) {}
        ~BloomPass() override = default;

        void Init(int subpassID,
                  FrameGraph &frameGraph,
                  std::map<std::string, vke_ds::id32_t> &blackboard,
                  ResourceNodeIDMap &currentResourceNodeID) override
        {
            RenderPassBase::Init(subpassID, frameGraph, blackboard, currentResourceNodeID);
            auto bloomShaderResult = vke_common::AssetManager::LoadVertFragShaderUnique(vke_common::BUILTIN_VFSHADER_BLOOM_ID);
            VKE_FATAL_IF(!bloomShaderResult, "{}", bloomShaderResult.error())
            bloomShader = std::move(*bloomShaderResult);
            constructFrameGraph(frameGraph, blackboard, currentResourceNodeID);
            allocateDescriptorSet();
            createGraphicsPipeline();
        }

        void Render(TaskNode &node, FrameGraph &frameGraph, VkCommandBuffer commandBuffer, uint32_t currentFrame, uint32_t imageIndex) override;
        void OnWindowResize(FrameGraph &, RenderContext *ctx) override { context = ctx; }

    private:
        VkDescriptorSet bloomDescriptorSets[MAX_FRAMES_IN_FLIGHT];
        std::unique_ptr<GraphicsPipeline> renderPipeline;
        std::shared_ptr<ShaderModuleSet> bloomShader;
        HDRColorManager *hdrColorManager;
        vke_ds::id32_t bloomTaskNodeID;
        uint32_t inputHDRColorImageIndex;
        uint32_t outputHDRColorImageIndex;
        BloomConfig constants;

        void constructFrameGraph(FrameGraph &frameGraph,
                                 std::map<std::string, vke_ds::id32_t> &blackboard,
                                 ResourceNodeIDMap &currentResourceNodeID);
        void allocateDescriptorSet();
        void createGraphicsPipeline();
        void onTransientResourcesReady(FrameGraph &frameGraph, uint32_t currentFrame);
    };
}

#endif
