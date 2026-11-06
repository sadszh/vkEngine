#ifndef COMPONENT_LIGHT_H
#define COMPONENT_LIGHT_H

#include <json_validation.hpp>
#include <component/transform.hpp>
#include <render/render.hpp>

namespace vke_component
{
    struct DirectionalLightData
    {
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
        // Call before constructing from JSON; asset checks happen in LoadAssets where applicable.
        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            return Object(json).Require({"color", "intensity"}).Vectors({"color"}, 3)
                .Numbers({"intensity"}).Result();
        }

        DirectionalLightData() = default;
        DirectionalLightData(const nlohmann::json &json)
            : color(json["color"][0], json["color"][1], json["color"][2]), intensity(json["intensity"]) {}
        nlohmann::json ToJSON() const
        {
            return {{"type", "directionalLight"}, {"color", {color.r, color.g, color.b}}, {"intensity", intensity}};
        }
    };

    struct PointLightData
    {
        glm::vec3 color{1.0f};
        float radius = 5.0f;
        float intensity = 1.0f;
        // Call before constructing from JSON; asset checks happen in LoadAssets where applicable.
        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            return Object(json).Require({"color", "intensity", "radius"}).Vectors({"color"}, 3)
                .Numbers({"intensity", "radius"}).Result();
        }

        PointLightData() = default;
        PointLightData(const nlohmann::json &json)
            : color(json["color"][0], json["color"][1], json["color"][2]),
              radius(json["radius"]), intensity(json["intensity"]) {}
        nlohmann::json ToJSON() const
        {
            return {{"type", "pointLight"}, {"color", {color.r, color.g, color.b}}, {"radius", radius}, {"intensity", intensity}};
        }
    };

    struct SpotLightData
    {
        glm::vec3 color{1.0f};
        float radius = 5.0f;
        float intensity = 1.0f;
        float innerConeCos = glm::cos(glm::radians(15.0f));
        float outerConeCos = glm::cos(glm::radians(30.0f));
        bool castShadow = false;
        // Call before constructing from JSON; asset checks happen in LoadAssets where applicable.
        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            return Object(json).Require({"color", "intensity", "radius", "innerCone", "outerCone"}).Vectors({"color"}, 3)
                .Numbers({"intensity", "radius", "innerCone", "outerCone"}).Booleans({"castShadow"}).Unsigneds({"shadowSlot"}, UINT32_MAX).Result();
        }

        SpotLightData() = default;
        SpotLightData(const nlohmann::json &json)
            : color(json["color"][0], json["color"][1], json["color"][2]),
              radius(json["radius"]), intensity(json["intensity"]),
              innerConeCos(glm::cos(glm::radians(json["innerCone"].get<float>()))),
              outerConeCos(glm::cos(glm::radians(json["outerCone"].get<float>()))),
              castShadow(json.value("castShadow", json.value("shadowSlot", 0u) != 0u)) {}
        nlohmann::json ToJSON() const
        {
            return {{"type", "spotLight"}, {"color", {color.r, color.g, color.b}}, {"radius", radius}, {"intensity", intensity}, {"innerCone", glm::degrees(glm::acos(glm::clamp(innerConeCos, -1.0f, 1.0f)))}, {"outerCone", glm::degrees(glm::acos(glm::clamp(outerConeCos, -1.0f, 1.0f)))}, {"castShadow", castShadow}};
        }
    };

    // Runtime tags only. LightManager owns all mutable light parameters, keyed by entity.
    struct DirectionalLight
    {
        DirectionalLight(const DirectionalLight &) = delete;
        DirectionalLight &operator=(const DirectionalLight &) = delete;
        DirectionalLight(DirectionalLight &&) noexcept = default;
        DirectionalLight &operator=(DirectionalLight &&) noexcept = default;

        DirectionalLight(entt::entity entity, const vke_common::Transform &transform,
                         const DirectionalLightData &componentData)
        {
            glm::vec3 forward =
                transform.GetGlobalRotation() * glm::vec3(0.0f, 0.0f, -1.0f);
            vke_render::Renderer::GetInstance()->lightManager->AddLight<vke_render::DirectionalLight>(
                entity,
                glm::vec4(glm::normalize(forward), 0.0f),
                glm::vec4(componentData.color, componentData.intensity));
        }

        static void Unload(entt::entity entity)
        {
            vke_render::Renderer::GetInstance()->lightManager->RemoveLight<vke_render::DirectionalLight>(entity);
        }

        static void FillData(entt::entity entity, DirectionalLightData &data)
        {
            const auto &light = vke_render::Renderer::GetInstance()->lightManager
                                    ->GetLightWithoutCheckByEntity<vke_render::DirectionalLight>(entity);
            data.color = glm::vec3(light.colorWithIntensity);
            data.intensity = light.colorWithIntensity.w;
        }
    };

    struct PointLight
    {
        PointLight(const PointLight &) = delete;
        PointLight &operator=(const PointLight &) = delete;
        PointLight(PointLight &&) noexcept = default;
        PointLight &operator=(PointLight &&) noexcept = default;

        PointLight(entt::entity entity, const vke_common::Transform &transform,
                   const PointLightData &componentData)
        {
            vke_render::Renderer::GetInstance()->lightManager->AddLight<vke_render::PointLight>(
                entity,
                glm::vec4(transform.GetGlobalPosition(), componentData.radius),
                glm::vec4(componentData.color, componentData.intensity));
        }

        static void Unload(entt::entity entity)
        {
            vke_render::Renderer::GetInstance()->lightManager->RemoveLight<vke_render::PointLight>(entity);
        }

        static void FillData(entt::entity entity, PointLightData &data)
        {
            const auto &light = vke_render::Renderer::GetInstance()->lightManager
                                    ->GetLightWithoutCheckByEntity<vke_render::PointLight>(entity);
            data.color = glm::vec3(light.colorWithIntensity);
            data.radius = light.positionWithRadius.w;
            data.intensity = light.colorWithIntensity.w;
        }
    };

    struct SpotLight
    {
        SpotLight(const SpotLight &) = delete;
        SpotLight &operator=(const SpotLight &) = delete;
        SpotLight(SpotLight &&) noexcept = default;
        SpotLight &operator=(SpotLight &&) noexcept = default;

        SpotLight(entt::entity entity, const vke_common::Transform &transform,
                  const SpotLightData &componentData)
        {
            glm::vec3 forward =
                transform.GetGlobalRotation() * glm::vec3(0.0f, 0.0f, -1.0f);
            vke_render::Renderer::GetInstance()->lightManager->AddLight<vke_render::SpotLight>(
                entity,
                glm::vec4(transform.GetGlobalPosition(), componentData.radius),
                glm::vec4(glm::normalize(forward), 0.0f),
                glm::vec4(componentData.color, componentData.intensity),
                glm::vec4(componentData.innerConeCos, componentData.outerConeCos,
                          componentData.castShadow ? 1.0f : 0.0f, 0.0f));
        }

        static void Unload(entt::entity entity)
        {
            vke_render::Renderer::GetInstance()->lightManager->RemoveLight<vke_render::SpotLight>(entity);
        }

        static void FillData(entt::entity entity, SpotLightData &data)
        {
            const auto &light = vke_render::Renderer::GetInstance()->lightManager
                                    ->GetLightWithoutCheckByEntity<vke_render::SpotLight>(entity);
            data.color = glm::vec3(light.colorWithIntensity);
            data.radius = light.positionWithRadius.w;
            data.intensity = light.colorWithIntensity.w;
            data.innerConeCos = light.cone.x;
            data.outerConeCos = light.cone.y;
            data.castShadow = light.CastShadow();
        }
    };
}

#endif
