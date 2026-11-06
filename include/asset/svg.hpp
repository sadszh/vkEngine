#ifndef ASSET_SVG_H
#define ASSET_SVG_H

#include <common.hpp>

#include <cstdint>
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
    Result<SVGImage> RasterizeSVG(std::string_view source, int width = 0, int height = 0);
    Result<SVGImage> LoadSVG(const std::filesystem::path &path, int width = 0, int height = 0);
}

#endif
