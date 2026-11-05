#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace vke_common
{
    std::filesystem::path PathFromUTF8(std::string_view text);
    std::string PathToUTF8(const std::filesystem::path &path);

    class Paths
    {
    public:
        static void Init(const std::filesystem::path &configFile,
                         const std::filesystem::path &projectRoot = ".",
                         const std::filesystem::path &engineRoot = {});
        static const std::filesystem::path &ProjectRoot();
        static std::filesystem::path Project(const std::filesystem::path &path);
        static std::filesystem::path Engine(const std::filesystem::path &path);
        static std::filesystem::path ProjectRelative(const std::filesystem::path &path);

    private:
        static std::filesystem::path projectRoot;
        static std::filesystem::path engineRoot;
        static std::filesystem::path resolve(const std::filesystem::path &root,
                                             const std::filesystem::path &path);
    };
}
