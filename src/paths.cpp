#include <paths.hpp>
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace vke_common
{
    std::filesystem::path Paths::projectRoot;
    std::filesystem::path Paths::engineRoot;

    std::filesystem::path PathFromUTF8(std::string_view text)
    {
        return std::filesystem::path(std::u8string(
            reinterpret_cast<const char8_t *>(text.data()), text.size()));
    }

    std::string PathToUTF8(const std::filesystem::path &path)
    {
        const auto text = path.generic_u8string();
        return std::string(reinterpret_cast<const char *>(text.data()), text.size());
    }

    void Paths::Init(const std::filesystem::path &configFile,
                     const std::filesystem::path &project, const std::filesystem::path &engine)
    {
        const auto configDirectory = std::filesystem::absolute(configFile).parent_path();
        projectRoot = std::filesystem::weakly_canonical(configDirectory / project);
        if (!engine.empty())
            engineRoot = std::filesystem::weakly_canonical(configDirectory / engine);
        else
        {
            std::wstring executable(32768, L'\0');
            const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
            if (length == 0 || length == executable.size())
                throw std::runtime_error("Cannot locate engine executable");
            executable.resize(length);
            // Executables live in <engine root>/out (or another single bin directory).
            engineRoot = std::filesystem::path(executable).parent_path().parent_path();
        }
    }

    const std::filesystem::path &Paths::ProjectRoot() { return projectRoot; }

    std::filesystem::path Paths::resolve(const std::filesystem::path &root, const std::filesystem::path &path)
    {
        if (root.empty()) throw std::logic_error("Paths must be initialized before loading files");
        const auto normalized = path.lexically_normal();
        if (path.has_root_path() || (!normalized.empty() && *normalized.begin() == ".."))
            throw std::invalid_argument("Expected a path within its root: " + PathToUTF8(path));
        return (root / normalized).lexically_normal();
    }

    std::filesystem::path Paths::Project(const std::filesystem::path &path) { return resolve(projectRoot, path); }
    std::filesystem::path Paths::Engine(const std::filesystem::path &path) { return resolve(engineRoot, path); }

    std::filesystem::path Paths::ProjectRelative(const std::filesystem::path &path)
    {
        const auto full = std::filesystem::weakly_canonical(path.is_absolute() ? path : Project(path));
        const auto relative = full.lexically_relative(projectRoot);
        if (relative.empty() || relative.has_root_path() || *relative.begin() == "..")
            throw std::invalid_argument("File is outside the project: " + PathToUTF8(path));
        return relative;
    }
}
