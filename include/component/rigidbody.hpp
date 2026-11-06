#ifndef RIGIDBODY_H
#define RIGIDBODY_H

#include <json_validation.hpp>
#include <utility>
#include <physics/shape.hpp>
#include <component/transform.hpp>
#include <Jolt/Physics/Body/BodyLock.h>

namespace vke_component
{
    struct RigidBodyData
    {
        vke_physics::PhyscisShapeData shape;
        JPH::EMotionType motionType = JPH::EMotionType::Dynamic;
        JPH::EMotionQuality motionQuality = JPH::EMotionQuality::Discrete;
        JPH::ObjectLayer layer = vke_physics::DefaultObjectLayers::MOVING;
        float friction = 0.2f;
        float restitution = 0.0f;
        float gravityFactor = 1.0f;
        bool hasMassOverride = false;
        float mass = 0.0f;

        // Call before constructing from JSON; asset checks happen in LoadAssets where applicable.
        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            auto result = Object(json).Require({"shape", "motionType", "layer", "friction", "restitution"})
                .Unsigneds({"motionType"}, 2).Unsigneds({"motionQuality"}, 1).Unsigneds({"layer"}, UINT32_MAX)
                .Numbers({"friction", "restitution", "gravityFactor", "mass"}).Result();
            if (!result) return result;
            if (json.contains("mass") && json["mass"].get<float>() <= 0.0f)
                return std::unexpected("rigidbody requires positive mass override");
            return vke_physics::PhyscisShapeData::ValidateJSON(json["shape"]);
        }

        RigidBodyData() = default;
        RigidBodyData(const nlohmann::json &json)
            : shape(json["shape"]), motionType(json["motionType"]),
              motionQuality(json.value("motionQuality", JPH::EMotionQuality::Discrete)),
              layer(json["layer"]), friction(json["friction"]), restitution(json["restitution"]),
              gravityFactor(json.value("gravityFactor", 1.0f)), hasMassOverride(json.contains("mass")),
              mass(hasMassOverride ? json["mass"].get<float>() : 0.0f) {}
        nlohmann::json ToJSON() const
        {
            nlohmann::json json = {{"type", "rigidbody"}, {"motionType", motionType},
                                   {"motionQuality", motionQuality}, {"layer", (int)layer},
                                   {"friction", friction}, {"restitution", restitution},
                                   {"gravityFactor", gravityFactor}, {"shape", shape.ToJSON()}};
            if (hasMassOverride)
                json["mass"] = mass;
            return json;
        }
    };

    class RigidBody
    {
    public:
        JPH::BodyID bodyID;
        // Jolt stores the resulting mass, not whether it was explicitly overridden.
        bool hasMassOverride = false;
        float mass = 0.0f;

        RigidBody(const RigidBody &) = delete;
        RigidBody &operator=(const RigidBody &) = delete;

        RigidBody(RigidBody &&other) noexcept
            : bodyID(std::exchange(other.bodyID, JPH::BodyID{})),
              hasMassOverride(other.hasMassOverride),
              mass(other.mass),
              staticMotionQuality(other.staticMotionQuality),
              staticGravityFactor(other.staticGravityFactor) {}

        RigidBody &operator=(RigidBody &&other) noexcept
        {
            if (this != &other)
            {
                Unload();
                bodyID = std::exchange(other.bodyID, JPH::BodyID{});
                hasMassOverride = other.hasMassOverride;
                mass = other.mass;
                staticMotionQuality = other.staticMotionQuality;
                staticGravityFactor = other.staticGravityFactor;
            }
            return *this;
        }

        RigidBody(entt::entity entity, const vke_common::Transform &transform,
                  const RigidBodyData &data)
        {
            const vke_physics::PhyscisShape shape(data.shape);
            const glm::vec3 position = transform.GetGlobalPosition();
            const glm::quat rotation = transform.GetGlobalRotation();
            JPH::BodyCreationSettings settings(shape.shapeRef,
                JPH::RVec3(position.x, position.y, position.z),
                JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w), data.motionType, data.layer);
            settings.mUserData = static_cast<uint64_t>(entity);
            settings.mFriction = data.friction;
            settings.mRestitution = data.restitution;
            settings.mMotionQuality = data.motionQuality;
            settings.mGravityFactor = data.gravityFactor;
            hasMassOverride = data.hasMassOverride;
            mass = data.mass;
            staticMotionQuality = data.motionQuality;
            staticGravityFactor = data.gravityFactor;
            applyMassOverride(settings);
            bodyID = vke_physics::PhysicsManager::GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::Activate);
        }

        // A transient snapshot for export/editor use, with authoring intent restored.
        JPH::BodyCreationSettings GetSettings() const
        {
            JPH::BodyLockRead lock(vke_physics::PhysicsManager::GetPhysicsSystem().GetBodyLockInterface(), bodyID);
            VKE_FATAL_IF(!lock.Succeeded(), "RigidBody is not loaded")
            auto settings = lock.GetBody().GetBodyCreationSettings();
            if (!settings.mAllowDynamicOrKinematic)
            {
                settings.mMotionQuality = staticMotionQuality;
                settings.mGravityFactor = staticGravityFactor;
            }
            applyMassOverride(settings);
            return settings;
        }

        void FillData(RigidBodyData &data) const
        {
            const auto settings = GetSettings();
            data.motionType = settings.mMotionType;
            data.motionQuality = settings.mMotionQuality;
            data.layer = settings.mObjectLayer;
            data.friction = settings.mFriction;
            data.restitution = settings.mRestitution;
            data.gravityFactor = settings.mGravityFactor;
            data.hasMassOverride = hasMassOverride;
            data.mass = mass;
            vke_physics::PhyscisShape(settings.GetShape()).FillData(data.shape);
        }

        void SetGravityFactor(float factor)
        {
            staticGravityFactor = factor;
            vke_physics::PhysicsManager::GetBodyInterface().SetGravityFactor(bodyID, factor);
        }

        void SetMotionQuality(JPH::EMotionQuality quality)
        {
            staticMotionQuality = quality;
            vke_physics::PhysicsManager::GetBodyInterface().SetMotionQuality(bodyID, quality);
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

    private:
        // Initially static bodies have no MotionProperties in Jolt. Preserve these
        // settings for serialization even though they do not affect a static body.
        JPH::EMotionQuality staticMotionQuality = JPH::EMotionQuality::Discrete;
        float staticGravityFactor = 1.0f;

        void applyMassOverride(JPH::BodyCreationSettings &settings) const
        {
            settings.mOverrideMassProperties = hasMassOverride
                ? JPH::EOverrideMassProperties::CalculateInertia
                : JPH::EOverrideMassProperties::CalculateMassAndInertia;
            if (hasMassOverride)
                settings.mMassPropertiesOverride.mMass = mass;
        }
    };
}

#endif
