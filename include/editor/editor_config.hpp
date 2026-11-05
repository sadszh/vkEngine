#ifndef EDITOR_CONFIG_H
#define EDITOR_CONFIG_H

#include <game_config.hpp>
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

        static EditorConfig *Init(const nlohmann::json &json)
        {
            instance = new EditorConfig(json);
            const nlohmann::json &gameConfigJSON = vke_common::LoadJSON(
                vke_common::Paths::Project(instance->gameConfigPath));
            instance->gameConfig = vke_common::GameConfig::Init(gameConfigJSON);
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
