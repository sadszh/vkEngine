#ifndef INTEROP_PHYSICS_H
#define INTEROP_PHYSICS_H

#include <cstdint>
#include <interop/interop.hpp>
#include <interop/math.hpp>

namespace vke_interop
{
    struct CSharpPhysicsFunctions
    {
        void (*rebindRigidBody)(uint32_t, uint32_t) = nullptr;
    };

    struct RaycastHit
    {
        uint32_t Entity;
        uint32_t BodyID;
        uint32_t SubShapeID;
        Vector3<float> Point;
        Vector3<float> Normal;
        float Distance;
        float Fraction;
        int32_t IsSensor;
    };

    struct NativeSphereShape
    {
        float Radius;
    };

    struct NativeBoxShape
    {
        Vector3<float> HalfExtent;
    };

    struct NativeCapsuleShape
    {
        float HalfHeight;
        float Radius;
    };

    struct NativeCylinderShape
    {
        float HalfHeight;
        float Radius;
    };

    struct NativeTriangleShape
    {
        Vector3<float> V1;
        Vector3<float> V2;
        Vector3<float> V3;
    };

    struct NativePlaneShape
    {
        Vector3<float> Normal;
        float Constant;
    };

    struct CollidePointHit
    {
        uint32_t Entity;
        uint32_t BodyID;
        uint32_t SubShapeID;
        int32_t IsSensor;
    };

    struct CollideShapeHit
    {
        uint32_t Entity;
        uint32_t BodyID;
        uint32_t SubShapeID1;
        uint32_t SubShapeID2;
        Vector3<float> ContactPointOnShape;
        Vector3<float> ContactPointOnBody;
        Vector3<float> PenetrationAxis;
        float PenetrationDepth;
        int32_t IsSensor;
    };

    struct ShapeCastHit
    {
        uint32_t Entity;
        uint32_t BodyID;
        uint32_t SubShapeID1;
        uint32_t SubShapeID2;
        Vector3<float> ContactPointOnShape;
        Vector3<float> ContactPointOnBody;
        Vector3<float> PenetrationAxis;
        float PenetrationDepth;
        float Distance;
        float Fraction;
        int32_t IsBackFaceHit;
        int32_t IsSensor;
    };

    struct ContactEvent
    {
        int32_t Type;
        uint32_t Entity1;
        uint32_t Entity2;
        uint32_t BodyID1;
        uint32_t BodyID2;
        uint32_t SubShapeID1;
        uint32_t SubShapeID2;
        Vector3<float> Point1;
        Vector3<float> Point2;
        Vector3<float> Normal;
        float PenetrationDepth;
        int32_t IsSensor;
    };

    using GetRigidBodyBodyIDFn = uint32_t(VKE_INTEROP_CDECL *)(uint32_t);
    using GetSensorBodyIDFn = uint32_t(VKE_INTEROP_CDECL *)(uint32_t);
    using ActivateBodyFn = void(VKE_INTEROP_CDECL *)(uint32_t);
    using DeactivateBodyFn = void(VKE_INTEROP_CDECL *)(uint32_t);
    using IsBodyActiveFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t);
    using SetBodyObjectLayerFn = void(VKE_INTEROP_CDECL *)(uint32_t, uint32_t);
    using GetBodyObjectLayerFn = uint32_t(VKE_INTEROP_CDECL *)(uint32_t);
    using SetBodyRestitutionFn = void(VKE_INTEROP_CDECL *)(uint32_t, float);
    using GetBodyRestitutionFn = float(VKE_INTEROP_CDECL *)(uint32_t);
    using SetBodyFrictionFn = void(VKE_INTEROP_CDECL *)(uint32_t, float);
    using GetBodyFrictionFn = float(VKE_INTEROP_CDECL *)(uint32_t);
    using SetBodyGravityFactorFn = void(VKE_INTEROP_CDECL *)(uint32_t, float);
    using GetBodyGravityFactorFn = float(VKE_INTEROP_CDECL *)(uint32_t);
    using GetBodyCenterOfMassPositionFn = void(VKE_INTEROP_CDECL *)(uint32_t, Vector3<float> *);
    using MoveKinematicBodyFn = void(VKE_INTEROP_CDECL *)(uint32_t, const Vector3<float> *, const Quaternion<float> *, float);
    using SetBodyLinearAndAngularVelocityFn = void(VKE_INTEROP_CDECL *)(uint32_t, const Vector3<float> *, const Vector3<float> *);
    using GetBodyLinearAndAngularVelocityFn = void(VKE_INTEROP_CDECL *)(uint32_t, Vector3<float> *, Vector3<float> *);
    using SetBodyLinearVelocityFn = void(VKE_INTEROP_CDECL *)(uint32_t, const Vector3<float> *);
    using GetBodyLinearVelocityFn = void(VKE_INTEROP_CDECL *)(uint32_t, Vector3<float> *);
    using AddBodyLinearVelocityFn = void(VKE_INTEROP_CDECL *)(uint32_t, const Vector3<float> *);
    using AddBodyLinearAndAngularVelocityFn = void(VKE_INTEROP_CDECL *)(uint32_t, const Vector3<float> *, const Vector3<float> *);
    using SetBodyAngularVelocityFn = void(VKE_INTEROP_CDECL *)(uint32_t, const Vector3<float> *);
    using GetBodyAngularVelocityFn = void(VKE_INTEROP_CDECL *)(uint32_t, Vector3<float> *);
    using GetBodyPointVelocityFn = void(VKE_INTEROP_CDECL *)(uint32_t, const Vector3<float> *, Vector3<float> *);
    using AddBodyForceFn = void(VKE_INTEROP_CDECL *)(uint32_t, const Vector3<float> *, int32_t);
    using AddBodyForceAtPointFn = void(VKE_INTEROP_CDECL *)(uint32_t, const Vector3<float> *, const Vector3<float> *, int32_t);
    using AddBodyTorqueFn = void(VKE_INTEROP_CDECL *)(uint32_t, const Vector3<float> *, int32_t);
    using AddBodyForceAndTorqueFn = void(VKE_INTEROP_CDECL *)(uint32_t, const Vector3<float> *, const Vector3<float> *, int32_t);
    using AddBodyImpulseFn = void(VKE_INTEROP_CDECL *)(uint32_t, const Vector3<float> *);
    using AddBodyImpulseAtPointFn = void(VKE_INTEROP_CDECL *)(uint32_t, const Vector3<float> *, const Vector3<float> *);
    using AddBodyAngularImpulseFn = void(VKE_INTEROP_CDECL *)(uint32_t, const Vector3<float> *);
    using ApplyBodyBuoyancyImpulseFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, const Vector3<float> *, const Vector3<float> *, float, float, float, const Vector3<float> *, const Vector3<float> *, float);
    using SetBodyMotionTypeFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, int32_t, int32_t);
    using GetBodyMotionTypeFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t);
    using SetBodyMotionQualityFn = void(VKE_INTEROP_CDECL *)(uint32_t, int32_t);
    using GetBodyMotionQualityFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t);
    using GetPhysicsObjectLayerCountFn = uint32_t(VKE_INTEROP_CDECL *)();
    using GetPhysicsBroadPhaseLayerCountFn = uint32_t(VKE_INTEROP_CDECL *)();
    using GetPhysicsObjectLayerBroadPhaseLayerFn = uint32_t(VKE_INTEROP_CDECL *)(uint32_t);
    using SetPhysicsObjectLayerCollisionFn = void(VKE_INTEROP_CDECL *)(uint32_t, uint32_t, int32_t);
    using GetPhysicsObjectLayerCollisionFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint32_t);
    using SetPhysicsObjectVsBroadPhaseLayerCollisionFn = void(VKE_INTEROP_CDECL *)(uint32_t, uint32_t, int32_t);
    using GetPhysicsObjectVsBroadPhaseLayerCollisionFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint32_t);
    using GetPhysicsContactEventCountFn = uint32_t(VKE_INTEROP_CDECL *)();
    using GetPhysicsContactEventsFn = uint32_t(VKE_INTEROP_CDECL *)(ContactEvent *, uint32_t);
    using GetMouseRayFn = int32_t(VKE_INTEROP_CDECL *)(Vector3<float> *, Vector3<float> *);
    using PhysicsRaycastFn = int32_t(VKE_INTEROP_CDECL *)(const Vector3<float> *, const Vector3<float> *, float, uint32_t, uint32_t, RaycastHit *);
    using PhysicsRaycastAllFn = uint32_t(VKE_INTEROP_CDECL *)(const Vector3<float> *, const Vector3<float> *, float, uint32_t, uint32_t, RaycastHit *, uint32_t);
    using PhysicsCollidePointFn = uint32_t(VKE_INTEROP_CDECL *)(const Vector3<float> *, uint32_t, uint32_t, CollidePointHit *, uint32_t);
    using PhysicsCollideShapeFn = uint32_t(VKE_INTEROP_CDECL *)(int32_t, const void *, const Vector3<float> *, const Quaternion<float> *, const Vector3<float> *, uint32_t, uint32_t, CollideShapeHit *, uint32_t);
    using PhysicsCastShapeFn = uint32_t(VKE_INTEROP_CDECL *)(int32_t, const void *, const Vector3<float> *, const Quaternion<float> *, const Vector3<float> *, const Vector3<float> *, float, uint32_t, uint32_t, ShapeCastHit *, uint32_t);

    uint32_t VKE_INTEROP_CDECL GetRigidBodyBodyID(uint32_t entity);
    uint32_t VKE_INTEROP_CDECL GetSensorBodyID(uint32_t entity);
    void VKE_INTEROP_CDECL ActivateBody(uint32_t bodyID);
    void VKE_INTEROP_CDECL DeactivateBody(uint32_t bodyID);
    int32_t VKE_INTEROP_CDECL IsBodyActive(uint32_t bodyID);
    void VKE_INTEROP_CDECL SetBodyObjectLayer(uint32_t bodyID, uint32_t objectLayer);
    uint32_t VKE_INTEROP_CDECL GetBodyObjectLayer(uint32_t bodyID);
    void VKE_INTEROP_CDECL SetBodyRestitution(uint32_t bodyID, float restitution);
    float VKE_INTEROP_CDECL GetBodyRestitution(uint32_t bodyID);
    void VKE_INTEROP_CDECL SetBodyFriction(uint32_t bodyID, float friction);
    float VKE_INTEROP_CDECL GetBodyFriction(uint32_t bodyID);
    void VKE_INTEROP_CDECL SetBodyGravityFactor(uint32_t bodyID, float gravityFactor);
    float VKE_INTEROP_CDECL GetBodyGravityFactor(uint32_t bodyID);
    void VKE_INTEROP_CDECL GetBodyCenterOfMassPosition(uint32_t bodyID, Vector3<float> *position);
    void VKE_INTEROP_CDECL MoveKinematicBody(uint32_t bodyID, const Vector3<float> *targetPosition, const Quaternion<float> *targetRotation, float deltaTime);
    void VKE_INTEROP_CDECL SetBodyLinearAndAngularVelocity(uint32_t bodyID, const Vector3<float> *linearVelocity, const Vector3<float> *angularVelocity);
    void VKE_INTEROP_CDECL GetBodyLinearAndAngularVelocity(uint32_t bodyID, Vector3<float> *linearVelocity, Vector3<float> *angularVelocity);
    void VKE_INTEROP_CDECL SetBodyLinearVelocity(uint32_t bodyID, const Vector3<float> *linearVelocity);
    void VKE_INTEROP_CDECL GetBodyLinearVelocity(uint32_t bodyID, Vector3<float> *linearVelocity);
    void VKE_INTEROP_CDECL AddBodyLinearVelocity(uint32_t bodyID, const Vector3<float> *linearVelocity);
    void VKE_INTEROP_CDECL AddBodyLinearAndAngularVelocity(uint32_t bodyID, const Vector3<float> *linearVelocity, const Vector3<float> *angularVelocity);
    void VKE_INTEROP_CDECL SetBodyAngularVelocity(uint32_t bodyID, const Vector3<float> *angularVelocity);
    void VKE_INTEROP_CDECL GetBodyAngularVelocity(uint32_t bodyID, Vector3<float> *angularVelocity);
    void VKE_INTEROP_CDECL GetBodyPointVelocity(uint32_t bodyID, const Vector3<float> *point, Vector3<float> *velocity);
    void VKE_INTEROP_CDECL AddBodyForce(uint32_t bodyID, const Vector3<float> *force, int32_t activation);
    void VKE_INTEROP_CDECL AddBodyForceAtPoint(uint32_t bodyID, const Vector3<float> *force, const Vector3<float> *point, int32_t activation);
    void VKE_INTEROP_CDECL AddBodyTorque(uint32_t bodyID, const Vector3<float> *torque, int32_t activation);
    void VKE_INTEROP_CDECL AddBodyForceAndTorque(uint32_t bodyID, const Vector3<float> *force, const Vector3<float> *torque, int32_t activation);
    void VKE_INTEROP_CDECL AddBodyImpulse(uint32_t bodyID, const Vector3<float> *impulse);
    void VKE_INTEROP_CDECL AddBodyImpulseAtPoint(uint32_t bodyID, const Vector3<float> *impulse, const Vector3<float> *point);
    void VKE_INTEROP_CDECL AddBodyAngularImpulse(uint32_t bodyID, const Vector3<float> *angularImpulse);
    int32_t VKE_INTEROP_CDECL ApplyBodyBuoyancyImpulse(uint32_t bodyID, const Vector3<float> *surfacePosition, const Vector3<float> *surfaceNormal, float buoyancy, float linearDrag, float angularDrag, const Vector3<float> *fluidVelocity, const Vector3<float> *gravity, float deltaTime);
    int32_t VKE_INTEROP_CDECL SetBodyMotionType(uint32_t bodyID, int32_t motionType, int32_t activation);
    int32_t VKE_INTEROP_CDECL GetBodyMotionType(uint32_t bodyID);
    void VKE_INTEROP_CDECL SetBodyMotionQuality(uint32_t bodyID, int32_t motionQuality);
    int32_t VKE_INTEROP_CDECL GetBodyMotionQuality(uint32_t bodyID);
    uint32_t VKE_INTEROP_CDECL GetPhysicsObjectLayerCount();
    uint32_t VKE_INTEROP_CDECL GetPhysicsBroadPhaseLayerCount();
    uint32_t VKE_INTEROP_CDECL GetPhysicsObjectLayerBroadPhaseLayer(uint32_t objectLayer);
    void VKE_INTEROP_CDECL SetPhysicsObjectLayerCollision(uint32_t layer1, uint32_t layer2, int32_t shouldCollide);
    int32_t VKE_INTEROP_CDECL GetPhysicsObjectLayerCollision(uint32_t layer1, uint32_t layer2);
    void VKE_INTEROP_CDECL SetPhysicsObjectVsBroadPhaseLayerCollision(uint32_t objectLayer, uint32_t broadPhaseLayer, int32_t shouldCollide);
    int32_t VKE_INTEROP_CDECL GetPhysicsObjectVsBroadPhaseLayerCollision(uint32_t objectLayer, uint32_t broadPhaseLayer);
    uint32_t VKE_INTEROP_CDECL GetPhysicsContactEventCount();
    uint32_t VKE_INTEROP_CDECL GetPhysicsContactEvents(ContactEvent *events, uint32_t maxEvents);
    int32_t VKE_INTEROP_CDECL GetMouseRay(Vector3<float> *origin, Vector3<float> *direction);
    int32_t VKE_INTEROP_CDECL PhysicsRaycast(const Vector3<float> *origin, const Vector3<float> *direction, float maxDistance, uint32_t broadPhaseLayerMask, uint32_t objectLayerMask, RaycastHit *hit);
    uint32_t VKE_INTEROP_CDECL PhysicsRaycastAll(const Vector3<float> *origin, const Vector3<float> *direction, float maxDistance, uint32_t broadPhaseLayerMask, uint32_t objectLayerMask, RaycastHit *hits, uint32_t maxHits);
    uint32_t VKE_INTEROP_CDECL PhysicsCollidePoint(const Vector3<float> *point, uint32_t broadPhaseLayerMask, uint32_t objectLayerMask, CollidePointHit *hits, uint32_t maxHits);
    uint32_t VKE_INTEROP_CDECL PhysicsCollideShape(int32_t shapeType, const void *shapeParams, const Vector3<float> *position, const Quaternion<float> *rotation, const Vector3<float> *scale, uint32_t broadPhaseLayerMask, uint32_t objectLayerMask, CollideShapeHit *hits, uint32_t maxHits);
    uint32_t VKE_INTEROP_CDECL PhysicsCastShape(int32_t shapeType, const void *shapeParams, const Vector3<float> *position, const Quaternion<float> *rotation, const Vector3<float> *scale, const Vector3<float> *direction, float maxDistance, uint32_t broadPhaseLayerMask, uint32_t objectLayerMask, ShapeCastHit *hits, uint32_t maxHits);
}

#endif
