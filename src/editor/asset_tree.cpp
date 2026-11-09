#include <editor/editor.hpp>
#include <algorithm>

namespace vke_editor
{
    namespace
    {
        std::string NormalizeAssetPath(std::string path)
        {
            std::replace(path.begin(), path.end(), '\\', '/');
            while (!path.empty() && path.front() == '/')
                path.erase(path.begin());
            return path;
        }

        std::vector<std::string> GetAssetDirectoryParts(const std::string &assetPath)
        {
            const std::string normalizedPath = NormalizeAssetPath(assetPath);
            const size_t fileNamePos = normalizedPath.find_last_of('/');
            if (fileNamePos == std::string::npos)
                return {};

            std::vector<std::string> parts;
            const std::string directory = normalizedPath.substr(0, fileNamePos);
            size_t begin = 0;
            while (begin < directory.length())
            {
                const size_t end = directory.find('/', begin);
                const std::string part = directory.substr(
                    begin, end == std::string::npos ? std::string::npos : end - begin);
                if (!part.empty())
                    parts.push_back(part);
                if (end == std::string::npos)
                    break;
                begin = end + 1;
            }
            return parts;
        }

        template <typename Fn>
        vke_common::Result<void> AddAssetsToDirectoryTree(AssetTreeNode &root, vke_common::AssetType type, Fn &&iterate)
        {
            return iterate([&](auto &asset)
                           {
                AssetTreeNode *node = &root;
                const std::filesystem::path assetPath = asset.path.lexically_normal();
                std::vector<std::string> directoryParts =
                    GetAssetDirectoryParts(vke_common::PathToUTF8(assetPath));

                for (const std::string &part : directoryParts)
                {
                    AssetTreeNode &child = node->children[part];
                    if (child.name.empty())
                        child.name = part;
                    node = &child;
                }

                node->assets.push_back({type, asset.id, asset.name, vke_common::PathToUTF8(asset.path)}); });
        }
    }

    vke_common::Result<void> Editor::rebuildAssetDirectoryTree(vke_common::AssetManager *assetManager)
    {
        AssetTreeNode tree;
        tree.name = "Assets";

        AssetTreeNode &builtinRoot = tree.children["Built-in"];
        builtinRoot.name = "Built-in";
        vke_common::AssetDBBase *builtinDB = vke_common::AssetManager::GetBuiltinAssets();
        if (auto result = AddAssetsToDirectoryTree(builtinRoot, vke_common::ASSET_TEXTURE, [&](auto &&op)
                                                   { return builtinDB->IterateTextureAsset(op); });
            !result)
            return result;
        if (auto result = AddAssetsToDirectoryTree(builtinRoot, vke_common::ASSET_MESH, [&](auto &&op)
                                                   { return builtinDB->IterateMeshAsset(op); });
            !result)
            return result;
        if (auto result = AddAssetsToDirectoryTree(builtinRoot, vke_common::ASSET_VF_SHADER, [&](auto &&op)
                                                   { return builtinDB->IterateVFShaderAsset(op); });
            !result)
            return result;
        if (auto result = AddAssetsToDirectoryTree(builtinRoot, vke_common::ASSET_COMPUTE_SHADER, [&](auto &&op)
                                                   { return builtinDB->IterateComputeShaderAsset(op); });
            !result)
            return result;
        if (auto result = AddAssetsToDirectoryTree(builtinRoot, vke_common::ASSET_MATERIAL, [&](auto &&op)
                                                   { return builtinDB->IterateMaterialAsset(op); });
            !result)
            return result;
        if (auto result = AddAssetsToDirectoryTree(builtinRoot, vke_common::ASSET_SKELETON, [&](auto &&op)
                                                   { return builtinDB->IterateSkeletonAsset(op); });
            !result)
            return result;
        if (auto result = AddAssetsToDirectoryTree(builtinRoot, vke_common::ASSET_ANIMATION, [&](auto &&op)
                                                   { return builtinDB->IterateAnimationAsset(op); });
            !result)
            return result;
        if (auto result = AddAssetsToDirectoryTree(builtinRoot, vke_common::ASSET_SCENE, [&](auto &&op)
                                                   { return builtinDB->IterateSceneAsset(op); });
            !result)
            return result;
        if (auto result = AddAssetsToDirectoryTree(builtinRoot, vke_common::ASSET_FONT, [&](auto &&op)
                                                   { return builtinDB->IterateFontAsset(op); });
            !result)
            return result;
        if (auto result = AddAssetsToDirectoryTree(builtinRoot, vke_common::ASSET_AUDIO_CLIP, [&](auto &&op)
                                                   { return builtinDB->IterateAudioClipAsset(op); });
            !result)
            return result;

        AssetTreeNode &projectRoot = tree.children["Project"];
        projectRoot.name = "Project";
        vke_common::AssetDBBase *projectDB = vke_common::AssetManager::GetAssetDB();
        if (auto result = AddAssetsToDirectoryTree(projectRoot, vke_common::ASSET_TEXTURE, [&](auto &&op)
                                                   { return projectDB->IterateTextureAsset(op); });
            !result)
            return result;
        if (auto result = AddAssetsToDirectoryTree(projectRoot, vke_common::ASSET_MESH, [&](auto &&op)
                                                   { return projectDB->IterateMeshAsset(op); });
            !result)
            return result;
        if (auto result = AddAssetsToDirectoryTree(projectRoot, vke_common::ASSET_VF_SHADER, [&](auto &&op)
                                                   { return projectDB->IterateVFShaderAsset(op); });
            !result)
            return result;
        if (auto result = AddAssetsToDirectoryTree(projectRoot, vke_common::ASSET_COMPUTE_SHADER, [&](auto &&op)
                                                   { return projectDB->IterateComputeShaderAsset(op); });
            !result)
            return result;
        if (auto result = AddAssetsToDirectoryTree(projectRoot, vke_common::ASSET_MATERIAL, [&](auto &&op)
                                                   { return projectDB->IterateMaterialAsset(op); });
            !result)
            return result;
        if (auto result = AddAssetsToDirectoryTree(projectRoot, vke_common::ASSET_SKELETON, [&](auto &&op)
                                                   { return projectDB->IterateSkeletonAsset(op); });
            !result)
            return result;
        if (auto result = AddAssetsToDirectoryTree(projectRoot, vke_common::ASSET_ANIMATION, [&](auto &&op)
                                                   { return projectDB->IterateAnimationAsset(op); });
            !result)
            return result;
        if (auto result = AddAssetsToDirectoryTree(projectRoot, vke_common::ASSET_SCENE, [&](auto &&op)
                                                   { return projectDB->IterateSceneAsset(op); });
            !result)
            return result;
        if (auto result = AddAssetsToDirectoryTree(projectRoot, vke_common::ASSET_FONT, [&](auto &&op)
                                                   { return projectDB->IterateFontAsset(op); });
            !result)
            return result;
        if (auto result = AddAssetsToDirectoryTree(projectRoot, vke_common::ASSET_AUDIO_CLIP, [&](auto &&op)
                                                   { return projectDB->IterateAudioClipAsset(op); });
            !result)
            return result;
        assetDirectoryTree = std::move(tree);
        return {};
    }

}
