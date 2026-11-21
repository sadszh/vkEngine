#pragma once
#include <asset/asset_manager.hpp>
#include <asset/asset_ref.hpp>
#include <navigation/navmesh_manager.hpp>
#include <logger.hpp>

namespace vke_component
{
    struct NavMeshTileData
    {
        vke_common::AssetRef<vke_navmesh::NavMeshTilesData> tiles;
        NavMeshTileData() = default;
        NavMeshTileData(const nlohmann::json &json) { tiles.SetHandle(json["tiles"]); }
        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            return Object(json).Require({"tiles"}).Unsigneds({"tiles"}).Result();
        }
        vke_common::Result<void> ValidateAssets() const
        {
            if (!tiles.Get())
                return vke_common::AssetManager::ValidateNavMeshTiles(tiles.Handle());
            return {};
        }
        void ConstructAssetLoadBatch(vke_common::LoadBatch &batch) const
        {
            if (!tiles.Get())
                batch[vke_common::ASSET_NAV_MESH_TILES].insert(tiles.Handle());
        }
        vke_common::Result<void> LoadAssets()
        {
            if (!tiles.Get())
            {
                auto loaded = vke_common::AssetManager::LoadNavMeshTiles(tiles.Handle());
                if (!loaded)
                    return std::unexpected(loaded.error());
                tiles.Resolve(std::move(*loaded));
            }
            return {};
        }
        vke_common::Result<void> ResolveAssets(const vke_common::BatchResult &loaded)
        {
            if (!tiles.Get())
                tiles.Resolve(*loaded.navMeshes.at(tiles.Handle()));
            return {};
        }
        nlohmann::json ToJSON() const { return {{"type", "navMeshTile"}, {"tiles", tiles.Handle()}}; }
    };

    class NavMeshTile
    {
    public:
        explicit NavMeshTile(const NavMeshTileData &data) : tiles(data.tiles.Get()) {}
        void Start()
        {
            if (registered)
                return;
            auto result = vke_navmesh::NavMeshManager::GetInstance()->Register(*tiles);
            if (!result)
            {
                VKE_LOG_ERROR("NavMeshTile {}: {}", tiles->handle, result.error());
                return;
            }
            registered = true;
        }
        void Unload()
        {
            if (!registered)
                return;
            vke_navmesh::NavMeshManager::GetInstance()->Unregister(tiles->navMeshId, tiles->handle);
            registered = false;
        }
        void FillData(NavMeshTileData &value) const { value.tiles.SetAsset(tiles); }

    private:
        std::shared_ptr<vke_navmesh::NavMeshTilesData> tiles;
        bool registered = false;
    };
}
