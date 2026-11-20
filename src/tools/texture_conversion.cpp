#include <tools/texture_conversion.hpp>
#include <binary_io.hpp>
#include <tools/svg.hpp>
#include <stb/stb_image.h>
#include <bc7enc/bc7enc.h>
#include <mutex>
#include <glm/gtc/packing.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>

namespace vke_tools
{
    using namespace vke_common;

    Result<TextureSource> DecodeTextureSource(std::span<const unsigned char> pixels,
                                              int width, int height, int channels, int bits)
    {
        if (width <= 0 || height <= 0 || channels < 1 || channels > 4 || (bits != 8 && bits != 16))
            return std::unexpected("unsupported decoded image dimensions, channels or bit depth");
        const uint64_t count = uint64_t(width) * height;
        if (count > pixels.size() / (channels * (bits / 8)))
            return std::unexpected("truncated decoded image");
        TextureSource source{static_cast<uint32_t>(width), static_cast<uint32_t>(height), false};
        source.rgba.resize(static_cast<size_t>(count) * 4);
        for (size_t i = 0; i < count; ++i)
        {
            float components[4]{0, 0, 0, 1};
            for (int c = 0; c < channels; ++c)
            {
                const size_t index = i * channels + c;
                uint16_t value;
                if (bits == 16)
                    std::memcpy(&value, pixels.data() + index * 2, 2);
                else
                    value = pixels[index];
                components[c] = value / (bits == 16 ? 65535.0f : 255.0f);
            }
            if (channels <= 2)
            {
                components[3] = channels == 2 ? components[1] : 1.0f;
                components[1] = components[2] = components[0];
            }
            std::copy_n(components, 4, source.rgba.data() + i * 4);
        }
        return source;
    }

    Result<TextureSource> LoadTextureSource(const std::filesystem::path &path, int rasterWidth, int rasterHeight)
    {
        if (IsSVGPath(path))
        {
            auto image = LoadSVG(path, rasterWidth, rasterHeight);
            if (!image)
                return std::unexpected(image.error());
            return DecodeTextureSource(image->pixels, image->width, image->height, 4, 8);
        }
        if (rasterWidth != 0 || rasterHeight != 0)
            return std::unexpected("raster dimensions require an SVG input");
        std::unique_ptr<FILE, decltype(&fclose)> file(_wfopen(path.c_str(), L"rb"), fclose);
        if (!file)
            return std::unexpected("cannot open source image: " + PathToUTF8(path));
        int width, height, channels;
        const bool hdr = stbi_is_hdr_from_file(file.get()) != 0;
        const bool sixteen = !hdr && stbi_is_16_bit_from_file(file.get());
        void *decoded = hdr       ? static_cast<void *>(stbi_loadf_from_file(file.get(), &width, &height, &channels, 4))
                        : sixteen ? static_cast<void *>(stbi_load_from_file_16(file.get(), &width, &height, &channels, 4))
                                  : static_cast<void *>(stbi_load_from_file(file.get(), &width, &height, &channels, 4));
        std::unique_ptr<void, decltype(&stbi_image_free)> pixels(decoded, stbi_image_free);
        if (!pixels)
            return std::unexpected(std::string("image decode failed: ") + (stbi_failure_reason() ? stbi_failure_reason() : "unknown error"));
        if (!hdr)
            return DecodeTextureSource({static_cast<unsigned char *>(decoded), size_t(width) * height * 4 * (sixteen ? 2 : 1)},
                                       width, height, 4, sixteen ? 16 : 8);
        const auto *values = static_cast<const float *>(decoded);
        return TextureSource{static_cast<uint32_t>(width), static_cast<uint32_t>(height), true, {values, values + size_t(width) * height * 4}};
    }

    namespace
    {
        float linear(float value)
        {
            return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        float srgb(float value)
        {
            return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
        }

        void normalizeNormals(std::vector<float> &pixels)
        {
            for (size_t i = 0; i < pixels.size(); i += 4)
            {
                float x = pixels[i] * 2 - 1, y = pixels[i + 1] * 2 - 1, z = pixels[i + 2] * 2 - 1;
                const float length = std::sqrt(x * x + y * y + z * z);
                if (length > 1e-8f)
                {
                    x /= length;
                    y /= length;
                    z /= length;
                }
                else
                {
                    x = y = 0;
                    z = 1;
                }
                pixels[i] = x * 0.5f + 0.5f;
                pixels[i + 1] = y * 0.5f + 0.5f;
                pixels[i + 2] = z * 0.5f + 0.5f;
            }
        }

        std::vector<unsigned char> compressBC7(const std::vector<unsigned char> &rgba,
                                               uint32_t width, uint32_t height, bool perceptual)
        {
            static std::once_flag initialized;
            std::call_once(initialized, bc7enc_compress_block_init);
            bc7enc_compress_block_params params;
            bc7enc_compress_block_params_init(&params);
            if (!perceptual)
                bc7enc_compress_block_params_init_linear_weights(&params);
            const uint32_t blocksX = (width + 3) / 4, blocksY = (height + 3) / 4;
            std::vector<unsigned char> result(size_t(blocksX) * blocksY * BC7ENC_BLOCK_SIZE);
            for (uint32_t y = 0; y < blocksY; ++y)
                for (uint32_t x = 0; x < blocksX; ++x)
                {
                    color_rgba block[16];
                    for (uint32_t by = 0; by < 4; ++by)
                        for (uint32_t bx = 0; bx < 4; ++bx)
                        {
                            // Replicate edge texels for partial blocks and small mip levels.
                            const size_t pixel = size_t(std::min(y * 4 + by, height - 1)) * width +
                                                 std::min(x * 4 + bx, width - 1);
                            std::copy_n(rgba.data() + pixel * 4, 4, block[by * 4 + bx].m_c);
                        }
                    bc7enc_compress_block(result.data() + (size_t(y) * blocksX + x) * BC7ENC_BLOCK_SIZE,
                                          block, &params);
                }
            return result;
        }

        std::vector<float> downsample(const std::vector<float> &pixels, uint32_t width, uint32_t height,
                                      uint32_t nextWidth, uint32_t nextHeight, TexturePurpose purpose)
        {
            std::vector<float> result(size_t(nextWidth) * nextHeight * 4);
            // Area weights include the edge texels of odd-sized images.
            for (uint32_t y = 0; y < nextHeight; ++y)
                for (uint32_t x = 0; x < nextWidth; ++x)
                {
                    const double left = double(x) * width / nextWidth, right = double(x + 1) * width / nextWidth;
                    const double top = double(y) * height / nextHeight, bottom = double(y + 1) * height / nextHeight;
                    double sum[4]{};
                    for (uint32_t sy = uint32_t(top); sy < uint32_t(std::ceil(bottom)); ++sy)
                        for (uint32_t sx = uint32_t(left); sx < uint32_t(std::ceil(right)); ++sx)
                        {
                            const double weight = (std::min(right, double(sx + 1)) - std::max(left, double(sx))) *
                                                  (std::min(bottom, double(sy + 1)) - std::max(top, double(sy)));
                            const float *pixel = pixels.data() + (size_t(sy) * width + sx) * 4;
                            for (int c = 0; c < 4; ++c)
                                sum[c] += weight * pixel[c] * (purpose == TexturePurpose::Color && c < 3 ? pixel[3] : 1);
                        }
                    float *pixel = result.data() + (size_t(y) * nextWidth + x) * 4;
                    for (int c = 0; c < 4; ++c)
                    {
                        const double divisor = purpose == TexturePurpose::Color && c < 3 ? sum[3] : (right - left) * (bottom - top);
                        pixel[c] = divisor > 0 ? static_cast<float>(sum[c] / divisor) : 0;
                    }
                }
            if (purpose == TexturePurpose::Normal)
                normalizeNormals(result);
            return result;
        }
    }

    Result<TextureMetadata> WriteTexture(TextureSource source, const std::filesystem::path &path, const TextureOptions &options)
    {
        if (!IsTexturePath(path))
            return std::unexpected("output path must have .texture extension");
        const VkFormat format = options.format != VK_FORMAT_UNDEFINED       ? options.format
                                : source.hdr                                ? VK_FORMAT_R16G16B16A16_SFLOAT
                                : options.purpose == TexturePurpose::Color  ? VK_FORMAT_BC7_SRGB_BLOCK
                                : options.purpose == TexturePurpose::Normal ? VK_FORMAT_R8G8B8A8_UNORM
                                                                            : VK_FORMAT_BC7_UNORM_BLOCK;
        const bool bc7 = format == VK_FORMAT_BC7_UNORM_BLOCK || format == VK_FORMAT_BC7_SRGB_BLOCK;
        const bool srgbOutput = format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_BC7_SRGB_BLOCK;
        if (srgbOutput && options.purpose != TexturePurpose::Color)
            return std::unexpected("sRGB output requires color purpose");
        if (options.purpose == TexturePurpose::Normal && (format == VK_FORMAT_R8_UNORM || format == VK_FORMAT_R8G8_UNORM))
            return std::unexpected("normal textures require all three normal components");
        TextureMetadata metadata{1, source.width, source.height,
                                 options.mipmaps ? static_cast<uint32_t>(std::bit_width(std::max(source.width, source.height))) : 1u, format};
        auto layout = metadata.BuildLayout();
        if (!layout)
            return std::unexpected(layout.error());
        if (uint64_t(source.width) * source.height * 4 != source.rgba.size())
            return std::unexpected("source pixel count does not match dimensions");
        for (size_t i = 0; i < source.rgba.size(); ++i)
        {
            if (!std::isfinite(source.rgba[i]))
                return std::unexpected("source contains non-finite samples");
            if (options.purpose == TexturePurpose::Color && !source.hdr && i % 4 != 3)
                source.rgba[i] = linear(source.rgba[i]);
        }
        if (options.purpose == TexturePurpose::Normal)
            normalizeNormals(source.rgba);
        std::ofstream output(path, std::ios::binary);
        if (!output)
            return std::unexpected("cannot open output: " + PathToUTF8(path));
        metadata.WriteHeader(output);
        const uint32_t texelSize = bc7 ? 4 : GetTextureFormatInfo(format).bytesPerBlock;
        const uint32_t channels = std::min(texelSize, 4u);
        uint64_t written = 0;
        uint32_t width = source.width, height = source.height;
        for (const auto &region : layout->regions)
        {
            const char padding[16]{};
            WriteBytes(output, padding, region.bufferOffset - written);
            std::vector<unsigned char> encoded(size_t(width) * height * texelSize);
            for (size_t i = 0; i < size_t(width) * height; ++i)
                for (uint32_t c = 0; c < channels; ++c)
                {
                    float value = source.rgba[i * 4 + c];
                    if (srgbOutput && c < 3)
                        value = srgb(value);
                    if (texelSize == 8)
                    {
                        const uint16_t bits = format == VK_FORMAT_R16G16B16A16_SFLOAT
                                                  ? glm::packHalf1x16(std::clamp(value, -65504.0f, 65504.0f))
                                                  : static_cast<uint16_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 65535));
                        encoded[i * 8 + c * 2] = static_cast<unsigned char>(bits);
                        encoded[i * 8 + c * 2 + 1] = static_cast<unsigned char>(bits >> 8);
                    }
                    else
                        encoded[i * texelSize + c] = static_cast<unsigned char>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255));
                }
            if (bc7)
                encoded = compressBC7(encoded, width, height, srgbOutput);
            WriteBytes(output, encoded.data(), encoded.size());
            written = region.bufferOffset + encoded.size();
            if (region.imageSubresource.mipLevel + 1 < metadata.mipCount)
            {
                const auto nextWidth = std::max(1u, width / 2), nextHeight = std::max(1u, height / 2);
                source.rgba = downsample(source.rgba, width, height, nextWidth, nextHeight, options.purpose);
                width = nextWidth;
                height = nextHeight;
            }
        }
        output.close();
        if (!output)
            return std::unexpected("cannot write texture: " + PathToUTF8(path));
        return metadata;
    }
}
