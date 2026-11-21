#include <component/navmesh_agent.hpp>
#include <scene_transform_system.hpp>
#include <component/camera.hpp>
#include <component/renderable_object.hpp>
#include <component/skeleton_animator.hpp>
#include <component/rigidbody.hpp>
#include <component/sensor.hpp>
#include <component/character_controller.hpp>
#include <render/render.hpp>
#include <vector>

namespace vke_common
{
    static inline glm::vec3 TransformForward(const Transform &transform)
    {
        return transform.GetGlobalRotation() * glm::vec3(0.0f, 0.0f, -1.0f);
    }

    void SceneTransformSystem::CollectEntitySubtree(std::vector<entt::entity> &entities) const
    {
        size_t current = 0;
        while (current < entities.size())
        {
            const Transform &transform = registry.get<Transform>(entities[current++]);
            entities.insert(entities.end(), transform.children.begin(), transform.children.end());
        }
    }

    void SceneTransformSystem::CollectEntitiesSubtree(std::unordered_set<entt::entity> &entitySet, std::vector<entt::entity> &entities) const
    {
        size_t current = 0;
        while (current < entities.size())
        {
            const Transform &transform = registry.get<Transform>(entities[current++]);
            for (const entt::entity child : transform.children)
                if (entitySet.insert(child).second)
                    entities.push_back(child);
        }
    }

    void SceneTransformSystem::RemoveChild(entt::entity entity, entt::entity childEntity)
    {
        registry.get<Transform>(entity).children.erase(childEntity);
    }

    void SceneTransformSystem::SetParent(entt::entity entity, entt::entity parentEntity, bool updatePhysicsComponents)
    {
        Transform &transform = registry.get<Transform>(entity);

        if (transform.parent == parentEntity)
            return;

        if (transform.parent != entt::null)
        {
            RemoveChild(transform.parent, entity);
            transform.RemoveParent();
        }
        transform.parent = parentEntity;
        if (transform.parent == entt::null)
        {
            updateTransform(entity, transform, true, updatePhysicsComponents);
            return;
        }

        transform.SetParent(registry.get<Transform>(transform.parent));
        registry.get<Transform>(transform.parent).children.insert(entity);
        updateTransform(entity, transform, true, updatePhysicsComponents);
    }

    void SceneTransformSystem::SetLocalPosition(entt::entity entity, const glm::vec3 &position, bool updatePhysicsComponents)
    {
        Transform &transform = registry.get<Transform>(entity);
        transform.parent != entt::null ? transform.SetLocalPositionWithParent(registry.get<Transform>(transform.parent).model, position)
                                       : transform.SetLocalPosition(position);
        updateTransform(entity, transform, true, updatePhysicsComponents);
    }

    void SceneTransformSystem::SetGlobalPosition(entt::entity entity, const glm::vec3 &position, bool updatePhysicsComponents)
    {
        Transform &transform = registry.get<Transform>(entity);
        transform.parent != entt::null ? transform.SetGlobalPositionWithParent(registry.get<Transform>(transform.parent).model, position)
                                       : transform.SetGlobalPosition(position);
        updateTransform(entity, transform, true, updatePhysicsComponents);
    }

    void SceneTransformSystem::SetLocalRotation(entt::entity entity, const glm::quat &rotation, bool updatePhysicsComponents)
    {
        Transform &transform = registry.get<Transform>(entity);
        transform.parent != entt::null ? transform.SetLocalRotationWithParent(registry.get<Transform>(transform.parent), rotation)
                                       : transform.SetLocalRotation(rotation);
        updateTransform(entity, transform, true, updatePhysicsComponents);
    }

    void SceneTransformSystem::SetGlobalRotation(entt::entity entity, const glm::quat &rotation, bool updatePhysicsComponents)
    {
        Transform &transform = registry.get<Transform>(entity);
        transform.parent != entt::null ? transform.SetGlobalRotationWithParent(registry.get<Transform>(transform.parent), rotation)
                                       : transform.SetGlobalRotation(rotation);
        updateTransform(entity, transform, true, updatePhysicsComponents);
    }

    void SceneTransformSystem::SetLocalScale(entt::entity entity, const glm::vec3 &scale, bool updatePhysicsComponents)
    {
        Transform &transform = registry.get<Transform>(entity);
        transform.parent != entt::null ? transform.SetLocalScaleWithParent(registry.get<Transform>(transform.parent), scale)
                                       : transform.SetLocalScale(scale);
        updateTransform(entity, transform, true, updatePhysicsComponents);
    }

    void SceneTransformSystem::RotateGlobal(entt::entity entity, float det, const glm::vec3 &axis, bool updatePhysicsComponents)
    {
        Transform &transform = registry.get<Transform>(entity);
        transform.parent != entt::null ? transform.RotateGlobalWithParent(registry.get<Transform>(transform.parent), det, axis)
                                       : transform.RotateGlobal(det, axis);
        updateTransform(entity, transform, true, updatePhysicsComponents);
    }

    void SceneTransformSystem::RotateLocal(entt::entity entity, float det, const glm::vec3 &axis, bool updatePhysicsComponents)
    {
        Transform &transform = registry.get<Transform>(entity);
        transform.parent != entt::null ? transform.RotateLocalWithParent(registry.get<Transform>(transform.parent), det, axis)
                                       : transform.RotateLocal(det, axis);
        updateTransform(entity, transform, true, updatePhysicsComponents);
    }

    void SceneTransformSystem::TranslateLocal(entt::entity entity, const glm::vec3 &det, bool updatePhysicsComponents)
    {
        Transform &transform = registry.get<Transform>(entity);
        transform.parent != entt::null ? transform.TranslateLocalWithParent(registry.get<Transform>(transform.parent).model, det)
                                       : transform.TranslateLocal(det);
        updateTransform(entity, transform, true, updatePhysicsComponents);
    }

    void SceneTransformSystem::TranslateGlobal(entt::entity entity, const glm::vec3 &det, bool updatePhysicsComponents)
    {
        Transform &transform = registry.get<Transform>(entity);
        transform.parent != entt::null ? transform.TranslateGlobalWithParent(registry.get<Transform>(transform.parent).model, det)
                                       : transform.TranslateGlobal(det);
        updateTransform(entity, transform, true, updatePhysicsComponents);
    }

    void SceneTransformSystem::Scale(entt::entity entity, const glm::vec3 &scale, bool updatePhysicsComponents)
    {
        Transform &transform = registry.get<Transform>(entity);
        transform.parent != entt::null ? transform.ScaleWithParent(registry.get<Transform>(transform.parent), scale)
                                       : transform.Scale(scale);
        updateTransform(entity, transform, true, updatePhysicsComponents);
    }

    void SceneTransformSystem::updateTransform(entt::entity entity, Transform &transform, bool first, bool updatePhysicsComponents)
    {
        if (!first)
            transform.UpdateWithParent(registry.get<Transform>(transform.parent));

        if (registry.all_of<vke_component::Camera>(entity))
            registry.get<vke_component::Camera>(entity).OnTransformed(transform);

        if (auto *renderable = registry.try_get<vke_component::RenderableObject>(entity))
            renderable->OnTransformed(transform);

        if (auto *animator = registry.try_get<vke_component::SkeletonAnimator>(entity))
            animator->OnTransformed(transform);

        if (updatePhysicsComponents && registry.all_of<vke_component::RigidBody>(entity))
            registry.get<vke_component::RigidBody>(entity).OnTransformed(transform);

        if (updatePhysicsComponents && registry.all_of<vke_component::Sensor>(entity))
            registry.get<vke_component::Sensor>(entity).OnTransformed(transform);

        if (updatePhysicsComponents && registry.all_of<vke_component::CharacterController>(entity))
            registry.get<vke_component::CharacterController>(entity).OnTransformed(transform);

        if (auto *agent = registry.try_get<vke_component::NavMeshAgent>(entity))
            agent->OnTransformed(transform);

        auto *lightManager = vke_render::Renderer::GetInstance()->lightManager.get();

        if (lightManager->HasLight<vke_render::DirectionalLight>(entity))
        {
            auto &light = lightManager->GetLightWithoutCheckByEntity<vke_render::DirectionalLight>(entity);
            light.direction = glm::vec4(glm::normalize(TransformForward(transform)), 0.0f);
            lightManager->MarkDirty<vke_render::DirectionalLight>();
        }

        if (lightManager->HasLight<vke_render::PointLight>(entity))
        {
            auto &light = lightManager->GetLightWithoutCheckByEntity<vke_render::PointLight>(entity);
            light.positionWithRadius = glm::vec4(transform.GetGlobalPosition(), light.positionWithRadius.w);
            lightManager->MarkDirty<vke_render::PointLight>();
        }

        if (lightManager->HasLight<vke_render::SpotLight>(entity))
        {
            auto &light = lightManager->GetLightWithoutCheckByEntity<vke_render::SpotLight>(entity);
            light.positionWithRadius = glm::vec4(transform.GetGlobalPosition(), light.positionWithRadius.w);
            light.direction = glm::vec4(glm::normalize(TransformForward(transform)), 0.0f);
            lightManager->MarkDirty<vke_render::SpotLight>();
            if (light.CastShadow())
                lightManager->UpdateSpotShadow(entity);
        }

        for (auto &child : transform.children)
        {
            auto &childTransform = registry.get<Transform>(child);
            updateTransform(child, childTransform, false, updatePhysicsComponents);
        }
    }
}
