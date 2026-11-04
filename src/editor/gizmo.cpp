#include <editor/gizmo.hpp>
#include <scene.hpp>
#include <render/light_manager.hpp>
#include <physics/physics.hpp>
#include <component/rigidbody.hpp>
#include <component/sensor.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <array>

#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif

namespace vke_editor
{
    void appendFrustumLines(const vke_render::CameraInfo &camera, std::vector<glm::vec3> &lines)
    {
        const float tangent = glm::tan(camera.fov * 0.5f);
        std::array<glm::vec3, 8> corners;
        for (unsigned plane = 0; plane < 2; ++plane)
        {
            const float distance = plane == 0 ? camera.near : camera.far;
            const float height = tangent * distance;
            const float width = height * camera.aspect;
            const glm::vec3 local[] = {{-width, -height, -distance}, {width, -height, -distance}, {width, height, -distance}, {-width, height, -distance}};
            for (unsigned corner = 0; corner < 4; ++corner)
                corners[plane * 4 + corner] = glm::vec3(camera.invView * glm::vec4(local[corner], 1.0f));
        }
        for (unsigned corner = 0; corner < 4; ++corner)
        {
            const unsigned next = (corner + 1) % 4;
            lines.insert(lines.end(), {corners[corner], corners[next],
                                       corners[corner + 4], corners[next + 4],
                                       corners[corner], corners[corner + 4]});
        }
    }

    namespace
    {
        constexpr unsigned circleSegments = 48;

        void appendCircle(glm::vec3 center, glm::vec3 u, glm::vec3 v, std::vector<glm::vec3> &lines)
        {
            glm::vec3 previous = center + u;
            for (unsigned i = 1; i <= circleSegments; ++i)
            {
                const float angle = glm::two_pi<float>() * i / circleSegments;
                const glm::vec3 next = center + u * glm::cos(angle) + v * glm::sin(angle);
                lines.insert(lines.end(), {previous, next});
                previous = next;
            }
        }

        glm::vec3 perpendicular(glm::vec3 direction)
        {
            const glm::vec3 axis = glm::abs(direction.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            return glm::normalize(glm::cross(direction, axis));
        }
    }

    void extractLightLines(const vke_common::SceneManager &scene, const vke_render::LightManager &lights,
                           entt::entity entity, std::vector<glm::vec3> &lines)
    {
        if (entity == entt::null || scene.IsPendingDestroy(entity))
            return;
        if (lights.HasLight<vke_render::DirectionalLight>(entity))
        {
            const auto &light = lights.GetLightWithoutCheckByEntity<vke_render::DirectionalLight>(entity);
            const auto position = scene.registry.get<vke_common::Transform>(entity).GetGlobalPosition();
            const glm::vec3 direction(light.direction);
            const auto u = perpendicular(direction);
            const auto v = glm::cross(direction, u);
            const auto tip = position + direction * 2.0f;
            const auto base = tip - direction * 0.35f;
            lines.insert(lines.end(), {position, tip, tip, base + u * 0.2f, tip, base - u * 0.2f,
                                       tip, base + v * 0.2f, tip, base - v * 0.2f});
        }
        if (lights.HasLight<vke_render::PointLight>(entity))
        {
            const auto &light = lights.GetLightWithoutCheckByEntity<vke_render::PointLight>(entity);
            const glm::vec3 position(light.positionWithRadius);
            const float radius = light.positionWithRadius.w;
            if (radius > 0.0f)
            {
                const glm::vec3 x(radius, 0, 0), y(0, radius, 0), z(0, 0, radius);
                appendCircle(position, x, y, lines);
                appendCircle(position, x, z, lines);
                appendCircle(position, y, z, lines);
            }
        }
        if (lights.HasLight<vke_render::SpotLight>(entity))
        {
            const auto &light = lights.GetLightWithoutCheckByEntity<vke_render::SpotLight>(entity);
            const float radius = light.positionWithRadius.w;
            if (radius <= 0.0f)
                return;
            const glm::vec3 position(light.positionWithRadius);
            const glm::vec3 direction(light.direction);
            const auto u = perpendicular(direction);
            const auto v = glm::cross(direction, u);
            lines.insert(lines.end(), {position, position + direction * radius});
            const auto appendCone = [&](float cosine)
            {
                const float angle = glm::acos(cosine);
                const float ringRadius = radius * glm::sin(angle);
                const auto center = position + direction * (radius * cosine);
                appendCircle(center, u * ringRadius, v * ringRadius, lines);
                for (const auto radial : {u, v, -u, -v})
                {
                    lines.insert(lines.end(), {position, center + radial * ringRadius});
                    glm::vec3 previous = position + direction * radius;
                    for (unsigned i = 1; i <= circleSegments / 4; ++i)
                    {
                        const float theta = angle * i / (circleSegments / 4);
                        const auto next = position + radius * (direction * glm::cos(theta) + radial * glm::sin(theta));
                        lines.insert(lines.end(), {previous, next});
                        previous = next;
                    }
                }
            };
            appendCone(light.cone.y);
            if (light.cone.x != light.cone.y)
                appendCone(light.cone.x);
        }
    }

    void extractCollisionLines(const vke_common::SceneManager &scene, entt::entity entity, std::vector<glm::vec3> &outVertices)
    {
        outVertices.clear();

        if (!scene.registry.valid(entity))
            return;

        JPH::BodyID bodyID;

        auto *rigidBody = scene.registry.try_get<vke_component::RigidBody>(entity);
        auto *sensor = scene.registry.try_get<vke_component::Sensor>(entity);

        if (rigidBody && !rigidBody->bodyID.IsInvalid())
        {
            bodyID = rigidBody->bodyID;
        }
        else if (sensor && !sensor->bodyID.IsInvalid())
        {
            bodyID = sensor->bodyID;
        }
        else
        {
            return;
        }

        JPH::BodyInterface &bodyInterface = vke_physics::PhysicsManager::GetBodyInterface();
        if (!bodyInterface.IsAdded(bodyID))
            return;

        JPH::TransformedShape ts = bodyInterface.GetTransformedShape(bodyID);

        struct TriangleCollector final : JPH::TransformedShapeCollector
        {
            std::vector<glm::vec3> &outVertices;

            TriangleCollector(std::vector<glm::vec3> &out)
                : outVertices(out) {}

            void AddHit(const JPH::TransformedShape &leaf) override
            {
                JPH::Shape::GetTrianglesContext ctx;
                leaf.GetTrianglesStart(ctx, JPH::AABox::sBiggest(), JPH::RVec3::sZero());

                const int batchSize = 1024;
                JPH::Float3 triangleVerts[batchSize * 3];
                int numTriangles;
                while ((numTriangles = leaf.GetTrianglesNext(ctx, batchSize, triangleVerts, nullptr)) > 0)
                {
                    for (int t = 0; t < numTriangles; t++)
                    {
                        JPH::Float3 &v0 = triangleVerts[t * 3 + 0];
                        JPH::Float3 &v1 = triangleVerts[t * 3 + 1];
                        JPH::Float3 &v2 = triangleVerts[t * 3 + 2];

                        outVertices.emplace_back(v0.x, v0.y, v0.z);
                        outVertices.emplace_back(v1.x, v1.y, v1.z);
                        outVertices.emplace_back(v1.x, v1.y, v1.z);
                        outVertices.emplace_back(v2.x, v2.y, v2.z);
                        outVertices.emplace_back(v2.x, v2.y, v2.z);
                        outVertices.emplace_back(v0.x, v0.y, v0.z);
                    }
                }
            }
        };

        TriangleCollector collector(outVertices);
        ts.CollectTransformedShapes(JPH::AABox::sBiggest(), collector);
    }

}
