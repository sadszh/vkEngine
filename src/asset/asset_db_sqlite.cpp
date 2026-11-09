#include <asset/asset_db_sqlite.hpp>
#include <sqlite/sqlite3.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <vector>
#include <string>

namespace vke_common
{
    namespace
    {
        using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
    }

    std::string AssetDBSQLite::databaseError(const std::string &operation) const
    {
        return PathToUTF8(path) + ": " + operation + ": " + sqlite3_errmsg(db);
    }

    Result<void> AssetDBSQLite::rollback(std::string error)
    {
        if (!sqlite3_get_autocommit(db))
            if (auto result = execSQL("ROLLBACK"); !result)
                error += "; " + result.error() + "; database must be discarded";
        return std::unexpected(std::move(error));
    }


    AssetDBSQLite::AssetDBSQLite(const std::filesystem::path &dbPath, AssetHandle stid)
        : AssetDBBase(dbPath, stid)
    {
        for (int i = 0; i < ASSET_CNT_FLAG; ++i)
            ids[i] = stid;
    }

    AssetDBSQLite::~AssetDBSQLite()
    {
        if (db)
        {
            sqlite3_close(db);
            db = nullptr;
        }
    }

    static const char *TABLE_NAMES[] = {
        "texture_assets",
        "mesh_assets",
        "vf_shader_assets",
        "compute_shader_assets",
        "material_assets",
        "skeleton_assets",
        "animation_assets",
        "scene_assets",
        "font_assets",
        "audio_clip_assets",
    };

    Result<void> AssetDBSQLite::execSQL(const std::string &sql)
    {
        char *errMsg = nullptr;
        const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK)
        {
            const std::string error = PathToUTF8(path) + ": " + sql + ": " +
                                      (errMsg ? errMsg : sqlite3_errmsg(db));
            sqlite3_free(errMsg);
            return std::unexpected(error);
        }
        return {};
    }

    Result<void> AssetDBSQLite::createTables()
    {
        for (const auto &table : TABLE_NAMES)
        {
            auto result = execSQL(std::string("CREATE TABLE IF NOT EXISTS ") + table +
                                  " (id INTEGER PRIMARY KEY, data TEXT NOT NULL)");
            if (!result) return result;
        }
        return {};
    }

    Result<void> AssetDBSQLite::updateNextIds()
    {
        for (int type = 0; type < ASSET_CNT_FLAG; ++type)
        {
            const std::string sql = "SELECT MAX(id) FROM " + std::string(TABLE_NAMES[type]);
            sqlite3_stmt *raw = nullptr;
            const int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &raw, nullptr);
            Statement stmt(raw, sqlite3_finalize);
            if (rc != SQLITE_OK || sqlite3_step(stmt.get()) != SQLITE_ROW)
                return std::unexpected(databaseError(sql));
            ids[type] = stID;
            if (sqlite3_column_type(stmt.get(), 0) != SQLITE_NULL)
            {
                const sqlite3_int64 maxId = sqlite3_column_int64(stmt.get(), 0);
                if (maxId >= 0) ids[type] = std::max(stID, static_cast<AssetHandle>(maxId) + 1);
            }
        }
        return {};
    }

    Result<void> AssetDBSQLite::saveToDB(const std::string &table, AssetHandle id, const std::string &jsonData)
    {
        const std::string operation = "saveToDB: " + table + " asset " + std::to_string(id);
        if (id > INT64_MAX)
            return std::unexpected(PathToUTF8(path) + ": " + operation + ": id exceeds SQLite integer range");
        const std::string sql = "INSERT OR REPLACE INTO " + table + " (id, data) VALUES (?1, ?2)";
        sqlite3_stmt *raw = nullptr;
        const int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &raw, nullptr);
        Statement stmt(raw, sqlite3_finalize);
        if (rc != SQLITE_OK) return std::unexpected(databaseError(operation));
        if (sqlite3_bind_int64(stmt.get(), 1, static_cast<sqlite3_int64>(id)) != SQLITE_OK ||
            sqlite3_bind_text(stmt.get(), 2, jsonData.c_str(), static_cast<int>(jsonData.size()), SQLITE_TRANSIENT) != SQLITE_OK)
            return std::unexpected(databaseError(operation));
        if (sqlite3_step(stmt.get()) != SQLITE_DONE)
            return std::unexpected(databaseError(operation));
        return {};
    }

    Result<void> AssetDBSQLite::removeFromDB(const std::string &table, AssetHandle id)
    {
        const std::string operation = "removeFromDB: " + table + " asset " + std::to_string(id);
        if (id > INT64_MAX)
            return std::unexpected(PathToUTF8(path) + ": " + operation + ": id exceeds SQLite integer range");
        const std::string sql = "DELETE FROM " + table + " WHERE id = ?1";
        sqlite3_stmt *raw = nullptr;
        const int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &raw, nullptr);
        Statement stmt(raw, sqlite3_finalize);
        if (rc != SQLITE_OK) return std::unexpected(databaseError(operation));
        if (sqlite3_bind_int64(stmt.get(), 1, static_cast<sqlite3_int64>(id)) != SQLITE_OK)
            return std::unexpected(databaseError(operation));
        if (sqlite3_step(stmt.get()) != SQLITE_DONE)
            return std::unexpected(databaseError(operation));
        return {};
    }

    Result<void> AssetDBSQLite::Init()
    {
        const int rc = sqlite3_open_v2(PathToUTF8(path).c_str(), &db,
                                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
        Result<void> result;
        if (rc != SQLITE_OK) result = std::unexpected(databaseError("open database"));
        else
        {
            result = createTables();
            if (result) result = updateNextIds();
        }
        if (!result)
        {
            sqlite3_close(db);
            db = nullptr;
        }
        return result;
    }

    Result<void> AssetDBSQLite::ClearAll()
    {
        if (auto result = execSQL("BEGIN IMMEDIATE"); !result) return result;
        for (const auto &table : TABLE_NAMES)
            if (auto result = execSQL("DELETE FROM " + std::string(table)); !result)
                return rollback(result.error());
        if (auto result = execSQL("COMMIT"); !result) return rollback(result.error());

        textureCache.clear();
        meshCache.clear();
        vfShaderCache.clear();
        computeShaderCache.clear();
        materialCache.clear();
        skeletonCache.clear();
        animationCache.clear();
        sceneCache.clear();
        fontCache.clear();
        audioCache.clear();

        for (auto &id : ids)
            id = stID;

        dirtyTexture.clear();
        dirtyMesh.clear();
        dirtyVFShader.clear();
        dirtyComputeShader.clear();
        dirtyMaterial.clear();
        dirtySkeleton.clear();
        dirtyAnimation.clear();
        dirtyScene.clear();
        dirtyFont.clear();
        dirtyAudio.clear();

        deletedTexture.clear();
        deletedMesh.clear();
        deletedVFShader.clear();
        deletedComputeShader.clear();
        deletedMaterial.clear();
        deletedSkeleton.clear();
        deletedAnimation.clear();
        deletedScene.clear();
        deletedFont.clear();
        deletedAudio.clear();
        return {};
    }

    Result<void> AssetDBSQLite::BulkLoad(const std::filesystem::path &pth)
    {
        auto loaded = LoadJSON(pth);
        if (!loaded)
            return std::unexpected(loaded.error());
        const auto &assets = *loaded;
        if (auto valid = ValidateAssetListJSON(assets); !valid)
            return std::unexpected(PathToUTF8(pth) + ": " + valid.error());

        for (const auto &asset : assets)
            if (asset["id"].get<AssetHandle>() > INT64_MAX)
                return std::unexpected(PathToUTF8(pth) + ": asset id exceeds SQLite integer range");

        // BulkLoad only changes the in-memory layer. SyncAll/Sync* persists the
        // imported dirty entries to SQLite later.
#define CACHE_IMPORTED_ASSET(typeId, tp, cache, dirty, deleted) \
    case typeId:                                                \
        cache[id] = tp(id, asset);                              \
        dirty.insert(id);                                       \
        deleted.erase(id);                                      \
        break;

        for (const auto &asset : assets)
        {
            const int type = asset["type"].get<int>();
            const AssetHandle id = asset["id"].get<AssetHandle>();
            switch (type)
            {
                CACHE_IMPORTED_ASSET(ASSET_TEXTURE, TextureAsset, textureCache, dirtyTexture, deletedTexture)
                CACHE_IMPORTED_ASSET(ASSET_MESH, MeshAsset, meshCache, dirtyMesh, deletedMesh)
                CACHE_IMPORTED_ASSET(ASSET_VF_SHADER, VFShaderAsset, vfShaderCache, dirtyVFShader, deletedVFShader)
                CACHE_IMPORTED_ASSET(ASSET_COMPUTE_SHADER, ComputeShaderAsset, computeShaderCache, dirtyComputeShader, deletedComputeShader)
                CACHE_IMPORTED_ASSET(ASSET_MATERIAL, MaterialAsset, materialCache, dirtyMaterial, deletedMaterial)
                CACHE_IMPORTED_ASSET(ASSET_SKELETON, SkeletonAsset, skeletonCache, dirtySkeleton, deletedSkeleton)
                CACHE_IMPORTED_ASSET(ASSET_ANIMATION, AnimationAsset, animationCache, dirtyAnimation, deletedAnimation)
                CACHE_IMPORTED_ASSET(ASSET_SCENE, SceneAsset, sceneCache, dirtyScene, deletedScene)
                CACHE_IMPORTED_ASSET(ASSET_FONT, FontAsset, fontCache, dirtyFont, deletedFont)
                CACHE_IMPORTED_ASSET(ASSET_AUDIO_CLIP, AudioClipAsset, audioCache, dirtyAudio, deletedAudio)
            }
            ids[type] = std::max(ids[type], id + 1);
        }

#undef CACHE_IMPORTED_ASSET
        return {};
    }

    Result<void> AssetDBSQLite::SyncAll()
    {
        if (auto result = execSQL("BEGIN IMMEDIATE"); !result) return result;
        if (auto result = flushChanges("texture_assets", textureCache, dirtyTexture, deletedTexture); !result) return rollback(result.error());
        if (auto result = flushChanges("mesh_assets", meshCache, dirtyMesh, deletedMesh); !result) return rollback(result.error());
        if (auto result = flushChanges("vf_shader_assets", vfShaderCache, dirtyVFShader, deletedVFShader); !result) return rollback(result.error());
        if (auto result = flushChanges("compute_shader_assets", computeShaderCache, dirtyComputeShader, deletedComputeShader); !result) return rollback(result.error());
        if (auto result = flushChanges("material_assets", materialCache, dirtyMaterial, deletedMaterial); !result) return rollback(result.error());
        if (auto result = flushChanges("skeleton_assets", skeletonCache, dirtySkeleton, deletedSkeleton); !result) return rollback(result.error());
        if (auto result = flushChanges("animation_assets", animationCache, dirtyAnimation, deletedAnimation); !result) return rollback(result.error());
        if (auto result = flushChanges("scene_assets", sceneCache, dirtyScene, deletedScene); !result) return rollback(result.error());
        if (auto result = flushChanges("font_assets", fontCache, dirtyFont, deletedFont); !result) return rollback(result.error());
        if (auto result = flushChanges("audio_clip_assets", audioCache, dirtyAudio, deletedAudio); !result) return rollback(result.error());
        if (auto result = execSQL("COMMIT"); !result) return rollback(result.error());

        dirtyTexture.clear();
        dirtyMesh.clear();
        dirtyVFShader.clear();
        dirtyComputeShader.clear();
        dirtyMaterial.clear();
        dirtySkeleton.clear();
        dirtyAnimation.clear();
        dirtyScene.clear();
        dirtyFont.clear();
        dirtyAudio.clear();

        deletedTexture.clear();
        deletedMesh.clear();
        deletedVFShader.clear();
        deletedComputeShader.clear();
        deletedMaterial.clear();
        deletedSkeleton.clear();
        deletedAnimation.clear();
        deletedScene.clear();
        deletedFont.clear();
        deletedAudio.clear();
        return {};
    }

    template <typename T>
    static Result<T> parseAssetRecord(const char *raw, int size, AssetHandle id)
    {
        if (!raw || size <= 0) return std::unexpected("empty JSON record");
        auto json = nlohmann::json::parse(raw, raw + size, nullptr, false);
        if (json.is_discarded()) return std::unexpected("invalid JSON");
        if (auto valid = ValidateAssetJSON(json); !valid) return std::unexpected(valid.error());
        if (json["type"].get<AssetType>() != T::type || json["id"].get<AssetHandle>() != id)
            return std::unexpected("JSON type or id does not match the database record");
        return T(id, json);
    }

    template <typename T>
    Result<bool> AssetDBSQLite::loadAssetFromDB(const std::string &table, AssetHandle id, T &outAsset)
    {
        if (id > INT64_MAX) return false;
        const std::string operation = "read " + table + " asset " + std::to_string(id);
        const std::string sql = "SELECT data FROM " + table + " WHERE id = ?1";
        sqlite3_stmt *raw = nullptr;
        const int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &raw, nullptr);
        Statement stmt(raw, sqlite3_finalize);
        if (rc != SQLITE_OK) return std::unexpected(databaseError(operation));
        if (sqlite3_bind_int64(stmt.get(), 1, static_cast<sqlite3_int64>(id)) != SQLITE_OK)
            return std::unexpected(databaseError(operation));
        const int step = sqlite3_step(stmt.get());
        if (step == SQLITE_DONE) return false;
        if (step != SQLITE_ROW) return std::unexpected(databaseError(operation));
        auto parsed = parseAssetRecord<T>(reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 0)),
                                          sqlite3_column_bytes(stmt.get(), 0), id);
        if (!parsed) return std::unexpected(PathToUTF8(path) + ": " + operation + ": " + parsed.error());
        outAsset = std::move(*parsed);
        return true;
    }

    template <typename T>
    Result<void> AssetDBSQLite::loadCleanAssetsFromDB(const std::string &table, std::map<AssetHandle, T> &cache,
                                              const std::set<AssetHandle> &deleted)
    {
        const std::string sql = "SELECT id, data FROM " + table;
        sqlite3_stmt *raw = nullptr;
        const int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &raw, nullptr);
        Statement stmt(raw, sqlite3_finalize);
        if (rc != SQLITE_OK) return std::unexpected(databaseError(sql));
        int step;
        while ((step = sqlite3_step(stmt.get())) == SQLITE_ROW)
        {
            const AssetHandle id = static_cast<AssetHandle>(sqlite3_column_int64(stmt.get(), 0));
            if (cache.contains(id) || deleted.contains(id)) continue;
            auto parsed = parseAssetRecord<T>(reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 1)),
                                              sqlite3_column_bytes(stmt.get(), 1), id);
            if (!parsed)
                return std::unexpected(PathToUTF8(path) + ": read " + table + " asset " +
                                       std::to_string(id) + ": " + parsed.error());
            cache.emplace(id, std::move(*parsed));
        }
        if (step != SQLITE_DONE) return std::unexpected(databaseError(sql));
        return {};
    }

    template <typename T>
    Result<void> AssetDBSQLite::flushChanges(const std::string &table, std::map<AssetHandle, T> &cache,
                                     const std::set<AssetHandle> &dirty,
                                     const std::set<AssetHandle> &deleted)
    {
        for (const AssetHandle id : dirty)
            if (auto result = saveToDB(table, id, cache.at(id).ToJSON().dump()); !result) return result;
        for (const AssetHandle id : deleted)
            if (auto result = removeFromDB(table, id); !result) return result;
        return {};
    }

    template Result<bool> AssetDBSQLite::loadAssetFromDB<TextureAsset>(const std::string &, AssetHandle, TextureAsset &);
    template Result<bool> AssetDBSQLite::loadAssetFromDB<MeshAsset>(const std::string &, AssetHandle, MeshAsset &);
    template Result<bool> AssetDBSQLite::loadAssetFromDB<VFShaderAsset>(const std::string &, AssetHandle, VFShaderAsset &);
    template Result<bool> AssetDBSQLite::loadAssetFromDB<ComputeShaderAsset>(const std::string &, AssetHandle, ComputeShaderAsset &);
    template Result<bool> AssetDBSQLite::loadAssetFromDB<MaterialAsset>(const std::string &, AssetHandle, MaterialAsset &);
    template Result<bool> AssetDBSQLite::loadAssetFromDB<SkeletonAsset>(const std::string &, AssetHandle, SkeletonAsset &);
    template Result<bool> AssetDBSQLite::loadAssetFromDB<AnimationAsset>(const std::string &, AssetHandle, AnimationAsset &);
    template Result<bool> AssetDBSQLite::loadAssetFromDB<SceneAsset>(const std::string &, AssetHandle, SceneAsset &);
    template Result<bool> AssetDBSQLite::loadAssetFromDB<FontAsset>(const std::string &, AssetHandle, FontAsset &);
    template Result<bool> AssetDBSQLite::loadAssetFromDB<AudioClipAsset>(const std::string &, AssetHandle, AudioClipAsset &);

    template Result<void> AssetDBSQLite::loadCleanAssetsFromDB<TextureAsset>(const std::string &, std::map<AssetHandle, TextureAsset> &, const std::set<AssetHandle> &);
    template Result<void> AssetDBSQLite::loadCleanAssetsFromDB<MeshAsset>(const std::string &, std::map<AssetHandle, MeshAsset> &, const std::set<AssetHandle> &);
    template Result<void> AssetDBSQLite::loadCleanAssetsFromDB<VFShaderAsset>(const std::string &, std::map<AssetHandle, VFShaderAsset> &, const std::set<AssetHandle> &);
    template Result<void> AssetDBSQLite::loadCleanAssetsFromDB<ComputeShaderAsset>(const std::string &, std::map<AssetHandle, ComputeShaderAsset> &, const std::set<AssetHandle> &);
    template Result<void> AssetDBSQLite::loadCleanAssetsFromDB<MaterialAsset>(const std::string &, std::map<AssetHandle, MaterialAsset> &, const std::set<AssetHandle> &);
    template Result<void> AssetDBSQLite::loadCleanAssetsFromDB<SkeletonAsset>(const std::string &, std::map<AssetHandle, SkeletonAsset> &, const std::set<AssetHandle> &);
    template Result<void> AssetDBSQLite::loadCleanAssetsFromDB<AnimationAsset>(const std::string &, std::map<AssetHandle, AnimationAsset> &, const std::set<AssetHandle> &);
    template Result<void> AssetDBSQLite::loadCleanAssetsFromDB<SceneAsset>(const std::string &, std::map<AssetHandle, SceneAsset> &, const std::set<AssetHandle> &);
    template Result<void> AssetDBSQLite::loadCleanAssetsFromDB<FontAsset>(const std::string &, std::map<AssetHandle, FontAsset> &, const std::set<AssetHandle> &);
    template Result<void> AssetDBSQLite::loadCleanAssetsFromDB<AudioClipAsset>(const std::string &, std::map<AssetHandle, AudioClipAsset> &, const std::set<AssetHandle> &);

    template Result<void> AssetDBSQLite::flushChanges<TextureAsset>(const std::string &, std::map<AssetHandle, TextureAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template Result<void> AssetDBSQLite::flushChanges<MeshAsset>(const std::string &, std::map<AssetHandle, MeshAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template Result<void> AssetDBSQLite::flushChanges<VFShaderAsset>(const std::string &, std::map<AssetHandle, VFShaderAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template Result<void> AssetDBSQLite::flushChanges<ComputeShaderAsset>(const std::string &, std::map<AssetHandle, ComputeShaderAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template Result<void> AssetDBSQLite::flushChanges<MaterialAsset>(const std::string &, std::map<AssetHandle, MaterialAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template Result<void> AssetDBSQLite::flushChanges<SkeletonAsset>(const std::string &, std::map<AssetHandle, SkeletonAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template Result<void> AssetDBSQLite::flushChanges<AnimationAsset>(const std::string &, std::map<AssetHandle, AnimationAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template Result<void> AssetDBSQLite::flushChanges<SceneAsset>(const std::string &, std::map<AssetHandle, SceneAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template Result<void> AssetDBSQLite::flushChanges<FontAsset>(const std::string &, std::map<AssetHandle, FontAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template Result<void> AssetDBSQLite::flushChanges<AudioClipAsset>(const std::string &, std::map<AssetHandle, AudioClipAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);

}
