#pragma once

#include <asset/texture_data.hpp>
#include <span>

namespace vke_tools
{
    enum class TexturePurpose
    {
        Color,
        Normal,
        Data
    };
    struct TextureSource
    {
        uint32_t width = 0, height = 0;
        bool hdr = false;
        std::vector<float> rgba;
    };
    struct TextureOptions
    {
        TexturePurpose purpose = TexturePurpose::Color;
        VkFormat format = VK_FORMAT_UNDEFINED;
        bool mipmaps = true;
    };

    vke_common::Result<TextureSource> LoadTextureSource(const std::filesystem::path &path,
                                                        int rasterWidth = 0, int rasterHeight = 0);
    vke_common::Result<TextureSource> DecodeTextureSource(std::span<const unsigned char> pixels,
                                                          int width, int height, int channels, int bits);
    vke_common::Result<vke_common::TextureMetadata> WriteTexture(
        TextureSource source, const std::filesystem::path &path, const TextureOptions &options);
}
