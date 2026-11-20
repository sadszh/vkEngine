#ifndef FRUSTUM_H
#define FRUSTUM_H

#include <render/render_common.hpp>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <array>

namespace vke_render
{
    class Frustum
    {
    public:
        explicit Frustum(const glm::mat4 &viewProjection)
        {
            const glm::mat4 rows = glm::transpose(viewProjection);
            // Vulkan clip space: -w <= x,y <= w and 0 <= z <= w.
            planes = {rows[3] + rows[0], rows[3] - rows[0],
                      rows[3] + rows[1], rows[3] - rows[1],
                      rows[2], rows[3] - rows[2]};
        }

        bool Intersects(const AABB &bounds, const glm::mat4 &model) const
        {
            const AABB world = bounds.TransformBounds(model);
            const glm::vec3 center = (world.min + world.max) * 0.5f;
            const glm::vec3 extent = (world.max - world.min) * 0.5f;
            for (const glm::vec4 &plane : planes)
            {
                const glm::vec3 normal(plane);
                const float distance = glm::dot(normal, center) + plane.w;
                const float radius = glm::dot(glm::abs(normal), extent);
                if (distance + radius < 0.0f)
                    return false;
            }
            return true;
        }

    private:
        std::array<glm::vec4, 6> planes;
    };
}

#endif
