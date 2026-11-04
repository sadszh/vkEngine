#ifndef ASSET_SVG_H
#define ASSET_SVG_H

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace vke_common
{
    inline constexpr int MaxSVGDimension = 4096;

    struct SVGImage
    {
        int width;
        int height;
        std::vector<uint8_t> pixels;
    };

    bool IsSVGPath(const std::filesystem::path &path);
    std::expected<SVGImage, std::string> RasterizeSVG(std::string_view source, int width = 0, int height = 0);
    std::expected<SVGImage, std::string> LoadSVG(const std::filesystem::path &path, int width = 0, int height = 0);
}

#endif
