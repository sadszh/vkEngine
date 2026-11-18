#pragma once

#include <render/render_common.hpp>
#include <array>
#include <memory>
#include <mutex>
#include <vector>

namespace vke_render
{
    class HostCoherentBuffer;
}

namespace vke_common
{
    class AssetUploadTimeline
    {
    public:
        AssetUploadTimeline();
        ~AssetUploadTimeline();
        AssetUploadTimeline(const AssetUploadTimeline &) = delete;
        AssetUploadTimeline &operator=(const AssetUploadTimeline &) = delete;

        uint64_t Submit(VkCommandBuffer transfer, VkCommandBuffer graphics);
        void Wait(uint64_t value) const;
        uint64_t GetCompletedValue() const;

    private:
        std::mutex submitMutex;
        VkSemaphore transferComplete = VK_NULL_HANDLE;
        VkSemaphore graphicsComplete = VK_NULL_HANDLE;
        uint64_t submitted = 0;
    };

    class AssetUploadContext
    {
    public:
        explicit AssetUploadContext(AssetUploadTimeline &timeline);
        ~AssetUploadContext();
        AssetUploadContext(const AssetUploadContext &) = delete;
        AssetUploadContext &operator=(const AssetUploadContext &) = delete;

        uint64_t Submit();
        void Drain();
        size_t GetStagedBytes() const { return stagedBytes; }
        void UploadBuffer(VkBuffer destination, size_t offset, const void *data, size_t size);
        VkCommandBuffer UploadImage(VkImage image, const void *data, size_t size,
                                    uint32_t width, uint32_t height, uint32_t mipLevels);

    private:
        static constexpr size_t SlotCount = 3;
        struct Slot
        {
            VkCommandBuffer transfer = VK_NULL_HANDLE;
            VkCommandBuffer graphics = VK_NULL_HANDLE;
            uint64_t value = 0;
            std::vector<std::unique_ptr<vke_render::HostCoherentBuffer>> staging;
        };

        AssetUploadTimeline &timeline;
        VkCommandPool transferPool = VK_NULL_HANDLE;
        VkCommandPool graphicsPool = VK_NULL_HANDLE;
        uint32_t transferFamily = 0;
        uint32_t graphicsFamily = 0;
        std::array<Slot, SlotCount> slots;
        size_t slotIndex = SlotCount - 1;
        size_t stagedBytes = 0;

        void createCommandPools();
        void reclaimCompletedSlots(uint64_t completed);
        void beginCommands();
        VkBuffer stage(const void *data, size_t size);
    };
}
