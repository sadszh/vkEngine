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
            const glm::vec3 localCenter = (bounds.min + bounds.max) * 0.5f;
            const glm::vec3 localExtent = (bounds.max - bounds.min) * 0.5f;
            const glm::vec3 center = glm::vec3(model * glm::vec4(localCenter, 1.0f));
            // Enclose the transformed box, including negative scale and shear.
            const glm::vec3 extent = glm::abs(glm::vec3(model[0])) * localExtent.x +
                                     glm::abs(glm::vec3(model[1])) * localExtent.y +
                                     glm::abs(glm::vec3(model[2])) * localExtent.z;
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
