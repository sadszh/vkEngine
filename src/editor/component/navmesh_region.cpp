#include <editor/editor.hpp>
#include <component/navmesh_region.hpp>
#include <navigation/navmesh_bake.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_set>

namespace vke_editor
{
    namespace
    {
        constexpr const char *NavMeshDirectory = "Assets/NavMesh";

        bool IsReservedFileName(const std::string &stem)
        {
            static const char *const reserved[] = {
                "CON", "PRN", "AUX", "NUL",
                "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
            std::string upper = stem;
            std::transform(upper.begin(), upper.end(), upper.begin(),
                           [](unsigned char character)
                           { return static_cast<char>(std::toupper(character)); });
            for (const char *name : reserved)
                if (upper == name)
                    return true;
            return false;
        }

        std::string SanitizeAssetStem(const std::string &name)
        {
            std::string stem = name;
            for (char &character : stem)
            {
                const unsigned char value = static_cast<unsigned char>(character);
                if (value < 0x20 || std::string("<>:\"/\\|?*").find(character) != std::string::npos)
                    character = '_';
            }
            while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.'))
                stem.pop_back();
            if (IsReservedFileName(stem))
                stem.insert(stem.begin(), '_');
            return stem;
        }

        vke_common::Result<std::filesystem::path> MakeUniqueNavMeshPath(const std::string &entityName)
        {
            std::unordered_set<std::string> usedPaths;
            if (auto listed = vke_common::AssetManager::IterateNavMeshAsset(
                    [&usedPaths](const vke_common::NavMeshAsset &asset)
                    { usedPaths.insert(vke_common::PathToUTF8(asset.path)); });
                !listed)
                return std::unexpected("cannot list navmesh assets: " + listed.error());

            std::string stem = SanitizeAssetStem(entityName);
            if (stem.empty())
                stem = "NavMesh";

            const std::filesystem::path directory = vke_common::PathFromUTF8(NavMeshDirectory);
            std::filesystem::path relative = directory / vke_common::PathFromUTF8(stem + ".navmesh");
            for (int suffix = 2;; ++suffix)
            {
                std::error_code error;
                if (!usedPaths.contains(vke_common::PathToUTF8(relative)) &&
                    !std::filesystem::exists(vke_common::Paths::Project(relative), error))
                    return relative;
                relative = directory / vke_common::PathFromUTF8(stem + " " + std::to_string(suffix) + ".navmesh");
            }
        }

        vke_common::Result<vke_common::AssetHandle> CreateNavMeshAsset(
            const std::string &entityName, const vke_navmesh::NavMeshBakeResult &baked)
        {
            auto uniquePath = MakeUniqueNavMeshPath(entityName);
            if (!uniquePath)
                return std::unexpected(uniquePath.error());
            const std::filesystem::path relativePath = *uniquePath;
            const std::filesystem::path fullPath = vke_common::Paths::Project(relativePath);
            std::error_code error;
            std::filesystem::create_directories(fullPath.parent_path(), error);
            if (error)
                return std::unexpected("cannot create navmesh directory: " + error.message());
            if (auto written = vke_navmesh::WriteNavMeshFile(fullPath, baked.tiles); !written)
                return std::unexpected(written.error());

            vke_common::NavMeshAsset asset;
            asset.name = vke_common::PathToUTF8(relativePath.stem());
            asset.path = relativePath;
            asset.agentConfig = baked.agentConfig;
            asset.settings = baked.settings;

            vke_common::AssetDBBase *database = vke_common::AssetManager::GetAssetDB();
            const vke_common::AssetHandle id = database->CreateNavMeshAsset(std::move(asset));
            if (auto synced = database->SyncAll(); !synced)
            {
                database->RemoveNavMeshAsset(id);
                std::error_code removeError;
                std::filesystem::remove(fullPath, removeError);
                return std::unexpected(synced.error());
            }
            return id;
        }
    }

    void Editor::drawNavMeshRegionComponent()
    {
        if (selectedEntity == entt::null ||
            !sceneManager->registry.all_of<vke_component::NavMeshRegion>(selectedEntity))
            return;
        if (!ImGui::TreeNodeEx("NavMeshRegion", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        vke_component::NavMeshRegion &region =
            sceneManager->registry.get<vke_component::NavMeshRegion>(selectedEntity);
        ImGui::InputFloat3("Center", glm::value_ptr(region.center));
        glm::vec3 halfExtent = region.halfExtent;
        if (ImGui::InputFloat3("Half Extent", glm::value_ptr(halfExtent)))
            region.halfExtent = glm::max(halfExtent, glm::vec3(0.0f));

        if (ImGui::Button("Bake NavMesh"))
            ImGui::OpenPopup("NavMeshAgentConfig");

        vke_common::AssetHandle agentConfigToBake = 0;
        if (ImGui::BeginPopup("NavMeshAgentConfig"))
        {
            ImGui::TextUnformatted("Select Agent Config");
            ImGui::Separator();
            bool hasConfigs = false;
            ShowAssetIterationResult(vke_common::AssetManager::IterateNavAgentConfigAsset(
                [&](const vke_common::NavAgentConfigAsset &asset)
                {
                    hasConfigs = true;
                    const std::string label = std::to_string(asset.id) + "  " + asset.name;
                    if (ImGui::Selectable(label.c_str()))
                        agentConfigToBake = asset.id;
                }));
            if (!hasConfigs)
                ImGui::TextDisabled("Create a Nav Agent Config in the Asset menu first.");
            ImGui::EndPopup();
        }

        if (agentConfigToBake != 0)
        {
            auto baked = vke_navmesh::BakeNavMeshRegion(selectedEntity, agentConfigToBake);
            if (!baked)
            {
                VKE_LOG_ERROR("NavMesh bake failed: {}", baked.error())
            }
            else
            {
                const std::string &entityName =
                    sceneManager->registry.get<vke_common::GameObject>(selectedEntity).name;
                auto created = CreateNavMeshAsset(entityName, *baked);
                if (!created)
                {
                    VKE_LOG_ERROR("NavMesh bake failed: {}", created.error())
                }
                else
                {
                    VKE_LOG_INFO("Baked navmesh asset {} with {} tiles", *created, baked->tiles.size());
                    assetDirectoryTree = {};
                    selectAsset(vke_common::ASSET_NAV_MESH, *created);
                }
            }
        }

        ImGui::TreePop();
    }

    void Editor::createNavAgentConfig()
    {
        std::unordered_set<std::string> names;
        if (auto listed = vke_common::AssetManager::IterateNavAgentConfigAsset(
                [&names](const vke_common::NavAgentConfigAsset &asset)
                { names.insert(asset.name); });
            !listed)
        {
            VKE_LOG_ERROR("Cannot list nav agent configs: {}", listed.error());
            return;
        }

        std::string name = "Nav Agent Config";
        for (int suffix = 2; names.contains(name); ++suffix)
            name = "Nav Agent Config " + std::to_string(suffix);

        vke_common::AssetDBBase *database = vke_common::AssetManager::GetAssetDB();
        vke_common::NavAgentConfigAsset asset;
        asset.name = name;
        const vke_common::AssetHandle id = database->CreateNavAgentConfigAsset(std::move(asset));
        if (auto synced = database->SyncAll(); !synced)
        {
            database->RemoveNavAgentConfigAsset(id);
            VKE_LOG_ERROR("Cannot create nav agent config: {}", synced.error());
            return;
        }
        assetDirectoryTree = {};
        selectAsset(vke_common::ASSET_NAV_AGENT, id);
    }
}
