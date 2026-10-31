#ifndef TEXTURE_H
#define TEXTURE_H

#include <render/vulkan_resource_manager.hpp>
#include <render/buffer.hpp>
#include <logger.hpp>

namespace vke_render
{
    class Texture2D
    {
    public:
        vke_common::AssetHandle handle;
        VkImage textureImage;
        VmaAllocation textureImageAllocation;
        VkImageView textureImageView;
        VkSampler textureSampler;
        uint32_t width;
        uint32_t height;
        uint32_t mipLevelCnt;

        Texture2D(const vke_common::AssetHandle hdl, void *pixels, int texWidth, int texHeight,
                  VkFormat format = VK_FORMAT_R8G8B8A8_SRGB,
                  VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                  VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VkFilter minFilter = VK_FILTER_LINEAR,
                  VkFilter magFilter = VK_FILTER_LINEAR,
                  VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                  bool anisotropyEnable = VK_TRUE,
                  bool generateMipMap = true) : handle(hdl),
                                                width(static_cast<uint32_t>(texWidth)),
                                                height(static_cast<uint32_t>(texHeight)),
                                                mipLevelCnt(generateMipMap ? (static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1) : 1)
        {
            VulkanResourceManager::CreateImage(
                texWidth, texHeight, format,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | usage,
                mipLevelCnt,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &textureImage,
                &textureImageAllocation, nullptr);

            size_t imageSize = static_cast<size_t>(texWidth) * static_cast<size_t>(texHeight) * getBytesPerPixel(format);
            HostCoherentBuffer stagingBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
            stagingBuffer.ToBuffer(0, pixels, imageSize);

            RenderEnvironment *instance = RenderEnvironment::GetInstance();
            VkCommandBuffer commandBuffer = RenderEnvironment::BeginSingleTimeCommands(instance->commandPool);
            VulkanResourceManager::MakeLayoutTransition(commandBuffer, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                                                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                    textureImage, VK_IMAGE_ASPECT_COLOR_BIT, mipLevelCnt,
                                                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            VulkanResourceManager::CopyBufferToImage(commandBuffer, stagingBuffer.buffer, textureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));

            if (generateMipMap)
                generateMipmaps(commandBuffer, texWidth, texHeight);
            else
                VulkanResourceManager::MakeLayoutTransition(commandBuffer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                        textureImage, VK_IMAGE_ASPECT_COLOR_BIT, mipLevelCnt,
                                                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

            RenderEnvironment::EndSingleTimeCommands(RenderEnvironment::GetGraphicsQueue(), instance->commandPool, commandBuffer);

            textureImageView = VulkanResourceManager::CreateImageView(textureImage, format, VK_IMAGE_ASPECT_COLOR_BIT, mipLevelCnt);
            textureSampler = VulkanResourceManager::GetSampler(minFilter, magFilter, addressMode, anisotropyEnable);
        }

        Texture2D(int texWidth, int texHeight,
                  VkFormat format = VK_FORMAT_R8G8B8A8_SRGB,
                  VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                  VkImageLayout initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VkFilter minFilter = VK_FILTER_LINEAR,
                  VkFilter magFilter = VK_FILTER_LINEAR,
                  VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                  bool anisotropyEnable = VK_TRUE) : handle(0), width(static_cast<uint32_t>(texWidth)), height(static_cast<uint32_t>(texHeight)), mipLevelCnt(1)
        {
            VulkanResourceManager::CreateImage(
                texWidth, texHeight, format,
                VK_IMAGE_TILING_OPTIMAL,
                usage,
                mipLevelCnt,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &textureImage,
                &textureImageAllocation, nullptr);
            RenderEnvironment *instance = RenderEnvironment::GetInstance();
            VkCommandBuffer commandBuffer = RenderEnvironment::BeginSingleTimeCommands(instance->commandPool);
            VulkanResourceManager::MakeLayoutTransition(commandBuffer, VK_ACCESS_NONE, VK_ACCESS_NONE,
                                                    VK_IMAGE_LAYOUT_UNDEFINED, initialLayout,
                                                    textureImage, VK_IMAGE_ASPECT_COLOR_BIT, mipLevelCnt,
                                                    VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_NONE);
            RenderEnvironment::EndSingleTimeCommands(RenderEnvironment::GetGraphicsQueue(), instance->commandPool, commandBuffer);
            textureImageView = VulkanResourceManager::CreateImageView(textureImage, format, VK_IMAGE_ASPECT_COLOR_BIT);
            textureSampler = VulkanResourceManager::GetSampler(minFilter, magFilter, addressMode, anisotropyEnable);
        }

        Texture2D(int texWidth, int texHeight,
                  VkFormat format,
                  VkImageUsageFlags usage,
                  VkImageLayout initialLayout,
                  VkFilter minFilter,
                  VkFilter magFilter,
                  VkSamplerAddressMode addressModeU,
                  VkSamplerAddressMode addressModeV,
                  VkSamplerAddressMode addressModeW,
                  bool anisotropyEnable = VK_TRUE) : handle(0), width(static_cast<uint32_t>(texWidth)), height(static_cast<uint32_t>(texHeight)), mipLevelCnt(1)
        {
            VulkanResourceManager::CreateImage(
                texWidth, texHeight, format,
                VK_IMAGE_TILING_OPTIMAL,
                usage,
                mipLevelCnt,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &textureImage,
                &textureImageAllocation, nullptr);
            RenderEnvironment *instance = RenderEnvironment::GetInstance();
            VkCommandBuffer commandBuffer = RenderEnvironment::BeginSingleTimeCommands(instance->commandPool);
            VulkanResourceManager::MakeLayoutTransition(commandBuffer, VK_ACCESS_NONE, VK_ACCESS_NONE,
                                                    VK_IMAGE_LAYOUT_UNDEFINED, initialLayout,
                                                    textureImage, VK_IMAGE_ASPECT_COLOR_BIT, mipLevelCnt,
                                                    VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_NONE);
            RenderEnvironment::EndSingleTimeCommands(RenderEnvironment::GetGraphicsQueue(), instance->commandPool, commandBuffer);
            textureImageView = VulkanResourceManager::CreateImageView(textureImage, format, VK_IMAGE_ASPECT_COLOR_BIT);
            textureSampler = VulkanResourceManager::GetSampler(minFilter, magFilter, addressModeU, addressModeV, addressModeW, anisotropyEnable);
        }

        ~Texture2D()
        {
            vkDestroyImageView(globalLogicalDevice, textureImageView, nullptr);
            vmaDestroyImage(RenderEnvironment::GetInstance()->vmaAllocator, textureImage, textureImageAllocation);
        }

    private:
        void generateMipmaps(VkCommandBuffer commandBuffer, int32_t texWidth, int32_t texHeight)
        {
            RenderEnvironment *inst = RenderEnvironment::GetInstance();

            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.image = textureImage;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.subresourceRange.levelCount = 1;

            int32_t mipWidth = texWidth;
            int32_t mipHeight = texHeight;

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
                blit.srcSubresource.layerCount = 1;

                blit.dstOffsets[0] = {0, 0, 0};
                blit.dstOffsets[1] = {
                    mipWidth > 1 ? mipWidth / 2 : 1,
                    mipHeight > 1 ? mipHeight / 2 : 1,
                    1};
                blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.dstSubresource.mipLevel = i;
                blit.dstSubresource.baseArrayLayer = 0;
                blit.dstSubresource.layerCount = 1;

                vkCmdBlitImage(commandBuffer,
                               textureImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1, &blit,
                               VK_FILTER_LINEAR);

                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(commandBuffer,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &barrier);
        }

        static size_t getBytesPerPixel(VkFormat format)
        {
            switch (format)
            {
            case VK_FORMAT_R8_UNORM:
            case VK_FORMAT_R8_SNORM:
            case VK_FORMAT_R8_UINT:
            case VK_FORMAT_R8_SINT:
                return 1;
            case VK_FORMAT_R16_UNORM:
            case VK_FORMAT_R16_SNORM:
            case VK_FORMAT_R16_UINT:
            case VK_FORMAT_R16_SINT:
            case VK_FORMAT_R16_SFLOAT:
                return 2;
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
