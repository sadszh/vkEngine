#include <asset/asset_manager.hpp>
#include <vector>
#include <fstream>
#include <string>
#include <algorithm>
#include <span>

namespace vke_common
{
    AssetManager *AssetManager::instance = nullptr;

    extern const std::vector<vke_render::Vertex> planeVertices;
    extern const std::vector<uint32_t> planeIndices;

    AssetManager *AssetManager::Init(std::unique_ptr<AssetDBBase> &&db)
    {
        instance = new AssetManager();
        instance->assetDB = std::move(db);
        instance->assetDB->Init();
        instance->ftLibrary = nullptr;
        VKE_FATAL_IF(FT_Init_FreeType(&(instance->ftLibrary)), "Failed to initialize FreeType library!")
        instance->initBuiltinAssets();
        return instance;
    }

    std::filesystem::path AssetManager::resolveAssetPath(AssetHandle handle, const std::filesystem::path &path)
    {
        return handle < CUSTOM_ASSET_ID_ST ? Paths::Engine(path) : Paths::Project(path);
    }

    void AssetManager::initBuiltinAssets()
    {
        builtinAssets->Init();
        MeshAsset plane(BUILTIN_MESH_PLANE_ID, "Plane", "");
        plane.val = std::make_shared<vke_render::Mesh>(BUILTIN_MESH_PLANE_ID,
                                                       std::span<const vke_render::Vertex>(planeVertices),
                                                       std::span<const uint32_t>(planeIndices));
        builtinAssets->SetMeshAsset(plane);
    }

#define AM_GET_IMPL(tp)                                  \
    tp *AssetManager::Get##tp(AssetHandle id)            \
    {                                                    \
        if (id < CUSTOM_ASSET_ID_ST)                     \
            return instance->builtinAssets->Get##tp(id); \
        return instance->assetDB->Get##tp(id);           \
    }

    AM_GET_IMPL(TextureAsset)
    AM_GET_IMPL(MeshAsset)
    AM_GET_IMPL(VFShaderAsset)
    AM_GET_IMPL(ComputeShaderAsset)
    AM_GET_IMPL(MaterialAsset)
    AM_GET_IMPL(SkeletonAsset)
    AM_GET_IMPL(AnimationAsset)
    AM_GET_IMPL(SceneAsset)
    AM_GET_IMPL(FontAsset)
    AM_GET_IMPL(AudioClipAsset)

#undef AM_GET_IMPL

#define AM_ITERATE_IMPL(tp)                                            \
    void AssetManager::Iterate##tp(std::function<void(const tp &)> op) \
    {                                                                  \
        instance->builtinAssets->Iterate##tp(op);                      \
        instance->assetDB->Iterate##tp(op);                            \
    }

    AM_ITERATE_IMPL(TextureAsset)
    AM_ITERATE_IMPL(MeshAsset)
    AM_ITERATE_IMPL(VFShaderAsset)
    AM_ITERATE_IMPL(ComputeShaderAsset)
    AM_ITERATE_IMPL(MaterialAsset)
    AM_ITERATE_IMPL(SkeletonAsset)
    AM_ITERATE_IMPL(AnimationAsset)
    AM_ITERATE_IMPL(SceneAsset)
    AM_ITERATE_IMPL(FontAsset)
    AM_ITERATE_IMPL(AudioClipAsset)

#undef AM_ITERATE_IMPL
};
