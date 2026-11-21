#include <component/rigidbody.hpp>
#include <component/sensor.hpp>
#include <interop/physics.hpp>
#include <input.hpp>
#include <physics/physics.hpp>
#include <physics/shape.hpp>
#include <scene.hpp>
#include <vector>

namespace vke_interop
{
    static JPH::Vec3 ToJoltVec3(const Vector3<float> &value)
    {
        return JPH::Vec3(value.x, value.y, value.z);
    }

    static JPH::RVec3 ToJoltRVec3(const Vector3<float> &value)
    {
        return JPH::RVec3(value.x, value.y, value.z);
    }

    static JPH::Quat ToJoltQuat(const Quaternion<float> &value)
    {
        return JPH::Quat(value.x, value.y, value.z, value.w).Normalized();
    }

    static Vector3<float> ToInterop(const JPH::Vec3 &value)
    {
        return Vector3<float>{value.GetX(), value.GetY(), value.GetZ()};
    }

    static JPH::BodyID ToBodyID(uint32_t bodyID)
    {
        return JPH::BodyID(bodyID);
    }

    static JPH::EActivation ToActivation(int32_t activation)
    {
        return activation == 0 ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
    }

    static JPH::ShapeRefC CreateJoltShape(int32_t shapeType, const void *shapeParams)
    {
        if (shapeParams == nullptr)
            return nullptr;

        switch (static_cast<vke_physics::PhyscisShapeType>(shapeType))
        {
        case vke_physics::PHYSICS_SHAPE_SPHERE:
        {
            const auto *shape = static_cast<const NativeSphereShape *>(shapeParams);
            return new JPH::SphereShape(shape->Radius);
        }
        case vke_physics::PHYSICS_SHAPE_BOX:
        {
            const auto *shape = static_cast<const NativeBoxShape *>(shapeParams);
            return new JPH::BoxShape(ToJoltVec3(shape->HalfExtent));
        }
        case vke_physics::PHYSICS_SHAPE_CAPSULE:
        {
            const auto *shape = static_cast<const NativeCapsuleShape *>(shapeParams);
            return new JPH::CapsuleShape(shape->HalfHeight, shape->Radius);
        }
        case vke_physics::PHYSICS_SHAPE_CYLINDER:
        {
            const auto *shape = static_cast<const NativeCylinderShape *>(shapeParams);
            return new JPH::CylinderShape(shape->HalfHeight, shape->Radius);
        }
        case vke_physics::PHYSICS_SHAPE_TRIANGLE:
        {
            const auto *shape = static_cast<const NativeTriangleShape *>(shapeParams);
            return new JPH::TriangleShape(ToJoltVec3(shape->V1), ToJoltVec3(shape->V2), ToJoltVec3(shape->V3));
        }
        case vke_physics::PHYSICS_SHAPE_PLANE:
        {
            const auto *shape = static_cast<const NativePlaneShape *>(shapeParams);
            return new JPH::PlaneShape(JPH::Plane(ToJoltVec3(shape->Normal), shape->Constant));
        }
        default:
            return nullptr;
        }
    }

    uint32_t VKE_INTEROP_CDECL GetRigidBodyBodyID(uint32_t entity)
    {
        auto *scene = vke_common::SceneManager::GetInstance();
        entt::entity ent = static_cast<entt::entity>(entity);
        if (!scene->registry.valid(ent) || !scene->registry.all_of<vke_component::RigidBody>(ent))
            return JPH::BodyID::cInvalidBodyID;

        return scene->registry.get<vke_component::RigidBody>(ent).bodyID.GetIndexAndSequenceNumber();
    }

    uint32_t VKE_INTEROP_CDECL GetSensorBodyID(uint32_t entity)
    {
        auto *scene = vke_common::SceneManager::GetInstance();
        entt::entity ent = static_cast<entt::entity>(entity);
        if (!scene->registry.valid(ent) || !scene->registry.all_of<vke_component::Sensor>(ent))
            return JPH::BodyID::cInvalidBodyID;

        return scene->registry.get<vke_component::Sensor>(ent).bodyID.GetIndexAndSequenceNumber();
    }

    void VKE_INTEROP_CDECL ActivateBody(uint32_t bodyID)
    {
        vke_physics::PhysicsManager::GetBodyInterface().ActivateBody(ToBodyID(bodyID));
    }

    void VKE_INTEROP_CDECL DeactivateBody(uint32_t bodyID)
    {
        vke_physics::PhysicsManager::GetBodyInterface().DeactivateBody(ToBodyID(bodyID));
    }

    int32_t VKE_INTEROP_CDECL IsBodyActive(uint32_t bodyID)
    {
        return vke_physics::PhysicsManager::GetBodyInterface().IsActive(ToBodyID(bodyID)) ? 1 : 0;
    }

    void VKE_INTEROP_CDECL SetBodyObjectLayer(uint32_t bodyID, uint32_t objectLayer)
    {
        vke_physics::PhysicsManager::GetBodyInterface().SetObjectLayer(ToBodyID(bodyID), static_cast<JPH::ObjectLayer>(objectLayer));
    }

    uint32_t VKE_INTEROP_CDECL GetBodyObjectLayer(uint32_t bodyID)
    {
        return vke_physics::PhysicsManager::GetBodyInterface().GetObjectLayer(ToBodyID(bodyID));
    }

    void VKE_INTEROP_CDECL SetBodyRestitution(uint32_t bodyID, float restitution)
    {
        vke_physics::PhysicsManager::GetBodyInterface().SetRestitution(ToBodyID(bodyID), restitution);
    }

    float VKE_INTEROP_CDECL GetBodyRestitution(uint32_t bodyID)
    {
        return vke_physics::PhysicsManager::GetBodyInterface().GetRestitution(ToBodyID(bodyID));
    }

    void VKE_INTEROP_CDECL SetBodyFriction(uint32_t bodyID, float friction)
    {
        vke_physics::PhysicsManager::GetBodyInterface().SetFriction(ToBodyID(bodyID), friction);
    }

    float VKE_INTEROP_CDECL GetBodyFriction(uint32_t bodyID)
    {
        return vke_physics::PhysicsManager::GetBodyInterface().GetFriction(ToBodyID(bodyID));
    }

    void VKE_INTEROP_CDECL SetBodyGravityFactor(uint32_t bodyID, float gravityFactor)
    {
        vke_physics::PhysicsManager::GetBodyInterface().SetGravityFactor(ToBodyID(bodyID), gravityFactor);
    }

    float VKE_INTEROP_CDECL GetBodyGravityFactor(uint32_t bodyID)
    {
        return vke_physics::PhysicsManager::GetBodyInterface().GetGravityFactor(ToBodyID(bodyID));
    }

    void VKE_INTEROP_CDECL GetBodyCenterOfMassPosition(uint32_t bodyID, Vector3<float> *position)
    {
        const JPH::RVec3 value = vke_physics::PhysicsManager::GetBodyInterface().GetCenterOfMassPosition(ToBodyID(bodyID));
        *position = Vector3<float>{static_cast<float>(value.GetX()), static_cast<float>(value.GetY()), static_cast<float>(value.GetZ())};
    }

    void VKE_INTEROP_CDECL MoveKinematicBody(uint32_t bodyID, const Vector3<float> *targetPosition, const Quaternion<float> *targetRotation, float deltaTime)
    {
        vke_physics::PhysicsManager::GetBodyInterface().MoveKinematic(ToBodyID(bodyID), ToJoltRVec3(*targetPosition), ToJoltQuat(*targetRotation), deltaTime);
    }

    void VKE_INTEROP_CDECL SetBodyLinearAndAngularVelocity(uint32_t bodyID, const Vector3<float> *linearVelocity, const Vector3<float> *angularVelocity)
    {
        vke_physics::PhysicsManager::GetBodyInterface().SetLinearAndAngularVelocity(ToBodyID(bodyID), ToJoltVec3(*linearVelocity), ToJoltVec3(*angularVelocity));
    }

    void VKE_INTEROP_CDECL GetBodyLinearAndAngularVelocity(uint32_t bodyID, Vector3<float> *linearVelocity, Vector3<float> *angularVelocity)
    {
        JPH::Vec3 linear;
        JPH::Vec3 angular;
        vke_physics::PhysicsManager::GetBodyInterface().GetLinearAndAngularVelocity(ToBodyID(bodyID), linear, angular);
        *linearVelocity = ToInterop(linear);
        *angularVelocity = ToInterop(angular);
    }

    void VKE_INTEROP_CDECL SetBodyLinearVelocity(uint32_t bodyID, const Vector3<float> *linearVelocity)
    {
        vke_physics::PhysicsManager::GetBodyInterface().SetLinearVelocity(ToBodyID(bodyID), ToJoltVec3(*linearVelocity));
    }

    void VKE_INTEROP_CDECL GetBodyLinearVelocity(uint32_t bodyID, Vector3<float> *linearVelocity)
    {
        *linearVelocity = ToInterop(vke_physics::PhysicsManager::GetBodyInterface().GetLinearVelocity(ToBodyID(bodyID)));
    }

    void VKE_INTEROP_CDECL AddBodyLinearVelocity(uint32_t bodyID, const Vector3<float> *linearVelocity)
    {
        vke_physics::PhysicsManager::GetBodyInterface().AddLinearVelocity(ToBodyID(bodyID), ToJoltVec3(*linearVelocity));
    }

    void VKE_INTEROP_CDECL AddBodyLinearAndAngularVelocity(uint32_t bodyID, const Vector3<float> *linearVelocity, const Vector3<float> *angularVelocity)
    {
        vke_physics::PhysicsManager::GetBodyInterface().AddLinearAndAngularVelocity(ToBodyID(bodyID), ToJoltVec3(*linearVelocity), ToJoltVec3(*angularVelocity));
    }

    void VKE_INTEROP_CDECL SetBodyAngularVelocity(uint32_t bodyID, const Vector3<float> *angularVelocity)
    {
        vke_physics::PhysicsManager::GetBodyInterface().SetAngularVelocity(ToBodyID(bodyID), ToJoltVec3(*angularVelocity));
    }

    void VKE_INTEROP_CDECL GetBodyAngularVelocity(uint32_t bodyID, Vector3<float> *angularVelocity)
    {
        *angularVelocity = ToInterop(vke_physics::PhysicsManager::GetBodyInterface().GetAngularVelocity(ToBodyID(bodyID)));
    }

    void VKE_INTEROP_CDECL GetBodyPointVelocity(uint32_t bodyID, const Vector3<float> *point, Vector3<float> *velocity)
    {
        *velocity = ToInterop(vke_physics::PhysicsManager::GetBodyInterface().GetPointVelocity(ToBodyID(bodyID), ToJoltRVec3(*point)));
    }

    void VKE_INTEROP_CDECL AddBodyForce(uint32_t bodyID, const Vector3<float> *force, int32_t activation)
    {
        vke_physics::PhysicsManager::GetBodyInterface().AddForce(ToBodyID(bodyID), ToJoltVec3(*force), ToActivation(activation));
    }

    void VKE_INTEROP_CDECL AddBodyForceAtPoint(uint32_t bodyID, const Vector3<float> *force, const Vector3<float> *point, int32_t activation)
    {
        vke_physics::PhysicsManager::GetBodyInterface().AddForce(ToBodyID(bodyID), ToJoltVec3(*force), ToJoltRVec3(*point), ToActivation(activation));
    }

    void VKE_INTEROP_CDECL AddBodyTorque(uint32_t bodyID, const Vector3<float> *torque, int32_t activation)
    {
        vke_physics::PhysicsManager::GetBodyInterface().AddTorque(ToBodyID(bodyID), ToJoltVec3(*torque), ToActivation(activation));
    }

    void VKE_INTEROP_CDECL AddBodyForceAndTorque(uint32_t bodyID, const Vector3<float> *force, const Vector3<float> *torque, int32_t activation)
    {
        vke_physics::PhysicsManager::GetBodyInterface().AddForceAndTorque(ToBodyID(bodyID), ToJoltVec3(*force), ToJoltVec3(*torque), ToActivation(activation));
    }

    void VKE_INTEROP_CDECL AddBodyImpulse(uint32_t bodyID, const Vector3<float> *impulse)
    {
        vke_physics::PhysicsManager::GetBodyInterface().AddImpulse(ToBodyID(bodyID), ToJoltVec3(*impulse));
    }

    void VKE_INTEROP_CDECL AddBodyImpulseAtPoint(uint32_t bodyID, const Vector3<float> *impulse, const Vector3<float> *point)
    {
        vke_physics::PhysicsManager::GetBodyInterface().AddImpulse(ToBodyID(bodyID), ToJoltVec3(*impulse), ToJoltRVec3(*point));
    }

    void VKE_INTEROP_CDECL AddBodyAngularImpulse(uint32_t bodyID, const Vector3<float> *angularImpulse)
    {
        vke_physics::PhysicsManager::GetBodyInterface().AddAngularImpulse(ToBodyID(bodyID), ToJoltVec3(*angularImpulse));
    }

    int32_t VKE_INTEROP_CDECL ApplyBodyBuoyancyImpulse(uint32_t bodyID, const Vector3<float> *surfacePosition, const Vector3<float> *surfaceNormal, float buoyancy, float linearDrag, float angularDrag, const Vector3<float> *fluidVelocity, const Vector3<float> *gravity, float deltaTime)
    {
        return vke_physics::PhysicsManager::GetBodyInterface().ApplyBuoyancyImpulse(ToBodyID(bodyID), ToJoltRVec3(*surfacePosition), ToJoltVec3(*surfaceNormal), buoyancy, linearDrag, angularDrag, ToJoltVec3(*fluidVelocity), ToJoltVec3(*gravity), deltaTime) ? 1 : 0;
    }

    int32_t VKE_INTEROP_CDECL SetBodyMotionType(uint32_t bodyID, int32_t motionType, int32_t activation)
    {
        return vke_physics::PhysicsManager::SetMotionType(ToBodyID(bodyID), static_cast<JPH::EMotionType>(motionType), ToActivation(activation)) ? 1 : 0;
    }

    int32_t VKE_INTEROP_CDECL GetBodyMotionType(uint32_t bodyID)
    {
        return static_cast<int32_t>(vke_physics::PhysicsManager::GetBodyInterface().GetMotionType(ToBodyID(bodyID)));
    }

    void VKE_INTEROP_CDECL SetBodyMotionQuality(uint32_t bodyID, int32_t motionQuality)
    {
        vke_physics::PhysicsManager::GetBodyInterface().SetMotionQuality(ToBodyID(bodyID), static_cast<JPH::EMotionQuality>(motionQuality));
    }

    int32_t VKE_INTEROP_CDECL GetBodyMotionQuality(uint32_t bodyID)
    {
        return static_cast<int32_t>(vke_physics::PhysicsManager::GetBodyInterface().GetMotionQuality(ToBodyID(bodyID)));
    }

    uint32_t VKE_INTEROP_CDECL GetPhysicsObjectLayerCount()
    {
        return vke_physics::PhysicsManager::GetConfig().objectLayerCount;
    }

    uint32_t VKE_INTEROP_CDECL GetPhysicsBroadPhaseLayerCount()
    {
        return vke_physics::PhysicsManager::GetConfig().broadPhaseLayerCount;
    }

    uint32_t VKE_INTEROP_CDECL GetPhysicsObjectLayerBroadPhaseLayer(uint32_t objectLayer)
    {
        const vke_physics::PhysicsConfig &config = vke_physics::PhysicsManager::GetConfig();
        if (objectLayer >= config.objectLayerCount)
            return 0;

        return config.objectToBroadPhase[objectLayer].GetValue();
    }

    void VKE_INTEROP_CDECL SetPhysicsObjectLayerCollision(uint32_t layer1, uint32_t layer2, int32_t shouldCollide)
    {
        vke_physics::PhysicsManager::SetObjectLayerCollision(static_cast<JPH::ObjectLayer>(layer1), static_cast<JPH::ObjectLayer>(layer2), shouldCollide != 0);
    }

    int32_t VKE_INTEROP_CDECL GetPhysicsObjectLayerCollision(uint32_t layer1, uint32_t layer2)
    {
        const vke_physics::PhysicsConfig &config = vke_physics::PhysicsManager::GetConfig();
        if (layer1 >= config.objectLayerCount || layer2 >= config.objectLayerCount)
            return 0;

        return config.objectLayerPairs[layer1][layer2] ? 1 : 0;
    }

    void VKE_INTEROP_CDECL SetPhysicsObjectVsBroadPhaseLayerCollision(uint32_t objectLayer, uint32_t broadPhaseLayer, int32_t shouldCollide)
    {
        vke_physics::PhysicsManager::SetObjectVsBroadPhaseLayerCollision(static_cast<JPH::ObjectLayer>(objectLayer), JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(broadPhaseLayer)), shouldCollide != 0);
    }

    int32_t VKE_INTEROP_CDECL GetPhysicsObjectVsBroadPhaseLayerCollision(uint32_t objectLayer, uint32_t broadPhaseLayer)
    {
        const vke_physics::PhysicsConfig &config = vke_physics::PhysicsManager::GetConfig();
        if (objectLayer >= config.objectLayerCount || broadPhaseLayer >= config.broadPhaseLayerCount)
            return 0;

        return config.objectVsBroadPhaseLayerPairs[objectLayer][broadPhaseLayer] ? 1 : 0;
    }

    static ContactEvent ToInterop(const vke_physics::ContactEvent &event)
    {
        return ContactEvent{
            static_cast<int32_t>(event.type),
            event.entity1,
            event.entity2,
            event.bodyID1.GetIndexAndSequenceNumber(),
            event.bodyID2.GetIndexAndSequenceNumber(),
            event.subShapeID1.GetValue(),
            event.subShapeID2.GetValue(),
            Vector3<float>{static_cast<float>(event.point1.GetX()), static_cast<float>(event.point1.GetY()), static_cast<float>(event.point1.GetZ())},
            Vector3<float>{static_cast<float>(event.point2.GetX()), static_cast<float>(event.point2.GetY()), static_cast<float>(event.point2.GetZ())},
            ToInterop(event.normal),
            event.penetrationDepth,
            event.isSensor ? 1 : 0};
    }

    uint32_t VKE_INTEROP_CDECL GetPhysicsContactEventCount()
    {
        return vke_physics::PhysicsManager::GetContactEventCount();
    }

    uint32_t VKE_INTEROP_CDECL GetPhysicsContactEvents(ContactEvent *events, uint32_t maxEvents)
    {
        if (events == nullptr || maxEvents == 0)
            return 0;

        std::vector<vke_physics::ContactEvent> physicsEvents(maxEvents);
        const uint32_t eventCount = vke_physics::PhysicsManager::GetContactEvents(physicsEvents.data(), maxEvents);
        for (uint32_t i = 0; i < eventCount; ++i)
            events[i] = ToInterop(physicsEvents[i]);

        return eventCount;
    }

    static RaycastHit ToInterop(const vke_physics::RaycastHit &hit)
    {
        const uint32_t bodyID = hit.bodyID.GetIndexAndSequenceNumber();
        return RaycastHit{
            hit.entity,
            bodyID,
            hit.subShapeID.GetValue(),
            Vector3<float>{static_cast<float>(hit.point.GetX()), static_cast<float>(hit.point.GetY()), static_cast<float>(hit.point.GetZ())},
            ToInterop(hit.normal),
            hit.distance,
            hit.fraction,
            hit.isSensor ? 1 : 0};
    }

    static CollidePointHit ToInterop(const vke_physics::CollidePointHit &hit)
    {
        const uint32_t bodyID = hit.bodyID.GetIndexAndSequenceNumber();
        return CollidePointHit{
            hit.entity,
            bodyID,
            hit.subShapeID.GetValue(),
            hit.isSensor ? 1 : 0};
    }

    static CollideShapeHit ToInterop(const vke_physics::CollideShapeHit &hit)
    {
        const uint32_t bodyID = hit.bodyID.GetIndexAndSequenceNumber();
        return CollideShapeHit{
            hit.entity,
            bodyID,
            hit.subShapeID1.GetValue(),
            hit.subShapeID2.GetValue(),
            ToInterop(hit.contactPointOn1),
            ToInterop(hit.contactPointOn2),
            ToInterop(hit.penetrationAxis),
            hit.penetrationDepth,
            hit.isSensor ? 1 : 0};
    }

    static ShapeCastHit ToInterop(const vke_physics::ShapeCastHit &hit)
    {
        const uint32_t bodyID = hit.bodyID.GetIndexAndSequenceNumber();
        return ShapeCastHit{
            hit.entity,
            bodyID,
            hit.subShapeID1.GetValue(),
            hit.subShapeID2.GetValue(),
            ToInterop(hit.contactPointOn1),
            ToInterop(hit.contactPointOn2),
            ToInterop(hit.penetrationAxis),
            hit.penetrationDepth,
            hit.distance,
            hit.fraction,
            hit.isBackFaceHit ? 1 : 0,
            hit.isSensor ? 1 : 0};
    }

    int32_t VKE_INTEROP_CDECL GetMouseRay(Vector3<float> *origin, Vector3<float> *direction)
    {
        *origin = {};
        *direction = {};
        const auto mouse = vke_common::InputManager::GetMouseViewportPosition();
        if (!mouse)
            return 0;
        const auto &camera = vke_render::Renderer::GetCameraInfo();
        auto point = camera.invProjection * glm::vec4(2 * mouse->x - 1, 2 * mouse->y - 1, 0, 1);
        point /= point.w;
        const auto ray = glm::normalize(glm::vec3(camera.invView * glm::vec4(glm::vec3(point), 0)));
        *origin = {camera.viewPos.x, camera.viewPos.y, camera.viewPos.z};
        *direction = {ray.x, ray.y, ray.z};
        return 1;
    }

    int32_t VKE_INTEROP_CDECL PhysicsRaycast(const Vector3<float> *origin, const Vector3<float> *direction, float maxDistance, uint32_t broadPhaseLayerMask, uint32_t objectLayerMask, RaycastHit *hit)
    {
        if (origin == nullptr || direction == nullptr || hit == nullptr)
            return 0;

        vke_physics::RaycastHit physicsHit;
        if (!vke_physics::PhysicsManager::Raycast(ToJoltRVec3(*origin), ToJoltVec3(*direction), maxDistance, physicsHit, broadPhaseLayerMask, objectLayerMask))
            return 0;

        *hit = ToInterop(physicsHit);
        return 1;
    }

    uint32_t VKE_INTEROP_CDECL PhysicsRaycastAll(const Vector3<float> *origin, const Vector3<float> *direction, float maxDistance, uint32_t broadPhaseLayerMask, uint32_t objectLayerMask, RaycastHit *hits, uint32_t maxHits)
    {
        if (origin == nullptr || direction == nullptr || hits == nullptr || maxHits == 0)
            return 0;

        std::vector<vke_physics::RaycastHit> physicsHits(maxHits);
        const uint32_t hitCount = vke_physics::PhysicsManager::RaycastAll(ToJoltRVec3(*origin), ToJoltVec3(*direction), maxDistance, physicsHits.data(), maxHits, broadPhaseLayerMask, objectLayerMask);
        for (uint32_t i = 0; i < hitCount; ++i)
            hits[i] = ToInterop(physicsHits[i]);

        return hitCount;
    }

    uint32_t VKE_INTEROP_CDECL PhysicsCollidePoint(const Vector3<float> *point, uint32_t broadPhaseLayerMask, uint32_t objectLayerMask, CollidePointHit *hits, uint32_t maxHits)
    {
        if (point == nullptr || hits == nullptr || maxHits == 0)
            return 0;

        std::vector<vke_physics::CollidePointHit> physicsHits(maxHits);
        const uint32_t hitCount = vke_physics::PhysicsManager::CollidePoint(ToJoltRVec3(*point), physicsHits.data(), maxHits, broadPhaseLayerMask, objectLayerMask);
        for (uint32_t i = 0; i < hitCount; ++i)
            hits[i] = ToInterop(physicsHits[i]);

        return hitCount;
    }

    uint32_t VKE_INTEROP_CDECL PhysicsCollideShape(int32_t shapeType, const void *shapeParams, const Vector3<float> *position, const Quaternion<float> *rotation, const Vector3<float> *scale, uint32_t broadPhaseLayerMask, uint32_t objectLayerMask, CollideShapeHit *hits, uint32_t maxHits)
    {
        if (shapeParams == nullptr || position == nullptr || rotation == nullptr || scale == nullptr || hits == nullptr || maxHits == 0)
            return 0;

        JPH::ShapeRefC shapeRef = CreateJoltShape(shapeType, shapeParams);
        if (shapeRef == nullptr)
            return 0;

        std::vector<vke_physics::CollideShapeHit> physicsHits(maxHits);
        const uint32_t hitCount = vke_physics::PhysicsManager::CollideShape(shapeRef.GetPtr(), ToJoltVec3(*scale), ToJoltRVec3(*position), ToJoltQuat(*rotation), physicsHits.data(), maxHits, broadPhaseLayerMask, objectLayerMask);
        for (uint32_t i = 0; i < hitCount; ++i)
            hits[i] = ToInterop(physicsHits[i]);

        return hitCount;
    }

    uint32_t VKE_INTEROP_CDECL PhysicsCastShape(int32_t shapeType, const void *shapeParams, const Vector3<float> *position, const Quaternion<float> *rotation, const Vector3<float> *scale, const Vector3<float> *direction, float maxDistance, uint32_t broadPhaseLayerMask, uint32_t objectLayerMask, ShapeCastHit *hits, uint32_t maxHits)
    {
        if (shapeParams == nullptr || position == nullptr || rotation == nullptr || scale == nullptr || direction == nullptr || hits == nullptr || maxHits == 0)
            return 0;

        JPH::ShapeRefC shapeRef = CreateJoltShape(shapeType, shapeParams);
        if (shapeRef == nullptr)
            return 0;

        std::vector<vke_physics::ShapeCastHit> physicsHits(maxHits);
        const uint32_t hitCount = vke_physics::PhysicsManager::CastShape(shapeRef.GetPtr(), ToJoltVec3(*scale), ToJoltRVec3(*position), ToJoltQuat(*rotation), ToJoltVec3(*direction), maxDistance, physicsHits.data(), maxHits, broadPhaseLayerMask, objectLayerMask);
        for (uint32_t i = 0; i < hitCount; ++i)
            hits[i] = ToInterop(physicsHits[i]);

        return hitCount;
    }
}
