using System;

namespace vkEngine.EngineCore
{
    public unsafe sealed class SkeletonAnimator
    {
        private readonly UInt32 entity;

        private static delegate* unmanaged[Cdecl]<UInt32, UInt32> getAnimationCount;
        private static delegate* unmanaged[Cdecl]<UInt32, UInt32, float, void> setAnimationSpeed;
        private static delegate* unmanaged[Cdecl]<UInt32, UInt32, float> getAnimationSpeed;
        private static delegate* unmanaged[Cdecl]<UInt32, UInt32, float, void> setAnimationTimeRatio;
        private static delegate* unmanaged[Cdecl]<UInt32, UInt32, float> getAnimationTimeRatio;
        private static delegate* unmanaged[Cdecl]<UInt32, UInt32, Int32, void> setAnimationLoop;
        private static delegate* unmanaged[Cdecl]<UInt32, UInt32, Int32> getAnimationLoop;
        private static delegate* unmanaged[Cdecl]<UInt32, UInt32, Int32, void> setAnimationPlaying;
        private static delegate* unmanaged[Cdecl]<UInt32, UInt32, Int32> getAnimationPlaying;
        private static delegate* unmanaged[Cdecl]<UInt32, UInt32, float, void> setAnimationWeight;
        private static delegate* unmanaged[Cdecl]<UInt32, UInt32, float> getAnimationWeight;
        private static delegate* unmanaged[Cdecl]<UInt32, float*, UInt32, void> setBlendWeights;
        private static delegate* unmanaged[Cdecl]<UInt32, NVec3*, void> getRootMotionDeltaPosition;
        private static delegate* unmanaged[Cdecl]<UInt32, NQuat*, void> getRootMotionDeltaRotation;

        public SkeletonAnimator(UInt32 entity)
        {
            this.entity = entity;
        }

        public UInt32 Entity => entity;

        public UInt32 AnimationCount => getAnimationCount(entity);

        public NVec3 RootMotionDeltaPosition
        {
            get
            {
                NVec3 value = default;
                getRootMotionDeltaPosition(entity, &value);
                return value;
            }
        }

        public NQuat RootMotionDeltaRotation
        {
            get
            {
                NQuat value = default;
                getRootMotionDeltaRotation(entity, &value);
                return value;
            }
        }

        internal static void RegisterNativeFunctions(NativeFunctions* functions)
        {
            getAnimationCount = functions->GetSkeletonAnimatorAnimationCount;
            setAnimationSpeed = functions->SetSkeletonAnimatorAnimationSpeed;
            getAnimationSpeed = functions->GetSkeletonAnimatorAnimationSpeed;
            setAnimationTimeRatio = functions->SetSkeletonAnimatorAnimationTimeRatio;
            getAnimationTimeRatio = functions->GetSkeletonAnimatorAnimationTimeRatio;
            setAnimationLoop = functions->SetSkeletonAnimatorAnimationLoop;
            getAnimationLoop = functions->GetSkeletonAnimatorAnimationLoop;
            setAnimationPlaying = functions->SetSkeletonAnimatorAnimationPlaying;
            getAnimationPlaying = functions->GetSkeletonAnimatorAnimationPlaying;
            setAnimationWeight = functions->SetSkeletonAnimatorAnimationWeight;
            getAnimationWeight = functions->GetSkeletonAnimatorAnimationWeight;
            setBlendWeights = functions->SetSkeletonAnimatorBlendWeights;
            getRootMotionDeltaPosition = functions->GetSkeletonAnimatorRootMotionDeltaPosition;
            getRootMotionDeltaRotation = functions->GetSkeletonAnimatorRootMotionDeltaRotation;
        }

        public void SetAnimationSpeed(UInt32 index, float speed)
        {
            ValidateIndex(index);
            setAnimationSpeed(entity, index, speed);
        }

        public float GetAnimationSpeed(UInt32 index)
        {
            ValidateIndex(index);
            return getAnimationSpeed(entity, index);
        }

        public void SetAnimationTimeRatio(UInt32 index, float ratio)
        {
            ValidateIndex(index);
            setAnimationTimeRatio(entity, index, ratio);
        }

        public float GetAnimationTimeRatio(UInt32 index)
        {
            ValidateIndex(index);
            return getAnimationTimeRatio(entity, index);
        }

        public void SetAnimationLoop(UInt32 index, bool loop)
        {
            ValidateIndex(index);
            setAnimationLoop(entity, index, loop ? 1 : 0);
        }

        public bool GetAnimationLoop(UInt32 index)
        {
            ValidateIndex(index);
            return getAnimationLoop(entity, index) != 0;
        }

        public void SetAnimationPlaying(UInt32 index, bool playing)
        {
            ValidateIndex(index);
            setAnimationPlaying(entity, index, playing ? 1 : 0);
        }

        public bool GetAnimationPlaying(UInt32 index)
        {
            ValidateIndex(index);
            return getAnimationPlaying(entity, index) != 0;
        }

        public void SetAnimationWeight(UInt32 index, float weight)
        {
            ValidateIndex(index);
            setAnimationWeight(entity, index, weight);
        }

        public float GetAnimationWeight(UInt32 index)
        {
            ValidateIndex(index);
            return getAnimationWeight(entity, index);
        }

        public void SetBlendWeights(ReadOnlySpan<float> weights)
        {
            if (weights.Length > AnimationCount)
                throw new ArgumentOutOfRangeException(nameof(weights), "Blend weight count cannot exceed AnimationCount.");

            fixed (float* weightPtr = weights)
            {
                setBlendWeights(entity, weightPtr, (UInt32)weights.Length);
            }
        }

        private void ValidateIndex(UInt32 index)
        {
            if (index >= AnimationCount)
                throw new ArgumentOutOfRangeException(nameof(index));
        }
    }
}
