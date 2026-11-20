#ifndef NAVMESH_H
#define NAVMESH_H

#include <common.hpp>
#include <json_validation.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace vke_navmesh
{
    inline constexpr uint32_t NavMeshFileMagic = 0x4E4D4B56; // "VKMN"
    inline constexpr uint32_t NavMeshFileVersion = 1;
    inline constexpr uint32_t MaxNavMeshTiles = 1u << 16;
    inline constexpr uint32_t MaxNavMeshTileBytes = 64u << 20;

    struct NavAgentConfig
    {
        float radius = 0.6f;
        float height = 2.0f;
        float maxSlopeAngle = 45.0f;
        float maxClimb = 0.9f;

        vke_common::Result<void> Validate() const
        {
            if (!std::isfinite(radius) || !std::isfinite(height) ||
                !std::isfinite(maxSlopeAngle) || !std::isfinite(maxClimb))
                return std::unexpected("agent parameters must be finite");
            if (radius <= 0.0f || height <= 0.0f)
                return std::unexpected("agent radius and height must be positive");
            if (maxSlopeAngle < 0.0f || maxSlopeAngle >= 90.0f)
                return std::unexpected("agent max slope angle must be in [0, 90)");
            if (maxClimb < 0.0f)
                return std::unexpected("agent max climb must be nonnegative");
            return {};
        }

        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            auto result = Object(json).Require({"radius", "height", "maxSlopeAngle", "maxClimb"}).Numbers({"radius", "height", "maxSlopeAngle", "maxClimb"}).Result();
            if (!result)
                return result;
            return NavAgentConfig(json).Validate();
        }

        NavAgentConfig() = default;
        NavAgentConfig(const nlohmann::json &json)
            : radius(json["radius"].get<float>()), height(json["height"].get<float>()),
              maxSlopeAngle(json["maxSlopeAngle"].get<float>()), maxClimb(json["maxClimb"].get<float>()) {}

        void WriteJSON(nlohmann::json &json) const
        {
            json["radius"] = radius;
            json["height"] = height;
            json["maxSlopeAngle"] = maxSlopeAngle;
            json["maxClimb"] = maxClimb;
        }
    };

    struct NavMeshBuildSettings
    {
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};
        float cellSize = 0.3f;
        float cellHeight = 0.2f;
        int tileSize = 128; // Voxels along one tile edge.
        float maxEdgeLength = 12.0f;
        float maxSimplificationError = 0.39f;
        float minRegionArea = 5.76f;
        float mergeRegionArea = 36.0f;
        float detailSampleDistance = 1.8f;
        float detailSampleMaxError = 0.2f;
        int maxVertsPerPoly = 6; // Recast supports up to 6.

        vke_common::Result<void> Validate() const
        {
            if (!(cellSize > 0.0f) || !(cellHeight > 0.0f))
                return std::unexpected("navmesh cell size and cell height must be positive");
            if (boundsMax.x <= boundsMin.x || boundsMax.y <= boundsMin.y || boundsMax.z <= boundsMin.z)
                return std::unexpected("navmesh bounds must be nonempty");
            if (tileSize <= 0)
                return std::unexpected("navmesh tile size must be positive");
            if (maxVertsPerPoly < 3 || maxVertsPerPoly > 6)
                return std::unexpected("navmesh max verts per polygon must be in [3, 6]");
            if (maxEdgeLength < 0.0f || maxSimplificationError < 0.0f ||
                minRegionArea < 0.0f || mergeRegionArea < 0.0f ||
                detailSampleDistance < 0.0f || detailSampleMaxError < 0.0f)
                return std::unexpected("navmesh build distances and areas must be nonnegative");
            return {};
        }

        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            auto result = Object(json)
                              .Require({"boundsMin", "boundsMax", "cellSize", "cellHeight", "tileSize",
                                        "maxEdgeLength", "maxSimplificationError", "minRegionArea",
                                        "mergeRegionArea", "detailSampleDistance", "detailSampleMaxError",
                                        "maxVertsPerPoly"})
                              .Vectors({"boundsMin", "boundsMax"}, 3)
                              .Numbers({"cellSize", "cellHeight", "maxEdgeLength", "maxSimplificationError",
                                        "minRegionArea", "mergeRegionArea", "detailSampleDistance",
                                        "detailSampleMaxError"})
                              .Unsigneds({"tileSize", "maxVertsPerPoly"}, INT32_MAX)
                              .Result();
            if (!result)
                return result;
            return NavMeshBuildSettings(json).Validate();
        }

        NavMeshBuildSettings() = default;
        NavMeshBuildSettings(const nlohmann::json &json)
        {
            const auto &min = json["boundsMin"];
            const auto &max = json["boundsMax"];
            boundsMin = glm::vec3(min[0].get<float>(), min[1].get<float>(), min[2].get<float>());
            boundsMax = glm::vec3(max[0].get<float>(), max[1].get<float>(), max[2].get<float>());
            cellSize = json["cellSize"].get<float>();
            cellHeight = json["cellHeight"].get<float>();
            tileSize = json["tileSize"].get<int>();
            maxEdgeLength = json["maxEdgeLength"].get<float>();
            maxSimplificationError = json["maxSimplificationError"].get<float>();
            minRegionArea = json["minRegionArea"].get<float>();
            mergeRegionArea = json["mergeRegionArea"].get<float>();
            detailSampleDistance = json["detailSampleDistance"].get<float>();
            detailSampleMaxError = json["detailSampleMaxError"].get<float>();
            maxVertsPerPoly = json["maxVertsPerPoly"].get<int>();
        }

        nlohmann::json ToJSON() const
        {
            return {{"boundsMin", {boundsMin.x, boundsMin.y, boundsMin.z}},
                    {"boundsMax", {boundsMax.x, boundsMax.y, boundsMax.z}},
                    {"cellSize", cellSize},
                    {"cellHeight", cellHeight},
                    {"tileSize", tileSize},
                    {"maxEdgeLength", maxEdgeLength},
                    {"maxSimplificationError", maxSimplificationError},
                    {"minRegionArea", minRegionArea},
                    {"mergeRegionArea", mergeRegionArea},
                    {"detailSampleDistance", detailSampleDistance},
                    {"detailSampleMaxError", detailSampleMaxError},
                    {"maxVertsPerPoly", maxVertsPerPoly}};
        }
    };

    struct NavMeshTile
    {
        int x = 0;
        int y = 0;
        int layer = 0;
        std::vector<unsigned char> data;
    };

    struct NavMeshData
    {
        std::vector<NavMeshTile> tiles;
    };

    vke_common::Result<std::vector<NavMeshTile>> BuildNavMeshTiles(
        const std::vector<glm::vec3> &triangles,
        const NavMeshBuildSettings &settings,
        const NavAgentConfig &agent);

    vke_common::Result<void> WriteNavMeshFile(const std::filesystem::path &path, const std::vector<NavMeshTile> &tiles);
    vke_common::Result<std::vector<NavMeshTile>> ReadNavMeshFile(const std::filesystem::path &path);
}

#endif
