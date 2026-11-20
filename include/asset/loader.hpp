#pragma once

#include <asset/asset.hpp>

namespace vke_common
{
    class AssetUploadContext;

    Result<std::unique_ptr<vke_render::Texture2D>> loadTexture2D(const AssetHandle hdl, const TextureAsset &asset, const std::filesystem::path &fullPath, AssetUploadContext *upload = nullptr);
    Result<std::unique_ptr<vke_render::Mesh>> loadMesh(const AssetHandle hdl, const std::filesystem::path &path, AssetUploadContext *upload = nullptr);
    Result<std::unique_ptr<vke_render::ShaderModuleSet>> loadVertFragShader(const std::filesystem::path &vpth, const std::filesystem::path &fpth);
    Result<std::unique_ptr<vke_render::ShaderModuleSet>> loadComputeShader(const std::filesystem::path &pth);
    Result<std::unique_ptr<Skeleton>> loadSkeleton(const SkeletonAsset &asset, const std::filesystem::path &fullPath);
    Result<std::unique_ptr<Animation>> loadAnimation(const AnimationAsset &asset, const std::filesystem::path &fullPath);
    Result<std::unique_ptr<Font>> loadFont(const FontAsset &asset, const std::filesystem::path &fullPath, AssetUploadContext *upload = nullptr);
    Result<std::unique_ptr<vke_audio::AudioClip>> loadAudioClip(const AudioClipAsset &asset, const std::filesystem::path &fullPath);
    Result<std::unique_ptr<vke_navmesh::NavAgentConfig>> loadNavAgentConfig(const NavAgentConfigAsset &asset);
    Result<std::unique_ptr<vke_navmesh::NavMeshData>> loadNavMesh(const std::filesystem::path &fullPath);
}
