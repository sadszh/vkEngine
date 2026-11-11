#include <render/atmosphere_pass.hpp>
#include <asset/asset_manager.hpp>

namespace vke_render
{
    AtmospherePass::AtmospherePass(RenderContext *ctx, VkDescriptorSet *globalDescriptorSets,
                                   SkyboxManager *skyboxManager, HDRColorManager *hdrColorManager)
        : RenderPassBase(ATMOSPHERE_PASS, ctx, globalDescriptorSets),
          skyboxManager(skyboxManager),
          hdrColorManager(hdrColorManager),
          gbuffer(GBuffer::GetInstance()),
          taskNodeID(0)
    {
    }

    void AtmospherePass::Init(int subpassID,
                              FrameGraph &frameGraph,
                              std::map<std::string, vke_ds::id32_t> &blackboard,
                              ResourceNodeIDMap &currentResourceNodeID)
    {
        RenderPassBase::Init(subpassID, frameGraph, blackboard, currentResourceNodeID);
        VKE_FATAL_IF(gbuffer == nullptr, "Atmosphere pass requires the gbuffer pass to be initialized first")
        auto shaderResult = vke_common::AssetManager::LoadVertFragShaderUnique(vke_common::BUILTIN_VFSHADER_ATMOSPHERE_ID);
        VKE_FATAL_IF(!shaderResult, "{}", shaderResult.error())
        shader = std::move(*shaderResult);
        constructFrameGraph(frameGraph, blackboard, currentResourceNodeID);
        createDescriptorSet();
        createGraphicsPipeline();
    }

    void AtmospherePass::constructFrameGraph(FrameGraph &frameGraph,
                                             std::map<std::string, vke_ds::id32_t> &blackboard,
                                             ResourceNodeIDMap &currentResourceNodeID)
    {
        inputHDRColorImageIndex = hdrColorManager->GetLatestImageIndex();
        outputHDRColorImageIndex = hdrColorManager->GetAlternateImageIndex();
        const vke_ds::id32_t outputNodeID = frameGraph.AllocResourceNode(
            "atmosphereOutHDRColor", hdrColorManager->GetResourceID(outputHDRColorImageIndex));

        taskNodeID = frameGraph.AllocTaskNode(
            "atmosphere",
            RENDER_TASK,
            std::bind(&AtmospherePass::Render, this,
                      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,
                      std::placeholders::_4, std::placeholders::_5));
        frameGraph.AddTransientReadyCallback(std::bind(&AtmospherePass::onTransientResourcesReady, this, std::placeholders::_1, std::placeholders::_2));

        frameGraph.AddTaskNodeResourceRef(
            taskNodeID, hdrColorManager->GetResourceNodeID(inputHDRColorImageIndex), 0,
            VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        frameGraph.AddTaskNodeResourceRef(
            taskNodeID, gbuffer->GetResourceNodeID(3), 0,
            VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        frameGraph.AddTaskNodeResourceRef(
            taskNodeID, 0, outputNodeID,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        hdrColorManager->UpdateResourceNode(outputHDRColorImageIndex, outputNodeID);
    }

    void AtmospherePass::createDescriptorSet()
    {
        VkDescriptorBufferInfo atmosphereBuffer = skyboxManager->GetAtmosphereParamBuffer()->GetDescriptorBufferInfo();
        Texture2D *transmittanceLUT = skyboxManager->GetTransmittanceLUT();
        Texture2D *scatteringLUT = skyboxManager->GetScatteringLUT();
        VkDescriptorImageInfo imageInfos[2] = {
            {transmittanceLUT->textureSampler, transmittanceLUT->textureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {scatteringLUT->textureSampler, scatteringLUT->textureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};

        for (uint32_t currentFrame = 0; currentFrame < MAX_FRAMES_IN_FLIGHT; ++currentFrame)
        {
            descriptorSets[currentFrame] = shader->CreateDescriptorSet(1);
            VkWriteDescriptorSet writes[3];
            ConstructDescriptorSetWrite(writes[0], descriptorSets[currentFrame], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &atmosphereBuffer);
            ConstructDescriptorSetWrite(writes[1], descriptorSets[currentFrame], 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfos[0]);
            ConstructDescriptorSetWrite(writes[2], descriptorSets[currentFrame], 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfos[1]);
            vkUpdateDescriptorSets(globalLogicalDevice, 3, writes, 0, nullptr);
        }
    }

    void AtmospherePass::createGraphicsPipeline()
    {
        VkPipelineRenderingCreateInfo renderingCreateInfo{};
        renderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingCreateInfo.colorAttachmentCount = 1;
        static constexpr VkFormat hdrFormat = HDRColorManager::HDR_COLOR_FORMAT;
        renderingCreateInfo.pColorAttachmentFormats = &hdrFormat;
        renderingCreateInfo.depthAttachmentFormat = VK_FORMAT_UNDEFINED;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.pNext = &renderingCreateInfo;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipeline = std::make_unique<GraphicsPipeline>(
            shader, std::vector<uint32_t>{}, VK_VERTEX_INPUT_RATE_VERTEX, pipelineInfo);
    }

    void AtmospherePass::Render(TaskNode &, FrameGraph &, VkCommandBuffer commandBuffer,
                                uint32_t currentFrame, uint32_t)
    {
        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = hdrColorManager->GetImageView(outputHDRColorImageIndex, currentFrame);
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea = {{0, 0}, {context->width, context->height}};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;

        vkCmdBeginRendering(commandBuffer, &renderingInfo);
        pipeline->Bind(commandBuffer);

        VkViewport viewport{0.0f, 0.0f, static_cast<float>(context->width), static_cast<float>(context->height), 0.0f, 1.0f};
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        VkRect2D scissor{{0, 0}, {context->width, context->height}};
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        VkDescriptorSet sets[2] = {globalDescriptorSets[currentFrame], descriptorSets[currentFrame]};
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline->pipelineLayout, 0, 2, sets, 0, nullptr);
        const AtmospherePushConstants &pushConstants = skyboxManager->GetAtmospherePushConstants();
        vkCmdPushConstants(commandBuffer, pipeline->pipelineLayout, VK_SHADER_STAGE_ALL,
                           0, sizeof(AtmospherePushConstants), &pushConstants);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        vkCmdEndRendering(commandBuffer);
    }

    void AtmospherePass::updateDescriptorSet(uint32_t currentFrame)
    {
        VkDescriptorImageInfo imageInfos[2] = {
            {hdrColorManager->sampler, hdrColorManager->GetImageView(inputHDRColorImageIndex, currentFrame), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {gbuffer->sampler, gbuffer->imageViews[3][currentFrame], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkWriteDescriptorSet writes[2];
        ConstructDescriptorSetWrite(writes[0], descriptorSets[currentFrame], 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfos[0]);
        ConstructDescriptorSetWrite(writes[1], descriptorSets[currentFrame], 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfos[1]);
        vkUpdateDescriptorSets(globalLogicalDevice, 2, writes, 0, nullptr);
    }

    void AtmospherePass::onTransientResourcesReady(FrameGraph &, uint32_t currentFrame)
    {
        updateDescriptorSet(currentFrame);
    }

    void AtmospherePass::OnWindowResize(FrameGraph &frameGraph, RenderContext *ctx)
    {
        context = ctx;
    }
}
