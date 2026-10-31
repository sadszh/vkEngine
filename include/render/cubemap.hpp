#ifndef CUBEMAP_H
#define CUBEMAP_H

#include <render/vulkan_resource_manager.hpp>
#include <render/buffer.hpp>
#include <logger.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace vke_render
{

    class CubeMap
    {
    public:
        vke_common::AssetHandle handle;
        VkImage image;
        VmaAllocation imageAllocation;
        VkImageView imageView;
        VkSampler sampler;
        uint32_t mipLevelCnt;

        CubeMap(const vke_common::AssetHandle hdl, void *const pixels[6], int faceWidth, int faceHeight,
                VkFormat format = VK_FORMAT_R8G8B8A8_SRGB,
                VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VkFilter minFilter = VK_FILTER_LINEAR,
                VkFilter magFilter = VK_FILTER_LINEAR,
                VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                bool anisotropyEnable = VK_TRUE,
                bool generateMipMap = true) : handle(hdl),
                                              mipLevelCnt(generateMipMap ? (static_cast<uint32_t>(std::floor(std::log2(std::max(faceWidth, faceHeight)))) + 1) : 1)
        {
            createCubeImage(faceWidth, faceHeight, format,
                            VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | usage,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            const size_t faceSize = static_cast<size_t>(faceWidth) * static_cast<size_t>(faceHeight) * GetBytesPerPixel(format);
            HostCoherentBuffer stagingBuffer(faceSize * 6, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
            for (uint32_t i = 0; i < 6; ++i)
                stagingBuffer.ToBuffer(faceSize * i, pixels[i], faceSize);

            RenderEnvironment *instance = RenderEnvironment::GetInstance();
            VkCommandBuffer commandBuffer = RenderEnvironment::BeginSingleTimeCommands(instance->commandPool);
            makeLayoutTransition(commandBuffer, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                                 VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            copyBufferToCubeImage(commandBuffer, stagingBuffer.buffer, static_cast<uint32_t>(faceWidth), static_cast<uint32_t>(faceHeight), faceSize);

            if (generateMipMap)
                generateMipmaps(commandBuffer, faceWidth, faceHeight, layout);
            else
                makeLayoutTransition(commandBuffer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, layout,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

            RenderEnvironment::EndSingleTimeCommands(RenderEnvironment::GetGraphicsQueue(), instance->commandPool, commandBuffer);

            imageView = createCubeImageView(format, 0, mipLevelCnt);
            createMipImageViews(format);
            sampler = VulkanResourceManager::GetSampler(minFilter, magFilter, addressMode, anisotropyEnable);
        }

        CubeMap(const vke_common::AssetHandle hdl, const std::array<void *, 6> &pixels, int faceWidth, int faceHeight,
                VkFormat format = VK_FORMAT_R8G8B8A8_SRGB,
                VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VkFilter minFilter = VK_FILTER_LINEAR,
                VkFilter magFilter = VK_FILTER_LINEAR,
                VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                bool anisotropyEnable = VK_TRUE,
                bool generateMipMap = true)
            : CubeMap(hdl, pixels.data(), faceWidth, faceHeight, format, usage, layout,
                      minFilter, magFilter, addressMode, anisotropyEnable, generateMipMap) {}

        CubeMap(int faceWidth, int faceHeight,
                VkFormat format = VK_FORMAT_R8G8B8A8_SRGB,
                VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                VkImageLayout initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VkFilter minFilter = VK_FILTER_LINEAR,
                VkFilter magFilter = VK_FILTER_LINEAR,
                VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                bool anisotropyEnable = VK_TRUE,
                uint32_t mipLevels = 1) : handle(0), mipLevelCnt(mipLevels)
        {
            createCubeImage(faceWidth, faceHeight, format,
                            VK_IMAGE_TILING_OPTIMAL,
                            usage,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            RenderEnvironment *instance = RenderEnvironment::GetInstance();
            VkCommandBuffer commandBuffer = RenderEnvironment::BeginSingleTimeCommands(instance->commandPool);
            makeLayoutTransition(commandBuffer, VK_ACCESS_NONE, VK_ACCESS_NONE,
                                 VK_IMAGE_LAYOUT_UNDEFINED, initialLayout,
                                 VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_NONE);
            RenderEnvironment::EndSingleTimeCommands(RenderEnvironment::GetGraphicsQueue(), instance->commandPool, commandBuffer);

            imageView = createCubeImageView(format, 0, mipLevelCnt);
            createMipImageViews(format);
            sampler = VulkanResourceManager::GetSampler(minFilter, magFilter, addressMode, anisotropyEnable);
        }

        VkImageView GetMipImageView(uint32_t mipLevel)
        {
            VKE_FATAL_IF(mipLevel >= mipImageViews.size(), "Cube map mip image view index out of range!")
            return mipImageViews[mipLevel];
        }

        ~CubeMap()
        {
            for (VkImageView mipImageView : mipImageViews)
                vkDestroyImageView(globalLogicalDevice, mipImageView, nullptr);
            vkDestroyImageView(globalLogicalDevice, imageView, nullptr);
            vmaDestroyImage(RenderEnvironment::GetInstance()->vmaAllocator, image, imageAllocation);
        }

    private:
        std::vector<VkImageView> mipImageViews;

        void createCubeImage(uint32_t width, uint32_t height, VkFormat format,
                             VkImageTiling tiling, VkImageUsageFlags usage,
                             VkMemoryPropertyFlags properties)
        {
            VKE_FATAL_IF(width != height, "Cube map faces must be square!")

            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = width;
            imageInfo.extent.height = height;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = mipLevelCnt;
            imageInfo.arrayLayers = 6;
            imageInfo.format = format;
            imageInfo.tiling = tiling;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = usage;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo allocationCreateInfo{};
            allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            allocationCreateInfo.preferredFlags = properties;
            VKE_VK_CHECK(vmaCreateImage(RenderEnvironment::GetInstance()->vmaAllocator, &imageInfo, &allocationCreateInfo,
                                        &image, &imageAllocation, nullptr),
                         "Failed to create cube map image!")
        }

        VkImageView createCubeImageView(VkFormat format, uint32_t baseMipLevel, uint32_t levelCount)
        {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
            viewInfo.format = format;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = baseMipLevel;
            viewInfo.subresourceRange.levelCount = levelCount;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 6;

            VkImageView ret;
            VKE_VK_CHECK(vkCreateImageView(globalLogicalDevice, &viewInfo, nullptr, &ret), "Failed to create cube map image view!")
            return ret;
        }

        void createMipImageViews(VkFormat format)
        {
            mipImageViews.resize(mipLevelCnt);
            for (uint32_t i = 0; i < mipLevelCnt; ++i)
                mipImageViews[i] = createCubeImageView(format, i, 1);
        }

        void makeLayoutTransition(VkCommandBuffer commandBuffer,
                                  VkAccessFlags srcAccessMask,
                                  VkAccessFlags dstAccessMask,
                                  VkImageLayout oldLayout,
                                  VkImageLayout newLayout,
                                  VkPipelineStageFlags sourceStage,
                                  VkPipelineStageFlags destinationStage)
        {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = srcAccessMask;
            barrier.dstAccessMask = dstAccessMask;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = mipLevelCnt;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 6;

            vkCmdPipelineBarrier(commandBuffer,
                                 sourceStage, destinationStage,
                                 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &barrier);
        }

        void copyBufferToCubeImage(VkCommandBuffer commandBuffer, VkBuffer buffer, uint32_t width, uint32_t height, size_t faceSize)
        {
            std::array<VkBufferImageCopy, 6> regions{};
            for (uint32_t i = 0; i < 6; ++i)
            {
                regions[i].bufferOffset = faceSize * i;
                regions[i].bufferRowLength = 0;
                regions[i].bufferImageHeight = 0;
                regions[i].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                regions[i].imageSubresource.mipLevel = 0;
                regions[i].imageSubresource.baseArrayLayer = i;
                regions[i].imageSubresource.layerCount = 1;
                regions[i].imageOffset = {0, 0, 0};
                regions[i].imageExtent = {width, height, 1};
            }

            vkCmdCopyBufferToImage(commandBuffer, buffer, image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   static_cast<uint32_t>(regions.size()), regions.data());
        }

        void generateMipmaps(VkCommandBuffer commandBuffer, int32_t faceWidth, int32_t faceHeight, VkImageLayout finalLayout)
        {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.image = image;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 6;
            barrier.subresourceRange.levelCount = 1;

            int32_t mipWidth = faceWidth;
            int32_t mipHeight = faceHeight;

            for (uint32_t i = 1; i < mipLevelCnt; i++)
            {
                barrier.subresourceRange.baseMipLevel = i - 1;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

                vkCmdPipelineBarrier(commandBuffer,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     0,
                                     0, nullptr,
                                     0, nullptr,
                                     1, &barrier);

                VkImageBlit blit{};
                blit.srcOffsets[0] = {0, 0, 0};
                blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
                blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.srcSubresource.mipLevel = i - 1;
                blit.srcSubresource.baseArrayLayer = 0;
                blit.srcSubresource.layerCount = 6;

                blit.dstOffsets[0] = {0, 0, 0};
                blit.dstOffsets[1] = {
                    mipWidth > 1 ? mipWidth / 2 : 1,
                    mipHeight > 1 ? mipHeight / 2 : 1,
                    1};
                blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.dstSubresource.mipLevel = i;
                blit.dstSubresource.baseArrayLayer = 0;
                blit.dstSubresource.layerCount = 6;

                vkCmdBlitImage(commandBuffer,
                               image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1, &blit,
                               VK_FILTER_LINEAR);

                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.newLayout = finalLayout;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                vkCmdPipelineBarrier(commandBuffer,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                     0,
                                     0, nullptr,
                                     0, nullptr,
                                     1, &barrier);

                mipWidth = std::max(1, mipWidth / 2);
                mipHeight = std::max(1, mipHeight / 2);
            }

            barrier.subresourceRange.baseMipLevel = mipLevelCnt - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = finalLayout;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(commandBuffer,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &barrier);
        }

        static size_t GetBytesPerPixel(VkFormat format)
        {
            switch (format)
            {
            case VK_FORMAT_R16G16B16A16_UNORM:
            case VK_FORMAT_R16G16B16A16_SFLOAT:
                return 8;
            default:
                return 4;
            }
        }
    };
}

#endif
