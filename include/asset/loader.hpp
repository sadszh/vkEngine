#pragma once

#include <asset/asset.hpp>

namespace vke_common
{
    Result<AssetFinalizer<vke_render::Texture2D>> prepareTexture2D(const AssetHandle hdl, const TextureAsset &asset, const std::filesystem::path &fullPath);
    Result<AssetFinalizer<vke_render::Mesh>> prepareMesh(const AssetHandle hdl, const std::filesystem::path &path);
    Result<AssetFinalizer<vke_render::ShaderModuleSet>> prepareVertFragShader(const std::filesystem::path &vpth, const std::filesystem::path &fpth);
    Result<AssetFinalizer<vke_render::ShaderModuleSet>> prepareComputeShader(const std::filesystem::path &pth);
    Result<AssetFinalizer<Skeleton>> prepareSkeleton(const SkeletonAsset &asset, const std::filesystem::path &fullPath);
    Result<AssetFinalizer<Animation>> prepareAnimation(const AnimationAsset &asset, const std::filesystem::path &fullPath);
    Result<AssetFinalizer<Font>> prepareFont(const FontAsset &asset, const std::filesystem::path &fullPath);
    Result<AssetFinalizer<vke_audio::AudioClip>> prepareAudioClip(const AudioClipAsset &asset, const std::filesystem::path &fullPath);
}
