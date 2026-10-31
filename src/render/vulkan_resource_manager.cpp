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
}
