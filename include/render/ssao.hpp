#ifndef SSAO_H
#define SSAO_H

#include <render/pipeline.hpp>
#include <render/subpass.hpp>
#include <render/gbuffer.hpp>
#include <render/render_config.hpp>
#include <asset/asset_manager.hpp>

namespace vke_render
{
    class SSAOPass : public RenderPassBase
    {
    public:
        static constexpr VkFormat SSAO_FORMAT = VK_FORMAT_R8_UNORM;

        SSAOPass(RenderContext *ctx, VkDescriptorSet *globalDescriptorSets, const nlohmann::json &configJSON)
            : RenderPassBase(SSAO_PASS, ctx, globalDescriptorSets),
              gbuffer(nullptr),
              sampler(VK_NULL_HANDLE),
              ssaoResourceID(0),
              ssaoRawResourceID(0),
              ssaoTaskNodeID(0),
              ssaoBlurTaskNodeID(0),
              constants(configJSON)
        {
            for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
            {
                imageViews[i] = VK_NULL_HANDLE;
                rawImageViews[i] = VK_NULL_HANDLE;
            }
            sampler = VulkanResourceManager::GetSampler(
                VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                false, VK_SAMPLER_MIPMAP_MODE_NEAREST, 0.0f);

            imageCreateInfo = VkImageCreateInfo{};
            imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
            imageCreateInfo.extent = {context->width, context->height, 1};
            imageCreateInfo.mipLevels = 1;
            imageCreateInfo.arrayLayers = 1;
            imageCreateInfo.format = SSAO_FORMAT;
            imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                    VK_IMAGE_USAGE_SAMPLED_BIT |
                                    VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
            imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        ~SSAOPass() override;

        void Init(int subpassID,
                  FrameGraph &frameGraph,
                  std::map<std::string, vke_ds::id32_t> &blackboard,
                  ResourceNodeIDMap &currentResourceNodeID) override
        {
            RenderPassBase::Init(subpassID, frameGraph, blackboard, currentResourceNodeID);
            gbuffer = GBuffer::GetInstance();
            auto ssaoShaderResult = vke_common::AssetManager::LoadVertFragShaderUnique(vke_common::BUILTIN_VFSHADER_SSAO_ID);
            VKE_FATAL_IF(!ssaoShaderResult, "{}", ssaoShaderResult.error())
            ssaoShader = std::move(*ssaoShaderResult);
            auto ssaoBlurShaderResult = vke_common::AssetManager::LoadVertFragShaderUnique(vke_common::BUILTIN_VFSHADER_SSAO_BLUR_ID);
            VKE_FATAL_IF(!ssaoBlurShaderResult, "{}", ssaoBlurShaderResult.error())
            ssaoBlurShader = std::move(*ssaoBlurShaderResult);
            constructFrameGraph(frameGraph, blackboard, currentResourceNodeID);
            allocateDescriptorSet();
            createGraphicsPipeline();
        }

        void Render(TaskNode &node, FrameGraph &frameGraph, VkCommandBuffer commandBuffer, uint32_t currentFrame, uint32_t imageIndex) override;
        void OnWindowResize(FrameGraph &frameGraph, RenderContext *ctx) override
        {
            context = ctx;
            cleanupImageViews();
            imageCreateInfo.extent = {context->width, context->height, 1};
            frameGraph.SetTransientImageCreateInfo(ssaoResourceID, imageCreateInfo);
            frameGraph.SetTransientImageCreateInfo(ssaoRawResourceID, imageCreateInfo);
        }

        VkSampler GetOutputSampler() const { return sampler; }
        VkImageView GetOutputImageView(uint32_t currentFrame) const { return imageViews[currentFrame]; }
        vke_ds::id32_t GetResourceID() const { return ssaoResourceID; }

    private:
        VkDescriptorSet ssaoDescriptorSets[MAX_FRAMES_IN_FLIGHT];
        VkDescriptorSet ssaoBlurDescriptorSets[MAX_FRAMES_IN_FLIGHT];
        std::unique_ptr<GraphicsPipeline> renderPipeline;
        std::unique_ptr<GraphicsPipeline> blurPipeline;
        std::shared_ptr<ShaderModuleSet> ssaoShader;
        std::shared_ptr<ShaderModuleSet> ssaoBlurShader;
        GBuffer *gbuffer;
        VkImageCreateInfo imageCreateInfo;
        VkSampler sampler;
        VkImageView imageViews[MAX_FRAMES_IN_FLIGHT];
        VkImageView rawImageViews[MAX_FRAMES_IN_FLIGHT];
        vke_ds::id32_t ssaoResourceID;
        vke_ds::id32_t ssaoRawResourceID;
        vke_ds::id32_t ssaoTaskNodeID;
        vke_ds::id32_t ssaoBlurTaskNodeID;
        SSAOConfig constants;

        void constructFrameGraph(FrameGraph &frameGraph,
                                 std::map<std::string, vke_ds::id32_t> &blackboard,
                                 ResourceNodeIDMap &currentResourceNodeID);
        void allocateDescriptorSet();
        void createGraphicsPipeline();
        void onSSAORawResourcesReady(FrameGraph &frameGraph, uint32_t currentFrame);
        void onSSAOBlurResourcesReady(FrameGraph &frameGraph, uint32_t currentFrame);
        void renderFullscreen(VkCommandBuffer commandBuffer, uint32_t currentFrame, VkImageView outputImageView,
                              GraphicsPipeline *pipeline, VkDescriptorSet descriptorSet, const float clearValue);
        void createImageView(FrameGraph &frameGraph, uint32_t currentFrame);
        void cleanupImageViews();
    };
}

#endif
