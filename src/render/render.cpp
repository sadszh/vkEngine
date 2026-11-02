#include <render/render.hpp>
#include <logger.hpp>
#include <algorithm>

namespace vke_render
{
    Renderer *Renderer::instance;

    void Renderer::Reset()
    {
        WaitIdle();
        instance->renderUpdateCallbacks.clear();
        instance->cameras.clear();
        instance->resizeEventHub.Reset();
        instance->cameraIDAllocator = vke_ds::NaiveIDAllocator<vke_ds::id32_t>(1);
        instance->currentCamera = std::numeric_limits<vke_ds::id32_t>::max();
        for (auto &pass : instance->subPasses)
            pass->Reset();
        instance->lightManager->Reset();
        instance->quadManager.Reset();
        // Keep frame-graph synchronization counters and the swapchain intact.
        instance->hostCameraInfo = CameraInfo(0.1f, 1000.0f, glm::radians(60.0f), 1.0f,
                                              glm::mat4(1.0f), glm::mat4(1.0f), glm::vec3(0.0f));
        instance->cameraInfoUpdateCnt = MAX_FRAMES_IN_FLIGHT;
    }

    void Renderer::initDescriptorSet()
    {
        std::vector<VkDescriptorSetLayoutBinding> bindingInfos;
        VkDescriptorSetLayoutBinding camInfoLayoutBinding{};
        camInfoLayoutBinding.binding = 0;
        camInfoLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        camInfoLayoutBinding.descriptorCount = 1;
        camInfoLayoutBinding.stageFlags = VK_SHADER_STAGE_ALL;
        bindingInfos.push_back(camInfoLayoutBinding);
        // nolight
        globalDescriptorSetInfos[GLOBAL_DESCRIPTOR_SET_NO_LIGHT].AddCnt(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1);
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        layoutInfo.pBindings = bindingInfos.data();
        vkCreateDescriptorSetLayout(globalLogicalDevice, &layoutInfo,
                                    nullptr, &(globalDescriptorSetInfos[GLOBAL_DESCRIPTOR_SET_NO_LIGHT].layout));

        // light
        globalDescriptorSetInfos[GLOBAL_DESCRIPTOR_SET_LIGHT].AddCnt(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1);
        lightManager->GetBindingInfos(bindingInfos, globalDescriptorSetInfos[GLOBAL_DESCRIPTOR_SET_LIGHT]);
        layoutInfo.bindingCount = bindingInfos.size();
        layoutInfo.pBindings = bindingInfos.data();
        vkCreateDescriptorSetLayout(globalLogicalDevice, &layoutInfo,
                                    nullptr, &(globalDescriptorSetInfos[GLOBAL_DESCRIPTOR_SET_LIGHT].layout));

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            globalDescriptorSets[GLOBAL_DESCRIPTOR_SET_NO_LIGHT][i] = VulkanResourceManager::AllocateDescriptorSet(globalDescriptorSetInfos[GLOBAL_DESCRIPTOR_SET_NO_LIGHT]);
            globalDescriptorSets[GLOBAL_DESCRIPTOR_SET_LIGHT][i] = VulkanResourceManager::AllocateDescriptorSet(globalDescriptorSetInfos[GLOBAL_DESCRIPTOR_SET_LIGHT]);
        }

        // write descriptor set
        VkDescriptorBufferInfo camBufferInfos[MAX_FRAMES_IN_FLIGHT] = {camInfoBuffers[0].GetDescriptorBufferInfo(), camInfoBuffers[1].GetDescriptorBufferInfo()};
        VkWriteDescriptorSet noLightDescriptorWrite{};
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            ConstructDescriptorSetWrite(noLightDescriptorWrite, globalDescriptorSets[GLOBAL_DESCRIPTOR_SET_NO_LIGHT][i], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &camBufferInfos[i]);
            vkUpdateDescriptorSets(globalLogicalDevice, 1, &noLightDescriptorWrite, 0, nullptr);
        }

        std::vector<VkWriteDescriptorSet> lightDescriptorWrites(1, VkWriteDescriptorSet{});
        std::vector<VkDescriptorBufferInfo> lightBufferInfos;

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            ConstructDescriptorSetWrite(lightDescriptorWrites[0], globalDescriptorSets[GLOBAL_DESCRIPTOR_SET_LIGHT][i], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &camBufferInfos[i]);
            lightManager->GetDescriptorSetWrites(i, lightDescriptorWrites, globalDescriptorSets[GLOBAL_DESCRIPTOR_SET_LIGHT][i], lightBufferInfos);
            vkUpdateDescriptorSets(globalLogicalDevice, lightDescriptorWrites.size(), lightDescriptorWrites.data(), 0, nullptr);
            lightDescriptorWrites.resize(1);
            lightBufferInfos.clear();
        }
        lightManager->SetGlobalDescriptorSets(globalDescriptorSets[GLOBAL_DESCRIPTOR_SET_LIGHT]);
    }

    void Renderer::constructFrameGraph(std::map<std::string, vke_ds::id32_t> &blackboard,
                                       ResourceNodeIDMap &currentResourceNodeID)
    {
        instance->frameGraph = std::make_unique<FrameGraph>(MAX_FRAMES_IN_FLIGHT, context);
        colorAttachmentResourceID = frameGraph->AddPermanentImageResource("colorAttachment", true, context->colorImages.data(), VK_IMAGE_ASPECT_COLOR_BIT, true,
                                                                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_IMAGE_LAYOUT_UNDEFINED, context->outColorLayout);
        depthAttachmentResourceID = frameGraph->AddPermanentImageResource("depthAttachment", true, context->depthImages, VK_IMAGE_ASPECT_DEPTH_BIT, false,
                                                                          VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, std::nullopt, std::nullopt);

        frameGraph->AddTargetResource(colorAttachmentResourceID);
        frameGraph->AddTargetResource(depthAttachmentResourceID);

        blackboard["colorAttachment"] = colorAttachmentResourceID;
        blackboard["depthAttachment"] = depthAttachmentResourceID;

        vke_ds::id32_t oriColorResourceNodeID = frameGraph->AllocResourceNode("oriColor", colorAttachmentResourceID);
        vke_ds::id32_t oriDepthResourceNodeID = frameGraph->AllocResourceNode("oriDepth", depthAttachmentResourceID);

        currentResourceNodeID[colorAttachmentResourceID] = oriColorResourceNodeID;
        currentResourceNodeID[depthAttachmentResourceID] = oriDepthResourceNodeID;
        hdrColorManager = std::make_unique<HDRColorManager>(context);
        hdrColorManager->ConstructFrameGraph(*frameGraph, blackboard, currentResourceNodeID);
    }

    void Renderer::cleanup() {}

    void Renderer::recreate(RenderContext *ctx)
    {
        context = ctx;
        ((ImageResource *)frameGraph->resources[colorAttachmentResourceID].get())->images = context->colorImages;
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            ((ImageResource *)frameGraph->resources[depthAttachmentResourceID].get())->images[i] = context->depthImages[i];
        }

        hdrColorManager->OnWindowResize(*frameGraph, context);
    }

    void Renderer::render()
    {
        uint32_t imageIndex = context->AcquireNextImage(currentFrame);

        frameGraph->Sync(currentFrame);
#ifdef VKE_ENABLE_FRAME_GRAPH_PROFILING
        frameGraph->LogProfile(currentFrame);
#endif

        for (auto &kv : renderUpdateCallbacks)
            kv.second(currentFrame);

        bool cameraUpdated = cameraInfoUpdateCnt > 0;
        if (cameraUpdated)
        {
            VKE_LOG_DEBUG("CAM INFO UPD {}", cameraInfoUpdateCnt)
            --cameraInfoUpdateCnt;
            camInfoBuffers[currentFrame].ToBuffer(0, &hostCameraInfo, sizeof(vke_render::CameraInfo));
        }

        lightManager->Update(currentFrame, cameraUpdated);
        skyboxManager->Update(hostCameraInfo.viewPos.y);
        quadManager.Sync(currentFrame);

        frameGraph->PrepareForExecute(currentFrame);

        frameGraph->Execute(currentFrame, imageIndex);
        context->Present(currentFrame, imageIndex);
    }

    void Renderer::Update()
    {
        render();
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }
}
