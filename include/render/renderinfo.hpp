#ifndef RENDER_INFO_H
#define RENDER_INFO_H

#include <render/material.hpp>
#include <render/mesh.hpp>
#include <render/frustum.hpp>
#include <render/pipeline.hpp>
#include <ds/id_allocator.hpp>

namespace vke_render
{
    struct RenderUnit
    {
        std::shared_ptr<const Mesh> mesh;
        std::vector<PushConstantInfo> pushConstantInfos;
        uint32_t perPrimitiveStart;
        VkDescriptorSet perUnitDescriptorSet;
        bool ownsDescriptorSet = true;
        glm::mat4 modelMatrix{1.0f};

        RenderUnit() : perPrimitiveStart(0), perUnitDescriptorSet(nullptr) {}

        RenderUnit(std::shared_ptr<const Mesh> &msh, const glm::mat4 &model, VkDescriptorSet descriptorSet = nullptr, bool ownsDescriptorSet = true)
            : mesh(msh), perPrimitiveStart(0), perUnitDescriptorSet(descriptorSet), ownsDescriptorSet(ownsDescriptorSet), modelMatrix(model) {}

        // Additional constants follow the model matrix at offset zero.
        RenderUnit(std::shared_ptr<const Mesh> &msh, const glm::mat4 &model, std::vector<PushConstantInfo> &&cInfos, uint32_t perPrimitiveStart, VkDescriptorSet descriptorSet = nullptr, bool ownsDescriptorSet = true)
            : mesh(msh), pushConstantInfos(std::move(cInfos)), perPrimitiveStart(std::min(perPrimitiveStart, (uint32_t)pushConstantInfos.size())),
              perUnitDescriptorSet(descriptorSet), ownsDescriptorSet(ownsDescriptorSet), modelMatrix(model) {}

        RenderUnit(const RenderUnit &) = delete;
        RenderUnit &operator=(const RenderUnit &) = delete;

        ~RenderUnit()
        {
            if (ownsDescriptorSet)
                VulkanResourceManager::FreeDescriptorSet(perUnitDescriptorSet);
        }

        bool IsVisible(const Frustum &frustum) const
        {
            return !mesh->joints.empty() || frustum.Intersects(mesh->bounds, modelMatrix);
        }

        void Render(VkCommandBuffer &commandBuffer, VkPipelineLayout &pipelineLayout, int descriptorSetOffset)
        {
            if (mesh->infos.size() == 0)
                return;

            vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_ALL, 0, sizeof(modelMatrix), &modelMatrix);

            if (perUnitDescriptorSet != nullptr)
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipelineLayout,
                    descriptorSetOffset,
                    1,
                    &perUnitDescriptorSet,
                    0, nullptr);

            for (int i = 0; i < perPrimitiveStart; i++)
            {
                auto &info = pushConstantInfos[i];
                vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_ALL, info.offset, info.size, info.pValues);
            }

            if (mesh->infos.size() == 1)
            {
                for (int i = perPrimitiveStart; i < pushConstantInfos.size(); i++)
                {
                    auto &info = pushConstantInfos[i];
                    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_ALL, info.offset, info.size, info.pValues);
                }
                mesh->Render(commandBuffer);
                return;
            }

            VkIndexType prevIndexType = VK_INDEX_TYPE_MAX_ENUM;
            size_t innerOffset = 0;
            for (int i = 0; i < mesh->infos.size(); i++)
            {
                for (int j = perPrimitiveStart; j < pushConstantInfos.size(); j++)
                {
                    auto &info = pushConstantInfos[j];
                    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_ALL, info.offset, info.size,
                                       ((char *)info.pValues) + i * info.size);
                }
                mesh->RenderPrimitive(commandBuffer, i, prevIndexType);
            }
        }
    };

    class RenderInfo
    {
    public:
        std::shared_ptr<Material> material;
        std::unique_ptr<GraphicsPipeline> renderPipeline;
        VkDescriptorSet commonDescriptorSet;
        std::map<vke_ds::id64_t, RenderUnit *> units;

        RenderInfo(std::shared_ptr<Material> &mat)
            : material(mat),
              commonDescriptorSet(nullptr)
        {
            if (material->textures.size() > 0)
            {
                commonDescriptorSet = material->shader->CreateDescriptorSet(1);
                material->UpdateDescriptorSet(commonDescriptorSet);
            }
        }

        ~RenderInfo()
        {
            VulkanResourceManager::FreeDescriptorSet(commonDescriptorSet);
        }

        void CreatePipeline(const std::vector<uint32_t> &vertexAttributeSizes,
                            VkVertexInputRate vertexInputRate,
                            VkGraphicsPipelineCreateInfo &pipelineInfo)
        {
            renderPipeline = std::make_unique<GraphicsPipeline>(
                material->shader, vertexAttributeSizes, vertexInputRate, pipelineInfo);
        }

        vke_ds::id64_t AddUnit(RenderUnit *unit)
        {
            vke_ds::id64_t id = allocator.Alloc();
            units[id] = unit;
            return id;
        }

        RenderUnit *GetUnit(vke_ds::id64_t id)
        {
            const auto &kv = units.find(id);
            if (kv == units.end())
                return nullptr;
            return kv->second;
        }

        void RemoveUnit(vke_ds::id64_t id)
        {
            units.erase(id);
        }

        void Render(VkCommandBuffer &commandBuffer, VkDescriptorSet globalDescriptorSet, const Frustum *frustum = nullptr)
        {
            int setcnt = 0;
            if (globalDescriptorSet != nullptr)
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    renderPipeline->pipelineLayout,
                    setcnt++,
                    1,
                    &globalDescriptorSet,
                    0, nullptr);

            if (commonDescriptorSet != nullptr)
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    renderPipeline->pipelineLayout,
                    setcnt++,
                    1,
                    &commonDescriptorSet,
                    0, nullptr);

            material->SetPushConstants(commandBuffer, renderPipeline->pipelineLayout);
            for (auto &unit : units)
                if (frustum == nullptr || unit.second->IsVisible(*frustum))
                    unit.second->Render(commandBuffer, renderPipeline->pipelineLayout, setcnt);
        }

    private:
        vke_ds::NaiveIDAllocator<vke_ds::id64_t> allocator;
    };
}

#endif
