#include <asset/svg.hpp>
#include <lunasvg/include/lunasvg.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace vke_common
{
    bool IsSVGPath(const std::filesystem::path &path)
    {
        const auto extension = path.extension().string();
        return extension.size() == 4 && extension[0] == '.' &&
               (extension[1] == 's' || extension[1] == 'S') &&
               (extension[2] == 'v' || extension[2] == 'V') &&
               (extension[3] == 'g' || extension[3] == 'G');
    }

    Result<SVGImage> RasterizeSVG(std::string_view source, int width, int height)
    {
        if (width < 0 || height < 0 || width > MaxSVGDimension || height > MaxSVGDimension)
            return std::unexpected("SVG raster dimensions must be between 0 and 4096");
        auto document = lunasvg::Document::loadFromData(source.data(), source.size());
        if (!document)
            return std::unexpected("unable to parse SVG document");
        const double intrinsicWidth = document->width();
        const double intrinsicHeight = document->height();
        if (!std::isfinite(intrinsicWidth) || !std::isfinite(intrinsicHeight) || intrinsicWidth <= 0 || intrinsicHeight <= 0)
            return std::unexpected("SVG must have finite positive intrinsic dimensions");

        const double outputWidth = width != 0 ? width : std::ceil(height != 0 ? height * intrinsicWidth / intrinsicHeight : intrinsicWidth);
        const double outputHeight = height != 0 ? height : std::ceil(width != 0 ? width * intrinsicHeight / intrinsicWidth : intrinsicHeight);
        if (outputWidth > MaxSVGDimension || outputHeight > MaxSVGDimension)
            return std::unexpected("resolved SVG raster dimensions exceed 4096");
        width = static_cast<int>(outputWidth);
        height = static_cast<int>(outputHeight);

        lunasvg::Bitmap bitmap(width, height);
        if (bitmap.isNull())
            return std::unexpected("unable to allocate SVG bitmap");
        bitmap.clear(0);
        const double scale = std::min(width / intrinsicWidth, height / intrinsicHeight);
        const double x = (width - intrinsicWidth * scale) * 0.5;
        const double y = (height - intrinsicHeight * scale) * 0.5;
        document->render(bitmap, lunasvg::Matrix(scale, 0, 0, scale, x, y));
        bitmap.convertToRGBA();
        SVGImage result{width, height, std::vector<uint8_t>(static_cast<size_t>(width) * height * 4)};
        const size_t rowBytes = static_cast<size_t>(width) * 4;
        for (int row = 0; row < height; ++row)
            std::memcpy(result.pixels.data() + row * rowBytes, bitmap.data() + row * bitmap.stride(), rowBytes);
        return result;
    }

    Result<SVGImage> LoadSVG(const std::filesystem::path &path, int width, int height)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            return std::unexpected("unable to open SVG file: " + PathToUTF8(path));
        const std::string source{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        if (stream.bad())
            return std::unexpected("unable to read SVG file: " + PathToUTF8(path));
        return RasterizeSVG(source, width, height);
    }
}
