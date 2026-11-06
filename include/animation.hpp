#ifndef ANIMATION_H
#define ANIMATION_H

#include <common.hpp>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/track.h>
#include <ozz/animation/runtime/skeleton.h>

namespace vke_common
{

    class Skeleton
    {
    public:
        vke_common::AssetHandle handle;
        ozz::animation::Skeleton skeleton;

        Skeleton() = default;
        Skeleton(vke_common::AssetHandle hdl) : handle(hdl) {}
    };

    class Animation
    {
    public:
        vke_common::AssetHandle handle;
        ozz::animation::Animation animation;
        bool hasRootMotion;
        ozz::animation::Float3Track rootMotionPosition;
        ozz::animation::QuaternionTrack rootMotionRotation;
        // Track-space cycle transform M(1) * inverse(M(0)), computed on load.
        glm::vec3 rootMotionCyclePosition{0.0f};
        glm::quat rootMotionCycleRotation{1.0f, 0.0f, 0.0f, 0.0f};

        Animation() : handle(0), hasRootMotion(false) {}
        Animation(vke_common::AssetHandle hdl) : handle(hdl), hasRootMotion(false) {}

        float Duration() const
        {
            return animation.duration();
        }

        Result<void> ValidateSkeleton(const ozz::animation::Skeleton &skeleton) const
        {
            if (animation.num_tracks() != skeleton.num_joints())
                return std::unexpected("animation: track count must match skeleton joint count");
            if (!std::isfinite(Duration()) || Duration() <= 0.0f)
                return std::unexpected("animation: duration must be finite and positive");
            return {};
        }
    };
}

#endif
