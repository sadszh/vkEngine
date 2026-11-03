#ifndef SKELETON_ANIMATOR_H
#define SKELETON_ANIMATOR_H

#include <json_validation.hpp>
#include <algorithm>
#include <cmath>
#include <utility>
#include <span>
#include <component/transform.hpp>
#include <scene_transform_system.hpp>
#include <animation.hpp>
#include <asset/asset_manager.hpp>
#include <asset/asset_ref.hpp>
#include <render/render.hpp>
#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/track_sampling_job.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/maths/quaternion.h>
#include <ozz/base/maths/vec_float.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/containers/vector.h>

namespace vke_component
{

    const uint32_t MAX_BONE_PER_SKELETON = 256;

    struct SkeletonAnimationData
    {
        vke_common::AssetRef<vke_common::Animation> animation;
        float weight = 0.0f;
        float playbackSpeed = 1.0f;
        float timeRatio = 0.0f;
        bool playing = true;
        bool loop = true;

        // Call before constructing from JSON; asset checks happen in ValidateAssets.
        static vke_common::SceneResult<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            return Object(json).Require({"animation"}).Unsigneds({"animation"}).Numbers({"weight", "speed", "timeRatio"}).Booleans({"playing", "loop"}).Result();
        }

        SkeletonAnimationData() = default;
        SkeletonAnimationData(const nlohmann::json &json)
            : animation(json["animation"].get<vke_common::AssetHandle>()),
              weight(json.value("weight", 0.0f)), playbackSpeed(json.value("speed", 1.0f)),
              timeRatio(json.value("timeRatio", 0.0f)), playing(json.value("playing", true)),
              loop(json.value("loop", true)) {}
        vke_common::SceneResult<void> ValidateAssets() const
        {
            using vke_common::AssetManager;
            if (!animation.Get())
                if (auto result = AssetManager::ValidateAnimation(animation.Handle()); !result)
                    return std::unexpected("animation: " + result.error());
            return {};
        }

        // Requires successful ValidateAssets() before loading.
        vke_common::SceneResult<void> LoadAssets()
        {
            using vke_common::AssetManager;
            if (!animation.Get())
            {
                animation.Resolve(AssetManager::LoadAnimation(animation.Handle()));
                if (!animation.Get())
                    return std::unexpected("animation asset " + std::to_string(animation.Handle()) + ": loading failed");
            }
            return {};
        }

        nlohmann::json ToJSON() const
        {
            return {{"animation", animation.Handle()}, {"weight", weight}, {"speed", playbackSpeed}, {"timeRatio", timeRatio}, {"loop", loop}, {"playing", playing}};
        }
    };

    struct SkeletonAnimatorData
    {
        vke_common::AssetRef<vke_render::Material> material;
        vke_common::AssetRef<const vke_render::Mesh> mesh;
        vke_common::AssetRef<vke_common::Skeleton> skeleton;
        std::vector<SkeletonAnimationData> animations;
        bool castsShadow = true;

        // Call before constructing from JSON; asset checks happen in ValidateAssets.
        static vke_common::SceneResult<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            auto result = Object(json).Require({"material", "mesh", "skeleton"}).Unsigneds({"material", "mesh", "skeleton", "animation"}).Booleans({"castsShadow"}).Result();
            if (!result)
                return result;
            if (json.contains("animations"))
            {
                if (!json["animations"].is_array())
                    return std::unexpected("invalid animations");
                size_t index = 0;
                for (const auto &state : json["animations"])
                {
                    if (auto valid = SkeletonAnimationData::ValidateJSON(state); !valid)
                        return std::unexpected("animations[" + std::to_string(index) + "]: " + valid.error());
                    ++index;
                }
            }
            return {};
        }

        SkeletonAnimatorData() = default;
        SkeletonAnimatorData(const nlohmann::json &json)
            : material(json["material"].get<vke_common::AssetHandle>()),
              mesh(json["mesh"].get<vke_common::AssetHandle>()),
              skeleton(json["skeleton"].get<vke_common::AssetHandle>()),
              castsShadow(json.value("castsShadow", true))
        {
            if (json.contains("animations") && json["animations"].is_array())
                for (const nlohmann::json &state : json["animations"])
                    if (state.contains("animation"))
                    {
                        animations.emplace_back(state);
                        if (animations.size() == 1 && !state.contains("weight"))
                            animations.back().weight = 1.0f;
                    }
            if (!json.contains("animations") && json.value("animation", vke_common::AssetHandle{0}) != 0)
            {
                SkeletonAnimationData state;
                state.animation.SetHandle(json["animation"].get<vke_common::AssetHandle>());
                state.weight = 1.0f;
                animations.push_back(std::move(state));
            }
        }
        vke_common::SceneResult<void> ValidateAssets() const
        {
            using vke_common::AssetManager;
            if (!material.Get())
                if (auto result = AssetManager::ValidateMaterial(material.Handle()); !result)
                    return std::unexpected("material: " + result.error());
            if (!mesh.Get())
                if (auto result = AssetManager::ValidateMesh(mesh.Handle()); !result)
                    return std::unexpected("mesh: " + result.error());
            if (!skeleton.Get())
                if (auto result = AssetManager::ValidateSkeleton(skeleton.Handle()); !result)
                    return std::unexpected("skeleton: " + result.error());
            for (const auto &state : animations)
                if (auto result = state.ValidateAssets(); !result)
                    return result;
            return {};
        }

        // Requires successful ValidateAssets() before loading.
        vke_common::SceneResult<void> LoadAssets()
        {
            using vke_common::AssetManager;
            if (!material.Get())
            {
                material.Resolve(AssetManager::LoadMaterial(material.Handle()));
                if (!material.Get())
                    return std::unexpected("material asset " + std::to_string(material.Handle()) + ": loading failed");
            }
            if (!mesh.Get())
            {
                mesh.Resolve(AssetManager::LoadMesh(mesh.Handle()));
                if (!mesh.Get())
                    return std::unexpected("mesh asset " + std::to_string(mesh.Handle()) + ": loading failed");
            }
            if (!skeleton.Get())
            {
                skeleton.Resolve(AssetManager::LoadSkeleton(skeleton.Handle()));
                if (!skeleton.Get())
                    return std::unexpected("skeleton asset " + std::to_string(skeleton.Handle()) + ": loading failed");
            }
            const int jointCount = skeleton.Get()->skeleton.num_joints();
            if (jointCount == 0)
                return std::unexpected("skeleton: no joints");
            if (mesh.Get()->joints.empty() || mesh.Get()->joints.size() > MAX_BONE_PER_SKELETON)
                return std::unexpected("mesh: skin joint count must be between 1 and " + std::to_string(MAX_BONE_PER_SKELETON));
            for (int joint : mesh.Get()->joints)
                if (joint < 0 || joint >= jointCount)
                    return std::unexpected("mesh: skin joint index is outside the skeleton");
            if (!mesh.Get()->invBindMatrices.empty() && mesh.Get()->invBindMatrices.size() != mesh.Get()->joints.size())
                return std::unexpected("mesh: inverse bind matrix count must match skin joint count");
            for (auto &state : animations)
            {
                if (auto result = state.LoadAssets(); !result)
                    return result;
                if (auto result = state.animation.Get()->ValidateSkeleton(skeleton.Get()->skeleton); !result)
                    return result;
            }
            return {};
        }

        nlohmann::json ToJSON() const
        {
            nlohmann::json animationArray = nlohmann::json::array();
            for (const SkeletonAnimationData &state : animations)
                if (state.animation.Handle() != 0)
                    animationArray.push_back(state.ToJSON());
            return {{"type", "animator"},
                    {"material", material.Handle()},
                    {"mesh", mesh.Handle()},
                    {"skeleton", skeleton.Handle()},
                    {"animations", animationArray},
                    {"castsShadow", castsShadow}};
        }
    };

    class SkeletonAnimator
    {
    public:
        struct AnimationState
        {
            std::shared_ptr<vke_common::Animation> animation;
            float weight = 0.0f;
            float playbackSpeed = 1.0f;
            float timeRatio = 0.0f;
            bool playing = true;
            bool loop = true;
            std::unique_ptr<ozz::animation::SamplingJob::Context> context;
            ozz::vector<ozz::math::SoaTransform> locals;

            AnimationState()
                : context(std::make_unique<ozz::animation::SamplingJob::Context>()) {}
        };

        std::shared_ptr<vke_render::Material> material;
        std::shared_ptr<vke_common::Skeleton> skeleton;
        std::vector<AnimationState> animations;
        std::unique_ptr<vke_render::RenderUnit> renderUnit;
        std::unique_ptr<vke_render::RenderUnit> shadowRenderUnit;
        bool castsShadow;
        // The render update callback captures this. Keep EnTT storage addresses stable.
        static constexpr bool in_place_delete = true;

        SkeletonAnimator(const SkeletonAnimator &) = delete;
        SkeletonAnimator &operator=(const SkeletonAnimator &) = delete;

        SkeletonAnimator(SkeletonAnimator &&other) noexcept
            : material(std::move(other.material)),
              skeleton(std::move(other.skeleton)),
              animations(std::move(other.animations)),
              renderUnit(std::move(other.renderUnit)),
              shadowRenderUnit(std::move(other.shadowRenderUnit)),
              castsShadow(other.castsShadow),
              renderID(other.renderID),
              shadowRenderID(std::exchange(other.shadowRenderID, 0)),
              ownerRegistry(std::exchange(other.ownerRegistry, nullptr)),
              ownerEntity(std::exchange(other.ownerEntity, entt::null)),
              blendedLocals(std::move(other.blendedLocals)),
              models(std::move(other.models)),
              skinningMatrices(std::move(other.skinningMatrices)),
              descriptorSets(std::exchange(other.descriptorSets, {})),
              skeletonBuffers(std::move(other.skeletonBuffers))
        {
            bindRenderUpdateCallback();
        }

        SkeletonAnimator &operator=(SkeletonAnimator &&other) noexcept
        {
            if (this != &other)
            {
                if (renderUnit)
                {
                    vke_render::Renderer::WaitIdle();
                    Unload();
                }
                releaseDescriptorSets();
                material = std::move(other.material);
                skeleton = std::move(other.skeleton);
                animations = std::move(other.animations);
                renderUnit = std::move(other.renderUnit);
                shadowRenderUnit = std::move(other.shadowRenderUnit);
                castsShadow = other.castsShadow;
                renderID = other.renderID;
                shadowRenderID = std::exchange(other.shadowRenderID, 0);
                ownerRegistry = std::exchange(other.ownerRegistry, nullptr);
                ownerEntity = std::exchange(other.ownerEntity, entt::null);
                blendedLocals = std::move(other.blendedLocals);
                models = std::move(other.models);
                skinningMatrices = std::move(other.skinningMatrices);
                descriptorSets = std::exchange(other.descriptorSets, {});
                skeletonBuffers = std::move(other.skeletonBuffers);
                bindRenderUpdateCallback();
            }
            return *this;
        }

        SkeletonAnimator(entt::registry &registry, entt::entity entity,
                         vke_common::Transform &transform, const SkeletonAnimatorData &componentData)
            : material(componentData.material.Get()), skeleton(componentData.skeleton.Get()),
              castsShadow(componentData.castsShadow),
              shadowRenderID(0)
        {
            for (const SkeletonAnimationData &animation : componentData.animations)
                addAnimation(animation.animation.Get(), animation.weight, animation.playbackSpeed,
                             animation.timeRatio, animation.loop, animation.playing);
            std::shared_ptr<const vke_render::Mesh> mesh = componentData.mesh.Get();
            int numSOAJoints = skeleton->skeleton.num_soa_joints();
            int numJoints = skeleton->skeleton.num_joints();
            blendedLocals.resize(numSOAJoints);
            models.resize(numJoints);
            skinningMatrices.resize(mesh->joints.size());

            for (int i = 0; i < vke_render::MAX_FRAMES_IN_FLIGHT; ++i)
                skeletonBuffers.emplace_back(sizeof(float) * 16 * MAX_BONE_PER_SKELETON, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

            VkDescriptorSetLayoutBinding layoutBinding{};
            layoutBinding.binding = 0;
            layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            layoutBinding.descriptorCount = 1;
            layoutBinding.stageFlags = VK_SHADER_STAGE_ALL;

            vke_render::DescriptorSetInfo descriptorSetInfo;
            descriptorSetInfo.AddCnt(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1);

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &layoutBinding;
            vkCreateDescriptorSetLayout(vke_render::globalLogicalDevice, &layoutInfo, nullptr, &(descriptorSetInfo.layout));

            descriptorSets.resize(vke_render::MAX_FRAMES_IN_FLIGHT);
            for (int i = 0; i < vke_render::MAX_FRAMES_IN_FLIGHT; ++i)
                descriptorSets[i] = vke_render::VulkanResourceManager::AllocateDescriptorSet(descriptorSetInfo);

            VkWriteDescriptorSet descriptorSetWrite{};
            for (int i = 0; i < vke_render::MAX_FRAMES_IN_FLIGHT; ++i)
            {
                VkDescriptorBufferInfo bufferInfo = skeletonBuffers[i].GetDescriptorBufferInfo();
                vke_render::ConstructDescriptorSetWrite(descriptorSetWrite, descriptorSets[i], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &bufferInfo);
                vkUpdateDescriptorSets(vke_render::globalLogicalDevice, 1, &descriptorSetWrite, 0, nullptr);
            }

            renderUnit = std::make_unique<vke_render::RenderUnit>(mesh, transform.model, descriptorSets[0], false);
            shadowRenderUnit = std::make_unique<vke_render::RenderUnit>(mesh, transform.model, descriptorSets[0], false);

            const auto &names = skeleton->skeleton.joint_names();
            for (auto &n : names)
            {
                std::cout << std::string(n) << "\n";
            }

            ownerRegistry = &registry;
            ownerEntity = entity;
            vke_render::Renderer *renderer = vke_render::Renderer::GetInstance();
            renderID = renderer->GetGBufferPass()->AddUnit(material, renderUnit.get(), true);
            if (castsShadow)
            {
                vke_render::ShadowPass *shadowPass = renderer->GetShadowPass();
                if (shadowPass != nullptr)
                    shadowRenderID = shadowPass->AddUnit(shadowRenderUnit.get(), true);
            }
            updatePose(0.0f);
            bindRenderUpdateCallback();
        }

        ~SkeletonAnimator()
        {
            releaseDescriptorSets();
        }

        void OnTransformed(const vke_common::Transform &transform)
        {
            renderUnit->modelMatrix = transform.model;
            shadowRenderUnit->modelMatrix = transform.model;
        }

        void FillData(SkeletonAnimatorData &data) const
        {
            data.material.SetAsset(material);
            data.mesh.SetAsset(renderUnit->mesh);
            data.skeleton.SetAsset(skeleton);
            data.castsShadow = castsShadow;
            data.animations.clear();
            data.animations.reserve(animations.size());
            for (const AnimationState &state : animations)
            {
                SkeletonAnimationData animationData;
                animationData.animation.SetAsset(state.animation);
                animationData.weight = state.weight;
                animationData.playbackSpeed = state.playbackSpeed;
                animationData.timeRatio = state.timeRatio;
                animationData.playing = state.playing;
                animationData.loop = state.loop;
                data.animations.push_back(std::move(animationData));
            }
        }

        void Unload()
        {
            vke_render::Renderer *renderer = vke_render::Renderer::GetInstance();
            renderer->GetGBufferPass()->RemoveUnit(material.get(), renderID);
            if (castsShadow && shadowRenderID != 0)
            {
                vke_render::ShadowPass *shadowPass = renderer->GetShadowPass();
                if (shadowPass != nullptr)
                    shadowPass->RemoveUnit(shadowRenderID);
                shadowRenderID = 0;
            }
            renderer->RemoveRenderUpdateCallback(renderID);
        }

        void Start() { updatePose(0.0f); }

        void Update(float deltaTime) { updatePose(deltaTime); }

        std::span<const ozz::math::Float4x4> GetSkinningMatrices() const
        {
            return {skinningMatrices.data(), skinningMatrices.size()};
        }

        vke_common::SceneResult<void> AddAnimation(
            std::shared_ptr<vke_common::Animation> anim,
            float weight = 0.0f,
            float speed = 1.0f,
            float ratio = 0.0f,
            bool loop = true,
            bool playing = true)
        {
            if (anim == nullptr)
                return std::unexpected("animation: asset is empty");
            if (auto result = anim->ValidateSkeleton(skeleton->skeleton); !result)
                return result;
            addAnimation(std::move(anim), weight, speed, ratio, loop, playing);
            return {};
        }

        uint32_t GetAnimationCount() const
        {
            return static_cast<uint32_t>(animations.size());
        }

        void SetAnimationSpeed(size_t index, float speed)
        {
            if (index < animations.size())
                animations[index].playbackSpeed = speed;
        }

        float GetAnimationSpeed(size_t index) const
        {
            return index < animations.size() ? animations[index].playbackSpeed : 0.0f;
        }

        void SetAnimationTimeRatio(size_t index, float ratio)
        {
            if (index >= animations.size())
                return;

            AnimationState &state = animations[index];
            state.timeRatio = state.loop ? ratio - glm::floor(ratio) : glm::clamp(ratio, 0.0f, 1.0f);
        }

        float GetAnimationTimeRatio(size_t index) const
        {
            return index < animations.size() ? animations[index].timeRatio : 0.0f;
        }

        void SetAnimationLoop(size_t index, bool loop)
        {
            if (index >= animations.size())
                return;

            animations[index].loop = loop;
            SetAnimationTimeRatio(index, animations[index].timeRatio);
        }

        bool GetAnimationLoop(size_t index) const
        {
            return index < animations.size() && animations[index].loop;
        }

        void SetAnimationPlaying(size_t index, bool playing)
        {
            if (index < animations.size())
                animations[index].playing = playing;
        }

        bool GetAnimationPlaying(size_t index) const
        {
            return index < animations.size() && animations[index].playing;
        }

        void SetBlendWeights(const float *weights, size_t count)
        {
            if (weights == nullptr)
                return;

            const size_t n = std::min(count, animations.size());
            for (size_t i = 0; i < n; ++i)
                animations[i].weight = weights[i];
        }

        void SetAnimationWeight(size_t index, float weight)
        {
            if (index < animations.size())
                animations[index].weight = weight;
        }

        float GetAnimationWeight(size_t index) const
        {
            return index < animations.size() ? animations[index].weight : 0.0f;
        }

    private:
        // Both scene loading and AddAnimation validate compatibility before this point.
        void addAnimation(std::shared_ptr<vke_common::Animation> anim, float weight, float speed,
                          float ratio, bool loop, bool playing)
        {
            AnimationState state;
            state.animation = std::move(anim);
            state.weight = weight;
            state.playbackSpeed = speed;
            state.loop = loop;
            state.playing = playing;
            state.locals.resize(skeleton->skeleton.num_soa_joints());
            state.context->Resize(skeleton->skeleton.num_joints());
            animations.push_back(std::move(state));
            SetAnimationTimeRatio(animations.size() - 1, ratio);
        }

        void bindRenderUpdateCallback()
        {
            vke_render::Renderer::AddRenderUpdateCallback(
                renderID, std::bind(&SkeletonAnimator::upload, this, std::placeholders::_1));
        }

        void releaseDescriptorSets()
        {
            for (auto set : descriptorSets)
                vke_render::VulkanResourceManager::FreeDescriptorSet(set);
            descriptorSets.clear();
        }

        vke_ds::id64_t renderID;
        vke_ds::id64_t shadowRenderID;

        static glm::vec3 toGlm(const ozz::math::Float3 &value)
        {
            return glm::vec3(value.x, value.y, value.z);
        }

        static glm::quat toGlm(const ozz::math::Quaternion &value)
        {
            return glm::normalize(glm::quat(value.w, value.x, value.y, value.z));
        }

        static ozz::math::Float3 sampleRootMotionPosition(const vke_common::Animation &animation, float ratio)
        {
            ozz::math::Float3 result(0.0f, 0.0f, 0.0f);
            ozz::animation::Float3TrackSamplingJob job;
            job.track = &animation.rootMotionPosition;
            job.ratio = ratio;
            job.result = &result;
            if (!job.Run())
                VKE_LOG_ERROR("ROOT MOTION POSITION SAMPLING FAIL")
            return result;
        }

        static ozz::math::Quaternion sampleRootMotionRotation(const vke_common::Animation &animation, float ratio)
        {
            ozz::math::Quaternion result = ozz::math::Quaternion::identity();
            ozz::animation::QuaternionTrackSamplingJob job;
            job.track = &animation.rootMotionRotation;
            job.ratio = ratio;
            job.result = &result;
            if (!job.Run())
                VKE_LOG_ERROR("ROOT MOTION ROTATION SAMPLING FAIL")
            return result;
        }

        struct RootMotionDelta
        {
            glm::vec3 position;
            glm::quat rotation;
        };

        static RootMotionDelta calculateRootMotionDelta(const vke_common::Animation &animation, float previousRatio,
                                                        float currentRatio, double loops)
        {
            const glm::vec3 previousPosition = toGlm(sampleRootMotionPosition(animation, previousRatio));
            const glm::vec3 currentPosition = toGlm(sampleRootMotionPosition(animation, currentRatio));
            const glm::quat inversePreviousRotation = glm::inverse(toGlm(sampleRootMotionRotation(animation, previousRatio)));
            const glm::quat currentRotation = toGlm(sampleRootMotionRotation(animation, currentRatio));
            if (loops == 0.0)
                return {inversePreviousRotation * (currentPosition - previousPosition),
                        glm::normalize(inversePreviousRotation * currentRotation)};

            glm::dvec3 cyclePosition(animation.rootMotionCyclePosition);
            glm::dquat cycleRotation(animation.rootMotionCycleRotation);
            if (loops < 0.0)
            {
                cycleRotation = glm::inverse(cycleRotation);
                cyclePosition = -(cycleRotation * cyclePosition);
            }
            glm::dvec3 unwrappedPosition(currentPosition);
            glm::dquat unwrappedRotation(currentRotation);
            // Compose C^loops * M(current), rotating each cycle's translation too.
            for (double remaining = std::abs(loops); remaining >= 1.0; remaining = std::floor(remaining * 0.5))
            {
                if (std::fmod(remaining, 2.0) != 0.0)
                {
                    unwrappedPosition = cyclePosition + cycleRotation * unwrappedPosition;
                    unwrappedRotation = glm::normalize(cycleRotation * unwrappedRotation);
                }
                cyclePosition += cycleRotation * cyclePosition;
                cycleRotation = glm::normalize(cycleRotation * cycleRotation);
            }
            // The entity consumes a local increment: inverse(M(previous)) * C^loops * M(current).
            const glm::dquat inversePrevious(inversePreviousRotation);
            return {glm::vec3(inversePrevious * (unwrappedPosition - glm::dvec3(previousPosition))),
                    glm::normalize(glm::quat(inversePrevious * unwrappedRotation))};
        }

        void applyRootMotion(const glm::vec3 &localDeltaPosition, const glm::quat &localDeltaRotation)
        {
            // Use the scene path so children and render snapshots follow root motion too.
            vke_common::SceneTransformSystem transforms(*ownerRegistry);
            if (glm::dot(localDeltaPosition, localDeltaPosition) > 0.0f)
                transforms.TranslateLocal(ownerEntity, localDeltaPosition);

            if (glm::dot(glm::vec3(localDeltaRotation.x, localDeltaRotation.y, localDeltaRotation.z),
                         glm::vec3(localDeltaRotation.x, localDeltaRotation.y, localDeltaRotation.z)) > 0.0f)
            {
                const auto rotation = ownerRegistry->get<vke_common::Transform>(ownerEntity).localRotation;
                transforms.SetLocalRotation(ownerEntity, glm::normalize(rotation * localDeltaRotation));
            }
        }

        void updatePose(float deltaTime)
        {
            ozz::vector<ozz::animation::BlendingJob::Layer> layers;
            layers.reserve(animations.size());
            ozz::animation::BlendingJob blending_job;
            glm::vec3 blendedRootMotionPosition(0.0f);
            glm::vec4 blendedRootMotionRotation(0.0f);
            float blendWeightSum = 0.0f;

            for (AnimationState &state : animations)
            {
                if (state.animation == nullptr)
                    continue;

                const float previousRatio = state.timeRatio;
                double loops = 0.0;
                if (state.playing)
                {
                    const double newRatio = previousRatio + static_cast<double>(deltaTime) * state.playbackSpeed / state.animation->Duration();
                    if (!std::isfinite(newRatio))
                    {
                        VKE_LOG_ERROR("Animation time advance must be finite")
                        continue;
                    }
                    if (state.loop)
                    {
                        loops = std::floor(newRatio);
                        state.timeRatio = static_cast<float>(newRatio - loops);
                        // Rounding to float can reach the next cycle's endpoint.
                        if (state.timeRatio == 1.0f)
                        {
                            state.timeRatio = 0.0f;
                            loops += 1.0;
                        }
                    }
                    else
                        state.timeRatio = static_cast<float>(std::clamp(newRatio, 0.0, 1.0));
                }
                const float currentRatio = state.timeRatio;

                if (state.weight <= 0.0f)
                    continue;

                {
                    ozz::animation::SamplingJob sampling_job;
                    sampling_job.animation = &(state.animation->animation);
                    sampling_job.context = state.context.get();
                    sampling_job.ratio = state.timeRatio;
                    sampling_job.output = make_span(state.locals);
                    if (!sampling_job.Run())
                    {
                        VKE_LOG_ERROR("SAMPLING FAIL")
                        continue;
                    }
                }

                {
                    ozz::animation::BlendingJob::Layer layer;
                    layer.weight = state.weight;
                    layer.transform = make_span(state.locals);
                    layers.push_back(layer);
                }

                glm::quat deltaRotation(1.0f, 0.0f, 0.0f, 0.0f);
                if (state.animation->hasRootMotion && state.playing)
                {
                    const RootMotionDelta delta = calculateRootMotionDelta(*state.animation, previousRatio, currentRatio, loops);
                    deltaRotation = delta.rotation;
                    blendedRootMotionPosition += delta.position * state.weight;
                }
                // All pose layers participate; paused and in-place clips contribute identity motion.
                glm::vec4 deltaRotationVector(deltaRotation.x, deltaRotation.y, deltaRotation.z, deltaRotation.w);
                if (glm::dot(blendedRootMotionRotation, deltaRotationVector) < 0.0f)
                    deltaRotationVector = -deltaRotationVector;
                blendedRootMotionRotation += deltaRotationVector * state.weight;
                blendWeightSum += state.weight;
            }

            if (blendWeightSum > 0.0f)
            {
                // Match the pose blend's rest-pose contribution below its threshold.
                blendedRootMotionPosition /= std::max(blendWeightSum, blending_job.threshold);
                const float restWeight = std::max(0.0f, blending_job.threshold - blendWeightSum);
                blendedRootMotionRotation.w += blendedRootMotionRotation.w < 0.0f ? -restWeight : restWeight;
                glm::quat deltaRotation(
                    blendedRootMotionRotation.w,
                    blendedRootMotionRotation.x,
                    blendedRootMotionRotation.y,
                    blendedRootMotionRotation.z);
                applyRootMotion(blendedRootMotionPosition, glm::normalize(deltaRotation));
            }

            blending_job.layers = make_span(layers);
            blending_job.rest_pose = skeleton->skeleton.joint_rest_poses();
            blending_job.output = make_span(blendedLocals);
            if (!blending_job.Run())
                VKE_LOG_ERROR("BLENDING FAIL")

            ozz::animation::LocalToModelJob ltm_job;
            ltm_job.skeleton = &(skeleton->skeleton);
            ltm_job.input = make_span(blendedLocals);
            ltm_job.output = make_span(models);
            if (!ltm_job.Run())
                VKE_LOG_ERROR("LTM FAIL")

            const vke_render::Mesh &mesh = *(renderUnit->mesh);
            for (int i = 0; i < skinningMatrices.size(); ++i)
            {
                int idx = mesh.joints[i];
                skinningMatrices[i] = mesh.invBindMatrices.empty()
                                          ? models[idx]
                                          : models[idx] * mesh.invBindMatrices[i];
            }
        }

        void upload(uint32_t currentFrame)
        {
            float *bufferp = (float *)(skeletonBuffers[currentFrame].data);
            for (int i = 0; i < skinningMatrices.size(); ++i)
                for (int j = 0; j < 4; ++j)
                    ozz::math::StorePtrU(skinningMatrices[i].cols[j], bufferp + (i << 4) + (j << 2));
            renderUnit->perUnitDescriptorSet = descriptorSets[currentFrame];
            shadowRenderUnit->perUnitDescriptorSet = descriptorSets[currentFrame];
        }

        entt::registry *ownerRegistry = nullptr;
        entt::entity ownerEntity = entt::null;
        ozz::vector<ozz::math::SoaTransform> blendedLocals;
        ozz::vector<ozz::math::Float4x4> models;
        ozz::vector<ozz::math::Float4x4> skinningMatrices;
        // Owns all frame sets; both render units borrow the current frame's handle.
        std::vector<VkDescriptorSet> descriptorSets;
        std::vector<vke_render::HostCoherentBuffer> skeletonBuffers;
    };
}

#endif
