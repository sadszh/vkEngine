#include <editor/render.hpp>
#include <render/vulkan_resource_manager.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

namespace vke_editor
{
    EditorRenderer *EditorRenderer::instance = nullptr;

    static void CheckImGuiVkResult(VkResult result)
    {
        VKE_VK_CHECK(result, "ImGui Vulkan backend call failed!")
    }

    EditorRenderer *EditorRenderer::Init(GLFWwindow *window,
                                         vke_render::RenderContext *ctx,
                                         uint32_t sceneViewportWidth,
                                         uint32_t sceneViewportHeight)
    {
        instance = new EditorRenderer();
        instance->currentFrame = 0;
        instance->window = window;
        instance->context = ctx;
        instance->sceneTexturesRegistered = false;
        vke_render::RenderEnvironment *env = vke_render::RenderEnvironment::GetInstance();

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (int i = 0; i < vke_render::MAX_FRAMES_IN_FLIGHT; ++i)
        {
            VKE_VK_CHECK(vkCreateSemaphore(vke_render::globalLogicalDevice, &semaphoreInfo, nullptr,
                                           &instance->sceneImageAvailableSemaphores[i]),
                         "Failed to create editor scene semaphore!")
            VKE_VK_CHECK(vkCreateSemaphore(vke_render::globalLogicalDevice, &semaphoreInfo, nullptr,
                                           &instance->sceneRenderFinishedSemaphores[i]),
                         "Failed to create editor scene render finished semaphore!")
            VKE_VK_CHECK(vkCreateFence(vke_render::globalLogicalDevice, &fenceInfo, nullptr,
                                       &instance->sceneInFlightFences[i]),
                         "Failed to create editor scene in-flight fence!")
        }

        VkCommandPoolCreateInfo commandPoolInfo{};
        commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        commandPoolInfo.queueFamilyIndex = env->queueFamilyIndices.graphicsAndComputeFamily.value();
        VKE_VK_CHECK(vkCreateCommandPool(vke_render::globalLogicalDevice, &commandPoolInfo, nullptr,
                                         &instance->commandPool),
                     "Failed to create editor command pool!")

        VkCommandBufferAllocateInfo commandBufferInfo{};
        commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandBufferInfo.commandPool = instance->commandPool;
        commandBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandBufferInfo.commandBufferCount = vke_render::MAX_FRAMES_IN_FLIGHT;
        VKE_VK_CHECK(vkAllocateCommandBuffers(vke_render::globalLogicalDevice, &commandBufferInfo,
                                              instance->commandBuffers),
                     "Failed to allocate editor command buffers!")

        VkSemaphoreSubmitInfo signalSemaphoreInfos[vke_render::MAX_FRAMES_IN_FLIGHT]{};
        for (int i = 0; i < vke_render::MAX_FRAMES_IN_FLIGHT; ++i)
        {
            signalSemaphoreInfos[i].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signalSemaphoreInfos[i].semaphore = instance->sceneImageAvailableSemaphores[i];
            signalSemaphoreInfos[i].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            signalSemaphoreInfos[i].deviceIndex = 0;
        }

        VkSubmitInfo2 initialSignalSubmitInfo{};
        initialSignalSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        initialSignalSubmitInfo.signalSemaphoreInfoCount = vke_render::MAX_FRAMES_IN_FLIGHT;
        initialSignalSubmitInfo.pSignalSemaphoreInfos = signalSemaphoreInfos;
        vke_render::RenderEnvironment::GetGraphicsQueue()->Submit(1, &initialSignalSubmitInfo, VK_NULL_HANDLE);

        instance->createSceneRenderContext(sceneViewportWidth, sceneViewportHeight);
        instance->gizmoRenderer = std::make_unique<GizmoRenderer>(&instance->sceneRenderContext);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ImGui::StyleColorsDark();

        ImGui_ImplVulkan_InitInfo initInfo{};
        vke_render::GPUCommandQueue *graphicsQueue = vke_render::RenderEnvironment::GetGraphicsQueue();

        initInfo.ApiVersion = VK_API_VERSION_1_3;
        initInfo.Instance = env->vkinstance;
        initInfo.PhysicalDevice = env->physicalDevice;
        initInfo.Device = vke_render::globalLogicalDevice;
        initInfo.QueueFamily = env->queueFamilyIndices.graphicsAndComputeFamily.value();
        initInfo.Queue = graphicsQueue->queue;
        initInfo.DescriptorPoolSize = 512;
        initInfo.MinImageCount = vke_render::MAX_FRAMES_IN_FLIGHT;
        initInfo.ImageCount = static_cast<uint32_t>(ctx->colorImages.size());
        initInfo.UseDynamicRendering = true;
        initInfo.PipelineInfoMain.Subpass = 0;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        VkFormat colorAttachmentFormat = ctx->colorFormat;
        VkPipelineRenderingCreateInfoKHR pipelineRenderingInfo{};
        pipelineRenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
        pipelineRenderingInfo.colorAttachmentCount = 1;
        pipelineRenderingInfo.pColorAttachmentFormats = &colorAttachmentFormat;
        initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = pipelineRenderingInfo;
        initInfo.CheckVkResultFn = CheckImGuiVkResult;

        VKE_FATAL_IF(!ImGui_ImplVulkan_Init(&initInfo), "Failed to initialize ImGui Vulkan backend!")
        VKE_FATAL_IF(!ImGui_ImplGlfw_InitForVulkan(window, true), "Failed to initialize ImGui GLFW backend!")
        instance->registerSceneTextures();
        return instance;
    }

    uint32_t EditorRenderer::AcquireSceneNextImage(uint32_t currentFrame)
    {
        VkFence fence = instance->sceneInFlightFences[currentFrame];
        vkWaitForFences(vke_render::globalLogicalDevice, 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(vke_render::globalLogicalDevice, 1, &fence);

        VkSemaphoreSubmitInfo waitSemaphoreInfo{};
        waitSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waitSemaphoreInfo.semaphore = instance->sceneImageAvailableSemaphores[currentFrame];
        waitSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        waitSemaphoreInfo.deviceIndex = 0;

        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.waitSemaphoreInfoCount = 1;
        submitInfo.pWaitSemaphoreInfos = &waitSemaphoreInfo;
        vke_render::RenderEnvironment::GetGraphicsQueue()->Submit(1, &submitInfo, VK_NULL_HANDLE);
        return currentFrame;
    }

    void EditorRenderer::PresentScene(uint32_t currentFrame, uint32_t imageIndex)
    {
        VkSemaphoreSubmitInfo signalSemaphoreInfo{};
        signalSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signalSemaphoreInfo.semaphore = instance->sceneRenderFinishedSemaphores[currentFrame];
        signalSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signalSemaphoreInfo.deviceIndex = 0;

        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.signalSemaphoreInfoCount = 1;
        submitInfo.pSignalSemaphoreInfos = &signalSemaphoreInfo;
        vke_render::RenderEnvironment::GetGraphicsQueue()->Submit(
            1, &submitInfo, instance->sceneInFlightFences[currentFrame]);
    }

    void EditorRenderer::Dispose()
    {
        vkDeviceWaitIdle(vke_render::globalLogicalDevice);
        instance->removeSceneTextures();
        ImGui_ImplGlfw_Shutdown();
        ImGui_ImplVulkan_Shutdown();
        ImGui::DestroyContext();
        instance->cleanupSceneRenderContext();
        vkFreeCommandBuffers(vke_render::globalLogicalDevice, instance->commandPool,
                             vke_render::MAX_FRAMES_IN_FLIGHT, instance->commandBuffers);
        vkDestroyCommandPool(vke_render::globalLogicalDevice, instance->commandPool, nullptr);
        for (int i = 0; i < vke_render::MAX_FRAMES_IN_FLIGHT; ++i)
        {
            vkDestroySemaphore(vke_render::globalLogicalDevice, instance->sceneImageAvailableSemaphores[i], nullptr);
            vkDestroySemaphore(vke_render::globalLogicalDevice, instance->sceneRenderFinishedSemaphores[i], nullptr);
            vkDestroyFence(vke_render::globalLogicalDevice, instance->sceneInFlightFences[i], nullptr);
        }
        delete instance;
        instance = nullptr;
    }

    void EditorRenderer::registerSceneTextures()
    {
        for (int i = 0; i < vke_render::MAX_FRAMES_IN_FLIGHT; ++i)
        {
            sceneTextureDescriptorSets[i] = ImGui_ImplVulkan_AddTexture(
                sceneColorImageViews[i], sceneRenderContext.outColorLayout);
        }
        sceneTexturesRegistered = true;
    }

    void EditorRenderer::removeSceneTextures()
    {
        if (!sceneTexturesRegistered)
            return;

        for (int i = 0; i < vke_render::MAX_FRAMES_IN_FLIGHT; ++i)
            ImGui_ImplVulkan_RemoveTexture(sceneTextureDescriptorSets[i]);
        sceneTexturesRegistered = false;
    }

    void EditorRenderer::createSceneRenderContext(uint32_t width, uint32_t height)
    {
        vke_render::RenderEnvironment *env = vke_render::RenderEnvironment::GetInstance();
        std::vector<VkImage> colorImages(vke_render::MAX_FRAMES_IN_FLIGHT);
        std::vector<VkImageView> colorImageViews(vke_render::MAX_FRAMES_IN_FLIGHT);

        for (int i = 0; i < vke_render::MAX_FRAMES_IN_FLIGHT; ++i)
        {
            vke_render::VulkanResourceManager::CreateImage(
                width, height, context->colorFormat,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                1, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                &sceneColorImages[i], &sceneColorImageVmaAllocations[i], nullptr);
            sceneColorImageViews[i] = vke_render::VulkanResourceManager::CreateImageView(
                sceneColorImages[i], context->colorFormat, VK_IMAGE_ASPECT_COLOR_BIT);
            VKE_LOG_INFO("createSceneRenderContext {}", (void *)sceneColorImages[i])

            vke_render::VulkanResourceManager::CreateImage(
                width, height, env->depthFormat,
                VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 1,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                &sceneDepthImages[i], &sceneDepthImageVmaAllocations[i], nullptr);
            sceneDepthImageViews[i] = vke_render::VulkanResourceManager::CreateImageView(
                sceneDepthImages[i], env->depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

            colorImages[i] = sceneColorImages[i];
            colorImageViews[i] = sceneColorImageViews[i];
        }

        VkCommandBuffer commandBuffer = commandBuffers[0];
        vkResetCommandBuffer(commandBuffer, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VKE_VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo), "Failed to begin editor scene init command buffer!")
        for (int i = 0; i < vke_render::MAX_FRAMES_IN_FLIGHT; ++i)
        {
            VkImageMemoryBarrier colorBarrier{};
            colorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            colorBarrier.srcAccessMask = VK_ACCESS_NONE;
            colorBarrier.dstAccessMask = VK_ACCESS_NONE;
            colorBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            colorBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            colorBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            colorBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            colorBarrier.image = colorImages[i];
            colorBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            colorBarrier.subresourceRange.baseMipLevel = 0;
            colorBarrier.subresourceRange.levelCount = 1;
            colorBarrier.subresourceRange.baseArrayLayer = 0;
            colorBarrier.subresourceRange.layerCount = 1;

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &colorBarrier);

            VkImageMemoryBarrier depthBarrier{};
            depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            depthBarrier.srcAccessMask = VK_ACCESS_NONE;
            depthBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            depthBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            depthBarrier.image = sceneDepthImages[i];
            depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            depthBarrier.subresourceRange.baseMipLevel = 0;
            depthBarrier.subresourceRange.levelCount = 1;
            depthBarrier.subresourceRange.baseArrayLayer = 0;
            depthBarrier.subresourceRange.layerCount = 1;

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &depthBarrier);
        }
        VKE_VK_CHECK(vkEndCommandBuffer(commandBuffer), "Failed to end editor scene init command buffer!")

        VkCommandBufferSubmitInfo commandBufferInfo{};
        commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        commandBufferInfo.commandBuffer = commandBuffer;

        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &commandBufferInfo;

        vke_render::RenderEnvironment::GetGraphicsQueue()->Submit(1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(vke_render::RenderEnvironment::GetGraphicsQueue()->queue);

        sceneRenderContext = vke_render::RenderContext(
            width, height,
            context->colorFormat, env->depthFormat,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            sceneDepthImages, sceneDepthImageViews,
            colorImages, colorImageViews,
            &sceneResizeEventHub,
            AcquireSceneNextImage, PresentScene);
    }

    void EditorRenderer::cleanupSceneRenderContext()
    {
        vke_render::RenderEnvironment *env = vke_render::RenderEnvironment::GetInstance();
        for (int i = 0; i < vke_render::MAX_FRAMES_IN_FLIGHT; ++i)
        {
            vkDestroyImageView(vke_render::globalLogicalDevice, sceneColorImageViews[i], nullptr);
            vkDestroyImageView(vke_render::globalLogicalDevice, sceneDepthImageViews[i], nullptr);
            vmaDestroyImage(env->vmaAllocator, sceneColorImages[i], sceneColorImageVmaAllocations[i]);
            vmaDestroyImage(env->vmaAllocator, sceneDepthImages[i], sceneDepthImageVmaAllocations[i]);
        }
    }

    void EditorRenderer::ResizeSceneViewport(uint32_t width, uint32_t height)
    {
        if (width == sceneRenderContext.width && height == sceneRenderContext.height)
            return;

        vke_render::RenderEnvironment *env = vke_render::RenderEnvironment::GetInstance();
        vkDeviceWaitIdle(vke_render::globalLogicalDevice);
        ((vke_render::CPUCommandQueue *)env->commandQueues[vke_render::CPU_QUEUE].get())->WaitIdle();
        removeSceneTextures();
        cleanupSceneRenderContext();
        createSceneRenderContext(width, height);
        registerSceneTextures();
        sceneResizeEventHub.DispatchEvent(&sceneRenderContext);
    }

    void EditorRenderer::BeginFrame()
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

    }

    void EditorRenderer::render(const glm::mat4 &viewProj, std::span<const glm::vec3> lines, std::span<const GizmoBatch> batches)
    {
        uint32_t imageIndex = context->AcquireNextImage(currentFrame);

        ImGui::Render();

        VkCommandBuffer commandBuffer = commandBuffers[currentFrame];
        vkResetCommandBuffer(commandBuffer, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        VKE_VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo), "Failed to begin editor command buffer!")

        if (!lines.empty())
        {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.oldLayout = sceneRenderContext.outColorLayout;
            barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = sceneColorImages[currentFrame];
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(commandBuffer,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                                 0, nullptr, 0, nullptr, 1, &barrier);

            gizmoRenderer->Render(commandBuffer, currentFrame,
                                  viewProj, lines, batches);

            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.newLayout = sceneRenderContext.outColorLayout;
            vkCmdPipelineBarrier(commandBuffer,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                 0, nullptr, 0, nullptr, 1, &barrier);
        }

        VkImageMemoryBarrier colorBeginBarrier{};
        colorBeginBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        colorBeginBarrier.srcAccessMask = 0;
        colorBeginBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        colorBeginBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorBeginBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorBeginBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        colorBeginBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        colorBeginBarrier.image = context->colorImages[imageIndex];
        colorBeginBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        colorBeginBarrier.subresourceRange.baseMipLevel = 0;
        colorBeginBarrier.subresourceRange.levelCount = 1;
        colorBeginBarrier.subresourceRange.baseArrayLayer = 0;
        colorBeginBarrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &colorBeginBarrier);

        VkRenderingAttachmentInfo colorAttachmentInfo{};
        colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachmentInfo.imageView = context->colorImageViews[imageIndex];
        colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachmentInfo.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea = {{0, 0}, {context->width, context->height}};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachmentInfo;
        vkCmdBeginRendering(commandBuffer, &renderingInfo);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
        vkCmdEndRendering(commandBuffer);

        VkImageMemoryBarrier colorEndBarrier{};
        colorEndBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        colorEndBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        colorEndBarrier.dstAccessMask = 0;
        colorEndBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorEndBarrier.newLayout = context->outColorLayout;
        colorEndBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        colorEndBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        colorEndBarrier.image = context->colorImages[imageIndex];
        colorEndBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        colorEndBarrier.subresourceRange.baseMipLevel = 0;
        colorEndBarrier.subresourceRange.levelCount = 1;
        colorEndBarrier.subresourceRange.baseArrayLayer = 0;
        colorEndBarrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &colorEndBarrier);

        VKE_VK_CHECK(vkEndCommandBuffer(commandBuffer), "Failed to end editor command buffer!")

        VkCommandBufferSubmitInfo commandBufferInfo{};
        commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        commandBufferInfo.commandBuffer = commandBuffer;

        VkSemaphoreSubmitInfo waitSemaphoreInfo{};
        waitSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waitSemaphoreInfo.semaphore = sceneRenderFinishedSemaphores[currentFrame];
        waitSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        waitSemaphoreInfo.deviceIndex = 0;

        VkSemaphoreSubmitInfo signalSemaphoreInfo{};
        signalSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signalSemaphoreInfo.semaphore = sceneImageAvailableSemaphores[currentFrame];
        signalSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signalSemaphoreInfo.deviceIndex = 0;

        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &commandBufferInfo;
        submitInfo.waitSemaphoreInfoCount = 1;
        submitInfo.pWaitSemaphoreInfos = &waitSemaphoreInfo;
        submitInfo.signalSemaphoreInfoCount = 1;
        submitInfo.pSignalSemaphoreInfos = &signalSemaphoreInfo;

        vke_render::RenderEnvironment::GetGraphicsQueue()->Submit(1, &submitInfo, VK_NULL_HANDLE);

        context->Present(currentFrame, imageIndex);
    }
}
