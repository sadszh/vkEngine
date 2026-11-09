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

    Result<AssetManager *> AssetManager::Init(std::unique_ptr<AssetDBBase> &&db)
    {
        auto *manager = new AssetManager();
        manager->assetDB = std::move(db);
        auto result = manager->assetDB->Init();
        if (result && FT_Init_FreeType(&manager->ftLibrary))
            result = std::unexpected("Failed to initialize FreeType library");
        if (result) result = manager->initBuiltinAssets();
        if (!result)
        {
            delete manager;
            return std::unexpected(result.error());
        }
        instance = manager;
        return manager;
    }

    std::filesystem::path AssetManager::resolveAssetPath(AssetHandle handle, const std::filesystem::path &path)
    {
        return handle < CUSTOM_ASSET_ID_ST ? Paths::Engine(path) : Paths::Project(path);
    }

    Result<void> AssetManager::initBuiltinAssets()
    {
        if (auto result = builtinAssets->Init(); !result) return result;
        MeshAsset plane(BUILTIN_MESH_PLANE_ID, "Plane", "");
        plane.val = std::make_shared<vke_render::Mesh>(BUILTIN_MESH_PLANE_ID,
                                                       std::span<const vke_render::Vertex>(planeVertices),
                                                       std::span<const uint32_t>(planeIndices));
        builtinAssets->SetMeshAsset(plane);
        return {};
    }

#define AM_GET_IMPL(tp)                                  \
    Result<tp *> AssetManager::Get##tp(AssetHandle id)   \
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

#define AM_ITERATE_IMPL(tp)                                                                 \
    Result<void> AssetManager::Iterate##tp(std::function<void(const tp &)> op)              \
    {                                                                                       \
        if (auto result = instance->builtinAssets->Iterate##tp(op); !result) return result; \
        return instance->assetDB->Iterate##tp(op);                                          \
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
