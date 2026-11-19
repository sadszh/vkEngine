#pragma once

#include <common.hpp>
#include <vulkan/vulkan.h>
#include <istream>
#include <ostream>
#include <vector>

namespace vke_common
{
    struct TextureLayout
    {
        std::vector<VkBufferImageCopy> regions;
        uint64_t size = 0;
    };

    struct TextureMetadata
    {
        uint32_t textureVersion = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t mipCount = 0;
        VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;

        Result<TextureLayout> BuildLayout() const;
        void WriteHeader(std::ostream &output) const;
    };

    struct TextureFileInfo
    {
        TextureMetadata metadata;
        TextureLayout layout;

        static Result<TextureFileInfo> Read(std::istream &input);
    };

    inline constexpr uint32_t TextureHeaderSize = 24;

    bool IsTexturePath(const std::filesystem::path &path);
    struct TextureFormatInfo
    {
        uint32_t blockWidth;
        uint32_t blockHeight;
        uint32_t bytesPerBlock;
    };

    TextureFormatInfo GetTextureFormatInfo(VkFormat format);
}
