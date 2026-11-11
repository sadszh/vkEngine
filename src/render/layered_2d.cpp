#include <render/layered_2d.hpp>
#include <asset/asset_manager.hpp>
#include <algorithm>
#include <cstring>

namespace vke_render
{
    bool QuadIDVertexBufferPool::Ensure(Allocation &allocation, uint32_t count)
    {
        if (count == 0)
        {
            Release(allocation);
            return true;
        }

        if (allocation.Valid() && count <= allocation.capacity)
            return true;

        Release(allocation);
        allocation = allocateCapacity(capacityClass(count));
        return allocation.Valid();
    }

    void QuadIDVertexBufferPool::Update(const Allocation &allocation, const std::vector<QuadID> &quadIDs,
                                        uint32_t currentFrame)
    {
        if (!allocation.Valid() || quadIDs.empty())
            return;
        chunks[allocation.chunkIndex].buffers[currentFrame]->ToBuffer(
            static_cast<size_t>(allocation.offset) * sizeof(QuadID),
            quadIDs.data(),
            quadIDs.size() * sizeof(QuadID));
    }

    void QuadIDVertexBufferPool::Release(Allocation &allocation)
    {
        if (!allocation.Valid())
            return;

        allocation.count = 0;
        freeLists[allocation.capacity].push_back(allocation);
        allocation = {};
    }

    VkBuffer QuadIDVertexBufferPool::GetBuffer(const Allocation &allocation, uint32_t currentFrame) const
    {
        return chunks[allocation.chunkIndex].buffers[currentFrame]->buffer;
    }

    VkDeviceSize QuadIDVertexBufferPool::GetOffset(const Allocation &allocation) const
    {
        return static_cast<VkDeviceSize>(allocation.offset) * sizeof(QuadID);
    }

    QuadIDVertexBufferPool::Allocation QuadIDVertexBufferPool::allocateCapacity(uint32_t capacity)
    {
        auto &freeList = freeLists[capacity];
        if (!freeList.empty())
        {
            Allocation allocation = freeList.back();
            freeList.pop_back();
            allocation.count = 0;
            return allocation;
        }

        for (uint32_t chunkIndex = 0; chunkIndex < chunks.size(); ++chunkIndex)
        {
            Chunk &chunk = chunks[chunkIndex];
            if (capacity <= chunk.capacity - chunk.used)
            {
                Allocation allocation{chunkIndex, chunk.used, capacity, 0};
                chunk.used += capacity;
                return allocation;
            }
        }

        const uint32_t chunkCapacity = std::max(DEFAULT_CHUNK_CAPACITY, capacity);
        Chunk chunk;
        for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
            chunk.buffers[frame] = std::make_unique<HostCoherentBuffer>(
                static_cast<VkDeviceSize>(chunkCapacity) * sizeof(QuadID), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        chunk.capacity = chunkCapacity;
        chunks.push_back(std::move(chunk));

        Allocation allocation{static_cast<uint32_t>(chunks.size() - 1), 0, capacity, 0};
        chunks.back().used = capacity;
        return allocation;
    }

    uint32_t QuadIDVertexBufferPool::capacityClass(uint32_t count)
    {
        uint32_t capacity = 1;
        while (capacity < count)
            capacity <<= 1;
        return capacity;
    }

    Layered2DRenderUnit::~Layered2DRenderUnit()
    {
        if (quadIDPool != nullptr)
            quadIDPool->Release(quadIDAllocation);
    }

    void Layered2DRenderUnit::SetQuadIDs(std::vector<QuadID> ids)
    {
        quadIDs = std::move(ids);
        if (quadIDs.empty())
        {
            quadIDPool->Release(quadIDAllocation);
            quadIDUpdateCnt = 0;
            return;
        }

        if (quadIDPool->Ensure(quadIDAllocation, static_cast<uint32_t>(quadIDs.size())))
        {
            quadIDAllocation.count = static_cast<uint32_t>(quadIDs.size());
            quadIDUpdateCnt = MAX_FRAMES_IN_FLIGHT;
        }
    }

    void Layered2DRenderUnit::Render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                                     const glm::vec2 &viewportSize, const glm::vec2 &atlasSize, uint32_t currentFrame)
    {
        if (quadIDs.empty() || !quadIDAllocation.Valid() || quadIDPool == nullptr)
            return;

        if (quadIDUpdateCnt > 0)
        {
            quadIDPool->Update(quadIDAllocation, quadIDs, currentFrame);
            --quadIDUpdateCnt;
        }
        VkBuffer quadIDBuffer = quadIDPool->GetBuffer(quadIDAllocation, currentFrame);
        VkDeviceSize offset = quadIDPool->GetOffset(quadIDAllocation);
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &quadIDBuffer, &offset);
        Layered2DRenderPushConstants constants{modelMatrix, viewportSize, atlasSize};
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_ALL, 0, sizeof(constants), &constants);
        vkCmdDraw(commandBuffer, 6, quadIDAllocation.count, 0, 0);
    }

    const Layered2DMaterialState &Layered2DRenderer::getOrCreateMaterialState(const std::shared_ptr<Material> &material)
    {
        auto it = materialStates.find(material.get());
        if (it != materialStates.end())
            return *it->second;

        auto state = std::make_unique<Layered2DMaterialState>();
        state->material = material;
        if (!material->textures.empty())
        {
            state->commonDescriptorSet = material->shader->CreateDescriptorSet(1);
            if (state->commonDescriptorSet != VK_NULL_HANDLE)
                material->UpdateDescriptorSet(state->commonDescriptorSet);
        }
        createGraphicsPipeline(*state);
        return *materialStates.emplace(material.get(), std::move(state)).first->second;
    }

    void Layered2DRenderer::createGraphicsPipeline(Layered2DMaterialState &state)
    {
        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &context->colorFormat;
        renderingInfo.depthAttachmentFormat = VK_FORMAT_UNDEFINED;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (state.material->blendMode == MaterialBlendMode::ADDITIVE)
        {
            blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        }
        else if (state.material->blendMode == MaterialBlendMode::PREMULTIPLIED_ALPHA)
        {
            blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        }
        else
        {
            blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        }

        VkPipelineColorBlendStateCreateInfo blendState{};
        blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blendState.attachmentCount = 1;
        blendState.pAttachments = &blendAttachment;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        VkPipelineRasterizationStateCreateInfo rasterization{};
        rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterization.cullMode = VK_CULL_MODE_NONE;

        pipelineInfo.pRasterizationState = &rasterization;
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &blendState;
        state.pipeline = std::make_unique<GraphicsPipeline>(
            state.material->shader, std::vector<uint32_t>{sizeof(QuadID)}, VK_VERTEX_INPUT_RATE_INSTANCE, pipelineInfo);
    }

    Layered2DRenderer::~Layered2DRenderer()
    {
        for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
        {
            VulkanResourceManager::FreeDescriptorSet(imageDescriptorSets[frame]);
        }
    }

    void Layered2DRenderer::resetImageTextures()
    {
        imageTextureIndices.clear();
        imageTextures.clear();
        imageTextures.push_back({whiteTexture, 0});
        freeImageTextureSlots.clear();
        for (auto &textures : frameImageTextures)
            textures.clear();
        imageTexturesDirty.fill(true);
    }

    vke_common::Result<uint32_t> Layered2DRenderer::RegisterImageTexture(const std::shared_ptr<Texture2D> &texture)
    {
        if (!texture)
            return 0;
        auto it = imageTextureIndices.find(texture.get());
        if (it != imageTextureIndices.end())
        {
            ++imageTextures[it->second].references;
            return it->second;
        }
        uint32_t index;
        if (!freeImageTextureSlots.empty())
        {
            index = freeImageTextureSlots.back();
            freeImageTextureSlots.pop_back();
            imageTextures[index] = {texture, 1};
        }
        else
        {
            if (imageTextures.size() == DEFAULT_BINDLESS_CNT)
                return std::unexpected("UI image texture table is full");
            index = static_cast<uint32_t>(imageTextures.size());
            imageTextures.push_back({texture, 1});
        }
        imageTextureIndices.emplace(texture.get(), index);
        imageTexturesDirty.fill(true);
        return index;
    }

    void Layered2DRenderer::UnregisterImageTexture(uint32_t index)
    {
        if (index == 0)
            return;
        auto &slot = imageTextures[index];
        if (--slot.references != 0)
            return;
        imageTextureIndices.erase(slot.texture.get());
        slot.texture.reset();
        freeImageTextureSlots.push_back(index);
        imageTexturesDirty.fill(true);
    }

    void Layered2DRenderer::syncImageTextures(uint32_t currentFrame)
    {
        if (!imageTexturesDirty[currentFrame])
            return;
        std::vector<std::shared_ptr<Texture2D>> textures;
        textures.reserve(imageTextures.size());
        std::vector<VkDescriptorImageInfo> infos;
        infos.reserve(imageTextures.size());
        for (const auto &slot : imageTextures)
        {
            const auto &texture = slot.texture ? slot.texture : whiteTexture;
            textures.push_back(texture);
            infos.push_back({texture->textureSampler, texture->textureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
        }
        VkWriteDescriptorSet write{};
        ConstructDescriptorSetWrite(write, imageDescriptorSets[currentFrame], 1,
                                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, infos.data(), 0,
                                    static_cast<uint32_t>(infos.size()));
        vkUpdateDescriptorSets(globalLogicalDevice, 1, &write, 0, nullptr);

        frameImageTextures[currentFrame] = std::move(textures);
        imageTexturesDirty[currentFrame] = false;
    }

    void Layered2DRenderer::Init(int subpassID, FrameGraph &frameGraph,
                                 std::map<std::string, vke_ds::id32_t> &blackboard,
                                 ResourceNodeIDMap &currentResourceNodeID)
    {
        RenderPassBase::Init(subpassID, frameGraph, blackboard, currentResourceNodeID);
        auto textShaderResult = vke_common::AssetManager::LoadVertFragShader(vke_common::BUILTIN_VFSHADER_TEXT_ID);
        VKE_FATAL_IF(!textShaderResult, "{}", textShaderResult.error())
        textShader = std::move(*textShaderResult);
        defaultMaterial = std::make_shared<Material>(0);
        defaultMaterial->shader = textShader;
        defaultMaterial->renderMode = MaterialRenderMode::BLEND_MODE;

        imageMaterial = std::make_shared<Material>(0);
        auto imageShaderResult = vke_common::AssetManager::LoadVertFragShader(vke_common::BUILTIN_VFSHADER_UI_IMAGE_ID);
        VKE_FATAL_IF(!imageShaderResult, "{}", imageShaderResult.error())
        imageMaterial->shader = std::move(*imageShaderResult);
        imageMaterial->renderMode = MaterialRenderMode::BLEND_MODE;
        uint32_t white = 0xffffffff;
        whiteTexture = std::make_shared<Texture2D>(0, &white, 1, 1,
                                                   VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                   VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false, false);
        resetImageTextures();

        constructFrameGraph(frameGraph, blackboard, currentResourceNodeID);
        createDescriptorSets();
    }

    Layered2DRenderer::FontState &Layered2DRenderer::getOrCreateFontState(const std::shared_ptr<vke_common::Font> &font)
    {
        auto it = fontStates.find(font.get());
        if (it != fontStates.end()) return *it->second;
        auto state = std::make_unique<FontState>();
        state->font = font;
        for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
        {
            state->stagingBuffers[frame] = std::make_unique<HostCoherentBuffer>(
                static_cast<VkDeviceSize>(vke_common::Font::ATLAS_SIZE) * vke_common::Font::ATLAS_SIZE,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
            state->textures[frame] = std::make_unique<Texture2D>(
                font->handle, const_cast<uint8_t *>(font->GetDynamicAtlasPixels().data()),
                vke_common::Font::ATLAS_SIZE, vke_common::Font::ATLAS_SIZE,
                VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false, false);
            state->descriptorSets[frame] = textShader->CreateDescriptorSet(0);
            VkDescriptorBufferInfo quadBufferInfo = quadManager->GetDescriptorBufferInfo(frame);

            VkDescriptorImageInfo staticAtlasInfo{
                font->atlasTexture->textureSampler, font->atlasTexture->textureImageView,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkDescriptorImageInfo dynamicAtlasInfo{
                state->textures[frame]->textureSampler, state->textures[frame]->textureImageView,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet writes[3];
            ConstructDescriptorSetWrite(writes[0], state->descriptorSets[frame], 0,
                                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &staticAtlasInfo);
            ConstructDescriptorSetWrite(writes[1], state->descriptorSets[frame], 1,
                                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &dynamicAtlasInfo);
            ConstructDescriptorSetWrite(writes[2], state->descriptorSets[frame], 2,
                                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &quadBufferInfo);
            vkUpdateDescriptorSets(globalLogicalDevice, 3, writes, 0, nullptr);
        }

        return *fontStates.emplace(font.get(), std::move(state)).first->second;
    }

    void Layered2DRenderer::UpdateUnitFont(vke_ds::id32_t unitID, const std::shared_ptr<vke_common::Font> &font)
    {
        auto it = units.find(unitID);
        if (it != units.end()) it->second.fontState = &getOrCreateFontState(font);
    }

    vke_ds::id32_t Layered2DRenderer::AllocateUnit(const std::shared_ptr<Material> &material,
                                                   const glm::mat4 &modelMatrix, const std::vector<QuadID> &quadIDs,
                                                   const std::shared_ptr<vke_common::Font> &unitFont)
    {
        const vke_ds::id32_t unitID = unitAllocator.Alloc();
        UnitState state;
        state.materialState = &getOrCreateMaterialState(material == nullptr ? defaultMaterial : material);
        if (state.materialState->material != imageMaterial)
            state.fontState = &getOrCreateFontState(unitFont);
        state.unit = std::make_unique<Layered2DRenderUnit>(unitID, modelMatrix, &quadIDPool);
        state.unit->SetQuadIDs(quadIDs);
        units.emplace(unitID, std::move(state));
        return unitID;
    }

    void Layered2DRenderer::DestroyUnit(vke_ds::id32_t unitID)
    {
        units.erase(unitID);
    }

    bool Layered2DRenderer::UpdateUnitQuadIDs(vke_ds::id32_t unitID, const std::vector<QuadID> &quadIDs)
    {
        auto it = units.find(unitID);
        if (it == units.end())
            return false;

        it->second.unit->SetQuadIDs(quadIDs);
        return true;
    }

    Layered2DRenderUnit *Layered2DRenderer::GetUnit(vke_ds::id32_t unitID)
    {
        auto it = units.find(unitID);
        return it == units.end() ? nullptr : it->second.unit.get();
    }

    void Layered2DRenderer::constructFrameGraph(FrameGraph &frameGraph,
                                                std::map<std::string, vke_ds::id32_t> &blackboard,
                                                ResourceNodeIDMap &currentResourceNodeID)
    {
        const vke_ds::id32_t colorResourceID = blackboard["colorAttachment"];
        const vke_ds::id32_t outColorNodeID = frameGraph.AllocResourceNode("layered2DOutColor", colorResourceID);

        const vke_ds::id32_t taskNodeID = frameGraph.AllocTaskNode(
            "layered 2D render", RENDER_TASK,
            std::bind(&Layered2DRenderer::Render, this, std::placeholders::_1, std::placeholders::_2,
                      std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
        frameGraph.AddTaskNodeResourceRef(taskNodeID, currentResourceNodeID[colorResourceID], outColorNodeID,
                                          VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                          VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
                                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        currentResourceNodeID[colorResourceID] = outColorNodeID;
    }

    void Layered2DRenderer::createDescriptorSets()
    {
        for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
        {
            VkDescriptorBufferInfo quadBufferInfo = quadManager->GetDescriptorBufferInfo(frame);

            imageDescriptorSets[frame] = imageMaterial->shader->CreateDescriptorSet(0);
            VkWriteDescriptorSet imageBufferWrite{};
            ConstructDescriptorSetWrite(imageBufferWrite, imageDescriptorSets[frame], 0,
                                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &quadBufferInfo);
            vkUpdateDescriptorSets(globalLogicalDevice, 1, &imageBufferWrite, 0, nullptr);
        }
    }

    void Layered2DRenderer::Render(TaskNode &, FrameGraph &, VkCommandBuffer commandBuffer,
                                   uint32_t currentFrame, uint32_t imageIndex)
    {
        for (auto &[key, state] : fontStates)
            syncDynamicAtlas(*state, commandBuffer, currentFrame);
        syncImageTextures(currentFrame);
        if (units.empty())
            return;

        std::vector<UnitState *> sorted;
        sorted.reserve(units.size());
        for (auto &entry : units)
            sorted.push_back(&entry.second);
        std::sort(sorted.begin(), sorted.end(), [](const UnitState *a, const UnitState *b)
                  { return a->unit->modelMatrix[3].z < b->unit->modelMatrix[3].z; });

        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = context->colorImageViews[imageIndex];
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea = {{0, 0}, {context->width, context->height}};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;
        vkCmdBeginRendering(commandBuffer, &renderingInfo);

        VkViewport viewport{0.0f, 0.0f, static_cast<float>(context->width),
                            static_cast<float>(context->height), 0.0f, 1.0f};
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        VkRect2D scissor{{0, 0}, {context->width, context->height}};
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        const glm::vec2 viewportSize(static_cast<float>(context->width), static_cast<float>(context->height));
        const Layered2DMaterialState *boundMaterialState = nullptr;
        VkDescriptorSet boundRendererSet = VK_NULL_HANDLE;
        for (const UnitState *draw : sorted)
        {
            const Layered2DMaterialState &state = *draw->materialState;
            const VkPipelineLayout pipelineLayout = state.pipeline->pipelineLayout;
            if (boundMaterialState != &state)
            {
                state.pipeline->Bind(commandBuffer);
                if (state.commonDescriptorSet != VK_NULL_HANDLE)
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            pipelineLayout, 1, 1, &state.commonDescriptorSet, 0, nullptr);
                state.material->SetPushConstants(commandBuffer, pipelineLayout);
                boundMaterialState = &state;
                boundRendererSet = VK_NULL_HANDLE;
            }
            const VkDescriptorSet rendererSet = draw->fontState
                ? draw->fontState->descriptorSets[currentFrame] : imageDescriptorSets[currentFrame];
            if (boundRendererSet != rendererSet)
            {
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipelineLayout, 0, 1, &rendererSet, 0, nullptr);
                boundRendererSet = rendererSet;
            }
            const glm::vec2 atlasSize = draw->fontState
                ? glm::vec2(draw->fontState->font->atlasWidth, draw->fontState->font->atlasHeight) : glm::vec2(1.0f);
            draw->unit->Render(commandBuffer, pipelineLayout, viewportSize, atlasSize, currentFrame);
        }
        vkCmdEndRendering(commandBuffer);
    }

    void Layered2DRenderer::OnWindowResize(FrameGraph &, RenderContext *ctx)
    {
        context = ctx;
    }

    void Layered2DRenderer::syncDynamicAtlas(FontState &state, VkCommandBuffer commandBuffer, uint32_t currentFrame)
    {
        if (state.font->GetDynamicAtlasUpdateCnt() == 0)
            return;
        const auto &pixels = state.font->GetDynamicAtlasPixels();
        state.stagingBuffers[currentFrame]->ToBuffer(0, pixels.data(), pixels.size());

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = state.textures[currentFrame]->textureImage;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        VulkanResourceManager::CopyBufferToImage(commandBuffer, state.stagingBuffers[currentFrame]->buffer,
                                             state.textures[currentFrame]->textureImage,
                                             vke_common::Font::ATLAS_SIZE, vke_common::Font::ATLAS_SIZE);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        state.font->ConsumeDynamicAtlasUpdate();
    }
}
