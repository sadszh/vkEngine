#ifndef GAME_CONFIG_H
#define GAME_CONFIG_H

#include <common.hpp>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <physics/physics_config.hpp>
#include <render/render_config.hpp>
#include <reflect/reflect.hpp>

namespace vke_common
{
    REFLECT_STRUCT(GameConfig)
    {
    private:
        static GameConfig *instance;

        GameConfig(const GameConfig &);
        GameConfig &operator=(const GameConfig &);

    public:
        REFLECT_FIELD(uint32_t, windowWidth);
        REFLECT_FIELD(uint32_t, windowHeight);
        REFLECT_FIELD(bool, enableVulkanValidationLayers);
        REFLECT_FIELD(std::string, assetLUTPath);
        REFLECT_FIELD(std::string, defaultScenePath);
        REFLECT_FIELD(std::string, gameScriptPath);
        REFLECT_FIELD(std::string, gameScriptTypeInfoPath);
        vke_physics::PhysicsConfig physicsConfig;
        vke_render::RenderConfig renderConfig;
        nlohmann::json sourceJSON;

        static SceneResult<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace json_validation;
            auto valid = Object(json).Unsigneds({"windowWidth", "windowHeight"}, INT32_MAX).Booleans({"enableVulkanValidationLayers"}).Strings({"projectRoot", "engineRoot"}).RelativePaths({"assetLUTPath", "defaultScenePath", "gameScriptPath", "gameScriptTypeInfoPath"}).Result();
            if (!valid)
                return valid;
            for (auto field : {"windowWidth", "windowHeight"})
                if (json.contains(field) && json[field] == 0)
                    return std::unexpected(std::string(field) + " must be positive");
            if (json.contains("physicsConfig"))
                if (auto result = vke_physics::PhysicsConfig::ValidateJSON(json["physicsConfig"]); !result)
                    return std::unexpected("physicsConfig: " + result.error());
            if (json.contains("renderConfig"))
                if (auto result = vke_render::RenderConfig::ValidateJSON(json["renderConfig"]); !result)
                    return std::unexpected("renderConfig: " + result.error());
            return {};
        }

        GameConfig() : windowWidth(1920), windowHeight(1080), enableVulkanValidationLayers(true),
                       assetLUTPath(), defaultScenePath(), gameScriptPath(),
                       gameScriptTypeInfoPath(), physicsConfig(), renderConfig() {}
        GameConfig(const nlohmann::json &json) : GameConfig()
        {
            sourceJSON = json;
            loadJSON(json);
            if (json.contains("physicsConfig"))
                physicsConfig.LoadJSON(json["physicsConfig"]);
            if (json.contains("renderConfig"))
                renderConfig.LoadJSON(json["renderConfig"]);
        }

        static GameConfig *GetInstance()
        {
            VKE_FATAL_IF(instance == nullptr, "GameConfig not initialized!")
            return instance;
        }

        static GameConfig *Init(const nlohmann::json &json)
        {
            instance = new GameConfig(json);
            return instance;
        }
        static GameConfig *Init()
        {
            instance = new GameConfig();
            return instance;
        }

        static void Dispose()
        {
            delete instance;
            instance = nullptr;
        }

    private:
        LOAD_JSON;
    };
}

#endif
