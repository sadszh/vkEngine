#include <render/ssao.hpp>

namespace vke_render
{
    SSAOPass::~SSAOPass()
    {
        cleanupImageViews();
    }

    void SSAOPass::constructFrameGraph(FrameGraph &frameGraph,
                                       std::map<std::string, vke_ds::id32_t> &blackboard,
                                       ResourceNodeIDMap &currentResourceNodeID)
    {
        ssaoRawResourceID = frameGraph.AddTransientImageResource("ssaoRaw", imageCreateInfo, VK_IMAGE_ASPECT_COLOR_BIT);
        ssaoResourceID = frameGraph.AddTransientImageResource("ssao", imageCreateInfo, VK_IMAGE_ASPECT_COLOR_BIT);
        blackboard["ssao"] = ssaoResourceID;
        vke_ds::id32_t ssaoRawOutResourceNodeID = frameGraph.AllocResourceNode("ssaoRawOut", ssaoRawResourceID);
        vke_ds::id32_t ssaoBlurOutResourceNodeID = frameGraph.AllocResourceNode("ssaoBlurOut", ssaoResourceID);

        ssaoTaskNodeID = frameGraph.AllocTaskNode("ssao", RENDER_TASK,
                                                  std::bind(&SSAOPass::Render, this,
                                                            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,
                                                            std::placeholders::_4, std::placeholders::_5));
        frameGraph.AddTransientReadyCallback(std::bind(&SSAOPass::onSSAORawResourcesReady, this, std::placeholders::_1, std::placeholders::_2));
        ssaoBlurTaskNodeID = frameGraph.AllocTaskNode("ssao blur", RENDER_TASK,
                                                      std::bind(&SSAOPass::Render, this,
                                                                std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,
                                                                std::placeholders::_4, std::placeholders::_5));
        frameGraph.AddTransientReadyCallback(std::bind(&SSAOPass::onSSAOBlurResourcesReady, this, std::placeholders::_1, std::placeholders::_2));

        frameGraph.AddTaskNodeResourceRef(ssaoTaskNodeID, gbuffer->GetResourceNodeID(1), 0,
                                          VK_ACCESS_SHADER_READ_BIT,
                                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                          VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        frameGraph.AddTaskNodeResourceRef(ssaoTaskNodeID, gbuffer->GetResourceNodeID(3), 0,
                                          VK_ACCESS_SHADER_READ_BIT,
                                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                          VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        frameGraph.AddTaskNodeResourceRef(ssaoTaskNodeID, 0, ssaoRawOutResourceNodeID,
                                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                          VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
                                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        frameGraph.AddTaskNodeResourceRef(ssaoBlurTaskNodeID, ssaoRawOutResourceNodeID, 0,
                                          VK_ACCESS_SHADER_READ_BIT,
                                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                          VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        frameGraph.AddTaskNodeResourceRef(ssaoBlurTaskNodeID, gbuffer->GetResourceNodeID(1), 0,
                                          VK_ACCESS_SHADER_READ_BIT,
                                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                          VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        frameGraph.AddTaskNodeResourceRef(ssaoBlurTaskNodeID, gbuffer->GetResourceNodeID(3), 0,
                                          VK_ACCESS_SHADER_READ_BIT,
                                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                          VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        frameGraph.AddTaskNodeResourceRef(ssaoBlurTaskNodeID, 0, ssaoBlurOutResourceNodeID,
                                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                          VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
                                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        currentResourceNodeID[ssaoResourceID] = ssaoBlurOutResourceNodeID;
    }

    void SSAOPass::allocateDescriptorSet()
    {
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            ssaoDescriptorSets[i] = ssaoShader->CreateDescriptorSet(1);
            ssaoBlurDescriptorSets[i] = ssaoBlurShader->CreateDescriptorSet(1);
        }
    }

    void SSAOPass::createGraphicsPipeline()
    {
        VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo{};
        pipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        pipelineRenderingCreateInfo.pNext = nullptr;
        pipelineRenderingCreateInfo.colorAttachmentCount = 1;
        static constexpr VkFormat ssaoFormat = SSAO_FORMAT;
        pipelineRenderingCreateInfo.pColorAttachmentFormats = &ssaoFormat;
        pipelineRenderingCreateInfo.depthAttachmentFormat = VK_FORMAT_UNDEFINED;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        VkGraphicsPipelineCreateInfo ssaoPipelineInfo{};
        ssaoPipelineInfo.pNext = &pipelineRenderingCreateInfo;
        ssaoPipelineInfo.pDepthStencilState = &depthStencil;

        VkGraphicsPipelineCreateInfo blurPipelineInfo{};
        blurPipelineInfo.pNext = &pipelineRenderingCreateInfo;
        blurPipelineInfo.pDepthStencilState = &depthStencil;

        std::vector<uint32_t> vertexAttributeSizes;
        renderPipeline = std::make_unique<GraphicsPipeline>(ssaoShader, vertexAttributeSizes, VK_VERTEX_INPUT_RATE_VERTEX, ssaoPipelineInfo);
        blurPipeline = std::make_unique<GraphicsPipeline>(ssaoBlurShader, vertexAttributeSizes, VK_VERTEX_INPUT_RATE_VERTEX, blurPipelineInfo);
    }

    void SSAOPass::Render(TaskNode &node, FrameGraph &frameGraph, VkCommandBuffer commandBuffer, uint32_t currentFrame, uint32_t imageIndex)
    {
        if (node.taskID == ssaoBlurTaskNodeID)
        {
            renderFullscreen(commandBuffer, currentFrame, imageViews[currentFrame], blurPipeline.get(),
                             ssaoBlurDescriptorSets[currentFrame], 1.0f);
            return;
        }

        renderFullscreen(commandBuffer, currentFrame, rawImageViews[currentFrame], renderPipeline.get(),
                         ssaoDescriptorSets[currentFrame], 1.0f);
    }

    void SSAOPass::renderFullscreen(VkCommandBuffer commandBuffer, uint32_t currentFrame, VkImageView outputImageView,
                                    GraphicsPipeline *pipeline, VkDescriptorSet descriptorSet, const float clearValue)
    {
        VkRenderingAttachmentInfo colorAttachmentInfo{};
        colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachmentInfo.pNext = nullptr;
        colorAttachmentInfo.imageView = outputImageView;
        colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachmentInfo.clearValue.color = {{clearValue, clearValue, clearValue, 1.0f}};
        colorAttachmentInfo.resolveMode = VK_RESOLVE_MODE_NONE;

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.pNext = nullptr;
        renderingInfo.renderArea = {{0, 0}, {context->width, context->height}};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachmentInfo;
        renderingInfo.pDepthAttachment = nullptr;

        vkCmdBeginRendering(commandBuffer, &renderingInfo);
        pipeline->Bind(commandBuffer);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(context->width);
        viewport.height = static_cast<float>(context->height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = {context->width, context->height};
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        VkDescriptorSet descriptorSets[2] = {globalDescriptorSets[currentFrame], descriptorSet};
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline->pipelineLayout, 0, 2, descriptorSets, 0, nullptr);

        vkCmdPushConstants(commandBuffer, pipeline->pipelineLayout, VK_SHADER_STAGE_ALL,
                           0, sizeof(SSAOConfig), &constants);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);

        vkCmdEndRendering(commandBuffer);
    }

    void SSAOPass::onSSAORawResourcesReady(FrameGraph &frameGraph, uint32_t currentFrame)
    {
        createImageView(frameGraph, currentFrame);

        VkDescriptorImageInfo imageInfos[2] = {
            {gbuffer->sampler, gbuffer->imageViews[1][currentFrame], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {gbuffer->sampler, gbuffer->imageViews[3][currentFrame], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};

        VkWriteDescriptorSet descriptorWrites[2];
        ConstructDescriptorSetWrite(descriptorWrites[0], ssaoDescriptorSets[currentFrame], 0,
                                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfos[0]);
        ConstructDescriptorSetWrite(descriptorWrites[1], ssaoDescriptorSets[currentFrame], 1,
                                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfos[1]);
        vkUpdateDescriptorSets(globalLogicalDevice, 2, descriptorWrites, 0, nullptr);
    }

    void SSAOPass::onSSAOBlurResourcesReady(FrameGraph &frameGraph, uint32_t currentFrame)
    {
        createImageView(frameGraph, currentFrame);

        VkDescriptorImageInfo imageInfos[3] = {
            {sampler, rawImageViews[currentFrame], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {gbuffer->sampler, gbuffer->imageViews[1][currentFrame], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {gbuffer->sampler, gbuffer->imageViews[3][currentFrame], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};

        VkWriteDescriptorSet descriptorWrites[3];
        ConstructDescriptorSetWrite(descriptorWrites[0], ssaoBlurDescriptorSets[currentFrame], 0,
                                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfos[0]);
        ConstructDescriptorSetWrite(descriptorWrites[1], ssaoBlurDescriptorSets[currentFrame], 1,
                                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfos[1]);
        ConstructDescriptorSetWrite(descriptorWrites[2], ssaoBlurDescriptorSets[currentFrame], 2,
                                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfos[2]);
        vkUpdateDescriptorSets(globalLogicalDevice, 3, descriptorWrites, 0, nullptr);
    }

    void SSAOPass::createImageView(FrameGraph &frameGraph, uint32_t currentFrame)
    {
        if (imageViews[currentFrame] != VK_NULL_HANDLE)
            vkDestroyImageView(globalLogicalDevice, imageViews[currentFrame], nullptr);
        VkImage image = frameGraph.GetImageResource(ssaoResourceID).images[currentFrame];
        imageViews[currentFrame] = VulkanResourceManager::CreateImageView(image, SSAO_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT);

        if (rawImageViews[currentFrame] != VK_NULL_HANDLE)
            vkDestroyImageView(globalLogicalDevice, rawImageViews[currentFrame], nullptr);
        VkImage rawImage = frameGraph.GetImageResource(ssaoRawResourceID).images[currentFrame];
        rawImageViews[currentFrame] = VulkanResourceManager::CreateImageView(rawImage, SSAO_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT);
    }

    void SSAOPass::cleanupImageViews()
    {
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            if (imageViews[i] != VK_NULL_HANDLE)
            {
                vkDestroyImageView(globalLogicalDevice, imageViews[i], nullptr);
                imageViews[i] = VK_NULL_HANDLE;
            }
            if (rawImageViews[i] != VK_NULL_HANDLE)
            {
                vkDestroyImageView(globalLogicalDevice, rawImageViews[i], nullptr);
                rawImageViews[i] = VK_NULL_HANDLE;
            }
        }
    }
}
