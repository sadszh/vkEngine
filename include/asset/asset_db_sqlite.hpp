#ifndef ASSET_DB_SQLITE_H
#define ASSET_DB_SQLITE_H

#include <asset/asset_db.hpp>
#include <set>

struct sqlite3;
struct sqlite3_stmt;

namespace vke_common
{

#define DBSQLITE_GET_ASSET_FUNC(tp, cache, tbl, deleted)           \
    virtual Result<tp *> Get##tp(AssetHandle id) override          \
    {                                                              \
        auto it = cache.find(id);                                  \
        if (it != cache.end())                                     \
            return &it->second;                                    \
        if (deleted.contains(id))                                  \
            return nullptr;                                        \
        tp asset;                                                  \
        auto loaded = loadAssetFromDB(tbl, id, asset);             \
        if (!loaded)                                               \
            return std::unexpected(loaded.error());                \
        if (!*loaded)                                              \
            return nullptr;                                        \
        return &cache.emplace(id, std::move(asset)).first->second; \
    }

#define DBSQLITE_SET_ASSET_FUNC(tp, cache, dirty, deleted) \
    virtual void Set##tp(tp &asset) override               \
    {                                                      \
        cache[asset.id] = asset;                           \
        dirty.insert(asset.id);                            \
        deleted.erase(asset.id);                           \
        if (asset.id >= ids[tp::type])                     \
            ids[tp::type] = asset.id + 1;                  \
    }

#define DBSQLITE_SYNC_ASSET_FUNC(tp, cache, dirty, deleted, tbl)                  \
    virtual Result<void> Sync##tp(AssetHandle id) override                        \
    {                                                                             \
        if (deleted.contains(id))                                                 \
        {                                                                         \
            if (auto result = removeFromDB(tbl, id); !result)                     \
                return result;                                                    \
            deleted.erase(id);                                                    \
            return {};                                                            \
        }                                                                         \
        auto it = cache.find(id);                                                 \
        if (it == cache.end())                                                    \
            return std::unexpected(PathToUTF8(path) + ": sync " tbl " asset " +   \
                                   std::to_string(id) + ": asset is not cached"); \
        if (auto result = saveToDB(tbl, id, it->second.ToJSON().dump()); !result) \
            return result;                                                        \
        dirty.erase(id);                                                          \
        return {};                                                                \
    }

#define DBSQLITE_CREATE_ASSET_FUNC(tp, cache, dirty, deleted) \
    virtual AssetHandle Create##tp(tp &asset) override        \
    {                                                         \
        AssetHandle newId = ids[tp::type]++;                  \
        asset.id = newId;                                     \
        cache[newId] = asset;                                 \
        dirty.insert(newId);                                  \
        deleted.erase(newId);                                 \
        return newId;                                         \
    }

#define DBSQLITE_REMOVE_ASSET_FUNC(tp, cache, dirty, deleted) \
    virtual void Remove##tp(AssetHandle id) override          \
    {                                                         \
        cache.erase(id);                                      \
        dirty.erase(id);                                      \
        deleted.insert(id);                                   \
    }

#define DBSQLITE_ITERATE_ASSET_FUNC(tp, cache, dirty, deleted, tbl)               \
    virtual Result<void> Iterate##tp(std::function<void(const tp &)> op) override \
    {                                                                             \
        if (auto result = loadCleanAssetsFromDB(tbl, cache, deleted); !result)    \
            return result;                                                        \
        for (auto &kv : cache)                                                    \
            op(kv.second);                                                        \
        return {};                                                                \
    }

#define DBSQLITE_MARKDIRTY_ASSET_FUNC(tp, cache, ds)    \
    virtual void MarkDirty##tp(AssetHandle id) override \
    {                                                   \
        if (!cache.contains(id))                        \
            return;                                     \
        ds.insert(id);                                  \
    }

#define DBSQLITE_ASSET_OP_FUNCS(tp, cache, dirty, deleted, tbl) \
    DBSQLITE_GET_ASSET_FUNC(tp, cache, tbl, deleted)            \
    DBSQLITE_SET_ASSET_FUNC(tp, cache, dirty, deleted)          \
    DBSQLITE_SYNC_ASSET_FUNC(tp, cache, dirty, deleted, tbl)    \
    DBSQLITE_CREATE_ASSET_FUNC(tp, cache, dirty, deleted)       \
    DBSQLITE_REMOVE_ASSET_FUNC(tp, cache, dirty, deleted)       \
    DBSQLITE_ITERATE_ASSET_FUNC(tp, cache, dirty, deleted, tbl) \
    DBSQLITE_MARKDIRTY_ASSET_FUNC(tp, cache, dirty)

    class AssetDBSQLite : public AssetDBBase
    {
    public:
        AssetDBSQLite(const std::filesystem::path &dbPath, AssetHandle stid = 1);
        AssetDBSQLite(const AssetDBSQLite &) = delete;
        AssetDBSQLite &operator=(const AssetDBSQLite &) = delete;
        virtual ~AssetDBSQLite();

        virtual Result<void> Init() override;
        virtual Result<void> ClearAll() override;
        virtual Result<void> BulkLoad(const std::filesystem::path &pth) override;
        virtual Result<void> SyncAll() override;

        DBSQLITE_ASSET_OP_FUNCS(TextureAsset, textureCache, dirtyTexture, deletedTexture, "texture_assets")
        DBSQLITE_ASSET_OP_FUNCS(MeshAsset, meshCache, dirtyMesh, deletedMesh, "mesh_assets")
        DBSQLITE_ASSET_OP_FUNCS(VFShaderAsset, vfShaderCache, dirtyVFShader, deletedVFShader, "vf_shader_assets")
        DBSQLITE_ASSET_OP_FUNCS(ComputeShaderAsset, computeShaderCache, dirtyComputeShader, deletedComputeShader, "compute_shader_assets")
        DBSQLITE_ASSET_OP_FUNCS(MaterialAsset, materialCache, dirtyMaterial, deletedMaterial, "material_assets")
        DBSQLITE_ASSET_OP_FUNCS(SkeletonAsset, skeletonCache, dirtySkeleton, deletedSkeleton, "skeleton_assets")
        DBSQLITE_ASSET_OP_FUNCS(AnimationAsset, animationCache, dirtyAnimation, deletedAnimation, "animation_assets")
        DBSQLITE_ASSET_OP_FUNCS(SceneAsset, sceneCache, dirtyScene, deletedScene, "scene_assets")
        DBSQLITE_ASSET_OP_FUNCS(FontAsset, fontCache, dirtyFont, deletedFont, "font_assets")
        DBSQLITE_ASSET_OP_FUNCS(AudioClipAsset, audioCache, dirtyAudio, deletedAudio, "audio_clip_assets")

    private:
        sqlite3 *db = nullptr;
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

        std::set<AssetHandle> dirtyTexture;
        std::set<AssetHandle> dirtyMesh;
        std::set<AssetHandle> dirtyVFShader;
        std::set<AssetHandle> dirtyComputeShader;
        std::set<AssetHandle> dirtyMaterial;
        std::set<AssetHandle> dirtySkeleton;
        std::set<AssetHandle> dirtyAnimation;
        std::set<AssetHandle> dirtyScene;
        std::set<AssetHandle> dirtyFont;
        std::set<AssetHandle> dirtyAudio;

        std::set<AssetHandle> deletedTexture;
        std::set<AssetHandle> deletedMesh;
        std::set<AssetHandle> deletedVFShader;
        std::set<AssetHandle> deletedComputeShader;
        std::set<AssetHandle> deletedMaterial;
        std::set<AssetHandle> deletedSkeleton;
        std::set<AssetHandle> deletedAnimation;
        std::set<AssetHandle> deletedScene;
        std::set<AssetHandle> deletedFont;
        std::set<AssetHandle> deletedAudio;

        std::string databaseError(const std::string &operation) const;
        Result<void> rollback(std::string error);

        Result<void> execSQL(const std::string &sql);
        Result<void> createTables();
        Result<void> updateNextIds();

        Result<void> saveToDB(const std::string &table, AssetHandle id, const std::string &jsonData);
        Result<void> removeFromDB(const std::string &table, AssetHandle id);

        template <typename T>
        Result<bool> loadAssetFromDB(const std::string &table, AssetHandle id, T &outAsset);

        template <typename T>
        Result<void> loadCleanAssetsFromDB(const std::string &table, std::map<AssetHandle, T> &cache,
                                           const std::set<AssetHandle> &deleted);

        template <typename T>
        Result<void> flushChanges(const std::string &table, std::map<AssetHandle, T> &cache,
                                  const std::set<AssetHandle> &dirty,
                                  const std::set<AssetHandle> &deleted);
    };

} // namespace vke_common

#endif // ASSET_DB_SQLITE_H
