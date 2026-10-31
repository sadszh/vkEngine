#ifndef BUFFER_H
#define BUFFER_H

#include <render/environment.hpp>
#include <render/vulkan_resource_manager.hpp>
#include <ds/mem_with_len.hpp>

namespace vke_render
{
    template <size_t Alignment = alignof(std::max_align_t)>
    using CPUBuffer = vke_ds::Memory<Alignment>;

    class Buffer
    {
    public:
        size_t bufferSize;
        VkBufferUsageFlags usage;
        VmaAllocationCreateFlags vmaFlags;
        VmaMemoryUsage vmaUsage;
        VkBuffer buffer;
        VmaAllocation vmaAllocation;
        VmaAllocationInfo vmaAllocationInfo;

        Buffer(VkDeviceSize size, VkBufferUsageFlags usage,
               VmaAllocationCreateFlags vmaFlags, VmaMemoryUsage vmaUsage)
            : bufferSize(size), usage(usage),
              vmaFlags(vmaFlags), vmaUsage(vmaUsage), buffer(nullptr), vmaAllocation(nullptr), vmaAllocationInfo{}
        {
            vke_render::VulkanResourceManager::CreateBuffer(
                size, usage, vmaFlags, vmaUsage,
                &buffer, &vmaAllocation, &vmaAllocationInfo);
        }

        Buffer(Buffer &&ori)
            : bufferSize(ori.bufferSize),
              usage(ori.usage),
              vmaFlags(ori.vmaFlags),
              vmaUsage(ori.vmaUsage),
              buffer(ori.buffer),
              vmaAllocation(ori.vmaAllocation),
              vmaAllocationInfo(ori.vmaAllocationInfo)
        {
            ori.buffer = nullptr;
            ori.vmaAllocation = nullptr;
        }

        ~Buffer()
        {
            if (buffer)
                vmaDestroyBuffer(RenderEnvironment::GetInstance()->vmaAllocator, buffer, vmaAllocation);
        }

        VkDescriptorBufferInfo GetDescriptorBufferInfo(VkDeviceSize offset = 0)
        {
            VkDescriptorBufferInfo bufferInfo;
            bufferInfo.buffer = buffer;
            bufferInfo.offset = offset;
            bufferInfo.range = bufferSize;
            return bufferInfo;
        }
    };

    class HostCoherentBuffer : public Buffer
    {
    public:
        void *data;

        HostCoherentBuffer(VkDeviceSize size, VkBufferUsageFlags usage)
            : Buffer(size, usage,
                     VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                     VMA_MEMORY_USAGE_AUTO_PREFER_HOST)
        {
            data = vmaAllocationInfo.pMappedData;
        }

        HostCoherentBuffer(HostCoherentBuffer &&ori)
            : data(ori.data), Buffer(std::forward<HostCoherentBuffer>(ori)) {}

        void ToBuffer(size_t dstOffset, const void *src, size_t size)
        {
            memcpy((char *)data + dstOffset, src, size);
        }

        void FromBuffer(size_t srcOffset, void *dst, size_t size)
        {
            memcpy(dst, (char *)data + srcOffset, size);
        }
    };

    class DeviceBuffer : public Buffer
    {
    public:
        DeviceBuffer(VkDeviceSize size, VkBufferUsageFlags usage)
            : Buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
                     0, VMA_MEMORY_USAGE_GPU_ONLY) {}

        DeviceBuffer(DeviceBuffer &&ori) : Buffer(std::forward<DeviceBuffer>(ori)) {}

        ~DeviceBuffer() {}

        void ToBuffer(size_t dstOffset, const void *src, size_t size)
        {
            HostCoherentBuffer stagingBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
            memcpy((char *)stagingBuffer.data, src, size);
            VulkanResourceManager::CopyBuffer(stagingBuffer.buffer, buffer, size, 0, dstOffset);
        }

        void FromBuffer(size_t srcOffset, void *dst, size_t size)
        {
            HostCoherentBuffer stagingBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
            VulkanResourceManager::CopyBuffer(buffer, stagingBuffer.buffer, size, srcOffset, 0);
            memcpy(dst, (char *)stagingBuffer.data, size);
        }
    };

    class StagedBuffer : public Buffer
    {
    public:
        void *data;
        HostCoherentBuffer stagingBuffer;

        StagedBuffer(VkDeviceSize size, VkBufferUsageFlags usage)
            : stagingBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT),
              Buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
                     0, VMA_MEMORY_USAGE_GPU_ONLY)
        {
            data = stagingBuffer.data;
        }

        StagedBuffer(StagedBuffer &&ori)
            : data(ori.data), stagingBuffer(std::move(ori.stagingBuffer)), Buffer(std::forward<StagedBuffer>(ori)) {}

        void ToBuffer(size_t offset = 0)
        {
            VulkanResourceManager::CopyBuffer(stagingBuffer.buffer, buffer, bufferSize, offset, offset);
        }

        void ToBuffer(size_t offset, size_t size)
        {
            VulkanResourceManager::CopyBuffer(stagingBuffer.buffer, buffer, size, offset, offset);
        }

        void ToBufferAsync(VkCommandBuffer commandBuffer, size_t offset, size_t size)
        {
            VkBufferMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = stagingBuffer.buffer;
            barrier.offset = offset;
            barrier.size = size;

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_HOST_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0, nullptr,
                1, &barrier,
                0, nullptr);

            VkBufferCopy copyRegion = {offset, offset, size};
            vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer, buffer, 1, &copyRegion);
        }

        void FromBuffer(size_t offset, size_t size)
        {
            VulkanResourceManager::CopyBuffer(buffer, stagingBuffer.buffer, size, offset, offset);
        }

        void FromBufferAsync(VkCommandBuffer commandBuffer, size_t offset, size_t size)
        {
            VkBufferCopy copyRegion = {offset, offset, size};
            vkCmdCopyBuffer(commandBuffer, buffer, stagingBuffer.buffer, 1, &copyRegion);
        }
    };
}

#endif