#include <navigation/navmesh_manager.hpp>
#include <DetourAlloc.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cstring>
#include <array>

namespace vke_navmesh
{
    using vke_common::Result;
    NavMeshManager *NavMeshManager::instance = nullptr;

    namespace
    {
        constexpr int MaxPath = 4096;
        bool finite(const glm::vec3 &v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
        Result<dtPolyRef> nearest(dtNavMeshQuery &query, const glm::vec3 &p, const glm::vec3 &extent, glm::vec3 &projected)
        {
            dtPolyRef ref = 0;
            dtQueryFilter filter;
            if (dtStatusFailed(query.findNearestPoly(glm::value_ptr(p), glm::value_ptr(extent), &filter, &ref, glm::value_ptr(projected))) || !ref)
                return std::unexpected("no navigable polygon within query extent");
            return ref;
        }
    }

    Result<void> NavMeshManager::Register(const NavMeshTilesData &data)
    {
        const auto id = data.navMeshId;
        const auto resource = data.handle;
        const auto agent = data.agentConfig;
        const auto &settings = data.settings;
        auto [it, created] = meshes.try_emplace(id);
        auto &entry = it->second;
        auto fail = [&](const char *message) -> Result<void>
        { if (created) meshes.erase(it); return std::unexpected(message); };
        if (auto found = entry.resources.find(resource); found != entry.resources.end())
        {
            ++found->second.users;
            return {};
        }
        if (created)
        {
            entry.agent = agent;
            entry.settings = settings;
            entry.mesh.reset(dtAllocNavMesh());
            entry.query.reset(dtAllocNavMeshQuery());
            dtNavMeshParams params{};
            params.tileWidth = params.tileHeight = settings.cellSize * settings.tileSize;
            // 32-bit Detour refs leave ten salt bits with these capacities.
            params.maxTiles = 4096;
            params.maxPolys = 1024;
            if (!entry.mesh || !entry.query || dtStatusFailed(entry.mesh->init(&params)) ||
                dtStatusFailed(entry.query->init(entry.mesh.get(), 16384)))
                return fail("cannot initialize navigation mesh");
        }
        else
        {
            auto expected = entry.settings;
            expected.boundsMin.x = settings.boundsMin.x;
            expected.boundsMin.z = settings.boundsMin.z;
            expected.boundsMax.x = settings.boundsMax.x;
            expected.boundsMax.z = settings.boundsMax.z;
            if (entry.agent != agent || expected != settings)
                return fail("navmesh resources require matching agent, build settings and vertical bounds");
        }
        Registration registration;
        auto rollback = [&]()
        { for (auto ref : registration.tiles) entry.mesh->removeTile(ref, nullptr, nullptr); };
        for (const auto &tile : data.tiles)
        {
            if (entry.mesh->getTileAt(tile.x, tile.y, tile.layer))
            {
                rollback();
                return fail("navmesh tile coordinate is already registered");
            }
            const auto *header = reinterpret_cast<const dtMeshHeader *>(tile.data.data());
            if (header->polyCount > 1024)
            {
                rollback();
                return fail("Detour tile exceeds 1024 polygons");
            }
            auto *bytes = static_cast<unsigned char *>(dtAlloc(tile.data.size(), DT_ALLOC_PERM));
            if (!bytes)
            {
                rollback();
                return fail("cannot allocate navigation tile");
            }
            std::memcpy(bytes, tile.data.data(), tile.data.size());
            dtTileRef ref = 0;
            if (dtStatusFailed(entry.mesh->addTile(bytes, static_cast<int>(tile.data.size()), DT_TILE_FREE_DATA, 0, &ref)))
            {
                dtFree(bytes);
                rollback();
                return fail("cannot register navigation tile");
            }
            registration.tiles.push_back(ref);
        }
        entry.resources.emplace(resource, std::move(registration));
        return {};
    }

    void NavMeshManager::Unregister(uint32_t id, vke_common::AssetHandle resource)
    {
        auto it = meshes.find(id);
        if (it == meshes.end())
            return;
        auto found = it->second.resources.find(resource);
        if (found == it->second.resources.end() || --found->second.users != 0)
            return;
        for (auto ref : found->second.tiles)
            it->second.mesh->removeTile(ref, nullptr, nullptr);
        it->second.resources.erase(found);
        if (it->second.resources.empty())
            meshes.erase(it);
    }

    bool NavMeshManager::MatchesAgent(uint32_t id, vke_common::AssetHandle agent) const
    {
        auto it = meshes.find(id);
        return it != meshes.end() && it->second.agent == agent;
    }

    Result<dtNavMeshQuery *> NavMeshManager::getQuery(uint32_t id, const glm::vec3 &position, const glm::vec3 &extent)
    {
        if (!finite(position) || !finite(extent) || extent.x <= 0 || extent.y <= 0 || extent.z <= 0)
            return std::unexpected("query requires finite coordinates and positive extents");
        auto it = meshes.find(id);
        if (it == meshes.end())
            return std::unexpected("navigation mesh is unavailable");
        return it->second.query.get();
    }

    Result<glm::vec3> NavMeshManager::SamplePosition(uint32_t id, const glm::vec3 &position, const glm::vec3 &extent)
    {
        auto query = getQuery(id, position, extent);
        if (!query)
            return std::unexpected(query.error());
        glm::vec3 result;
        auto ref = nearest(**query, position, extent, result);
        if (!ref)
            return std::unexpected(ref.error());
        return result;
    }

    Result<NavPath> NavMeshManager::FindPath(uint32_t id, const glm::vec3 &start, const glm::vec3 &end, const glm::vec3 &extent)
    {
        auto query = getQuery(id, start, extent);
        if (!query)
            return std::unexpected(query.error());
        if (!finite(end))
            return std::unexpected("non-finite destination");
        glm::vec3 from, to;
        auto a = nearest(**query, start, extent, from), b = nearest(**query, end, extent, to);
        if (!a)
            return std::unexpected(a.error());
        if (!b)
            return std::unexpected(b.error());
        dtQueryFilter filter;
        std::array<dtPolyRef, MaxPath> corridor;
        int count = 0;
        auto status = (*query)->findPath(*a, *b, glm::value_ptr(from), glm::value_ptr(to), &filter, corridor.data(), &count, MaxPath);
        if (dtStatusFailed(status) || !count)
            return std::unexpected("path search failed");
        bool partial = corridor[count - 1] != *b || dtStatusDetail(status, DT_PARTIAL_RESULT | DT_BUFFER_TOO_SMALL | DT_OUT_OF_NODES);
        if (partial && dtStatusFailed((*query)->closestPointOnPoly(corridor[count - 1], glm::value_ptr(to), glm::value_ptr(to), nullptr)))
            return std::unexpected("cannot project partial path endpoint");
        std::array<float, MaxPath * 3> points;
        int pointCount = 0;
        status = (*query)->findStraightPath(glm::value_ptr(from), glm::value_ptr(to), corridor.data(), count, points.data(), nullptr, nullptr, &pointCount, MaxPath);
        if (dtStatusFailed(status))
            return std::unexpected("cannot extract path points");
        partial |= dtStatusDetail(status, DT_BUFFER_TOO_SMALL);
        NavPath result;
        result.status = partial ? PathStatus::Partial : PathStatus::Complete;
        for (int i = 0; i < pointCount; ++i)
            result.points.emplace_back(points[i * 3], points[i * 3 + 1], points[i * 3 + 2]);
        return result;
    }

    Result<NavRaycast> NavMeshManager::Raycast(uint32_t id, const glm::vec3 &start, const glm::vec3 &end, const glm::vec3 &extent)
    {
        auto query = getQuery(id, start, extent);
        if (!query)
            return std::unexpected(query.error());
        if (!finite(end))
            return std::unexpected("non-finite destination");
        glm::vec3 from;
        auto ref = nearest(**query, start, extent, from);
        if (!ref)
            return std::unexpected(ref.error());
        dtQueryFilter filter;
        float fraction;
        NavRaycast result;
        auto status = (*query)->raycast(*ref, glm::value_ptr(from), glm::value_ptr(end), &filter, &fraction, glm::value_ptr(result.normal), nullptr, nullptr, 0);
        if (dtStatusFailed(status))
            return std::unexpected("navigation raycast failed");
        result.blocked = fraction < 1.0f;
        result.position = from + (end - from) * std::min(fraction, 1.0f);
        return result;
    }
}
