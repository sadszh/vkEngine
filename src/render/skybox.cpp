#include <render/skybox.hpp>
#include <asset/asset_manager.hpp>

namespace vke_render
{
    namespace
    {
        const glm::vec4 DEFAULT_SUN_LIGHT_DIR = glm::normalize(glm::vec4(1, 2, 0, 0));
    }

    void SkyboxManager::constructFrameGraph(FrameGraph &frameGraph,
                                            std::map<std::string, vke_ds::id32_t> &blackboard,
                                            ResourceNodeIDMap &currentResourceNodeID)
    {
        vke_ds::id32_t lutTaskID = frameGraph.AllocTaskNode("gen sky lut", RENDER_TASK,
                                                            std::bind(&SkyboxManager::generateLUT, this,
                                                                      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
        vke_ds::id32_t iblTaskID = frameGraph.AllocTaskNode("gen sky ibl lut", RENDER_TASK,
                                                            std::bind(&SkyboxManager::generateIBLLUT, this,
                                                                      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));

        VkImage skyLUTImages[MAX_FRAMES_IN_FLIGHT];
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
            skyLUTImages[i] = skyLUTs[i]->image;
        vke_ds::id32_t lutResourceID = frameGraph.AddPermanentImageResource("skyLUT", true, skyLUTImages,
                                                                            VK_IMAGE_ASPECT_COLOR_BIT, skyLUTs[0]->mipLevelCnt, 6, false,
                                                                            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        vke_ds::id32_t lutOutResourceNodeID = frameGraph.AllocResourceNode("outSkyLUT", lutResourceID);

        VkImage skyIrradianceImages[MAX_FRAMES_IN_FLIGHT];
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
            skyIrradianceImages[i] = skyIrradianceLUTs[i]->image;
        vke_ds::id32_t irradianceResourceID = frameGraph.AddPermanentImageResource("skyIrradianceLUT", true, skyIrradianceImages,
                                                                                   VK_IMAGE_ASPECT_COLOR_BIT, skyIrradianceLUTs[0]->mipLevelCnt, 6, false,
                                                                                   VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        vke_ds::id32_t irradianceOutResourceNodeID = frameGraph.AllocResourceNode("outSkyIrradianceLUT", irradianceResourceID);

        VkImage skySpecularImages[MAX_FRAMES_IN_FLIGHT];
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
            skySpecularImages[i] = skySpecularLUTs[i]->image;
        vke_ds::id32_t specularResourceID = frameGraph.AddPermanentImageResource("skySpecularLUT", true, skySpecularImages,
                                                                                 VK_IMAGE_ASPECT_COLOR_BIT, skySpecularLUTs[0]->mipLevelCnt, 6, false,
                                                                                 VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        vke_ds::id32_t specularOutResourceNodeID = frameGraph.AllocResourceNode("outSkySpecularLUT", specularResourceID);

        frameGraph.AddTaskNodeResourceRef(lutTaskID, 0, lutOutResourceNodeID,
                                          VK_ACCESS_SHADER_WRITE_BIT,
                                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                          VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE,
                                          VK_IMAGE_LAYOUT_GENERAL);
        frameGraph.AddTaskNodeResourceRef(iblTaskID, lutOutResourceNodeID, 0,
                                          VK_ACCESS_SHADER_READ_BIT,
                                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                          VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        frameGraph.AddTaskNodeResourceRef(iblTaskID, 0, irradianceOutResourceNodeID,
                                          VK_ACCESS_SHADER_WRITE_BIT,
                                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                          VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE,
                                          VK_IMAGE_LAYOUT_GENERAL);
        frameGraph.AddTaskNodeResourceRef(iblTaskID, 0, specularOutResourceNodeID,
                                          VK_ACCESS_SHADER_WRITE_BIT,
                                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                          VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE,
                                          VK_IMAGE_LAYOUT_GENERAL);

        blackboard["skyLUT"] = lutResourceID;
        blackboard["skyIrradianceLUT"] = irradianceResourceID;
        blackboard["skySpecularLUT"] = specularResourceID;
        currentResourceNodeID[lutResourceID] = lutOutResourceNodeID;
        currentResourceNodeID[irradianceResourceID] = irradianceOutResourceNodeID;
        currentResourceNodeID[specularResourceID] = specularOutResourceNodeID;
    }

    void SkyboxManager::initResources()
    {
        atmosphereParamBuffer = std::make_unique<DeviceBuffer>(sizeof(AtmosphereParameter), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        atmosphereParamBuffer->ToBuffer(0, &atmosphereParameter, sizeof(AtmosphereParameter));

        transmittanceLUT = std::make_unique<Texture2D>(
            TRANSMITTANCE_LUT_WIDTH, TRANSMITTANCE_LUT_HEIGHT,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_FILTER_LINEAR, VK_FILTER_LINEAR,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
        scatteringLUT = std::make_unique<Texture2D>(
            SCATTERING_LUT_SIZE, SCATTERING_LUT_SIZE,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_FILTER_LINEAR, VK_FILTER_LINEAR,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);

        std::shared_ptr<ShaderModuleSet> atmosphereLUTShader =
            vke_common::AssetManager::LoadComputeShaderUnique(vke_common::BUILTIN_COMPUTE_SHADER_ATMOSPHERE_LUT_ID);
        atmosphereLUTGenerationPipeline = std::make_unique<ComputePipeline>(std::move(atmosphereLUTShader));
        std::shared_ptr<ShaderModuleSet> skyLUTShader = vke_common::AssetManager::LoadComputeShaderUnique(vke_common::BUILTIN_COMPUTE_SHADER_SKYLUT_ID);
        skyLUTGenerationPipeline = std::make_unique<ComputePipeline>(std::move(skyLUTShader));
        std::shared_ptr<ShaderModuleSet> iblLUTShader = vke_common::AssetManager::LoadComputeShaderUnique(vke_common::BUILTIN_COMPUTE_SHADER_IBL_LUT_ID);
        iblLUTGenerationPipeline = std::make_unique<ComputePipeline>(std::move(iblLUTShader));

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            skyLUTs[i] = std::make_unique<CubeMap>(SKY_VIEW_CUBE_SIZE, SKY_VIEW_CUBE_SIZE,
                                                   VK_FORMAT_R16G16B16A16_SFLOAT,
                                                   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
                                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                   VK_FILTER_LINEAR,
                                                   VK_FILTER_LINEAR,
                                                   VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                                   false);
            skyIrradianceLUTs[i] = std::make_unique<CubeMap>(SKY_IRRADIANCE_CUBE_SIZE, SKY_IRRADIANCE_CUBE_SIZE,
                                                             VK_FORMAT_R16G16B16A16_SFLOAT,
                                                             VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
                                                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                             VK_FILTER_LINEAR,
                                                             VK_FILTER_LINEAR,
                                                             VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                                             false);
            skySpecularLUTs[i] = std::make_unique<CubeMap>(SKY_SPECULAR_CUBE_SIZE, SKY_SPECULAR_CUBE_SIZE,
                                                           VK_FORMAT_R16G16B16A16_SFLOAT,
                                                           VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
                                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                           VK_FILTER_LINEAR,
                                                           VK_FILTER_LINEAR,
                                                           VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                                           false,
                                                           SKY_SPECULAR_MIP_LEVELS);
        }

        generateAtmosphereLUTs();
    }

    void SkyboxManager::createDescriptorSet()
    {
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            skyLUTDescriptorSets[i] = skyLUTGenerationPipeline->shader->CreateDescriptorSet(0);
            skyIBLDescriptorSets[i] = iblLUTGenerationPipeline->shader->CreateDescriptorSet(0);
        }

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            VkWriteDescriptorSet skyDescriptorWrites[5];
            VkDescriptorBufferInfo bufferInfo = atmosphereParamBuffer->GetDescriptorBufferInfo();
            vke_render::ConstructDescriptorSetWrite(skyDescriptorWrites[0], skyLUTDescriptorSets[i], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &bufferInfo);
            VkDescriptorImageInfo transmittanceInfo{transmittanceLUT->textureSampler, transmittanceLUT->textureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            vke_render::ConstructDescriptorSetWrite(skyDescriptorWrites[1], skyLUTDescriptorSets[i], 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &transmittanceInfo);
            VkDescriptorImageInfo scatteringInfo{scatteringLUT->textureSampler, scatteringLUT->textureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            vke_render::ConstructDescriptorSetWrite(skyDescriptorWrites[2], skyLUTDescriptorSets[i], 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &scatteringInfo);
            VkDescriptorImageInfo skyReadInfo{skyLUTs[i]->sampler, skyLUTs[i]->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            vke_render::ConstructDescriptorSetWrite(skyDescriptorWrites[3], skyLUTDescriptorSets[i], 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &skyReadInfo);
            VkDescriptorImageInfo skyWriteInfo{nullptr, skyLUTs[i]->imageView, VK_IMAGE_LAYOUT_GENERAL};
            vke_render::ConstructDescriptorSetWrite(skyDescriptorWrites[4], skyLUTDescriptorSets[i], 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &skyWriteInfo);
            vkUpdateDescriptorSets(globalLogicalDevice, 5, skyDescriptorWrites, 0, nullptr);

            VkWriteDescriptorSet iblDescriptorWrites[3];
            VkDescriptorImageInfo skyViewInfo{skyLUTs[i]->sampler, skyLUTs[i]->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            vke_render::ConstructDescriptorSetWrite(iblDescriptorWrites[0], skyIBLDescriptorSets[i], 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &skyViewInfo);
            VkDescriptorImageInfo irradianceImageInfo{nullptr, skyIrradianceLUTs[i]->imageView, VK_IMAGE_LAYOUT_GENERAL};
            vke_render::ConstructDescriptorSetWrite(iblDescriptorWrites[1], skyIBLDescriptorSets[i], 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &irradianceImageInfo);

            VkDescriptorImageInfo specularImageInfos[SKY_SPECULAR_MIP_LEVELS];
            for (uint32_t mip = 0; mip < SKY_SPECULAR_MIP_LEVELS; ++mip)
                specularImageInfos[mip] = {nullptr, skySpecularLUTs[i]->GetMipImageView(mip), VK_IMAGE_LAYOUT_GENERAL};
            vke_render::ConstructDescriptorSetWrite(iblDescriptorWrites[2], skyIBLDescriptorSets[i], 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, specularImageInfos, 0, SKY_SPECULAR_MIP_LEVELS);
            vkUpdateDescriptorSets(globalLogicalDevice, 3, iblDescriptorWrites, 0, nullptr);
        }
    }

    void SkyboxManager::generateAtmosphereLUTs()
    {
        VkDescriptorSet descriptorSet = atmosphereLUTGenerationPipeline->shader->CreateDescriptorSet(0);
        VkDescriptorBufferInfo parameterInfo = atmosphereParamBuffer->GetDescriptorBufferInfo();
        VkDescriptorImageInfo transmittanceSampleInfo{
            transmittanceLUT->textureSampler, transmittanceLUT->textureImageView, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo transmittanceStorageInfo{
            VK_NULL_HANDLE, transmittanceLUT->textureImageView, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo scatteringStorageInfo{
            VK_NULL_HANDLE, scatteringLUT->textureImageView, VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet writes[4];
        ConstructDescriptorSetWrite(writes[0], descriptorSet, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &parameterInfo);
        ConstructDescriptorSetWrite(writes[1], descriptorSet, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &transmittanceSampleInfo);
        ConstructDescriptorSetWrite(writes[2], descriptorSet, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &transmittanceStorageInfo);
        ConstructDescriptorSetWrite(writes[3], descriptorSet, 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &scatteringStorageInfo);
        vkUpdateDescriptorSets(globalLogicalDevice, 4, writes, 0, nullptr);

        RenderEnvironment *environment = RenderEnvironment::GetInstance();
        VkCommandBuffer commandBuffer = RenderEnvironment::BeginSingleTimeCommands(environment->commandPool);

        uint32_t pass = 0;
        atmosphereLUTGenerationPipeline->Dispatch(
            commandBuffer, std::vector<VkDescriptorSet>{descriptorSet}, &pass,
            glm::ivec3((TRANSMITTANCE_LUT_WIDTH + 7) / 8, (TRANSMITTANCE_LUT_HEIGHT + 7) / 8, 1));

        VkImageMemoryBarrier transmittanceBarrier{};
        transmittanceBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        transmittanceBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        transmittanceBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        transmittanceBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        transmittanceBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        transmittanceBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        transmittanceBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        transmittanceBarrier.image = transmittanceLUT->textureImage;
        transmittanceBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        transmittanceBarrier.subresourceRange.baseMipLevel = 0;
        transmittanceBarrier.subresourceRange.levelCount = 1;
        transmittanceBarrier.subresourceRange.baseArrayLayer = 0;
        transmittanceBarrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &transmittanceBarrier);

        pass = 1;
        atmosphereLUTGenerationPipeline->Dispatch(
            commandBuffer, std::vector<VkDescriptorSet>{descriptorSet}, &pass,
            glm::ivec3((SCATTERING_LUT_SIZE + 7) / 8, (SCATTERING_LUT_SIZE + 7) / 8, 1));

        VulkanResourceManager::MakeLayoutTransition(
            commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            transmittanceLUT->textureImage, VK_IMAGE_ASPECT_COLOR_BIT, 1,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        VulkanResourceManager::MakeLayoutTransition(
            commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            scatteringLUT->textureImage, VK_IMAGE_ASPECT_COLOR_BIT, 1,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        RenderEnvironment::EndSingleTimeCommands(
            RenderEnvironment::GetGraphicsQueue(), environment->commandPool, commandBuffer);
    }

    void SkyboxManager::updateAtmospherePushConstants()
    {
        DirectionalLight *sun = lightManager == nullptr ? nullptr : lightManager->GetSun();
        if (sun == nullptr)
        {
            atmospherePushConstants.mainLightDir = DEFAULT_SUN_LIGHT_DIR;
            atmospherePushConstants.mainLightColorIntensity = glm::vec4(1.0f, 1.0f, 1.0f, 7.4f);
            return;
        }

        atmospherePushConstants.mainLightDir = -glm::normalize(sun->direction);
        atmospherePushConstants.mainLightColorIntensity = sun->colorWithIntensity;
    }

    void SkyboxManager::Update(float cameraWorldHeight)
    {
        updateAtmospherePushConstants();
        const float cameraHeight = std::max(cameraWorldHeight - atmosphereParameter.seaLevel, 1.0f);
        const auto &previous = lutParameters.atmosphere;
        const glm::vec3 directionDelta = glm::vec3(atmospherePushConstants.mainLightDir - previous.mainLightDir);
        const float directionThreshold = 2.0f * std::sin(glm::radians(lutConfig.sunDirectionThresholdDegrees) * 0.5f);
        const glm::vec4 lightDelta = glm::abs(atmospherePushConstants.mainLightColorIntensity - previous.mainLightColorIntensity);

        if (hasLUTParameters &&
            glm::dot(directionDelta, directionDelta) <= directionThreshold * directionThreshold &&
            std::max({lightDelta.x, lightDelta.y, lightDelta.z}) <= lutConfig.sunColorThreshold &&
            lightDelta.w <= lutConfig.sunIntensityThreshold &&
            std::abs(cameraHeight - lutParameters.cameraHeight) <= lutConfig.cameraHeightThreshold)
            return;

        lutParameters.atmosphere = atmospherePushConstants;
        lutParameters.groundColorBrightness = glm::vec4(
            lutConfig.groundColor[0], lutConfig.groundColor[1], lutConfig.groundColor[2], lutConfig.groundBrightness);
        lutParameters.cameraHeight = cameraHeight;
        lutParameters.groundHorizonSin = std::sin(glm::radians(lutConfig.groundHorizonWidthDegrees));
        lutParameters.groundCurve = lutConfig.groundCurve;
        hasLUTParameters = true;
        lutNeedsUpdate.fill(true);
    }

    void SkyboxManager::generateLUT(TaskNode &node, FrameGraph &frameGraph, VkCommandBuffer commandBuffer, uint32_t currentFrame, uint32_t imageIndex)
    {
        if (!lutNeedsUpdate[currentFrame])
            return;

        skyLUTGenerationPipeline->Dispatch(commandBuffer, std::vector<VkDescriptorSet>{skyLUTDescriptorSets[currentFrame]},
                                           &lutParameters, glm::ivec3((SKY_VIEW_CUBE_SIZE + 31) / 32, (SKY_VIEW_CUBE_SIZE + 31) / 32, 6));
    }

    void SkyboxManager::generateIBLLUT(TaskNode &node, FrameGraph &frameGraph, VkCommandBuffer commandBuffer, uint32_t currentFrame, uint32_t imageIndex)
    {
        if (!lutNeedsUpdate[currentFrame])
            return;

        iblLUTGenerationPipeline->Dispatch(commandBuffer,
                                           std::vector<VkDescriptorSet>{skyIBLDescriptorSets[currentFrame]},
                                           glm::ivec3((SKY_SPECULAR_CUBE_SIZE + 7) / 8, (SKY_SPECULAR_CUBE_SIZE + 7) / 8, 6));
        lutNeedsUpdate[currentFrame] = false;
    }
}
