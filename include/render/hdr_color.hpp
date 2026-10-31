#ifndef HDR_COLOR_H
#define HDR_COLOR_H

#include <render/vulkan_resource_manager.hpp>
#include <render/frame_graph.hpp>

namespace vke_render
{
    class HDRColorManager
    {
    public:
        static constexpr VkFormat HDR_COLOR_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
        VkSampler sampler;

        HDRColorManager(RenderContext *ctx)
            : context(ctx), sampler(VK_NULL_HANDLE), latestImageIndex(0)
        {
            for (uint32_t imageIndex = 0; imageIndex < IMAGE_COUNT; ++imageIndex)
            {
                resourceIDs[imageIndex] = 0;
                resourceNodeIDs[imageIndex] = 0;
                for (int frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
                    imageViews[imageIndex][frame] = VK_NULL_HANDLE;
            }
            sampler = VulkanResourceManager::GetSampler(
                VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                false, VK_SAMPLER_MIPMAP_MODE_NEAREST, 0.0f);
            imageCreateInfo = VkImageCreateInfo{};
            imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
            imageCreateInfo.extent = {context->width, context->height, 1};
            imageCreateInfo.mipLevels = 1;
            imageCreateInfo.arrayLayers = 1;
            imageCreateInfo.format = HDR_COLOR_FORMAT;
            imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                    VK_IMAGE_USAGE_SAMPLED_BIT |
                                    VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
            imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        ~HDRColorManager()
        {
            cleanupImageViews();
        }

        HDRColorManager(const HDRColorManager &) = delete;
        HDRColorManager &operator=(const HDRColorManager &) = delete;

        void ConstructFrameGraph(FrameGraph &frameGraph,
                                 std::map<std::string, vke_ds::id32_t> &blackboard,
                                 ResourceNodeIDMap &currentResourceNodeID)
        {
            latestImageIndex = 0;
            for (uint32_t imageIndex = 0; imageIndex < IMAGE_COUNT; ++imageIndex)
            {
                resourceNodeIDs[imageIndex] = 0;
                resourceIDs[imageIndex] = frameGraph.AddTransientImageResource(
                    imageIndex == 0 ? "hdrColor0" : "hdrColor1", imageCreateInfo, VK_IMAGE_ASPECT_COLOR_BIT);
            }
            frameGraph.AddTransientReadyCallback([this](FrameGraph &frameGraph, uint32_t currentFrame)
                                                 { createImageViews(frameGraph, currentFrame); });
        }

        void OnWindowResize(FrameGraph &frameGraph, RenderContext *ctx)
        {
            context = ctx;
            cleanupImageViews();
            imageCreateInfo.extent = {context->width, context->height, 1};

            for (uint32_t imageIndex = 0; imageIndex < IMAGE_COUNT; ++imageIndex)
                frameGraph.SetTransientImageCreateInfo(resourceIDs[imageIndex], imageCreateInfo);
        }

        static constexpr uint32_t IMAGE_COUNT = 2;
        uint32_t GetLatestImageIndex() const { return latestImageIndex; }
        uint32_t GetAlternateImageIndex() const { return 1u - latestImageIndex; }
        vke_ds::id32_t GetResourceID(uint32_t imageIndex) const { return resourceIDs[imageIndex]; }
        vke_ds::id32_t GetResourceNodeID(uint32_t imageIndex) const { return resourceNodeIDs[imageIndex]; }
        VkImageView GetImageView(uint32_t imageIndex, uint32_t currentFrame) const { return imageViews[imageIndex][currentFrame]; }
        VkImageView GetLatestImageView(uint32_t currentFrame) const { return GetImageView(latestImageIndex, currentFrame); }

        void UpdateResourceNode(uint32_t imageIndex, vke_ds::id32_t resourceNodeID)
        {
            resourceNodeIDs[imageIndex] = resourceNodeID;
            latestImageIndex = imageIndex;
        }

    private:
        RenderContext *context;
        VkImageCreateInfo imageCreateInfo;
        VkImageView imageViews[IMAGE_COUNT][MAX_FRAMES_IN_FLIGHT];
        vke_ds::id32_t resourceIDs[IMAGE_COUNT];
        vke_ds::id32_t resourceNodeIDs[IMAGE_COUNT];
        uint32_t latestImageIndex;

        void createImageViews(FrameGraph &frameGraph, uint32_t currentFrame)
        {
            for (uint32_t imageIndex = 0; imageIndex < IMAGE_COUNT; ++imageIndex)
            {
                if (imageViews[imageIndex][currentFrame] != VK_NULL_HANDLE)
                    vkDestroyImageView(globalLogicalDevice, imageViews[imageIndex][currentFrame], nullptr);
                VkImage image = frameGraph.GetImageResource(resourceIDs[imageIndex]).images[currentFrame];
                imageViews[imageIndex][currentFrame] = RenderEnvironment::CreateImageView(image, HDR_COLOR_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT);
            }
        }

        void cleanupImageViews()
        {
            for (auto &imageViewSet : imageViews)
                for (VkImageView &imageView : imageViewSet)
                    if (imageView != VK_NULL_HANDLE)
                    {
                        vkDestroyImageView(globalLogicalDevice, imageView, nullptr);
                        imageView = VK_NULL_HANDLE;
                    }
        }
    };
}

#endif
