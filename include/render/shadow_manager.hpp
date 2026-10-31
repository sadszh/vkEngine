#ifndef SHADOW_MANAGER_H
#define SHADOW_MANAGER_H

#include <render/buffer.hpp>
#include <render/frame_graph.hpp>
#include <render/camera.hpp>
#include <render/render_config.hpp>
#include <array>
#include <entt/entity/entity.hpp>
#include <memory>
#include <vector>

namespace vke_render
{
    struct SpotLight;
    struct DirectionalLight;

    constexpr uint32_t INVALID_SHADOW_LIGHT_INDEX = 0xFFFFFFFFu;

    struct DirectionalShadowInfoCPU
    {
        glm::uvec4 lightIndex;
        glm::uvec4 cascadeCnt;
        glm::mat4 lightViewProj[MAX_DIRECTIONAL_SHADOW_CASCADE_CNT];
        glm::vec4 cascadeSplits;
        glm::vec4 cascadeTexelSizes;

        DirectionalShadowInfoCPU()
            : lightIndex(INVALID_SHADOW_LIGHT_INDEX, 0, 0, 0), cascadeCnt(0), cascadeSplits(0.0f), cascadeTexelSizes(0.0f)
        {
            for (uint32_t i = 0; i < MAX_DIRECTIONAL_SHADOW_CASCADE_CNT; ++i)
                lightViewProj[i] = glm::mat4(1.0f);
        }
    };

    struct SpotShadowInfoCPU
    {
        glm::mat4 lightViewProj;

        SpotShadowInfoCPU() : lightViewProj(1.0f) {}
    };

    class ShadowManager
    {
    public:
        ShadowManager(RenderContext *ctx, FrameGraph &frameGraph, const CameraInfo *cameraInfo,
                      const DirectionalShadowConfig &directionalConfig);
        ~ShadowManager();
        ShadowManager(const ShadowManager &) = delete;
        ShadowManager &operator=(const ShadowManager &) = delete;

        void UpdateDirectionalShadowInfo(const DirectionalLight *sun);
        void CalcSpotShadowVPMatrix(SpotLight &light);
        void SyncDirectionalShadowToGPU(uint32_t currentFrame);
        void SyncSpotShadowToGPU(uint32_t currentFrame);
        uint32_t ActivateSpotShadow(entt::entity lightEntity, SpotLight &light);
        void DeactivateSpotShadow(entt::entity lightEntity, SpotLight &light);
        void ClearLights() { clearLights(); }

        vke_ds::id32_t GetDirectionalShadowMapResourceID() const { return directionalShadowMapResourceID; }
        vke_ds::id32_t GetDirectionalShadowMapResourceNodeID() const { return directionalShadowMapResourceNodeID; }
        vke_ds::id32_t GetSpotShadowMapResourceID() const { return spotShadowMapResourceID; }
        vke_ds::id32_t GetSpotShadowMapResourceNodeID() const { return spotShadowMapResourceNodeID; }
        VkImageView GetDirectionalShadowCascadeView(uint32_t currentFrame, uint32_t cascade) const { return directionalShadowCascadeImageViews[currentFrame][cascade]; }
        VkImageView GetSpotShadowMapLayerView(uint32_t currentFrame, uint32_t slot) const { return spotShadowMapLayerViews[currentFrame][slot]; }
        VkDescriptorSet GetShadowPassDescriptorSet(uint32_t currentFrame) const { return shadowPassDescriptorSets[currentFrame]; }
        VkDescriptorSet GetDeferredLightingDescriptorSet(uint32_t currentFrame) const { return deferredLightingDescriptorSets[currentFrame]; }
        const DirectionalShadowInfoCPU &GetDirectionalShadowInfo() const { return directionalShadowInfo; }
        const SpotShadowInfoCPU &GetSpotShadowInfo(uint32_t slot) const { return spotShadowInfos[slot]; }
        bool IsSpotShadowSlotActive(uint32_t slot) const { return spotShadowLightEntities[slot] != entt::null; }
        const DirectionalShadowConfig &GetDirectionalConfig() const { return directionalConfig; }
        const SpotShadowConfig &GetSpotConfig() const { return spotConfig; }

    private:
        RenderContext *context;
        const CameraInfo *cameraInfo;
        DirectionalShadowConfig directionalConfig;
        SpotShadowConfig spotConfig;
        std::unique_ptr<HostCoherentBuffer> directionalShadowInfoBuffers[MAX_FRAMES_IN_FLIGHT];
        std::unique_ptr<HostCoherentBuffer> spotShadowInfoBuffers[MAX_FRAMES_IN_FLIGHT];
        VkDescriptorSet shadowPassDescriptorSets[MAX_FRAMES_IN_FLIGHT];
        VkDescriptorSet deferredLightingDescriptorSets[MAX_FRAMES_IN_FLIGHT];
        VkImageCreateInfo directionalShadowMapImageCreateInfo;
        VkImageView directionalShadowMapImageViews[MAX_FRAMES_IN_FLIGHT];
        VkImageView directionalShadowCascadeImageViews[MAX_FRAMES_IN_FLIGHT][MAX_DIRECTIONAL_SHADOW_CASCADE_CNT];
        VkImageCreateInfo spotShadowMapImageCreateInfo;
        VkImageView spotShadowMapImageViews[MAX_FRAMES_IN_FLIGHT];
        VkImageView spotShadowMapLayerViews[MAX_FRAMES_IN_FLIGHT][MAX_SPOT_LIGHT_SHADOW_CNT];
        VkSampler shadowMapSampler;
        DirectionalShadowInfoCPU directionalShadowInfo;
        SpotShadowInfoCPU spotShadowInfos[MAX_SPOT_LIGHT_SHADOW_CNT];
        entt::entity spotShadowLightEntities[MAX_SPOT_LIGHT_SHADOW_CNT];
        uint32_t spotShadowUpdateCnts[MAX_SPOT_LIGHT_SHADOW_CNT];
        vke_ds::id32_t directionalShadowMapResourceID;
        vke_ds::id32_t directionalShadowMapResourceNodeID;
        vke_ds::id32_t spotShadowMapResourceID;
        vke_ds::id32_t spotShadowMapResourceNodeID;

        void registerFrameGraphResources(FrameGraph &frameGraph);
        void createDescriptorSets();
        void createDirectionalImageViews(FrameGraph &frameGraph, uint32_t currentFrame);
        void createSpotShadowImageViews(FrameGraph &frameGraph, uint32_t currentFrame);
        void createSampler();
        void updateDeferredLightingDescriptorSet(uint32_t currentFrame);
        void onTransientResourcesReady(FrameGraph &frameGraph, uint32_t currentFrame);
        int32_t findSpotShadowSlot(entt::entity lightEntity) const;
        void clearLights();
    };
}

#endif
