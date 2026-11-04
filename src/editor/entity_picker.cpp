#include <editor/entity_picker.hpp>
#include <editor/builtin_assets.hpp>
#include <scene.hpp>
#include <component/renderable_object.hpp>
#include <component/skeleton_animator.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstddef>
#include <cstring>

namespace vke_editor
{
    namespace
    {
        constexpr VkFormat idFormat = VK_FORMAT_R32_UINT;

        struct PickingImage
        {
            VkImage image;
            VmaAllocation allocation;
            VkImageView view;

            PickingImage(glm::uvec2 extent, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect)
            {
                vke_render::VulkanResourceManager::CreateImage(extent.x, extent.y, format,
                                                               VK_IMAGE_TILING_OPTIMAL, usage, 1, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                               &image, &allocation, nullptr);
                view = vke_render::VulkanResourceManager::CreateImageView(image, format, aspect);
            }

            ~PickingImage()
            {
                vkDestroyImageView(vke_render::globalLogicalDevice, view, nullptr);
                vmaDestroyImage(vke_render::RenderEnvironment::GetInstance()->vmaAllocator, image, allocation);
            }

            PickingImage(const PickingImage &) = delete;
            PickingImage &operator=(const PickingImage &) = delete;
        };

        struct alignas(16) PickingConstants
        {
            glm::mat4 mvp;
            uint32_t entity;
            uint32_t jointOffset;
        };

        struct PickDraw
        {
            entt::entity entity;
            const vke_render::Mesh *mesh;
            glm::mat4 model;
            bool skinned;
            uint32_t jointOffset;
        };
    }

    EntityPicker::EntityPicker(VkFormat depthFormat) : depthFormat(depthFormat)
    {
        shaders[0] = vke_common::AssetManager::LoadVertFragShaderUnique(BUILTIN_VFSHADER_ENTITY_PICKING_ID);
        shaders[1] = vke_common::AssetManager::LoadVertFragShaderUnique(BUILTIN_VFSHADER_ENTITY_PICKING_SKIN_ID);
    }

    vke_render::GraphicsPipeline &EntityPicker::getPipeline(bool skinned, uint32_t stride)
    {
        auto [it, inserted] = pipelines.try_emplace({skinned, stride});
        if (!inserted)
            return *it->second;

        VkVertexInputBindingDescription binding{0, stride, VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attributes[] = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
            {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(vke_render::SkinVertex, weights)},
            {2, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(vke_render::SkinVertex, jointIDs)}};
        VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = skinned ? 3 : 1;
        vertexInput.pVertexAttributeDescriptions = attributes;

        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &idFormat;
        rendering.depthAttachmentFormat = depthFormat;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &rendering;
        info.pVertexInputState = &vertexInput;
        info.pDepthStencilState = &depth;
        it->second = std::make_unique<vke_render::GraphicsPipeline>(shaders[skinned],
                                                                    std::vector<uint32_t>{}, VK_VERTEX_INPUT_RATE_VERTEX, info);
        return *it->second;
    }

    entt::entity EntityPicker::Pick(vke_common::SceneManager &scene, const vke_render::CameraInfo &camera,
                                    glm::uvec2 extent, glm::uvec2 pixel)
    {
        using namespace vke_render;
        const glm::mat4 viewProjection = camera.projection * camera.view;
        const Frustum frustum(viewProjection);
        std::vector<PickDraw> draws;
        std::vector<glm::mat4> joints;
        for (auto [entity, renderable] : scene.registry.view<vke_component::RenderableObject>().each())
        {
            if (!scene.IsPendingDestroy(entity) && renderable.renderUnit->IsVisible(frustum))
                draws.push_back({entity, renderable.renderUnit->mesh.get(), renderable.renderUnit->modelMatrix, false, 0});
        }
        for (auto [entity, animator] : scene.registry.view<vke_component::SkeletonAnimator>().each())
        {
            if (scene.IsPendingDestroy(entity))
                continue;
            const auto pose = animator.GetSkinningMatrices();
            const uint32_t offset = static_cast<uint32_t>(joints.size());
            draws.push_back({entity, animator.renderUnit->mesh.get(), animator.renderUnit->modelMatrix, true, offset});
            joints.resize(joints.size() + pose.size());
            for (size_t i = 0; i < pose.size(); ++i)
                for (int column = 0; column < 4; ++column)
                    ozz::math::StorePtrU(pose[i].cols[column], glm::value_ptr(joints[offset + i]) + column * 4);
        }
        if (draws.empty())
            return entt::null;

        auto *environment = RenderEnvironment::GetInstance();
        PickingImage ids(extent, idFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT);
        PickingImage depth(extent, depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        Buffer readback(sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                        VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST);
        std::unique_ptr<HostCoherentBuffer> poseBuffer;
        VkDescriptorSet poseSet = VK_NULL_HANDLE;
        if (!joints.empty())
        {
            const size_t bytes = joints.size() * sizeof(glm::mat4);
            poseBuffer = std::make_unique<HostCoherentBuffer>(bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            poseBuffer->ToBuffer(0, joints.data(), bytes);
            VKE_VK_CHECK(vmaFlushAllocation(environment->vmaAllocator, poseBuffer->vmaAllocation, 0, bytes),
                         "Failed to flush picking pose!")
            poseSet = shaders[1]->CreateDescriptorSet(0);
            const auto bufferInfo = poseBuffer->GetDescriptorBufferInfo();
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = poseSet;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.pBufferInfo = &bufferInfo;
            vkUpdateDescriptorSets(globalLogicalDevice, 1, &write, 0, nullptr);
        }

        VkCommandBuffer command = RenderEnvironment::BeginSingleTimeCommands(environment->commandPool);
        VulkanResourceManager::MakeLayoutTransition(command, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, ids.image, VK_IMAGE_ASPECT_COLOR_BIT, 1,
                                                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        const VkImageAspectFlags depthAspect = VK_IMAGE_ASPECT_DEPTH_BIT |
                                               ((depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT || depthFormat == VK_FORMAT_D24_UNORM_S8_UINT)
                                                    ? VK_IMAGE_ASPECT_STENCIL_BIT
                                                    : 0);
        VulkanResourceManager::MakeLayoutTransition(command, 0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, depth.image, depthAspect, 1,
                                                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);

        VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        colorAttachment.imageView = ids.view;
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue.color.uint32[0] = entt::to_integral(entt::entity(entt::null));
        VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        depthAttachment.imageView = depth.view;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.clearValue.depthStencil.depth = 1.0f;
        const VkRect2D pixelRect{{static_cast<int32_t>(pixel.x), static_cast<int32_t>(pixel.y)}, {1, 1}};
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea = pixelRect;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &colorAttachment;
        rendering.pDepthAttachment = &depthAttachment;
        vkCmdBeginRendering(command, &rendering);
        const VkViewport viewport{0, 0, static_cast<float>(extent.x), static_cast<float>(extent.y), 0, 1};
        vkCmdSetViewport(command, 0, 1, &viewport);
        vkCmdSetScissor(command, 0, 1, &pixelRect);
        for (const auto &draw : draws)
        {
            const PickingConstants constants{viewProjection * draw.model, entt::to_integral(draw.entity), draw.jointOffset};
            VkIndexType indexType = VK_INDEX_TYPE_MAX_ENUM;
            for (uint32_t primitive = 0; primitive < draw.mesh->infos.size(); ++primitive)
            {
                const auto &meshInfo = draw.mesh->infos[primitive];
                auto &pipeline = getPipeline(draw.skinned, static_cast<uint32_t>(meshInfo.getVertexUnitSize()));
                pipeline.Bind(command);
                if (draw.skinned)
                    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipelineLayout,
                                            0, 1, &poseSet, 0, nullptr);
                vkCmdPushConstants(command, pipeline.pipelineLayout, VK_SHADER_STAGE_ALL, 0,
                                   offsetof(PickingConstants, jointOffset) + sizeof(uint32_t), &constants);
                draw.mesh->RenderPrimitive(command, primitive, indexType);
            }
        }
        vkCmdEndRendering(command);
        VulkanResourceManager::MakeLayoutTransition(command, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ids.image,
                                                    VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageOffset = {static_cast<int32_t>(pixel.x), static_cast<int32_t>(pixel.y), 0};
        copy.imageExtent = {1, 1, 1};
        vkCmdCopyImageToBuffer(command, ids.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy);
        VkBufferMemoryBarrier hostRead{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        hostRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        hostRead.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        hostRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostRead.buffer = readback.buffer;
        hostRead.size = sizeof(uint32_t);
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                             0, nullptr, 1, &hostRead, 0, nullptr);
        RenderEnvironment::EndSingleTimeCommands(RenderEnvironment::GetGraphicsQueue(), environment->commandPool, command);
        VKE_VK_CHECK(vmaInvalidateAllocation(environment->vmaAllocator, readback.vmaAllocation, 0, sizeof(uint32_t)),
                     "Failed to invalidate picking readback!")
        uint32_t id;
        std::memcpy(&id, readback.vmaAllocationInfo.pMappedData, sizeof(id));
        if (poseSet != VK_NULL_HANDLE)
            VulkanResourceManager::FreeDescriptorSet(poseSet);
        return static_cast<entt::entity>(id);
    }
}
