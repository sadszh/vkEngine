#include <render/transparent_pass.hpp>
#include <asset/asset_manager.hpp>
#include <algorithm>

namespace vke_render
{
    static const std::vector<uint32_t> transparentVertexAttributeSizes = {
        sizeof(Vertex::pos), sizeof(Vertex::normal), sizeof(Vertex::tangent), sizeof(Vertex::texCoord)};

    TransparentPass::TransparentPass(RenderContext *ctx, VkDescriptorSet *globalDescriptorSets,
                                     LightManager *lightManager, SkyboxManager *skyboxManager,
                                     ShadowManager *shadowManager, HDRColorManager *hdrColorManager,
                                     const CameraInfo *cameraInfo)
        : RenderPassBase(TRANSPARENT_PASS, ctx, globalDescriptorSets),
          lightManager(lightManager), skyboxManager(skyboxManager), shadowManager(shadowManager),
          hdrColorManager(hdrColorManager), cameraInfo(cameraInfo), unitAllocator(1), taskNodeID(0)
    {
    }

    void TransparentPass::Init(int subpassID, FrameGraph &frameGraph,
                               std::map<std::string, vke_ds::id32_t> &blackboard,
                               ResourceNodeIDMap &currentResourceNodeID)
    {
        RenderPassBase::Init(subpassID, frameGraph, blackboard, currentResourceNodeID);
        brdfLUT = vke_common::AssetManager::LoadTexture2D(vke_common::BUILTIN_TEXTURE_BRDF_LUT_ID);
        constructFrameGraph(frameGraph, blackboard, currentResourceNodeID);
    }

    void TransparentPass::constructFrameGraph(FrameGraph &frameGraph,
                                              std::map<std::string, vke_ds::id32_t> &blackboard,
                                              ResourceNodeIDMap &currentResourceNodeID)
    {
        hdrColorImageIndex = hdrColorManager->GetLatestImageIndex();
        const vke_ds::id32_t outputNodeID = frameGraph.AllocResourceNode(
            "transparentOutHDRColor", hdrColorManager->GetResourceID(hdrColorImageIndex));
        taskNodeID = frameGraph.AllocTaskNode(
            "transparent", RENDER_TASK,
            std::bind(&TransparentPass::Render, this, std::placeholders::_1, std::placeholders::_2,
                      std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
        frameGraph.AddTransientReadyCallback(
            std::bind(&TransparentPass::onTransientResourcesReady, this, std::placeholders::_1, std::placeholders::_2));

        frameGraph.AddTaskNodeResourceRef(
            taskNodeID, hdrColorManager->GetResourceNodeID(hdrColorImageIndex), outputNodeID,
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        const vke_ds::id32_t depthResourceID = blackboard.at("depthAttachment");
        frameGraph.AddTaskNodeResourceRef(
            taskNodeID, currentResourceNodeID.at(depthResourceID), 0,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

        for (const char *clusterName : {"pointLightClusterBuffer", "spotLightClusterBuffer"})
        {
            const vke_ds::id32_t resourceID = blackboard.at(clusterName);
            frameGraph.AddTaskNodeResourceRef(
                taskNodeID, currentResourceNodeID.at(resourceID), 0,
                VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_DONT_CARE);
        }

        for (const char *lutName : {"skyIrradianceLUT", "skySpecularLUT"})
        {
            const vke_ds::id32_t resourceID = blackboard.at(lutName);
            frameGraph.AddTaskNodeResourceRef(
                taskNodeID, currentResourceNodeID.at(resourceID), 0,
                VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        for (const char *shadowName : {"directionalShadowMap0", "spotShadowMap"})
        {
            auto it = blackboard.find(shadowName);
            if (it == blackboard.end())
                continue;
            frameGraph.AddTaskNodeResourceRef(
                taskNodeID, currentResourceNodeID.at(it->second), 0,
                VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        hdrColorManager->UpdateResourceNode(hdrColorImageIndex, outputNodeID);
    }

    void TransparentPass::registerMaterial(std::shared_ptr<Material> &material)
    {
        Material *key = material.get();
        if (materialStates.contains(key))
            return;
        VKE_FATAL_IF(material->renderMode != MaterialRenderMode::BLEND_MODE,
                     "Non-blend material registered with transparent pass")

        auto state = std::make_unique<MaterialState>();
        state->material = material;
        if (!material->textures.empty())
        {
            state->commonDescriptorSet = material->shader->CreateDescriptorSet(1);
            material->UpdateDescriptorSet(state->commonDescriptorSet);
        }
        for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
        {
            state->environmentDescriptorSets[frame] = material->shader->CreateDescriptorSet(3);
            // Registration can happen after the frame graph's initial ready callbacks,
            // including when rebuilding a scene after Reset. These LUTs already exist.
            updateEnvironmentDescriptorSet(*state, frame);
        }
        createGraphicsPipeline(*state);
        materialStates[key] = std::move(state);
    }

    void TransparentPass::createGraphicsPipeline(MaterialState &state)
    {
        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 1;
        static constexpr VkFormat hdrFormat = HDRColorManager::HDR_COLOR_FORMAT;
        renderingInfo.pColorAttachmentFormats = &hdrFormat;
        renderingInfo.depthAttachmentFormat = context->depthFormat;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.dstColorBlendFactor = state.material->blendMode == MaterialBlendMode::ADDITIVE
                                                  ? VK_BLEND_FACTOR_ONE
                                                  : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.srcColorBlendFactor =
            state.material->blendMode == MaterialBlendMode::PREMULTIPLIED_ALPHA
                ? VK_BLEND_FACTOR_ONE
                : VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = state.material->blendMode == MaterialBlendMode::ADDITIVE
                                                  ? VK_BLEND_FACTOR_ONE
                                                  : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

        VkPipelineColorBlendStateCreateInfo blendState{};
        blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blendState.attachmentCount = 1;
        blendState.pAttachments = &blendAttachment;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &blendState;
        state.pipeline = std::make_unique<GraphicsPipeline>(
            state.material->shader, transparentVertexAttributeSizes,
            VK_VERTEX_INPUT_RATE_VERTEX, pipelineInfo);
    }

    vke_ds::id64_t TransparentPass::AddUnit(std::shared_ptr<Material> &material, RenderUnit *unit)
    {
        registerMaterial(material);
        const vke_ds::id64_t id = unitAllocator.Alloc();
        units[id] = {material.get(), unit};
        return id;
    }

    void TransparentPass::RemoveUnit(vke_ds::id64_t id)
    {
        units.erase(id);
    }

    void TransparentPass::updateEnvironmentDescriptorSet(MaterialState &state, uint32_t currentFrame)
    {
        VkDescriptorSet descriptorSet = state.environmentDescriptorSets[currentFrame];
        if (descriptorSet == VK_NULL_HANDLE)
            return;
        CubeMap *irradiance = skyboxManager->GetSkyIrradianceLUT(currentFrame);
        CubeMap *specular = skyboxManager->GetSkySpecularLUT(currentFrame);
        VkDescriptorImageInfo imageInfos[3] = {
            {irradiance->sampler, irradiance->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {specular->sampler, specular->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {brdfLUT->textureSampler, brdfLUT->textureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkWriteDescriptorSet writes[3];
        for (uint32_t binding = 0; binding < 3; ++binding)
            ConstructDescriptorSetWrite(writes[binding], descriptorSet, binding,
                                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfos[binding]);
        vkUpdateDescriptorSets(globalLogicalDevice, 3, writes, 0, nullptr);
    }

    void TransparentPass::onTransientResourcesReady(FrameGraph &, uint32_t currentFrame)
    {
        for (auto &entry : materialStates)
            updateEnvironmentDescriptorSet(*entry.second, currentFrame);
    }

    void TransparentPass::Render(TaskNode &, FrameGraph &, VkCommandBuffer commandBuffer,
                                 uint32_t currentFrame, uint32_t)
    {
        if (units.empty())
            return;

        std::vector<SortedUnit> sorted;
        sorted.reserve(units.size());
        const Frustum frustum(cameraInfo->projection * cameraInfo->view);
        for (auto &entry : units)
        {
            if (!entry.second.unit->IsVisible(frustum))
                continue;
            const glm::vec4 worldOrigin = entry.second.unit->modelMatrix[3];
            sorted.push_back({&entry.second, (cameraInfo->view * worldOrigin).z});
        }
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const SortedUnit &a, const SortedUnit &b)
                         { return a.viewDepth < b.viewDepth; });

        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = hdrColorManager->GetImageView(hdrColorImageIndex, currentFrame);
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingAttachmentInfo depthAttachment{};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = context->depthImageViews[currentFrame];
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea = {{0, 0}, {context->width, context->height}};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;
        renderingInfo.pDepthAttachment = &depthAttachment;
        vkCmdBeginRendering(commandBuffer, &renderingInfo);

        VkViewport viewport{0.0f, 0.0f, static_cast<float>(context->width),
                            static_cast<float>(context->height), 0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, {context->width, context->height}};
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        Material *boundMaterial = nullptr;
        for (const SortedUnit &draw : sorted)
        {
            MaterialState &state = *materialStates.at(draw.entry->material);
            if (boundMaterial != draw.entry->material)
            {
                state.pipeline->Bind(commandBuffer);
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        state.pipeline->pipelineLayout, 0, 1,
                                        &globalDescriptorSets[currentFrame], 0, nullptr);
                if (state.commonDescriptorSet != VK_NULL_HANDLE)
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            state.pipeline->pipelineLayout, 1, 1,
                                            &state.commonDescriptorSet, 0, nullptr);
                VkDescriptorSet shadowSet = shadowManager->GetDeferredLightingDescriptorSet(currentFrame);
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        state.pipeline->pipelineLayout, 2, 1, &shadowSet, 0, nullptr);
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        state.pipeline->pipelineLayout, 3, 1,
                                        &state.environmentDescriptorSets[currentFrame], 0, nullptr);
                state.material->SetPushConstants(commandBuffer, state.pipeline->pipelineLayout);
                const glm::uvec4 forwardInfo{
                    lightManager->GetLightCnt(LightType::DIRECTIONAL_LIGHT),
                    context->width, context->height,
                    state.material->blendMode == MaterialBlendMode::PREMULTIPLIED_ALPHA ? 1u : 0u};
                vkCmdPushConstants(commandBuffer, state.pipeline->pipelineLayout, VK_SHADER_STAGE_ALL,
                                   112, sizeof(forwardInfo), &forwardInfo);
                boundMaterial = draw.entry->material;
            }
            draw.entry->unit->Render(commandBuffer, state.pipeline->pipelineLayout,
                                     state.commonDescriptorSet != VK_NULL_HANDLE ? 4 : 1);
        }
        vkCmdEndRendering(commandBuffer);
    }
}
