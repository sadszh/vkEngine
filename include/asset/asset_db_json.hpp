#ifndef ASSET_DB_JSON_H
#define ASSET_DB_JSON_H

#include <asset/asset.hpp>
#include <asset/asset_db.hpp>

namespace vke_common
{

#define DBJSON_GET_ASSET_FUNC(tp, cache)                  \
    virtual Result<tp *> Get##tp(AssetHandle id) override \
    {                                                     \
        auto it = cache.find(id);                         \
        if (it != cache.end())                            \
            return &it->second;                           \
        return nullptr;                                   \
    }

#define DBJSON_SET_ASSET_FUNC(tp, cache)     \
    virtual void Set##tp(tp &asset) override \
    {                                        \
        cache[asset.id] = asset;             \
    }

#define DBJSON_SYNC_ASSET_FUNC(tp, cache) \
    virtual Result<void> Sync##tp(AssetHandle id) override { return std::unexpected("JSON asset database does not support syncing a single asset"); }

#define DBJSON_CREATE_ASSET_FUNC(tp, cache)            \
    virtual AssetHandle Create##tp(tp &asset) override \
    {                                                  \
        AssetHandle newId = ids[tp::type]++;           \
        asset.id = newId;                              \
        cache[newId] = asset;                          \
        return newId;                                  \
    }

#define DBJSON_REMOVE_ASSET_FUNC(tp, cache)          \
    virtual void Remove##tp(AssetHandle id) override \
    {                                                \
        cache.erase(id);                             \
    }

#define DBJSON_ITERATE_ASSET_FUNC(tp, cache)                                      \
    virtual Result<void> Iterate##tp(std::function<void(const tp &)> op) override \
    {                                                                             \
        for (auto &kv : cache)                                                    \
            op(kv.second);                                                        \
        return {};                                                                \
    }

#define DBJSON_MARKDIRTY_ASSET_FUNC(tp) \
    virtual void MarkDirty##tp(AssetHandle id) override {}

#define DBJSON_ASSET_OP_FUNCS(tp, cache) \
    DBJSON_GET_ASSET_FUNC(tp, cache)     \
    DBJSON_SET_ASSET_FUNC(tp, cache)     \
    DBJSON_SYNC_ASSET_FUNC(tp, cache)    \
    DBJSON_CREATE_ASSET_FUNC(tp, cache)  \
    DBJSON_REMOVE_ASSET_FUNC(tp, cache)  \
    DBJSON_ITERATE_ASSET_FUNC(tp, cache) \
    DBJSON_MARKDIRTY_ASSET_FUNC(tp)

    class AssetDBJSON : public AssetDBBase
    {
    public:
        AssetDBJSON(const std::filesystem::path &pth, AssetHandle stid = 1)
            : AssetDBBase(pth, stid)
        {
            for (int i = 0; i < ASSET_CNT_FLAG; ++i)
                ids[i] = stid;
        }

        virtual ~AssetDBJSON() {}

        virtual Result<void> Init() override { return bulkLoad(path); }
        virtual Result<void> ClearAll() override;
        virtual Result<void> BulkLoad(const std::filesystem::path &pth) override { return bulkLoad(pth); }
        virtual Result<void> SyncAll() override { return saveAll(path); }

        DBJSON_ASSET_OP_FUNCS(TextureAsset, textureCache)
        DBJSON_ASSET_OP_FUNCS(MeshAsset, meshCache)
        DBJSON_ASSET_OP_FUNCS(VFShaderAsset, vfShaderCache)
        DBJSON_ASSET_OP_FUNCS(ComputeShaderAsset, computeShaderCache)
        DBJSON_ASSET_OP_FUNCS(MaterialAsset, materialCache)
        DBJSON_ASSET_OP_FUNCS(SkeletonAsset, skeletonCache)
        DBJSON_ASSET_OP_FUNCS(AnimationAsset, animationCache)
        DBJSON_ASSET_OP_FUNCS(SceneAsset, sceneCache)
        DBJSON_ASSET_OP_FUNCS(FontAsset, fontCache)
        DBJSON_ASSET_OP_FUNCS(AudioClipAsset, audioCache)

    private:
        AssetHandle ids[ASSET_CNT_FLAG] = {};
        std::map<AssetHandle, TextureAsset> textureCache;
        std::map<AssetHandle, MeshAsset> meshCache;
        std::map<AssetHandle, VFShaderAsset> vfShaderCache;
        std::map<AssetHandle, ComputeShaderAsset> computeShaderCache;
        std::map<AssetHandle, MaterialAsset> materialCache;
        std::map<AssetHandle, SkeletonAsset> skeletonCache;
        std::map<AssetHandle, AnimationAsset> animationCache;
        std::map<AssetHandle, SceneAsset> sceneCache;
        std::map<AssetHandle, FontAsset> fontCache;
        std::map<AssetHandle, AudioClipAsset> audioCache;

        Result<void> bulkLoad(const std::filesystem::path &pth);
        Result<void> saveAll(const std::filesystem::path &pth);
    };
}

#endif
