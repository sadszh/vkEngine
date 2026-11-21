#ifndef COMPONENT_H
#define COMPONENT_H

#include <cstdint>

namespace vke_common
{
    enum class ComponentType : int32_t
    {
        Transform = 0,
        Camera = 1,
        RenderableObject = 2,
        SkeletonAnimator = 3,
        RigidBody = 4,
        Sensor = 5,
        CharacterController = 6,
        DirectionalLight = 7,
        PointLight = 8,
        SpotLight = 9,
        Script = 10,
        AudioSource = 12,
        AudioListener = 13,
        Canvas = 14,
        NavMeshRegion = 15,
        NavMeshTile = 16,
        NavMeshAgent = 17
    };
}

#endif
