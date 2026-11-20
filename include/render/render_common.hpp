#ifndef RENDER_COMMON_H
#define RENDER_COMMON_H

#include <common.hpp>
#define VKE_VK_CHECK(op, ...) VKE_FATAL_IF(op != VK_SUCCESS, __VA_ARGS__)

#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#endif
#include <GLFW/glfw3.h>

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/common.hpp>

namespace vke_render
{
    struct AABB
    {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};

        AABB TransformBounds(const glm::mat4 &model) const
        {
            const glm::vec3 localCenter = (min + max) * 0.5f;
            const glm::vec3 localExtent = (max - min) * 0.5f;
            const glm::vec3 center = glm::vec3(model * glm::vec4(localCenter, 1.0f));
            const glm::vec3 extent = glm::abs(glm::vec3(model[0])) * localExtent.x +
                                     glm::abs(glm::vec3(model[1])) * localExtent.y +
                                     glm::abs(glm::vec3(model[2])) * localExtent.z;
            return {center - extent, center + extent};
        }

        bool Intersects(const AABB &other) const
        {
            return min.x <= other.max.x && max.x >= other.min.x &&
                   min.y <= other.max.y && max.y >= other.min.y &&
                   min.z <= other.max.z && max.z >= other.min.z;
        }
    };

    extern VkDevice globalLogicalDevice;
}

#endif
