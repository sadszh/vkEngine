#include <asset/asset_manager.hpp>
#include <asset/svg.hpp>
#include <logger.hpp>
#include <stb/stb_image.h>
#include <vector>
#include <fstream>
#include <string>
#include <limits>
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

    Result<void> ReadFile(const std::filesystem::path &filename, std::vector<char> &buffer)
    {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);

        if (!file) return std::unexpected(PathToUTF8(filename) + ": cannot open file");
        const auto fileSize = file.tellg();
        if (fileSize < 0) return std::unexpected(PathToUTF8(filename) + ": cannot determine file size");
        buffer.resize(fileSize);

        file.seekg(0);
        file.read(buffer.data(), fileSize);
        if (!file) return std::unexpected(PathToUTF8(filename) + ": cannot read file");

        return {};
    }

    template <typename T, typename Load>
    static Result<std::shared_ptr<T>> loadFromCacheOrUpdate(std::shared_ptr<T> &cache, Load &&load)
    {
        if (cache) return cache;
        auto loaded = load();
        if (!loaded) return std::unexpected(loaded.error());
        cache = std::move(*loaded);
        return cache;
    }

    static Result<std::unique_ptr<vke_render::Texture2D>> loadTexture2D(const AssetHandle hdl, const TextureAsset &asset, const std::filesystem::path &fullPath)
    {
        const std::string context = "texture " + std::to_string(hdl) + " (" + PathToUTF8(fullPath) + "): ";
        if (IsSVGPath(asset.path))
        {
            if (asset.format != VK_FORMAT_R8G8B8A8_SRGB && asset.format != VK_FORMAT_R8G8B8A8_UNORM)
                return std::unexpected(context + "SVG textures require R8G8B8A8_SRGB or R8G8B8A8_UNORM");
            auto image = LoadSVG(fullPath, asset.rasterWidth, asset.rasterHeight);
            if (!image) return std::unexpected(context + image.error());
            return std::make_unique<vke_render::Texture2D>(hdl, image->pixels.data(), image->width, image->height,
                                                          asset.format, asset.usage, asset.layout,
                                                          asset.minFilter, asset.magFilter, asset.addressMode,
                                                          asset.anisotropyEnable, asset.generateMipMap);
        }
        int texWidth, texHeight, texChannels;
        std::unique_ptr<FILE, decltype(&fclose)> input(_wfopen(fullPath.c_str(), L"rb"), fclose);
        if (!input) return std::unexpected(context + "cannot open texture file");
        void *decoded = asset.format == VK_FORMAT_R16G16B16A16_UNORM
            ? static_cast<void *>(stbi_load_from_file_16(input.get(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha))
            : static_cast<void *>(stbi_load_from_file(input.get(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha));
        std::unique_ptr<void, decltype(&stbi_image_free)> pixels(decoded, stbi_image_free);
        if (ferror(input.get())) return std::unexpected(context + "cannot read texture file");
        if (!pixels)
        {
            const char *reason = stbi_failure_reason();
            return std::unexpected(context + "image decode failed: " + (reason ? reason : "unknown image error"));
        }
        return std::make_unique<vke_render::Texture2D>(hdl, pixels.get(), texWidth, texHeight,
                                                      asset.format, asset.usage, asset.layout,
                                                      asset.minFilter, asset.magFilter, asset.addressMode,
                                                      asset.anisotropyEnable, asset.generateMipMap);
    }

    Result<std::unique_ptr<vke_render::Texture2D>> AssetManager::LoadTexture2DUnique(const AssetHandle hdl)
    {
        auto result = GetTextureAsset(hdl);
        if (!result) return std::unexpected("texture " + std::to_string(hdl) + ": " + result.error());
        auto *asset = *result;
        if (!asset) return std::unexpected("texture " + std::to_string(hdl) + ": asset not found");
        return loadTexture2D(hdl, *asset, resolveAssetPath(hdl, asset->path));
    }

    Result<std::shared_ptr<vke_render::Texture2D>> AssetManager::LoadTexture2D(const AssetHandle hdl)
    {
        auto result = GetTextureAsset(hdl);
        if (!result) return std::unexpected("texture " + std::to_string(hdl) + ": " + result.error());
        auto *asset = *result;
        if (!asset) return std::unexpected("texture " + std::to_string(hdl) + ": asset not found");
        return loadFromCacheOrUpdate(asset->val, [&]
        { return loadTexture2D(hdl, *asset, resolveAssetPath(hdl, asset->path)); });
    }

    static Result<std::unique_ptr<vke_render::Mesh>> loadMesh(const AssetHandle hdl, const std::filesystem::path &path)
    {
        const std::string context = "mesh " + std::to_string(hdl) + " (" + PathToUTF8(path) + "): ";
        std::vector<char> data;
        if (auto read = ReadFile(path, data); !read) return std::unexpected(context + read.error());
        auto loaded = vke_render::Mesh::Create(hdl, data);
        if (!loaded) return std::unexpected(context + loaded.error());
        return loaded;
    }

    Result<std::unique_ptr<vke_render::Mesh>> AssetManager::LoadMeshUnique(const AssetHandle hdl)
    {
        const std::string context = "mesh " + std::to_string(hdl) + ": ";
        auto result = GetMeshAsset(hdl);
        if (!result) return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset) return std::unexpected(context + "asset not found");
        return loadMesh(hdl, resolveAssetPath(hdl, asset->path));
    }

    Result<std::shared_ptr<vke_render::Mesh>> AssetManager::LoadMesh(const AssetHandle hdl)
    {
        const std::string context = "mesh " + std::to_string(hdl) + ": ";
        auto result = GetMeshAsset(hdl);
        if (!result) return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset) return std::unexpected(context + "asset not found");
        return loadFromCacheOrUpdate(asset->val, [&]
        { return loadMesh(hdl, resolveAssetPath(hdl, asset->path)); });
    }

    static Result<std::unique_ptr<vke_render::ShaderModuleSet>> loadVertFragShader(const std::filesystem::path &vpth, const std::filesystem::path &fpth)
    {
        std::vector<char> vcode, fcode;
        if (auto read = ReadFile(vpth, vcode); !read) return std::unexpected("vertex shader: " + read.error());
        if (auto read = ReadFile(fpth, fcode); !read) return std::unexpected("fragment shader: " + read.error());
        auto loaded = vke_render::ShaderModuleSet::Create({{VK_SHADER_STAGE_VERTEX_BIT, vcode}, {VK_SHADER_STAGE_FRAGMENT_BIT, fcode}});
        if (!loaded) return std::unexpected(PathToUTF8(vpth) + " / " + PathToUTF8(fpth) + ": " + loaded.error());
        return loaded;
    }

    Result<std::unique_ptr<vke_render::ShaderModuleSet>> AssetManager::LoadVertFragShaderUnique(const AssetHandle hdl)
    {
        const std::string context = "VertFrag shader " + std::to_string(hdl) + ": ";
        auto result = GetVFShaderAsset(hdl);
        if (!result) return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset) return std::unexpected(context + "asset not found");
        auto loaded = loadVertFragShader(resolveAssetPath(hdl, asset->path), resolveAssetPath(hdl, asset->fragPath));
        if (!loaded) return std::unexpected(context + loaded.error());
        return loaded;
    }

    Result<std::shared_ptr<vke_render::ShaderModuleSet>> AssetManager::LoadVertFragShader(const AssetHandle hdl)
    {
        const std::string context = "VertFrag shader " + std::to_string(hdl) + ": ";
        auto result = GetVFShaderAsset(hdl);
        if (!result) return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset) return std::unexpected(context + "asset not found");
        auto loaded = loadFromCacheOrUpdate(asset->val, [&]
        { return loadVertFragShader(resolveAssetPath(hdl, asset->path), resolveAssetPath(hdl, asset->fragPath)); });
        if (!loaded) return std::unexpected(context + loaded.error());
        return loaded;
    }

    static Result<std::unique_ptr<vke_render::ShaderModuleSet>> loadComputeShader(const std::filesystem::path &pth)
    {
        std::vector<char> code;
        if (auto read = ReadFile(pth, code); !read) return std::unexpected(read.error());
        auto loaded = vke_render::ShaderModuleSet::Create({{VK_SHADER_STAGE_COMPUTE_BIT, code}});
        if (!loaded) return std::unexpected(PathToUTF8(pth) + ": " + loaded.error());
        return loaded;
    }

    Result<std::unique_ptr<vke_render::ShaderModuleSet>> AssetManager::LoadComputeShaderUnique(const AssetHandle hdl)
    {
        const std::string context = "Compute shader " + std::to_string(hdl) + ": ";
        auto result = GetComputeShaderAsset(hdl);
        if (!result) return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset) return std::unexpected(context + "asset not found");
        auto loaded = loadComputeShader(resolveAssetPath(hdl, asset->path));
        if (!loaded) return std::unexpected(context + loaded.error());
        return loaded;
    }

    Result<std::shared_ptr<vke_render::ShaderModuleSet>> AssetManager::LoadComputeShader(const AssetHandle hdl)
    {
        const std::string context = "Compute shader " + std::to_string(hdl) + ": ";
        auto result = GetComputeShaderAsset(hdl);
        if (!result) return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset) return std::unexpected(context + "asset not found");
        auto loaded = loadFromCacheOrUpdate(asset->val, [&]
        { return loadComputeShader(resolveAssetPath(hdl, asset->path)); });
        if (!loaded) return std::unexpected(context + loaded.error());
        return loaded;
    }

    static Result<std::unique_ptr<vke_render::Material>> loadMaterial(MaterialAsset &asset)
    {
        auto mat = std::make_unique<vke_render::Material>(asset.id);
        for (size_t i = 0; i < asset.textures.size(); ++i)
        {
            auto texture = AssetManager::LoadTexture2D(asset.textures[i]);
            if (!texture)
                return std::unexpected("material " + std::to_string(asset.id) + ": textures[" +
                                       std::to_string(i) + "]: " + texture.error());
            mat->textures.push_back(std::move(*texture));
        }
        auto shader = AssetManager::LoadVertFragShader(asset.shader);
        if (!shader) return std::unexpected("material " + std::to_string(asset.id) + ": shader: " + shader.error());
        mat->shader = std::move(*shader);
        mat->renderMode = asset.renderMode;
        mat->blendMode = asset.blendMode;
        mat->textureBindingInfos = asset.textureBindingInfos;
        mat->pushConstantInfos = asset.pushConstantInfos;
        mat->pushConstantData = asset.pushConstantData;
        return mat;
    }

    Result<std::unique_ptr<vke_render::Material>> AssetManager::LoadMaterialUnique(const AssetHandle hdl)
    {
        auto result = GetMaterialAsset(hdl);
        if (!result) return std::unexpected("material " + std::to_string(hdl) + ": " + result.error());
        auto *asset = *result;
        if (!asset) return std::unexpected("material " + std::to_string(hdl) + ": asset not found");
        return loadMaterial(*asset);
    }

    Result<std::shared_ptr<vke_render::Material>> AssetManager::LoadMaterial(const AssetHandle hdl)
    {
        auto result = GetMaterialAsset(hdl);
        if (!result) return std::unexpected("material " + std::to_string(hdl) + ": " + result.error());
        auto *asset = *result;
        if (!asset) return std::unexpected("material " + std::to_string(hdl) + ": asset not found");
        return loadFromCacheOrUpdate(asset->val, [&]
        { return loadMaterial(*asset); });
    }

    static Result<std::unique_ptr<Skeleton>> loadSkeleton(const SkeletonAsset &asset, const std::filesystem::path &fullPath)
    {
        const std::string context = "skeleton " + std::to_string(asset.id) + " (" + PathToUTF8(fullPath) + "): ";
        ozz::io::File file(_wfopen(fullPath.c_str(), L"rb"));
        if (!file.opened()) return std::unexpected(context + "cannot open archive");
        ozz::io::IArchive archive(&file);
        if (!archive.TestTag<ozz::animation::Skeleton>())
            return std::unexpected(context + "invalid skeleton archive type tag");
        auto ret = std::make_unique<Skeleton>(asset.id);
        archive >> ret->skeleton;
        return ret;
    }

    Result<std::unique_ptr<Skeleton>> AssetManager::LoadSkeletonUnique(const AssetHandle hdl)
    {
        const std::string context = "skeleton " + std::to_string(hdl) + ": ";
        auto result = GetSkeletonAsset(hdl);
        if (!result) return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset) return std::unexpected(context + "asset not found");
        return loadSkeleton(*asset, resolveAssetPath(hdl, asset->path));
    }

    Result<std::shared_ptr<Skeleton>> AssetManager::LoadSkeleton(const AssetHandle hdl)
    {
        const std::string context = "skeleton " + std::to_string(hdl) + ": ";
        auto result = GetSkeletonAsset(hdl);
        if (!result) return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset) return std::unexpected(context + "asset not found");
        return loadFromCacheOrUpdate(asset->val, [&]
        { return loadSkeleton(*asset, resolveAssetPath(hdl, asset->path)); });
    }

    static Result<std::unique_ptr<Animation>> loadAnimation(const AnimationAsset &asset, const std::filesystem::path &fullPath)
    {
        const std::string context = "animation " + std::to_string(asset.id) + " (" + PathToUTF8(fullPath) + "): ";
        ozz::io::File file(_wfopen(fullPath.c_str(), L"rb"));
        if (!file.opened()) return std::unexpected(context + "cannot open archive");
        ozz::io::IArchive archive(&file);
        if (!archive.TestTag<ozz::animation::Animation>())
            return std::unexpected(context + "invalid animation archive type tag");
        auto ret = std::make_unique<Animation>(asset.id);
        archive >> ret->animation;
        ret->hasRootMotion = asset.hasRootMotion;
        if (ret->hasRootMotion)
        {
            if (!archive.TestTag<ozz::animation::Float3Track>())
                return std::unexpected(context + "root motion position: invalid track type tag");
            archive >> ret->rootMotionPosition;

            if (!archive.TestTag<ozz::animation::QuaternionTrack>())
                return std::unexpected(context + "root motion rotation: invalid track type tag");
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

    Result<std::unique_ptr<Animation>> AssetManager::LoadAnimationUnique(const AssetHandle hdl)
    {
        const std::string context = "animation " + std::to_string(hdl) + ": ";
        auto result = GetAnimationAsset(hdl);
        if (!result) return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset) return std::unexpected(context + "asset not found");
        return loadAnimation(*asset, resolveAssetPath(hdl, asset->path));
    }

    Result<std::shared_ptr<Animation>> AssetManager::LoadAnimation(const AssetHandle hdl)
    {
        const std::string context = "animation " + std::to_string(hdl) + ": ";
        auto result = GetAnimationAsset(hdl);
        if (!result) return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset) return std::unexpected(context + "asset not found");
        return loadFromCacheOrUpdate(asset->val, [&]
        { return loadAnimation(*asset, resolveAssetPath(hdl, asset->path)); });
    }

    static Result<std::unique_ptr<Font>> loadFont(const FontAsset &asset, const std::filesystem::path &fullPath)
    {
        const std::string context = "font " + std::to_string(asset.id) + " (" + PathToUTF8(fullPath) + "): ";
        if (asset.pixelSize == 0) return std::unexpected(context + "pixel size must be positive");
        auto ret = std::make_unique<Font>(asset.id);
        if (auto read = ReadFile(fullPath, ret->fontData); !read) return std::unexpected(context + read.error());
        if (ret->fontData.size() > static_cast<size_t>(std::numeric_limits<FT_Long>::max()))
            return std::unexpected(context + "font file exceeds FreeType size limit");
        FT_Error error = FT_New_Memory_Face(AssetManager::GetInstance()->ftLibrary,
            reinterpret_cast<const FT_Byte *>(ret->fontData.data()), static_cast<FT_Long>(ret->fontData.size()), 0, &(ret->face));
        if (error) return std::unexpected(context + "FT_New_Memory_Face failed: " + std::to_string(error));

        error = FT_Select_Charmap(ret->face, FT_ENCODING_UNICODE);
        if (error) return std::unexpected(context + "FT_Select_Charmap(Unicode) failed: " + std::to_string(error));

        error = FT_Set_Pixel_Sizes(ret->face, 0, asset.pixelSize);
        if (error) return std::unexpected(context + "FT_Set_Pixel_Sizes(" + std::to_string(asset.pixelSize) + ") failed: " + std::to_string(error));

        ret->familyName = ret->face->family_name == nullptr ? "" : ret->face->family_name;
        ret->styleName = ret->face->style_name == nullptr ? "" : ret->face->style_name;
        ret->glyphCount = static_cast<uint32_t>(ret->face->num_glyphs);
        ret->pixelSize = asset.pixelSize;
        ret->firstCodepoint = asset.firstCodepoint;
        ret->requestedCharacterCount = asset.characterCount;
        ret->ascender = static_cast<int>(ret->face->size->metrics.ascender >> 6);
        ret->descender = static_cast<int>(ret->face->size->metrics.descender >> 6);
        ret->lineHeight = static_cast<int>(ret->face->size->metrics.height >> 6);
        if (auto built = ret->BuildStaticAtlas(asset.characters, asset.characterCount, asset.firstCodepoint); !built)
            return std::unexpected(context + built.error());
        return ret;
    }

    Result<std::unique_ptr<Font>> AssetManager::LoadFontUnique(const AssetHandle hdl)
    {
        const std::string context = "font " + std::to_string(hdl) + ": ";
        auto result = GetFontAsset(hdl);
        if (!result) return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset) return std::unexpected(context + "asset not found");
        return loadFont(*asset, resolveAssetPath(hdl, asset->path));
    }

    Result<std::shared_ptr<Font>> AssetManager::LoadFont(const AssetHandle hdl)
    {
        const std::string context = "font " + std::to_string(hdl) + ": ";
        auto result = GetFontAsset(hdl);
        if (!result) return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset) return std::unexpected(context + "asset not found");
        return loadFromCacheOrUpdate(asset->val, [&]
        { return loadFont(*asset, resolveAssetPath(hdl, asset->path)); });
    }

    static Result<std::unique_ptr<vke_audio::AudioClip>> loadAudioClip(const AudioClipAsset &asset, const std::filesystem::path &fullPath)
    {
        const std::string context = "audio clip " + std::to_string(asset.id) + " (" + PathToUTF8(fullPath) + "): ";
        std::vector<char> encoded;
        if (auto read = ReadFile(fullPath, encoded); !read) return std::unexpected(context + read.error());
        auto clip = std::make_unique<vke_audio::AudioClip>(asset.id);
        auto config = ma_decoder_config_init(ma_format_f32, 0, 0);
        ma_uint64 frameCount = 0;
        void *samples = nullptr;
        const auto result = ma_decode_memory(encoded.data(), encoded.size(), &config, &frameCount, &samples);
        clip->samples.reset(static_cast<float *>(samples));
        if (result != MA_SUCCESS)
            return std::unexpected(context + "cannot decode audio: " + ma_result_description(result));
        if (frameCount == 0) return std::unexpected(context + "audio contains no frames");
        clip->frameCount = frameCount;
        clip->channels = config.channels;
        clip->sampleRate = config.sampleRate;
        return clip;
    }

    Result<std::unique_ptr<vke_audio::AudioClip>> AssetManager::LoadAudioClipUnique(const AssetHandle hdl)
    {
        const std::string context = "audio clip " + std::to_string(hdl) + ": ";
        auto result = GetAudioClipAsset(hdl);
        if (!result) return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset) return std::unexpected(context + "asset not found");

        const auto path = resolveAssetPath(hdl, asset->path);
        return loadAudioClip(*asset, path);
    }

    Result<std::shared_ptr<vke_audio::AudioClip>> AssetManager::LoadAudioClip(const AssetHandle hdl)
    {
        const std::string context = "audio clip " + std::to_string(hdl) + ": ";
        auto result = GetAudioClipAsset(hdl);
        if (!result) return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset) return std::unexpected(context + "asset not found");
        return loadFromCacheOrUpdate(asset->val, [&]
        { return loadAudioClip(*asset, resolveAssetPath(hdl, asset->path)); });
    }
}
