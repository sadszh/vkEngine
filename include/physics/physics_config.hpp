#ifndef PHYSICS_CONFIG_H
#define PHYSICS_CONFIG_H

#include <Jolt/Jolt.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <json_validation.hpp>
#include <array>
#include <cstdint>
#include <string>

namespace vke_physics
{
    static constexpr uint32_t MaxObjectLayers = 32;
    static constexpr uint32_t MaxBroadPhaseLayers = 32;
    static constexpr uint32_t AllPhysicsLayersMask = 0xffffffffu;

    namespace DefaultObjectLayers
    {
        static constexpr JPH::ObjectLayer NON_MOVING = 0;
        static constexpr JPH::ObjectLayer MOVING = 1;
        static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
    };

    namespace DefaultBroadPhaseLayers
    {
        static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
        static constexpr JPH::BroadPhaseLayer MOVING(1);
        static constexpr uint32_t NUM_LAYERS(2);
    };

    struct PhysicsConfig
    {
        uint32_t maxBodies = 65536 * 4;
        uint32_t numBodyMutexes = 0;
        uint32_t maxBodyPairs = 65536 * 4;
        uint32_t maxContactConstraints = 10240;
        uint32_t collisionSteps = 1;
        uint32_t tempAllocatorSize = 10 * 1024 * 1024;
        float stepTime = 1.0f / 60.0f;
        JPH::Vec3 gravity = JPH::Vec3(0, -9.8f, 0);

        uint32_t objectLayerCount = 2;
        uint32_t broadPhaseLayerCount = 2;
        std::array<std::string, MaxObjectLayers> objectLayerNames{};
        std::array<std::string, MaxBroadPhaseLayers> broadPhaseLayerNames{};
        std::array<JPH::BroadPhaseLayer, MaxObjectLayers> objectToBroadPhase{};
        std::array<std::array<bool, MaxObjectLayers>, MaxObjectLayers> objectLayerPairs{};
        std::array<std::array<bool, MaxBroadPhaseLayers>, MaxObjectLayers> objectVsBroadPhaseLayerPairs{};

        PhysicsConfig();
        explicit PhysicsConfig(const nlohmann::json &json);

        JPH::ObjectLayer AddObjectLayer(const std::string &name, JPH::BroadPhaseLayer broadPhaseLayer = JPH::BroadPhaseLayer(0));
        JPH::BroadPhaseLayer AddBroadPhaseLayer(const std::string &name);
        void SetObjectLayerBroadPhaseLayer(JPH::ObjectLayer objectLayer, JPH::BroadPhaseLayer broadPhaseLayer);
        void SetObjectLayerCollision(JPH::ObjectLayer layer1, JPH::ObjectLayer layer2, bool shouldCollide);
        void SetObjectVsBroadPhaseLayerCollision(JPH::ObjectLayer objectLayer, JPH::BroadPhaseLayer broadPhaseLayer, bool shouldCollide);
        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json);
        void LoadJSON(const nlohmann::json &json);

    private:
        void resetDefault();
        void rebuildObjectVsBroadPhaseLayerPairs();
    };
}

#endif
