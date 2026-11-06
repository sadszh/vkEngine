#ifndef CHARACTER_CONTROLLER_H
#define CHARACTER_CONTROLLER_H

#include <json_validation.hpp>
#include <physics/shape.hpp>
#include <component/transform.hpp>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/ShapeFilter.h>
#include <memory>

namespace vke_component
{
    struct CharacterControllerData
    {
        vke_physics::PhyscisShapeData shape;
        JPH::ObjectLayer layer = vke_physics::DefaultObjectLayers::MOVING;
        float mass = 70.0f;
        float maxStrength = 100.0f;
        float maxSlopeAngleRadians = glm::radians(50.0f);
        float predictiveContactDistance = 0.1f;
        float characterPadding = 0.02f;
        bool createInnerBody = true;

        // Call before constructing from JSON; asset checks happen in LoadAssets where applicable.
        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            auto result = Object(json).Require({"shape"}).Unsigneds({"layer"}, UINT32_MAX).Numbers({"mass", "maxStrength", "maxSlopeAngle", "predictiveContactDistance", "characterPadding"}).Booleans({"createInnerBody"}).Result();
            if (!result)
                return result;
            return vke_physics::PhyscisShapeData::ValidateJSON(json["shape"]);
        }

        CharacterControllerData()
        {
            shape.type = vke_physics::PHYSICS_SHAPE_CAPSULE;
        }
        CharacterControllerData(const nlohmann::json &json)
            : shape(json["shape"]),
              layer(json.value("layer", (int)vke_physics::DefaultObjectLayers::MOVING))
        {
            mass = json.value("mass", mass);
            maxStrength = json.value("maxStrength", maxStrength);
            maxSlopeAngleRadians =
                glm::radians(json.value("maxSlopeAngle",
                                        glm::degrees(maxSlopeAngleRadians)));
            predictiveContactDistance = json.value("predictiveContactDistance", predictiveContactDistance);
            characterPadding = json.value("characterPadding", characterPadding);
            createInnerBody = json.value("createInnerBody", true);
        }
        nlohmann::json ToJSON() const
        {
            return {{"type", "characterController"},
                    {"layer", (int)layer},
                    {"mass", mass},
                    {"maxStrength", maxStrength},
                    {"maxSlopeAngle", glm::degrees(maxSlopeAngleRadians)},
                    {"predictiveContactDistance", predictiveContactDistance},
                    {"characterPadding", characterPadding},
                    {"createInnerBody", createInnerBody},
                    {"shape", shape.ToJSON()}};
        }
    };

    class CharacterController
    {
    public:
        std::shared_ptr<vke_physics::PhyscisShape> shape;
        JPH::Ref<JPH::CharacterVirtualSettings> settings;
        JPH::Ref<JPH::CharacterVirtual> character;
        JPH::ObjectLayer layer;
        JPH::Vec3 desiredVelocity;
        float verticalVelocity;

        CharacterController(const CharacterController &) = delete;
        CharacterController &operator=(const CharacterController &) = delete;
        CharacterController(CharacterController &&) noexcept = default;
        CharacterController &operator=(CharacterController &&) noexcept = default;

        CharacterController(entt::entity entity, const vke_common::Transform &transform,
                            const CharacterControllerData &componentData)
            : shape(std::make_shared<vke_physics::PhyscisShape>(componentData.shape)),
              settings(new JPH::CharacterVirtualSettings()),
              layer(componentData.layer),
              desiredVelocity(JPH::Vec3::sZero()), verticalVelocity(0.0f)
        {
            settings->mMass = componentData.mass;
            settings->mMaxStrength = componentData.maxStrength;
            settings->mMaxSlopeAngle = componentData.maxSlopeAngleRadians;
            settings->mPredictiveContactDistance = componentData.predictiveContactDistance;
            settings->mCharacterPadding = componentData.characterPadding;
            if (componentData.createInnerBody)
                settings->mInnerBodyShape = shape->shapeRef;
            settings->mShape = shape->shapeRef;
            settings->mInnerBodyLayer = layer;

            const glm::vec3 position = transform.GetGlobalPosition();
            const glm::quat rotation = transform.GetGlobalRotation();
            character = new JPH::CharacterVirtual(settings,
                                                  JPH::RVec3(position.x, position.y, position.z),
                                                  JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w),
                                                  static_cast<uint64_t>(entity),
                                                  &vke_physics::PhysicsManager::GetPhysicsSystem());
        }

        ~CharacterController() {}

        void FillData(CharacterControllerData &data) const
        {
            data.layer = layer;
            data.mass = settings->mMass;
            data.maxStrength = settings->mMaxStrength;
            data.maxSlopeAngleRadians = settings->mMaxSlopeAngle;
            data.predictiveContactDistance = settings->mPredictiveContactDistance;
            data.characterPadding = settings->mCharacterPadding;
            data.createInnerBody = settings->mInnerBodyShape != nullptr;
            shape->FillData(data.shape);
        }

        void Unload()
        {
            character = nullptr;
        }

        void SetDesiredVelocity(const glm::vec3 &velocity)
        {
            if (velocity.y > 0.0f && desiredVelocity.GetY() <= 0.0f)
                verticalVelocity = velocity.y;
            desiredVelocity = JPH::Vec3(velocity.x, velocity.y, velocity.z);
        }

        glm::vec3 GetLinearVelocity() const
        {
            if (character == nullptr)
                return glm::vec3(0.0f);

            const JPH::Vec3 velocity = character->GetLinearVelocity();
            return glm::vec3(velocity.GetX(), velocity.GetY(), velocity.GetZ());
        }

        bool IsGrounded() const
        {
            return character != nullptr && character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
        }

        void FixedUpdate(float deltaTime)
        {
            if (character == nullptr)
                return;

            const JPH::Vec3 gravity = vke_physics::PhysicsManager::GetPhysicsSystem().GetGravity();
            verticalVelocity += gravity.GetY() * deltaTime;
            if (IsGrounded() && verticalVelocity < 0.0f)
                verticalVelocity = 0.0f;

            JPH::Vec3 velocity(desiredVelocity.GetX(),
                               verticalVelocity,
                               desiredVelocity.GetZ());
            character->SetLinearVelocity(velocity);

            JPH::PhysicsSystem &physicsSystem = vke_physics::PhysicsManager::GetPhysicsSystem();
            JPH::DefaultBroadPhaseLayerFilter broadPhaseFilter = physicsSystem.GetDefaultBroadPhaseLayerFilter(layer);
            JPH::DefaultObjectLayerFilter objectLayerFilter = physicsSystem.GetDefaultLayerFilter(layer);
            JPH::BodyFilter bodyFilter;
            JPH::ShapeFilter shapeFilter;
            character->Update(deltaTime, gravity, broadPhaseFilter, objectLayerFilter, bodyFilter, shapeFilter,
                              vke_physics::PhysicsManager::GetTempAllocator());

            // A ceiling collision must also consume the jump velocity kept between steps.
            if (verticalVelocity > 0.0f)
                for (const auto &contact : character->GetActiveContacts())
                    if (contact.mHadCollision && !contact.mWasDiscarded && contact.mContactNormal.GetY() < 0.0f)
                    {
                        verticalVelocity = 0.0f;
                        velocity.SetY(0.0f);
                        character->SetLinearVelocity(velocity);
                        break;
                    }
        }

        void OnTransformed(vke_common::Transform &transform)
        {
            if (character == nullptr)
                return;

            const glm::vec3 position = transform.GetGlobalPosition();
            const glm::quat rotation = transform.GetGlobalRotation();
            character->SetPosition(JPH::RVec3(position.x, position.y, position.z));
            character->SetRotation(JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w));
        }
    };
}

#endif
