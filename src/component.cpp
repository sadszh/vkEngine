#include <component/camera.hpp>
#include <component/renderable_object.hpp>
#include <component/skeleton_animator.hpp>
#include <component/rigidbody.hpp>
#include <component/sensor.hpp>
#include <component/character_controller.hpp>
#include <component/light.hpp>
#include <component/canvas.hpp>
#include <component/script.hpp>
#include <scene.hpp>
#include <reflect/value_view.hpp>

namespace vke_component
{
    vke_common::Result<ScriptStateData> ScriptStateData::FromJSON(const nlohmann::json &json)
    {
        using namespace vke_common;
        if (auto valid = ValidateJSON(json); !valid)
            return std::unexpected(valid.error());
        const auto className = json["className"].get<std::string>();
        auto *manager = ScriptManager::GetInstance();
        if (!manager)
            return std::unexpected("ScriptManager is not initialized");
        auto type = manager->FindTypeInfo(className);
        if (!type)
            return std::unexpected("missing script TypeInfo: " + className);
        auto encoded = type->EncodeBinaryFromJson(json["data"]);
        if (!encoded)
            return std::unexpected(className + " at " + encoded.error().path + ": " + std::string(ToString(encoded.error().code)));
        if ((*encoded)->size() > INT32_MAX)
            return std::unexpected("script data exceeds interop size limit");
        return ScriptStateData{className, std::move(**encoded)};
    }

    vke_common::Result<nlohmann::json> ScriptStateData::ToJSON() const
    {
        using namespace vke_common;
        auto *manager = ScriptManager::GetInstance();
        if (!manager)
            return std::unexpected("ScriptManager is not initialized");
        auto type = manager->FindTypeInfo(className);
        if (!type)
            return std::unexpected("missing script TypeInfo: " + className);
        auto value = ValueView::Parse(type, data);
        if (!value)
            return std::unexpected(className + ": " + std::string(ToString(value.error().code)));
        return nlohmann::json{{"type", "script"}, {"className", className}, {"data", value->ToJSON()}};
    }
}

namespace vke_common
{
    Result<void> SceneData::loadComponent(const entt::entity entity,
                                               const nlohmann::json &component)
    {
        const std::string type = component["type"];
        auto load = [&]<typename T>() -> Result<void>
        {
            if (auto valid = T::ValidateJSON(component); !valid)
                return valid;
            registry.emplace<T>(entity, component);
            return {};
        };
        if (type == "camera")
            return load.operator()<vke_component::CameraData>();
        if (type == "renderableObject")
            return load.operator()<vke_component::RenderableObjectData>();
        if (type == "canvas")
            return load.operator()<vke_component::CanvasData>();
        if (type == "animator")
            return load.operator()<vke_component::SkeletonAnimatorData>();
        if (type == "rigidbody")
            return load.operator()<vke_component::RigidBodyData>();
        if (type == "sensor")
            return load.operator()<vke_component::SensorData>();
        if (type == "characterController")
            return load.operator()<vke_component::CharacterControllerData>();
        if (type == "audioSource")
            return load.operator()<vke_component::AudioSourceData>();
        if (type == "audioListener")
            return load.operator()<vke_component::AudioListenerData>();
        if (type == "directionalLight")
            return load.operator()<vke_component::DirectionalLightData>();
        if (type == "pointLight")
            return load.operator()<vke_component::PointLightData>();
        if (type == "spotLight")
            return load.operator()<vke_component::SpotLightData>();
        if (type == "script")
        {
            auto script = vke_component::ScriptStateData::FromJSON(component);
            if (!script) return std::unexpected(script.error());
            if (!registry.all_of<ScriptDataList>(entity))
                registry.emplace<ScriptDataList>(entity);
            registry.get<ScriptDataList>(entity).push_back(std::move(*script));
            return {};
        }
        return std::unexpected("unknown component type: " + type);
    }

    Result<void> SceneData::componentToJSON(entt::entity entity, nlohmann::json &components) const
    {
        if (registry.all_of<vke_component::CameraData>(entity))
            components.push_back(registry.get<vke_component::CameraData>(entity).ToJSON());

        if (registry.all_of<vke_component::RenderableObjectData>(entity))
            components.push_back(registry.get<vke_component::RenderableObjectData>(entity).ToJSON());

        if (registry.all_of<vke_component::CanvasData>(entity))
            components.push_back(registry.get<vke_component::CanvasData>(entity).ToJSON());

        if (registry.all_of<vke_component::SkeletonAnimatorData>(entity))
            components.push_back(registry.get<vke_component::SkeletonAnimatorData>(entity).ToJSON());

        if (registry.all_of<vke_component::RigidBodyData>(entity))
            components.push_back(registry.get<vke_component::RigidBodyData>(entity).ToJSON());

        if (registry.all_of<vke_component::SensorData>(entity))
            components.push_back(registry.get<vke_component::SensorData>(entity).ToJSON());

        if (registry.all_of<vke_component::CharacterControllerData>(entity))
            components.push_back(registry.get<vke_component::CharacterControllerData>(entity).ToJSON());

        if (registry.all_of<vke_component::AudioSourceData>(entity))
            components.push_back(registry.get<vke_component::AudioSourceData>(entity).ToJSON());

        if (registry.all_of<vke_component::AudioListenerData>(entity))
            components.push_back(registry.get<vke_component::AudioListenerData>(entity).ToJSON());

        if (registry.all_of<vke_component::DirectionalLightData>(entity))
            components.push_back(registry.get<vke_component::DirectionalLightData>(entity).ToJSON());

        if (registry.all_of<vke_component::PointLightData>(entity))
            components.push_back(registry.get<vke_component::PointLightData>(entity).ToJSON());

        if (registry.all_of<vke_component::SpotLightData>(entity))
            components.push_back(registry.get<vke_component::SpotLightData>(entity).ToJSON());

        if (registry.all_of<ScriptDataList>(entity))
            for (const vke_component::ScriptStateData &script :
                 registry.get<ScriptDataList>(entity))
            {
                auto json = script.ToJSON();
                if (!json) return std::unexpected(json.error());
                components.push_back(std::move(*json));
            }
        return {};
    }

    Result<void> SceneManager::validateComponentMutation(entt::entity entity) const
    {
        if (shuttingDown)
            return std::unexpected("SceneManager is shutting down");
        if (!registry.valid(entity))
            return std::unexpected("invalid entity");
        if (IsPendingDestroy(entity))
            return std::unexpected("entity is pending destruction");
        if (!registry.all_of<GameObject, Transform>(entity))
            return std::unexpected("entity requires GameObject and Transform");
        return {};
    }

    template <typename Component, typename Data>
    Result<void> SceneManager::addComponent(entt::entity entity, Data data)
    {
        if (auto valid = validateComponentMutation(entity); !valid)
            return valid;
        if (registry.all_of<Component>(entity))
            return std::unexpected("component already exists");
        if constexpr (requires { data.Validate(); })
            if (auto valid = data.Validate(); !valid) return valid;
        if constexpr (requires { data.ValidateAssets(); })
        {
            if (auto valid = data.ValidateAssets(); !valid)
                return valid;
            if (auto loaded = data.LoadAssets(); !loaded)
                return loaded;
        }
        addPreparedComponent<Component>(entity, data);
        if constexpr (requires(Component &c) { c.Start(); })
            if (IsRunning())
                registry.get<Component>(entity).Start();
        return {};
    }

    Result<void> SceneManager::AddComponent(entt::entity entity, const vke_component::CameraData &data)
    {
        return addComponent<vke_component::Camera>(entity, data);
    }

    Result<void> SceneManager::AddComponent(entt::entity entity, const vke_component::RenderableObjectData &data)
    {
        return addComponent<vke_component::RenderableObject>(entity, data);
    }

    Result<void> SceneManager::AddComponent(entt::entity entity, const vke_component::SkeletonAnimatorData &data)
    {
        return addComponent<vke_component::SkeletonAnimator>(entity, data);
    }

    Result<void> SceneManager::AddComponent(entt::entity entity, const vke_component::RigidBodyData &data)
    {
        return addComponent<vke_component::RigidBody>(entity, data);
    }

    Result<void> SceneManager::AddComponent(entt::entity entity, const vke_component::SensorData &data)
    {
        return addComponent<vke_component::Sensor>(entity, data);
    }

    Result<void> SceneManager::AddComponent(entt::entity entity, const vke_component::CharacterControllerData &data)
    {
        return addComponent<vke_component::CharacterController>(entity, data);
    }

    Result<void> SceneManager::AddComponent(entt::entity entity, const vke_component::DirectionalLightData &data)
    {
        return addComponent<vke_component::DirectionalLight>(entity, data);
    }

    Result<void> SceneManager::AddComponent(entt::entity entity, const vke_component::PointLightData &data)
    {
        return addComponent<vke_component::PointLight>(entity, data);
    }

    Result<void> SceneManager::AddComponent(entt::entity entity, const vke_component::SpotLightData &data)
    {
        return addComponent<vke_component::SpotLight>(entity, data);
    }

    Result<void> SceneManager::AddComponent(entt::entity entity, const vke_component::CanvasData &data)
    {
        return addComponent<vke_component::Canvas>(entity, data);
    }

    Result<void> SceneManager::AddComponent(entt::entity entity, const vke_component::AudioSourceData &data)
    {
        return addComponent<vke_component::AudioSource>(entity, data);
    }

    Result<void> SceneManager::AddComponent(entt::entity entity, const vke_component::AudioListenerData &data)
    {
        return addComponent<vke_component::AudioListener>(entity, data);
    }

    Result<void> SceneManager::AddComponent(entt::entity entity, ComponentType componentType)
    {
        if (auto valid = validateComponentMutation(entity); !valid)
            return valid;
        if (HasComponent(entity, componentType))
            return std::unexpected("component already exists");
        using namespace vke_component;
        switch (componentType)
        {
        case ComponentType::Camera:
            return AddComponent(entity, CameraData{});
        case ComponentType::RenderableObject:
            return AddComponent(entity, RenderableObjectData::Default());
        case ComponentType::RigidBody:
            return AddComponent(entity, RigidBodyData{});
        case ComponentType::Sensor:
            return AddComponent(entity, SensorData{});
        case ComponentType::CharacterController:
            return AddComponent(entity, CharacterControllerData{});
        case ComponentType::DirectionalLight:
            return AddComponent(entity, DirectionalLightData{});
        case ComponentType::PointLight:
            return AddComponent(entity, PointLightData{});
        case ComponentType::SpotLight:
            return AddComponent(entity, SpotLightData{});
        case ComponentType::Canvas:
            return AddComponent(entity, CanvasData{});
        case ComponentType::AudioSource:
            return AddComponent(entity, AudioSourceData{});
        case ComponentType::AudioListener:
            return AddComponent(entity, AudioListenerData{});
        case ComponentType::SkeletonAnimator:
            return std::unexpected("SkeletonAnimator requires explicit material, mesh and skeleton data");
        case ComponentType::Transform:
            return std::unexpected("Transform is created with the entity");
        case ComponentType::Script:
            return std::unexpected("dynamic script addition is not supported");
        default:
            return std::unexpected("unknown component type");
        }
    }

    Result<void> SceneManager::RemoveComponent(entt::entity entity, ComponentType componentType)
    {
        if (auto valid = validateComponentMutation(entity); !valid)
            return valid;
        if (componentType == ComponentType::Transform)
            return std::unexpected("Transform cannot be removed");
        if (componentType == ComponentType::Script)
            return std::unexpected("dynamic script removal is not supported");
        if (!HasComponent(entity, componentType))
            return std::unexpected("component does not exist");
        // Unlike registration, removal can release buffers still referenced by submitted work:
        // animators own skinning buffers, and a renderable may own the last reference to its mesh.
        // Camera/light/Canvas removal only changes CPU state or returns slots to per-frame pools.
        if (componentType == ComponentType::RenderableObject || componentType == ComponentType::SkeletonAnimator)
            vke_render::Renderer::WaitIdle();
        removeNativeComponent(entity, componentType);
        return {};
    }

    void SceneManager::removeNativeComponent(entt::entity entity, ComponentType componentType)
    {
        // Callback cleanup must run while the native body is still available for its ID lookup.
        if ((componentType == ComponentType::RigidBody || componentType == ComponentType::Sensor) &&
            ScriptManager::GetInstance())
            ScriptManager::UnregisterComponentCallbacks(entity, componentType);

        auto remove = [&]<typename Component>()
        {
            if (auto *component = registry.try_get<Component>(entity))
            {
                component->Unload();
                registry.remove<Component>(entity);
            }
        };
        auto removeLight = [&]<typename Component>()
        {
            if (!registry.all_of<Component>(entity))
                return;
            Component::Unload(entity);
            registry.remove<Component>(entity);
        };
        switch (componentType)
        {
        case ComponentType::Camera:
            remove.operator()<vke_component::Camera>();
            break;
        case ComponentType::RenderableObject:
            remove.operator()<vke_component::RenderableObject>();
            break;
        case ComponentType::SkeletonAnimator:
            remove.operator()<vke_component::SkeletonAnimator>();
            break;
        case ComponentType::RigidBody:
            remove.operator()<vke_component::RigidBody>();
            break;
        case ComponentType::Sensor:
            remove.operator()<vke_component::Sensor>();
            break;
        case ComponentType::CharacterController:
            remove.operator()<vke_component::CharacterController>();
            break;
        case ComponentType::DirectionalLight:
            removeLight.operator()<vke_component::DirectionalLight>();
            break;
        case ComponentType::PointLight:
            removeLight.operator()<vke_component::PointLight>();
            break;
        case ComponentType::SpotLight:
            removeLight.operator()<vke_component::SpotLight>();
            break;
        case ComponentType::Canvas:
            remove.operator()<vke_component::Canvas>();
            break;
        case ComponentType::AudioSource:
            remove.operator()<vke_component::AudioSource>();
            break;
        case ComponentType::AudioListener:
            remove.operator()<vke_component::AudioListener>();
            break;
        default:
            break;
        }
    }

    bool SceneManager::HasComponent(entt::entity entity, ComponentType componentType) const
    {
        if (!registry.valid(entity))
            return false;

        switch (componentType)
        {
        case ComponentType::Transform:
            return registry.all_of<vke_common::Transform>(entity);
        case ComponentType::Camera:
            return registry.all_of<vke_component::Camera>(entity);
        case ComponentType::RenderableObject:
            return registry.all_of<vke_component::RenderableObject>(entity);
        case ComponentType::SkeletonAnimator:
            return registry.all_of<vke_component::SkeletonAnimator>(entity);
        case ComponentType::RigidBody:
            return registry.all_of<vke_component::RigidBody>(entity);
        case ComponentType::Sensor:
            return registry.all_of<vke_component::Sensor>(entity);
        case ComponentType::CharacterController:
            return registry.all_of<vke_component::CharacterController>(entity);
        case ComponentType::DirectionalLight:
            return registry.all_of<vke_component::DirectionalLight>(entity);
        case ComponentType::PointLight:
            return registry.all_of<vke_component::PointLight>(entity);
        case ComponentType::SpotLight:
            return registry.all_of<vke_component::SpotLight>(entity);
        case ComponentType::Script:
            return ScriptManager::HasScripts(entity);
        case ComponentType::Canvas:
            return registry.all_of<vke_component::Canvas>(entity);
        case ComponentType::AudioSource:
            return registry.all_of<vke_component::AudioSource>(entity);
        case ComponentType::AudioListener:
            return registry.all_of<vke_component::AudioListener>(entity);
        default:
            return false;
        }
    }
}
