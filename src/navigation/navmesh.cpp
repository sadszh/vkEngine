#include <navigation/navmesh.hpp>
#include <binary_io.hpp>
#include <logger.hpp>
#include <Recast.h>
#include <DetourNavMeshBuilder.h>
#include <algorithm>
#include <fstream>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <system_error>

namespace vke_navmesh
{
    using vke_common::PathToUTF8;
    using vke_common::ReadBytes;
    using vke_common::Result;
    using vke_common::WriteBytes;

    namespace
    {
        class BuildContext final : public rcContext
        {
        protected:
            void doLog(const rcLogCategory category, const char *msg, const int len) override
            {
                const std::string message(msg, static_cast<size_t>(len));
                if (category == RC_LOG_ERROR)
                    VKE_LOG_ERROR("[recast] {}", message)
                else if (category == RC_LOG_WARNING)
                    VKE_LOG_WARN("[recast] {}", message)
            }
        };

        template <typename T, void (*Free)(T *)>
        using RecastPtr = std::unique_ptr<T, decltype(Free)>;
    }

    Result<std::vector<NavMeshTile>> BuildNavMeshTiles(
        const std::vector<glm::vec3> &triangles,
        const NavMeshBuildSettings &settings,
        const NavAgentConfig &agent)
    {
        if (auto valid = settings.Validate(); !valid)
            return std::unexpected(valid.error());
        if (auto valid = agent.Validate(); !valid)
            return std::unexpected(valid.error());
        if (triangles.empty() || triangles.size() % 3 != 0)
            return std::unexpected("navmesh geometry must contain whole triangles");
        if (triangles.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
            return std::unexpected("navmesh geometry exceeds the supported vertex count");

        const float cs = settings.cellSize;
        const float ch = settings.cellHeight;
        const float radiusInCells = std::ceil(agent.radius / cs);
        if (!(radiusInCells <= 127.0f))
            return std::unexpected("agent radius must span at most 127 cells; increase the cell size");
        const int walkableRadius = static_cast<int>(radiusInCells);
        const int walkableHeight = static_cast<int>(std::ceil(agent.height / ch));
        const int walkableClimb = static_cast<int>(std::floor(agent.maxClimb / ch));
        if (walkableHeight < 3)
            return std::unexpected("agent height must span at least three cell heights");

        float bmin[3] = {settings.boundsMin.x, settings.boundsMin.y, settings.boundsMin.z};
        float bmax[3] = {settings.boundsMax.x, settings.boundsMax.y, settings.boundsMax.z};
        int gridWidth = 0;
        int gridHeight = 0;
        rcCalcGridSize(bmin, bmax, cs, &gridWidth, &gridHeight);
        if (gridWidth <= 0 || gridHeight <= 0)
            return std::unexpected("navmesh build bounds are smaller than one cell");
        constexpr int64_t MaxGridCells = 1 << 24;
        if (static_cast<int64_t>(gridWidth) * gridHeight > MaxGridCells)
            return std::unexpected("navmesh build area exceeds the supported grid size; increase the cell size");

        const int tileSize = settings.tileSize;
        const int tileCountX = (gridWidth + tileSize - 1) / tileSize;
        const int tileCountY = (gridHeight + tileSize - 1) / tileSize;
        const int borderSize = walkableRadius + 3;
        const int maxEdgeLen = static_cast<int>(settings.maxEdgeLength / cs);
        const float maxSimplificationError = settings.maxSimplificationError / cs;
        const int minRegionArea = static_cast<int>(settings.minRegionArea / (cs * cs));
        const int mergeRegionArea = static_cast<int>(settings.mergeRegionArea / (cs * cs));
        const float detailSampleDist = settings.detailSampleDistance < cs ? 0.0f : settings.detailSampleDistance;

        std::vector<float> vertices;
        vertices.reserve(triangles.size() * 3);
        for (const glm::vec3 &vertex : triangles)
        {
            vertices.push_back(vertex.x);
            vertices.push_back(vertex.y);
            vertices.push_back(vertex.z);
        }

        const int triangleCount = static_cast<int>(triangles.size() / 3);
        std::vector<int> indices(triangles.size());
        std::iota(indices.begin(), indices.end(), 0);
        std::vector<unsigned char> areas(triangleCount, RC_NULL_AREA);

        BuildContext context;
        rcMarkWalkableTriangles(&context, agent.maxSlopeAngle, vertices.data(),
                                static_cast<int>(triangles.size()), indices.data(), triangleCount, areas.data());

        std::vector<NavMeshTile> tiles;
        for (int tileY = 0; tileY < tileCountY; ++tileY)
        {
            for (int tileX = 0; tileX < tileCountX; ++tileX)
            {
                const int tileWidth = std::min(tileSize, gridWidth - tileX * tileSize);
                const int tileHeight = std::min(tileSize, gridHeight - tileY * tileSize);
                float tileBmin[3] = {bmin[0] + tileX * tileSize * cs, bmin[1], bmin[2] + tileY * tileSize * cs};
                float tileBmax[3] = {tileBmin[0] + tileWidth * cs, bmax[1], tileBmin[2] + tileHeight * cs};
                float hfBmin[3] = {tileBmin[0] - borderSize * cs, tileBmin[1], tileBmin[2] - borderSize * cs};
                float hfBmax[3] = {tileBmax[0] + borderSize * cs, tileBmax[1], tileBmax[2] + borderSize * cs};
                const int hfWidth = tileWidth + 2 * borderSize;
                const int hfHeight = tileHeight + 2 * borderSize;

                RecastPtr<rcHeightfield, rcFreeHeightField> heightfield(rcAllocHeightfield(), rcFreeHeightField);
                if (!heightfield || !rcCreateHeightfield(&context, *heightfield, hfWidth, hfHeight,
                                                         hfBmin, hfBmax, cs, ch))
                    return std::unexpected("navmesh tile " + std::to_string(tileX) + "," + std::to_string(tileY) +
                                           ": cannot create heightfield");

                if (!rcRasterizeTriangles(&context, vertices.data(), static_cast<int>(triangles.size()),
                                          indices.data(), areas.data(), triangleCount, *heightfield, walkableClimb))
                    return std::unexpected("navmesh tile " + std::to_string(tileX) + "," + std::to_string(tileY) +
                                           ": cannot rasterize geometry");

                rcFilterLowHangingWalkableObstacles(&context, walkableClimb, *heightfield);
                rcFilterLedgeSpans(&context, walkableHeight, walkableClimb, *heightfield);
                rcFilterWalkableLowHeightSpans(&context, walkableHeight, *heightfield);

                RecastPtr<rcCompactHeightfield, rcFreeCompactHeightfield> compactHeightfield(
                    rcAllocCompactHeightfield(), rcFreeCompactHeightfield);
                if (!compactHeightfield || !rcBuildCompactHeightfield(&context, walkableHeight, walkableClimb,
                                                                      *heightfield, *compactHeightfield))
                    return std::unexpected("navmesh tile " + std::to_string(tileX) + "," + std::to_string(tileY) +
                                           ": cannot build compact heightfield");

                if (!rcErodeWalkableArea(&context, walkableRadius, *compactHeightfield) ||
                    !rcBuildDistanceField(&context, *compactHeightfield) ||
                    !rcBuildRegions(&context, *compactHeightfield, borderSize, minRegionArea, mergeRegionArea))
                    return std::unexpected("navmesh tile " + std::to_string(tileX) + "," + std::to_string(tileY) +
                                           ": cannot build regions");

                RecastPtr<rcContourSet, rcFreeContourSet> contourSet(rcAllocContourSet(), rcFreeContourSet);
                if (!contourSet || !rcBuildContours(&context, *compactHeightfield, maxSimplificationError,
                                                    maxEdgeLen, *contourSet))
                    return std::unexpected("navmesh tile " + std::to_string(tileX) + "," + std::to_string(tileY) +
                                           ": cannot build contours");
                if (contourSet->nconts == 0)
                    continue;

                RecastPtr<rcPolyMesh, rcFreePolyMesh> polyMesh(rcAllocPolyMesh(), rcFreePolyMesh);
                if (!polyMesh || !rcBuildPolyMesh(&context, *contourSet, settings.maxVertsPerPoly, *polyMesh))
                    return std::unexpected("navmesh tile " + std::to_string(tileX) + "," + std::to_string(tileY) +
                                           ": cannot build polygon mesh");
                if (polyMesh->npolys == 0)
                    continue;

                for (int i = 0; i < polyMesh->npolys; ++i)
                {
                    if (polyMesh->areas[i] == RC_WALKABLE_AREA)
                        polyMesh->areas[i] = 0;
                    polyMesh->flags[i] = 1;
                }

                RecastPtr<rcPolyMeshDetail, rcFreePolyMeshDetail> detailMesh(
                    rcAllocPolyMeshDetail(), rcFreePolyMeshDetail);
                if (!detailMesh || !rcBuildPolyMeshDetail(&context, *polyMesh, *compactHeightfield,
                                                          detailSampleDist, settings.detailSampleMaxError,
                                                          *detailMesh))
                    return std::unexpected("navmesh tile " + std::to_string(tileX) + "," + std::to_string(tileY) +
                                           ": cannot build detail mesh");

                dtNavMeshCreateParams params{};
                params.verts = polyMesh->verts;
                params.vertCount = polyMesh->nverts;
                params.polys = polyMesh->polys;
                params.polyAreas = polyMesh->areas;
                params.polyFlags = polyMesh->flags;
                params.polyCount = polyMesh->npolys;
                params.nvp = polyMesh->nvp;
                params.detailMeshes = detailMesh->meshes;
                params.detailVerts = detailMesh->verts;
                params.detailVertsCount = detailMesh->nverts;
                params.detailTris = detailMesh->tris;
                params.detailTriCount = detailMesh->ntris;
                params.walkableHeight = agent.height;
                params.walkableRadius = agent.radius;
                params.walkableClimb = agent.maxClimb;
                params.tileX = tileX;
                params.tileY = tileY;
                params.tileLayer = 0;
                rcVcopy(params.bmin, polyMesh->bmin);
                rcVcopy(params.bmax, polyMesh->bmax);
                params.cs = cs;
                params.ch = ch;
                params.buildBvTree = true;

                unsigned char *navData = nullptr;
                int navDataSize = 0;
                if (!dtCreateNavMeshData(&params, &navData, &navDataSize))
                    return std::unexpected("navmesh tile " + std::to_string(tileX) + "," + std::to_string(tileY) +
                                           ": cannot create Detour tile data");

                NavMeshTile tile;
                tile.x = tileX;
                tile.y = tileY;
                tile.layer = 0;
                tile.data.assign(navData, navData + navDataSize);
                dtFree(navData);
                tiles.push_back(std::move(tile));

                VKE_LOG_DEBUG("Built navmesh tile {},{},{},{} bytes", tileX, tileY, 0, navDataSize)
            }
        }

        if (tiles.empty())
            return std::unexpected("navmesh region contains no walkable surface");
        return tiles;
    }

    Result<void> WriteNavMeshFile(const std::filesystem::path &path, const std::vector<NavMeshTile> &tiles)
    {
        if (tiles.empty())
            return std::unexpected(PathToUTF8(path) + ": navmesh contains no tiles");
        if (tiles.size() > MaxNavMeshTiles)
            return std::unexpected(PathToUTF8(path) + ": too many navmesh tiles");
        for (const NavMeshTile &tile : tiles)
            if (tile.data.empty() || tile.data.size() > MaxNavMeshTileBytes)
                return std::unexpected(PathToUTF8(path) + ": invalid navmesh tile size");

        auto temporary = path;
        temporary += ".tmp";
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out)
            return std::unexpected(PathToUTF8(temporary) + ": cannot create temporary navmesh file");

        const uint32_t magic = NavMeshFileMagic;
        const uint32_t version = NavMeshFileVersion;
        const uint32_t tileCount = static_cast<uint32_t>(tiles.size());
        bool written = WriteBytes(out, &magic, sizeof(magic)) &&
                       WriteBytes(out, &version, sizeof(version)) &&
                       WriteBytes(out, &tileCount, sizeof(tileCount));
        for (const NavMeshTile &tile : tiles)
        {
            const int32_t x = tile.x;
            const int32_t y = tile.y;
            const int32_t layer = tile.layer;
            const uint32_t dataSize = static_cast<uint32_t>(tile.data.size());
            if (!written ||
                !WriteBytes(out, &x, sizeof(x)) || !WriteBytes(out, &y, sizeof(y)) ||
                !WriteBytes(out, &layer, sizeof(layer)) || !WriteBytes(out, &dataSize, sizeof(dataSize)) ||
                !WriteBytes(out, tile.data.data(), tile.data.size()))
            {
                written = false;
                break;
            }
        }
        out.close();
        if (!written || out.fail())
        {
            std::error_code removeError;
            std::filesystem::remove(temporary, removeError);
            return std::unexpected(PathToUTF8(temporary) + ": failed to write navmesh file");
        }

        std::error_code renameError;
        std::filesystem::rename(temporary, path, renameError);
        if (renameError)
        {
            std::error_code removeError;
            std::filesystem::remove(temporary, removeError);
            return std::unexpected(PathToUTF8(path) + ": replace navmesh file: " + renameError.message());
        }
        return {};
    }

    Result<std::vector<NavMeshTile>> ReadNavMeshFile(const std::filesystem::path &path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return std::unexpected(PathToUTF8(path) + ": cannot open navmesh file");

        uint32_t magic = 0;
        uint32_t version = 0;
        uint32_t tileCount = 0;
        if (!ReadBytes(in, &magic, sizeof(magic)) || !ReadBytes(in, &version, sizeof(version)) ||
            !ReadBytes(in, &tileCount, sizeof(tileCount)))
            return std::unexpected(PathToUTF8(path) + ": truncated navmesh header");
        if (magic != NavMeshFileMagic)
            return std::unexpected(PathToUTF8(path) + ": not a navmesh file");
        if (version != NavMeshFileVersion)
            return std::unexpected(PathToUTF8(path) + ": unsupported navmesh version " + std::to_string(version));
        if (tileCount == 0 || tileCount > MaxNavMeshTiles)
            return std::unexpected(PathToUTF8(path) + ": invalid navmesh tile count");

        std::vector<NavMeshTile> tiles;
        tiles.reserve(tileCount);
        for (uint32_t i = 0; i < tileCount; ++i)
        {
            int32_t x = 0;
            int32_t y = 0;
            int32_t layer = 0;
            uint32_t dataSize = 0;
            if (!ReadBytes(in, &x, sizeof(x)) || !ReadBytes(in, &y, sizeof(y)) ||
                !ReadBytes(in, &layer, sizeof(layer)) || !ReadBytes(in, &dataSize, sizeof(dataSize)))
                return std::unexpected(PathToUTF8(path) + ": truncated navmesh tile header");
            if (x < 0 || y < 0 || layer < 0 || dataSize == 0 || dataSize > MaxNavMeshTileBytes)
                return std::unexpected(PathToUTF8(path) + ": invalid navmesh tile");

            NavMeshTile tile;
            tile.x = x;
            tile.y = y;
            tile.layer = layer;
            tile.data.resize(dataSize);
            if (!ReadBytes(in, tile.data.data(), tile.data.size()))
                return std::unexpected(PathToUTF8(path) + ": truncated navmesh tile data");
            tiles.push_back(std::move(tile));
        }
        if (in.peek() != std::char_traits<char>::eof())
            return std::unexpected(PathToUTF8(path) + ": unexpected trailing navmesh data");
        return tiles;
    }
}
