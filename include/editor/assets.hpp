#ifndef EDITOR_ASSETS_H
#define EDITOR_ASSETS_H

#include <asset/asset_manager.hpp>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <imgui.h>

namespace vke_editor
{
    template <typename T>
    T *ShowAssetReadResult(const vke_common::Result<T *> &result)
    {
        if (!result)
        {
            ImGui::TextWrapped("Asset read failed: %s", result.error().c_str());
            return nullptr;
        }
        return *result;
    }

    inline void ShowAssetIterationResult(const vke_common::Result<void> &result)
    {
        if (!result) ImGui::TextWrapped("Asset list incomplete: %s", result.error().c_str());
    }

    extern const std::filesystem::path EditorAssetLUTPath;

    enum class AssetBrowserMode
    {
        ByType,
        ByDirectory
    };

    struct AssetTreeEntry
    {
        vke_common::AssetType type;
        vke_common::AssetHandle id;
        std::string name;
        std::string path;
    };

    struct AssetTreeNode
    {
        std::string name;
        std::map<std::string, AssetTreeNode> children;
        std::vector<AssetTreeEntry> assets;
    };
}

#endif
