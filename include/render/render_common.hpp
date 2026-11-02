#ifndef RENDER_COMMON_H
#define RENDER_COMMON_H

#include <common.hpp>
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
