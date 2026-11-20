#ifndef NAVMESH_BAKE_H
#define NAVMESH_BAKE_H

#include <navigation/navmesh.hpp>
#include <common.hpp>
#include <render/render_common.hpp>
#include <entt/entity/entity.hpp>
#include <glm/vec3.hpp>
#include <vector>

namespace vke_common
{
    class SceneManager;
}

namespace vke_navmesh
{
    struct NavMeshBakeResult
    {
        vke_common::AssetHandle agentConfig = 0;
        NavMeshBuildSettings settings;
        std::vector<NavMeshTile> tiles;
    };

    vke_common::Result<std::vector<glm::vec3>> CollectNavMeshGeometry(
        const vke_common::SceneManager &scene, entt::entity root, const vke_render::AABB &bounds);

    vke_common::Result<NavMeshBakeResult> BakeNavMeshRegion(entt::entity regionEntity, vke_common::AssetHandle agentConfigHandle);
}

#endif
