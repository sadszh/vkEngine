#include <asset/texture_data.hpp>
#include <array>
#include <algorithm>
#include <bit>
#include <cctype>

namespace vke_common
{
    bool IsTexturePath(const std::filesystem::path &path)
    {
        auto extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return extension == ".texture";
    }

    TextureFormatInfo GetTextureFormatInfo(VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_R8_UNORM:
            return {1, 1, 1};
        case VK_FORMAT_R8G8_UNORM:
            return {1, 1, 2};
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            return {1, 1, 4};
        case VK_FORMAT_R16G16B16A16_UNORM:
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return {1, 1, 8};
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return {4, 4, 16};
        default:
            return {0, 0, 0};
        }
    }

    Result<TextureLayout> TextureMetadata::BuildLayout() const
    {
        if (textureVersion != 1)
            return std::unexpected("unsupported textureVersion (expected 1)");
        const auto formatInfo = GetTextureFormatInfo(format);
        if (formatInfo.bytesPerBlock == 0)
            return std::unexpected("unsupported texture format");
        if (width == 0 || height == 0 ||
            width > INT32_MAX || height > INT32_MAX)
            return std::unexpected("texture dimensions must be positive int32 values");
        if (mipCount == 0 || mipCount > std::bit_width(std::max(width, height)))
            return std::unexpected("invalid texture mipCount");

        TextureLayout layout;
        const uint64_t alignment = std::max(4u, formatInfo.bytesPerBlock);
        uint32_t mipWidth = width, mipHeight = height;
        for (uint32_t mip = 0; mip < mipCount; ++mip)
        {
            const uint64_t blocks = ((uint64_t(mipWidth) + formatInfo.blockWidth - 1) / formatInfo.blockWidth) *
                                    ((uint64_t(mipHeight) + formatInfo.blockHeight - 1) / formatInfo.blockHeight);
            if (blocks > uint64_t(INT64_MAX) / formatInfo.bytesPerBlock)
                return std::unexpected("texture data exceeds supported file size");
            const uint64_t size = blocks * formatInfo.bytesPerBlock;
            if (layout.size > uint64_t(INT64_MAX) - (alignment - 1) ||
                size > uint64_t(INT64_MAX) - ((layout.size + alignment - 1) & ~(alignment - 1)))
                return std::unexpected("texture data exceeds supported file size");
            VkBufferImageCopy region{};
            region.bufferOffset = (layout.size + alignment - 1) & ~(alignment - 1);
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
            region.imageExtent = {mipWidth, mipHeight, 1};
            layout.regions.push_back(region);
            layout.size = region.bufferOffset + size;
            mipWidth = std::max(1u, mipWidth / 2);
            mipHeight = std::max(1u, mipHeight / 2);
        }
        return layout;
    }

    Result<TextureFileInfo> TextureFileInfo::Read(std::istream &input)
    {
        input.seekg(0);
        std::array<unsigned char, TextureHeaderSize> header{};
        if (!input.read(reinterpret_cast<char *>(header.data()), header.size()))
            return std::unexpected("cannot read texture header");
        if (header[0] != 'V' || header[1] != 'K' || header[2] != 'E' || header[3] != 'T')
            return std::unexpected("invalid texture magic");
        const auto field = [&](size_t offset)
        {
            return uint32_t(header[offset]) | (uint32_t(header[offset + 1]) << 8) |
                   (uint32_t(header[offset + 2]) << 16) | (uint32_t(header[offset + 3]) << 24);
        };
        TextureMetadata metadata{field(4), field(8), field(12), field(16), static_cast<VkFormat>(field(20))};
        auto layout = metadata.BuildLayout();
        if (!layout)
            return std::unexpected(layout.error());
        input.seekg(0, std::ios::end);
        const auto size = input.tellg();
        if (size < TextureHeaderSize || uint64_t(size - std::streamoff(TextureHeaderSize)) != layout->size)
            return std::unexpected("texture file size does not match header");
        input.seekg(TextureHeaderSize);
        if (!input)
            return std::unexpected("cannot seek texture payload");
        return TextureFileInfo{metadata, std::move(*layout)};
    }

    void TextureMetadata::WriteHeader(std::ostream &output) const
    {
        output.write("VKET", 4);
        for (uint32_t field : {textureVersion, width, height,
                               mipCount, static_cast<uint32_t>(format)})
        {
            const unsigned char bytes[]{static_cast<unsigned char>(field), static_cast<unsigned char>(field >> 8),
                                        static_cast<unsigned char>(field >> 16), static_cast<unsigned char>(field >> 24)};
            output.write(reinterpret_cast<const char *>(bytes), sizeof(bytes));
        }
    }
}
