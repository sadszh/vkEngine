#ifndef LIGHTING_PASS_H
#define LIGHTING_PASS_H

#include <render/skybox.hpp>
#include <render/gbuffer.hpp>
#include <render/renderinfo.hpp>
#include <render/subpass.hpp>
#include <render/light_manager.hpp>
#include <render/shadow_manager.hpp>
#include <render/hdr_color.hpp>
#include <asset/asset_manager.hpp>
#include <functional>

namespace vke_render
{
    class DeferredLightingPass : public RenderPassBase
    {
    public:
        DeferredLightingPass(RenderContext *ctx, VkDescriptorSet *globalDescriptorSets, LightManager *lightManager, SkyboxManager *skyboxManager, HDRColorManager *hdrColorManager, ShadowManager *shadowManager)
            : RenderPassBase(DEFERRED_LIGHTING_PASS, ctx, globalDescriptorSets), lightManager(lightManager), skyboxManager(skyboxManager),
              hdrColorManager(hdrColorManager), shadowManager(shadowManager),
              ssaoSampler(VK_NULL_HANDLE),
              ssaoImageViewGetter(nullptr) {}

        void Init(int subpassID,
                  FrameGraph &frameGraph,
                  std::map<std::string, vke_ds::id32_t> &blackboard,
                  ResourceNodeIDMap &currentResourceNodeID) override
        {
            RenderPassBase::Init(subpassID, frameGraph, blackboard, currentResourceNodeID);
            gbuffer = GBuffer::GetInstance();
            auto lightingShaderResult = vke_common::AssetManager::LoadVertFragShaderUnique(vke_common::BUILTIN_VFSHADER_DEFERRED_LIGHTING_ID);
            VKE_FATAL_IF(!lightingShaderResult, "{}", lightingShaderResult.error())
            lightingShader = std::move(*lightingShaderResult);
            auto brdfLUTResult = vke_common::AssetManager::LoadTexture2D(vke_common::BUILTIN_TEXTURE_BRDF_LUT_ID);
            VKE_FATAL_IF(!brdfLUTResult, "{}", brdfLUTResult.error())
            brdfLUT = std::move(*brdfLUTResult);
            auto defaultOcclusionTextureResult = vke_common::AssetManager::LoadTexture2D(vke_common::BUILTIN_TEXTURE_DEFAULT_ID);
            VKE_FATAL_IF(!defaultOcclusionTextureResult, "{}", defaultOcclusionTextureResult.error())
            defaultOcclusionTexture = std::move(*defaultOcclusionTextureResult);
            constructFrameGraph(frameGraph, blackboard, currentResourceNodeID);
            allocateDescriptorSet();
            createGraphicsPipeline();
        }

        ~DeferredLightingPass() {}

        void Render(TaskNode &node, FrameGraph &frameGraph, VkCommandBuffer commandBuffer, uint32_t currentFrame, uint32_t imageIndex) override;
        void OnWindowResize(FrameGraph &frameGraph, RenderContext *ctx) override;
        void SetSSAOInput(VkSampler sampler, std::function<VkImageView(uint32_t)> imageViewGetter)
        {
            ssaoSampler = sampler;
            ssaoImageViewGetter = std::move(imageViewGetter);
        }

    private:
        GBuffer *gbuffer;
        LightManager *lightManager;
        SkyboxManager *skyboxManager;
        HDRColorManager *hdrColorManager;
        ShadowManager *shadowManager;
        VkDescriptorSet lightingDescriptorSets[MAX_FRAMES_IN_FLIGHT];
        std::unique_ptr<GraphicsPipeline> renderPipeline;
        std::shared_ptr<ShaderModuleSet> lightingShader;
        std::shared_ptr<Texture2D> brdfLUT;
        std::shared_ptr<Texture2D> defaultOcclusionTexture;
        VkSampler ssaoSampler;
        std::function<VkImageView(uint32_t)> ssaoImageViewGetter;
        vke_ds::id32_t lightingTaskNodeID;
        uint32_t hdrColorImageIndex;

        void constructFrameGraph(FrameGraph &frameGraph,
                                 std::map<std::string, vke_ds::id32_t> &blackboard,
                                 ResourceNodeIDMap &currentResourceNodeID);
        void allocateDescriptorSet();
        void updateDescriptorSet(uint32_t currentFrame);
        void createGraphicsPipeline();
        void onTransientResourcesReady(FrameGraph &frameGraph, uint32_t currentFrame);
    };
};

#endif
