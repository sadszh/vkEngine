#include <render/gbuffer_pass.hpp>

namespace vke_render
{
    GBuffer *GBuffer::instance = nullptr;

    void GBufferPass::constructFrameGraph(FrameGraph &frameGraph,
                                          std::map<std::string, vke_ds::id32_t> &blackboard,
                                          ResourceNodeIDMap &currentResourceNodeID)
    {
        gbuffer->RegisterFrameGraphResources(frameGraph);

        vke_ds::id32_t depthAttachmentResourceID = blackboard["depthAttachment"];

        vke_ds::id32_t gbufferOutDepthResourceNodeID = frameGraph.AllocResourceNode("gbufferOutDepth", depthAttachmentResourceID);
        gbufferTaskNodeID = frameGraph.AllocTaskNode("gbuffer pass", RENDER_TASK,
                                                     std::bind(&GBufferPass::Render, this,
                                                               std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
        frameGraph.AddTransientReadyCallback(std::bind(&GBufferPass::onTransientResourcesReady, this, std::placeholders::_1, std::placeholders::_2));
        for (int i = 0; i < GBUFFER_CNT; i++)
            frameGraph.AddTaskNodeResourceRef(gbufferTaskNodeID, 0, gbuffer->GetResourceNodeID(i),
                                              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                              VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
                                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        frameGraph.AddTaskNodeResourceRef(gbufferTaskNodeID, currentResourceNodeID[depthAttachmentResourceID], gbufferOutDepthResourceNodeID,
                                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                                          VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
                                          VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

        currentResourceNodeID[depthAttachmentResourceID] = gbufferOutDepthResourceNodeID;
    }

    static const std::vector<uint32_t> noskinVertexAttributeSizes = {sizeof(vke_render::Vertex::pos), sizeof(vke_render::Vertex::normal),
                                                                     sizeof(vke_render::Vertex::tangent), sizeof(vke_render::Vertex::texCoord)};
    static const std::vector<uint32_t> skinVertexAttributeSizes = {
        sizeof(vke_render::SkinVertex::pos),
        sizeof(vke_render::SkinVertex::normal),
        sizeof(vke_render::SkinVertex::tangent),
        sizeof(vke_render::SkinVertex::texCoord),
        sizeof(vke_render::SkinVertex::weights),
        sizeof(vke_render::SkinVertex::jointIDs),
    };

    void GBufferPass::createGraphicsPipeline(RenderInfo &renderInfo, bool isSkin)
    {
        VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo{};
        pipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        pipelineRenderingCreateInfo.pNext = nullptr;
        pipelineRenderingCreateInfo.colorAttachmentCount = GBUFFER_CNT;
        pipelineRenderingCreateInfo.pColorAttachmentFormats = gbufferFormats;
        pipelineRenderingCreateInfo.depthAttachmentFormat = context->depthFormat;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.minDepthBounds = 0.0f; // Optional
        depthStencil.maxDepthBounds = 1.0f; // Optional
        depthStencil.stencilTestEnable = VK_FALSE;
        depthStencil.front = {}; // Optional
        depthStencil.back = {};  // Optional

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.pNext = &pipelineRenderingCreateInfo;
        pipelineInfo.pDepthStencilState = &depthStencil;

        renderInfo.CreatePipeline(isSkin ? skinVertexAttributeSizes : noskinVertexAttributeSizes, VK_VERTEX_INPUT_RATE_VERTEX, pipelineInfo);
    }

    void GBufferPass::Render(TaskNode &node, FrameGraph &frameGraph, VkCommandBuffer commandBuffer, uint32_t currentFrame, uint32_t imageIndex)
    {
        VkRenderingAttachmentInfo colorAttachmentInfos[GBUFFER_CNT] = {};

        for (int i = 0; i < GBUFFER_CNT; ++i)
        {
            VkRenderingAttachmentInfo &colorAttachmentInfo = colorAttachmentInfos[i];
            colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachmentInfo.pNext = nullptr;
            colorAttachmentInfo.imageView = gbuffer->imageViews[i][currentFrame];
            colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachmentInfo.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            colorAttachmentInfo.resolveMode = VK_RESOLVE_MODE_NONE;
        }

        VkRenderingAttachmentInfo depthAttachmentInfo{};
        depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachmentInfo.pNext = nullptr;
        depthAttachmentInfo.imageView = context->depthImageViews[currentFrame];
        depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachmentInfo.clearValue.depthStencil = {1.0f, 0};
        depthAttachmentInfo.resolveMode = VK_RESOLVE_MODE_NONE;

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.pNext = nullptr;
        renderingInfo.renderArea = {{0, 0}, {context->width, context->height}};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = GBUFFER_CNT;
        renderingInfo.pColorAttachments = colorAttachmentInfos;
        renderingInfo.pDepthAttachment = &depthAttachmentInfo;

        vkCmdBeginRendering(commandBuffer, &renderingInfo);

        const Frustum frustum(cameraInfo->projection * cameraInfo->view);
        for (auto &kv : renderInfoMap)
        {
            std::unique_ptr<RenderInfo> &renderInfo = kv.second;
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderInfo->renderPipeline->pipeline);
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

            renderInfo->Render(commandBuffer, globalDescriptorSets[currentFrame], &frustum);
        }

        vkCmdEndRendering(commandBuffer);
    }

    void GBufferPass::OnWindowResize(FrameGraph &frameGraph, RenderContext *ctx)
    {
        gbuffer->Recreate(ctx->width, ctx->height);
        for (int i = 0; i < GBUFFER_CNT; ++i)
            frameGraph.SetTransientImageCreateInfo(gbuffer->GetResourceID(i), gbuffer->CreateImageInfo(i));
    }

    void GBufferPass::onTransientResourcesReady(FrameGraph &frameGraph, uint32_t currentFrame)
    {
        gbuffer->CreateImageViews(frameGraph, currentFrame);
    }
}
