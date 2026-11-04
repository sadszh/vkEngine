#include <asset/asset_manager.hpp>
#include <asset/svg.hpp>
#include <logger.hpp>
#include <stb/stb_image.h>
#include <vector>
#include <fstream>
#include <string>
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>
#include <ozz/animation/runtime/track_sampling_job.h>

namespace vke_common
{
    SceneResult<void> AssetManager::ValidateTexture2D(AssetHandle handle)
    {
        return validateAsset(handle, GetTextureAsset);
    }

    SceneResult<void> AssetManager::ValidateMesh(AssetHandle handle)
    {
        return validateAsset(handle, GetMeshAsset);
    }

    SceneResult<void> AssetManager::ValidateVertFragShader(AssetHandle handle)
    {
        return validateAsset(handle, GetVFShaderAsset);
    }

    SceneResult<void> AssetManager::ValidateComputeShader(AssetHandle handle)
    {
        return validateAsset(handle, GetComputeShaderAsset);
    }

    SceneResult<void> AssetManager::ValidateMaterial(AssetHandle handle)
    {
        return validateAsset(handle, GetMaterialAsset);
    }

    SceneResult<void> AssetManager::ValidateSkeleton(AssetHandle handle)
    {
        return validateAsset(handle, GetSkeletonAsset);
    }

    SceneResult<void> AssetManager::ValidateAnimation(AssetHandle handle)
    {
        return validateAsset(handle, GetAnimationAsset);
    }

    SceneResult<void> AssetManager::ValidateFont(AssetHandle handle)
    {
        return validateAsset(handle, GetFontAsset);
    }

    SceneResult<void> AssetManager::ValidateAudioClip(AssetHandle handle)
    {
        return validateAsset(handle, GetAudioClipAsset);
    }

    SceneResult<void> AssetManager::checkPath(AssetHandle handle, const std::filesystem::path &path) const
    {
        const auto full = (handle < CUSTOM_ASSET_ID_ST ? RelDir : pathPrefix) / path;
        std::error_code error;
        if (!std::filesystem::is_regular_file(full, error))
            return std::unexpected("asset file not found: " + full.string());
        return {};
    }

    SceneResult<void> AssetManager::checkAsset(const MaterialAsset *material) const
    {
        if (!material) return std::unexpected("material not found");
        if (material->val) return {};
        if (auto result = ValidateVertFragShader(material->shader); !result)
            return std::unexpected("shader: " + result.error());
        for (auto texture : material->textures)
            if (auto result = ValidateTexture2D(texture); !result)
                return std::unexpected("texture: " + result.error());
        return {};
    }

    SceneResult<void> AssetManager::checkAsset(const VFShaderAsset *shader) const
    {
        if (!shader) return std::unexpected("shader not found");
        if (shader->val) return {};
        if (auto result = checkPath(shader->id, shader->path); !result)
            return std::unexpected("vertex shader: " + result.error());
        if (auto result = checkPath(shader->id, shader->fragPath); !result)
            return std::unexpected("fragment shader: " + result.error());
        return {};
    }

    void ReadFile(const std::string &filename, std::vector<char> &buffer)
    {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);

        size_t fileSize = (size_t)file.tellg();
        buffer.resize(fileSize);

        file.seekg(0);
        file.read(buffer.data(), fileSize);

        file.close();
    }

    nlohmann::json LoadJSON(const std::string &pth)
    {
        nlohmann::json ret;
        std::ifstream ifs(pth, std::ios::in);
        ifs >> ret;
        ifs.close();
        return ret;
    }

    template <typename T, typename AT>
    static inline std::shared_ptr<T> loadFromCacheOrUpdate(
        AT *asset,
        std::function<std::unique_ptr<T>(AT &)> &load)
    {
        if (asset->val != nullptr)
            return asset->val;

        asset->val = load(*asset);
        return asset->val;
    }

    static inline std::unique_ptr<vke_render::Texture2D> loadTexture2D(const AssetHandle hdl, const TextureAsset &asset, const std::string &fullPath)
    {
        if (IsSVGPath(asset.path))
        {
            VKE_FATAL_IF(asset.format != VK_FORMAT_R8G8B8A8_SRGB && asset.format != VK_FORMAT_R8G8B8A8_UNORM,
                         "SVG textures require R8G8B8A8_SRGB or R8G8B8A8_UNORM: {}", fullPath)
            auto image = LoadSVG(fullPath, asset.rasterWidth, asset.rasterHeight);
            VKE_FATAL_IF(!image, "Failed to load SVG {}: {}", fullPath, image.error())
            return std::make_unique<vke_render::Texture2D>(hdl, image->pixels.data(), image->width, image->height,
                                                          asset.format, asset.usage, asset.layout,
                                                          asset.minFilter, asset.magFilter, asset.addressMode,
                                                          asset.anisotropyEnable, asset.generateMipMap);
        }
        int texWidth, texHeight, texChannels;
        void *pixels = nullptr;
        if (asset.format == VK_FORMAT_R16G16B16A16_UNORM)
            pixels = stbi_load_16(fullPath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        else
            pixels = stbi_load(fullPath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        VKE_FATAL_IF(!pixels, "Failed to load texture image!")

        std::unique_ptr<vke_render::Texture2D> texture = std::make_unique<vke_render::Texture2D>(hdl, pixels, texWidth, texHeight,
                                                                                                 asset.format, asset.usage, asset.layout,
                                                                                                 asset.minFilter, asset.magFilter, asset.addressMode,
                                                                                                 asset.anisotropyEnable, asset.generateMipMap);
        stbi_image_free(pixels);
        return texture;
    }

    std::unique_ptr<vke_render::Texture2D> AssetManager::LoadTexture2DUnique(const AssetHandle hdl)
    {
        auto *asset = hdl < CUSTOM_ASSET_ID_ST
                          ? instance->builtinAssets->GetTextureAsset(hdl)
                          : instance->assetDB->GetTextureAsset(hdl);
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        std::string path = hdl < CUSTOM_ASSET_ID_ST
                               ? (RelDir / asset->path).string()
                               : (instance->pathPrefix / asset->path).string();
        return loadTexture2D(hdl, *asset, path);
    }

    std::shared_ptr<vke_render::Texture2D> AssetManager::LoadTexture2D(const AssetHandle hdl)
    {
        auto *asset = hdl < CUSTOM_ASSET_ID_ST
                          ? instance->builtinAssets->GetTextureAsset(hdl)
                          : instance->assetDB->GetTextureAsset(hdl);
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        std::string path = hdl < CUSTOM_ASSET_ID_ST
                               ? (RelDir / asset->path).string()
                               : (instance->pathPrefix / asset->path).string();
        std::function<std::unique_ptr<vke_render::Texture2D>(TextureAsset &)> op = [hdl, path](TextureAsset &a)
        { return loadTexture2D(hdl, a, path); };

        return loadFromCacheOrUpdate<vke_render::Texture2D>(asset, op);
    }

    static inline std::unique_ptr<vke_render::Mesh> loadMesh(const AssetHandle hdl, const std::string &pth)
    {
        std::ifstream file(pth, std::ios::binary);
        return std::make_unique<vke_render::Mesh>(hdl, file);
    }

    std::unique_ptr<vke_render::Mesh> AssetManager::LoadMeshUnique(const AssetHandle hdl)
    {
        auto *asset = hdl < CUSTOM_ASSET_ID_ST
                          ? instance->builtinAssets->GetMeshAsset(hdl)
                          : instance->assetDB->GetMeshAsset(hdl);
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        std::string path = hdl < CUSTOM_ASSET_ID_ST
                               ? (RelDir / asset->path).string()
                               : (instance->pathPrefix / asset->path).string();
        return loadMesh(hdl, path);
    }

    std::shared_ptr<vke_render::Mesh> AssetManager::LoadMesh(const AssetHandle hdl)
    {
        auto *asset = hdl < CUSTOM_ASSET_ID_ST
                          ? instance->builtinAssets->GetMeshAsset(hdl)
                          : instance->assetDB->GetMeshAsset(hdl);
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        std::string path = hdl < CUSTOM_ASSET_ID_ST
                               ? (RelDir / asset->path).string()
                               : (instance->pathPrefix / asset->path).string();
        std::function<std::unique_ptr<vke_render::Mesh>(MeshAsset &)> op = [hdl, path](MeshAsset &a)
        { return loadMesh(hdl, path); };

        return loadFromCacheOrUpdate<vke_render::Mesh>(asset, op);
    }

    static inline std::unique_ptr<vke_render::ShaderModuleSet> loadVertFragShader(const AssetHandle hdl, const std::string &vpth, const std::string &fpth)
    {
        std::vector<char> vcode, fcode;
        ReadFile(vpth, vcode);
        ReadFile(fpth, fcode);
        return std::make_unique<vke_render::ShaderModuleSet>(vcode, fcode);
    }

    std::unique_ptr<vke_render::ShaderModuleSet> AssetManager::LoadVertFragShaderUnique(const AssetHandle hdl)
    {
        auto *asset = hdl < CUSTOM_ASSET_ID_ST
                          ? instance->builtinAssets->GetVFShaderAsset(hdl)
                          : instance->assetDB->GetVFShaderAsset(hdl);
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        std::string vertPath = hdl < CUSTOM_ASSET_ID_ST
                                   ? (RelDir / asset->path).string()
                                   : (instance->pathPrefix / asset->path).string();
        std::string fragPath = hdl < CUSTOM_ASSET_ID_ST
                                   ? (RelDir / asset->fragPath).string()
                                   : (instance->pathPrefix / asset->fragPath).string();
        return loadVertFragShader(hdl, vertPath, fragPath);
    }

    std::shared_ptr<vke_render::ShaderModuleSet> AssetManager::LoadVertFragShader(const AssetHandle hdl)
    {
        auto *asset = hdl < CUSTOM_ASSET_ID_ST
                          ? instance->builtinAssets->GetVFShaderAsset(hdl)
                          : instance->assetDB->GetVFShaderAsset(hdl);
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        std::string vertPath = hdl < CUSTOM_ASSET_ID_ST
                                   ? (RelDir / asset->path).string()
                                   : (instance->pathPrefix / asset->path).string();
        std::string fragPath = hdl < CUSTOM_ASSET_ID_ST
                                   ? (RelDir / asset->fragPath).string()
                                   : (instance->pathPrefix / asset->fragPath).string();
        std::function<std::unique_ptr<vke_render::ShaderModuleSet>(VFShaderAsset &)> op = [hdl, vertPath, fragPath](VFShaderAsset &a)
        { return loadVertFragShader(hdl, vertPath, fragPath); };

        return loadFromCacheOrUpdate<vke_render::ShaderModuleSet>(asset, op);
    }

    static inline std::unique_ptr<vke_render::ShaderModuleSet> loadComputeShader(const AssetHandle hdl, const std::string &pth)
    {
        std::vector<char> code;
        ReadFile(pth, code);
        return std::make_unique<vke_render::ShaderModuleSet>(code);
    }

    std::unique_ptr<vke_render::ShaderModuleSet> AssetManager::LoadComputeShaderUnique(const AssetHandle hdl)
    {
        auto *asset = hdl < CUSTOM_ASSET_ID_ST
                          ? instance->builtinAssets->GetComputeShaderAsset(hdl)
                          : instance->assetDB->GetComputeShaderAsset(hdl);
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        std::string path = hdl < CUSTOM_ASSET_ID_ST
                               ? (RelDir / asset->path).string()
                               : (instance->pathPrefix / asset->path).string();
        return loadComputeShader(hdl, path);
    }

    std::shared_ptr<vke_render::ShaderModuleSet> AssetManager::LoadComputeShader(const AssetHandle hdl)
    {
        auto *asset = hdl < CUSTOM_ASSET_ID_ST
                          ? instance->builtinAssets->GetComputeShaderAsset(hdl)
                          : instance->assetDB->GetComputeShaderAsset(hdl);
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        std::string path = hdl < CUSTOM_ASSET_ID_ST
                               ? (RelDir / asset->path).string()
                               : (instance->pathPrefix / asset->path).string();
        std::function<std::unique_ptr<vke_render::ShaderModuleSet>(ComputeShaderAsset &)> op = [hdl, path](ComputeShaderAsset &a)
        { return loadComputeShader(hdl, path); };

        return loadFromCacheOrUpdate<vke_render::ShaderModuleSet>(asset, op);
    }

    std::unique_ptr<vke_render::Material> loadMaterial(MaterialAsset &asset)
    {
        vke_render::Material *mat = new vke_render::Material(asset.id);

        mat->shader = AssetManager::LoadVertFragShader(asset.shader);
        mat->renderMode = asset.renderMode;
        mat->blendMode = asset.blendMode;
        int bindingID = 0;
        for (auto tex : asset.textures)
            mat->textures.push_back(AssetManager::LoadTexture2D(tex));
        mat->textureBindingInfos = asset.textureBindingInfos;
        mat->pushConstantInfos = asset.pushConstantInfos;
        mat->pushConstantData = asset.pushConstantData;

        return std::unique_ptr<vke_render::Material>(mat);
    }

    std::unique_ptr<vke_render::Material> AssetManager::LoadMaterialUnique(const AssetHandle hdl)
    {
        auto *asset = hdl < CUSTOM_ASSET_ID_ST
                          ? instance->builtinAssets->GetMaterialAsset(hdl)
                          : instance->assetDB->GetMaterialAsset(hdl);
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")
        return loadMaterial(*asset);
    }

    std::shared_ptr<vke_render::Material> AssetManager::LoadMaterial(const AssetHandle hdl)
    {
        auto *asset = hdl < CUSTOM_ASSET_ID_ST
                          ? instance->builtinAssets->GetMaterialAsset(hdl)
                          : instance->assetDB->GetMaterialAsset(hdl);
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        std::function<std::unique_ptr<vke_render::Material>(MaterialAsset &)> op(loadMaterial);
        return loadFromCacheOrUpdate<vke_render::Material>(asset, op);
    }

    std::unique_ptr<Skeleton> loadSkeleton(SkeletonAsset &asset, const std::string &fullPath)
    {
        auto ret = std::make_unique<Skeleton>(asset.id);
        ozz::io::File file(fullPath.c_str(), "rb");
        ozz::io::IArchive archive(&file);
        if (!archive.TestTag<ozz::animation::Skeleton>())
        {
            VKE_LOG_ERROR("Failed to load skeleton instance from file {}", fullPath)
            return nullptr;
        }
        archive >> ret->skeleton;
        return ret;
    }

    std::unique_ptr<Skeleton> AssetManager::LoadSkeletonUnique(const AssetHandle hdl)
    {
        auto *asset = hdl < CUSTOM_ASSET_ID_ST
                          ? instance->builtinAssets->GetSkeletonAsset(hdl)
                          : instance->assetDB->GetSkeletonAsset(hdl);
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        std::string path = hdl < CUSTOM_ASSET_ID_ST
                               ? (RelDir / asset->path).string()
                               : (instance->pathPrefix / asset->path).string();
        return loadSkeleton(*asset, path);
    }

    std::shared_ptr<Skeleton> AssetManager::LoadSkeleton(const AssetHandle hdl)
    {
        auto *asset = hdl < CUSTOM_ASSET_ID_ST
                          ? instance->builtinAssets->GetSkeletonAsset(hdl)
                          : instance->assetDB->GetSkeletonAsset(hdl);
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        std::string path = hdl < CUSTOM_ASSET_ID_ST
                               ? (RelDir / asset->path).string()
                               : (instance->pathPrefix / asset->path).string();
        std::function<std::unique_ptr<Skeleton>(SkeletonAsset &)> op = [path](SkeletonAsset &a)
        { return loadSkeleton(a, path); };
        return loadFromCacheOrUpdate<Skeleton>(asset, op);
    }

    std::unique_ptr<Animation> loadAnimation(AnimationAsset &asset, const std::string &fullPath)
    {
        auto ret = std::make_unique<Animation>(asset.id);
        ozz::io::File file(fullPath.c_str(), "rb");
        ozz::io::IArchive archive(&file);
        if (!archive.TestTag<ozz::animation::Animation>())
        {
            VKE_LOG_ERROR("Failed to load animation instance from file {}", fullPath)
            return nullptr;
        }
        archive >> ret->animation;
        ret->hasRootMotion = asset.hasRootMotion;
        if (ret->hasRootMotion)
        {
            if (!archive.TestTag<ozz::animation::Float3Track>())
            {
                VKE_LOG_ERROR("Failed to load root motion position track from file {}", fullPath)
                return nullptr;
            }
            archive >> ret->rootMotionPosition;

            if (!archive.TestTag<ozz::animation::QuaternionTrack>())
            {
                VKE_LOG_ERROR("Failed to load root motion rotation track from file {}", fullPath)
                return nullptr;
            }
            archive >> ret->rootMotionRotation;

            ozz::math::Float3 startPosition, endPosition;
            ozz::animation::Float3TrackSamplingJob positionJob;
            positionJob.track = &ret->rootMotionPosition;
            positionJob.ratio = 0.0f;
            positionJob.result = &startPosition;
            positionJob.Run();
            positionJob.ratio = 1.0f;
            positionJob.result = &endPosition;
            positionJob.Run();

            ozz::math::Quaternion startRotation, endRotation;
            ozz::animation::QuaternionTrackSamplingJob rotationJob;
            rotationJob.track = &ret->rootMotionRotation;
            rotationJob.ratio = 0.0f;
            rotationJob.result = &startRotation;
            rotationJob.Run();
            rotationJob.ratio = 1.0f;
            rotationJob.result = &endRotation;
            rotationJob.Run();
            const glm::quat start = glm::normalize(glm::quat(startRotation.w, startRotation.x, startRotation.y, startRotation.z));
            const glm::quat end = glm::normalize(glm::quat(endRotation.w, endRotation.x, endRotation.y, endRotation.z));
            ret->rootMotionCycleRotation = glm::normalize(end * glm::inverse(start));
            ret->rootMotionCyclePosition = glm::vec3(endPosition.x, endPosition.y, endPosition.z) -
                                           ret->rootMotionCycleRotation * glm::vec3(startPosition.x, startPosition.y, startPosition.z);
        }
        return ret;
    }

    std::unique_ptr<Animation> AssetManager::LoadAnimationUnique(const AssetHandle hdl)
    {
        auto *asset = hdl < CUSTOM_ASSET_ID_ST
                          ? instance->builtinAssets->GetAnimationAsset(hdl)
                          : instance->assetDB->GetAnimationAsset(hdl);
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        std::string path = hdl < CUSTOM_ASSET_ID_ST
                               ? (RelDir / asset->path).string()
                               : (instance->pathPrefix / asset->path).string();
        return loadAnimation(*asset, path);
    }

    std::shared_ptr<Animation> AssetManager::LoadAnimation(const AssetHandle hdl)
    {
        auto *asset = hdl < CUSTOM_ASSET_ID_ST
                          ? instance->builtinAssets->GetAnimationAsset(hdl)
                          : instance->assetDB->GetAnimationAsset(hdl);
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        std::string path = hdl < CUSTOM_ASSET_ID_ST
                               ? (RelDir / asset->path).string()
                               : (instance->pathPrefix / asset->path).string();
        std::function<std::unique_ptr<Animation>(AnimationAsset &)> op = [path](AnimationAsset &a)
        { return loadAnimation(a, path); };
        return loadFromCacheOrUpdate<Animation>(asset, op);
    }

    static inline std::unique_ptr<Font> loadFont(FontAsset &asset, const std::string &fullPath)
    {
        auto ret = std::make_unique<Font>(asset.id);
        FT_Error error = FT_New_Face(AssetManager::GetInstance()->ftLibrary, fullPath.c_str(), 0, &(ret->face));
        VKE_FATAL_IF(error != FT_Err_Ok, "Failed to load font face from {}", fullPath)

        if (ret->face->charmap == nullptr)
            FT_Select_Charmap(ret->face, FT_ENCODING_UNICODE);

        error = FT_Set_Pixel_Sizes(ret->face, 0, asset.pixelSize);
        VKE_FATAL_IF(error != FT_Err_Ok, "Failed to set font pixel size to {} for {}", asset.pixelSize, fullPath)

        ret->familyName = ret->face->family_name == nullptr ? "" : ret->face->family_name;
        ret->styleName = ret->face->style_name == nullptr ? "" : ret->face->style_name;
        ret->glyphCount = static_cast<uint32_t>(ret->face->num_glyphs);
        ret->pixelSize = asset.pixelSize;
        ret->firstCodepoint = asset.firstCodepoint;
        ret->requestedCharacterCount = asset.characterCount;
        ret->ascender = static_cast<int>(ret->face->size->metrics.ascender >> 6);
        ret->descender = static_cast<int>(ret->face->size->metrics.descender >> 6);
        ret->lineHeight = static_cast<int>(ret->face->size->metrics.height >> 6);
        ret->BuildStaticAtlas(asset.characters, asset.characterCount, asset.firstCodepoint);
        return ret;
    }

    std::unique_ptr<Font> AssetManager::LoadFontUnique(const AssetHandle hdl)
    {
        auto *asset = hdl < CUSTOM_ASSET_ID_ST
                          ? instance->builtinAssets->GetFontAsset(hdl)
                          : instance->assetDB->GetFontAsset(hdl);
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        std::string path = hdl < CUSTOM_ASSET_ID_ST
                               ? (RelDir / asset->path).string()
                               : (instance->pathPrefix / asset->path).string();
        return loadFont(*asset, path);
    }

    std::shared_ptr<Font> AssetManager::LoadFont(const AssetHandle hdl)
    {
        auto *asset = hdl < CUSTOM_ASSET_ID_ST
                          ? instance->builtinAssets->GetFontAsset(hdl)
                          : instance->assetDB->GetFontAsset(hdl);
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        std::string path = hdl < CUSTOM_ASSET_ID_ST
                               ? (RelDir / asset->path).string()
                               : (instance->pathPrefix / asset->path).string();
        std::function<std::unique_ptr<Font>(FontAsset &)> op = [path](FontAsset &a)
        { return loadFont(a, path); };
        return loadFromCacheOrUpdate<Font>(asset, op);
    }

    static std::unique_ptr<vke_audio::AudioClip> loadAudioClip(AudioClipAsset &asset, const std::string &fullPath)
    {
        return std::make_unique<vke_audio::AudioClip>(asset.id, fullPath);
    }

    std::unique_ptr<vke_audio::AudioClip> AssetManager::LoadAudioClipUnique(const AssetHandle hdl)
    {
        auto *asset = hdl < CUSTOM_ASSET_ID_ST
                          ? instance->builtinAssets->GetAudioClipAsset(hdl)
                          : instance->assetDB->GetAudioClipAsset(hdl);
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        std::string path = hdl < CUSTOM_ASSET_ID_ST
                               ? (RelDir / asset->path).string()
                               : (instance->pathPrefix / asset->path).string();
        return loadAudioClip(*asset, path);
    }

    std::shared_ptr<vke_audio::AudioClip> AssetManager::LoadAudioClip(const AssetHandle hdl)
    {
        auto *asset = hdl < CUSTOM_ASSET_ID_ST
                          ? instance->builtinAssets->GetAudioClipAsset(hdl)
                          : instance->assetDB->GetAudioClipAsset(hdl);
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        std::string path = hdl < CUSTOM_ASSET_ID_ST
                               ? (RelDir / asset->path).string()
                               : (instance->pathPrefix / asset->path).string();
        std::function<std::unique_ptr<vke_audio::AudioClip>(AudioClipAsset &)> op = [path](AudioClipAsset &a)
        { return loadAudioClip(a, path); };
        return loadFromCacheOrUpdate<vke_audio::AudioClip>(asset, op);
    }
}
