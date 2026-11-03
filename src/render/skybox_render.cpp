#include <render/skybox_render.hpp>
#include <asset/asset_manager.hpp>

namespace vke_render
{
    void SkyboxRenderer::constructFrameGraph(FrameGraph &frameGraph,
                                             std::map<std::string, vke_ds::id32_t> &blackboard,
                                             ResourceNodeIDMap &currentResourceNodeID)
    {
        vke_ds::id32_t lutResourceID = blackboard.at("skyLUT");
        vke_ds::id32_t lutOutResourceNodeID = currentResourceNodeID.at(lutResourceID);
        hdrColorImageIndex = hdrColorManager->GetLatestImageIndex();
        vke_ds::id32_t hdrColorResourceID = hdrColorManager->GetResourceID(hdrColorImageIndex);
        vke_ds::id32_t depthAttachmentResourceID = blackboard["depthAttachment"];

        vke_ds::id32_t skyOutColorResourceNodeID = frameGraph.AllocResourceNode("skyOutHDRColor", hdrColorResourceID);
        vke_ds::id32_t skyTaskNodeID = frameGraph.AllocTaskNode("skybox render", RENDER_TASK,
                                                                std::bind(&SkyboxRenderer::Render, this,
                                                                          std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
        frameGraph.AddTaskNodeResourceRef(skyTaskNodeID, hdrColorManager->GetResourceNodeID(hdrColorImageIndex), skyOutColorResourceNodeID,
                                          VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                          VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
                                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        // TODO: Separate depth preservation from FrameGraph write tracking; STORE currently
        // forces a depth output node and write synchronization for this depth-test-only pass.
        const auto depthOutput = frameGraph.AllocResourceNode("skyOutDepth", depthAttachmentResourceID);
        frameGraph.AddTaskNodeResourceRef(skyTaskNodeID, currentResourceNodeID[depthAttachmentResourceID], depthOutput,
                                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                                          VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
                                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        currentResourceNodeID[depthAttachmentResourceID] = depthOutput;
        frameGraph.AddTaskNodeResourceRef(skyTaskNodeID, lutOutResourceNodeID, 0,
                                          VK_ACCESS_SHADER_READ_BIT,
                                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                          VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        hdrColorManager->UpdateResourceNode(hdrColorImageIndex, skyOutColorResourceNodeID);
    }

    void SkyboxRenderer::createGraphicsPipeline()
    {
        VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo{};
        pipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        pipelineRenderingCreateInfo.pNext = nullptr;
        pipelineRenderingCreateInfo.colorAttachmentCount = 1;
        static constexpr VkFormat hdrFormat = HDRColorManager::HDR_COLOR_FORMAT;
        pipelineRenderingCreateInfo.pColorAttachmentFormats = &hdrFormat;
        pipelineRenderingCreateInfo.depthAttachmentFormat = context->depthFormat;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.minDepthBounds = 0.0f; // Optional
        depthStencil.maxDepthBounds = 1.0f; // Optional
        depthStencil.stencilTestEnable = VK_FALSE;
        depthStencil.front = {}; // Optional
        depthStencil.back = {};  // Optional

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.pNext = &pipelineRenderingCreateInfo;
        pipelineInfo.pDepthStencilState = &depthStencil;

        std::vector<uint32_t> vertexAttributeSizes = {sizeof(glm::vec3)};
        renderPipeline = std::make_unique<GraphicsPipeline>(skyboxMaterial->shader, vertexAttributeSizes, VK_VERTEX_INPUT_RATE_VERTEX, pipelineInfo);
    }

    const std::vector<glm::vec3> skyboxVertices = {
        // positions
        {-1.0f, 1.0f, -1.0f},
        {1.0f, -1.0f, -1.0f},
        {-1.0f, -1.0f, -1.0f},
        {1.0f, -1.0f, -1.0f},
        {-1.0f, 1.0f, -1.0f},
        {1.0f, 1.0f, -1.0f},

        {-1.0f, -1.0f, 1.0f},
        {-1.0f, 1.0f, -1.0f},
        {-1.0f, -1.0f, -1.0f},
        {-1.0f, 1.0f, -1.0f},
        {-1.0f, -1.0f, 1.0f},
        {-1.0f, 1.0f, 1.0f},

        {1.0f, -1.0f, -1.0f},
        {1.0f, 1.0f, 1.0f},
        {1.0f, -1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f},
        {1.0f, -1.0f, -1.0f},
        {1.0f, 1.0f, -1.0f},

        {-1.0f, -1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f},
        {-1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f},
        {-1.0f, -1.0f, 1.0f},
        {1.0f, -1.0f, 1.0f},

        {-1.0f, 1.0f, -1.0f},
        {1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, -1.0f},
        {1.0f, 1.0f, 1.0f},
        {-1.0f, 1.0f, -1.0f},
        {-1.0f, 1.0f, 1.0f},

        {-1.0f, -1.0f, -1.0f},
        {1.0f, -1.0f, -1.0f},
        {-1.0f, -1.0f, 1.0f},
        {1.0f, -1.0f, -1.0f},
        {1.0f, -1.0f, 1.0f},
        {-1.0f, -1.0f, 1.0f}

    };

    void SkyboxRenderer::initResources()
    {
        skyboxMaterial = vke_common::AssetManager::LoadMaterial(vke_common::BUILTIN_MATERIAL_SKYBOX_ID);
        // skybox mesh
        std::vector<uint32_t> indices;
        for (int i = 0; i < skyboxVertices.size(); i++)
            indices.push_back(i);
        skyboxMesh = std::make_unique<Mesh>(0, std::span<const glm::vec3>(skyboxVertices), std::span<const uint32_t>(indices));
    }

    void SkyboxRenderer::createDescriptorSet()
    {
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            skyBoxDescriptorSets[i] = skyboxMaterial->shader->CreateDescriptorSet(1);
            skyboxMaterial->UpdateDescriptorSet(skyBoxDescriptorSets[i]);
        }

        std::vector<VkWriteDescriptorSet> descriptorWrites(4);

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            // layout (std430, set = 1, binding = 0) uniform UBO { AtmosphereParameter parameters; };
            VkDescriptorBufferInfo bufferInfo = skyboxManager->GetAtmosphereParamBuffer()->GetDescriptorBufferInfo();
            vke_render::ConstructDescriptorSetWrite(descriptorWrites[0], skyBoxDescriptorSets[i], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &bufferInfo);
            Texture2D *transmittanceLUT = skyboxManager->GetTransmittanceLUT();
            VkDescriptorImageInfo transmittanceInfo{
                transmittanceLUT->textureSampler,
                transmittanceLUT->textureImageView,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            vke_render::ConstructDescriptorSetWrite(descriptorWrites[1], skyBoxDescriptorSets[i], 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &transmittanceInfo);
            Texture2D *scatteringLUT = skyboxManager->GetScatteringLUT();
            VkDescriptorImageInfo scatteringInfo{
                scatteringLUT->textureSampler,
                scatteringLUT->textureImageView,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            vke_render::ConstructDescriptorSetWrite(descriptorWrites[2], skyBoxDescriptorSets[i], 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &scatteringInfo);
            // layout(set = 1, binding = 3) uniform samplerCube skyViewLUT;
            CubeMap *skyLUT = skyboxManager->GetSkyLUT(i);
            VkDescriptorImageInfo imageInfo1{skyLUT->sampler, skyLUT->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            vke_render::ConstructDescriptorSetWrite(descriptorWrites[3], skyBoxDescriptorSets[i], 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfo1);
            vkUpdateDescriptorSets(globalLogicalDevice, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
        }
    }

    void SkyboxRenderer::Render(TaskNode &node, FrameGraph &frameGraph, VkCommandBuffer commandBuffer, uint32_t currentFrame, uint32_t imageIndex)
    {
        VkRenderingAttachmentInfo colorAttachmentInfo{};
        colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachmentInfo.pNext = nullptr;
        colorAttachmentInfo.imageView = hdrColorManager->GetImageView(hdrColorImageIndex, currentFrame);
        colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachmentInfo.resolveMode = VK_RESOLVE_MODE_NONE;

        VkRenderingAttachmentInfo depthAttachmentInfo{};
        depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachmentInfo.pNext = nullptr;
        depthAttachmentInfo.imageView = context->depthImageViews[currentFrame];
        depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        // TODO: Revisit this depth store when preservation and FrameGraph write dependencies are separated.
        depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.pNext = nullptr;
        renderingInfo.renderArea = {{0, 0}, {context->width, context->height}};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachmentInfo;
        renderingInfo.pDepthAttachment = &depthAttachmentInfo;

        vkCmdBeginRendering(commandBuffer, &renderingInfo);
        renderPipeline->Bind(commandBuffer);

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

        VkDescriptorSet descriptorSets[2] = {globalDescriptorSets[currentFrame], skyBoxDescriptorSets[currentFrame]};

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                renderPipeline->pipelineLayout, 0, 2, descriptorSets, 0, nullptr);
        const AtmospherePushConstants &pushConstants = skyboxManager->GetAtmospherePushConstants();
        vkCmdPushConstants(commandBuffer, renderPipeline->pipelineLayout, VK_SHADER_STAGE_ALL,
                           0, sizeof(AtmospherePushConstants), &pushConstants);
        skyboxMesh->Render(commandBuffer);

        vkCmdEndRendering(commandBuffer);
    }

}
