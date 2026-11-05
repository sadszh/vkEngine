#include <asset/asset_db_sqlite.hpp>
#include <sqlite/sqlite3.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <vector>
#include <string>

namespace vke_common
{

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

    bool AssetDBSQLite::execSQL(const std::string &sql)
    {
        char *errMsg = nullptr;
        const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK)
        {
            const std::string err = errMsg ? errMsg : "unknown error";
            sqlite3_free(errMsg);
            VKE_LOG_ERROR("AssetDBSQLite SQL error: {}\nSQL: {}", err, sql);
            return false;
        }
        return true;
    }

    bool AssetDBSQLite::createTables()
    {
        for (const auto &tbl : TABLE_NAMES)
        {
            const std::string sql = std::string("CREATE TABLE IF NOT EXISTS ") + tbl +
                                    " (id INTEGER PRIMARY KEY, data TEXT NOT NULL)";
            if (!execSQL(sql))
                return false;
        }
        return true;
    }

    bool AssetDBSQLite::updateNextIds()
    {
        for (int type = 0; type < ASSET_CNT_FLAG; ++type)
        {
            const std::string sql = "SELECT MAX(id) FROM " + std::string(TABLE_NAMES[type]);
            sqlite3_stmt *stmt = nullptr;
            const int prepareResult = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
            if (prepareResult != SQLITE_OK)
            {
                VKE_LOG_ERROR("AssetDBSQLite::updateNextIds - prepare failed for table {}: {}",
                              TABLE_NAMES[type], sqlite3_errmsg(db));
                sqlite3_finalize(stmt);
                return false;
            }

            const int stepResult = sqlite3_step(stmt);
            if (stepResult != SQLITE_ROW)
            {
                VKE_LOG_ERROR("AssetDBSQLite::updateNextIds - query failed for table {}: {}",
                              TABLE_NAMES[type], sqlite3_errmsg(db));
                sqlite3_finalize(stmt);
                return false;
            }

            ids[type] = stID;
            if (sqlite3_column_type(stmt, 0) != SQLITE_NULL)
            {
                const sqlite3_int64 maxId = sqlite3_column_int64(stmt, 0);
                if (maxId >= 0)
                    ids[type] = std::max(stID, static_cast<AssetHandle>(maxId) + 1);
            }
            sqlite3_finalize(stmt);
        }
        return true;
    }

    bool AssetDBSQLite::saveToDB(const std::string &table, AssetHandle id, const std::string &jsonData)
    {
        const std::string sql = "INSERT OR REPLACE INTO " + table + " (id, data) VALUES (?1, ?2)";
        sqlite3_stmt *stmt = nullptr;

        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            VKE_LOG_ERROR("AssetDBSQLite::saveToDB - prepare failed: {}", sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return false;
        }

        if (sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(id)) != SQLITE_OK ||
            sqlite3_bind_text(stmt, 2, jsonData.c_str(), static_cast<int>(jsonData.size()), SQLITE_TRANSIENT) != SQLITE_OK)
        {
            VKE_LOG_ERROR("AssetDBSQLite::saveToDB - bind failed: {}", sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return false;
        }

        if (sqlite3_step(stmt) != SQLITE_DONE)
        {
            VKE_LOG_ERROR("AssetDBSQLite::saveToDB - step failed: {}", sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return false;
        }

        return sqlite3_finalize(stmt) == SQLITE_OK;
    }

    bool AssetDBSQLite::removeFromDB(const std::string &table, AssetHandle id)
    {
        const std::string sql = "DELETE FROM " + table + " WHERE id = ?1";
        sqlite3_stmt *stmt = nullptr;

        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            VKE_LOG_ERROR("AssetDBSQLite::removeFromDB - prepare failed: {}", sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return false;
        }

        if (sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(id)) != SQLITE_OK)
        {
            VKE_LOG_ERROR("AssetDBSQLite::removeFromDB - bind failed: {}", sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return false;
        }
        if (sqlite3_step(stmt) != SQLITE_DONE)
        {
            VKE_LOG_ERROR("AssetDBSQLite::removeFromDB - step failed: {}", sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return false;
        }
        return sqlite3_finalize(stmt) == SQLITE_OK;
    }

    void AssetDBSQLite::Init()
    {
        const int rc = sqlite3_open_v2(
            PathToUTF8(path).c_str(),
            &db,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
            nullptr);

        if (rc != SQLITE_OK)
        {
            VKE_FATAL("AssetDBSQLite::Init - failed to open database '{}': {}",
                      path.string().c_str(), sqlite3_errmsg(db));
        }

        VKE_FATAL_IF(!createTables(), "AssetDBSQLite::Init - failed to create tables")
        VKE_FATAL_IF(!updateNextIds(), "AssetDBSQLite::Init - failed to initialize asset IDs")
    }

    bool AssetDBSQLite::ClearAll()
    {
        if (!execSQL("BEGIN IMMEDIATE"))
            return false;

        bool cleared = true;
        for (const auto &tbl : TABLE_NAMES)
        {
            if (!execSQL("DELETE FROM " + std::string(tbl)))
            {
                cleared = false;
                break;
            }
        }

        if (!cleared || !execSQL("COMMIT"))
        {
            execSQL("ROLLBACK");
            return false;
        }

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
        return true;
    }

    bool AssetDBSQLite::BulkLoad(const std::filesystem::path &pth)
    {
        const nlohmann::json assets = LoadJSON(pth);

        if (!assets.is_array())
        {
            VKE_LOG_ERROR("AssetDBSQLite::BulkLoad - '{}' must contain a JSON array",
                          pth.string());
            return false;
        }

        bool valid = true;
        for (const auto &asset : assets)
        {
            if (!asset.is_object() || !asset.contains("type") || !asset["type"].is_number_integer() ||
                !asset.contains("id") || !asset["id"].is_number_integer())
            {
                VKE_LOG_ERROR("AssetDBSQLite::BulkLoad - asset in '{}' must have integer type and id fields",
                              pth.string());
                valid = false;
                break;
            }

            const int type = asset["type"].get<int>();
            const AssetHandle id = asset["id"].get<AssetHandle>();
            if (type < 0 || type >= ASSET_CNT_FLAG)
            {
                VKE_LOG_ERROR("AssetDBSQLite::BulkLoad - asset {} has invalid type {}",
                              id, type);
                valid = false;
                break;
            }

            // Validate the complete batch before changing any cache.
            switch (type)
            {
            case ASSET_TEXTURE:
                (void)TextureAsset(id, asset);
                break;
            case ASSET_MESH:
                (void)MeshAsset(id, asset);
                break;
            case ASSET_VF_SHADER:
                (void)VFShaderAsset(id, asset);
                break;
            case ASSET_COMPUTE_SHADER:
                (void)ComputeShaderAsset(id, asset);
                break;
            case ASSET_MATERIAL:
                (void)MaterialAsset(id, asset);
                break;
            case ASSET_SKELETON:
                (void)SkeletonAsset(id, asset);
                break;
            case ASSET_ANIMATION:
                (void)AnimationAsset(id, asset);
                break;
            case ASSET_SCENE:
                (void)SceneAsset(id, asset);
                break;
            case ASSET_FONT:
                (void)FontAsset(id, asset);
                break;
            case ASSET_AUDIO_CLIP:
                (void)AudioClipAsset(id, asset);
                break;
            }
        }

        if (!valid)
            return false;

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
        return true;
    }

    bool AssetDBSQLite::SyncAll()
    {
        if (!execSQL("BEGIN IMMEDIATE"))
            return false;

        const bool flushed =
            flushChanges("texture_assets", textureCache, dirtyTexture, deletedTexture) &&
            flushChanges("mesh_assets", meshCache, dirtyMesh, deletedMesh) &&
            flushChanges("vf_shader_assets", vfShaderCache, dirtyVFShader, deletedVFShader) &&
            flushChanges("compute_shader_assets", computeShaderCache, dirtyComputeShader, deletedComputeShader) &&
            flushChanges("material_assets", materialCache, dirtyMaterial, deletedMaterial) &&
            flushChanges("skeleton_assets", skeletonCache, dirtySkeleton, deletedSkeleton) &&
            flushChanges("animation_assets", animationCache, dirtyAnimation, deletedAnimation) &&
            flushChanges("scene_assets", sceneCache, dirtyScene, deletedScene) &&
            flushChanges("font_assets", fontCache, dirtyFont, deletedFont) &&
            flushChanges("audio_clip_assets", audioCache, dirtyAudio, deletedAudio);

        if (!flushed || !execSQL("COMMIT"))
        {
            execSQL("ROLLBACK");
            return false;
        }

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
        return true;
    }

    template <typename T>
    bool AssetDBSQLite::loadAssetFromDB(const std::string &table, AssetHandle id, T &outAsset)
    {
        const std::string sql = "SELECT data FROM " + table + " WHERE id = ?1";
        sqlite3_stmt *stmt = nullptr;

        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            sqlite3_finalize(stmt);
            return false;
        }

        sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(id));

        bool found = false;
        const int stepResult = sqlite3_step(stmt);
        if (stepResult == SQLITE_ROW)
        {
            const char *raw = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
            const int rawBytes = sqlite3_column_bytes(stmt, 0);

            if (raw != nullptr && rawBytes > 0)
            {
                const nlohmann::json &json = nlohmann::json::parse(raw, raw + rawBytes);
                outAsset = T(id, json);
                found = true;
            }
        }
        else if (stepResult != SQLITE_DONE)
        {
            VKE_LOG_ERROR("AssetDBSQLite::loadAssetFromDB - query failed for table {}: {}",
                          table, sqlite3_errmsg(db));
        }

        sqlite3_finalize(stmt);
        return found;
    }

    template <typename T>
    bool AssetDBSQLite::loadCleanAssetsFromDB(const std::string &table, std::map<AssetHandle, T> &cache,
                                              const std::set<AssetHandle> &dirty,
                                              const std::set<AssetHandle> &deleted)
    {
        const std::string sql = "SELECT id, data FROM " + table;
        sqlite3_stmt *stmt = nullptr;

        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            VKE_LOG_ERROR("AssetDBSQLite::loadCleanAssetsFromDB - prepare failed for table {}: {}",
                          table, sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return false;
        }

        int stepResult = SQLITE_OK;
        while ((stepResult = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            const AssetHandle id = static_cast<AssetHandle>(sqlite3_column_int64(stmt, 0));
            if (cache.contains(id) || dirty.contains(id) || deleted.contains(id))
                continue;

            const char *raw = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
            const int rawBytes = sqlite3_column_bytes(stmt, 1);
            if (raw == nullptr || rawBytes <= 0)
                continue;

            const nlohmann::json &json = nlohmann::json::parse(raw, raw + rawBytes);
            cache[id] = T(id, json);
        }

        if (stepResult != SQLITE_DONE)
        {
            const std::string error = sqlite3_errmsg(db);
            sqlite3_finalize(stmt);
            VKE_LOG_ERROR("AssetDBSQLite::loadCleanAssetsFromDB - query failed for table {}: {}",
                          table, error);
            return false;
        }

        return sqlite3_finalize(stmt) == SQLITE_OK;
    }

    template <typename T>
    bool AssetDBSQLite::flushChanges(const std::string &table, std::map<AssetHandle, T> &cache,
                                     const std::set<AssetHandle> &dirty,
                                     const std::set<AssetHandle> &deleted)
    {
        for (const AssetHandle id : dirty)
        {
            if (deleted.contains(id))
                continue;
            auto it = cache.find(id);
            if (it == cache.end())
            {
                VKE_LOG_ERROR("AssetDBSQLite::flushChanges - dirty asset {} is missing from table {} cache",
                              id, table);
                return false;
            }
            if (!saveToDB(table, id, it->second.ToJSON().dump()))
                return false;
        }

        for (const AssetHandle id : deleted)
        {
            if (!removeFromDB(table, id))
                return false;
        }
        return true;
    }

    template bool AssetDBSQLite::loadAssetFromDB<TextureAsset>(const std::string &, AssetHandle, TextureAsset &);
    template bool AssetDBSQLite::loadAssetFromDB<MeshAsset>(const std::string &, AssetHandle, MeshAsset &);
    template bool AssetDBSQLite::loadAssetFromDB<VFShaderAsset>(const std::string &, AssetHandle, VFShaderAsset &);
    template bool AssetDBSQLite::loadAssetFromDB<ComputeShaderAsset>(const std::string &, AssetHandle, ComputeShaderAsset &);
    template bool AssetDBSQLite::loadAssetFromDB<MaterialAsset>(const std::string &, AssetHandle, MaterialAsset &);
    template bool AssetDBSQLite::loadAssetFromDB<SkeletonAsset>(const std::string &, AssetHandle, SkeletonAsset &);
    template bool AssetDBSQLite::loadAssetFromDB<AnimationAsset>(const std::string &, AssetHandle, AnimationAsset &);
    template bool AssetDBSQLite::loadAssetFromDB<SceneAsset>(const std::string &, AssetHandle, SceneAsset &);
    template bool AssetDBSQLite::loadAssetFromDB<FontAsset>(const std::string &, AssetHandle, FontAsset &);
    template bool AssetDBSQLite::loadAssetFromDB<AudioClipAsset>(const std::string &, AssetHandle, AudioClipAsset &);

    template bool AssetDBSQLite::loadCleanAssetsFromDB<TextureAsset>(const std::string &, std::map<AssetHandle, TextureAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template bool AssetDBSQLite::loadCleanAssetsFromDB<MeshAsset>(const std::string &, std::map<AssetHandle, MeshAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template bool AssetDBSQLite::loadCleanAssetsFromDB<VFShaderAsset>(const std::string &, std::map<AssetHandle, VFShaderAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template bool AssetDBSQLite::loadCleanAssetsFromDB<ComputeShaderAsset>(const std::string &, std::map<AssetHandle, ComputeShaderAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template bool AssetDBSQLite::loadCleanAssetsFromDB<MaterialAsset>(const std::string &, std::map<AssetHandle, MaterialAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template bool AssetDBSQLite::loadCleanAssetsFromDB<SkeletonAsset>(const std::string &, std::map<AssetHandle, SkeletonAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template bool AssetDBSQLite::loadCleanAssetsFromDB<AnimationAsset>(const std::string &, std::map<AssetHandle, AnimationAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template bool AssetDBSQLite::loadCleanAssetsFromDB<SceneAsset>(const std::string &, std::map<AssetHandle, SceneAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template bool AssetDBSQLite::loadCleanAssetsFromDB<FontAsset>(const std::string &, std::map<AssetHandle, FontAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template bool AssetDBSQLite::loadCleanAssetsFromDB<AudioClipAsset>(const std::string &, std::map<AssetHandle, AudioClipAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);

    template bool AssetDBSQLite::flushChanges<TextureAsset>(const std::string &, std::map<AssetHandle, TextureAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template bool AssetDBSQLite::flushChanges<MeshAsset>(const std::string &, std::map<AssetHandle, MeshAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template bool AssetDBSQLite::flushChanges<VFShaderAsset>(const std::string &, std::map<AssetHandle, VFShaderAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template bool AssetDBSQLite::flushChanges<ComputeShaderAsset>(const std::string &, std::map<AssetHandle, ComputeShaderAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template bool AssetDBSQLite::flushChanges<MaterialAsset>(const std::string &, std::map<AssetHandle, MaterialAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template bool AssetDBSQLite::flushChanges<SkeletonAsset>(const std::string &, std::map<AssetHandle, SkeletonAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template bool AssetDBSQLite::flushChanges<AnimationAsset>(const std::string &, std::map<AssetHandle, AnimationAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template bool AssetDBSQLite::flushChanges<SceneAsset>(const std::string &, std::map<AssetHandle, SceneAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template bool AssetDBSQLite::flushChanges<FontAsset>(const std::string &, std::map<AssetHandle, FontAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);
    template bool AssetDBSQLite::flushChanges<AudioClipAsset>(const std::string &, std::map<AssetHandle, AudioClipAsset> &, const std::set<AssetHandle> &, const std::set<AssetHandle> &);

}
