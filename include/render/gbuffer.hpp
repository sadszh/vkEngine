#ifndef GBUFFER_H
#define GBUFFER_H

#include <render/vulkan_resource_manager.hpp>
#include <render/environment.hpp>
#include <render/frame_graph.hpp>

namespace vke_render
{
    const uint32_t GBUFFER_CNT = 4;
    const VkFormat gbufferFormats[GBUFFER_CNT] = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_A2R10G10B10_UNORM_PACK32,
                                                  VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R32_SFLOAT};

    class GBuffer
    {
    private:
        static GBuffer *instance;
        GBuffer() {}
        GBuffer(uint32_t w, uint32_t h) : width(w), height(h) {}
        ~GBuffer() {}
        GBuffer(const GBuffer &);
        GBuffer &operator=(const GBuffer);

    public:
        static GBuffer *GetInstance()
        {
            return instance;
        }

        static GBuffer *Init(uint32_t w, uint32_t h)
        {
            if (instance != nullptr)
                return instance;
            instance = new GBuffer(w, h);
            instance->init();
            return instance;
        }

        static void Dispose()
        {
            instance->dispose();
            delete instance;
            instance = nullptr;
        }

        void RegisterFrameGraphResources(FrameGraph &frameGraph)
        {
            for (int i = 0; i < GBUFFER_CNT; ++i)
            {
                resourceIDs[i] = frameGraph.AddTransientImageResource("gbuffer" + std::to_string(i), CreateImageInfo(i), VK_IMAGE_ASPECT_COLOR_BIT);
                resourceNodeIDs[i] = frameGraph.AllocResourceNode("oriGBuffer" + std::to_string(i), resourceIDs[i]);
            }
        }

        vke_ds::id32_t GetResourceID(uint32_t index) const
        {
            return resourceIDs[index];
        }

        vke_ds::id32_t GetResourceNodeID(uint32_t index) const
        {
            return resourceNodeIDs[index];
        }

        void CreateImageViews(FrameGraph &frameGraph, uint32_t currentFrame)
        {
            for (int i = 0; i < GBUFFER_CNT; ++i)
            {
                if (imageViews[i][currentFrame] != VK_NULL_HANDLE)
                    vkDestroyImageView(globalLogicalDevice, imageViews[i][currentFrame], nullptr);
                VkImage image = frameGraph.GetImageResource(resourceIDs[i]).images[currentFrame];
                imageViews[i][currentFrame] = RenderEnvironment::CreateImageView(image, gbufferFormats[i], VK_IMAGE_ASPECT_COLOR_BIT);
            }
        }

        static void Recreate(uint32_t w, uint32_t h)
        {
            instance->width = w;
            instance->height = h;
            instance->cleanupImageViews();
        }

        VkImageCreateInfo CreateImageInfo(uint32_t index) const
        {
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent = {width, height, 1};
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = gbufferFormats[index];
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT |
                              VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            return imageInfo;
        }

        uint32_t width;
        uint32_t height;
        VkImageView imageViews[GBUFFER_CNT][MAX_FRAMES_IN_FLIGHT];
        VkSampler sampler;
        vke_ds::id32_t resourceIDs[GBUFFER_CNT];
        vke_ds::id32_t resourceNodeIDs[GBUFFER_CNT];

    private:
        void dispose()
        {
            cleanupImageViews();
        }

        void init()
        {
            for (int i = 0; i < GBUFFER_CNT; ++i)
            {
                resourceIDs[i] = 0;
                resourceNodeIDs[i] = 0;
                for (int j = 0; j < MAX_FRAMES_IN_FLIGHT; ++j)
                    imageViews[i][j] = VK_NULL_HANDLE;
            }

            sampler = VulkanResourceManager::GetSampler(
                VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                false, VK_SAMPLER_MIPMAP_MODE_NEAREST, 0.0f);
        }

        void cleanupImageViews()
        {
            for (int i = 0; i < GBUFFER_CNT; ++i)
                for (int j = 0; j < MAX_FRAMES_IN_FLIGHT; ++j)
                    if (imageViews[i][j] != VK_NULL_HANDLE)
                    {
                        vkDestroyImageView(globalLogicalDevice, imageViews[i][j], nullptr);
                        imageViews[i][j] = VK_NULL_HANDLE;
                    }
        }
    };
}

#endif
