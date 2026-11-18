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

        Texture2D(vke_common::AssetHandle hdl, VkImage image, VmaAllocation allocation,
                  uint32_t texWidth, uint32_t texHeight, uint32_t mipLevels, VkFormat format,
                  VkFilter minFilter, VkFilter magFilter, VkSamplerAddressMode addressMode, bool anisotropyEnable)
            : handle(hdl), textureImage(image), textureImageAllocation(allocation),
              width(texWidth), height(texHeight), mipLevelCnt(mipLevels)
        {
            textureImageView = VulkanResourceManager::CreateImageView(textureImage, format, VK_IMAGE_ASPECT_COLOR_BIT, mipLevelCnt);
            textureSampler = VulkanResourceManager::GetSampler(minFilter, magFilter, addressMode, anisotropyEnable);
        }

        Texture2D(const vke_common::AssetHandle hdl, const void *pixels, int texWidth, int texHeight,
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

            size_t imageSize = static_cast<size_t>(texWidth) * static_cast<size_t>(texHeight) * VulkanResourceManager::GetTextureBytesPerPixel(format);
            HostCoherentBuffer stagingBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
            stagingBuffer.ToBuffer(0, pixels, imageSize);
            RenderEnvironment *instance = RenderEnvironment::GetInstance();
            VkCommandBuffer commandBuffer = RenderEnvironment::BeginSingleTimeCommands(instance->commandPool);
            VulkanResourceManager::MakeLayoutTransition(commandBuffer, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                                                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                        textureImage, VK_IMAGE_ASPECT_COLOR_BIT, mipLevelCnt,
                                                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            VulkanResourceManager::CopyBufferToImage(commandBuffer, stagingBuffer.buffer, textureImage, width, height);

            if (generateMipMap)
                VulkanResourceManager::GenerateMipmaps2D(commandBuffer, textureImage, texWidth, texHeight, mipLevelCnt);
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
    };
}

#endif
