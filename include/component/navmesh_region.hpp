#ifndef NAVMESH_REGION_H
#define NAVMESH_REGION_H

#include <json_validation.hpp>
#include <common.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

namespace vke_component
{
    struct NavMeshRegionData
    {
        glm::vec3 center{0.0f};
        glm::vec3 halfExtent{16.0f};

        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            auto result = Object(json).Require({"center", "halfExtent"}).Vectors({"center", "halfExtent"}, 3).Result();
            if (!result)
                return result;
            for (const auto &extent : json["halfExtent"])
                if (extent.get<float>() < 0.0f)
                    return std::unexpected("navmesh region half extent must be nonnegative");
            return {};
        }

        NavMeshRegionData() = default;
        NavMeshRegionData(const nlohmann::json &json)
            : center(json["center"][0].get<float>(), json["center"][1].get<float>(), json["center"][2].get<float>()),
              halfExtent(json["halfExtent"][0].get<float>(), json["halfExtent"][1].get<float>(), json["halfExtent"][2].get<float>()) {}

        nlohmann::json ToJSON() const
        {
            return {{"type", "navMeshRegion"},
                    {"center", {center.x, center.y, center.z}},
                    {"halfExtent", {halfExtent.x, halfExtent.y, halfExtent.z}}};
        }
    };

    class NavMeshRegion
    {
    public:
        glm::vec3 center;
        glm::vec3 halfExtent;

        NavMeshRegion(const NavMeshRegionData &componentData)
            : center(componentData.center), halfExtent(componentData.halfExtent) {}

        void FillData(NavMeshRegionData &data) const
        {
            data.center = center;
            data.halfExtent = halfExtent;
        }

        void Unload() {}
    };
}

#endif
