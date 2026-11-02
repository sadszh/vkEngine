#ifndef SHADER_H
#define SHADER_H

#include <render/vulkan_resource_manager.hpp>
#include <assert.h>
#include <string>
#include <iostream>
#include <map>
#include <spirv_reflect.h>

namespace vke_render
{
    class ShaderModule
    {
    public:
        VkShaderModule module;
        SpvReflectShaderModule reflectInfo;

        ShaderModule() : module(nullptr) {}

        ShaderModule(const std::vector<char> &code)
        {
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = code.size();
            createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());
            VKE_VK_CHECK(vkCreateShaderModule(globalLogicalDevice, &createInfo, nullptr, &module), "Failed to create shader module!")

            SpvReflectResult result = spvReflectCreateShaderModule(code.size(), code.data(), &reflectInfo);
            assert(result == SPV_REFLECT_RESULT_SUCCESS);
        }

        ShaderModule(const ShaderModule &) = delete;
        ShaderModule &operator=(const ShaderModule &) = delete;

        ShaderModule(ShaderModule &&ano)
        {
            if (this == &ano)
                return;
            module = ano.module;
            reflectInfo = ano.reflectInfo;
            ano.module = nullptr;
        }

        ShaderModule &operator=(ShaderModule &&ano)
        {
            if (this == &ano)
                return *this;
            dispose();

            module = ano.module;
            reflectInfo = ano.reflectInfo;
            ano.module = nullptr;

            return *this;
        }

        ~ShaderModule()
        {
            dispose();
        }

    private:
        void dispose()
        {
            if (module != nullptr)
            {
                vkDestroyShaderModule(globalLogicalDevice, module, nullptr);
                spvReflectDestroyShaderModule(&reflectInfo);
            }
        }
    };

    class ShaderModuleSet
    {
    public:
        std::vector<ShaderModule> modules;
        std::vector<VkPipelineShaderStageCreateInfo> stageCreateInfos;
        std::map<uint32_t, DescriptorSetInfo> descriptorSetInfoMap;
        std::map<uint32_t, std::vector<VkDescriptorSetLayoutBinding>> bindingInfoMap;
        std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
        std::map<std::string, VkPushConstantRange> pushConstantRangeMap;

        ShaderModuleSet() {}

        ShaderModuleSet(const ShaderModuleSet &) = delete;
        ShaderModuleSet &operator=(const ShaderModuleSet &) = delete;

        ShaderModuleSet(ShaderModuleSet &&ano)
        {
            if (this == &ano)
                return;
            modules = std::move(ano.modules);
            stageCreateInfos = std::move(ano.stageCreateInfos);
            descriptorSetInfoMap = std::move(ano.descriptorSetInfoMap);
            bindingInfoMap = std::move(ano.bindingInfoMap);
            descriptorSetLayouts = std::move(ano.descriptorSetLayouts);
            pushConstantRangeMap = std::move(ano.pushConstantRangeMap);
        }

        ShaderModuleSet &operator=(ShaderModuleSet &&ano)
        {
            if (this == &ano)
                return *this;
            modules = std::move(ano.modules);
            stageCreateInfos = std::move(ano.stageCreateInfos);
            descriptorSetInfoMap = std::move(ano.descriptorSetInfoMap);
            bindingInfoMap = std::move(ano.bindingInfoMap);
            descriptorSetLayouts = std::move(ano.descriptorSetLayouts);
            pushConstantRangeMap = std::move(ano.pushConstantRangeMap);

            return *this;
        }

        template <typename... Args>
        ShaderModuleSet(Args &&...code)
        {
            (modules.emplace_back(std::forward<Args>(code)), ...);
            for (auto &module : modules)
            {
                VkPipelineShaderStageCreateInfo stageCreateInfo = VkPipelineShaderStageCreateInfo{};
                stageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stageCreateInfo.stage = (VkShaderStageFlagBits)module.reflectInfo.shader_stage;
                stageCreateInfo.module = module.module;
                stageCreateInfo.pName = module.reflectInfo.entry_point_name;
                stageCreateInfos.push_back(stageCreateInfo);
                constructDescriptorSetInfo(module.reflectInfo);
                constructPushConstant(module.reflectInfo);
            }
            constructDescriptorSetLayout();
        }

        VkDescriptorSet CreateDescriptorSet(uint32_t setID)
        {
            auto it = descriptorSetInfoMap.find(setID);
            if (it == descriptorSetInfoMap.end())
                return nullptr;

            DescriptorSetInfo &setInfo = it->second;
            VkDescriptorSet descriptorSet = vke_render::VulkanResourceManager::AllocateDescriptorSet(setInfo);
            return descriptorSet;
        }

        void CreateDescriptorSets(std::vector<VkDescriptorSet> &descriptorSets)
        {
            for (auto &kv : descriptorSetInfoMap)
                descriptorSets.push_back(vke_render::VulkanResourceManager::AllocateDescriptorSet(kv.second));
        }

    private:
        void constructDescriptorSetInfo(SpvReflectShaderModule &reflectInfo);
        void constructPushConstant(SpvReflectShaderModule &reflectInfo);
        void constructDescriptorSetLayout();
    };
}

#endif