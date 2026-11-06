#ifndef SENSOR_H
#define SENSOR_H

#include <json_validation.hpp>
#include <utility>
#include <physics/shape.hpp>
#include <component/transform.hpp>
#include <Jolt/Physics/Body/BodyLock.h>

namespace vke_component
{
    struct SensorData
    {
        vke_physics::PhyscisShapeData shape;
        bool isStatic = true;
        JPH::EMotionQuality motionQuality = JPH::EMotionQuality::Discrete;
        JPH::ObjectLayer layer = vke_physics::DefaultObjectLayers::NON_MOVING;

        // Call before constructing from JSON; asset checks happen in LoadAssets where applicable.
        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            auto result = Object(json).Require({"shape", "layer"}).Booleans({"isStatic"})
                .Unsigneds({"motionQuality"}, 1).Unsigneds({"layer"}, UINT32_MAX).Result();
            if (!result) return result;
            return vke_physics::PhyscisShapeData::ValidateJSON(json["shape"]);
        }

        SensorData() = default;
        SensorData(const nlohmann::json &json)
            : shape(json["shape"]),
              isStatic(json.value("isStatic", true)),
              motionQuality(json.value("motionQuality", JPH::EMotionQuality::Discrete)),
              layer(json["layer"]) {}
        nlohmann::json ToJSON() const
        {
            return {{"type", "sensor"},
                    {"isStatic", isStatic},
                    {"motionQuality", motionQuality},
                    {"layer", (int)layer},
                    {"shape", shape.ToJSON()}};
        }
    };

    class Sensor
    {
    public:
        JPH::BodyID bodyID;

        Sensor(const Sensor &) = delete;
        Sensor &operator=(const Sensor &) = delete;

        Sensor(Sensor &&other) noexcept
            : bodyID(std::exchange(other.bodyID, JPH::BodyID{})) {}

        Sensor &operator=(Sensor &&other) noexcept
        {
            if (this != &other)
            {
                Unload();
                bodyID = std::exchange(other.bodyID, JPH::BodyID{});
            }
            return *this;
        }

        Sensor(entt::entity entity, const vke_common::Transform &transform,
               const SensorData &data)
        {
            const vke_physics::PhyscisShape shape(data.shape);
            const glm::vec3 position = transform.GetGlobalPosition();
            const glm::quat rotation = transform.GetGlobalRotation();
            JPH::BodyCreationSettings settings(shape.shapeRef,
                JPH::RVec3(position.x, position.y, position.z),
                JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w),
                data.isStatic ? JPH::EMotionType::Static : JPH::EMotionType::Kinematic, data.layer);
            settings.mUserData = static_cast<uint64_t>(entity);
            settings.mIsSensor = true;
            settings.mAllowDynamicOrKinematic = true;
            settings.mMotionQuality = data.motionQuality;
            bodyID = vke_physics::PhysicsManager::GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::Activate);
        }

        JPH::BodyCreationSettings GetSettings() const
        {
            JPH::BodyLockRead lock(vke_physics::PhysicsManager::GetPhysicsSystem().GetBodyLockInterface(), bodyID);
            VKE_FATAL_IF(!lock.Succeeded(), "Sensor is not loaded")
            return lock.GetBody().GetBodyCreationSettings();
        }

        void FillData(SensorData &data) const
        {
            const auto settings = GetSettings();
            data.isStatic = settings.mMotionType == JPH::EMotionType::Static;
            data.motionQuality = settings.mMotionQuality;
            data.layer = settings.mObjectLayer;
            vke_physics::PhyscisShape(settings.GetShape()).FillData(data.shape);
        }

        void Unload()
        {
            if (bodyID.IsInvalid()) return;
            auto &interface = vke_physics::PhysicsManager::GetBodyInterface();
            interface.RemoveBody(bodyID);
            interface.DestroyBody(bodyID);
            bodyID = JPH::BodyID();
        }

        void OnTransformed(const vke_common::Transform &transform)
        {
            if (bodyID.IsInvalid() || vke_physics::PhysicsManager::GetInstance() == nullptr)
                return;
            const glm::vec3 position = transform.GetGlobalPosition();
            const glm::quat rotation = transform.GetGlobalRotation();
            auto &interface = vke_physics::PhysicsManager::GetBodyInterface();
            if (interface.IsAdded(bodyID))
                interface.SetPositionAndRotationWhenChanged(bodyID,
                    JPH::RVec3(position.x, position.y, position.z),
                    JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w), JPH::EActivation::Activate);
        }
    };
}

#endif
