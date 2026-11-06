#ifndef RENDER_COMMON_H
#define RENDER_COMMON_H

#include <common.hpp>
#define VKE_VK_CHECK(op, ...) VKE_FATAL_IF(op != VK_SUCCESS, __VA_ARGS__)

#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#endif
#include <GLFW/glfw3.h>

#include <glm/vec3.hpp>

namespace vke_render
{
    struct AABB
    {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
    };

    extern VkDevice globalLogicalDevice;
}

#endif
