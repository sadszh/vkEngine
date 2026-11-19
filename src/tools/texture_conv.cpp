#include <tools/texture_conversion.hpp>
#include <charconv>
#include <iostream>
#include <map>

int wmain(int argc, wchar_t **wideArgs)
{
    std::vector<std::string> argv;
    for (int i = 0; i < argc; ++i)
        argv.push_back(vke_common::PathToUTF8(std::filesystem::path(wideArgs[i])));
    using namespace vke_tools;
    if (argc < 3)
    {
        std::cerr << "Usage: texture_conv <input> <output.texture> [--purpose color|normal|data]\n"
                     "       [--format r8|rg8|rgba8|rgba8-srgb|rgba16|rgba16f|bc7|bc7-srgb] [--no-mips]\n"
                     "       [--width pixels] [--height pixels] (SVG raster dimensions; 0 = automatic)\n"
                     "Writes a .texture file with an embedded metadata header.\n";
        return 1;
    }
    TextureOptions options;
    int rasterWidth = 0, rasterHeight = 0;
    const std::map<std::string, VkFormat> formats{
        {"r8", VK_FORMAT_R8_UNORM}, {"rg8", VK_FORMAT_R8G8_UNORM},
        {"rgba8", VK_FORMAT_R8G8B8A8_UNORM}, {"rgba8-srgb", VK_FORMAT_R8G8B8A8_SRGB},
        {"rgba16", VK_FORMAT_R16G16B16A16_UNORM}, {"rgba16f", VK_FORMAT_R16G16B16A16_SFLOAT},
        {"bc7", VK_FORMAT_BC7_UNORM_BLOCK}, {"bc7-srgb", VK_FORMAT_BC7_SRGB_BLOCK}};
    for (int i = 3; i < argc; ++i)
    {
        const std::string option = argv[i];
        if (option == "--no-mips") options.mipmaps = false;
        else if (option == "--purpose" && i + 1 < argc)
        {
            const std::string purpose = argv[++i];
            if (purpose == "color") options.purpose = TexturePurpose::Color;
            else if (purpose == "normal") options.purpose = TexturePurpose::Normal;
            else if (purpose == "data") options.purpose = TexturePurpose::Data;
            else { std::cerr << "Invalid purpose\n"; return 1; }
        }
        else if (option == "--format" && i + 1 < argc)
        {
            const auto found = formats.find(argv[++i]);
            if (found == formats.end()) { std::cerr << "Invalid format\n"; return 1; }
            options.format = found->second;
        }
        else if ((option == "--width" || option == "--height") && i + 1 < argc)
        {
            const auto &value = argv[++i];
            int &dimension = option == "--width" ? rasterWidth : rasterHeight;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), dimension);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
            {
                std::cerr << "Invalid raster dimension: " << value << '\n';
                return 1;
            }
        }
        else { std::cerr << "Unknown or incomplete option: " << option << '\n'; return 1; }
    }
    auto source = LoadTextureSource(vke_common::PathFromUTF8(argv[1]), rasterWidth, rasterHeight);
    if (!source) { std::cerr << source.error() << '\n'; return 1; }
    const auto path = vke_common::PathFromUTF8(argv[2]);
    auto metadata = WriteTexture(std::move(*source), path, options);
    if (!metadata) { std::cerr << metadata.error() << '\n'; return 1; }
    std::cout << vke_common::PathToUTF8(path) << '\n';
    return 0;
}
