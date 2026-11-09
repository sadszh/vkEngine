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
    Result<void> AssetManager::ValidateTexture2D(AssetHandle handle)
    {
        return validateAsset(handle, GetTextureAsset);
    }

    Result<void> AssetManager::ValidateMesh(AssetHandle handle)
    {
        return validateAsset(handle, GetMeshAsset);
    }

    Result<void> AssetManager::ValidateVertFragShader(AssetHandle handle)
    {
        return validateAsset(handle, GetVFShaderAsset);
    }

    Result<void> AssetManager::ValidateComputeShader(AssetHandle handle)
    {
        return validateAsset(handle, GetComputeShaderAsset);
    }

    Result<void> AssetManager::ValidateMaterial(AssetHandle handle)
    {
        return validateAsset(handle, GetMaterialAsset);
    }

    Result<void> AssetManager::ValidateSkeleton(AssetHandle handle)
    {
        return validateAsset(handle, GetSkeletonAsset);
    }

    Result<void> AssetManager::ValidateAnimation(AssetHandle handle)
    {
        return validateAsset(handle, GetAnimationAsset);
    }

    Result<void> AssetManager::ValidateFont(AssetHandle handle)
    {
        return validateAsset(handle, GetFontAsset);
    }

    Result<void> AssetManager::ValidateAudioClip(AssetHandle handle)
    {
        return validateAsset(handle, GetAudioClipAsset);
    }

    Result<void> AssetManager::checkPath(AssetHandle handle, const std::filesystem::path &path) const
    {
        const auto full = resolveAssetPath(handle, path);
        std::error_code error;
        if (!std::filesystem::is_regular_file(full, error))
            return std::unexpected("asset file not found: " + PathToUTF8(full));
        return {};
    }

    Result<void> AssetManager::checkAsset(const MaterialAsset *material) const
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

    Result<void> AssetManager::checkAsset(const VFShaderAsset *shader) const
    {
        if (!shader) return std::unexpected("shader not found");
        if (shader->val) return {};
        if (auto result = checkPath(shader->id, shader->path); !result)
            return std::unexpected("vertex shader: " + result.error());
        if (auto result = checkPath(shader->id, shader->fragPath); !result)
            return std::unexpected("fragment shader: " + result.error());
        return {};
    }

    void ReadFile(const std::filesystem::path &filename, std::vector<char> &buffer)
    {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);

        VKE_FATAL_IF(!file, "Cannot open file: {}", PathToUTF8(filename))
        size_t fileSize = (size_t)file.tellg();
        buffer.resize(fileSize);

        file.seekg(0);
        file.read(buffer.data(), fileSize);
        VKE_FATAL_IF(!file, "Cannot read file: {}", PathToUTF8(filename))

        file.close();
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

    static inline std::unique_ptr<vke_render::Texture2D> loadTexture2D(const AssetHandle hdl, const TextureAsset &asset, const std::filesystem::path &fullPath)
    {
        if (IsSVGPath(asset.path))
        {
            VKE_FATAL_IF(asset.format != VK_FORMAT_R8G8B8A8_SRGB && asset.format != VK_FORMAT_R8G8B8A8_UNORM,
                         "SVG textures require R8G8B8A8_SRGB or R8G8B8A8_UNORM: {}", PathToUTF8(fullPath))
            auto image = LoadSVG(fullPath, asset.rasterWidth, asset.rasterHeight);
            VKE_FATAL_IF(!image, "Failed to load SVG {}: {}", PathToUTF8(fullPath), image.error())
            return std::make_unique<vke_render::Texture2D>(hdl, image->pixels.data(), image->width, image->height,
                                                          asset.format, asset.usage, asset.layout,
                                                          asset.minFilter, asset.magFilter, asset.addressMode,
                                                          asset.anisotropyEnable, asset.generateMipMap);
        }
        int texWidth, texHeight, texChannels;
        void *pixels = nullptr;
        FILE *input = _wfopen(fullPath.c_str(), L"rb");
        VKE_FATAL_IF(!input, "Cannot open texture: {}", PathToUTF8(fullPath))
        if (asset.format == VK_FORMAT_R16G16B16A16_UNORM)
            pixels = stbi_load_from_file_16(input, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        else
            pixels = stbi_load_from_file(input, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        fclose(input);
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
        auto result = GetTextureAsset(hdl);
        VKE_FATAL_IF(!result, "{}", result.error())
        auto *asset = *result;
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        const auto path = resolveAssetPath(hdl, asset->path);
        return loadTexture2D(hdl, *asset, path);
    }

    std::shared_ptr<vke_render::Texture2D> AssetManager::LoadTexture2D(const AssetHandle hdl)
    {
        auto result = GetTextureAsset(hdl);
        VKE_FATAL_IF(!result, "{}", result.error())
        auto *asset = *result;
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        const auto path = resolveAssetPath(hdl, asset->path);
        std::function<std::unique_ptr<vke_render::Texture2D>(TextureAsset &)> op = [hdl, path](TextureAsset &a)
        { return loadTexture2D(hdl, a, path); };

        return loadFromCacheOrUpdate<vke_render::Texture2D>(asset, op);
    }

    static inline std::unique_ptr<vke_render::Mesh> loadMesh(const AssetHandle hdl, const std::filesystem::path &pth)
    {
        std::ifstream file(pth, std::ios::binary);
        return std::make_unique<vke_render::Mesh>(hdl, file);
    }

    std::unique_ptr<vke_render::Mesh> AssetManager::LoadMeshUnique(const AssetHandle hdl)
    {
        auto result = GetMeshAsset(hdl);
        VKE_FATAL_IF(!result, "{}", result.error())
        auto *asset = *result;
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        const auto path = resolveAssetPath(hdl, asset->path);
        return loadMesh(hdl, path);
    }

    std::shared_ptr<vke_render::Mesh> AssetManager::LoadMesh(const AssetHandle hdl)
    {
        auto result = GetMeshAsset(hdl);
        VKE_FATAL_IF(!result, "{}", result.error())
        auto *asset = *result;
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        const auto path = resolveAssetPath(hdl, asset->path);
        std::function<std::unique_ptr<vke_render::Mesh>(MeshAsset &)> op = [hdl, path](MeshAsset &a)
        { return loadMesh(hdl, path); };

        return loadFromCacheOrUpdate<vke_render::Mesh>(asset, op);
    }

    static inline std::unique_ptr<vke_render::ShaderModuleSet> loadVertFragShader(const AssetHandle hdl, const std::filesystem::path &vpth, const std::filesystem::path &fpth)
    {
        std::vector<char> vcode, fcode;
        ReadFile(vpth, vcode);
        ReadFile(fpth, fcode);
        return std::make_unique<vke_render::ShaderModuleSet>(vcode, fcode);
    }

    std::unique_ptr<vke_render::ShaderModuleSet> AssetManager::LoadVertFragShaderUnique(const AssetHandle hdl)
    {
        auto result = GetVFShaderAsset(hdl);
        VKE_FATAL_IF(!result, "{}", result.error())
        auto *asset = *result;
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        const auto vertPath = resolveAssetPath(hdl, asset->path);
        const auto fragPath = resolveAssetPath(hdl, asset->fragPath);
        return loadVertFragShader(hdl, vertPath, fragPath);
    }

    std::shared_ptr<vke_render::ShaderModuleSet> AssetManager::LoadVertFragShader(const AssetHandle hdl)
    {
        auto result = GetVFShaderAsset(hdl);
        VKE_FATAL_IF(!result, "{}", result.error())
        auto *asset = *result;
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        const auto vertPath = resolveAssetPath(hdl, asset->path);
        const auto fragPath = resolveAssetPath(hdl, asset->fragPath);
        std::function<std::unique_ptr<vke_render::ShaderModuleSet>(VFShaderAsset &)> op = [hdl, vertPath, fragPath](VFShaderAsset &a)
        { return loadVertFragShader(hdl, vertPath, fragPath); };

        return loadFromCacheOrUpdate<vke_render::ShaderModuleSet>(asset, op);
    }

    static inline std::unique_ptr<vke_render::ShaderModuleSet> loadComputeShader(const AssetHandle hdl, const std::filesystem::path &pth)
    {
        std::vector<char> code;
        ReadFile(pth, code);
        return std::make_unique<vke_render::ShaderModuleSet>(code);
    }

    std::unique_ptr<vke_render::ShaderModuleSet> AssetManager::LoadComputeShaderUnique(const AssetHandle hdl)
    {
        auto result = GetComputeShaderAsset(hdl);
        VKE_FATAL_IF(!result, "{}", result.error())
        auto *asset = *result;
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        const auto path = resolveAssetPath(hdl, asset->path);
        return loadComputeShader(hdl, path);
    }

    std::shared_ptr<vke_render::ShaderModuleSet> AssetManager::LoadComputeShader(const AssetHandle hdl)
    {
        auto result = GetComputeShaderAsset(hdl);
        VKE_FATAL_IF(!result, "{}", result.error())
        auto *asset = *result;
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        const auto path = resolveAssetPath(hdl, asset->path);
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
        auto result = GetMaterialAsset(hdl);
        VKE_FATAL_IF(!result, "{}", result.error())
        auto *asset = *result;
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")
        return loadMaterial(*asset);
    }

    std::shared_ptr<vke_render::Material> AssetManager::LoadMaterial(const AssetHandle hdl)
    {
        auto result = GetMaterialAsset(hdl);
        VKE_FATAL_IF(!result, "{}", result.error())
        auto *asset = *result;
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        std::function<std::unique_ptr<vke_render::Material>(MaterialAsset &)> op(loadMaterial);
        return loadFromCacheOrUpdate<vke_render::Material>(asset, op);
    }

    std::unique_ptr<Skeleton> loadSkeleton(SkeletonAsset &asset, const std::filesystem::path &fullPath)
    {
        auto ret = std::make_unique<Skeleton>(asset.id);
        ozz::io::File file(_wfopen(fullPath.c_str(), L"rb"));
        if (!file.opened())
        {
            VKE_LOG_ERROR("Cannot open animation archive: {}", PathToUTF8(fullPath))
            return nullptr;
        }
        ozz::io::IArchive archive(&file);
        if (!archive.TestTag<ozz::animation::Skeleton>())
        {
            VKE_LOG_ERROR("Failed to load skeleton instance from file {}", PathToUTF8(fullPath))
            return nullptr;
        }
        archive >> ret->skeleton;
        return ret;
    }

    std::unique_ptr<Skeleton> AssetManager::LoadSkeletonUnique(const AssetHandle hdl)
    {
        auto result = GetSkeletonAsset(hdl);
        VKE_FATAL_IF(!result, "{}", result.error())
        auto *asset = *result;
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        const auto path = resolveAssetPath(hdl, asset->path);
        return loadSkeleton(*asset, path);
    }

    std::shared_ptr<Skeleton> AssetManager::LoadSkeleton(const AssetHandle hdl)
    {
        auto result = GetSkeletonAsset(hdl);
        VKE_FATAL_IF(!result, "{}", result.error())
        auto *asset = *result;
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        const auto path = resolveAssetPath(hdl, asset->path);
        std::function<std::unique_ptr<Skeleton>(SkeletonAsset &)> op = [path](SkeletonAsset &a)
        { return loadSkeleton(a, path); };
        return loadFromCacheOrUpdate<Skeleton>(asset, op);
    }

    std::unique_ptr<Animation> loadAnimation(AnimationAsset &asset, const std::filesystem::path &fullPath)
    {
        auto ret = std::make_unique<Animation>(asset.id);
        ozz::io::File file(_wfopen(fullPath.c_str(), L"rb"));
        if (!file.opened())
        {
            VKE_LOG_ERROR("Cannot open animation archive: {}", PathToUTF8(fullPath))
            return nullptr;
        }
        ozz::io::IArchive archive(&file);
        if (!archive.TestTag<ozz::animation::Animation>())
        {
            VKE_LOG_ERROR("Failed to load animation instance from file {}", PathToUTF8(fullPath))
            return nullptr;
        }
        archive >> ret->animation;
        ret->hasRootMotion = asset.hasRootMotion;
        if (ret->hasRootMotion)
        {
            if (!archive.TestTag<ozz::animation::Float3Track>())
            {
                VKE_LOG_ERROR("Failed to load root motion position track from file {}", PathToUTF8(fullPath))
                return nullptr;
            }
            archive >> ret->rootMotionPosition;

            if (!archive.TestTag<ozz::animation::QuaternionTrack>())
            {
                VKE_LOG_ERROR("Failed to load root motion rotation track from file {}", PathToUTF8(fullPath))
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
        auto result = GetAnimationAsset(hdl);
        VKE_FATAL_IF(!result, "{}", result.error())
        auto *asset = *result;
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        const auto path = resolveAssetPath(hdl, asset->path);
        return loadAnimation(*asset, path);
    }

    std::shared_ptr<Animation> AssetManager::LoadAnimation(const AssetHandle hdl)
    {
        auto result = GetAnimationAsset(hdl);
        VKE_FATAL_IF(!result, "{}", result.error())
        auto *asset = *result;
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        const auto path = resolveAssetPath(hdl, asset->path);
        std::function<std::unique_ptr<Animation>(AnimationAsset &)> op = [path](AnimationAsset &a)
        { return loadAnimation(a, path); };
        return loadFromCacheOrUpdate<Animation>(asset, op);
    }

    static inline std::unique_ptr<Font> loadFont(FontAsset &asset, const std::filesystem::path &fullPath)
    {
        auto ret = std::make_unique<Font>(asset.id);
        ReadFile(fullPath, ret->fontData);
        FT_Error error = FT_New_Memory_Face(AssetManager::GetInstance()->ftLibrary,
            reinterpret_cast<const FT_Byte *>(ret->fontData.data()), static_cast<FT_Long>(ret->fontData.size()), 0, &(ret->face));
        VKE_FATAL_IF(error != FT_Err_Ok, "Failed to load font face from {}", PathToUTF8(fullPath))

        if (ret->face->charmap == nullptr)
            FT_Select_Charmap(ret->face, FT_ENCODING_UNICODE);

        error = FT_Set_Pixel_Sizes(ret->face, 0, asset.pixelSize);
        VKE_FATAL_IF(error != FT_Err_Ok, "Failed to set font pixel size to {} for {}", asset.pixelSize, PathToUTF8(fullPath))

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
        auto result = GetFontAsset(hdl);
        VKE_FATAL_IF(!result, "{}", result.error())
        auto *asset = *result;
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        const auto path = resolveAssetPath(hdl, asset->path);
        return loadFont(*asset, path);
    }

    std::shared_ptr<Font> AssetManager::LoadFont(const AssetHandle hdl)
    {
        auto result = GetFontAsset(hdl);
        VKE_FATAL_IF(!result, "{}", result.error())
        auto *asset = *result;
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        const auto path = resolveAssetPath(hdl, asset->path);
        std::function<std::unique_ptr<Font>(FontAsset &)> op = [path](FontAsset &a)
        { return loadFont(a, path); };
        return loadFromCacheOrUpdate<Font>(asset, op);
    }

    static std::unique_ptr<vke_audio::AudioClip> loadAudioClip(AudioClipAsset &asset, const std::filesystem::path &fullPath)
    {
        return std::make_unique<vke_audio::AudioClip>(asset.id, PathToUTF8(fullPath));
    }

    std::unique_ptr<vke_audio::AudioClip> AssetManager::LoadAudioClipUnique(const AssetHandle hdl)
    {
        auto result = GetAudioClipAsset(hdl);
        VKE_FATAL_IF(!result, "{}", result.error())
        auto *asset = *result;
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        const auto path = resolveAssetPath(hdl, asset->path);
        return loadAudioClip(*asset, path);
    }

    std::shared_ptr<vke_audio::AudioClip> AssetManager::LoadAudioClip(const AssetHandle hdl)
    {
        auto result = GetAudioClipAsset(hdl);
        VKE_FATAL_IF(!result, "{}", result.error())
        auto *asset = *result;
        VKE_FATAL_IF(asset == nullptr, "Asset Not Exist!")

        const auto path = resolveAssetPath(hdl, asset->path);
        std::function<std::unique_ptr<vke_audio::AudioClip>(AudioClipAsset &)> op = [path](AudioClipAsset &a)
        { return loadAudioClip(a, path); };
        return loadFromCacheOrUpdate<vke_audio::AudioClip>(asset, op);
    }
}
