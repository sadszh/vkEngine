#ifndef AUDIO_SOURCE_H
#define AUDIO_SOURCE_H

#include <json_validation.hpp>
#include <utility>
#include <memory>
#include <cstdint>
#include <common.hpp>
#include <nlohmann/json.hpp>
#include <miniaudio/miniaudio.h>
#include <asset/asset_manager.hpp>
#include <asset/asset_ref.hpp>
#include <audio/audio_clip.hpp>
#include <audio/audio_manager.hpp>
#include <logger.hpp>

namespace vke_component
{
    struct AudioSourceData
    {
        vke_common::AssetRef<vke_audio::AudioClip> clip;
        bool playOnStart = true;
        bool looping = false;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool spatializationEnabled = true;
        int attenuationModel = 1; // 0=None, 1=Inverse, 2=Linear, 3=Exponential
        float rolloff = 1.0f;
        float minDistance = 1.0f;
        float maxDistance = 100.0f;
        float dopplerFactor = 1.0f;

        // Call before constructing from JSON; asset checks happen in ValidateAssets.
        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            return Object(json).Unsigneds({"clip"}).Unsigneds({"attenuationModel"}, 3)
                .Booleans({"playOnStart", "looping", "spatializationEnabled"})
                .Numbers({"volume", "pitch", "rolloff", "minDistance", "maxDistance", "dopplerFactor"}).Result();
        }

        AudioSourceData() = default;

        AudioSourceData(const nlohmann::json &json)
        {
            clip.SetHandle(json.value("clip", vke_common::AssetHandle{0}));

            playOnStart = json.value("playOnStart", true);
            looping = json.value("looping", false);
            volume = json.value("volume", 1.0f);
            pitch = json.value("pitch", 1.0f);
            spatializationEnabled = json.value("spatializationEnabled", true);
            attenuationModel = json.value("attenuationModel", 1);
            rolloff = json.value("rolloff", 1.0f);
            minDistance = json.value("minDistance", 1.0f);
            maxDistance = json.value("maxDistance", 100.0f);
            dopplerFactor = json.value("dopplerFactor", 1.0f);
        }

        vke_common::Result<void> ValidateAssets() const
        {
            using vke_common::AssetManager;
            if (clip.Handle() != 0 && !clip.Get())
                if (auto result = AssetManager::ValidateAudioClip(clip.Handle()); !result)
                    return std::unexpected("clip: " + result.error());
            return {};
        }

        // Requires successful ValidateAssets() before loading.
        vke_common::Result<void> LoadAssets()
        {
            using vke_common::AssetManager;
            if (clip.Handle() != 0 && !clip.Get())
            {
                clip.Resolve(AssetManager::LoadAudioClip(clip.Handle()));
                if (!clip.Get())
                    return std::unexpected("clip asset " + std::to_string(clip.Handle()) + ": loading failed");
            }
            return {};
        }

        nlohmann::json ToJSON() const
        {
            return {
                {"type", "audioSource"},
                {"clip", clip.Handle()},
                {"playOnStart", playOnStart},
                {"looping", looping},
                {"volume", volume},
                {"pitch", pitch},
                {"spatializationEnabled", spatializationEnabled},
                {"attenuationModel", attenuationModel},
                {"rolloff", rolloff},
                {"minDistance", minDistance},
                {"maxDistance", maxDistance},
                {"dopplerFactor", dopplerFactor}};
        }
    };

    class AudioSource
    {
    public:
        std::shared_ptr<vke_audio::AudioClip> clip;
        bool playOnStart = true;
        bool looping = false;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool spatializationEnabled = true;
        int attenuationModel = 1;
        float rolloff = 1.0f;
        float minDistance = 1.0f;
        float maxDistance = 100.0f;
        float dopplerFactor = 1.0f;
        ma_sound *sound = nullptr;
        bool soundInitialized = false;

        AudioSource(const AudioSource &) = delete;
        AudioSource &operator=(const AudioSource &) = delete;

        AudioSource(AudioSource &&other) noexcept
            : clip(std::move(other.clip)),
              playOnStart(other.playOnStart),
              looping(other.looping),
              volume(other.volume),
              pitch(other.pitch),
              spatializationEnabled(other.spatializationEnabled),
              attenuationModel(other.attenuationModel),
              rolloff(other.rolloff),
              minDistance(other.minDistance),
              maxDistance(other.maxDistance),
              dopplerFactor(other.dopplerFactor),
              sound(std::exchange(other.sound, nullptr)),
              soundInitialized(std::exchange(other.soundInitialized, false)) {}

        AudioSource &operator=(AudioSource &&other) noexcept
        {
            if (this != &other)
            {
                Unload();
                clip = std::move(other.clip);
                playOnStart = other.playOnStart;
                looping = other.looping;
                volume = other.volume;
                pitch = other.pitch;
                spatializationEnabled = other.spatializationEnabled;
                attenuationModel = other.attenuationModel;
                rolloff = other.rolloff;
                minDistance = other.minDistance;
                maxDistance = other.maxDistance;
                dopplerFactor = other.dopplerFactor;
                sound = std::exchange(other.sound, nullptr);
                soundInitialized = std::exchange(other.soundInitialized, false);
            }
            return *this;
        }

        AudioSource(const AudioSourceData &componentData)
            : clip(componentData.clip.Get()),
              playOnStart(componentData.playOnStart),
              looping(componentData.looping),
              volume(componentData.volume),
              pitch(componentData.pitch),
              spatializationEnabled(componentData.spatializationEnabled),
              attenuationModel(componentData.attenuationModel),
              rolloff(componentData.rolloff),
              minDistance(componentData.minDistance),
              maxDistance(componentData.maxDistance),
              dopplerFactor(componentData.dopplerFactor)
        {
            initializeSound();
        }

        void FillData(AudioSourceData &data) const
        {
            data.clip.SetAsset(clip);
            data.playOnStart = playOnStart;
            data.looping = looping;
            data.volume = volume;
            data.pitch = pitch;
            data.spatializationEnabled = spatializationEnabled;
            data.attenuationModel = attenuationModel;
            data.rolloff = rolloff;
            data.minDistance = minDistance;
            data.maxDistance = maxDistance;
            data.dopplerFactor = dopplerFactor;
        }

        void Start()
        {
            if (playOnStart)
                Play();
        }

        void Unload()
        {
            if (soundInitialized && sound != nullptr)
            {
                vke_audio::AudioManager::ReleaseSound(sound);
                sound = nullptr;
                soundInitialized = false;
            }
        }

        void SetClip(std::shared_ptr<vke_audio::AudioClip> newClip)
        {
            Unload();
            clip = std::move(newClip);
            if (clip && clip->IsValid())
                initializeSound();
        }

        void Play()
        {
            if (soundInitialized && sound != nullptr)
                ma_sound_start(sound);
        }

        void Replay()
        {
            if (soundInitialized && sound != nullptr)
            {
                ma_sound_stop(sound);
                ma_sound_seek_to_pcm_frame(sound, 0);
                ma_sound_start(sound);
            }
        }

        void Stop()
        {
            if (soundInitialized && sound != nullptr)
            {
                ma_sound_stop(sound);
                ma_sound_seek_to_pcm_frame(sound, 0);
            }
        }

        void Pause()
        {
            if (soundInitialized && sound != nullptr)
                ma_sound_stop(sound);
        }

        bool IsPlaying() const
        {
            return soundInitialized && sound != nullptr && ma_sound_is_playing(sound);
        }

        void SetLooping(bool loop)
        {
            looping = loop;
            if (soundInitialized && sound != nullptr)
                ma_sound_set_looping(sound, loop);
        }

        void SetVolume(float vol)
        {
            volume = vol;
            if (soundInitialized && sound != nullptr)
                ma_sound_set_volume(sound, vol);
        }

        void SetPitch(float p)
        {
            pitch = p;
            if (soundInitialized && sound != nullptr)
                ma_sound_set_pitch(sound, p);
        }

        void SetSpatializationEnabled(bool enabled)
        {
            spatializationEnabled = enabled;
            if (soundInitialized && sound != nullptr)
                ma_sound_set_spatialization_enabled(sound, enabled);
        }

        void SetAttenuationModel(int model)
        {
            attenuationModel = model;
            if (soundInitialized && sound != nullptr)
                ma_sound_set_attenuation_model(sound, (ma_attenuation_model)model);
        }

        void SetRolloff(float factor)
        {
            rolloff = factor;
            if (soundInitialized && sound != nullptr)
                ma_sound_set_rolloff(sound, factor);
        }

        void SetMinDistance(float distance)
        {
            minDistance = distance;
            if (soundInitialized && sound != nullptr)
                ma_sound_set_min_distance(sound, distance);
        }

        void SetMaxDistance(float distance)
        {
            maxDistance = distance;
            if (soundInitialized && sound != nullptr)
                ma_sound_set_max_distance(sound, distance);
        }

        void SetDopplerFactor(float factor)
        {
            dopplerFactor = factor;
            if (soundInitialized && sound != nullptr)
                ma_sound_set_doppler_factor(sound, factor);
        }

        float GetTime() const
        {
            if (!soundInitialized || sound == nullptr)
                return 0.0f;

            ma_uint64 frames;
            if (ma_sound_get_cursor_in_pcm_frames(sound, &frames) == MA_SUCCESS)
            {
                ma_engine *eng = ma_sound_get_engine(sound);
                if (eng == nullptr)
                    return 0.0f;
                float sampleRate = (float)ma_engine_get_sample_rate(eng);
                return sampleRate > 0.0f ? (float)frames / sampleRate : 0.0f;
            }
            return 0.0f;
        }

        void SetTime(float seconds)
        {
            if (!soundInitialized || sound == nullptr || seconds < 0.0f)
                return;

            ma_engine *eng = ma_sound_get_engine(sound);
            if (eng == nullptr)
                return;
            float sampleRate = (float)ma_engine_get_sample_rate(eng);
            if (sampleRate > 0.0f)
            {
                ma_uint64 frame = (ma_uint64)(seconds * sampleRate);
                ma_sound_seek_to_pcm_frame(sound, frame);
            }
        }

    private:
        void initializeSound()
        {
            if (!clip || !clip->IsValid())
            {
                VKE_LOG_WARN("AudioSource::initializeSound: invalid clip (asset {})", clip ? clip->handle : 0);
                return;
            }

            ma_engine *engine = vke_audio::AudioManager::GetEngine();
            if (engine == nullptr)
            {
                VKE_LOG_ERROR("AudioSource::initializeSound: AudioManager not initialized");
                return;
            }

            ma_uint32 flags = MA_SOUND_FLAG_DECODE;
            if (!spatializationEnabled)
                flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;

            sound = vke_audio::AudioManager::LoadSound(clip->path.c_str(), flags);
            if (sound == nullptr)
            {
                soundInitialized = false;
                return;
            }

            ma_sound_set_looping(sound, looping);
            ma_sound_set_volume(sound, volume);
            ma_sound_set_pitch(sound, pitch);

            if (spatializationEnabled)
            {
                ma_sound_set_attenuation_model(sound, (ma_attenuation_model)attenuationModel);
                ma_sound_set_rolloff(sound, rolloff);
                ma_sound_set_min_distance(sound, minDistance);
                ma_sound_set_max_distance(sound, maxDistance);
                ma_sound_set_doppler_factor(sound, dopplerFactor);
            }

            soundInitialized = true;
        }
    };
}

#endif
