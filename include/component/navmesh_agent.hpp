#pragma once
#include <asset/asset_manager.hpp>
#include <asset/asset_ref.hpp>
#include <component/transform.hpp>
#include <navigation/navmesh_manager.hpp>

namespace vke_component
{
    struct NavMeshAgentData
    {
        vke_common::AssetRef<vke_navmesh::NavAgentConfig> agentConfig;
        uint32_t navMeshId = 1;
        NavMeshAgentData() = default;
        NavMeshAgentData(const nlohmann::json &json)
        {
            agentConfig.SetHandle(json["agentConfig"]);
            navMeshId = json["navMeshId"];
        }
        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            return Object(json).Require({"agentConfig", "navMeshId"}).Unsigneds({"agentConfig"}).Unsigneds({"navMeshId"}, UINT32_MAX).Result();
        }
        vke_common::Result<void> ValidateAssets() const
        {
            if (!agentConfig.Get())
                return vke_common::AssetManager::ValidateNavAgentConfig(agentConfig.Handle());
            return {};
        }
        void ConstructAssetLoadBatch(vke_common::LoadBatch &batch) const
        {
            if (!agentConfig.Get())
                batch[vke_common::ASSET_NAV_AGENT].insert(agentConfig.Handle());
        }
        vke_common::Result<void> LoadAssets()
        {
            if (!agentConfig.Get())
            {
                auto loaded = vke_common::AssetManager::LoadNavAgentConfig(agentConfig.Handle());
                if (!loaded)
                    return std::unexpected(loaded.error());
                agentConfig.Resolve(std::move(*loaded));
            }
            return {};
        }
        vke_common::Result<void> ResolveAssets(const vke_common::BatchResult &loaded)
        {
            if (!agentConfig.Get())
                agentConfig.Resolve(*loaded.navAgentConfigs.at(agentConfig.Handle()));
            return {};
        }
        nlohmann::json ToJSON() const { return {{"type", "navMeshAgent"}, {"agentConfig", agentConfig.Handle()}, {"navMeshId", navMeshId}}; }
    };

    class NavMeshAgent
    {
    public:
        NavMeshAgent(vke_common::Transform &transform, const NavMeshAgentData &data)
            : agentConfig(data.agentConfig.Get()), navMeshId(data.navMeshId), position(transform.GetGlobalPosition()) {}
        void OnTransformed(const vke_common::Transform &transform) { position = transform.GetGlobalPosition(); }
        void Unload() {}
        void FillData(NavMeshAgentData &value) const
        {
            value.agentConfig.SetAsset(agentConfig);
            value.navMeshId = navMeshId;
        }
        vke_common::Result<vke_navmesh::NavPath> FindPath(const glm::vec3 &destination, const glm::vec3 &extent)
        {
            auto &manager = *vke_navmesh::NavMeshManager::GetInstance();
            if (!manager.MatchesAgent(navMeshId, agentConfig->handle))
                return std::unexpected("navigation mesh unavailable or agent config mismatch");
            return manager.FindPath(navMeshId, position, destination, extent);
        }

    private:
        std::shared_ptr<vke_navmesh::NavAgentConfig> agentConfig;
        uint32_t navMeshId;
        glm::vec3 position;
    };
}
