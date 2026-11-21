#include <asset/asset_manager.hpp>
#include <asset/loader.hpp>
#include <binary_io.hpp>
#include <logger.hpp>
#include <asset/texture_data.hpp>
#include <cstring>
#include <span>
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
        return validateAsset(handle, GetTextureAssetReadOnly);
    }

    Result<void> AssetManager::ValidateMesh(AssetHandle handle)
    {
        return validateAsset(handle, GetMeshAssetReadOnly);
    }

    Result<void> AssetManager::ValidateVertFragShader(AssetHandle handle)
    {
        return validateAsset(handle, GetVFShaderAssetReadOnly);
    }

    Result<void> AssetManager::ValidateComputeShader(AssetHandle handle)
    {
        return validateAsset(handle, GetComputeShaderAssetReadOnly);
    }

    Result<void> AssetManager::ValidateMaterial(AssetHandle handle)
    {
        return validateAsset(handle, GetMaterialAssetReadOnly);
    }

    Result<void> AssetManager::ValidateSkeleton(AssetHandle handle)
    {
        return validateAsset(handle, GetSkeletonAssetReadOnly);
    }

    Result<void> AssetManager::ValidateAnimation(AssetHandle handle)
    {
        return validateAsset(handle, GetAnimationAssetReadOnly);
    }

    Result<void> AssetManager::ValidateFont(AssetHandle handle)
    {
        return validateAsset(handle, GetFontAssetReadOnly);
    }

    Result<void> AssetManager::ValidateAudioClip(AssetHandle handle)
    {
        return validateAsset(handle, GetAudioClipAssetReadOnly);
    }

    Result<void> AssetManager::ValidateNavAgentConfig(AssetHandle handle)
    {
        return validateAsset(handle, GetNavAgentConfigAssetReadOnly);
    }

    Result<void> AssetManager::ValidateNavMeshTiles(AssetHandle handle)
    {
        return validateAsset(handle, GetNavMeshTilesAssetReadOnly);
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
        if (!material)
            return std::unexpected("material not found");
        if (material->val)
            return {};
        if (auto result = ValidateVertFragShader(material->shader); !result)
            return std::unexpected("shader: " + result.error());
        for (auto texture : material->textures)
            if (auto result = ValidateTexture2D(texture); !result)
                return std::unexpected("texture: " + result.error());
        return {};
    }

    Result<void> AssetManager::checkAsset(const NavAgentConfigAsset *agentConfig) const
    {
        if (!agentConfig)
            return std::unexpected("nav agent config not found");
        return {};
    }

    Result<void> AssetManager::checkAsset(const VFShaderAsset *shader) const
    {
        if (!shader)
            return std::unexpected("shader not found");
        if (shader->val)
            return {};
        if (auto result = checkPath(shader->id, shader->path); !result)
            return std::unexpected("vertex shader: " + result.error());
        if (auto result = checkPath(shader->id, shader->fragPath); !result)
            return std::unexpected("fragment shader: " + result.error());
        return {};
    }

    Result<void> ReadFile(const std::filesystem::path &filename, std::vector<char> &buffer)
    {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);

        if (!file)
            return std::unexpected(PathToUTF8(filename) + ": cannot open file");
        const auto fileSize = file.tellg();
        if (fileSize < 0)
            return std::unexpected(PathToUTF8(filename) + ": cannot determine file size");
        buffer.resize(fileSize);

        file.seekg(0);
        if (!ReadBytes(file, buffer.data(), buffer.size()))
            return std::unexpected(PathToUTF8(filename) + ": cannot read file");

        return {};
    }

    template <typename A, typename Load>
    Result<decltype(A::val)> AssetManager::loadFromCacheOrUpdate(A &asset, Load &&load)
    {
        if (asset.val)
            return asset.val;
        auto loaded = load();
        if (!loaded)
            return std::unexpected(loaded.error());
        asset.val = std::move(*loaded);
        asset.loadState = AssetLoadState::Loaded;
        return asset.val;
    }

    Result<std::unique_ptr<vke_render::Texture2D>> loadTexture2D(const AssetHandle hdl, const TextureAsset &asset, const std::filesystem::path &fullPath, AssetUploadContext *upload)
    {
        const std::string context = "texture " + std::to_string(hdl) + " (" + PathToUTF8(fullPath) + "): ";
        if (!IsTexturePath(asset.path))
            return std::unexpected(context + "texture assets require .texture");
        std::ifstream input(fullPath, std::ios::binary);
        if (!input)
            return std::unexpected(context + "cannot open texture file");
        auto info = TextureFileInfo::Read(input);
        if (!info)
            return std::unexpected(context + info.error());
        const auto &metadata = info->metadata;
        using namespace vke_render;
        auto *environment = RenderEnvironment::GetInstance();
        VkImageFormatProperties properties{};
        const auto usage = asset.usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (vkGetPhysicalDeviceImageFormatProperties(environment->physicalDevice, metadata.format, VK_IMAGE_TYPE_2D,
                                                     VK_IMAGE_TILING_OPTIMAL, usage, 0, &properties) != VK_SUCCESS ||
            metadata.width > properties.maxExtent.width || metadata.height > properties.maxExtent.height ||
            metadata.mipCount > properties.maxMipLevels)
            return std::unexpected(context + "texture dimensions or format/usage unsupported by device");
        std::vector<char> pixels(static_cast<size_t>(info->layout.size));
        if (!ReadBytes(input, pixels.data(), pixels.size()))
            return std::unexpected(context + "cannot read texture data");

        if (!upload)
            return std::make_unique<Texture2D>(hdl, pixels.data(), pixels.size(), metadata.width, metadata.height,
                                               metadata.mipCount, metadata.format, info->layout.regions, asset.usage,
                                               asset.minFilter, asset.magFilter, asset.addressMode, asset.anisotropyEnable);

        VkImage image;
        VmaAllocation allocation;
        VulkanResourceManager::CreateImage(metadata.width, metadata.height, metadata.format, VK_IMAGE_TILING_OPTIMAL,
                                           usage, metadata.mipCount, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                           &image, &allocation, nullptr);
        const auto commands = upload->UploadImage(image, pixels.data(), pixels.size(), metadata.mipCount, info->layout.regions);
        VulkanResourceManager::MakeLayoutTransition(commands, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                    image, VK_IMAGE_ASPECT_COLOR_BIT, metadata.mipCount,
                                                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        return std::make_unique<Texture2D>(hdl, image, allocation, metadata.width, metadata.height, metadata.mipCount, metadata.format,
                                           asset.minFilter, asset.magFilter, asset.addressMode, asset.anisotropyEnable);
    }

    Result<std::unique_ptr<vke_render::Texture2D>> AssetManager::LoadTexture2DUnique(const AssetHandle hdl)
    {
        auto result = GetTextureAssetReadOnly(hdl);
        if (!result)
            return std::unexpected("texture " + std::to_string(hdl) + ": " + result.error());
        auto *asset = *result;
        if (!asset)
            return std::unexpected("texture " + std::to_string(hdl) + ": asset not found");
        return loadTexture2D(hdl, *asset, resolveAssetPath(hdl, asset->path));
    }

    Result<std::shared_ptr<vke_render::Texture2D>> AssetManager::LoadTexture2D(const AssetHandle hdl)
    {
        auto result = getAssetForLoad(getTextureAsset(hdl));
        if (!result)
            return std::unexpected("texture " + std::to_string(hdl) + ": " + result.error());
        auto *asset = *result;
        if (!asset)
            return std::unexpected("texture " + std::to_string(hdl) + ": asset not found");
        return loadFromCacheOrUpdate(*asset, [&]
                                     { return loadTexture2D(hdl, *asset, resolveAssetPath(hdl, asset->path)); });
    }

    Result<std::unique_ptr<vke_render::Mesh>> loadMesh(const AssetHandle hdl, const std::filesystem::path &path, AssetUploadContext *upload)
    {
        const std::string context = "mesh " + std::to_string(hdl) + " (" + PathToUTF8(path) + "): ";
        std::vector<char> fileData;
        if (auto read = ReadFile(path, fileData); !read)
            return std::unexpected(context + read.error());
        std::span<const char> data(fileData);
        vke_render::AABB bounds;
        std::vector<vke_render::MeshInfo> infos;
        std::vector<ozz::math::Float4x4> invBindMatrices;
        std::vector<int> joints;
        auto read = [&data](void *destination, size_t size)
        {
            if (size > data.size())
                return false;
            if (size != 0)
                std::memcpy(destination, data.data(), size);
            data = data.subspan(size);
            return true;
        };
        uint32_t infoCount;
        if (!read(&bounds.min, 3 * sizeof(float)) ||
            !read(&bounds.max, 3 * sizeof(float)) || !read(&infoCount, sizeof(infoCount)))
            return std::unexpected(context + "truncated mesh header");
        if (infoCount == 0 || infoCount > data.size() / sizeof(vke_render::MeshInfo))
            return std::unexpected(context + "invalid mesh info count");
        infos.resize(infoCount);
        const size_t infoSize = infoCount * sizeof(vke_render::MeshInfo);
        std::memcpy(infos.data(), data.data(), infoSize);
        data = data.subspan(infoSize);

        uint64_t vertexSize, indexSize;
        if (!read(&vertexSize, sizeof(vertexSize)) || vertexSize == 0 || vertexSize > data.size())
            return std::unexpected(context + "invalid or truncated vertex buffer");
        const auto vertices = data.first(static_cast<size_t>(vertexSize));
        data = data.subspan(vertices.size());
        if (!read(&indexSize, sizeof(indexSize)) || indexSize == 0 || indexSize > data.size())
            return std::unexpected(context + "invalid or truncated index buffer");
        const auto indices = data.first(static_cast<size_t>(indexSize));
        data = data.subspan(indices.size());

        for (size_t i = 0; i < infos.size(); ++i)
        {
            const auto &info = infos[i];
            auto primitiveError = [&context, i](const char *message)
            { return std::unexpected(context + "primitive " + std::to_string(i) + ": " + message); };
            if (info.vertexCnt == 0 || info.indexCnt == 0 ||
                info.vertexOffset > vertexSize || info.vertexSize > vertexSize - info.vertexOffset ||
                info.indexOffset > indexSize || info.indexSize > indexSize - info.indexOffset ||
                info.vertexSize % info.vertexCnt != 0 || info.indexSize % info.indexCnt != 0)
                return primitiveError("invalid buffer range or element count");
            const auto vertexStride = info.getVertexUnitSize();
            const auto indexStride = info.getIndexUnitSize();
            if (vertexStride < 3 * sizeof(float) || (indexStride != 2 && indexStride != 4) ||
                info.vertexOffset % vertexStride != 0 || info.indexOffset % indexStride != 0)
                return primitiveError("invalid vertex or index layout");
            if (info.indexCnt > std::numeric_limits<uint32_t>::max() ||
                info.indexOffset / indexStride > std::numeric_limits<uint32_t>::max() ||
                info.vertexOffset / vertexStride > std::numeric_limits<int32_t>::max())
                return primitiveError("draw range exceeds Vulkan limits");
            for (uint64_t j = 0; j < info.indexCnt; ++j)
            {
                uint32_t index = 0;
                std::memcpy(&index, indices.data() + info.indexOffset + j * indexStride, indexStride);
                if (index >= info.vertexCnt)
                    return primitiveError("index references a vertex outside the primitive");
            }
        }

        if (!data.empty())
        {
            uint64_t matrixSize;
            constexpr size_t matrixStride = 16 * sizeof(float);
            if (!read(&matrixSize, sizeof(matrixSize)) || matrixSize % matrixStride != 0 || matrixSize > data.size())
                return std::unexpected(context + "invalid or truncated inverse bind matrices");
            const char *matrixData = data.data();
            data = data.subspan(static_cast<size_t>(matrixSize));
            invBindMatrices.resize(static_cast<size_t>(matrixSize / matrixStride));
            for (auto &matrix : invBindMatrices)
                for (auto &column : matrix.cols)
                {
                    float values[4];
                    std::memcpy(values, matrixData, sizeof(values));
                    matrixData += sizeof(values);
                    column = ozz::math::simd_float4::LoadPtrU(values);
                }
            uint32_t jointCount;
            if (!read(&jointCount, sizeof(jointCount)) || jointCount > data.size() / sizeof(int))
                return std::unexpected(context + "invalid or truncated joint list");
            joints.resize(jointCount);
            const size_t jointSize = jointCount * sizeof(int);
            if (jointSize != 0)
                std::memcpy(joints.data(), data.data(), jointSize);
            data = data.subspan(jointSize);
            if (!invBindMatrices.empty() && joints.size() != invBindMatrices.size())
                return std::unexpected(context + "joint and inverse bind matrix counts differ");
            if (!data.empty())
                return std::unexpected(context + "unexpected trailing mesh data");
        }

        auto vertexBuffer = std::make_unique<vke_render::DeviceBuffer>(vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        auto indexBuffer = std::make_unique<vke_render::DeviceBuffer>(indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        if (upload)
        {
            upload->UploadBuffer(vertexBuffer->buffer, 0, vertices.data(), vertices.size());
            upload->UploadBuffer(indexBuffer->buffer, 0, indices.data(), indices.size());
        }
        else
        {
            vertexBuffer->ToBuffer(0, vertices.data(), vertices.size());
            indexBuffer->ToBuffer(0, indices.data(), indices.size());
        }
        return std::make_unique<vke_render::Mesh>(hdl, std::move(vertexBuffer), std::move(indexBuffer), std::move(infos), bounds,
                                                  std::move(invBindMatrices), std::move(joints));
    }

    Result<std::unique_ptr<vke_render::Mesh>> AssetManager::LoadMeshUnique(const AssetHandle hdl)
    {
        const std::string context = "mesh " + std::to_string(hdl) + ": ";
        auto result = GetMeshAssetReadOnly(hdl);
        if (!result)
            return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset)
            return std::unexpected(context + "asset not found");
        return loadMesh(hdl, resolveAssetPath(hdl, asset->path));
    }

    Result<std::shared_ptr<vke_render::Mesh>> AssetManager::LoadMesh(const AssetHandle hdl)
    {
        const std::string context = "mesh " + std::to_string(hdl) + ": ";
        auto result = getAssetForLoad(getMeshAsset(hdl));
        if (!result)
            return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset)
            return std::unexpected(context + "asset not found");
        return loadFromCacheOrUpdate(*asset, [&]
                                     { return loadMesh(hdl, resolveAssetPath(hdl, asset->path)); });
    }

    Result<std::unique_ptr<vke_render::ShaderModuleSet>> loadVertFragShader(const std::filesystem::path &vpth, const std::filesystem::path &fpth)
    {
        std::vector<char> vcode, fcode;
        if (auto read = ReadFile(vpth, vcode); !read)
            return std::unexpected("vertex shader: " + read.error());
        if (auto read = ReadFile(fpth, fcode); !read)
            return std::unexpected("fragment shader: " + read.error());
        auto loaded = vke_render::ShaderModuleSet::Create({{VK_SHADER_STAGE_VERTEX_BIT, vcode}, {VK_SHADER_STAGE_FRAGMENT_BIT, fcode}});
        if (!loaded)
            return std::unexpected(PathToUTF8(vpth) + " / " + PathToUTF8(fpth) + ": " + loaded.error());
        return loaded;
    }

    Result<std::unique_ptr<vke_render::ShaderModuleSet>> AssetManager::LoadVertFragShaderUnique(const AssetHandle hdl)
    {
        const std::string context = "VertFrag shader " + std::to_string(hdl) + ": ";
        auto result = GetVFShaderAssetReadOnly(hdl);
        if (!result)
            return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset)
            return std::unexpected(context + "asset not found");
        auto loaded = loadVertFragShader(resolveAssetPath(hdl, asset->path), resolveAssetPath(hdl, asset->fragPath));
        if (!loaded)
            return std::unexpected(context + loaded.error());
        return loaded;
    }

    Result<std::shared_ptr<vke_render::ShaderModuleSet>> AssetManager::LoadVertFragShader(const AssetHandle hdl)
    {
        const std::string context = "VertFrag shader " + std::to_string(hdl) + ": ";
        auto result = getAssetForLoad(getVFShaderAsset(hdl));
        if (!result)
            return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset)
            return std::unexpected(context + "asset not found");
        return loadFromCacheOrUpdate(*asset, [&]() -> Result<std::unique_ptr<vke_render::ShaderModuleSet>>
                                     {
            auto loaded = loadVertFragShader(resolveAssetPath(hdl, asset->path), resolveAssetPath(hdl, asset->fragPath));
            if (!loaded) return std::unexpected(context + loaded.error());
            return loaded; });
    }

    Result<std::unique_ptr<vke_render::ShaderModuleSet>> loadComputeShader(const std::filesystem::path &pth)
    {
        std::vector<char> code;
        if (auto read = ReadFile(pth, code); !read)
            return std::unexpected(read.error());
        auto loaded = vke_render::ShaderModuleSet::Create({{VK_SHADER_STAGE_COMPUTE_BIT, code}});
        if (!loaded)
            return std::unexpected(PathToUTF8(pth) + ": " + loaded.error());
        return loaded;
    }

    Result<std::unique_ptr<vke_render::ShaderModuleSet>> AssetManager::LoadComputeShaderUnique(const AssetHandle hdl)
    {
        const std::string context = "Compute shader " + std::to_string(hdl) + ": ";
        auto result = GetComputeShaderAssetReadOnly(hdl);
        if (!result)
            return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset)
            return std::unexpected(context + "asset not found");
        auto loaded = loadComputeShader(resolveAssetPath(hdl, asset->path));
        if (!loaded)
            return std::unexpected(context + loaded.error());
        return loaded;
    }

    Result<std::shared_ptr<vke_render::ShaderModuleSet>> AssetManager::LoadComputeShader(const AssetHandle hdl)
    {
        const std::string context = "Compute shader " + std::to_string(hdl) + ": ";
        auto result = getAssetForLoad(getComputeShaderAsset(hdl));
        if (!result)
            return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset)
            return std::unexpected(context + "asset not found");
        return loadFromCacheOrUpdate(*asset, [&]() -> Result<std::unique_ptr<vke_render::ShaderModuleSet>>
                                     {
            auto loaded = loadComputeShader(resolveAssetPath(hdl, asset->path));
            if (!loaded) return std::unexpected(context + loaded.error());
            return loaded; });
    }

    static Result<std::unique_ptr<vke_render::Material>> loadMaterial(const MaterialAsset &asset)
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
        if (!shader)
            return std::unexpected("material " + std::to_string(asset.id) + ": shader: " + shader.error());
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
        auto result = GetMaterialAssetReadOnly(hdl);
        if (!result)
            return std::unexpected("material " + std::to_string(hdl) + ": " + result.error());
        auto *asset = *result;
        if (!asset)
            return std::unexpected("material " + std::to_string(hdl) + ": asset not found");
        return loadMaterial(*asset);
    }

    Result<std::shared_ptr<vke_render::Material>> AssetManager::LoadMaterial(const AssetHandle hdl)
    {
        auto result = getAssetForLoad(getMaterialAsset(hdl));
        if (!result)
            return std::unexpected("material " + std::to_string(hdl) + ": " + result.error());
        auto *asset = *result;
        if (!asset)
            return std::unexpected("material " + std::to_string(hdl) + ": asset not found");
        return loadFromCacheOrUpdate(*asset, [&]
                                     { return loadMaterial(*asset); });
    }

    Result<std::unique_ptr<Skeleton>> loadSkeleton(const SkeletonAsset &asset, const std::filesystem::path &fullPath)
    {
        const std::string context = "skeleton " + std::to_string(asset.id) + " (" + PathToUTF8(fullPath) + "): ";
        ozz::io::File file(_wfopen(fullPath.c_str(), L"rb"));
        if (!file.opened())
            return std::unexpected(context + "cannot open archive");
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
        auto result = GetSkeletonAssetReadOnly(hdl);
        if (!result)
            return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset)
            return std::unexpected(context + "asset not found");
        return loadSkeleton(*asset, resolveAssetPath(hdl, asset->path));
    }

    Result<std::shared_ptr<Skeleton>> AssetManager::LoadSkeleton(const AssetHandle hdl)
    {
        const std::string context = "skeleton " + std::to_string(hdl) + ": ";
        auto result = getAssetForLoad(getSkeletonAsset(hdl));
        if (!result)
            return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset)
            return std::unexpected(context + "asset not found");
        return loadFromCacheOrUpdate(*asset, [&]
                                     { return loadSkeleton(*asset, resolveAssetPath(hdl, asset->path)); });
    }

    Result<std::unique_ptr<Animation>> loadAnimation(const AnimationAsset &asset, const std::filesystem::path &fullPath)
    {
        const std::string context = "animation " + std::to_string(asset.id) + " (" + PathToUTF8(fullPath) + "): ";
        ozz::io::File file(_wfopen(fullPath.c_str(), L"rb"));
        if (!file.opened())
            return std::unexpected(context + "cannot open archive");
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
        auto result = GetAnimationAssetReadOnly(hdl);
        if (!result)
            return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset)
            return std::unexpected(context + "asset not found");
        return loadAnimation(*asset, resolveAssetPath(hdl, asset->path));
    }

    Result<std::shared_ptr<Animation>> AssetManager::LoadAnimation(const AssetHandle hdl)
    {
        const std::string context = "animation " + std::to_string(hdl) + ": ";
        auto result = getAssetForLoad(getAnimationAsset(hdl));
        if (!result)
            return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset)
            return std::unexpected(context + "asset not found");
        return loadFromCacheOrUpdate(*asset, [&]
                                     { return loadAnimation(*asset, resolveAssetPath(hdl, asset->path)); });
    }

    Result<std::unique_ptr<Font>> loadFont(const FontAsset &asset, const std::filesystem::path &fullPath, AssetUploadContext *upload)
    {
        const std::string context = "font " + std::to_string(asset.id) + " (" + PathToUTF8(fullPath) + "): ";
        if (asset.pixelSize == 0)
            return std::unexpected(context + "pixel size must be positive");
        auto ret = std::make_unique<Font>(asset.id);
        if (auto read = ReadFile(fullPath, ret->fontData); !read)
            return std::unexpected(context + read.error());
        if (ret->fontData.size() > static_cast<size_t>(std::numeric_limits<FT_Long>::max()))
            return std::unexpected(context + "font file exceeds FreeType size limit");
        FT_Error error = FT_Init_FreeType(&ret->library);
        if (error)
            return std::unexpected(context + "FT_Init_FreeType failed: " + std::to_string(error));
        error = FT_New_Memory_Face(ret->library,
                                   reinterpret_cast<const FT_Byte *>(ret->fontData.data()), static_cast<FT_Long>(ret->fontData.size()), 0, &(ret->face));
        if (error)
            return std::unexpected(context + "FT_New_Memory_Face failed: " + std::to_string(error));

        error = FT_Select_Charmap(ret->face, FT_ENCODING_UNICODE);
        if (error)
            return std::unexpected(context + "FT_Select_Charmap(Unicode) failed: " + std::to_string(error));

        error = FT_Set_Pixel_Sizes(ret->face, 0, asset.pixelSize);
        if (error)
            return std::unexpected(context + "FT_Set_Pixel_Sizes(" + std::to_string(asset.pixelSize) + ") failed: " + std::to_string(error));

        ret->familyName = ret->face->family_name == nullptr ? "" : ret->face->family_name;
        ret->styleName = ret->face->style_name == nullptr ? "" : ret->face->style_name;
        ret->glyphCount = static_cast<uint32_t>(ret->face->num_glyphs);
        ret->pixelSize = asset.pixelSize;
        ret->firstCodepoint = asset.firstCodepoint;
        ret->requestedCharacterCount = asset.characterCount;
        ret->ascender = static_cast<int>(ret->face->size->metrics.ascender >> 6);
        ret->descender = static_cast<int>(ret->face->size->metrics.descender >> 6);
        ret->lineHeight = static_cast<int>(ret->face->size->metrics.height >> 6);
        if (auto built = ret->PrepareStaticAtlas(asset.characters, asset.characterCount, asset.firstCodepoint); !built)
            return std::unexpected(context + built.error());
        using namespace vke_render;
        const auto &pixels = ret->GetStaticAtlasPixels();
        if (!upload)
        {
            ret->atlasTexture = std::make_shared<Texture2D>(asset.id, pixels.data(), Font::ATLAS_SIZE, Font::ATLAS_SIZE,
                                                            VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT,
                                                            VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
        }
        else
        {
            VkImage image;
            VmaAllocation allocation;
            VulkanResourceManager::CreateImage(Font::ATLAS_SIZE, Font::ATLAS_SIZE, VK_FORMAT_R8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                                               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                               1, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &image, &allocation, nullptr);
            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {Font::ATLAS_SIZE, Font::ATLAS_SIZE, 1};
            const auto commands = upload->UploadImage(image, pixels.data(), pixels.size(), 1, {&region, 1});
            VulkanResourceManager::MakeLayoutTransition(commands, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                        image, VK_IMAGE_ASPECT_COLOR_BIT, 1,
                                                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            ret->atlasTexture = std::make_shared<Texture2D>(asset.id, image, allocation, Font::ATLAS_SIZE, Font::ATLAS_SIZE, 1,
                                                            VK_FORMAT_R8_UNORM, VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                                                            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
        }
        return ret;
    }

    Result<std::unique_ptr<Font>> AssetManager::LoadFontUnique(const AssetHandle hdl)
    {
        const std::string context = "font " + std::to_string(hdl) + ": ";
        auto result = GetFontAssetReadOnly(hdl);
        if (!result)
            return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset)
            return std::unexpected(context + "asset not found");
        return loadFont(*asset, resolveAssetPath(hdl, asset->path));
    }

    Result<std::shared_ptr<Font>> AssetManager::LoadFont(const AssetHandle hdl)
    {
        const std::string context = "font " + std::to_string(hdl) + ": ";
        auto result = getAssetForLoad(getFontAsset(hdl));
        if (!result)
            return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset)
            return std::unexpected(context + "asset not found");
        return loadFromCacheOrUpdate(*asset, [&]
                                     { return loadFont(*asset, resolveAssetPath(hdl, asset->path)); });
    }

    Result<std::unique_ptr<vke_audio::AudioClip>> loadAudioClip(const AudioClipAsset &asset, const std::filesystem::path &fullPath)
    {
        const std::string context = "audio clip " + std::to_string(asset.id) + " (" + PathToUTF8(fullPath) + "): ";
        std::vector<char> encoded;
        if (auto read = ReadFile(fullPath, encoded); !read)
            return std::unexpected(context + read.error());
        auto clip = std::make_unique<vke_audio::AudioClip>(asset.id);
        auto config = ma_decoder_config_init(ma_format_f32, 0, 0);
        ma_uint64 frameCount = 0;
        void *samples = nullptr;
        const auto result = ma_decode_memory(encoded.data(), encoded.size(), &config, &frameCount, &samples);
        clip->samples.reset(static_cast<float *>(samples));
        if (result != MA_SUCCESS)
            return std::unexpected(context + "cannot decode audio: " + ma_result_description(result));
        if (frameCount == 0)
            return std::unexpected(context + "audio contains no frames");
        clip->frameCount = frameCount;
        clip->channels = config.channels;
        clip->sampleRate = config.sampleRate;
        return clip;
    }

    Result<std::unique_ptr<vke_audio::AudioClip>> AssetManager::LoadAudioClipUnique(const AssetHandle hdl)
    {
        const std::string context = "audio clip " + std::to_string(hdl) + ": ";
        auto result = GetAudioClipAssetReadOnly(hdl);
        if (!result)
            return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset)
            return std::unexpected(context + "asset not found");

        const auto path = resolveAssetPath(hdl, asset->path);
        return loadAudioClip(*asset, path);
    }

    Result<std::shared_ptr<vke_audio::AudioClip>> AssetManager::LoadAudioClip(const AssetHandle hdl)
    {
        const std::string context = "audio clip " + std::to_string(hdl) + ": ";
        auto result = getAssetForLoad(getAudioClipAsset(hdl));
        if (!result)
            return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset)
            return std::unexpected(context + "asset not found");
        return loadFromCacheOrUpdate(*asset, [&]
                                     { return loadAudioClip(*asset, resolveAssetPath(hdl, asset->path)); });
    }

    Result<std::unique_ptr<vke_navmesh::NavAgentConfig>> loadNavAgentConfig(const NavAgentConfigAsset &asset)
    {
        const std::string context = "nav agent config " + std::to_string(asset.id) + ": ";
        if (auto valid = asset.config.Validate(); !valid)
            return std::unexpected(context + valid.error());
        return std::make_unique<vke_navmesh::NavAgentConfig>(asset.id, asset.config);
    }

    Result<std::unique_ptr<vke_navmesh::NavAgentConfig>> AssetManager::LoadNavAgentConfigUnique(const AssetHandle hdl)
    {
        const std::string context = "nav agent config " + std::to_string(hdl) + ": ";
        auto result = GetNavAgentConfigAssetReadOnly(hdl);
        if (!result)
            return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset)
            return std::unexpected(context + "asset not found");
        return loadNavAgentConfig(*asset);
    }

    Result<std::shared_ptr<vke_navmesh::NavAgentConfig>> AssetManager::LoadNavAgentConfig(const AssetHandle hdl)
    {
        const std::string context = "nav agent config " + std::to_string(hdl) + ": ";
        auto result = getAssetForLoad(getNavAgentConfigAsset(hdl));
        if (!result)
            return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset)
            return std::unexpected(context + "asset not found");
        return loadFromCacheOrUpdate(*asset, [&]
                                     { return loadNavAgentConfig(*asset); });
    }

    Result<std::unique_ptr<vke_navmesh::NavMeshTilesData>> loadNavMeshTiles(const NavMeshTilesAsset &asset, const std::filesystem::path &fullPath)
    {
        auto tiles = vke_navmesh::ReadNavMeshFile(fullPath);
        if (!tiles)
            return std::unexpected(tiles.error());
        return std::make_unique<vke_navmesh::NavMeshTilesData>(
            asset.id, asset.navMeshId, asset.agentConfig, asset.settings, std::move(*tiles));
    }

    Result<std::unique_ptr<vke_navmesh::NavMeshTilesData>> AssetManager::LoadNavMeshTilesUnique(const AssetHandle hdl)
    {
        const std::string context = "navmesh " + std::to_string(hdl) + ": ";
        auto result = GetNavMeshTilesAssetReadOnly(hdl);
        if (!result)
            return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset)
            return std::unexpected(context + "asset not found");
        return loadNavMeshTiles(*asset, resolveAssetPath(hdl, asset->path));
    }

    Result<std::shared_ptr<vke_navmesh::NavMeshTilesData>> AssetManager::LoadNavMeshTiles(const AssetHandle hdl)
    {
        const std::string context = "navmesh " + std::to_string(hdl) + ": ";
        auto result = getAssetForLoad(getNavMeshTilesAsset(hdl));
        if (!result)
            return std::unexpected(context + result.error());
        auto *asset = *result;
        if (!asset)
            return std::unexpected(context + "asset not found");
        return loadFromCacheOrUpdate(*asset, [&]
                                     { return loadNavMeshTiles(*asset, resolveAssetPath(hdl, asset->path)); });
    }
}
