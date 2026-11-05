#ifndef ASSET_MANAGER_H
#define ASSET_MANAGER_H

#include <asset/asset_db_json.hpp>
#include <filesystem>
#include <common.hpp>
#include <type_traits>
#include <unordered_set>

namespace vke_common
{
#define AM_GET_FUNC(tp) \
    static tp *Get##tp(AssetHandle id);
#define AM_ITERATE_FUNC(tp) \
    static void Iterate##tp(std::function<void(const tp &)> op);

#define AM_OP_FUNCS(tp) \
    AM_GET_FUNC(tp)     \
    AM_ITERATE_FUNC(tp)

    class AssetManager
    {
    private:
        static AssetManager *instance;
        AssetManager()
            : ftLibrary(nullptr), builtinAssets(std::make_unique<AssetDBJSON>(Paths::Engine(BuiltinAssetLUTPath))) {};
        ~AssetManager()
        {
            assetDB.reset();
            builtinAssets.reset();
            if (ftLibrary != nullptr)
            {
                FT_Done_FreeType(ftLibrary);
                ftLibrary = nullptr;
            }
        }
        AssetManager(const AssetManager &);
        AssetManager &operator=(const AssetManager);

    public:
        FT_Library ftLibrary;

        static AssetManager *GetInstance()
        {
            VKE_FATAL_IF(instance == nullptr, "AssetManager not initialized!")
            return instance;
        }

        static AssetManager *Init(std::unique_ptr<AssetDBBase> &&db);

        static void Dispose()
        {
            delete instance;
            instance = nullptr;
        }

        static bool BulkLoad(const std::filesystem::path &pth, bool builtIn = false)
        {
            if (builtIn)
                return instance->builtinAssets->BulkLoad(pth);
            return instance->assetDB->BulkLoad(pth);
        }

        static AssetDBBase *GetAssetDB() { return instance->assetDB.get(); }
        static AssetDBBase *GetBuiltinAssets() { return instance->builtinAssets.get(); }

        AM_OP_FUNCS(TextureAsset)
        AM_OP_FUNCS(MeshAsset)
        AM_OP_FUNCS(VFShaderAsset)
        AM_OP_FUNCS(ComputeShaderAsset)
        AM_OP_FUNCS(MaterialAsset)
        AM_OP_FUNCS(SkeletonAsset)
        AM_OP_FUNCS(AnimationAsset)
        AM_OP_FUNCS(SceneAsset)
        AM_OP_FUNCS(FontAsset)
        AM_OP_FUNCS(AudioClipAsset)

        static std::unique_ptr<vke_render::Texture2D> LoadTexture2DUnique(const AssetHandle hdl);
        static std::unique_ptr<vke_render::Mesh> LoadMeshUnique(const AssetHandle hdl);
        static std::unique_ptr<vke_render::ShaderModuleSet> LoadVertFragShaderUnique(const AssetHandle hdl);
        static std::unique_ptr<vke_render::ShaderModuleSet> LoadComputeShaderUnique(const AssetHandle hdl);
        static std::unique_ptr<vke_render::Material> LoadMaterialUnique(const AssetHandle hdl);
        static std::unique_ptr<vke_common::Skeleton> LoadSkeletonUnique(const AssetHandle hdl);
        static std::unique_ptr<vke_common::Animation> LoadAnimationUnique(const AssetHandle hdl);
        static std::unique_ptr<vke_common::Font> LoadFontUnique(const AssetHandle hdl);
        static std::unique_ptr<vke_audio::AudioClip> LoadAudioClipUnique(const AssetHandle hdl);

        static std::shared_ptr<vke_render::Texture2D> LoadTexture2D(const AssetHandle hdl);
        static std::shared_ptr<vke_render::Mesh> LoadMesh(const AssetHandle hdl);
        static std::shared_ptr<vke_render::ShaderModuleSet> LoadVertFragShader(const AssetHandle hdl);
        static std::shared_ptr<vke_render::ShaderModuleSet> LoadComputeShader(const AssetHandle hdl);
        static std::shared_ptr<vke_render::Material> LoadMaterial(const AssetHandle hdl);
        static std::shared_ptr<vke_common::Skeleton> LoadSkeleton(const AssetHandle hdl);
        static std::shared_ptr<vke_common::Animation> LoadAnimation(const AssetHandle hdl);
        static std::shared_ptr<vke_common::Font> LoadFont(const AssetHandle hdl);
        static std::shared_ptr<vke_audio::AudioClip> LoadAudioClip(const AssetHandle hdl);

        // Preflight checks only: these never load assets or populate caches.
        static SceneResult<void> ValidateTexture2D(AssetHandle handle);
        static SceneResult<void> ValidateMesh(AssetHandle handle);
        static SceneResult<void> ValidateVertFragShader(AssetHandle handle);
        static SceneResult<void> ValidateComputeShader(AssetHandle handle);
        static SceneResult<void> ValidateMaterial(AssetHandle handle);
        static SceneResult<void> ValidateSkeleton(AssetHandle handle);
        static SceneResult<void> ValidateAnimation(AssetHandle handle);
        static SceneResult<void> ValidateFont(AssetHandle handle);
        static SceneResult<void> ValidateAudioClip(AssetHandle handle);

        static SceneResult<std::shared_ptr<const SceneData>> LoadSceneData(AssetHandle handle);
        static SceneResult<std::shared_ptr<const SceneData>> ReloadSceneData(AssetHandle handle);
        static void InvalidateSceneData(AssetHandle handle);
        static SceneResult<SceneData> LoadSceneFile(const std::filesystem::path &path);
        static SceneResult<void> PrepareSceneData(SceneData &data);

    private:
        static std::filesystem::path resolveAssetPath(AssetHandle handle, const std::filesystem::path &path);
        std::unique_ptr<AssetDBJSON> builtinAssets;
        std::unique_ptr<AssetDBBase> assetDB;
        std::unordered_map<AssetHandle, std::unordered_set<AssetHandle>> sceneDependencies;
        std::unordered_map<AssetHandle, std::unordered_set<AssetHandle>> sceneDependents;

        SceneResult<std::shared_ptr<const SceneData>> loadSceneData(
            AssetHandle handle, std::vector<AssetHandle> &stack);
        SceneResult<void> expandSceneData(SceneData &data, std::vector<AssetHandle> &stack);
        SceneResult<void> resolveSceneAssets(SceneData &data);

        SceneResult<void> checkPath(AssetHandle handle, const std::filesystem::path &path) const;

        template <typename Get>
        static SceneResult<void> validateAsset(AssetHandle handle, Get get)
        {
            if (!instance) return std::unexpected("AssetManager is not initialized");
            if (handle == 0) return std::unexpected("required asset is empty");
            if (auto result = instance->checkAsset(get(handle)); !result)
                return std::unexpected("asset " + std::to_string(handle) + ": " + result.error());
            return {};
        }

        template <typename Asset>
        SceneResult<void> checkAsset(const Asset *asset) const
        {
            if (!asset) return std::unexpected("asset not found");
            if (asset->val) return {};
            if constexpr (std::is_same_v<Asset, MeshAsset>)
                if (asset->id == BUILTIN_MESH_PLANE_ID) return {};
            return checkPath(asset->id, asset->path);
        }

        SceneResult<void> checkAsset(const MaterialAsset *material) const;
        SceneResult<void> checkAsset(const VFShaderAsset *shader) const;

        void initBuiltinAssets();
    };
}

#undef AM_GET_FUNC
#undef AM_ITERATE_FUNC
#undef AM_OP_FUNCS

#endif
