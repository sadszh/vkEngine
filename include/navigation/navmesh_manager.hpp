#pragma once
#include <navigation/navmesh.hpp>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <memory>
#include <unordered_map>

namespace vke_navmesh
{
    enum class PathStatus : int32_t
    {
        Failed,
        Complete,
        Partial
    };
    struct NavPath
    {
        PathStatus status = PathStatus::Failed;
        std::vector<glm::vec3> points;
    };
    struct NavRaycast
    {
        bool blocked = false;
        glm::vec3 position{0};
        glm::vec3 normal{0};
    };

    class NavMeshManager
    {
    public:
        static NavMeshManager *GetInstance() { return instance; }

        static NavMeshManager *Init()
        {
            instance = new NavMeshManager();
            return instance;
        }

        static void Dispose()
        {
            delete instance;
            instance = nullptr;
        }

        vke_common::Result<void> Register(const NavMeshTilesData &data);
        void Unregister(uint32_t id, vke_common::AssetHandle resource);
        void Clear() { meshes.clear(); }
        bool MatchesAgent(uint32_t id, vke_common::AssetHandle agent) const;
        vke_common::Result<glm::vec3> SamplePosition(uint32_t id, const glm::vec3 &position, const glm::vec3 &extent);
        vke_common::Result<NavPath> FindPath(uint32_t id, const glm::vec3 &start, const glm::vec3 &end, const glm::vec3 &extent);
        vke_common::Result<NavRaycast> Raycast(uint32_t id, const glm::vec3 &start, const glm::vec3 &end, const glm::vec3 &extent);

    private:
        static NavMeshManager *instance;
        NavMeshManager() = default;
        ~NavMeshManager() = default;
        NavMeshManager(const NavMeshManager &) = delete;
        NavMeshManager &operator=(const NavMeshManager &) = delete;

        struct Registration
        {
            size_t users = 1;
            std::vector<dtTileRef> tiles;
        };
        struct Mesh
        {
            std::unique_ptr<dtNavMesh, decltype(&dtFreeNavMesh)> mesh{nullptr, dtFreeNavMesh};
            std::unique_ptr<dtNavMeshQuery, decltype(&dtFreeNavMeshQuery)> query{nullptr, dtFreeNavMeshQuery};
            vke_common::AssetHandle agent = 0;
            NavMeshBuildSettings settings;
            std::unordered_map<vke_common::AssetHandle, Registration> resources;
        };
        std::unordered_map<uint32_t, Mesh> meshes;
        vke_common::Result<dtNavMeshQuery *> getQuery(uint32_t id, const glm::vec3 &position, const glm::vec3 &extent);
    };
}
