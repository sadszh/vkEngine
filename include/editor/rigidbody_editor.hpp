#ifndef EDITOR_RIGIDBODY_EDITOR_H
#define EDITOR_RIGIDBODY_EDITOR_H

#include <component/rigidbody.hpp>
#include <editor/physics_shape_editor.hpp>
#include <script.hpp>

namespace vke_editor
{
    // Edit-mode mutation on the main thread. Failure preserves the existing body.
    inline vke_common::Result<void> RebuildRigidBodyMotionType(
        vke_component::RigidBody &body, JPH::EMotionType motionType)
    {
        auto settings = body.GetSettings();
        if (motionType != JPH::EMotionType::Static &&
            !IsDynamicBodyShape(vke_physics::PhyscisShape(settings.GetShape()).type))
            return std::unexpected("Select a shape that supports moving bodies before changing motion type");

        if (!settings.mAllowDynamicOrKinematic)
        {
            // Static bodies have no stored damping or velocity limits. Use creation defaults.
            const JPH::BodyCreationSettings defaults;
            settings.mLinearDamping = defaults.mLinearDamping;
            settings.mAngularDamping = defaults.mAngularDamping;
            settings.mMaxLinearVelocity = defaults.mMaxLinearVelocity;
            settings.mMaxAngularVelocity = defaults.mMaxAngularVelocity;
        }
        settings.mMotionType = motionType;
        settings.mAllowDynamicOrKinematic = false;
        auto &bodyInterface = vke_physics::PhysicsManager::GetBodyInterface();
        auto *replacement = bodyInterface.CreateBody(settings);
        if (replacement == nullptr)
            return std::unexpected("Could not allocate the replacement rigid body");

        const auto oldBodyID = body.bodyID;
        body.Unload();
        body.bodyID = replacement->GetID();
        bodyInterface.AddBody(body.bodyID, JPH::EActivation::Activate);
        body.SetGravityFactor(settings.mGravityFactor);
        body.SetMotionQuality(settings.mMotionQuality);
        vke_common::ScriptManager::GetPhysicsFunctions().rebindRigidBody(
            oldBodyID.GetIndexAndSequenceNumber(), body.bodyID.GetIndexAndSequenceNumber());
        return {};
    }
}

#endif
