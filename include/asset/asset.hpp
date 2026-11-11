#ifndef ASSET_H
#define ASSET_H
#include <render/material.hpp>
#include <render/mesh.hpp>
#include <animation.hpp>
#include <font.hpp>
#include <audio/audio_clip.hpp>
#include <nlohmann/json.hpp>

#include <physics/physics.hpp>

#include <fstream>
#include <iostream>
#include <map>
#include <cstring>
#include <unordered_map>
#include <filesystem>

namespace vke_common
{
    struct SceneData;
    extern const std::filesystem::path BuiltinAssetLUTPath;

    const AssetHandle CUSTOM_ASSET_ID_ST = 1024;

    const AssetHandle BUILTIN_TEXTURE_DEFAULT_ID = 1;
    const AssetHandle BUILTIN_TEXTURE_BRDF_LUT_ID = 2;

    const AssetHandle BUILTIN_MESH_PLANE_ID = 1;
    const AssetHandle BUILTIN_MESH_CUBE_ID = 2;
    const AssetHandle BUILTIN_MESH_SPHERE_ID = 3;
    const AssetHandle BUILTIN_MESH_CYLINDER_ID = 4;
    const AssetHandle BUILTIN_MESH_MONKEY_ID = 5;

    const AssetHandle BUILTIN_VFSHADER_DEFAULT_ID = 1;
    const AssetHandle BUILTIN_VFSHADER_SKYBOX_ID = 2;
    const AssetHandle BUILTIN_VFSHADER_DEFAULT_MULTI_ID = 3;
    const AssetHandle BUILTIN_VFSHADER_DEFAULT_SKIN_ID = 4;
    const AssetHandle BUILTIN_VFSHADER_DEFERRED_LIGHTING_ID = 5;
    const AssetHandle BUILTIN_VFSHADER_TEXT_ID = 6;
    const AssetHandle BUILTIN_VFSHADER_SHADOW_ID = 7;
    const AssetHandle BUILTIN_VFSHADER_SHADOW_SKIN_ID = 8;
    const AssetHandle BUILTIN_VFSHADER_TONE_MAPPING_ID = 9;
    const AssetHandle BUILTIN_VFSHADER_BLOOM_ID = 10;
    const AssetHandle BUILTIN_VFSHADER_SSAO_ID = 11;
    const AssetHandle BUILTIN_VFSHADER_SSAO_BLUR_ID = 12;
    const AssetHandle BUILTIN_VFSHADER_ATMOSPHERE_ID = 13;
    const AssetHandle BUILTIN_VFSHADER_UI_IMAGE_ID = 14;
    const AssetHandle BUILTIN_COMPUTE_SHADER_SKYLUT_ID = 1;
    const AssetHandle BUILTIN_COMPUTE_SHADER_LIGHTCULL_ID = 2;
    const AssetHandle BUILTIN_COMPUTE_SHADER_IBL_LUT_ID = 3;
    const AssetHandle BUILTIN_COMPUTE_SHADER_ATMOSPHERE_LUT_ID = 4;

    const AssetHandle BUILTIN_MATERIAL_DEFAULT_ID = 1;
    const AssetHandle BUILTIN_MATERIAL_SKYBOX_ID = 2;

    const AssetHandle BUILTIN_FONT_ARIAL_ID = 1;

    enum AssetType
    {
        ASSET_TEXTURE,
        ASSET_MESH,
        ASSET_VF_SHADER,
        ASSET_COMPUTE_SHADER,
        ASSET_MATERIAL,
        ASSET_SKELETON,
        ASSET_ANIMATION,
        ASSET_SCENE,
        ASSET_FONT,
        ASSET_AUDIO_CLIP,
        ASSET_CNT_FLAG
    };

    const std::string AssetTypeToName[] = {"Texture", "Mesh", "VFShader", "ComputeShader",
                                           "Material", "Skeleton", "Animation", "Scene", "Font", "AudioClip"};

    Result<void> ReadFile(const std::filesystem::path &filename, std::vector<char> &buffer);

    [[nodiscard]] Result<nlohmann::json> LoadJSON(const std::filesystem::path &pth);
    [[nodiscard]] Result<void> ValidateAssetJSON(const nlohmann::json &json);
    [[nodiscard]] Result<void> ValidateAssetListJSON(const nlohmann::json &json);

    template <AssetType TID, typename T, typename VT>
    class Asset
    {
    public:
        static const AssetType type = TID;
        AssetHandle id;
        std::string name;
        std::filesystem::path path;
        std::shared_ptr<VT> val;

        Asset() : id(0), val(nullptr) {}

        Asset(AssetHandle id, const nlohmann::json &json)
            : id(id), name(json["name"]), path(PathFromUTF8(json["path"].get<std::string>())), val(nullptr) {}
        Asset(AssetHandle id, const std::string &nm, const std::string &pth)
            : id(id), name(nm), path(PathFromUTF8(pth)), val(nullptr) {}

        Asset<TID, T, VT> &operator=(const Asset<TID, T, VT> &ano)
        {
            id = ano.id;
            name = ano.name;
            path = ano.path;
            val = ano.val;
            return *this;
        }

        nlohmann::json ToJSON() const
        {
            nlohmann::json json = {
                {"type", type},
                {"id", id},
                {"name", name},
                {"path", PathToUTF8(path)}};
            static_cast<const T *>(this)->writeJSON(json);
            return json;
        }

    protected:
        void writeJSON(nlohmann::json &) const {}
    };

#define DEFAULT_CONSTRUCTOR(type) \
    type(AssetHandle id, const nlohmann::json &json) : Asset(id, json) {}

#define DEFAULT_CONSTRUCTOR2(type) \
    type(AssetHandle id, const std::string &nm, const std::string &pth) : Asset(id, nm, pth) {}

#define LEAF_ASSET_TYPE(type, typeid, valtype)       \
    class type : public Asset<typeid, type, valtype> \
    {                                                \
    public:                                          \
        type() {}                                    \
        DEFAULT_CONSTRUCTOR(type)                    \
        DEFAULT_CONSTRUCTOR2(type)                   \
    };

    LEAF_ASSET_TYPE(MeshAsset, ASSET_MESH, vke_render::Mesh)
    LEAF_ASSET_TYPE(ComputeShaderAsset, ASSET_COMPUTE_SHADER, vke_render::ShaderModuleSet)
    LEAF_ASSET_TYPE(SkeletonAsset, ASSET_SKELETON, vke_common::Skeleton)
    LEAF_ASSET_TYPE(SceneAsset, ASSET_SCENE, const SceneData);

    class AnimationAsset : public Asset<ASSET_ANIMATION, AnimationAsset, vke_common::Animation>
    {
    public:
        bool hasRootMotion;

        AnimationAsset() : hasRootMotion(false) {}

        AnimationAsset(AssetHandle id, const nlohmann::json &json)
            : Asset(id, json), hasRootMotion(json.value("hasRootMotion", false)) {}

        AnimationAsset(AssetHandle id, const std::string &nm, const std::string &pth)
            : Asset(id, nm, pth), hasRootMotion(false) {}

        void writeJSON(nlohmann::json &json) const
        {
            json["hasRootMotion"] = hasRootMotion;
        }
    };

    LEAF_ASSET_TYPE(AudioClipAsset, ASSET_AUDIO_CLIP, vke_audio::AudioClip);

    class TextureAsset : public Asset<ASSET_TEXTURE, TextureAsset, vke_render::Texture2D>
    {
    public:
        VkFormat format;
        VkImageUsageFlags usage;
        VkImageLayout layout;
        VkFilter minFilter;
        VkFilter magFilter;
        VkSamplerAddressMode addressMode;
        bool anisotropyEnable;
        bool generateMipMap;
        int rasterWidth = 0;
        int rasterHeight = 0;

        TextureAsset() {}

        TextureAsset(AssetHandle id, const nlohmann::json &json) : Asset(id, json)
        {
            format = json.contains("format") ? (VkFormat)json["format"] : VK_FORMAT_R8G8B8A8_SRGB;
            usage = json.contains("usage") ? (VkImageUsageFlags)json["usage"] : VK_IMAGE_USAGE_SAMPLED_BIT;
            layout = json.contains("layout") ? (VkImageLayout)json["layout"] : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            minFilter = json.contains("minFilter") ? (VkFilter)json["minFilter"] : VK_FILTER_LINEAR;
            magFilter = json.contains("magFilter") ? (VkFilter)json["magFilter"] : VK_FILTER_LINEAR;
            addressMode = json.contains("addressMode") ? (VkSamplerAddressMode)json["addressMode"] : VK_SAMPLER_ADDRESS_MODE_REPEAT;
            anisotropyEnable = json.contains("anisotropy") ? (bool)json["anisotropy"] : true;
            generateMipMap = json.contains("genMipMap") ? (bool)json["genMipMap"] : true;
            rasterWidth = json.value("rasterWidth", 0);
            rasterHeight = json.value("rasterHeight", 0);
        }

        TextureAsset(AssetHandle id, const std::string &nm, const std::string &pth)
            : Asset(id, nm, pth),
              format(VK_FORMAT_R8G8B8A8_SRGB), usage(VK_IMAGE_USAGE_SAMPLED_BIT), layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
              minFilter(VK_FILTER_LINEAR), magFilter(VK_FILTER_LINEAR), addressMode(VK_SAMPLER_ADDRESS_MODE_REPEAT), anisotropyEnable(true), generateMipMap(true) {}

        void writeJSON(nlohmann::json &json) const
        {
            json["format"] = format;
            json["usage"] = usage;
            json["layout"] = layout;
            json["minFilter"] = minFilter;
            json["magFilter"] = magFilter;
            json["addressMode"] = addressMode;
            json["anisotropy"] = anisotropyEnable;
            json["genMipMap"] = generateMipMap;
            json["rasterWidth"] = rasterWidth;
            json["rasterHeight"] = rasterHeight;
        }
    };

    class FontAsset : public Asset<ASSET_FONT, FontAsset, vke_common::Font>
    {
    public:
        uint32_t pixelSize;
        uint32_t characterCount;
        uint32_t firstCodepoint;
        std::string characters;

        FontAsset() {}

        FontAsset(AssetHandle id, const nlohmann::json &json) : Asset(id, json)
        {
            pixelSize = json.contains("pixelSize") ? (uint32_t)json["pixelSize"] : 48;
            characterCount = json.contains("characterCount") ? (uint32_t)json["characterCount"] : 128;
            firstCodepoint = json.contains("firstCodepoint") ? (uint32_t)json["firstCodepoint"] : 32;
            characters = json.contains("characters") ? json["characters"].get<std::string>() : "";
        }

        FontAsset(AssetHandle id, const std::string &nm, const std::string &pth)
            : Asset(id, nm, pth), pixelSize(48), characterCount(128), firstCodepoint(32) {}

        void writeJSON(nlohmann::json &json) const
        {
            json["pixelSize"] = pixelSize;
            json["characterCount"] = characterCount;
            json["firstCodepoint"] = firstCodepoint;
            if (!characters.empty())
                json["characters"] = characters;
        }
    };

    class VFShaderAsset : public Asset<ASSET_VF_SHADER, VFShaderAsset, vke_render::ShaderModuleSet>
    {
    public:
        std::filesystem::path fragPath;

        VFShaderAsset() {}

        VFShaderAsset(AssetHandle id, const nlohmann::json &json)
            : fragPath(PathFromUTF8(json["fragPath"].get<std::string>())), Asset(id, json) {}

        DEFAULT_CONSTRUCTOR2(VFShaderAsset)

        VFShaderAsset(AssetHandle id, const std::string &nm, const std::string &pth, const std::string &fragpth)
            : fragPath(PathFromUTF8(fragpth)), Asset(id, nm, pth) {}

        void writeJSON(nlohmann::json &json) const
        {
            json["fragPath"] = PathToUTF8(fragPath);
        }
    };

    class MaterialAsset : public Asset<ASSET_MATERIAL, MaterialAsset, vke_render::Material>
    {
    public:
        AssetHandle shader;
        std::vector<AssetHandle> textures;
        std::shared_ptr<std::vector<vke_render::TextureBindingInfo>> textureBindingInfos;
        std::shared_ptr<std::vector<vke_render::PushConstantInfo>> pushConstantInfos;
        std::shared_ptr<std::vector<std::unique_ptr<uint32_t[]>>> pushConstantData;
        vke_render::MaterialRenderMode renderMode;
        vke_render::MaterialBlendMode blendMode;

        MaterialAsset()
            : renderMode(vke_render::MaterialRenderMode::OPAQUE_MODE),
              blendMode(vke_render::MaterialBlendMode::ALPHA) {}

        MaterialAsset(AssetHandle id, const nlohmann::json &json)
            : shader(json["shader"]), Asset(id, json),
              renderMode(vke_render::MaterialRenderMode::OPAQUE_MODE),
              blendMode(vke_render::MaterialBlendMode::ALPHA)
        {
            const std::string renderModeName = json.value("renderMode", "opaque");
            if (renderModeName == "cutoff")
                renderMode = vke_render::MaterialRenderMode::CUTOFF_MODE;
            else if (renderModeName == "blend")
                renderMode = vke_render::MaterialRenderMode::BLEND_MODE;

            const std::string blendModeName = json.value("blendMode", "alpha");
            if (blendModeName == "premultipliedAlpha")
                blendMode = vke_render::MaterialBlendMode::PREMULTIPLIED_ALPHA;
            else if (blendModeName == "additive")
                blendMode = vke_render::MaterialBlendMode::ADDITIVE;

            auto &texs = json["textures"];
            for (auto &tex : texs)
                textures.push_back(tex);
            textureBindingInfos = std::make_shared<std::vector<vke_render::TextureBindingInfo>>();
            auto &bindingInfos = json["bindingInfos"];
            for (auto &bindingInfo : bindingInfos)
                textureBindingInfos->emplace_back(bindingInfo["binding"], bindingInfo["offset"], bindingInfo["cnt"]);

            pushConstantInfos = std::make_shared<std::vector<vke_render::PushConstantInfo>>();
            pushConstantData = std::make_shared<std::vector<std::unique_ptr<uint32_t[]>>>();
            if (!json.contains("pushConstantInfos"))
                return;

            auto &constantInfos = json["pushConstantInfos"];
            pushConstantData->reserve(constantInfos.size());
            pushConstantInfos->reserve(constantInfos.size());
            for (auto &constantInfo : constantInfos)
            {
                const uint32_t componentCnt = constantInfo["component_cnt"];
                const std::string componentType = constantInfo["component_type"];
                const auto &data = constantInfo["data"];

                const bool isFloat = componentType == "float";
                auto constantData = std::make_unique<uint32_t[]>(componentCnt);
                pushConstantInfos->emplace_back(
                    componentCnt * sizeof(uint32_t),
                    constantData.get(),
                    isFloat,
                    constantInfo["offset"]);

                const uint32_t valueCnt = pushConstantInfos->back().size / sizeof(uint32_t);
                for (uint32_t i = 0; i < valueCnt; ++i)
                {
                    if (pushConstantInfos->back().isFloat)
                    {
                        const float value = data[i].get<float>();
                        std::memcpy(&constantData[i], &value, sizeof(value));
                    }
                    else
                    {
                        const int32_t value = data[i].get<int32_t>();
                        std::memcpy(&constantData[i], &value, sizeof(value));
                    }
                }
                pushConstantData->emplace_back(std::move(constantData));
            }
        }

        MaterialAsset(AssetHandle id, const std::string &nm, const std::string &pth)
            : Asset(id, nm, pth), shader(0),
              renderMode(vke_render::MaterialRenderMode::OPAQUE_MODE),
              blendMode(vke_render::MaterialBlendMode::ALPHA) {}

        void writeJSON(nlohmann::json &json) const
        {
            nlohmann::json bindingInfosJSON = nlohmann::json::array();
            if (textureBindingInfos != nullptr)
            {
                for (const auto &bindingInfo : *textureBindingInfos)
                    bindingInfosJSON.push_back({{"binding", bindingInfo.binding},
                                                {"offset", bindingInfo.offset},
                                                {"cnt", bindingInfo.cnt}});
            }

            nlohmann::json pushConstantInfosJSON = nlohmann::json::array();
            if (pushConstantInfos != nullptr)
            {
                for (size_t constantIndex = 0; constantIndex < pushConstantInfos->size(); ++constantIndex)
                {
                    const auto &info = (*pushConstantInfos)[constantIndex];
                    const uint32_t componentCnt = info.size / sizeof(uint32_t);
                    nlohmann::json data = nlohmann::json::array();
                    for (uint32_t i = 0; i < componentCnt; ++i)
                    {
                        if (info.isFloat)
                        {
                            float value;
                            std::memcpy(&value, static_cast<const uint32_t *>(info.pValues) + i, sizeof(value));
                            data.push_back(value);
                        }
                        else
                        {
                            int32_t value;
                            std::memcpy(&value, static_cast<const uint32_t *>(info.pValues) + i, sizeof(value));
                            data.push_back(value);
                        }
                    }
                    pushConstantInfosJSON.push_back({{"name", "constant_" + std::to_string(constantIndex)},
                                                     {"offset", info.offset},
                                                     {"component_cnt", componentCnt},
                                                     {"component_type", info.isFloat ? "float" : "int"},
                                                     {"data", std::move(data)}});
                }
            }

            const char *renderModeName = renderMode == vke_render::MaterialRenderMode::CUTOFF_MODE ? "cutoff" : renderMode == vke_render::MaterialRenderMode::BLEND_MODE ? "blend"
                                                                                                                                                                         : "opaque";
            const char *blendModeName = blendMode == vke_render::MaterialBlendMode::PREMULTIPLIED_ALPHA ? "premultipliedAlpha" : blendMode == vke_render::MaterialBlendMode::ADDITIVE ? "additive"
                                                                                                                                                                                      : "alpha";
            json["shader"] = shader;
            json["renderMode"] = renderModeName;
            json["blendMode"] = blendModeName;
            json["textures"] = textures;
            json["bindingInfos"] = std::move(bindingInfosJSON);
            json["pushConstantInfos"] = std::move(pushConstantInfosJSON);
        }
    };
}

#endif
