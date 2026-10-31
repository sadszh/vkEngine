#ifndef DESCRIPTOR_H
#define DESCRIPTOR_H

#include <render/render_common.hpp>
#include <map>
#include <vector>
#include <stdexcept>

namespace vke_render
{

    const uint32_t DEFAULT_BINDLESS_CNT = 1024;

    void InitDescriptorSetLayoutBinding(VkDescriptorSetLayoutBinding &dst,
                                        uint32_t binding,
                                        VkDescriptorType descriptorType,
                                        uint32_t descriptorCount,
                                        VkShaderStageFlags stageFlags,
                                        const VkSampler *pImmutableSamplers);

    struct DescriptorSetInfo
    {
        VkDescriptorSetLayout layout;
        uint32_t variableDescriptorCnt;
        std::map<VkDescriptorType, uint32_t> descriptorCntMap;

        DescriptorSetInfo() : layout(nullptr), variableDescriptorCnt(0) {}

        DescriptorSetInfo(VkDescriptorSetLayout layout, uint32_t variableDescriptorCnt)
            : layout(layout),
              variableDescriptorCnt(variableDescriptorCnt) {}

        DescriptorSetInfo(const DescriptorSetInfo &) = delete;
        DescriptorSetInfo &operator=(const DescriptorSetInfo &) = delete;

        DescriptorSetInfo &operator=(DescriptorSetInfo &&ano)
        {
            if (this != &ano)
            {
                layout = ano.layout;
                variableDescriptorCnt = ano.variableDescriptorCnt;
                descriptorCntMap = std::move(ano.descriptorCntMap);
            }
            return *this;
        }

        DescriptorSetInfo(DescriptorSetInfo &&ano)
            : layout(ano.layout), variableDescriptorCnt(ano.variableDescriptorCnt), descriptorCntMap(std::move(ano.descriptorCntMap)) {}

        ~DescriptorSetInfo()
        {
            if (layout)
                vkDestroyDescriptorSetLayout(globalLogicalDevice, layout, nullptr);
        }

        void AddCnt(VkDescriptorType type, int cnt)
        {
            auto it = descriptorCntMap.find(type);
            if (it == descriptorCntMap.end())
            {
                descriptorCntMap[type] = cnt;
                return;
            }
            it->second += cnt;
        }

        void AddCnt(VkDescriptorSetLayoutBinding info)
        {
            AddCnt(info.descriptorType, info.descriptorCount);
        }
    };

    struct DescriptorSetPoolInfo
    {
        uint32_t setCnt;
        std::map<VkDescriptorType, uint32_t> descriptorCntMap;

        DescriptorSetPoolInfo() : setCnt(0) {}
        DescriptorSetPoolInfo(uint32_t setCnt, std::map<VkDescriptorType, uint32_t> &&descriptorCntMap)
            : setCnt(setCnt), descriptorCntMap(std::move(descriptorCntMap)) {}

        DescriptorSetPoolInfo(const DescriptorSetPoolInfo &) = delete;
        DescriptorSetPoolInfo &operator=(const DescriptorSetPoolInfo &) = delete;

        DescriptorSetPoolInfo &operator=(DescriptorSetPoolInfo &&ano)
        {
            if (this != &ano)
            {
                setCnt = ano.setCnt;
                descriptorCntMap = std::move(ano.descriptorCntMap);
            }
            return *this;
        }

        DescriptorSetPoolInfo(DescriptorSetPoolInfo &&ano)
            : setCnt(ano.setCnt), descriptorCntMap(std::move(ano.descriptorCntMap)) {}

        bool SuitableToAllocate(DescriptorSetInfo &info)
        {
            if (setCnt == 0)
                return false;
            for (auto &kv : info.descriptorCntMap)
            {
                auto it = descriptorCntMap.find(kv.first);
                if (it == descriptorCntMap.end() || it->second < kv.second)
                    return false;
            }
            return true;
        }

        void UpdateForAllocate(DescriptorSetInfo &info)
        {
            setCnt--;
            for (auto &kv : info.descriptorCntMap)
                descriptorCntMap[kv.first] -= kv.second;
        }

        void ConstructPoolSizes(std::vector<VkDescriptorPoolSize> &poolSizes)
        {
            for (auto &kv : descriptorCntMap)
            {
                VkDescriptorPoolSize uniformPoolSize{};
                uniformPoolSize.type = kv.first;
                uniformPoolSize.descriptorCount = kv.second;
                poolSizes.push_back(uniformPoolSize);
            }
        }
    };

    void ConstructDescriptorSetWrite(VkWriteDescriptorSet &descriptorWrite,
                                     VkDescriptorSet descriptorSet,
                                     uint32_t binding,
                                     VkDescriptorType descriptorType,
                                     VkDescriptorBufferInfo *bufferInfo,
                                     int st = 0,
                                     int cnt = 1);

    void ConstructDescriptorSetWrite(VkWriteDescriptorSet &descriptorWrite,
                                     VkDescriptorSet descriptorSet,
                                     uint32_t binding,
                                     VkDescriptorType descriptorType,
                                     VkDescriptorImageInfo *imageInfo,
                                     int st = 0,
                                     int cnt = 1);
}

#endif
