#ifndef QUAD_MANAGER_H
#define QUAD_MANAGER_H

#include <render/buffer.hpp>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace vke_render
{
    using QuadID = uint32_t;
    static constexpr QuadID INVALID_QUAD_ID = std::numeric_limits<QuadID>::max();

    struct QuadInstanceGPU
    {
        glm::vec4 quadRect;
        glm::vec4 uvRect;
        glm::vec4 color;
        glm::uvec4 atlasInfo;
    };

    constexpr uint32_t QUADS_PER_BUFFER = 512;
    constexpr uint32_t MAX_QUAD_BUFFER_CNT = 16;
    constexpr uint32_t MAX_QUAD_CNT = QUADS_PER_BUFFER * MAX_QUAD_BUFFER_CNT;
    constexpr VkDeviceSize QUAD_BUFFER_SIZE = sizeof(QuadInstanceGPU) * QUADS_PER_BUFFER;
    constexpr VkDeviceSize QUAD_DATA_SIZE = sizeof(QuadInstanceGPU) * MAX_QUAD_CNT;

    inline uint32_t GetQuadBufferIndex(QuadID quadID) { return quadID >> 9; }
    inline uint32_t GetQuadSlotIndex(QuadID quadID) { return quadID & (QUADS_PER_BUFFER - 1); }
    inline VkDeviceSize GetQuadBufferOffset(uint32_t bufferIndex) { return QUAD_BUFFER_SIZE * bufferIndex; }

    class QuadManager
    {
    public:
        QuadManager() : cpuQuads(QUAD_DATA_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
        {
            freeQuads.reserve(MAX_QUAD_CNT);
            for (uint32_t bufferIndex = MAX_QUAD_BUFFER_CNT; bufferIndex-- > 0;)
                for (uint32_t slot = QUADS_PER_BUFFER; slot-- > 0;)
                    freeQuads.push_back((bufferIndex << 9) | slot);
            for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
                deviceBuffers[frame] = std::make_unique<DeviceBuffer>(QUAD_DATA_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        }

        void Sync(uint32_t currentFrame)
        {
            for (uint32_t bufferIndex = 0; bufferIndex < MAX_QUAD_BUFFER_CNT; ++bufferIndex)
            {
                if (updateCnts[bufferIndex] == 0)
                    continue;
                --updateCnts[bufferIndex];
                const VkDeviceSize offset = GetQuadBufferOffset(bufferIndex);
                VulkanResourceManager::CopyBuffer(cpuQuads.buffer, deviceBuffers[currentFrame]->buffer,
                                              QUAD_BUFFER_SIZE, offset, offset);
            }
        }

        bool CanAllocate(size_t quadCount, size_t reclaimCount) const
        {
            return quadCount <= freeQuads.size() + reclaimCount;
        }

        void Allocate(const std::vector<QuadInstanceGPU> &quads, std::vector<QuadID> &quadIDs)
        {
            quadIDs.clear();
            quadIDs.reserve(quads.size());
            for (const QuadInstanceGPU &quad : quads)
            {
                QuadID quadID = freeQuads.back();
                freeQuads.pop_back();
                const uint32_t bufferIndex = GetQuadBufferIndex(quadID);
                quadAt(quadID) = quad;
                markDirty(bufferIndex);
                quadIDs.push_back(quadID);
            }
        }

        const QuadInstanceGPU &Get(QuadID quadID) const
        {
            return reinterpret_cast<const QuadInstanceGPU *>(cpuQuads.data)[quadID];
        }

        void Update(QuadID quadID, const QuadInstanceGPU &quad)
        {
            const uint32_t bufferIndex = GetQuadBufferIndex(quadID);
            quadAt(quadID) = quad;
            markDirty(bufferIndex);
        }

        void UpdateColor(QuadID quadID, const glm::vec4 &color)
        {
            const uint32_t bufferIndex = GetQuadBufferIndex(quadID);
            quadAt(quadID).color = color;
            markDirty(bufferIndex);
        }

        void Release(const std::vector<QuadID> &quadIDs)
        {
            for (QuadID quadID : quadIDs)
                freeQuads.push_back(quadID);
        }

        void ClearQuads()
        {
            updateCnts.fill(MAX_FRAMES_IN_FLIGHT);
            freeQuads.clear();
            freeQuads.reserve(MAX_QUAD_CNT);
            for (uint32_t bufferIndex = MAX_QUAD_BUFFER_CNT; bufferIndex-- > 0;)
                for (uint32_t slot = QUADS_PER_BUFFER; slot-- > 0;)
                    freeQuads.push_back((bufferIndex << 9) | slot);
        }

        void Reset()
        {
            std::memset(cpuQuads.data, 0, QUAD_DATA_SIZE);
            ClearQuads();
        }

        VkDescriptorBufferInfo GetDescriptorBufferInfo(uint32_t currentFrame) const
        {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = deviceBuffers[currentFrame]->buffer;
            bufferInfo.offset = 0;
            bufferInfo.range = QUAD_DATA_SIZE;
            return bufferInfo;
        }

    private:
        HostCoherentBuffer cpuQuads;
        std::array<uint32_t, MAX_QUAD_BUFFER_CNT> updateCnts{};
        std::vector<QuadID> freeQuads;
        std::unique_ptr<DeviceBuffer> deviceBuffers[MAX_FRAMES_IN_FLIGHT];

        QuadInstanceGPU &quadAt(QuadID quadID)
        {
            return reinterpret_cast<QuadInstanceGPU *>(cpuQuads.data)[quadID];
        }

        void markDirty(uint32_t bufferIndex)
        {
            updateCnts[bufferIndex] = MAX_FRAMES_IN_FLIGHT;
        }
    };
}

#endif
