#include <editor/editor.hpp>
#include <component/audio_source.hpp>
#include <component/audio_listener.hpp>
#include <algorithm>
#include <string>

namespace vke_editor
{
    static std::string AudioClipDisplayName(vke_common::AssetHandle handle, const char *name)
    {
        if (handle == 0)
            return "0  <none>";
        return std::to_string(handle) + "  " + (name ? name : "<missing>");
    }

    static void SetAudioClip(vke_component::AudioSource &source, vke_common::AssetHandle handle)
    {
        std::shared_ptr<vke_audio::AudioClip> clip;
        if (handle != 0)
        {
            auto loaded = vke_common::AssetManager::LoadAudioClip(handle);
            if (!loaded)
            {
                VKE_LOG_ERROR("Cannot set audio clip: {}", loaded.error())
                return;
            }
            clip = std::move(*loaded);
        }
        if (auto result = source.SetClip(std::move(clip)); !result)
            VKE_LOG_ERROR("Cannot set audio clip: {}", result.error())
    }

    void Editor::drawAudioSourceComponent()
    {
        if (selectedEntity == entt::null ||
            !sceneManager->registry.all_of<vke_component::AudioSource>(selectedEntity))
            return;

        if (!ImGui::TreeNodeEx("AudioSource", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        vke_component::AudioSource &source =
            sceneManager->registry.get<vke_component::AudioSource>(selectedEntity);
        const vke_common::AssetHandle clipHandle = source.clip ? source.clip->handle : 0;
        const vke_common::AudioClipAsset *clipAsset =
            ShowAssetReadResult(vke_common::AssetManager::GetAudioClipAsset(clipHandle));
        const std::string selectedClip =
            AudioClipDisplayName(clipHandle, clipAsset ? clipAsset->name.c_str() : nullptr);

        if (ImGui::BeginCombo("Clip", selectedClip.c_str()))
        {
            if (ImGui::Selectable("0  <none>", clipHandle == 0))
                SetAudioClip(source, 0);

            ShowAssetIterationResult(vke_common::AssetManager::IterateAudioClipAsset(
                [&](const vke_common::AudioClipAsset &asset)
                {
                    const bool selected = asset.id == clipHandle;
                    const std::string label = std::to_string(asset.id) + "  " + asset.name;
                    if (ImGui::Selectable(label.c_str(), selected))
                        SetAudioClip(source, asset.id);
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }));
            ImGui::EndCombo();
        }

        ImGui::Checkbox("Play On Start", &source.playOnStart);

        bool looping = source.looping;
        if (ImGui::Checkbox("Looping", &looping))
            source.SetLooping(looping);

        float volume = source.volume;
        if (ImGui::SliderFloat("Volume", &volume, 0.0f, 4.0f, "%.2f"))
            source.SetVolume(volume);

        float pitch = source.pitch;
        if (ImGui::InputFloat("Pitch", &pitch, 0.05f, 0.25f, "%.2f"))
            source.SetPitch(std::max(pitch, 0.0f));

        bool spatializationEnabled = source.spatializationEnabled;
        if (ImGui::Checkbox("Spatialization", &spatializationEnabled))
            source.SetSpatializationEnabled(spatializationEnabled);

        ImGui::BeginDisabled(!source.spatializationEnabled);
        int attenuationModel = std::clamp(source.attenuationModel, 0, 3);
        const char *attenuationModels[] = {"None", "Inverse", "Linear", "Exponential"};
        if (ImGui::Combo("Attenuation", &attenuationModel,
                         attenuationModels, IM_ARRAYSIZE(attenuationModels)))
            source.SetAttenuationModel(attenuationModel);

        float rolloff = source.rolloff;
        if (ImGui::InputFloat("Rolloff", &rolloff, 0.05f, 0.25f, "%.3f"))
            source.SetRolloff(std::max(rolloff, 0.0f));

        float minDistance = source.minDistance;
        if (ImGui::InputFloat("Min Distance", &minDistance, 0.1f, 1.0f, "%.3f"))
            source.SetMinDistance(std::clamp(minDistance, 0.0f, source.maxDistance));

        float maxDistance = source.maxDistance;
        if (ImGui::InputFloat("Max Distance", &maxDistance, 1.0f, 10.0f, "%.3f"))
            source.SetMaxDistance(std::max(maxDistance, source.minDistance));

        float dopplerFactor = source.dopplerFactor;
        if (ImGui::InputFloat("Doppler Factor", &dopplerFactor, 0.05f, 0.25f, "%.3f"))
            source.SetDopplerFactor(std::max(dopplerFactor, 0.0f));
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!source.sound);
        if (ImGui::Button(source.IsPlaying() ? "Pause" : "Play"))
        {
            if (source.IsPlaying())
                source.Pause();
            else
                source.Play();
        }
        ImGui::SameLine();
        if (ImGui::Button("Replay"))
            source.Replay();
        ImGui::SameLine();
        if (ImGui::Button("Stop"))
            source.Stop();

        float playbackTime = source.GetTime();
        if (ImGui::InputFloat("Playback Time", &playbackTime, 0.1f, 1.0f, "%.2f s"))
            source.SetTime(std::max(playbackTime, 0.0f));
        ImGui::EndDisabled();

        ImGui::TreePop();
    }

    void Editor::drawAudioListenerComponent()
    {
        if (selectedEntity == entt::null ||
            !sceneManager->registry.all_of<vke_component::AudioListener>(selectedEntity))
            return;

        if (!ImGui::TreeNodeEx("AudioListener", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        vke_component::AudioListener &listener =
            sceneManager->registry.get<vke_component::AudioListener>(selectedEntity);
        ImGui::Checkbox("Enabled", &listener.enabled);

        ImGui::TreePop();
    }
}
