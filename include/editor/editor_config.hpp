#ifndef EDITOR_CONFIG_H
#define EDITOR_CONFIG_H

#include <game_config.hpp>
#include <asset/asset.hpp>
#include <filesystem>

namespace vke_editor
{
    struct EditorConfig
    {
    private:
        static EditorConfig *instance;

        EditorConfig(const EditorConfig &) = delete;
        EditorConfig &operator=(const EditorConfig &) = delete;

    public:
        uint32_t windowWidth;
        uint32_t windowHeight;
        std::filesystem::path assetDBPath;    // relative to project root
        std::filesystem::path gameConfigPath; // relative to project root
        vke_common::GameConfig *gameConfig;

        static vke_common::SceneResult<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            auto valid = Object(json).Require({"windowWidth", "windowHeight", "assetDBPath"}).Unsigneds({"windowWidth", "windowHeight"}, INT32_MAX).Strings({"projectRoot", "engineRoot"}).RelativePaths({"assetDBPath", "gameConfigPath"}).Result();
            if (!valid)
                return valid;
            if (json["windowWidth"] == 0 || json["windowHeight"] == 0)
                return std::unexpected("window dimensions must be positive");
            return {};
        }

        EditorConfig() : windowWidth(1920), windowHeight(1080), gameConfig(nullptr) {};

        EditorConfig(const nlohmann::json &json)
            : windowWidth(json["windowWidth"]), windowHeight(json["windowHeight"]),
              assetDBPath(vke_common::PathFromUTF8(json["assetDBPath"].get<std::string>())),
              gameConfigPath(vke_common::PathFromUTF8(json.value("gameConfigPath", ""))),
              gameConfig(nullptr) {}

        static EditorConfig *GetInstance()
        {
            VKE_FATAL_IF(instance == nullptr, "EditorConfig not initialized!")
            return instance;
        }

        static vke_common::SceneResult<EditorConfig *> Init(const nlohmann::json &json)
        {
            auto config = std::make_unique<EditorConfig>(json);
            auto gameConfigJSON = vke_common::LoadJSON(
                vke_common::Paths::Project(config->gameConfigPath));
            if (!gameConfigJSON)
                return std::unexpected(gameConfigJSON.error());
            if (auto valid = vke_common::GameConfig::ValidateJSON(*gameConfigJSON); !valid)
                return std::unexpected(vke_common::PathToUTF8(config->gameConfigPath) + ": " + valid.error());
            config->gameConfig = vke_common::GameConfig::Init(*gameConfigJSON);
            instance = config.release();
            return instance;
        }

        static void Dispose()
        {
            vke_common::GameConfig::Dispose();
            delete instance;
            instance = nullptr;
        }
    };
}

#endif
