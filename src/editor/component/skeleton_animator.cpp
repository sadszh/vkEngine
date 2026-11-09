#include <editor/editor.hpp>
#include <component/skeleton_animator.hpp>

namespace vke_editor
{
    static std::string AssetDisplayName(vke_common::AssetHandle handle, const char *name)
    {
        if (handle == 0)
            return "0  <none>";
        return std::to_string(handle) + "  " + (name ? name : "<missing>");
    }

    static void DrawReadOnlyAsset(const char *label, const std::string &displayName)
    {
        std::vector<char> value(displayName.begin(), displayName.end());
        value.push_back('\0');

        ImGui::BeginDisabled();
        ImGui::InputText(label, value.data(), value.size(), ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();
    }

    void Editor::drawSkeletonAnimatorComponent()
    {
        if (selectedEntity == entt::null ||
            !sceneManager->registry.all_of<vke_component::SkeletonAnimator>(selectedEntity))
            return;

        if (!ImGui::TreeNodeEx("SkeletonAnimator", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        const vke_component::SkeletonAnimator &animator =
            sceneManager->registry.get<vke_component::SkeletonAnimator>(selectedEntity);
        const vke_common::AssetHandle materialHandle =
            animator.material == nullptr ? 0 : animator.material->handle;
        const vke_common::AssetHandle meshHandle =
            animator.renderUnit == nullptr || animator.renderUnit->mesh == nullptr
                ? 0
                : animator.renderUnit->mesh->handle;
        const vke_common::AssetHandle skeletonHandle =
            animator.skeleton == nullptr ? 0 : animator.skeleton->handle;

        {
            auto *mat = ShowAssetReadResult(vke_common::AssetManager::GetMaterialAsset(materialHandle));
            DrawReadOnlyAsset("Material", AssetDisplayName(materialHandle, mat ? mat->name.c_str() : nullptr));
        }
        {
            auto *meshAsset = ShowAssetReadResult(vke_common::AssetManager::GetMeshAsset(meshHandle));
            DrawReadOnlyAsset("Mesh", AssetDisplayName(meshHandle, meshAsset ? meshAsset->name.c_str() : nullptr));
        }
        {
            auto *skel = ShowAssetReadResult(vke_common::AssetManager::GetSkeletonAsset(skeletonHandle));
            DrawReadOnlyAsset("Skeleton", AssetDisplayName(skeletonHandle, skel ? skel->name.c_str() : nullptr));
        }
        if (ImGui::TreeNodeEx("Animations", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (size_t i = 0; i < animator.animations.size(); ++i)
            {
                const vke_component::SkeletonAnimator::AnimationState &state = animator.animations[i];
                const vke_common::AssetHandle animationHandle =
                    state.animation == nullptr ? 0 : state.animation->handle;
                ImGui::PushID(static_cast<int>(i));
                {
                    auto *anim = ShowAssetReadResult(vke_common::AssetManager::GetAnimationAsset(animationHandle));
                    DrawReadOnlyAsset("Animation", AssetDisplayName(animationHandle, anim ? anim->name.c_str() : nullptr));
                }
                ImGui::Text("Weight: %.3f  Speed: %.3f  Time: %.3f  Loop: %s",
                            state.weight,
                            state.playbackSpeed,
                            state.timeRatio,
                            state.loop ? "true" : "false");
                ImGui::Separator();
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        ImGui::TreePop();
    }
}
