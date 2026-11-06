#include <physics/physics_config.hpp>
#include <common.hpp>
#include <algorithm>
#include <cmath>

namespace vke_physics
{
    static uint32_t ReadLayerID(const nlohmann::json &json)
    {
        if (json.is_number_unsigned() || json.is_number_integer())
            return json.get<uint32_t>();

        return json.value("id", 0u);
    }

    PhysicsConfig::PhysicsConfig()
    {
        resetDefault();
    }

    PhysicsConfig::PhysicsConfig(const nlohmann::json &json)
    {
        LoadJSON(json);
    }

    void PhysicsConfig::resetDefault()
    {
        maxBodies = 65536 * 4;
        numBodyMutexes = 0;
        maxBodyPairs = 65536 * 4;
        maxContactConstraints = 10240;
        collisionSteps = 1;
        tempAllocatorSize = 10 * 1024 * 1024;
        stepTime = 1.0f / 60.0f;
        gravity = JPH::Vec3(0, -9.8f, 0);

        objectLayerCount = 2;
        broadPhaseLayerCount = 2;
        objectLayerNames.fill("");
        broadPhaseLayerNames.fill("");

        for (uint32_t i = 0; i < MaxObjectLayers; ++i)
        {
            objectToBroadPhase[i] = JPH::BroadPhaseLayer(0);
            for (uint32_t j = 0; j < MaxObjectLayers; ++j)
                objectLayerPairs[i][j] = false;
            for (uint32_t j = 0; j < MaxBroadPhaseLayers; ++j)
                objectVsBroadPhaseLayerPairs[i][j] = false;
        }

        objectLayerNames[DefaultObjectLayers::NON_MOVING] = "NON_MOVING";
        objectLayerNames[DefaultObjectLayers::MOVING] = "MOVING";
        broadPhaseLayerNames[DefaultBroadPhaseLayers::NON_MOVING.GetValue()] = "NON_MOVING";
        broadPhaseLayerNames[DefaultBroadPhaseLayers::MOVING.GetValue()] = "MOVING";
        objectToBroadPhase[DefaultObjectLayers::NON_MOVING] = DefaultBroadPhaseLayers::NON_MOVING;
        objectToBroadPhase[DefaultObjectLayers::MOVING] = DefaultBroadPhaseLayers::MOVING;
        SetObjectLayerCollision(DefaultObjectLayers::NON_MOVING, DefaultObjectLayers::MOVING, true);
        SetObjectLayerCollision(DefaultObjectLayers::MOVING, DefaultObjectLayers::MOVING, true);
        rebuildObjectVsBroadPhaseLayerPairs();
    }

    JPH::ObjectLayer PhysicsConfig::AddObjectLayer(const std::string &name, JPH::BroadPhaseLayer broadPhaseLayer)
    {
        VKE_FATAL_IF(objectLayerCount >= MaxObjectLayers, "Jolt object layer count cannot exceed {}", MaxObjectLayers)
        VKE_FATAL_IF(broadPhaseLayer.GetValue() >= broadPhaseLayerCount, "Invalid broad phase layer {}", broadPhaseLayer.GetValue())
        const JPH::ObjectLayer layer = static_cast<JPH::ObjectLayer>(objectLayerCount++);
        objectLayerNames[layer] = name;
        objectToBroadPhase[layer] = broadPhaseLayer;
        rebuildObjectVsBroadPhaseLayerPairs();
        return layer;
    }

    JPH::BroadPhaseLayer PhysicsConfig::AddBroadPhaseLayer(const std::string &name)
    {
        VKE_FATAL_IF(broadPhaseLayerCount >= MaxBroadPhaseLayers, "Jolt broad phase layer count cannot exceed {}", MaxBroadPhaseLayers)
        const JPH::BroadPhaseLayer layer(static_cast<JPH::BroadPhaseLayer::Type>(broadPhaseLayerCount++));
        broadPhaseLayerNames[layer.GetValue()] = name;
        return layer;
    }

    void PhysicsConfig::SetObjectLayerBroadPhaseLayer(JPH::ObjectLayer objectLayer, JPH::BroadPhaseLayer broadPhaseLayer)
    {
        VKE_FATAL_IF(objectLayer >= objectLayerCount, "Invalid object layer {}", objectLayer)
        VKE_FATAL_IF(broadPhaseLayer.GetValue() >= broadPhaseLayerCount, "Invalid broad phase layer {}", broadPhaseLayer.GetValue())
        objectToBroadPhase[objectLayer] = broadPhaseLayer;
        rebuildObjectVsBroadPhaseLayerPairs();
    }

    void PhysicsConfig::SetObjectLayerCollision(JPH::ObjectLayer layer1, JPH::ObjectLayer layer2, bool shouldCollide)
    {
        VKE_FATAL_IF(layer1 >= objectLayerCount || layer2 >= objectLayerCount, "Invalid object layer pair {}, {}", layer1, layer2)
        objectLayerPairs[layer1][layer2] = shouldCollide;
        objectLayerPairs[layer2][layer1] = shouldCollide;
        rebuildObjectVsBroadPhaseLayerPairs();
    }

    void PhysicsConfig::SetObjectVsBroadPhaseLayerCollision(JPH::ObjectLayer objectLayer, JPH::BroadPhaseLayer broadPhaseLayer, bool shouldCollide)
    {
        VKE_FATAL_IF(objectLayer >= objectLayerCount, "Invalid object layer {}", objectLayer)
        VKE_FATAL_IF(broadPhaseLayer.GetValue() >= broadPhaseLayerCount, "Invalid broad phase layer {}", broadPhaseLayer.GetValue())
        objectVsBroadPhaseLayerPairs[objectLayer][broadPhaseLayer.GetValue()] = shouldCollide;
    }

    void PhysicsConfig::rebuildObjectVsBroadPhaseLayerPairs()
    {
        for (uint32_t objectLayer = 0; objectLayer < MaxObjectLayers; ++objectLayer)
            for (uint32_t broadPhaseLayer = 0; broadPhaseLayer < MaxBroadPhaseLayers; ++broadPhaseLayer)
                objectVsBroadPhaseLayerPairs[objectLayer][broadPhaseLayer] = false;

        for (uint32_t objectLayer = 0; objectLayer < objectLayerCount; ++objectLayer)
        {
            for (uint32_t otherLayer = 0; otherLayer < objectLayerCount; ++otherLayer)
            {
                if (!objectLayerPairs[objectLayer][otherLayer])
                    continue;

                const uint32_t broadPhaseLayer = objectToBroadPhase[otherLayer].GetValue();
                if (broadPhaseLayer < broadPhaseLayerCount)
                    objectVsBroadPhaseLayerPairs[objectLayer][broadPhaseLayer] = true;
            }
        }
    }

    vke_common::SceneResult<void> PhysicsConfig::ValidateJSON(const nlohmann::json &json)
    {
        using namespace vke_common::json_validation;
        if (json.is_null()) return {};
        auto valid = Object(json).Unsigneds({"maxBodies", "numBodyMutexes", "maxBodyPairs", "maxContactConstraints", "tempAllocatorSize"}, UINT32_MAX)
            .Unsigneds({"collisionSteps"}, INT32_MAX).Numbers({"stepTime"}).Result();
        if (!valid) return valid;
        if (json.contains("stepTime") && json["stepTime"].get<float>() <= 0)
            return std::unexpected("stepTime must be positive");
        if (auto gravity = json.find("gravity"); gravity != json.end())
        {
            if (gravity->is_array())
            {
                if (gravity->size() < 3 || !Number((*gravity)[0]) || !Number((*gravity)[1]) || !Number((*gravity)[2]))
                    return std::unexpected("gravity must contain three finite numbers");
            }
            else if (auto result = Object(*gravity).Numbers({"x", "y", "z"}).Result(); !result)
                return std::unexpected("gravity: " + result.error());
        }

        auto validateLayers = [&](const char *field, uint32_t maximum) -> vke_common::SceneResult<uint32_t>
        {
            auto layers = json.find(field);
            if (layers == json.end()) return 2;
            if (!layers->is_array()) return std::unexpected(std::string(field) + " must be an array");
            uint32_t count = 0, nextID = 0;
            for (const auto &layer : *layers)
            {
                uint32_t id;
                if (layer.is_string()) id = nextID;
                else if (layer.is_number_integer())
                {
                    if (!Unsigned(layer, maximum - 1)) return std::unexpected(std::string(field) + ": invalid layer id");
                    id = layer.get<uint32_t>();
                }
                else
                {
                    auto result = Object(layer).Unsigneds({"id"}, maximum - 1).Strings({"name"}).Result();
                    if (!result) return std::unexpected(std::string(field) + ": " + result.error());
                    id = layer.value("id", 0u);
                }
                if (id >= maximum) return std::unexpected(std::string(field) + ": too many layers");
                nextID = id + 1;
                count = std::max(count, nextID);
            }
            return count;
        };
        auto objectCount = validateLayers("objectLayers", MaxObjectLayers);
        if (!objectCount) return std::unexpected(objectCount.error());
        auto broadCount = validateLayers("broadPhaseLayers", MaxBroadPhaseLayers);
        if (!broadCount) return std::unexpected(broadCount.error());

        for (auto field : {"objectToBroadPhase", "objectLayerPairs", "objectVsBroadPhaseLayerPairs"})
        {
            auto entries = json.find(field);
            if (entries == json.end()) continue;
            if (!entries->is_array()) return std::unexpected(std::string(field) + " must be an array");
            const bool objectPair = std::string_view(field) == "objectLayerPairs";
            const bool mapping = std::string_view(field) == "objectToBroadPhase";
            const uint32_t secondCount = objectPair ? *objectCount : *broadCount;
            for (size_t i = 0; i < entries->size(); ++i)
            {
                const auto &entry = (*entries)[i];
                const auto context = std::string(field) + "[" + std::to_string(i) + "]: ";
                uint32_t first, second;
                if (entry.is_array())
                {
                    if (entry.size() < 2 || !Unsigned(entry[0], UINT32_MAX) || !Unsigned(entry[1], UINT32_MAX) ||
                        (!mapping && entry.size() >= 3 && !entry[2].is_boolean()))
                        return std::unexpected(context + "invalid layer pair");
                    first = entry[0].get<uint32_t>();
                    second = entry[1].get<uint32_t>();
                }
                else
                {
                    auto result = objectPair
                        ? Object(entry).Unsigneds({"layer1", "layer2", "a", "b"}, UINT32_MAX).Booleans({"collide"}).Result()
                        : Object(entry).Unsigneds({"objectLayer", "object", "broadPhaseLayer", "broadPhase"}, UINT32_MAX).Booleans({"collide"}).Result();
                    if (!result) return std::unexpected(context + result.error());
                    first = objectPair ? entry.value("layer1", entry.value("a", 0u)) : entry.value("objectLayer", entry.value("object", 0u));
                    second = objectPair ? entry.value("layer2", entry.value("b", 0u)) : entry.value("broadPhaseLayer", entry.value("broadPhase", 0u));
                }
                if (first >= *objectCount || second >= secondCount)
                    return std::unexpected(context + "layer id is outside the configured layer count");
            }
        }
        if (json.contains("broadPhaseLayers") && !json.contains("objectToBroadPhase"))
            for (uint32_t i = 0; i < *objectCount; ++i)
                if ((i == 1 ? 1u : 0u) >= *broadCount)
                    return std::unexpected("broadPhaseLayers requires an explicit objectToBroadPhase mapping");
        return {};
    }

    void PhysicsConfig::LoadJSON(const nlohmann::json &json)
    {
        resetDefault();

        if (json.is_null())
            return;

        maxBodies = json.value("maxBodies", maxBodies);
        numBodyMutexes = json.value("numBodyMutexes", numBodyMutexes);
        maxBodyPairs = json.value("maxBodyPairs", maxBodyPairs);
        maxContactConstraints = json.value("maxContactConstraints", maxContactConstraints);
        collisionSteps = json.value("collisionSteps", collisionSteps);
        tempAllocatorSize = json.value("tempAllocatorSize", tempAllocatorSize);
        stepTime = json.value("stepTime", stepTime);

        if (json.contains("gravity"))
        {
            const auto &gravityJson = json["gravity"];
            if (gravityJson.is_array())
            {
                gravity = JPH::Vec3(gravityJson.at(0).get<float>(), gravityJson.at(1).get<float>(), gravityJson.at(2).get<float>());
            }
            else
            {
                gravity = JPH::Vec3(gravityJson.value("x", gravity.GetX()), gravityJson.value("y", gravity.GetY()), gravityJson.value("z", gravity.GetZ()));
            }
        }

        if (json.contains("objectLayers"))
        {
            objectLayerCount = 0;
            uint32_t nextID = 0;
            for (const auto &layerJson : json["objectLayers"])
            {
                const uint32_t id = layerJson.is_string() ? nextID : ReadLayerID(layerJson);
                objectLayerCount = std::max(objectLayerCount, id + 1);
                objectLayerNames[id] = layerJson.is_string() ? layerJson.get<std::string>() : layerJson.is_object() ? layerJson.value("name", std::to_string(id)) : std::to_string(id);
                nextID = id + 1;
            }
        }

        if (json.contains("broadPhaseLayers"))
        {
            broadPhaseLayerCount = 0;
            uint32_t nextID = 0;
            for (const auto &layerJson : json["broadPhaseLayers"])
            {
                const uint32_t id = layerJson.is_string() ? nextID : ReadLayerID(layerJson);
                broadPhaseLayerCount = std::max(broadPhaseLayerCount, id + 1);
                broadPhaseLayerNames[id] = layerJson.is_string() ? layerJson.get<std::string>() : layerJson.is_object() ? layerJson.value("name", std::to_string(id)) : std::to_string(id);
                nextID = id + 1;
            }
        }

        if (json.contains("objectToBroadPhase"))
        {
            objectToBroadPhase.fill(JPH::BroadPhaseLayer(0));
            for (const auto &mapping : json["objectToBroadPhase"])
            {
                uint32_t objectLayer = 0;
                uint32_t broadPhaseLayer = 0;
                if (mapping.is_array())
                {
                    objectLayer = mapping.at(0).get<uint32_t>();
                    broadPhaseLayer = mapping.at(1).get<uint32_t>();
                }
                else
                {
                    objectLayer = mapping.value("objectLayer", mapping.value("object", 0u));
                    broadPhaseLayer = mapping.value("broadPhaseLayer", mapping.value("broadPhase", 0u));
                }

                objectToBroadPhase[objectLayer] = JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(broadPhaseLayer));
            }
        }

        if (json.contains("objectLayerPairs"))
        {
            for (auto &pairs : objectLayerPairs)
                pairs.fill(false);
            for (const auto &pairJson : json["objectLayerPairs"])
            {
                uint32_t layer1 = 0;
                uint32_t layer2 = 0;
                bool shouldCollide = true;
                if (pairJson.is_array())
                {
                    layer1 = pairJson.at(0).get<uint32_t>();
                    layer2 = pairJson.at(1).get<uint32_t>();
                    shouldCollide = pairJson.size() < 3 || pairJson.at(2).get<bool>();
                }
                else
                {
                    layer1 = pairJson.value("layer1", pairJson.value("a", 0u));
                    layer2 = pairJson.value("layer2", pairJson.value("b", 0u));
                    shouldCollide = pairJson.value("collide", true);
                }
                SetObjectLayerCollision(static_cast<JPH::ObjectLayer>(layer1), static_cast<JPH::ObjectLayer>(layer2), shouldCollide);
            }
        }

        rebuildObjectVsBroadPhaseLayerPairs();

        if (json.contains("objectVsBroadPhaseLayerPairs"))
        {
            for (const auto &pairJson : json["objectVsBroadPhaseLayerPairs"])
            {
                uint32_t objectLayer = 0;
                uint32_t broadPhaseLayer = 0;
                bool shouldCollide = true;
                if (pairJson.is_array())
                {
                    objectLayer = pairJson.at(0).get<uint32_t>();
                    broadPhaseLayer = pairJson.at(1).get<uint32_t>();
                    shouldCollide = pairJson.size() < 3 || pairJson.at(2).get<bool>();
                }
                else
                {
                    objectLayer = pairJson.value("objectLayer", pairJson.value("object", 0u));
                    broadPhaseLayer = pairJson.value("broadPhaseLayer", pairJson.value("broadPhase", 0u));
                    shouldCollide = pairJson.value("collide", true);
                }
                SetObjectVsBroadPhaseLayerCollision(static_cast<JPH::ObjectLayer>(objectLayer), JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(broadPhaseLayer)), shouldCollide);
            }
        }
    }
}
