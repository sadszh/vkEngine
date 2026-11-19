#ifndef TOOLS_SVG_H
#define TOOLS_SVG_H

#include <common.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace vke_tools
{
    inline constexpr int MaxSVGDimension = 4096;

    struct SVGImage
    {
        int width;
        int height;
        std::vector<uint8_t> pixels;
    };

    bool IsSVGPath(const std::filesystem::path &path);
    vke_common::Result<SVGImage> RasterizeSVG(std::string_view source, int width = 0, int height = 0);
    vke_common::Result<SVGImage> LoadSVG(const std::filesystem::path &path, int width = 0, int height = 0);
}

#endif
