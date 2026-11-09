#include <asset/asset_db_json.hpp>

namespace vke_common
{
    Result<void> AssetDBJSON::ClearAll()
    {
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
        return {};
    }

#define LOAD_LUT_CASE(tpid, tp, assets)          \
    case tpid:                                   \
        assets[id] = tp(id, asset);              \
        ids[type] = std::max(ids[type], id + 1); \
        break;

    Result<void> AssetDBJSON::bulkLoad(const std::filesystem::path &pth)
    {
        auto loaded = LoadJSON(pth);
        if (!loaded)
            return std::unexpected(loaded.error());
        const auto &json = *loaded;
        if (auto valid = ValidateAssetListJSON(json); !valid)
            return std::unexpected(PathToUTF8(pth) + ": " + valid.error());
        for (const auto &asset : json)
        {
            AssetType type = asset["type"];
            AssetHandle id = asset["id"];
            switch (type)
            {
                LOAD_LUT_CASE(ASSET_TEXTURE, TextureAsset, textureCache)
                LOAD_LUT_CASE(ASSET_MESH, MeshAsset, meshCache)
                LOAD_LUT_CASE(ASSET_VF_SHADER, VFShaderAsset, vfShaderCache)
                LOAD_LUT_CASE(ASSET_COMPUTE_SHADER, ComputeShaderAsset, computeShaderCache)
                LOAD_LUT_CASE(ASSET_MATERIAL, MaterialAsset, materialCache)
                LOAD_LUT_CASE(ASSET_SKELETON, SkeletonAsset, skeletonCache)
                LOAD_LUT_CASE(ASSET_ANIMATION, AnimationAsset, animationCache)
                LOAD_LUT_CASE(ASSET_SCENE, SceneAsset, sceneCache)
                LOAD_LUT_CASE(ASSET_FONT, FontAsset, fontCache)
                LOAD_LUT_CASE(ASSET_AUDIO_CLIP, AudioClipAsset, audioCache)
            default:
                break;
            }
        }
        return {};
    }

#define ASSET_TO_JSON(cache)                \
    for (auto &kv : cache)                  \
        if (kv.first >= CUSTOM_ASSET_ID_ST) \
            ret.push_back(kv.second.ToJSON());

    Result<void> AssetDBJSON::saveAll(const std::filesystem::path &pth)
    {
        nlohmann::json ret = nlohmann::json::array();

        ASSET_TO_JSON(textureCache)
        ASSET_TO_JSON(meshCache)
        ASSET_TO_JSON(vfShaderCache)
        ASSET_TO_JSON(computeShaderCache)
        ASSET_TO_JSON(materialCache)
        ASSET_TO_JSON(skeletonCache)
        ASSET_TO_JSON(animationCache)
        ASSET_TO_JSON(fontCache)
        ASSET_TO_JSON(audioCache)
        for (auto &kv : sceneCache)
            ret.push_back(kv.second.ToJSON());

        const std::string data = ret.dump(4);
        auto temporary = pth;
        temporary += ".tmp";
        std::ofstream ofs(temporary, std::ios::out | std::ios::binary | std::ios::noreplace);
        if (!ofs)
            return std::unexpected(PathToUTF8(temporary) + ": cannot create temporary asset database");
        ofs << data;
        ofs.close();
        std::string error;
        if (ofs.fail()) error = PathToUTF8(temporary) + ": failed to write asset database";
        else
        {
            std::error_code replaceError;
            std::filesystem::rename(temporary, pth, replaceError);
            if (!replaceError) return {};
            error = PathToUTF8(pth) + ": replace asset database: " + replaceError.message();
        }

        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        if (cleanupError) error += "; remove temporary file: " + cleanupError.message();
        return std::unexpected(std::move(error));
    }
}
