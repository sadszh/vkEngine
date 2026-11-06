#ifndef RENDER_CONFIG_H
#define RENDER_CONFIG_H

#include <json_validation.hpp>
#include <array>
#include <cmath>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace vke_render
{
    constexpr uint32_t MAX_DIRECTIONAL_SHADOW_CASCADE_CNT = 4;
    constexpr uint32_t MAX_SPOT_LIGHT_SHADOW_CNT = 32;

    struct AtmosphereParameter
    {
        float seaLevel = 0.0f;
        float planetRadius = 6360000.0f;
        float atmosphereHeight = 60000.0f;
        float sunDiskAngle = 2.0f;
        float sunLightIntensityFactor = 1.0f;
        float rayleighScatteringScale = 1.0f;
        float rayleighScatteringScalarHeight = 8000.0f;
        float mieScatteringScale = 1.0f;
        float mieAnisotropy = 0.8f;
        float mieScatteringScalarHeight = 1200.0f;
        float ozoneAbsorptionScale = 1.0f;
        float ozoneLevelCenterHeight = 25000.0f;
        float ozoneLevelWidth = 15000.0f;
        float aerialPerspectiveDistance = 32000.0f;
        float aerialPerspectiveScale = 1.0f;

        AtmosphereParameter() = default;
        explicit AtmosphereParameter(const nlohmann::json &json)
        {
            LoadJSON(json);
        }

        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            if (json.is_null())
                return {};
            return Object(json).Numbers({"seaLevel", "planetRadius", "atmosphereHeight", "sunDiskAngle", "sunLightIntensityFactor", "rayleighScatteringScale", "rayleighScatteringScalarHeight", "mieScatteringScale", "mieAnisotropy", "mieScatteringScalarHeight", "ozoneAbsorptionScale", "ozoneLevelCenterHeight", "ozoneLevelWidth", "aerialPerspectiveDistance", "aerialPerspectiveScale"}).Result();
        }

        void LoadJSON(const nlohmann::json &json)
        {
            if (json.is_null())
                return;

            seaLevel = json.value("seaLevel", seaLevel);
            planetRadius = json.value("planetRadius", planetRadius);
            atmosphereHeight = json.value("atmosphereHeight", atmosphereHeight);
            sunDiskAngle = json.value("sunDiskAngle", sunDiskAngle);
            sunLightIntensityFactor = json.value("sunLightIntensityFactor", sunLightIntensityFactor);
            rayleighScatteringScale = json.value("rayleighScatteringScale", rayleighScatteringScale);
            rayleighScatteringScalarHeight = json.value("rayleighScatteringScalarHeight", rayleighScatteringScalarHeight);
            mieScatteringScale = json.value("mieScatteringScale", mieScatteringScale);
            mieAnisotropy = json.value("mieAnisotropy", mieAnisotropy);
            mieScatteringScalarHeight = json.value("mieScatteringScalarHeight", mieScatteringScalarHeight);
            ozoneAbsorptionScale = json.value("ozoneAbsorptionScale", ozoneAbsorptionScale);
            ozoneLevelCenterHeight = json.value("ozoneLevelCenterHeight", ozoneLevelCenterHeight);
            ozoneLevelWidth = json.value("ozoneLevelWidth", ozoneLevelWidth);
            aerialPerspectiveDistance = json.value("aerialPerspectiveDistance", aerialPerspectiveDistance);
            aerialPerspectiveScale = json.value("aerialPerspectiveScale", aerialPerspectiveScale);
        }
    };

    struct SkyLUTConfig
    {
        float sunDirectionThresholdDegrees = 0.1f;
        float sunColorThreshold = 0.01f;
        float sunIntensityThreshold = 0.01f;
        float cameraHeightThreshold = 1.0f;
        std::array<float, 3> groundColor{0.18f, 0.20f, 0.22f};
        float groundBrightness = 1.0f;
        float groundHorizonWidthDegrees = 25.0f;
        float groundCurve = 1.0f;

        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            if (json.is_null())
                return {};
            return Object(json).Numbers({"sunDirectionThresholdDegrees", "sunColorThreshold", "sunIntensityThreshold", "cameraHeightThreshold", "groundBrightness", "groundHorizonWidthDegrees", "groundCurve"}).Vectors({"groundColor"}, 3).Result();
        }

        void LoadJSON(const nlohmann::json &json)
        {
            if (json.is_null())
                return;

            sunDirectionThresholdDegrees = json.value("sunDirectionThresholdDegrees", sunDirectionThresholdDegrees);
            sunColorThreshold = json.value("sunColorThreshold", sunColorThreshold);
            sunIntensityThreshold = json.value("sunIntensityThreshold", sunIntensityThreshold);
            cameraHeightThreshold = json.value("cameraHeightThreshold", cameraHeightThreshold);
            if (auto color = json.find("groundColor"); color != json.end())
            {
                groundColor = color->get<std::array<float, 3>>();
            }
            groundBrightness = json.value("groundBrightness", groundBrightness);
            groundHorizonWidthDegrees = json.value("groundHorizonWidthDegrees", groundHorizonWidthDegrees);
            groundCurve = json.value("groundCurve", groundCurve);
        }
    };

    struct BloomConfig
    {
        float threshold = 1.0f;
        float intensity = 0.35f;
        float radius = 1.0f;
        float knee = 0.25f;

        BloomConfig() = default;
        explicit BloomConfig(const nlohmann::json &json)
        {
            LoadJSON(json);
        }

        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            if (json.is_null())
                return {};
            return Object(json).Numbers({"threshold", "intensity", "radius", "knee"}).Result();
        }

        void LoadJSON(const nlohmann::json &json)
        {
            if (json.is_null())
                return;

            threshold = json.value("threshold", threshold);
            intensity = json.value("intensity", intensity);
            radius = json.value("radius", radius);
            knee = json.value("knee", knee);
        }
    };

    struct ToneMappingConfig
    {
        float exposure = 1.0f;
        uint32_t toneMappingMode = 0;

        ToneMappingConfig() = default;
        explicit ToneMappingConfig(const nlohmann::json &json)
        {
            LoadJSON(json);
        }

        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            if (json.is_null())
                return {};
            return Object(json).Numbers({"exposure"}).Unsigneds({"toneMappingMode"}, UINT32_MAX).Result();
        }

        void LoadJSON(const nlohmann::json &json)
        {
            if (json.is_null())
                return;

            exposure = json.value("exposure", exposure);
            toneMappingMode = json.value("toneMappingMode", toneMappingMode);
        }
    };

    struct SSAOConfig
    {
        float radius = 0.1f;
        float bias = 0.025f;
        float intensity = 0.6f;
        float power = 1.0f;

        SSAOConfig() = default;
        explicit SSAOConfig(const nlohmann::json &json)
        {
            LoadJSON(json);
        }

        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            if (json.is_null())
                return {};
            return Object(json).Numbers({"radius", "bias", "intensity", "power"}).Result();
        }

        void LoadJSON(const nlohmann::json &json)
        {
            if (json.is_null())
                return;

            radius = json.value("radius", radius);
            bias = json.value("bias", bias);
            intensity = json.value("intensity", intensity);
            power = json.value("power", power);
        }
    };

    struct DirectionalShadowConfig
    {
        uint32_t mapSize = 4096;
        uint32_t cascadeCnt = MAX_DIRECTIONAL_SHADOW_CASCADE_CNT;
        float maxDistance = 180.0f;
        float splitLambda = 0.75f;
        float depthMargin = 60.0f;

        DirectionalShadowConfig() = default;
        explicit DirectionalShadowConfig(const nlohmann::json &json)
        {
            LoadJSON(json);
        }

        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            if (json.is_null())
                return {};
            auto valid = Object(json).Numbers({"maxDistance", "splitLambda", "depthMargin"}).Unsigneds({"mapSize", "cascadeCnt"}, UINT32_MAX).Result();
            if (!valid)
                return valid;
            const auto count = json.value("cascadeCnt", MAX_DIRECTIONAL_SHADOW_CASCADE_CNT);
            if (count < 1 || count > MAX_DIRECTIONAL_SHADOW_CASCADE_CNT)
                return std::unexpected("cascadeCnt must be between 1 and 4");
            return {};
        }

        void LoadJSON(const nlohmann::json &json)
        {
            if (json.is_null())
                return;

            mapSize = json.value("mapSize", mapSize);
            cascadeCnt = json.value("cascadeCnt", cascadeCnt);
            maxDistance = json.value("maxDistance", maxDistance);
            splitLambda = json.value("splitLambda", splitLambda);
            depthMargin = json.value("depthMargin", depthMargin);
        }
    };

    struct SpotShadowConfig
    {
        uint32_t mapSize = 1024;
    };

    struct RenderConfig
    {
        BloomConfig bloom;
        ToneMappingConfig toneMapping;
        SSAOConfig ssao;
        DirectionalShadowConfig directionalShadow;
        AtmosphereParameter atmosphere;
        SkyLUTConfig skyLUT;
        nlohmann::json sourceJSON = nlohmann::json::object();

        RenderConfig() = default;
        explicit RenderConfig(const nlohmann::json &json)
        {
            LoadJSON(json);
        }

        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            if (json.is_null())
                return {};
            if (!json.is_object())
                return std::unexpected("expected an object");
            if (auto field = json.find("bloom"); field != json.end())
                if (auto valid = BloomConfig::ValidateJSON(*field); !valid)
                    return std::unexpected("bloom: " + valid.error());
            if (auto field = json.find("toneMapping"); field != json.end())
                if (auto valid = ToneMappingConfig::ValidateJSON(*field); !valid)
                    return std::unexpected("toneMapping: " + valid.error());
            if (auto field = json.find("ssao"); field != json.end())
                if (auto valid = SSAOConfig::ValidateJSON(*field); !valid)
                    return std::unexpected("ssao: " + valid.error());
            if (auto field = json.find("directionalShadow"); field != json.end())
                if (auto valid = DirectionalShadowConfig::ValidateJSON(*field); !valid)
                    return std::unexpected("directionalShadow: " + valid.error());
            if (auto field = json.find("atmosphere"); field != json.end())
                if (auto valid = AtmosphereParameter::ValidateJSON(*field); !valid)
                    return std::unexpected("atmosphere: " + valid.error());
            if (auto field = json.find("skyLUT"); field != json.end())
                if (auto valid = SkyLUTConfig::ValidateJSON(*field); !valid)
                    return std::unexpected("skyLUT: " + valid.error());
            return {};
        }

        void LoadJSON(const nlohmann::json &json)
        {
            sourceJSON = json.is_null() ? nlohmann::json::object() : json;
            if (json.is_null())
                return;

            bloom.LoadJSON(json.value("bloom", nlohmann::json::object()));
            toneMapping.LoadJSON(json.value("toneMapping", nlohmann::json::object()));
            ssao.LoadJSON(json.value("ssao", nlohmann::json::object()));
            directionalShadow.LoadJSON(json.value("directionalShadow", nlohmann::json::object()));
            atmosphere.LoadJSON(json.value("atmosphere", nlohmann::json::object()));
            skyLUT.LoadJSON(json.value("skyLUT", nlohmann::json::object()));
        }
    };
}

#endif
