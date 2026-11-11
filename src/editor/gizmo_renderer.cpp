#include <editor/gizmo_renderer.hpp>
#include <editor/builtin_assets.hpp>
#include <asset/asset_manager.hpp>

namespace vke_editor
{
    GizmoRenderer::GizmoRenderer(vke_render::RenderContext *ctx)
        : context(ctx)
    {
        auto shaderResult = vke_common::AssetManager::LoadVertFragShaderUnique(BUILTIN_VFSHADER_GIZMO_ID);
        VKE_FATAL_IF(!shaderResult, "{}", shaderResult.error())
        shader = std::move(*shaderResult);
        createGraphicsPipeline();
    }

    void GizmoRenderer::createGraphicsPipeline()
    {
        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &context->colorFormat;
        renderingInfo.depthAttachmentFormat = context->depthFormat;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 4.0f;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo blendState{};
        blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blendState.attachmentCount = 1;
        blendState.pAttachments = &blendAttachment;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &blendState;

        static const std::vector<uint32_t> vertexAttrSizes = {sizeof(glm::vec3)};
        pipeline = std::make_unique<vke_render::GraphicsPipeline>(
            shader, vertexAttrSizes, VK_VERTEX_INPUT_RATE_VERTEX, pipelineInfo);
    }

    void GizmoRenderer::Render(VkCommandBuffer commandBuffer, uint32_t currentFrame,
                               const glm::mat4 &viewProj, std::span<const glm::vec3> lines,
                               std::span<const GizmoBatch> batches)
    {
        size_t neededBytes = lines.size() * sizeof(glm::vec3);
        auto &buf = vertexBuffers[currentFrame];
        if (!buf || buf->bufferSize < neededBytes)
        {
            buf = std::make_unique<vke_render::HostCoherentBuffer>(
                neededBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        }

        buf->ToBuffer(0, lines.data(), neededBytes);

        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = context->colorImageViews[currentFrame];
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingAttachmentInfo depthAttachment{};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = context->depthImageViews[currentFrame];
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        // TODO: Revisit whether depth needs to be preserved after the final editor overlay.
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        const VkExtent2D extent{context->width, context->height};
        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea = {{0, 0}, extent};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;
        renderingInfo.pDepthAttachment = &depthAttachment;

        vkCmdBeginRendering(commandBuffer, &renderingInfo);

        VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width),
                            static_cast<float>(extent.height), 0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, extent};
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        PushConstants pc;
        pc.viewProj = viewProj;
        pipeline->Bind(commandBuffer);

        VkBuffer vkBuf = vertexBuffers[currentFrame]->buffer;
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vkBuf, &offset);
        for (const auto &batch : batches)
        {
            pc.color = batch.color;
            vkCmdPushConstants(commandBuffer, pipeline->pipelineLayout,
                               VK_SHADER_STAGE_ALL, 0, sizeof(PushConstants), &pc);
            vkCmdDraw(commandBuffer, batch.vertexCount, 1, batch.firstVertex, 0);
        }

        vkCmdEndRendering(commandBuffer);
    }
}
