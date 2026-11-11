#ifndef SHADER_H
#define SHADER_H

#include <render/vulkan_resource_manager.hpp>
#include <span>
#include <initializer_list>
#include <utility>
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

        static vke_common::Result<ShaderModule> Create(std::span<const char> code, VkShaderStageFlagBits stage);

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

        static vke_common::Result<std::unique_ptr<ShaderModuleSet>> Create(
            std::initializer_list<std::pair<VkShaderStageFlagBits, std::span<const char>>> shaders);

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
        vke_common::Result<void> constructDescriptorSetInfo(SpvReflectShaderModule &reflectInfo);
        void constructPushConstant(SpvReflectShaderModule &reflectInfo);
        void constructDescriptorSetLayout();
    };
}

#endif