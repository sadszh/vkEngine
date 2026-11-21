#include <navigation/navmesh_bake.hpp>
#include <component/navmesh_region.hpp>
#include <component/skeleton_animator.hpp>
#include <scene.hpp>
#include <asset/asset_manager.hpp>
#include <render/mesh.hpp>
#include <physics/physics.hpp>
#include <Jolt/Physics/Collision/TransformedShape.h>
#include <glm/matrix.hpp>
#include <cstring>
#include <unordered_map>
#include <utility>

namespace vke_navmesh
{
    using vke_common::Result;

    namespace
    {
        struct MeshGeometry
        {
            std::vector<char> vertices;
            std::vector<char> indices;
        };

        void appendMeshTriangles(const vke_render::Mesh &mesh, const MeshGeometry &geometry,
                                 const glm::mat4 &model, std::vector<glm::vec3> &triangles)
        {
            const bool mirrored = glm::determinant(model) < 0.0f;
            const uint64_t corners[3] = {0, mirrored ? 2u : 1u, mirrored ? 1u : 2u};
            const char *vertexData = geometry.vertices.data();
            const char *indexData = geometry.indices.data();
            for (const vke_render::MeshInfo &info : mesh.infos)
            {
                if (info.indexCnt < 3)
                    continue;

                const uint64_t vertexStride = info.getVertexUnitSize();
                const uint64_t indexStride = info.getIndexUnitSize();
                const char *primitiveVertices = vertexData + info.vertexOffset;
                const char *primitiveIndices = indexData + info.indexOffset;
                for (uint64_t indexOffset = 0; indexOffset + 2 < info.indexCnt; indexOffset += 3)
                {
                    for (const uint64_t corner : corners)
                    {
                        uint32_t index = 0;
                        std::memcpy(&index, primitiveIndices + (indexOffset + corner) * indexStride, indexStride);
                        glm::vec3 position;
                        std::memcpy(&position, primitiveVertices + index * vertexStride, sizeof(position));
                        triangles.push_back(glm::vec3(model * glm::vec4(position, 1.0f)));
                    }
                }
            }
        }

        void appendColliderTriangles(const JPH::TransformedShape &shape, std::vector<glm::vec3> &triangles)
        {
            JPH::Shape::GetTrianglesContext context;
            shape.GetTrianglesStart(context, JPH::AABox::sBiggest(), JPH::RVec3::sZero());
            constexpr int batchSize = 1024;
            JPH::Float3 vertices[batchSize * 3];
            int triangleCount = 0;
            while ((triangleCount = shape.GetTrianglesNext(context, batchSize, vertices, nullptr)) > 0)
                for (int vertex = 0; vertex < triangleCount * 3; ++vertex)
                    triangles.emplace_back(vertices[vertex].x, vertices[vertex].y, vertices[vertex].z);
        }

        vke_render::AABB toAABB(const JPH::AABox &bounds)
        {
            return {{bounds.mMin.GetX(), bounds.mMin.GetY(), bounds.mMin.GetZ()},
                    {bounds.mMax.GetX(), bounds.mMax.GetY(), bounds.mMax.GetZ()}};
        }
    }

    Result<std::vector<glm::vec3>> CollectNavMeshGeometry(
        const vke_common::SceneManager &scene, entt::entity root, const vke_render::AABB &bounds)
    {
        const entt::registry &registry = scene.registry;
        if (!registry.valid(root) || !registry.all_of<vke_common::GameObject, vke_common::Transform>(root))
            return std::unexpected("navmesh region requires GameObject and Transform");

        std::vector<entt::entity> entities{root};
        scene.transformSystem.CollectEntitySubtree(entities);

        std::vector<glm::vec3> triangles;
        std::unordered_map<const vke_render::Mesh *, MeshGeometry> meshGeometry;

        for (const entt::entity entity : entities)
        {
            if (!registry.valid(entity) || scene.IsPendingDestroy(entity))
                continue;

            const vke_common::GameObject *object = registry.try_get<vke_common::GameObject>(entity);
            const vke_common::Transform *transform = registry.try_get<vke_common::Transform>(entity);
            if (object == nullptr || transform == nullptr || !object->isStatic)
                continue;

            const vke_component::RenderableObject *renderable =
                registry.try_get<vke_component::RenderableObject>(entity);
            const vke_render::Mesh *mesh =
                renderable != nullptr && renderable->renderUnit != nullptr
                    ? renderable->renderUnit->mesh.get()
                    : nullptr;
            if (mesh == nullptr || mesh->infos.empty())
            {
                const vke_component::SkeletonAnimator *animator =
                    registry.try_get<vke_component::SkeletonAnimator>(entity);
                mesh = animator != nullptr && animator->renderUnit != nullptr
                           ? animator->renderUnit->mesh.get()
                           : nullptr;
            }
            if (mesh != nullptr && !mesh->infos.empty())
            {
                if (!mesh->bounds.TransformBounds(transform->model).Intersects(bounds))
                    continue;

                MeshGeometry &geometry = meshGeometry[mesh];
                if (geometry.vertices.empty())
                    mesh->ReadData(geometry.vertices, geometry.indices);

                const size_t triangleStart = triangles.size();
                appendMeshTriangles(*mesh, geometry, transform->model, triangles);
                if (triangles.size() != triangleStart)
                    continue;
            }

            const vke_component::RigidBody *rigidBody = registry.try_get<vke_component::RigidBody>(entity);
            if (rigidBody == nullptr || rigidBody->bodyID.IsInvalid())
                continue;
            JPH::BodyInterface &bodyInterface = vke_physics::PhysicsManager::GetBodyInterface();
            if (!bodyInterface.IsAdded(rigidBody->bodyID))
                continue;

            const JPH::TransformedShape shape = bodyInterface.GetTransformedShape(rigidBody->bodyID);
            if (!toAABB(shape.GetWorldSpaceBounds()).Intersects(bounds))
                continue;
            appendColliderTriangles(shape, triangles);
        }

        if (triangles.empty())
            return std::unexpected("navmesh region contains no bakeable geometry");
        return triangles;
    }

    Result<NavMeshBakeResult> BakeNavMeshRegion(entt::entity regionEntity, vke_common::AssetHandle agentConfigHandle)
    {
        vke_common::SceneManager *scene = vke_common::SceneManager::GetInstance();
        const vke_component::NavMeshRegion *region =
            scene->registry.try_get<vke_component::NavMeshRegion>(regionEntity);
        if (region == nullptr)
            return std::unexpected("entity has no NavMeshRegion component");
        const vke_common::Transform *transform = scene->registry.try_get<vke_common::Transform>(regionEntity);
        if (transform == nullptr)
            return std::unexpected("navmesh region requires Transform");

        auto agentConfig = vke_common::AssetManager::LoadNavAgentConfig(agentConfigHandle);
        if (!agentConfig)
            return std::unexpected("agent config: " + agentConfig.error());

        const vke_render::AABB localBounds{region->center - region->halfExtent,
                                           region->center + region->halfExtent};
        const vke_render::AABB worldBounds = localBounds.TransformBounds(transform->model);

        NavMeshBakeResult result;
        result.navMeshId = region->navMeshId;
        result.agentConfig = agentConfigHandle;
        result.settings.boundsMin = worldBounds.min;
        result.settings.boundsMax = worldBounds.max;

        const float width = result.settings.cellSize * result.settings.tileSize;
        const float border = (std::ceil((*agentConfig)->radius / result.settings.cellSize) + 3) * result.settings.cellSize;
        auto sampleBounds = worldBounds;
        for (int axis : {0, 2})
        {
            sampleBounds.min[axis] = std::floor(worldBounds.min[axis] / width) * width - border;
            sampleBounds.max[axis] = std::ceil(worldBounds.max[axis] / width) * width + border;
        }
        auto triangles = CollectNavMeshGeometry(*scene, regionEntity, sampleBounds);
        if (!triangles)
            return std::unexpected(triangles.error());

        auto tiles = BuildNavMeshTiles(*triangles, result.settings, **agentConfig);
        if (!tiles)
            return std::unexpected(tiles.error());
        result.tiles = std::move(*tiles);
        return result;
    }
}
