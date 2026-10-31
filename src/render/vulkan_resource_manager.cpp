#include <render/vulkan_resource_manager.hpp>
#include <render/environment.hpp>

namespace vke_render
{
    VulkanResourceManager *VulkanResourceManager::instance = nullptr;

    VkSampler VulkanResourceManager::GetSampler(const SamplerDesc &desc)
    {
        SamplerDesc key = desc;

        if (!key.anisotropyEnable)
            key.maxAnisotropy = 1.0f;
        if (!key.compareEnable)
            key.compareOp = VK_COMPARE_OP_ALWAYS;
        if (key.addressModeU != VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER &&
            key.addressModeV != VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER &&
            key.addressModeW != VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER)
            key.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

        auto it = instance->samplers.find(key);
        if (it != instance->samplers.end())
            return it->second;

        VkSamplerCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.flags = key.flags;
        info.magFilter = key.magFilter;
        info.minFilter = key.minFilter;
        info.mipmapMode = key.mipmapMode;
        info.addressModeU = key.addressModeU;
        info.addressModeV = key.addressModeV;
        info.addressModeW = key.addressModeW;
        info.mipLodBias = key.mipLodBias;
        info.anisotropyEnable = key.anisotropyEnable;
        info.maxAnisotropy = key.maxAnisotropy;
        info.compareEnable = key.compareEnable;
        info.compareOp = key.compareOp;
        info.minLod = key.minLod;
        info.maxLod = key.maxLod;
        info.borderColor = key.borderColor;
        info.unnormalizedCoordinates = key.unnormalizedCoordinates;

        VkSampler sampler;
        VKE_VK_CHECK(vkCreateSampler(globalLogicalDevice, &info, nullptr, &sampler), "Failed to create sampler!")
        instance->samplers.emplace(key, sampler);
        return sampler;
    }

    VkSampler VulkanResourceManager::GetSampler(VkFilter minFilter, VkFilter magFilter,
                                                VkSamplerAddressMode addressMode,
                                                bool anisotropyEnable, VkSamplerMipmapMode mipmapMode,
                                                float maxLod)
    {
        return GetSampler(minFilter, magFilter, addressMode, addressMode, addressMode,
                          anisotropyEnable, mipmapMode, maxLod);
    }

    VkSampler VulkanResourceManager::GetSampler(VkFilter minFilter, VkFilter magFilter,
                                                VkSamplerAddressMode addressModeU,
                                                VkSamplerAddressMode addressModeV,
                                                VkSamplerAddressMode addressModeW,
                                                bool anisotropyEnable, VkSamplerMipmapMode mipmapMode,
                                                float maxLod)
    {
        SamplerDesc desc;
        desc.minFilter = minFilter;
        desc.magFilter = magFilter;
        desc.addressModeU = addressModeU;
        desc.addressModeV = addressModeV;
        desc.addressModeW = addressModeW;
        desc.anisotropyEnable = anisotropyEnable;
        if (anisotropyEnable)
            desc.maxAnisotropy = RenderEnvironment::GetInstance()->physicalDeviceProperties.limits.maxSamplerAnisotropy;
        desc.mipmapMode = mipmapMode;
        desc.maxLod = maxLod;
        return GetSampler(desc);
    }

    void VulkanResourceManager::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                             VmaAllocationCreateFlags vmaFlags, VmaMemoryUsage vmaUsage,
                                             VkBuffer *buffer, VmaAllocation *vmaAllocation, VmaAllocationInfo *vmaAllocationInfo)
    {
        RenderEnvironment *environment = RenderEnvironment::GetInstance();
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
        bufferInfo.queueFamilyIndexCount = environment->queueFamilyIndices.uniqueQueueFamilies.size();
        bufferInfo.pQueueFamilyIndices = environment->queueFamilyIndices.uniqueQueueFamilies.data();

        VmaAllocationCreateInfo allocationCreateInfo = {};
        allocationCreateInfo.flags = vmaFlags;
        allocationCreateInfo.usage = vmaUsage;

        vmaCreateBuffer(environment->vmaAllocator, &bufferInfo, &allocationCreateInfo,
                        buffer, vmaAllocation, vmaAllocationInfo);
    }

    void VulkanResourceManager::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, VkDeviceSize srcOffset, VkDeviceSize dstOffset)
    {
        RenderEnvironment *environment = RenderEnvironment::GetInstance();
        VkCommandBuffer commandBuffer = RenderEnvironment::BeginSingleTimeCommands(environment->transferCommandPool);

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = srcOffset;
        copyRegion.dstOffset = dstOffset;
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

        RenderEnvironment::EndSingleTimeCommands((GPUCommandQueue *)RenderEnvironment::GetQueueNotNull(TRANSFER_QUEUE), environment->transferCommandPool, commandBuffer);
    }

    void VulkanResourceManager::CreateImage(uint32_t width, uint32_t height, VkFormat format,
                                            VkImageTiling tiling, VkImageUsageFlags usage, uint32_t mipLevelCnt,
                                            VkMemoryPropertyFlags properties, VkImage *image,
                                            VmaAllocation *vmaAllocation, VmaAllocationInfo *vmaAllocationInfo)
    {
        RenderEnvironment *environment = RenderEnvironment::GetInstance();
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = mipLevelCnt;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = tiling;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo alloationCreateInfo = {};
        alloationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        alloationCreateInfo.preferredFlags = properties;
        vmaCreateImage(environment->vmaAllocator, &imageInfo, &alloationCreateInfo, image, vmaAllocation, vmaAllocationInfo);
    }

    VkImageView VulkanResourceManager::CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags,
                                                       uint32_t mipLevelCnt, uint32_t baseArrayLayer,
                                                       uint32_t layerCnt, VkImageViewType viewType)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = viewType;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspectFlags;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mipLevelCnt;
        viewInfo.subresourceRange.baseArrayLayer = baseArrayLayer;
        viewInfo.subresourceRange.layerCount = layerCnt;

        VkImageView imageView;
        VKE_VK_CHECK(vkCreateImageView(globalLogicalDevice, &viewInfo, nullptr, &imageView), "failed to create texture image view!")

        return imageView;
    }

    void VulkanResourceManager::MakeLayoutTransition(VkCommandBuffer commandBuffer,
                                                     VkAccessFlags srcAccessMask,
                                                     VkAccessFlags dstAccessMask,
                                                     VkImageLayout oldLayout,
                                                     VkImageLayout newLayout,
                                                     VkImage image,
                                                     VkImageAspectFlags aspectMask,
                                                     uint32_t mipLevelCnt,
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
        barrier.subresourceRange.aspectMask = aspectMask;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mipLevelCnt;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            commandBuffer,
            sourceStage,
            destinationStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier);
    }

    void VulkanResourceManager::CopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
    {
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;

        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;

        region.imageOffset = {0, 0, 0};
        region.imageExtent = {
            width,
            height,
            1};

        vkCmdCopyBufferToImage(
            commandBuffer,
            buffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &region);
    }

}
