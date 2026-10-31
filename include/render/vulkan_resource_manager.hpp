#ifndef VULKAN_RESOURCE_MANAGER_H
#define VULKAN_RESOURCE_MANAGER_H

#include <render/descriptor.hpp>
#include <vma/vk_mem_alloc.h>
#include <compare>

namespace vke_render
{
    struct SamplerDesc
    {
        VkSamplerCreateFlags flags = 0;
        VkFilter magFilter = VK_FILTER_LINEAR;
        VkFilter minFilter = VK_FILTER_LINEAR;
        VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        float mipLodBias = 0.0f;
        bool anisotropyEnable = false;
        float maxAnisotropy = 1.0f;
        bool compareEnable = false;
        VkCompareOp compareOp = VK_COMPARE_OP_ALWAYS;
        float minLod = 0.0f;
        float maxLod = VK_LOD_CLAMP_NONE;
        VkBorderColor borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        bool unnormalizedCoordinates = false;

        auto operator<=>(const SamplerDesc &) const = default;
    };

    class VulkanResourceManager
    {
    private:
        static VulkanResourceManager *instance;
        static const int MAX_SET_CNT = 10;
        static const int MAX_COMBINED_IMAGE_SAMPLER_DESC_CNT = 20;
        static const int MAX_STORAGE_IMAGE_DESC_CNT = 10;
        static const int MAX_UNIFORM_DESC_CNT = 10;
        static const int MAX_STORAGE_DESC_CNT = 10;

        VulkanResourceManager()
        {
            DescriptorSetPoolInfo info(MAX_SET_CNT,
                                       {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_COMBINED_IMAGE_SAMPLER_DESC_CNT},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MAX_STORAGE_IMAGE_DESC_CNT},
                                        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_UNIFORM_DESC_CNT},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_STORAGE_DESC_CNT}});
            VkDescriptorPool pool = createDescriptorPool(info);
            descriptorSetPools[pool] = std::move(info);
        }

        ~VulkanResourceManager() {}

        VulkanResourceManager(const VulkanResourceManager &);
        VulkanResourceManager &operator=(const VulkanResourceManager);

    public:
        static VulkanResourceManager *GetInstance()
        {
            return instance;
        }

        static VulkanResourceManager *Init()
        {
            instance = new VulkanResourceManager();
            return instance;
        }

        static void Dispose()
        {
            for (auto &kv : instance->descriptorSetPools)
                vkDestroyDescriptorPool(globalLogicalDevice, kv.first, nullptr);
            for (const auto &[desc, sampler] : instance->samplers)
                vkDestroySampler(globalLogicalDevice, sampler, nullptr);
            delete instance;
        }

        static void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                 VmaAllocationCreateFlags vmaFlags, VmaMemoryUsage vmaUsage,
                                 VkBuffer *buffer, VmaAllocation *vmaAllocation, VmaAllocationInfo *vmaAllocationInfo);

        static void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0);

        static void CreateImage(uint32_t width, uint32_t height, VkFormat format,
                                VkImageTiling tiling, VkImageUsageFlags usage, uint32_t mipLevelCnt,
                                VkMemoryPropertyFlags properties, VkImage *image,
                                VmaAllocation *vmaAllocation, VmaAllocationInfo *vmaAllocationInfo);

        static VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags,
                                           uint32_t mipLevelCnt = 1, uint32_t baseArrayLayer = 0,
                                           uint32_t layerCnt = 1, VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D);

        static void MakeLayoutTransition(VkCommandBuffer commandBuffer,
                                         VkAccessFlags srcAccessMask,
                                         VkAccessFlags dstAccessMask,
                                         VkImageLayout oldLayout,
                                         VkImageLayout newLayout,
                                         VkImage image,
                                         VkImageAspectFlags aspectMask,
                                         uint32_t mipLevelCnt,
                                         VkPipelineStageFlags sourceStage,
                                         VkPipelineStageFlags destinationStage);

        static void CopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

        static VkSampler GetSampler(const SamplerDesc &desc);

        static VkSampler GetSampler(VkFilter minFilter = VK_FILTER_LINEAR,
                                    VkFilter magFilter = VK_FILTER_LINEAR,
                                    VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                    bool anisotropyEnable = true,
                                    VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                                    float maxLod = VK_LOD_CLAMP_NONE);

        static VkSampler GetSampler(VkFilter minFilter, VkFilter magFilter,
                                    VkSamplerAddressMode addressModeU,
                                    VkSamplerAddressMode addressModeV,
                                    VkSamplerAddressMode addressModeW,
                                    bool anisotropyEnable = true,
                                    VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                                    float maxLod = VK_LOD_CLAMP_NONE);

        static VkDescriptorSet AllocateDescriptorSet(DescriptorSetInfo &info)
        {
            for (auto &pool : instance->descriptorSetPools)
            {
                DescriptorSetPoolInfo &poolInfo = pool.second;
                if (poolInfo.SuitableToAllocate(info))
                {
                    poolInfo.UpdateForAllocate(info);
                    return instance->allocateDescriptorSet(pool.first, info);
                }
            }

            std::map<VkDescriptorType, uint32_t> descriptorCntMap;
            for (auto &kv : info.descriptorCntMap)
                descriptorCntMap[kv.first] = 2 * kv.second;

            DescriptorSetPoolInfo poolInfo(MAX_SET_CNT, std::move(descriptorCntMap));
            VkDescriptorPool pool = instance->createDescriptorPool(poolInfo);
            instance->descriptorSetPools[pool] = std::move(poolInfo);
            instance->descriptorSetPools[pool].UpdateForAllocate(info);
            return instance->allocateDescriptorSet(pool, info);
        }

        static void FreeDescriptorSet(VkDescriptorSet set)
        {
            if (set == VK_NULL_HANDLE)
                return;
            auto it = instance->allocations.find(set);
            if (it == instance->allocations.end())
                return;
            auto &allocation = it->second;
            VKE_VK_CHECK(vkFreeDescriptorSets(globalLogicalDevice, allocation.pool, 1, &set),
                         "failed to free descriptor set!")
            auto &pool = instance->descriptorSetPools.at(allocation.pool);
            ++pool.setCnt;
            for (const auto &[type, count] : allocation.counts)
                pool.descriptorCntMap[type] += count;
            instance->allocations.erase(it);
        }

    private:
        std::map<SamplerDesc, VkSampler> samplers;
        std::map<VkDescriptorPool, DescriptorSetPoolInfo> descriptorSetPools;
        struct Allocation
        {
            VkDescriptorPool pool;
            std::map<VkDescriptorType, uint32_t> counts;
        };
        std::map<VkDescriptorSet, Allocation> allocations;

        VkDescriptorPool createDescriptorPool(DescriptorSetPoolInfo &info)
        {
            std::vector<VkDescriptorPoolSize> poolSizes;
            info.ConstructPoolSizes(poolSizes);

            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.poolSizeCount = poolSizes.size();
            poolInfo.pPoolSizes = poolSizes.data();
            poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            poolInfo.maxSets = info.setCnt;

            VkDescriptorPool ret;
            VKE_VK_CHECK(vkCreateDescriptorPool(globalLogicalDevice, &poolInfo, nullptr, &ret), "failed to create descriptor pool!")
            return ret;
        }

        VkDescriptorSet allocateDescriptorSet(const VkDescriptorPool &pool,
                                              const DescriptorSetInfo &info)
        {
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.pNext = nullptr;
            allocInfo.descriptorPool = pool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &info.layout;
            VkDescriptorSetVariableDescriptorCountAllocateInfo countAllocateInfo{};
            if (info.variableDescriptorCnt > 0) // bindless
            {
                countAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
                countAllocateInfo.descriptorSetCount = 1;
                countAllocateInfo.pDescriptorCounts = &info.variableDescriptorCnt;
                allocInfo.pNext = &countAllocateInfo;
            }

            VkDescriptorSet ret;
            VKE_VK_CHECK(vkAllocateDescriptorSets(globalLogicalDevice, &allocInfo, &ret),
                         "failed to allocate descriptor sets!")
            allocations.emplace(ret, Allocation{pool, info.descriptorCntMap});
            return ret;
        }
    };
}

#endif
