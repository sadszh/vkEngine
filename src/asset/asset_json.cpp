#include <asset/asset.hpp>
#include <json_validation.hpp>

namespace vke_common
{
    Result<nlohmann::json> LoadJSON(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) return std::unexpected(PathToUTF8(path) + ": cannot open JSON file");
        auto json = nlohmann::json::parse(input, nullptr, false);
        if (input.bad()) return std::unexpected(PathToUTF8(path) + ": cannot read JSON file");
        if (json.is_discarded()) return std::unexpected(PathToUTF8(path) + ": invalid JSON");
        return json;
    }

    Result<void> ValidateAssetJSON(const nlohmann::json &json)
    {
        using namespace json_validation;
        auto common = Object(json).Require({"type", "id", "name", "path"})
            .Unsigneds({"type"}, ASSET_CNT_FLAG - 1)
            .Unsigneds({"id"}, UINT64_MAX - 1).Strings({"name"}).RelativePaths({"path"}).Result();
        if (!common) return common;
        if (json["id"] == 0) return std::unexpected("id must be nonzero");

        switch (json["type"].get<AssetType>())
        {
        case ASSET_TEXTURE:
            return Object(json).Unsigneds({"format", "layout", "minFilter", "magFilter", "addressMode", "rasterWidth", "rasterHeight"}, INT32_MAX)
                .Unsigneds({"usage"}, UINT32_MAX).Booleans({"anisotropy", "genMipMap"}).Result();
        case ASSET_VF_SHADER:
            return Object(json).Require({"fragPath"}).RelativePaths({"fragPath"}).Result();
        case ASSET_ANIMATION:
            return Object(json).Booleans({"hasRootMotion"}).Result();
        case ASSET_FONT:
            return Object(json).Unsigneds({"pixelSize", "characterCount", "firstCodepoint"}, UINT32_MAX)
                .Strings({"characters"}).Result();
        case ASSET_MATERIAL:
            break;
        default:
            return {};
        }

        auto material = Object(json).Require({"shader", "textures", "bindingInfos"})
            .Unsigneds({"shader"}).Strings({"renderMode", "blendMode"}).Result();
        if (!material) return material;
        const auto renderMode = json.value("renderMode", "opaque");
        if (renderMode != "opaque" && renderMode != "cutoff" && renderMode != "blend")
            return std::unexpected("renderMode must be opaque, cutoff or blend");
        const auto blendMode = json.value("blendMode", "alpha");
        if (blendMode != "alpha" && blendMode != "premultipliedAlpha" && blendMode != "additive")
            return std::unexpected("blendMode must be alpha, premultipliedAlpha or additive");

        const auto &textures = json["textures"];
        if (!textures.is_array()) return std::unexpected("textures must be an array");
        for (size_t i = 0; i < textures.size(); ++i)
            if (!Unsigned(textures[i]))
                return std::unexpected("invalid textures[" + std::to_string(i) + "]");

        const auto &bindings = json["bindingInfos"];
        if (!bindings.is_array()) return std::unexpected("bindingInfos must be an array");
        size_t textureCount = 0;
        for (size_t i = 0; i < bindings.size(); ++i)
        {
            const auto &binding = bindings[i];
            auto valid = Object(binding).Require({"binding", "offset", "cnt"})
                .Unsigneds({"binding", "offset", "cnt"}, UINT32_MAX).Result();
            if (!valid) return std::unexpected("bindingInfos[" + std::to_string(i) + "]: " + valid.error());
            const auto count = binding["cnt"].get<uint32_t>();
            if (count > textures.size() - textureCount)
                return std::unexpected("bindingInfos texture count exceeds textures size");
            textureCount += count;
        }

        const auto constants = json.find("pushConstantInfos");
        if (constants == json.end()) return {};
        if (!constants->is_array()) return std::unexpected("pushConstantInfos must be an array");
        for (size_t i = 0; i < constants->size(); ++i)
        {
            const auto &constant = (*constants)[i];
            const auto context = "pushConstantInfos[" + std::to_string(i) + "]: ";
            auto valid = Object(constant).Require({"component_cnt", "component_type", "data", "offset"})
                .Unsigneds({"component_cnt"}, UINT32_MAX / sizeof(uint32_t))
                .Unsigneds({"offset"}, UINT32_MAX).Strings({"component_type"}).Result();
            if (!valid) return std::unexpected(context + valid.error());
            const auto &data = constant["data"];
            if (!data.is_array() || data.size() != constant["component_cnt"].get<uint32_t>())
                return std::unexpected(context + "data size must match component_cnt");
            const auto type = constant["component_type"].get<std::string>();
            if (type != "float" && type != "int")
                return std::unexpected(context + "component_type must be float or int");
            for (const auto &value : data)
            {
                if (type == "float")
                {
                    if (!Number(value)) return std::unexpected(context + "data must contain finite float values");
                }
                else if (!value.is_number_integer() || value.get<double>() < INT32_MIN || value.get<double>() > INT32_MAX)
                    return std::unexpected(context + "data must contain int32 values");
            }
        }
        return {};
    }

    Result<void> ValidateAssetListJSON(const nlohmann::json &json)
    {
        if (!json.is_array()) return std::unexpected("asset list must be an array");
        for (size_t i = 0; i < json.size(); ++i)
            if (auto valid = ValidateAssetJSON(json[i]); !valid)
                return std::unexpected("asset[" + std::to_string(i) + "]: " + valid.error());
        return {};
    }
}
