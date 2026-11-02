#include <interop/entity_access.hpp>
#include <script.hpp>
#include <component/transform.hpp>
#include <component/skeleton_animator.hpp>
#include <component/character_controller.hpp>
#include <input.hpp>
#include <time.hpp>
#include <engine_state.hpp>
#include <interop/math.hpp>
#include <interop/native.hpp>
#include <logger.hpp>
#include <scene.hpp>

namespace vke_interop
{
    static inline entt::entity GetEntity(uint32_t entity)
    {
        entt::entity ent = static_cast<entt::entity>(entity);
        return ent;
    }

    static vke_common::Transform &GetTransform(uint32_t entity)
    {
        return vke_common::SceneManager::GetInstance()->registry.get<vke_common::Transform>(GetEntity(entity));
    }

    static glm::vec3 ToGlm(const Vector3<float> &value)
    {
        return glm::vec3(value.x, value.y, value.z);
    }

    static glm::quat ToGlm(const Quaternion<float> &value)
    {
        return glm::normalize(glm::quat(value.w, value.x, value.y, value.z));
    }

    static Vector3<float> ToInterop(const glm::vec3 &value)
    {
        return Vector3<float>{value.x, value.y, value.z};
    }

    static Vector2<float> ToInterop(const glm::vec2 &value)
    {
        return Vector2<float>{value.x, value.y};
    }

    static Quaternion<float> ToInterop(const glm::quat &value)
    {
        return Quaternion<float>{value.x, value.y, value.z, value.w};
    }

    static void VKE_INTEROP_CDECL GetTransformLocalPosition(uint32_t entity, Vector3<float> *position)
    {
        if (!IsEntityValid(entity))
        {
            if (position)
                *position = {};
            return;
        }

        *position = ToInterop(GetTransform(entity).localPosition);
    }

    static void VKE_INTEROP_CDECL SetTransformLocalPosition(uint32_t entity, const Vector3<float> *position)
    {
        if (!IsEntityValid(entity))
        {
            return;
        }

        vke_common::SceneManager::GetInstance()->transformSystem.SetLocalPosition(GetEntity(entity), ToGlm(*position));
    }

    static void VKE_INTEROP_CDECL GetTransformLocalRotation(uint32_t entity, Quaternion<float> *rotation)
    {
        if (!IsEntityValid(entity))
        {
            if (rotation)
                *rotation = {};
            return;
        }

        *rotation = ToInterop(GetTransform(entity).localRotation);
    }

    static void VKE_INTEROP_CDECL SetTransformLocalRotation(uint32_t entity, const Quaternion<float> *rotation)
    {
        if (!IsEntityValid(entity))
        {
            return;
        }

        vke_common::SceneManager::GetInstance()->transformSystem.SetLocalRotation(GetEntity(entity), ToGlm(*rotation));
    }

    static void VKE_INTEROP_CDECL GetTransformLocalScale(uint32_t entity, Vector3<float> *scale)
    {
        if (!IsEntityValid(entity))
        {
            if (scale)
                *scale = {};
            return;
        }

        *scale = ToInterop(GetTransform(entity).localScale);
    }

    static void VKE_INTEROP_CDECL SetTransformLocalScale(uint32_t entity, const Vector3<float> *scale)
    {
        if (!IsEntityValid(entity))
        {
            return;
        }

        vke_common::SceneManager::GetInstance()->transformSystem.SetLocalScale(GetEntity(entity), ToGlm(*scale));
    }

    static void VKE_INTEROP_CDECL TranslateTransformLocal(uint32_t entity, const Vector3<float> *det)
    {
        if (!IsEntityValid(entity))
        {
            return;
        }

        vke_common::SceneManager::GetInstance()->transformSystem.TranslateLocal(GetEntity(entity), ToGlm(*det));
    }

    static void VKE_INTEROP_CDECL TranslateTransformGlobal(uint32_t entity, const Vector3<float> *det)
    {
        if (!IsEntityValid(entity))
        {
            return;
        }

        vke_common::SceneManager::GetInstance()->transformSystem.TranslateGlobal(GetEntity(entity), ToGlm(*det));
    }

    static void VKE_INTEROP_CDECL RotateTransformLocal(uint32_t entity, float det, const Vector3<float> *axis)
    {
        if (!IsEntityValid(entity))
        {
            return;
        }

        vke_common::SceneManager::GetInstance()->transformSystem.RotateLocal(GetEntity(entity), det, ToGlm(*axis));
    }

    static void VKE_INTEROP_CDECL RotateTransformGlobal(uint32_t entity, float det, const Vector3<float> *axis)
    {
        if (!IsEntityValid(entity))
        {
            return;
        }

        vke_common::SceneManager::GetInstance()->transformSystem.RotateGlobal(GetEntity(entity), det, ToGlm(*axis));
    }

    static void VKE_INTEROP_CDECL ScaleTransform(uint32_t entity, const Vector3<float> *scale)
    {
        if (!IsEntityValid(entity))
        {
            return;
        }

        vke_common::SceneManager::GetInstance()->transformSystem.Scale(GetEntity(entity), ToGlm(*scale));
    }

    static int32_t VKE_INTEROP_CDECL IsKeyDown(int32_t key)
    {
        return vke_common::InputManager::IsKeyDown(key) ? 1 : 0;
    }

    static int32_t VKE_INTEROP_CDECL IsKeyPressed(int32_t key)
    {
        return vke_common::InputManager::IsKeyPressed(key) ? 1 : 0;
    }

    static int32_t VKE_INTEROP_CDECL IsKeyReleased(int32_t key)
    {
        return vke_common::InputManager::IsKeyReleased(key) ? 1 : 0;
    }

    static int32_t VKE_INTEROP_CDECL IsMouseButtonDown(int32_t button)
    {
        return vke_common::InputManager::IsMouseButtonDown(button) ? 1 : 0;
    }

    static int32_t VKE_INTEROP_CDECL IsMouseButtonPressed(int32_t button)
    {
        return vke_common::InputManager::IsMouseButtonPressed(button) ? 1 : 0;
    }

    static int32_t VKE_INTEROP_CDECL IsMouseButtonReleased(int32_t button)
    {
        return vke_common::InputManager::IsMouseButtonReleased(button) ? 1 : 0;
    }

    static void VKE_INTEROP_CDECL GetMousePosition(Vector2<float> *position)
    {
        *position = ToInterop(vke_common::InputManager::GetMousePosition());
    }

    static void VKE_INTEROP_CDECL GetMouseDelta(Vector2<float> *delta)
    {
        *delta = ToInterop(vke_common::InputManager::GetMouseDelta());
    }

    static void VKE_INTEROP_CDECL GetMouseScrollDelta(Vector2<float> *delta)
    {
        *delta = ToInterop(vke_common::InputManager::GetScrollDelta());
    }

    static void VKE_INTEROP_CDECL SetCursorMode(int32_t mode)
    {
        vke_common::InputManager::SetCursorMode(mode);
    }

    static int32_t VKE_INTEROP_CDECL GetCursorMode()
    {
        return vke_common::InputManager::GetCursorMode();
    }

    static float VKE_INTEROP_CDECL GetTime()
    {
        return vke_common::TimeManager::GetTime();
    }

    static float VKE_INTEROP_CDECL GetDeltaTime()
    {
        return vke_common::TimeManager::GetDeltaTime();
    }

    static float VKE_INTEROP_CDECL GetPreviousFrameTime()
    {
        return vke_common::TimeManager::GetPreviousFrameTime();
    }

    static void VKE_INTEROP_CDECL SetEngineState(int32_t state)
    {
        vke_common::EngineStateManager::SetState(static_cast<vke_common::EngineState>(state));
    }

    static int32_t VKE_INTEROP_CDECL HasComponent(uint32_t entity, int32_t componentType)
    {
        return vke_common::SceneManager::GetInstance()->HasComponent(
                   GetEntity(entity), static_cast<vke_common::ComponentType>(componentType))
                   ? 1
                   : 0;
    }

    static vke_component::SkeletonAnimator *GetSkeletonAnimator(uint32_t entity)
    {
        auto *scene = vke_common::SceneManager::GetInstance();
        entt::entity ent = GetEntity(entity);
        if (!scene->registry.valid(ent) || !scene->registry.all_of<vke_component::SkeletonAnimator>(ent))
            return nullptr;

        return &scene->registry.get<vke_component::SkeletonAnimator>(ent);
    }

    static uint32_t VKE_INTEROP_CDECL GetSkeletonAnimatorAnimationCount(uint32_t entity)
    {
        vke_component::SkeletonAnimator *animator = GetSkeletonAnimator(entity);
        return animator == nullptr ? 0 : animator->GetAnimationCount();
    }

    static void VKE_INTEROP_CDECL SetSkeletonAnimatorAnimationSpeed(uint32_t entity, uint32_t index, float speed)
    {
        vke_component::SkeletonAnimator *animator = GetSkeletonAnimator(entity);
        if (animator != nullptr)
            animator->SetAnimationSpeed(index, speed);
    }

    static float VKE_INTEROP_CDECL GetSkeletonAnimatorAnimationSpeed(uint32_t entity, uint32_t index)
    {
        vke_component::SkeletonAnimator *animator = GetSkeletonAnimator(entity);
        return animator == nullptr ? 0.0f : animator->GetAnimationSpeed(index);
    }

    static void VKE_INTEROP_CDECL SetSkeletonAnimatorAnimationTimeRatio(uint32_t entity, uint32_t index, float ratio)
    {
        vke_component::SkeletonAnimator *animator = GetSkeletonAnimator(entity);
        if (animator != nullptr)
            animator->SetAnimationTimeRatio(index, ratio);
    }

    static float VKE_INTEROP_CDECL GetSkeletonAnimatorAnimationTimeRatio(uint32_t entity, uint32_t index)
    {
        vke_component::SkeletonAnimator *animator = GetSkeletonAnimator(entity);
        return animator == nullptr ? 0.0f : animator->GetAnimationTimeRatio(index);
    }

    static void VKE_INTEROP_CDECL SetSkeletonAnimatorAnimationLoop(uint32_t entity, uint32_t index, int32_t loop)
    {
        vke_component::SkeletonAnimator *animator = GetSkeletonAnimator(entity);
        if (animator != nullptr)
            animator->SetAnimationLoop(index, loop != 0);
    }

    static int32_t VKE_INTEROP_CDECL GetSkeletonAnimatorAnimationLoop(uint32_t entity, uint32_t index)
    {
        vke_component::SkeletonAnimator *animator = GetSkeletonAnimator(entity);
        return animator != nullptr && animator->GetAnimationLoop(index) ? 1 : 0;
    }

    static void VKE_INTEROP_CDECL SetSkeletonAnimatorAnimationPlaying(uint32_t entity, uint32_t index, int32_t playing)
    {
        vke_component::SkeletonAnimator *animator = GetSkeletonAnimator(entity);
        if (animator != nullptr)
            animator->SetAnimationPlaying(index, playing != 0);
    }

    static int32_t VKE_INTEROP_CDECL GetSkeletonAnimatorAnimationPlaying(uint32_t entity, uint32_t index)
    {
        vke_component::SkeletonAnimator *animator = GetSkeletonAnimator(entity);
        return animator != nullptr && animator->GetAnimationPlaying(index) ? 1 : 0;
    }

    static void VKE_INTEROP_CDECL SetSkeletonAnimatorAnimationWeight(uint32_t entity, uint32_t index, float weight)
    {
        vke_component::SkeletonAnimator *animator = GetSkeletonAnimator(entity);
        if (animator != nullptr)
            animator->SetAnimationWeight(index, weight);
    }

    static float VKE_INTEROP_CDECL GetSkeletonAnimatorAnimationWeight(uint32_t entity, uint32_t index)
    {
        vke_component::SkeletonAnimator *animator = GetSkeletonAnimator(entity);
        return animator == nullptr ? 0.0f : animator->GetAnimationWeight(index);
    }

    static void VKE_INTEROP_CDECL SetSkeletonAnimatorBlendWeights(uint32_t entity, const float *weights, uint32_t count)
    {
        vke_component::SkeletonAnimator *animator = GetSkeletonAnimator(entity);
        if (animator != nullptr)
            animator->SetBlendWeights(weights, count);
    }

    static vke_component::CharacterController *GetCharacterController(uint32_t entity)
    {
        auto *scene = vke_common::SceneManager::GetInstance();
        entt::entity ent = GetEntity(entity);
        if (!scene->registry.valid(ent) || !scene->registry.all_of<vke_component::CharacterController>(ent))
            return nullptr;

        return &scene->registry.get<vke_component::CharacterController>(ent);
    }

    static void VKE_INTEROP_CDECL SetCharacterControllerVelocity(uint32_t entity, const Vector3<float> *velocity)
    {
        vke_component::CharacterController *controller = GetCharacterController(entity);
        if (controller == nullptr)
            return;

        controller->SetDesiredVelocity(ToGlm(*velocity));
    }

    static void VKE_INTEROP_CDECL GetCharacterControllerVelocity(uint32_t entity, Vector3<float> *velocity)
    {
        vke_component::CharacterController *controller = GetCharacterController(entity);
        *velocity = controller == nullptr ? Vector3<float>{0.0f, 0.0f, 0.0f} : ToInterop(controller->GetLinearVelocity());
    }

    static int32_t VKE_INTEROP_CDECL IsCharacterControllerGrounded(uint32_t entity)
    {
        vke_component::CharacterController *controller = GetCharacterController(entity);
        return controller != nullptr && controller->IsGrounded() ? 1 : 0;
    }
    static int32_t VKE_INTEROP_CDECL InstantiatePrefab(uint64_t prefab,
        const Vector3<float> *position, const Quaternion<float> *rotation,
        const Vector3<float> *scale, uint32_t parent)
    {
        if (!position || !rotation || !scale) return 0;
        vke_common::InstantiateOptions options;
        options.parent = static_cast<entt::entity>(parent);
        options.rootTransform = vke_common::TransformData{};
        options.rootTransform->localPosition = ToGlm(*position);
        options.rootTransform->localRotation = ToGlm(*rotation);
        options.rootTransform->localScale = ToGlm(*scale);
        auto result = vke_common::SceneManager::RequestInstantiate(prefab, options);
        if (!result) VKE_LOG_ERROR("Prefab request rejected: {}", result.error());
        return result ? 1 : 0;
    }

    static void VKE_INTEROP_CDECL DestroyEntity(uint32_t entity)
    {
        vke_common::SceneManager::GetInstance()->DestroyEntity(static_cast<entt::entity>(entity));
    }

    static int32_t VKE_INTEROP_CDECL IsEntityPendingDestroy(uint32_t entity)
    {
        return vke_common::SceneManager::GetInstance()->IsPendingDestroy(static_cast<entt::entity>(entity)) ? 1 : 0;
    }

}

namespace vke_common
{

    void ScriptManager::registerNativeFunctions()
    {
        vke_interop::NativeFunctions nativeFunctions{
            &vke_interop::GetTransformLocalPosition,
            &vke_interop::SetTransformLocalPosition,
            &vke_interop::GetTransformLocalRotation,
            &vke_interop::SetTransformLocalRotation,
            &vke_interop::GetTransformLocalScale,
            &vke_interop::SetTransformLocalScale,
            &vke_interop::TranslateTransformLocal,
            &vke_interop::TranslateTransformGlobal,
            &vke_interop::RotateTransformLocal,
            &vke_interop::RotateTransformGlobal,
            &vke_interop::ScaleTransform,
            &vke_interop::IsKeyDown,
            &vke_interop::IsKeyPressed,
            &vke_interop::IsKeyReleased,
            &vke_interop::IsMouseButtonDown,
            &vke_interop::IsMouseButtonPressed,
            &vke_interop::IsMouseButtonReleased,
            &vke_interop::GetMousePosition,
            &vke_interop::GetMouseDelta,
            &vke_interop::GetMouseScrollDelta,
            &vke_interop::SetCursorMode,
            &vke_interop::GetCursorMode,
            &vke_interop::GetTime,
            &vke_interop::GetDeltaTime,
            &vke_interop::GetPreviousFrameTime,
            &vke_interop::SetEngineState,
            &vke_interop::HasComponent,
            &vke_interop::GetSkeletonAnimatorAnimationCount,
            &vke_interop::SetSkeletonAnimatorAnimationSpeed,
            &vke_interop::GetSkeletonAnimatorAnimationSpeed,
            &vke_interop::SetSkeletonAnimatorAnimationTimeRatio,
            &vke_interop::GetSkeletonAnimatorAnimationTimeRatio,
            &vke_interop::SetSkeletonAnimatorAnimationLoop,
            &vke_interop::GetSkeletonAnimatorAnimationLoop,
            &vke_interop::SetSkeletonAnimatorAnimationPlaying,
            &vke_interop::GetSkeletonAnimatorAnimationPlaying,
            &vke_interop::SetSkeletonAnimatorAnimationWeight,
            &vke_interop::GetSkeletonAnimatorAnimationWeight,
            &vke_interop::SetSkeletonAnimatorBlendWeights,
            &vke_interop::GetDirectionalLightColor,
            &vke_interop::SetDirectionalLightColor,
            &vke_interop::GetDirectionalLightIntensity,
            &vke_interop::SetDirectionalLightIntensity,
            &vke_interop::GetPointLightColor,
            &vke_interop::SetPointLightColor,
            &vke_interop::GetPointLightIntensity,
            &vke_interop::SetPointLightIntensity,
            &vke_interop::GetPointLightRadius,
            &vke_interop::SetPointLightRadius,
            &vke_interop::GetSpotLightColor,
            &vke_interop::SetSpotLightColor,
            &vke_interop::GetSpotLightIntensity,
            &vke_interop::SetSpotLightIntensity,
            &vke_interop::GetSpotLightRadius,
            &vke_interop::SetSpotLightRadius,
            &vke_interop::GetSpotLightInnerCone,
            &vke_interop::SetSpotLightInnerCone,
            &vke_interop::GetSpotLightOuterCone,
            &vke_interop::SetSpotLightOuterCone,
            &vke_interop::SetCharacterControllerVelocity,
            &vke_interop::GetCharacterControllerVelocity,
            &vke_interop::IsCharacterControllerGrounded,
            &vke_interop::GetRigidBodyBodyID,
            &vke_interop::GetSensorBodyID,
            &vke_interop::ActivateBody,
            &vke_interop::DeactivateBody,
            &vke_interop::IsBodyActive,
            &vke_interop::SetBodyObjectLayer,
            &vke_interop::GetBodyObjectLayer,
            &vke_interop::SetBodyRestitution,
            &vke_interop::GetBodyRestitution,
            &vke_interop::SetBodyFriction,
            &vke_interop::GetBodyFriction,
            &vke_interop::SetBodyGravityFactor,
            &vke_interop::GetBodyGravityFactor,
            &vke_interop::GetBodyCenterOfMassPosition,
            &vke_interop::MoveKinematicBody,
            &vke_interop::SetBodyLinearAndAngularVelocity,
            &vke_interop::GetBodyLinearAndAngularVelocity,
            &vke_interop::SetBodyLinearVelocity,
            &vke_interop::GetBodyLinearVelocity,
            &vke_interop::AddBodyLinearVelocity,
            &vke_interop::AddBodyLinearAndAngularVelocity,
            &vke_interop::SetBodyAngularVelocity,
            &vke_interop::GetBodyAngularVelocity,
            &vke_interop::GetBodyPointVelocity,
            &vke_interop::AddBodyForce,
            &vke_interop::AddBodyForceAtPoint,
            &vke_interop::AddBodyTorque,
            &vke_interop::AddBodyForceAndTorque,
            &vke_interop::AddBodyImpulse,
            &vke_interop::AddBodyImpulseAtPoint,
            &vke_interop::AddBodyAngularImpulse,
            &vke_interop::ApplyBodyBuoyancyImpulse,
            &vke_interop::SetBodyMotionType,
            &vke_interop::GetBodyMotionType,
            &vke_interop::SetBodyMotionQuality,
            &vke_interop::GetBodyMotionQuality,
            &vke_interop::GetPhysicsObjectLayerCount,
            &vke_interop::GetPhysicsBroadPhaseLayerCount,
            &vke_interop::GetPhysicsObjectLayerBroadPhaseLayer,
            &vke_interop::SetPhysicsObjectLayerCollision,
            &vke_interop::GetPhysicsObjectLayerCollision,
            &vke_interop::SetPhysicsObjectVsBroadPhaseLayerCollision,
            &vke_interop::GetPhysicsObjectVsBroadPhaseLayerCollision,
            &vke_interop::GetPhysicsContactEventCount,
            &vke_interop::GetPhysicsContactEvents,
            &vke_interop::PhysicsRaycast,
            &vke_interop::PhysicsRaycastAll,
            &vke_interop::PhysicsCollidePoint,
            &vke_interop::PhysicsCollideShape,
            &vke_interop::PhysicsCastShape,
            &vke_interop::AudioSourcePlay,
            &vke_interop::AudioSourceReplay,
            &vke_interop::AudioSourceStop,
            &vke_interop::AudioSourcePause,
            &vke_interop::AudioSourceGetIsPlaying,
            &vke_interop::AudioSourceSetLooping,
            &vke_interop::AudioSourceGetLooping,
            &vke_interop::AudioSourceSetVolume,
            &vke_interop::AudioSourceGetVolume,
            &vke_interop::AudioSourceSetPitch,
            &vke_interop::AudioSourceGetPitch,
            &vke_interop::AudioSourceSetTime,
            &vke_interop::AudioSourceGetTime,
            &vke_interop::AudioSourceSetSpatializationEnabled,
            &vke_interop::AudioSourceGetSpatializationEnabled,
            &vke_interop::AudioSourceSetAttenuationModel,
            &vke_interop::AudioSourceGetAttenuationModel,
            &vke_interop::AudioSourceSetRolloff,
            &vke_interop::AudioSourceGetRolloff,
            &vke_interop::AudioSourceSetMinDistance,
            &vke_interop::AudioSourceGetMinDistance,
            &vke_interop::AudioSourceSetMaxDistance,
            &vke_interop::AudioSourceGetMaxDistance,
            &vke_interop::AudioSourceSetDopplerFactor,
            &vke_interop::AudioSourceGetDopplerFactor,
            &vke_interop::AudioListenerSetEnabled,
            &vke_interop::AudioListenerGetEnabled,
            &vke_interop::DestroyEntity,
            &vke_interop::IsEntityPendingDestroy,
            &vke_interop::InstantiatePrefab,
            &vke_interop::GetUIType,
            &vke_interop::FindUIChild,
            &vke_interop::GetUIChildCount,
            &vke_interop::GetUIChild,
            &vke_interop::CreateUINode,
            &vke_interop::RemoveUINode,
            &vke_interop::SetUIParent,
            &vke_interop::SetUISiblingIndex,
            &vke_interop::GetUIRectTransform,
            &vke_interop::SetUIRectTransform,
            &vke_interop::GetUIResolvedSize,
            &vke_interop::GetCanvasReferenceResolution,
            &vke_interop::SetCanvasReferenceResolution,
            &vke_interop::GetCanvasSortOrder,
            &vke_interop::SetCanvasSortOrder,
            &vke_interop::GetUITextLength,
            &vke_interop::GetUITextText,
            &vke_interop::SetUITextText,
            &vke_interop::GetUITextColor,
            &vke_interop::SetUITextColor,
            &vke_interop::GetUIImageTexture,
            &vke_interop::SetUIImageTexture,
            &vke_interop::GetUIImageColor,
            &vke_interop::SetUIImageColor,
            &vke_interop::GetUIImageUVRect,
            &vke_interop::SetUIImageUVRect,
            &vke_interop::GetUIImagePreserveAspect,
            &vke_interop::SetUIImagePreserveAspect,
            &vke_interop::GetUIInteraction,
            &vke_interop::SetUIInteraction,
            &vke_interop::GetUIButtonColor,
            &vke_interop::SetUIButtonColor,
            &vke_interop::GetUITextLayoutOptions,
            &vke_interop::SetUITextLayoutOptions,
            &vke_interop::MeasureUIText,
            &vke_interop::GetUITextSize,
            &vke_interop::GetUISelfLayout,
            &vke_interop::SetUISelfLayout,
            &vke_interop::GetUIGroupLayout,
            &vke_interop::SetUIGroupLayout};
        csharpExports.registerNativeFunctions(&nativeFunctions);
    }
}
