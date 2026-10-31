#include <render/light_manager.hpp>
#include <asset/asset_manager.hpp>

namespace vke_render
{
    LightManager::LightManager(RenderContext *ctx, FrameGraph &frameGraph, const CameraInfo *cameraInfo, const DirectionalShadowConfig &config)
    {
        init();
        shadowManager = std::make_unique<ShadowManager>(ctx, frameGraph, cameraInfo, config);
        lightUpdateCnts[(int)LightType::DIRECTIONAL_LIGHT] = MAX_FRAMES_IN_FLIGHT;
    }

    void LightManager::init()
    {
        for (int i = 0; i < (int)LightType::LIGHT_TYPE_CNT; ++i)
        {
            lightCnts[i] = 0;
            cpuLightBuffers[i] = std::make_unique<HostCoherentBuffer>(LIGHT_SIZES[i] * MAX_LIGHT_CNTS[i],
                                                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
            lightUpdateCnts[i] = 0;
            dirtyFlags[i] = false;
        }

        auto lightCullingShader = vke_common::AssetManager::LoadComputeShader(vke_common::BUILTIN_COMPUTE_SHADER_LIGHTCULL_ID);
        lightCullingTask = std::make_unique<ComputePipeline>(lightCullingShader);

        for (int i = 0; i < (int)LightType::LIGHT_TYPE_CNT; ++i)
            for (int j = 0; j < MAX_FRAMES_IN_FLIGHT; ++j)
                lightBuffers[i][j] = std::make_unique<DeviceBuffer>(LIGHT_SIZES[i] * MAX_LIGHT_CNTS[i], VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < MAX_FRAMES_IN_FLIGHT; ++j)
                clusterBuffers[i][j] = std::make_unique<DeviceBuffer>(CLUSTER_DIM_X * CLUSTER_DIM_Y * CLUSTER_DIM_Z * MAX_LIGHT_PER_CLUSTER * sizeof(uint32_t),
                                                                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    }

    void LightManager::GetBindingInfos(std::vector<VkDescriptorSetLayoutBinding> &bindingInfos, DescriptorSetInfo &descriptorSetInfo)
    {
        VkDescriptorSetLayoutBinding layoutBinding{};
        layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        layoutBinding.descriptorCount = 1;
        layoutBinding.stageFlags = VK_SHADER_STAGE_ALL;

        for (int i = 1; i <= (int)LightType::LIGHT_TYPE_CNT; ++i)
        {
            layoutBinding.binding = i;
            bindingInfos.push_back(layoutBinding);
        }

        layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        for (int i = (int)LightType::LIGHT_TYPE_CNT + 1; i <= (int)LightType::LIGHT_TYPE_CNT + 2; ++i)
        {
            layoutBinding.binding = i;
            bindingInfos.push_back(layoutBinding);
        }

        descriptorSetInfo.AddCnt(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (int)LightType::LIGHT_TYPE_CNT);
        descriptorSetInfo.AddCnt(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2);
    }

    void LightManager::GetDescriptorSetWrites(uint32_t id,
                                              std::vector<VkWriteDescriptorSet> &descriptorSetWrites,
                                              VkDescriptorSet descriptorSet,
                                              std::vector<VkDescriptorBufferInfo> &bufferInfos)
    {
        uint32_t totWriteCnt = (int)LightType::LIGHT_TYPE_CNT + 2;
        descriptorSetWrites.insert(descriptorSetWrites.end(), totWriteCnt, VkWriteDescriptorSet{});
        bufferInfos.resize(totWriteCnt);

        for (int i = 0; i < (int)LightType::LIGHT_TYPE_CNT; ++i)
        {
            bufferInfos[i] = lightBuffers[i][id]->GetDescriptorBufferInfo();
            ConstructDescriptorSetWrite(descriptorSetWrites[i + 1], descriptorSet, i + 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &(bufferInfos[i]));
        }

        for (int i = (int)LightType::LIGHT_TYPE_CNT; i < totWriteCnt; ++i)
        {
            bufferInfos[i] = clusterBuffers[i - (int)LightType::LIGHT_TYPE_CNT][id]->GetDescriptorBufferInfo();
            ConstructDescriptorSetWrite(descriptorSetWrites[i + 1], descriptorSet, i + 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &(bufferInfos[i]));
        }
    }

    void LightManager::ConstructFrameGraph(FrameGraph &frameGraph,
                                           std::map<std::string, vke_ds::id32_t> &blackboard,
                                           ResourceNodeIDMap &currentResourceNodeID)
    {
        VkBuffer pointLightBuffers[MAX_FRAMES_IN_FLIGHT];
        VkBuffer spotLightBuffers[MAX_FRAMES_IN_FLIGHT];
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            pointLightBuffers[i] = clusterBuffers[0][i]->buffer;
            spotLightBuffers[i] = clusterBuffers[1][i]->buffer;
        }

        vke_ds::id32_t pointLightBufferResourceID = frameGraph.AddPermanentBufferResource("pointLightBuffer", true,
                                                                                          pointLightBuffers, 0, clusterBuffers[0][0]->bufferSize,
                                                                                          VK_PIPELINE_STAGE_NONE);
        vke_ds::id32_t spotLightBufferResourceID = frameGraph.AddPermanentBufferResource("spotLightBuffer", true,
                                                                                         spotLightBuffers, 0, clusterBuffers[1][0]->bufferSize,
                                                                                         VK_PIPELINE_STAGE_NONE);
        blackboard["pointLightClusterBuffer"] = pointLightBufferResourceID;
        blackboard["spotLightClusterBuffer"] = spotLightBufferResourceID;

        vke_ds::id32_t outPointLightBufferResourceNodeID = frameGraph.AllocResourceNode("outPointLight", pointLightBufferResourceID);
        vke_ds::id32_t outSpotLightBufferResourceNodeID = frameGraph.AllocResourceNode("outSpotLight", spotLightBufferResourceID);

        vke_ds::id32_t lightCullingTaskNodeID = frameGraph.AllocTaskNode("light culling", COMPUTE_TASK,
                                                                         std::bind(&LightManager::cullLights, this,
                                                                                   std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));

        frameGraph.AddTaskNodeResourceRef(lightCullingTaskNodeID, 0, outPointLightBufferResourceNodeID,
                                          VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                          VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                          VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE);

        frameGraph.AddTaskNodeResourceRef(lightCullingTaskNodeID, 0, outSpotLightBufferResourceNodeID,
                                          VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                          VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                          VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE);

        currentResourceNodeID[pointLightBufferResourceID] = outPointLightBufferResourceNodeID;
        currentResourceNodeID[spotLightBufferResourceID] = outSpotLightBufferResourceNodeID;
    }

    void LightManager::cullLights(TaskNode &node, FrameGraph &frameGraph, VkCommandBuffer commandBuffer, uint32_t currentFrame, uint32_t imageIndex)
    {
        lightCullingTask->Dispatch(commandBuffer,
                                   std::vector<VkDescriptorSet>{globalDescriptorSets[currentFrame]},
                                   lightCnts + 1,
                                   glm::ivec3(CLUSTER_DIM_X / 8, CLUSTER_DIM_Y / 8, CLUSTER_DIM_Z));
    }

    void LightManager::Update(uint32_t currentFrame, bool cameraUpdated)
    {
        bool directionalDirty = dirtyFlags[(int)LightType::DIRECTIONAL_LIGHT];
        for (int i = 0; i < (int)LightType::LIGHT_TYPE_CNT; ++i)
        {
            if (dirtyFlags[i])
            {
                lightUpdateCnts[i] = MAX_FRAMES_IN_FLIGHT;
                dirtyFlags[i] = false;
            }
        }

        if (cameraUpdated || directionalDirty)
            shadowManager->UpdateDirectionalShadowInfo(lightCnts[(int)LightType::DIRECTIONAL_LIGHT] == 0
                                                           ? nullptr
                                                           : reinterpret_cast<const DirectionalLight *>(cpuLightBuffers[(int)LightType::DIRECTIONAL_LIGHT]->data));

        bool directionalSync = lightUpdateCnts[(int)LightType::DIRECTIONAL_LIGHT] > 0;
        for (int i = 0; i < (int)LightType::LIGHT_TYPE_CNT; ++i)
        {
            if (lightUpdateCnts[i] > 0)
            {
                --lightUpdateCnts[i];
                VulkanResourceManager::CopyBuffer(cpuLightBuffers[i]->buffer, lightBuffers[i][currentFrame]->buffer,
                                              cpuLightBuffers[i]->bufferSize);
            }
        }
        if (cameraUpdated || directionalSync)
            shadowManager->SyncDirectionalShadowToGPU(currentFrame);
        shadowManager->SyncSpotShadowToGPU(currentFrame);
    }

    DirectionalLight *LightManager::GetSun()
    {
        if (lightCnts[(int)LightType::DIRECTIONAL_LIGHT] == 0)
            return nullptr;

        return reinterpret_cast<DirectionalLight *>(cpuLightBuffers[(int)LightType::DIRECTIONAL_LIGHT]->data);
    }

    void LightManager::ClearLights()
    {
        for (int i = 0; i < (int)LightType::LIGHT_TYPE_CNT; ++i)
        {
            lightCnts[i] = 0;
            entityToLight[i].clear();
            ownerMaps[i].clear();
            lightUpdateCnts[i] = MAX_FRAMES_IN_FLIGHT;
            dirtyFlags[i] = true;
        }

        shadowManager->ClearLights();
    }

    void LightManager::UpdateSpotShadow(entt::entity entity)
    {
        SpotLight &light = GetLightWithoutCheckByEntity<SpotLight>(entity);
        shadowManager->CalcSpotShadowVPMatrix(light);
    }

    uint32_t LightManager::ActivateSpotShadow(entt::entity entity)
    {
        SpotLight &light = GetLightWithoutCheckByEntity<SpotLight>(entity);
        uint32_t slot = shadowManager->ActivateSpotShadow(entity, light);
        if (slot != 0)
            dirtyFlags[(int)LightType::SPOT_LIGHT] = true;
        return slot;
    }

    void LightManager::DeactivateSpotShadow(entt::entity entity)
    {
        SpotLight &light = GetLightWithoutCheckByEntity<SpotLight>(entity);
        shadowManager->DeactivateSpotShadow(entity, light);
        dirtyFlags[(int)LightType::SPOT_LIGHT] = true;
    }
}
