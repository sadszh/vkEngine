#ifndef ASSET_DB_H
#define ASSET_DB_H

#include <asset/asset.hpp>
#include <functional>

namespace vke_common
{

#define DB_GET_ASSET_FUNC(tp)                         \
protected:                                            \
    virtual Result<tp *> get##tp(AssetHandle id) = 0; \
                                                      \
public:

#define DB_SET_ASSET_FUNC(tp) \
    virtual void Set##tp(tp &&asset) = 0;

#define DB_SYNC_ASSET_FUNC(tp) \
    virtual Result<void> Sync##tp(AssetHandle id) = 0;

#define DB_CREATE_ASSET_FUNC(tp) \
    virtual AssetHandle Create##tp(tp &&asset) = 0;

#define DB_REMOVE_ASSET_FUNC(tp) \
    virtual void Remove##tp(AssetHandle id) = 0;

#define DB_ITERATE_ASSET_FUNC(tp) \
    virtual Result<void> Iterate##tp(std::function<void(const tp &)> op) = 0;

#define DB_MARKDIRTY_ASSET_FUNC(tp) \
    virtual void MarkDirty##tp(AssetHandle id) = 0;

#define DB_ASSET_OP_FUNCS(tp) \
    DB_GET_ASSET_FUNC(tp)     \
    DB_SET_ASSET_FUNC(tp)     \
    DB_SYNC_ASSET_FUNC(tp)    \
    DB_CREATE_ASSET_FUNC(tp)  \
    DB_REMOVE_ASSET_FUNC(tp)  \
    DB_ITERATE_ASSET_FUNC(tp) \
    DB_MARKDIRTY_ASSET_FUNC(tp)

    class AssetDBBase
    {
        friend class AssetManager;

    public:
        AssetDBBase() = default;
        AssetDBBase(const std::filesystem::path &pth, AssetHandle stid)
            : path(pth), stID(stid) {}

        virtual ~AssetDBBase() {}

        virtual Result<void> Init() = 0;
        virtual Result<void> ClearAll() = 0;
        virtual Result<void> BulkLoad(const std::filesystem::path &pth) = 0;
        virtual Result<void> SyncAll() = 0;

        DB_ASSET_OP_FUNCS(TextureAsset)
        DB_ASSET_OP_FUNCS(MeshAsset)
        DB_ASSET_OP_FUNCS(VFShaderAsset)
        DB_ASSET_OP_FUNCS(ComputeShaderAsset)
        DB_ASSET_OP_FUNCS(MaterialAsset)
        DB_ASSET_OP_FUNCS(SkeletonAsset)
        DB_ASSET_OP_FUNCS(AnimationAsset)
        DB_ASSET_OP_FUNCS(SceneAsset)
        DB_ASSET_OP_FUNCS(FontAsset)
        DB_ASSET_OP_FUNCS(AudioClipAsset)
        DB_ASSET_OP_FUNCS(NavAgentConfigAsset)
        DB_ASSET_OP_FUNCS(NavMeshAsset)

        std::filesystem::path path;

    protected:
        template <typename A>
        static void eraseCachedAsset(std::map<AssetHandle, A> &cache, AssetHandle id)
        {
            auto found = cache.find(id);
            if (found == cache.end())
                return;
            found->second.WaitForLoad();
            cache.erase(found);
        }

        template <typename A>
        static void clearAssetCache(std::map<AssetHandle, A> &cache)
        {
            for (auto &[id, asset] : cache)
                asset.WaitForLoad();
            cache.clear();
        }

        AssetHandle stID = 1;

    private:
    };
}

#endif
