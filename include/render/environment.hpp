#ifndef ENV_H
#define ENV_H

#include <render/queue.hpp>
#include <event.hpp>
#include <vma/vk_mem_alloc.h>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <memory>
#include <vector>

namespace vke_render
{
    const int MAX_FRAMES_IN_FLIGHT = 2;

    struct SwapChainSupportDetails
    {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    struct RenderContext
    {
        uint32_t width;
        uint32_t height;
        VkFormat colorFormat;
        VkFormat depthFormat;
        VkImageLayout outColorLayout;
        VkImage depthImages[MAX_FRAMES_IN_FLIGHT];
        VkImageView depthImageViews[MAX_FRAMES_IN_FLIGHT];
        std::vector<VkImage> colorImages;
        std::vector<VkImageView> colorImageViews;
        vke_common::EventHub<RenderContext> *resizeEventHub;
        std::function<uint32_t(uint32_t)> AcquireNextImage;
        std::function<void(uint32_t, uint32_t)> Present;

        RenderContext() {}

        RenderContext(uint32_t w, uint32_t h, VkFormat colorFormat,
                      VkFormat depthFormat, VkImageLayout outColorLayout,
                      VkImage *depthImgs, VkImageView *depthImgViews,
                      const std::vector<VkImage> &colorImgs, const std::vector<VkImageView> &colorImgViews,
                      vke_common::EventHub<RenderContext> *resizeEventHub,
                      std::function<uint32_t(uint32_t)> AcquireNextImage,
                      std::function<void(uint32_t, uint32_t)> Present)
            : width(w), height(h), colorFormat(colorFormat), depthFormat(depthFormat), outColorLayout(outColorLayout),
              colorImages(colorImgs), colorImageViews(colorImgViews),
              resizeEventHub(resizeEventHub), AcquireNextImage(AcquireNextImage), Present(Present)
        {
            for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
            {
                depthImages[i] = depthImgs[i];
                depthImageViews[i] = depthImgViews[i];
            }
        }
    };

    class RenderEnvironment
    {
    private:
        static RenderEnvironment *instance;
        RenderEnvironment(bool enableValidationLayers)
            : windowResized(false), enableValidationLayers(enableValidationLayers) {}
        ~RenderEnvironment() {}
        RenderEnvironment(const RenderEnvironment &);
        RenderEnvironment &operator=(const RenderEnvironment);

        class Deletor
        {
        public:
            ~Deletor()
            {
                if (RenderEnvironment::instance != nullptr)
                    delete RenderEnvironment::instance;
            }
        };
        static Deletor deletor;

    public:
        static RenderEnvironment *GetInstance()
        {
            VKE_FATAL_IF(instance == nullptr, "RenderEnvironment not initialized!")
            return instance;
        }

        static RenderEnvironment *Init(GLFWwindow *window, bool enableValidationLayers)
        {
            instance = new RenderEnvironment(enableValidationLayers);
            instance->window = window;
            instance->createInstance();
            instance->createSurface();
            instance->pickPhysicalDevice();
            instance->createLogicalDevice();
            instance->createVulkanMemoryAllocator();
            instance->createCommandPool();
            instance->createSwapChain();
            instance->createSyncObjects();
            instance->createImageViews();
            instance->createCPUCommandQueue();

            vke_common::EventSystem::AddEventListener(vke_common::EVENT_WINDOW_RESIZE, instance, vke_common::EventCallback(OnWindowResize));
            instance->rootRenderContext = RenderContext(
                instance->swapChainExtent.width, instance->swapChainExtent.height,
                instance->swapChainImageFormat, instance->depthFormat,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                instance->depthImages, instance->depthImageViews,
                instance->swapChainImages, instance->swapChainImageViews,
                &(instance->resizeEventHub), AcquireNextImage, Present);
            return instance;
        }

        static void Dispose()
        {
            ((CPUCommandQueue *)(instance->commandQueues[CPU_QUEUE].get()))->Stop();
            instance->cleanupSwapChain();

            for (VkSemaphore semaphore : instance->renderFinishedSemaphores)
                vkDestroySemaphore(globalLogicalDevice, semaphore, nullptr);

            for (VkSemaphore semaphore : instance->imageAvailableSemaphores)
                vkDestroySemaphore(globalLogicalDevice, semaphore, nullptr);

            for (VkFence fence : instance->inFlightFences)
                vkDestroyFence(globalLogicalDevice, fence, nullptr);

            if (instance->computeCommandPool != instance->commandPool)
                vkDestroyCommandPool(globalLogicalDevice, instance->computeCommandPool, nullptr);
            if (instance->transferCommandPool != instance->commandPool)
                vkDestroyCommandPool(globalLogicalDevice, instance->transferCommandPool, nullptr);
            vkDestroyCommandPool(globalLogicalDevice, instance->commandPool, nullptr);
            vmaDestroyAllocator(instance->vmaAllocator);
            vkDestroyDevice(globalLogicalDevice, nullptr);
            vkDestroySurfaceKHR(instance->vkinstance, instance->surface, nullptr);
            vkDestroyInstance(instance->vkinstance, nullptr);
            glfwTerminate();
        }

        static uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
        {
            VkPhysicalDeviceMemoryProperties memProperties;
            vkGetPhysicalDeviceMemoryProperties(instance->physicalDevice, &memProperties);
            for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
                if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
                    return i;

            VKE_FATAL("Failed to find suitable memory type!")
        }

        static bool HasQueue(QueueType type)
        {
            return instance->commandQueues[type] != nullptr;
        }

        static GPUCommandQueue *GetGraphicsQueue()
        {
            return (GPUCommandQueue *)(instance->commandQueues[GRAPHICS_QUEUE].get());
        }

        static CommandQueue *GetQueue(QueueType type)
        {
            return instance->commandQueues[type].get();
        }

        static CommandQueue *GetQueueNotNull(QueueType type)
        {
            CommandQueue *ret = instance->commandQueues[type].get();
            return ret == nullptr ? instance->commandQueues[GRAPHICS_QUEUE].get() : ret;
        }

        static VkCommandBuffer BeginSingleTimeCommands(VkCommandPool commandPool)
        {
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandPool = commandPool;
            allocInfo.commandBufferCount = 1;

            VkCommandBuffer commandBuffer;
            vkAllocateCommandBuffers(globalLogicalDevice, &allocInfo, &commandBuffer);

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            vkBeginCommandBuffer(commandBuffer, &beginInfo);

            return commandBuffer;
        }

        static void EndSingleTimeCommands(GPUCommandQueue *queue, VkCommandPool commandPool, VkCommandBuffer commandBuffer)
        {
            vkEndCommandBuffer(commandBuffer);

            VkCommandBufferSubmitInfo commandBufferInfo{};
            commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            commandBufferInfo.commandBuffer = commandBuffer;

            VkSubmitInfo2 submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submitInfo.commandBufferInfoCount = 1;
            submitInfo.pCommandBufferInfos = &commandBufferInfo;

            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

            VkFence submitFence = VK_NULL_HANDLE;
            vkCreateFence(globalLogicalDevice, &fenceInfo, nullptr, &submitFence);

            queue->Submit(1, &submitInfo, submitFence);
            vkWaitForFences(globalLogicalDevice, 1, &submitFence, VK_TRUE, UINT64_MAX);

            vkDestroyFence(globalLogicalDevice, submitFence, nullptr);

            vkFreeCommandBuffers(globalLogicalDevice, commandPool, 1, &commandBuffer);
        }

        static SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice pdevice)
        {
            SwapChainSupportDetails details;
            VkSurfaceKHR surface = instance->surface;
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pdevice, surface, &details.capabilities);

            uint32_t formatCount;
            vkGetPhysicalDeviceSurfaceFormatsKHR(pdevice, surface, &formatCount, nullptr);

            if (formatCount != 0)
            {
                details.formats.resize(formatCount);
                vkGetPhysicalDeviceSurfaceFormatsKHR(pdevice, surface, &formatCount, details.formats.data());
            }

            uint32_t presentModeCount;
            vkGetPhysicalDeviceSurfacePresentModesKHR(pdevice, surface, &presentModeCount, nullptr);

            if (presentModeCount != 0)
            {
                details.presentModes.resize(presentModeCount);
                vkGetPhysicalDeviceSurfacePresentModesKHR(pdevice, surface, &presentModeCount, details.presentModes.data());
            }

            return details;
        }

        static void OnWindowResize(void *listener, void *info)
        {
            instance->windowResized = true;
        }

        static uint32_t AcquireNextImage(uint32_t currentFrame)
        {
            vkWaitForFences(globalLogicalDevice, 1, &instance->inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
            uint32_t imageIndex;
            while (true)
            {
                VkResult result = vkAcquireNextImageKHR(globalLogicalDevice,
                                                        instance->swapChain,
                                                        UINT64_MAX,
                                                        instance->imageAvailableSemaphores[currentFrame],
                                                        VK_NULL_HANDLE,
                                                        &imageIndex);

                if (result == VK_ERROR_OUT_OF_DATE_KHR)
                {
                    int width = 0, height = 0;
                    glfwGetFramebufferSize(instance->window, &width, &height);
                    while (width == 0 || height == 0)
                    {
                        glfwWaitEvents();
                        glfwGetFramebufferSize(instance->window, &width, &height);
                    }
                    instance->recreateSwapChain();
                    instance->windowResized = false;
                    continue;
                }

                VKE_FATAL_IF(result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR, "failed to acquire swap chain image!")
                break;
            }

            vkResetFences(globalLogicalDevice, 1, &instance->inFlightFences[currentFrame]);

            VkPipelineStageFlags2 waitDstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

            VkSemaphoreSubmitInfo waitSemaphoreInfo{};
            waitSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            waitSemaphoreInfo.semaphore = instance->imageAvailableSemaphores[currentFrame];
            waitSemaphoreInfo.stageMask = waitDstStageMask;

            VkSubmitInfo2 submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submitInfo.waitSemaphoreInfoCount = 1;
            submitInfo.pWaitSemaphoreInfos = &waitSemaphoreInfo;

            GetGraphicsQueue()->Submit(1, &submitInfo, nullptr);

            return imageIndex;
        }

        static void Present(uint32_t currentFrame, uint32_t imageIndex)
        {
            VkSemaphoreSubmitInfo signalSemaphoreInfo{};
            signalSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signalSemaphoreInfo.semaphore = instance->renderFinishedSemaphores[imageIndex];
            signalSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

            VkSubmitInfo2 submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submitInfo.signalSemaphoreInfoCount = 1;
            submitInfo.pSignalSemaphoreInfos = &signalSemaphoreInfo;

            GetGraphicsQueue()->Submit(1, &submitInfo, instance->inFlightFences[currentFrame]);

            VkPresentInfoKHR presentInfo{};
            presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

            presentInfo.waitSemaphoreCount = 1;
            presentInfo.pWaitSemaphores = &(instance->renderFinishedSemaphores[imageIndex]);

            VkSwapchainKHR swapChains[] = {instance->swapChain};
            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = swapChains;
            presentInfo.pImageIndices = &imageIndex;
            presentInfo.pResults = nullptr; // Optional
            VkResult result = vkQueuePresentKHR(instance->presentQueue, &presentInfo);
            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || instance->windowResized)
            {
                int width = 0, height = 0;
                glfwGetFramebufferSize(instance->window, &width, &height);
                while (width == 0 || height == 0)
                {
                    glfwWaitEvents();
                    glfwGetFramebufferSize(instance->window, &width, &height);
                }
                instance->recreateSwapChain();
                instance->windowResized = false;
            }
            else
                VKE_VK_CHECK(result, "failed to present swap chain image!")
        }

        uint32_t imageCnt;
        vke_common::EventHub<RenderContext> resizeEventHub;
        GLFWwindow *window;
        VkInstance vkinstance;
        VkSurfaceKHR surface;
        QueueFamilyIndices queueFamilyIndices;
        VkPhysicalDevice physicalDevice;
        VkPhysicalDeviceProperties physicalDeviceProperties;
        std::unique_ptr<CommandQueue> commandQueues[4];
        VkQueue presentQueue;
        VkFormat swapChainImageFormat;
        VkExtent2D swapChainExtent;
        VkFormat depthFormat;
        VkCommandPool commandPool;
        VkCommandPool computeCommandPool;
        VkCommandPool transferCommandPool;
        VmaAllocator vmaAllocator;
        std::vector<VkSemaphore> imageAvailableSemaphores;
        std::vector<VkSemaphore> renderFinishedSemaphores;
        std::vector<VkFence> inFlightFences;
        VkSwapchainKHR swapChain;
        std::vector<VkImage> swapChainImages;
        std::vector<VkImageView> swapChainImageViews;
        VkImage depthImages[MAX_FRAMES_IN_FLIGHT];
        VmaAllocation depthImageVmaAllocations[MAX_FRAMES_IN_FLIGHT];
        VkImageView depthImageViews[MAX_FRAMES_IN_FLIGHT];
        RenderContext rootRenderContext;

    private:
        bool windowResized;
        bool enableValidationLayers;

        void createInstance();
        void createSurface();
        void setQueueFamilies(VkPhysicalDevice pdevice);
        bool isDeviceSuitable(VkPhysicalDevice pdevice);
        void pickPhysicalDevice();
        void createLogicalDevice();
        void createVulkanMemoryAllocator();
        void createCommandPool();
        void createSyncObjects();
        void createSwapChain();
        void cleanupSwapChain();
        void recreateSwapChain();
        void createImageViews();
        void createCPUCommandQueue();
    };
}

#endif
