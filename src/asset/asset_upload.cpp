#include <asset/asset_upload.hpp>
#include <render/buffer.hpp>
#include <span>

namespace vke_common
{
    namespace
    {
        VkSemaphore createTimeline()
        {
            VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
            type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
            VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            info.pNext = &type;
            VkSemaphore semaphore;
            VKE_VK_CHECK(vkCreateSemaphore(vke_render::globalLogicalDevice, &info, nullptr, &semaphore), "Cannot create asset upload timeline")
            return semaphore;
        }

        VkCommandPool createUploadPool(uint32_t family)
        {
            VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            info.queueFamilyIndex = family;
            VkCommandPool pool;
            VKE_VK_CHECK(vkCreateCommandPool(vke_render::globalLogicalDevice, &info, nullptr, &pool), "Cannot create asset command pool")
            return pool;
        }

        void submitUpload(vke_render::CommandQueue &queue, std::span<const VkCommandBuffer> commands,
                          uint64_t value, VkSemaphore signalSemaphore, VkSemaphore waitSemaphore = VK_NULL_HANDLE)
        {
            VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
            signal.semaphore = signalSemaphore;
            signal.value = value;
            signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            auto wait = signal;
            wait.semaphore = waitSemaphore;

            VkCommandBufferSubmitInfo buffers[2]{};
            for (size_t i = 0; i < commands.size(); ++i)
            {
                buffers[i].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
                buffers[i].commandBuffer = commands[i];
                buffers[i].deviceMask = 1;
            }

            VkSubmitInfo2 info{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
            info.waitSemaphoreInfoCount = waitSemaphore != VK_NULL_HANDLE;
            info.pWaitSemaphoreInfos = &wait;
            info.signalSemaphoreInfoCount = 1;
            info.pSignalSemaphoreInfos = &signal;
            info.commandBufferInfoCount = static_cast<uint32_t>(commands.size());
            info.pCommandBufferInfos = buffers;
            queue.Submit(1, &info, VK_NULL_HANDLE);
        }
    }

    AssetUploadTimeline::AssetUploadTimeline()
    {
        graphicsComplete = createTimeline();
        if (vke_render::RenderEnvironment::HasQueue(vke_render::TRANSFER_QUEUE))
            transferComplete = createTimeline();
    }

    AssetUploadTimeline::~AssetUploadTimeline()
    {
        vkDestroySemaphore(vke_render::globalLogicalDevice, transferComplete, nullptr);
        vkDestroySemaphore(vke_render::globalLogicalDevice, graphicsComplete, nullptr);
    }

    void AssetUploadTimeline::Wait(uint64_t value) const
    {
        VkSemaphoreWaitInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
        info.semaphoreCount = 1;
        info.pSemaphores = &graphicsComplete;
        info.pValues = &value;
        VKE_VK_CHECK(vkWaitSemaphores(vke_render::globalLogicalDevice, &info, UINT64_MAX), "Cannot wait for asset upload")
    }

    uint64_t AssetUploadTimeline::GetCompletedValue() const
    {
        uint64_t value;
        VKE_VK_CHECK(vkGetSemaphoreCounterValue(vke_render::globalLogicalDevice, graphicsComplete, &value), "Cannot query asset upload timeline")
        return value;
    }

    uint64_t AssetUploadTimeline::Submit(VkCommandBuffer transfer, VkCommandBuffer graphics)
    {
        std::lock_guard lock(submitMutex);
        const uint64_t value = ++submitted;
        auto *graphicsQueue = vke_render::RenderEnvironment::GetGraphicsQueue();
        if (transferComplete)
        {
            auto *transferQueue = vke_render::RenderEnvironment::GetQueue(vke_render::TRANSFER_QUEUE);
            submitUpload(*transferQueue, {&transfer, 1}, value, transferComplete);
            submitUpload(*graphicsQueue, {&graphics, 1}, value, graphicsComplete, transferComplete);
        }
        else
        {
            const VkCommandBuffer commands[]{transfer, graphics};
            submitUpload(*graphicsQueue, commands, value, graphicsComplete);
        }
        return value;
    }

    AssetUploadContext::AssetUploadContext(AssetUploadTimeline &timeline) : timeline(timeline) {}

    void AssetUploadContext::createCommandPools()
    {
        graphicsFamily = vke_render::RenderEnvironment::GetQueueFamilyIndex(vke_render::GRAPHICS_QUEUE);
        transferFamily = vke_render::RenderEnvironment::GetQueueFamilyIndex(vke_render::TRANSFER_QUEUE);
        transferPool = createUploadPool(transferFamily);
        graphicsPool = transferFamily == graphicsFamily ? transferPool : createUploadPool(graphicsFamily);
        for (auto &slot : slots)
        {
            VkCommandBufferAllocateInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            info.commandBufferCount = 1;
            info.commandPool = transferPool;
            VKE_VK_CHECK(vkAllocateCommandBuffers(vke_render::globalLogicalDevice, &info, &slot.transfer), "Cannot allocate asset transfer commands")
            info.commandPool = graphicsPool;
            VKE_VK_CHECK(vkAllocateCommandBuffers(vke_render::globalLogicalDevice, &info, &slot.graphics), "Cannot allocate asset graphics commands")
        }
    }

    AssetUploadContext::~AssetUploadContext()
    {
        Drain();
        if (graphicsPool != transferPool)
            vkDestroyCommandPool(vke_render::globalLogicalDevice, graphicsPool, nullptr);
        vkDestroyCommandPool(vke_render::globalLogicalDevice, transferPool, nullptr);
    }

    void AssetUploadContext::Drain()
    {
        const auto value = slots[slotIndex].value;
        if (value == 0)
            return;
        timeline.Wait(value);
        reclaimCompletedSlots(value);
    }

    void AssetUploadContext::reclaimCompletedSlots(uint64_t completed)
    {
        for (auto &slot : slots)
        {
            if (slot.value == 0 || slot.value > completed)
                continue;
            slot.staging.clear();
            slot.value = 0;
        }
    }

    void AssetUploadContext::beginCommands()
    {
        if (!transferPool)
            createCommandPools();
        slotIndex = (slotIndex + 1) % SlotCount;
        auto &slot = slots[slotIndex];
        reclaimCompletedSlots(timeline.GetCompletedValue());
        if (slot.value != 0)
        {
            timeline.Wait(slot.value);
            reclaimCompletedSlots(slot.value);
        }
        for (auto command : {slot.transfer, slot.graphics})
        {
            VKE_VK_CHECK(vkResetCommandBuffer(command, 0), "Cannot reset asset commands")
            VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            VKE_VK_CHECK(vkBeginCommandBuffer(command, &info), "Cannot begin asset commands")
        }
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        vkCmdPipelineBarrier(slot.graphics, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    }

    VkBuffer AssetUploadContext::stage(const void *data, size_t size)
    {
        if (stagedBytes == 0)
            beginCommands();
        auto buffer = std::make_unique<vke_render::HostCoherentBuffer>(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        buffer->ToBuffer(0, data, size);
        VKE_VK_CHECK(vmaFlushAllocation(vke_render::RenderEnvironment::GetInstance()->vmaAllocator,
                                        buffer->vmaAllocation, 0, size),
                     "Cannot flush asset staging buffer")
        const auto handle = buffer->buffer;
        slots[slotIndex].staging.push_back(std::move(buffer));
        stagedBytes += size;
        return handle;
    }

    void AssetUploadContext::UploadBuffer(VkBuffer destination, size_t offset, const void *data, size_t size)
    {
        const auto source = stage(data, size);
        VkBufferCopy copy{0, offset, size};
        vkCmdCopyBuffer(slots[slotIndex].transfer, source, destination, 1, &copy);
    }

    VkCommandBuffer AssetUploadContext::UploadImage(VkImage image, const void *data, size_t size,
                                                    uint32_t mipLevels, std::span<const VkBufferImageCopy> regions)
    {
        const auto source = stage(data, size);
        auto &slot = slots[slotIndex];
        vke_render::VulkanResourceManager::MakeLayoutTransition(slot.transfer, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                                                                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, image,
                                                                VK_IMAGE_ASPECT_COLOR_BIT, mipLevels, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        vkCmdCopyBufferToImage(slot.transfer, source, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<uint32_t>(regions.size()), regions.data());
        if (transferFamily != graphicsFamily)
        {
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.oldLayout = barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = transferFamily;
            barrier.dstQueueFamilyIndex = graphicsFamily;
            barrier.image = image;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1};
            vkCmdPipelineBarrier(slot.transfer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(slot.graphics, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
        return slot.graphics;
    }

    uint64_t AssetUploadContext::Submit()
    {
        if (stagedBytes == 0)
            return 0;
        auto &slot = slots[slotIndex];
        VKE_VK_CHECK(vkEndCommandBuffer(slot.transfer), "Cannot end asset transfer commands")
        VKE_VK_CHECK(vkEndCommandBuffer(slot.graphics), "Cannot end asset graphics commands")
        slot.value = timeline.Submit(slot.transfer, slot.graphics);
        stagedBytes = 0;
        return slot.value;
    }
}
