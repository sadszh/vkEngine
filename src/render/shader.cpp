#include <render/shader.hpp>
#include <algorithm>

namespace vke_render
{
    vke_common::Result<ShaderModule> ShaderModule::Create(std::span<const char> code, VkShaderStageFlagBits stage)
    {
        if (code.size() < 5 * sizeof(uint32_t) || code.size() % sizeof(uint32_t) != 0)
            return std::unexpected("invalid SPIR-V byte size");
        ShaderModule module;
        const auto result = spvReflectCreateShaderModule(code.size(), code.data(), &module.reflectInfo);
        if (result != SPV_REFLECT_RESULT_SUCCESS)
            return std::unexpected("SPIR-V reflection failed (error " + std::to_string(result) + ")");
        if (module.reflectInfo.shader_stage != static_cast<SpvReflectShaderStageFlagBits>(stage))
        {
            spvReflectDestroyShaderModule(&module.reflectInfo);
            return std::unexpected("SPIR-V execution stage does not match the requested shader stage");
        }
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = spvReflectGetCode(&module.reflectInfo);
        VKE_VK_CHECK(vkCreateShaderModule(globalLogicalDevice, &createInfo, nullptr, &module.module), "Failed to create shader module!")
        return module;
    }

    vke_common::Result<std::unique_ptr<ShaderModuleSet>> ShaderModuleSet::Create(
        std::initializer_list<std::pair<VkShaderStageFlagBits, std::span<const char>>> shaders)
    {
        auto set = std::make_unique<ShaderModuleSet>();
        for (const auto &[stage, code] : shaders)
        {
            const std::string context = stage == VK_SHADER_STAGE_VERTEX_BIT ? "vertex shader: " :
                stage == VK_SHADER_STAGE_FRAGMENT_BIT ? "fragment shader: " : "compute shader: ";
            auto loaded = ShaderModule::Create(code, stage);
            if (!loaded) return std::unexpected(context + loaded.error());
            set->modules.push_back(std::move(*loaded));
            auto &module = set->modules.back();
            VkPipelineShaderStageCreateInfo stageInfo{};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage = stage;
            stageInfo.module = module.module;
            stageInfo.pName = module.reflectInfo.entry_point_name;
            set->stageCreateInfos.push_back(stageInfo);
            if (auto result = set->constructDescriptorSetInfo(module.reflectInfo); !result)
                return std::unexpected(context + result.error());
            set->constructPushConstant(module.reflectInfo);
        }
        set->constructDescriptorSetLayout();
        return set;
    }

    vke_common::Result<void> ShaderModuleSet::constructDescriptorSetInfo(SpvReflectShaderModule &reflectInfo)
    {
        int setCnt = reflectInfo.descriptor_set_count;
        for (int i = 0; i < setCnt; i++)
        {
            SpvReflectDescriptorSet &ds = reflectInfo.descriptor_sets[i];
            uint32_t setID = ds.set;
            auto infoIt = descriptorSetInfoMap.try_emplace(setID).first;
            DescriptorSetInfo &descriptorSetInfo = infoIt->second;
            std::vector<VkDescriptorSetLayoutBinding> &bindings = bindingInfoMap[setID];
            int bindingCnt = ds.binding_count;
            for (int j = 0; j < bindingCnt; j++)
            {
                SpvReflectDescriptorBinding &bindingInfo = *ds.bindings[j];
                uint32_t desCnt = 1;
                for (int k = 0; k < bindingInfo.array.dims_count; k++)
                    desCnt *= bindingInfo.array.dims[k];

                VkDescriptorSetLayoutBinding binding{
                    bindingInfo.binding,
                    (VkDescriptorType)bindingInfo.descriptor_type,
                    desCnt,
                    VK_SHADER_STAGE_ALL,
                    nullptr};
                if (desCnt == 0) // bindless
                {
                    binding.descriptorCount = DEFAULT_BINDLESS_CNT;
                    descriptorSetInfo.variableDescriptorCnt = std::max(
                        descriptorSetInfo.variableDescriptorCnt, DEFAULT_BINDLESS_CNT);
                }

                auto existing = std::find_if(
                    bindings.begin(), bindings.end(),
                    [&binding](const VkDescriptorSetLayoutBinding &candidate)
                    { return candidate.binding == binding.binding; });
                if (existing != bindings.end())
                {
                    if (existing->descriptorType != binding.descriptorType ||
                        existing->descriptorCount != binding.descriptorCount)
                        return std::unexpected("descriptor set " + std::to_string(setID) + " binding " +
                                               std::to_string(binding.binding) + " differs between shader stages");
                    existing->stageFlags |= binding.stageFlags;
                    continue;
                }

                descriptorSetInfo.AddCnt(binding.descriptorType, binding.descriptorCount);
                bindings.push_back(binding);
            }
        }
        return {};
    }

    void ShaderModuleSet::constructPushConstant(SpvReflectShaderModule &reflectInfo)
    {
        int pushConstantCnt = reflectInfo.push_constant_block_count;
        for (int i = 0; i < pushConstantCnt; i++)
        {
            SpvReflectBlockVariable &block = reflectInfo.push_constant_blocks[i];

            auto it = pushConstantRangeMap.find(block.name);
            if (it != pushConstantRangeMap.end())
                continue;

            VkPushConstantRange range{};
            range.stageFlags = VK_SHADER_STAGE_ALL;
            range.offset = block.offset;
            range.size = block.padded_size;
            pushConstantRangeMap[block.name] = range;
        }
    }

    void ShaderModuleSet::constructDescriptorSetLayout()
    {
        if (descriptorSetInfoMap.empty())
            return;

        const uint32_t maxSet = descriptorSetInfoMap.rbegin()->first;
        for (uint32_t setID = 0; setID <= maxSet; ++setID)
        {
            DescriptorSetInfo &descriptorSetInfo = descriptorSetInfoMap.try_emplace(setID).first->second;
            std::vector<VkDescriptorSetLayoutBinding> &bindings = bindingInfoMap[setID];

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.pNext = nullptr;
            layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
            layoutInfo.bindingCount = bindings.size();
            layoutInfo.pBindings = bindings.data();

            VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfo{};
            std::vector<VkDescriptorBindingFlags> flags;
            if (descriptorSetInfo.variableDescriptorCnt > 0) // bindless
            {
                flags.resize(bindings.size());
                for (int i = 0; i < bindings.size() - 1; i++)
                    flags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
                flags[bindings.size() - 1] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                                             VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                                             VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

                bindingFlagsCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
                bindingFlagsCreateInfo.pNext = nullptr;
                bindingFlagsCreateInfo.bindingCount = bindings.size();
                bindingFlagsCreateInfo.pBindingFlags = flags.data();
                layoutInfo.pNext = &bindingFlagsCreateInfo;
            }

            VKE_VK_CHECK(vkCreateDescriptorSetLayout(globalLogicalDevice,
                                                     &layoutInfo,
                                                     nullptr,
                                                     &(descriptorSetInfo.layout)),
                         "Failed to create descriptor set layout!")
            descriptorSetLayouts.push_back(descriptorSetInfo.layout);
        }
    }
}
