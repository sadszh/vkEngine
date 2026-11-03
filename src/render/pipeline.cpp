#include <render/pipeline.hpp>

namespace vke_render
{
    void GraphicsPipeline::createPipeline(const std::vector<uint32_t> &vertexAttributeSizes,
                                          VkVertexInputRate vertexInputRate,
                                          VkGraphicsPipelineCreateInfo &pipelineInfo)
    {
        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR};

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        SpvReflectShaderModule &vertReflectInfo = shader->modules[0].reflectInfo;
        uint32_t vertexAttributeOffset = 0;
        uint32_t inputVariableCount = std::min(vertReflectInfo.input_variable_count, (uint32_t)vertexAttributeSizes.size());
        std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions(inputVariableCount);
        for (int i = 0; i < inputVariableCount; i++)
        {
            int j;
            for (j = 0; j < inputVariableCount; j++)
                if (vertReflectInfo.input_variables[j]->location == i)
                    break;
            SpvReflectInterfaceVariable &variable = *(vertReflectInfo.input_variables[j]);
            vertexAttributeDescriptions[i].binding = 0;
            vertexAttributeDescriptions[i].location = variable.location;
            vertexAttributeDescriptions[i].format = (VkFormat)variable.format;
            vertexAttributeDescriptions[i].offset = vertexAttributeOffset;
            vertexAttributeOffset += vertexAttributeSizes[i];
        }

        VkVertexInputBindingDescription vertexBindingDescription{};
        vertexBindingDescription.binding = 0;
        vertexBindingDescription.stride = vertexAttributeOffset;
        vertexBindingDescription.inputRate = vertexInputRate;

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = inputVariableCount == 0 ? 0 : 1;
        vertexInputInfo.pVertexBindingDescriptions = &vertexBindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = vertexAttributeDescriptions.size();
        vertexInputInfo.pVertexAttributeDescriptions = vertexAttributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;
        if (pipelineInfo.pInputAssemblyState != nullptr)
        {
            inputAssembly.topology = pipelineInfo.pInputAssemblyState->topology;
            inputAssembly.primitiveRestartEnable = pipelineInfo.pInputAssemblyState->primitiveRestartEnable;
        }

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;

        if (pipelineInfo.pRasterizationState != nullptr)
        {
            rasterizer.cullMode = pipelineInfo.pRasterizationState->cullMode;
            rasterizer.frontFace = pipelineInfo.pRasterizationState->frontFace;
            rasterizer.depthBiasEnable = pipelineInfo.pRasterizationState->depthBiasEnable;
            rasterizer.depthBiasConstantFactor = pipelineInfo.pRasterizationState->depthBiasConstantFactor;
            rasterizer.depthBiasClamp = pipelineInfo.pRasterizationState->depthBiasClamp;
            rasterizer.depthBiasSlopeFactor = pipelineInfo.pRasterizationState->depthBiasSlopeFactor;
        }
        else
        {
            rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
            rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterizer.depthBiasEnable = VK_FALSE;
            rasterizer.depthBiasConstantFactor = 0.0f; // Optional
            rasterizer.depthBiasClamp = 0.0f;          // Optional
            rasterizer.depthBiasSlopeFactor = 0.0f;    // Optional
        }

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.minSampleShading = 1.0f;          // Optional
        multisampling.pSampleMask = nullptr;            // Optional
        multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
        multisampling.alphaToOneEnable = VK_FALSE;      // Optional

        VkPipelineRenderingCreateInfo *pipelineRenderingCreateInfo = (VkPipelineRenderingCreateInfo *)(pipelineInfo.pNext);

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
        if (pipelineInfo.pColorBlendState != nullptr)
            colorBlending = *pipelineInfo.pColorBlendState;
        else
        {
            colorBlendAttachments.resize(pipelineRenderingCreateInfo->colorAttachmentCount);
            for (auto &attachment : colorBlendAttachments)
            {
                attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                attachment.blendEnable = VK_FALSE;
            }
            colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlending.logicOpEnable = VK_FALSE;
            colorBlending.logicOp = VK_LOGIC_OP_COPY;
            colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
            colorBlending.pAttachments = colorBlendAttachments.data();
        }

        /////////////////////////////////////////////////////////////////////////////
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = shader->descriptorSetLayouts.size();
        pipelineLayoutInfo.pSetLayouts = shader->descriptorSetLayouts.data();
        std::vector<VkPushConstantRange> pushConstantRanges(shader->pushConstantRangeMap.size());
        int i = 0;
        for (auto &kv : shader->pushConstantRangeMap)
            pushConstantRanges[i++] = kv.second;
        pipelineLayoutInfo.pushConstantRangeCount = pushConstantRanges.size();
        pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();

        VKE_VK_CHECK(vkCreatePipelineLayout(globalLogicalDevice, &pipelineLayoutInfo, nullptr, &(pipelineLayout)), "Failed to create compute pipeline layout!")
        /////////////////////////////////////////////////////////////////////////////

        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = shader->stageCreateInfos.size();
        pipelineInfo.pStages = shader->stageCreateInfos.data();

        if (pipelineInfo.pVertexInputState == nullptr)
            pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        // pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipelineLayout;

        VKE_VK_CHECK(vkCreateGraphicsPipelines(globalLogicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline), "Failed to create graphics pipeline!")
    }

    void ComputePipeline::createPipeline()
    {
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = shader->descriptorSetLayouts.size();
        pipelineLayoutInfo.pSetLayouts = shader->descriptorSetLayouts.data();
        std::vector<VkPushConstantRange> pushConstantRanges(shader->pushConstantRangeMap.size());
        int i = 0;
        for (auto &kv : shader->pushConstantRangeMap)
            pushConstantRanges[i++] = kv.second;

        pipelineLayoutInfo.pushConstantRangeCount = pushConstantRanges.size();
        pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();

        VKE_VK_CHECK(vkCreatePipelineLayout(globalLogicalDevice, &pipelineLayoutInfo, nullptr, &(pipelineLayout)), "Failed to create compute pipeline layout!")

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.stage = shader->stageCreateInfos[0];

        VKE_VK_CHECK(vkCreateComputePipelines(globalLogicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline), "Failed to create compute pipeline!")
    }
}
