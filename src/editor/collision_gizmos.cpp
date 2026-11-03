#include <editor/editor.hpp>
#include <component/rigidbody.hpp>
#include <component/sensor.hpp>

namespace vke_editor
{
    void Editor::extractCollisionLines(entt::entity entity, std::vector<glm::vec3> &outVertices)
    {
        outVertices.clear();

        auto *scene = sceneManager;
        if (!scene->registry.valid(entity))
            return;

        JPH::BodyID bodyID;

        auto *rigidBody = scene->registry.try_get<vke_component::RigidBody>(entity);
        auto *sensor = scene->registry.try_get<vke_component::Sensor>(entity);

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
