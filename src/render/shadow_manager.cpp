#include <render/shadow_manager.hpp>
#include <render/light.hpp>
#include <asset/asset_manager.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>

#ifdef near
#undef near
#endif

#ifdef far
#undef far
#endif

namespace vke_render
{
    static float CalculateCascadeSplit(float cameraNear, float clipRange, float cameraFar, float splitProgress, float splitLambda)
    {
        float logSplit = cameraNear * std::pow(cameraFar / cameraNear, splitProgress);
        float uniformSplit = cameraNear + clipRange * splitProgress;
        return splitLambda * logSplit + (1.0f - splitLambda) * uniformSplit;
    }

    static std::array<glm::vec3, 8> CalculateCascadeCorners(const CameraInfo &cam, float nearClip, float farClip)
    {
        float tanHalfFov = std::tan(cam.fov * 0.5f);
        float nearHalfHeight = nearClip * tanHalfFov;
        float nearHalfWidth = nearHalfHeight * cam.aspect;
        float farHalfHeight = farClip * tanHalfFov;
        float farHalfWidth = farHalfHeight * cam.aspect;

        std::array<glm::vec3, 8> viewCorners = {
            glm::vec3(-nearHalfWidth, -nearHalfHeight, -nearClip),
            glm::vec3(-nearHalfWidth, nearHalfHeight, -nearClip),
            glm::vec3(nearHalfWidth, -nearHalfHeight, -nearClip),
            glm::vec3(nearHalfWidth, nearHalfHeight, -nearClip),
            glm::vec3(-farHalfWidth, -farHalfHeight, -farClip),
            glm::vec3(-farHalfWidth, farHalfHeight, -farClip),
            glm::vec3(farHalfWidth, -farHalfHeight, -farClip),
            glm::vec3(farHalfWidth, farHalfHeight, -farClip),
        };

        std::array<glm::vec3, 8> worldCorners;
        for (uint32_t i = 0; i < viewCorners.size(); ++i)
            worldCorners[i] = glm::vec3(cam.invView * glm::vec4(viewCorners[i], 1.0f));
        return worldCorners;
    }

    static float CalculateStableCascadeRadius(float fov, float aspect, float nearClip, float farClip)
    {
        float tanHalfFov = std::tan(fov * 0.5f);
        float nearHalfHeight = nearClip * tanHalfFov;
        float nearHalfWidth = nearHalfHeight * aspect;
        float farHalfHeight = farClip * tanHalfFov;
        float farHalfWidth = farHalfHeight * aspect;
        float centerZ = -(nearClip + farClip) * 0.5f;

        std::array<glm::vec3, 8> viewCorners = {
            glm::vec3(-nearHalfWidth, -nearHalfHeight, -nearClip),
            glm::vec3(-nearHalfWidth, nearHalfHeight, -nearClip),
            glm::vec3(nearHalfWidth, -nearHalfHeight, -nearClip),
            glm::vec3(nearHalfWidth, nearHalfHeight, -nearClip),
            glm::vec3(-farHalfWidth, -farHalfHeight, -farClip),
            glm::vec3(-farHalfWidth, farHalfHeight, -farClip),
            glm::vec3(farHalfWidth, -farHalfHeight, -farClip),
            glm::vec3(farHalfWidth, farHalfHeight, -farClip),
        };

        glm::vec3 center(0.0f, 0.0f, centerZ);
        float radius = 0.0f;
        for (const glm::vec3 &corner : viewCorners)
            radius = std::max(radius, glm::length(corner - center));
        return std::ceil(radius * 16.0f) / 16.0f;
    }

    ShadowManager::ShadowManager(RenderContext *ctx, FrameGraph &frameGraph, const CameraInfo *cameraInfo,
                                 const DirectionalShadowConfig &directionalConfig)
        : context(ctx), cameraInfo(cameraInfo), directionalConfig(directionalConfig),
          shadowMapSampler(VK_NULL_HANDLE), directionalShadowMapResourceID(0), directionalShadowMapResourceNodeID(0),
          spotShadowMapResourceID(0), spotShadowMapResourceNodeID(0), directionalShadowInfo()
    {
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            directionalShadowInfoBuffers[i] = std::make_unique<HostCoherentBuffer>(sizeof(DirectionalShadowInfoCPU), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
            spotShadowInfoBuffers[i] = std::make_unique<HostCoherentBuffer>(sizeof(SpotShadowInfoCPU) * MAX_SPOT_LIGHT_SHADOW_CNT, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
            shadowPassDescriptorSets[i] = VK_NULL_HANDLE;
            deferredLightingDescriptorSets[i] = VK_NULL_HANDLE;
            directionalShadowMapImageViews[i] = VK_NULL_HANDLE;
            spotShadowMapImageViews[i] = VK_NULL_HANDLE;
            for (uint32_t cascade = 0; cascade < MAX_DIRECTIONAL_SHADOW_CASCADE_CNT; ++cascade)
                directionalShadowCascadeImageViews[i][cascade] = VK_NULL_HANDLE;
            for (uint32_t slot = 0; slot < MAX_SPOT_LIGHT_SHADOW_CNT; ++slot)
                spotShadowMapLayerViews[i][slot] = VK_NULL_HANDLE;
        }

        for (uint32_t slot = 0; slot < MAX_SPOT_LIGHT_SHADOW_CNT; ++slot)
        {
            spotShadowLightEntities[slot] = entt::null;
            spotShadowUpdateCnts[slot] = 0;
        }

        directionalShadowMapImageCreateInfo = VkImageCreateInfo{};
        directionalShadowMapImageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        directionalShadowMapImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        directionalShadowMapImageCreateInfo.extent = {directionalConfig.mapSize, directionalConfig.mapSize, 1};
        directionalShadowMapImageCreateInfo.mipLevels = 1;
        directionalShadowMapImageCreateInfo.arrayLayers = directionalConfig.cascadeCnt;
        directionalShadowMapImageCreateInfo.format = context->depthFormat;
        directionalShadowMapImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        directionalShadowMapImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        directionalShadowMapImageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        directionalShadowMapImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        directionalShadowMapImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        spotShadowMapImageCreateInfo = VkImageCreateInfo{};
        spotShadowMapImageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        spotShadowMapImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        spotShadowMapImageCreateInfo.extent = {spotConfig.mapSize, spotConfig.mapSize, 1};
        spotShadowMapImageCreateInfo.mipLevels = 1;
        spotShadowMapImageCreateInfo.arrayLayers = MAX_SPOT_LIGHT_SHADOW_CNT;
        spotShadowMapImageCreateInfo.format = context->depthFormat;
        spotShadowMapImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        spotShadowMapImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        spotShadowMapImageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        spotShadowMapImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        spotShadowMapImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        createSampler();
        createDescriptorSets();
        registerFrameGraphResources(frameGraph);
        frameGraph.AddTransientReadyCallback(std::bind(&ShadowManager::onTransientResourcesReady, this, std::placeholders::_1, std::placeholders::_2));
    }

    ShadowManager::~ShadowManager()
    {
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            if (directionalShadowMapImageViews[i] != VK_NULL_HANDLE)
            {
                vkDestroyImageView(globalLogicalDevice, directionalShadowMapImageViews[i], nullptr);
                directionalShadowMapImageViews[i] = VK_NULL_HANDLE;
            }
            for (uint32_t cascade = 0; cascade < MAX_DIRECTIONAL_SHADOW_CASCADE_CNT; ++cascade)
            {
                if (directionalShadowCascadeImageViews[i][cascade] != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(globalLogicalDevice, directionalShadowCascadeImageViews[i][cascade], nullptr);
                    directionalShadowCascadeImageViews[i][cascade] = VK_NULL_HANDLE;
                }
            }
            if (spotShadowMapImageViews[i] != VK_NULL_HANDLE)
                vkDestroyImageView(globalLogicalDevice, spotShadowMapImageViews[i], nullptr);
            for (uint32_t slot = 0; slot < MAX_SPOT_LIGHT_SHADOW_CNT; ++slot)
            {
                if (spotShadowMapLayerViews[i][slot] != VK_NULL_HANDLE)
                    vkDestroyImageView(globalLogicalDevice, spotShadowMapLayerViews[i][slot], nullptr);
            }
        }
    }

    void ShadowManager::createDescriptorSets()
    {
        std::shared_ptr<ShaderModuleSet> shadowShader = vke_common::AssetManager::LoadVertFragShader(vke_common::BUILTIN_VFSHADER_SHADOW_ID);
        std::shared_ptr<ShaderModuleSet> lightingShader = vke_common::AssetManager::LoadVertFragShader(vke_common::BUILTIN_VFSHADER_DEFERRED_LIGHTING_ID);

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            shadowPassDescriptorSets[i] = shadowShader->CreateDescriptorSet(0);
            deferredLightingDescriptorSets[i] = lightingShader->CreateDescriptorSet(2);

            VkDescriptorBufferInfo bufferInfos[2] = {
                directionalShadowInfoBuffers[i]->GetDescriptorBufferInfo(),
                spotShadowInfoBuffers[i]->GetDescriptorBufferInfo()};
            VkWriteDescriptorSet descriptorWrites[4];
            ConstructDescriptorSetWrite(descriptorWrites[0], shadowPassDescriptorSets[i], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &bufferInfos[0]);
            ConstructDescriptorSetWrite(descriptorWrites[1], shadowPassDescriptorSets[i], 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &bufferInfos[1]);
            ConstructDescriptorSetWrite(descriptorWrites[2], deferredLightingDescriptorSets[i], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &bufferInfos[0]);
            ConstructDescriptorSetWrite(descriptorWrites[3], deferredLightingDescriptorSets[i], 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &bufferInfos[1]);
            vkUpdateDescriptorSets(globalLogicalDevice, 4, descriptorWrites, 0, nullptr);
        }
    }

    void ShadowManager::SyncDirectionalShadowToGPU(uint32_t currentFrame)
    {
        directionalShadowInfoBuffers[currentFrame]->ToBuffer(0, &directionalShadowInfo, sizeof(DirectionalShadowInfoCPU));
    }

    void
    ShadowManager::SyncSpotShadowToGPU(uint32_t currentFrame)
    {
        for (uint32_t slot = 0; slot < MAX_SPOT_LIGHT_SHADOW_CNT; ++slot)
        {
            if (spotShadowUpdateCnts[slot] == 0)
                continue;
            --spotShadowUpdateCnts[slot];
            spotShadowInfoBuffers[currentFrame]->ToBuffer(sizeof(SpotShadowInfoCPU) * slot, &spotShadowInfos[slot], sizeof(SpotShadowInfoCPU));
        }
    }

    void ShadowManager::clearLights()
    {
        directionalShadowInfo = DirectionalShadowInfoCPU();
        for (uint32_t slot = 0; slot < MAX_SPOT_LIGHT_SHADOW_CNT; ++slot)
        {
            spotShadowLightEntities[slot] = entt::null;
            spotShadowInfos[slot] = SpotShadowInfoCPU();
            spotShadowUpdateCnts[slot] = MAX_FRAMES_IN_FLIGHT;
        }
    }

    void ShadowManager::registerFrameGraphResources(FrameGraph &frameGraph)
    {
        directionalShadowMapResourceID = frameGraph.AddTransientImageResource(
            "directionalShadowMap0", directionalShadowMapImageCreateInfo, VK_IMAGE_ASPECT_DEPTH_BIT,
            1, directionalConfig.cascadeCnt);
        directionalShadowMapResourceNodeID = frameGraph.AllocResourceNode("directionalShadowMap0Out", directionalShadowMapResourceID);

        spotShadowMapResourceID = frameGraph.AddTransientImageResource(
            "spotShadowMap", spotShadowMapImageCreateInfo, VK_IMAGE_ASPECT_DEPTH_BIT,
            1, MAX_SPOT_LIGHT_SHADOW_CNT);
        spotShadowMapResourceNodeID = frameGraph.AllocResourceNode("spotShadowMapOut", spotShadowMapResourceID);
    }

    void ShadowManager::createDirectionalImageViews(FrameGraph &frameGraph, uint32_t currentFrame)
    {
        if (directionalShadowMapImageViews[currentFrame] != VK_NULL_HANDLE)
            vkDestroyImageView(globalLogicalDevice, directionalShadowMapImageViews[currentFrame], nullptr);
        VkImage image = frameGraph.GetImageResource(directionalShadowMapResourceID).images[currentFrame];
        directionalShadowMapImageViews[currentFrame] = RenderEnvironment::CreateImageView(image, context->depthFormat,
                                                                                          VK_IMAGE_ASPECT_DEPTH_BIT, 1, 0,
                                                                                          directionalConfig.cascadeCnt, VK_IMAGE_VIEW_TYPE_2D_ARRAY);
        for (uint32_t cascade = 0; cascade < directionalConfig.cascadeCnt; ++cascade)
        {
            if (directionalShadowCascadeImageViews[currentFrame][cascade] != VK_NULL_HANDLE)
                vkDestroyImageView(globalLogicalDevice, directionalShadowCascadeImageViews[currentFrame][cascade], nullptr);
            directionalShadowCascadeImageViews[currentFrame][cascade] = RenderEnvironment::CreateImageView(image,
                                                                                                           context->depthFormat,
                                                                                                           VK_IMAGE_ASPECT_DEPTH_BIT,
                                                                                                           1, cascade, 1,
                                                                                                           VK_IMAGE_VIEW_TYPE_2D);
        }
    }

    void ShadowManager::createSpotShadowImageViews(FrameGraph &frameGraph, uint32_t currentFrame)
    {
        VkImage image = frameGraph.GetImageResource(spotShadowMapResourceID).images[currentFrame];
        if (spotShadowMapImageViews[currentFrame] != VK_NULL_HANDLE)
            vkDestroyImageView(globalLogicalDevice, spotShadowMapImageViews[currentFrame], nullptr);
        spotShadowMapImageViews[currentFrame] = RenderEnvironment::CreateImageView(
            image, context->depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT,
            1, 0, MAX_SPOT_LIGHT_SHADOW_CNT, VK_IMAGE_VIEW_TYPE_2D_ARRAY);

        for (uint32_t slot = 0; slot < MAX_SPOT_LIGHT_SHADOW_CNT; ++slot)
        {
            if (spotShadowMapLayerViews[currentFrame][slot] != VK_NULL_HANDLE)
                vkDestroyImageView(globalLogicalDevice, spotShadowMapLayerViews[currentFrame][slot], nullptr);
            spotShadowMapLayerViews[currentFrame][slot] = RenderEnvironment::CreateImageView(
                image, context->depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT,
                1, slot, 1, VK_IMAGE_VIEW_TYPE_2D);
        }
    }

    void ShadowManager::createSampler()
    {
        SamplerDesc desc;
        desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        desc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        desc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        desc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        desc.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        desc.compareEnable = true;
        desc.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        desc.maxLod = 0.0f;
        shadowMapSampler = VulkanResourceManager::GetSampler(desc);
    }

    void ShadowManager::updateDeferredLightingDescriptorSet(uint32_t currentFrame)
    {
        VkDescriptorImageInfo imageInfos[2] = {
            {shadowMapSampler, directionalShadowMapImageViews[currentFrame], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {shadowMapSampler, spotShadowMapImageViews[currentFrame], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkWriteDescriptorSet descriptorWrites[2];
        ConstructDescriptorSetWrite(descriptorWrites[0], deferredLightingDescriptorSets[currentFrame], 1,
                                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfos[0]);
        ConstructDescriptorSetWrite(descriptorWrites[1], deferredLightingDescriptorSets[currentFrame], 3,
                                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfos[1]);
        vkUpdateDescriptorSets(globalLogicalDevice, 2, descriptorWrites, 0, nullptr);
    }

    void ShadowManager::UpdateDirectionalShadowInfo(const DirectionalLight *sun)
    {
        if (sun == nullptr)
        {
            directionalShadowInfo = DirectionalShadowInfoCPU();
            return;
        }

        glm::vec3 lightDir = glm::normalize(glm::vec3(sun->direction));
        if (glm::length(lightDir) < 0.0001f)
            lightDir = glm::vec3(0.0f, -1.0f, 0.0f);

        const CameraInfo &cam = *cameraInfo;
        const float cameraNear = std::max(cam.near, 0.01f);
        const float cameraFar = std::max(cameraNear + 0.01f, std::min(cam.far, directionalConfig.maxDistance));
        const float clipRange = cameraFar - cameraNear;

        float lastSplitDist = 0.0f;
        glm::vec3 up = std::abs(glm::dot(lightDir, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.98f
                           ? glm::vec3(0.0f, 0.0f, 1.0f)
                           : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 lightRight = glm::normalize(glm::cross(lightDir, up));
        glm::vec3 lightUp = glm::normalize(glm::cross(lightRight, lightDir));

        directionalShadowInfo.lightIndex = glm::uvec4(0, 0, 0, 0);
        directionalShadowInfo.cascadeCnt = glm::uvec4(directionalConfig.cascadeCnt, 0, 0, 0);
        directionalShadowInfo.cascadeSplits = glm::vec4(0.0f);
        directionalShadowInfo.cascadeTexelSizes = glm::vec4(0.0f);

        for (uint32_t cascade = 0; cascade < directionalConfig.cascadeCnt; ++cascade)
        {
            float p = static_cast<float>(cascade + 1) / static_cast<float>(directionalConfig.cascadeCnt);
            float split = CalculateCascadeSplit(cameraNear, clipRange, cameraFar, p, directionalConfig.splitLambda);
            float splitDist = (split - cameraNear) / clipRange;
            float nearClip = cameraNear + clipRange * lastSplitDist;
            float farClip = cameraNear + clipRange * splitDist;
            std::array<glm::vec3, 8> frustumCorners = CalculateCascadeCorners(cam, nearClip, farClip);

            glm::vec3 center(0.0f);
            for (const glm::vec3 &corner : frustumCorners)
                center += corner;
            center /= static_cast<float>(frustumCorners.size());

            float radius = CalculateStableCascadeRadius(cam.fov, cam.aspect, nearClip, farClip);
            float worldUnitsPerTexel = (radius * 2.0f) / static_cast<float>(directionalConfig.mapSize);
            directionalShadowInfo.cascadeTexelSizes[cascade] = worldUnitsPerTexel;

            float centerRight = glm::dot(center, lightRight);
            float centerUp = glm::dot(center, lightUp);
            float snappedRight = std::round(centerRight / worldUnitsPerTexel) * worldUnitsPerTexel;
            float snappedUp = std::round(centerUp / worldUnitsPerTexel) * worldUnitsPerTexel;
            glm::vec3 snappedCenter = center +
                                      lightRight * (snappedRight - centerRight) +
                                      lightUp * (snappedUp - centerUp);
            float lightDistance = radius + directionalConfig.depthMargin;
            glm::mat4 lightView = glm::lookAt(snappedCenter - lightDir * lightDistance,
                                              snappedCenter, lightUp);

            float lightNear = 0.0f;
            float lightFar = lightDistance + radius + directionalConfig.depthMargin;
            glm::mat4 lightProj = glm::ortho(-radius, radius, -radius, radius, lightNear, lightFar);
            lightProj[1][1] *= -1.0f;

            directionalShadowInfo.cascadeSplits[cascade] = split;
            directionalShadowInfo.lightViewProj[cascade] = lightProj * lightView;
            lastSplitDist = splitDist;
        }
    }

    int32_t ShadowManager::findSpotShadowSlot(entt::entity lightEntity) const
    {
        for (uint32_t slot = 0; slot < MAX_SPOT_LIGHT_SHADOW_CNT; ++slot)
            if (spotShadowLightEntities[slot] == lightEntity)
                return static_cast<int32_t>(slot);
        return -1;
    }

    uint32_t ShadowManager::ActivateSpotShadow(entt::entity lightEntity, SpotLight &light)
    {
        int32_t existingSlot = findSpotShadowSlot(lightEntity);
        if (existingSlot >= 0)
            return static_cast<uint32_t>(existingSlot + 1);

        uint32_t slot = MAX_SPOT_LIGHT_SHADOW_CNT;
        for (uint32_t i = 0; i < MAX_SPOT_LIGHT_SHADOW_CNT; ++i)
        {
            if (spotShadowLightEntities[i] == entt::null)
            {
                slot = i;
                break;
            }
        }
        if (slot == MAX_SPOT_LIGHT_SHADOW_CNT)
        {
            light.SetShadowSlot(false, 0);
            return 0;
        }

        spotShadowLightEntities[slot] = lightEntity;
        light.SetShadowSlot(true, slot);
        CalcSpotShadowVPMatrix(light);
        return slot + 1;
    }

    void ShadowManager::DeactivateSpotShadow(entt::entity lightEntity, SpotLight &light)
    {
        int32_t slot = findSpotShadowSlot(lightEntity);
        if (slot < 0)
            return;
        light.SetShadowSlot(false, 0);
        spotShadowLightEntities[slot] = entt::null;
        spotShadowInfos[slot] = SpotShadowInfoCPU();
        spotShadowUpdateCnts[slot] = MAX_FRAMES_IN_FLIGHT;
    }

    void ShadowManager::CalcSpotShadowVPMatrix(SpotLight &light)
    {
        glm::vec3 lightPos(light.positionWithRadius);
        glm::vec3 lightDir = glm::normalize(glm::vec3(light.direction));
        if (glm::length(lightDir) < 0.0001f)
            lightDir = glm::vec3(0.0f, -1.0f, 0.0f);

        glm::vec3 up = std::abs(glm::dot(lightDir, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.98f
                           ? glm::vec3(0.0f, 0.0f, 1.0f)
                           : glm::vec3(0.0f, 1.0f, 0.0f);

        const float outerCone = std::acos(glm::clamp(light.cone.y, -1.0f, 1.0f));
        const float fov = std::max(outerCone * 2.0f, glm::radians(1.0f));
        const float nearPlane = 0.05f;
        const float farPlane = std::max(light.positionWithRadius.w, nearPlane + 0.01f);
        glm::mat4 lightView = glm::lookAt(lightPos, lightPos + lightDir, up);
        glm::mat4 lightProj = glm::perspective(fov, 1.0f, nearPlane, farPlane);
        lightProj[1][1] *= -1.0f;

        uint32_t slot = light.GetShadowSlot();
        spotShadowInfos[slot].lightViewProj = lightProj * lightView;
        spotShadowUpdateCnts[slot] = MAX_FRAMES_IN_FLIGHT;
    }

    void ShadowManager::onTransientResourcesReady(FrameGraph &frameGraph, uint32_t currentFrame)
    {
        if (directionalShadowMapResourceID != 0)
            createDirectionalImageViews(frameGraph, currentFrame);
        if (spotShadowMapResourceID != 0)
            createSpotShadowImageViews(frameGraph, currentFrame);
        updateDeferredLightingDescriptorSet(currentFrame);
    }
}
