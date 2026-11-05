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
        void AddAssetsToDirectoryTree(AssetTreeNode &root, vke_common::AssetType type, Fn &&iterate)
        {
            iterate([&](auto &asset)
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

    void Editor::rebuildAssetDirectoryTree(vke_common::AssetManager *assetManager)
    {
        assetDirectoryTree = {};
        assetDirectoryTree.name = "Assets";

        AssetTreeNode &builtinRoot = assetDirectoryTree.children["Built-in"];
        builtinRoot.name = "Built-in";
        vke_common::AssetDBBase *builtinDB = vke_common::AssetManager::GetBuiltinAssets();
        AddAssetsToDirectoryTree(builtinRoot, vke_common::ASSET_TEXTURE, [&](auto &&op)
                                 { builtinDB->IterateTextureAsset(op); });
        AddAssetsToDirectoryTree(builtinRoot, vke_common::ASSET_MESH, [&](auto &&op)
                                 { builtinDB->IterateMeshAsset(op); });
        AddAssetsToDirectoryTree(builtinRoot, vke_common::ASSET_VF_SHADER, [&](auto &&op)
                                 { builtinDB->IterateVFShaderAsset(op); });
        AddAssetsToDirectoryTree(builtinRoot, vke_common::ASSET_COMPUTE_SHADER, [&](auto &&op)
                                 { builtinDB->IterateComputeShaderAsset(op); });
        AddAssetsToDirectoryTree(builtinRoot, vke_common::ASSET_MATERIAL, [&](auto &&op)
                                 { builtinDB->IterateMaterialAsset(op); });
        AddAssetsToDirectoryTree(builtinRoot, vke_common::ASSET_SKELETON, [&](auto &&op)
                                 { builtinDB->IterateSkeletonAsset(op); });
        AddAssetsToDirectoryTree(builtinRoot, vke_common::ASSET_ANIMATION, [&](auto &&op)
                                 { builtinDB->IterateAnimationAsset(op); });
        AddAssetsToDirectoryTree(builtinRoot, vke_common::ASSET_SCENE, [&](auto &&op)
                                 { builtinDB->IterateSceneAsset(op); });
        AddAssetsToDirectoryTree(builtinRoot, vke_common::ASSET_FONT, [&](auto &&op)
                                 { builtinDB->IterateFontAsset(op); });
        AddAssetsToDirectoryTree(builtinRoot, vke_common::ASSET_AUDIO_CLIP, [&](auto &&op)
                                 { builtinDB->IterateAudioClipAsset(op); });

        AssetTreeNode &projectRoot = assetDirectoryTree.children["Project"];
        projectRoot.name = "Project";
        vke_common::AssetDBBase *projectDB = vke_common::AssetManager::GetAssetDB();
        AddAssetsToDirectoryTree(projectRoot, vke_common::ASSET_TEXTURE, [&](auto &&op)
                                 { projectDB->IterateTextureAsset(op); });
        AddAssetsToDirectoryTree(projectRoot, vke_common::ASSET_MESH, [&](auto &&op)
                                 { projectDB->IterateMeshAsset(op); });
        AddAssetsToDirectoryTree(projectRoot, vke_common::ASSET_VF_SHADER, [&](auto &&op)
                                 { projectDB->IterateVFShaderAsset(op); });
        AddAssetsToDirectoryTree(projectRoot, vke_common::ASSET_COMPUTE_SHADER, [&](auto &&op)
                                 { projectDB->IterateComputeShaderAsset(op); });
        AddAssetsToDirectoryTree(projectRoot, vke_common::ASSET_MATERIAL, [&](auto &&op)
                                 { projectDB->IterateMaterialAsset(op); });
        AddAssetsToDirectoryTree(projectRoot, vke_common::ASSET_SKELETON, [&](auto &&op)
                                 { projectDB->IterateSkeletonAsset(op); });
        AddAssetsToDirectoryTree(projectRoot, vke_common::ASSET_ANIMATION, [&](auto &&op)
                                 { projectDB->IterateAnimationAsset(op); });
        AddAssetsToDirectoryTree(projectRoot, vke_common::ASSET_SCENE, [&](auto &&op)
                                 { projectDB->IterateSceneAsset(op); });
        AddAssetsToDirectoryTree(projectRoot, vke_common::ASSET_FONT, [&](auto &&op)
                                 { projectDB->IterateFontAsset(op); });
        AddAssetsToDirectoryTree(projectRoot, vke_common::ASSET_AUDIO_CLIP, [&](auto &&op)
                                 { projectDB->IterateAudioClipAsset(op); });
    }

}
