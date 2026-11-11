#ifndef AUDIO_SOURCE_H
#define AUDIO_SOURCE_H

#include <json_validation.hpp>
#include <algorithm>
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
                auto loaded = AssetManager::LoadAudioClip(clip.Handle());
                if (!loaded) return std::unexpected(loaded.error());
                clip.Resolve(std::move(*loaded));
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
    private:
        struct BufferDeleter
        {
            void operator()(ma_audio_buffer *buffer) const
            {
                ma_audio_buffer_uninit(buffer);
                delete buffer;
            }
        };

        struct SoundDeleter
        {
            void operator()(ma_sound *sound) const
            {
                ma_sound_uninit(sound);
                delete sound;
            }
        };

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
        std::unique_ptr<ma_audio_buffer, BufferDeleter> buffer;
        std::unique_ptr<ma_sound, SoundDeleter> sound;

        AudioSource(const AudioSource &) = delete;
        AudioSource &operator=(const AudioSource &) = delete;

        AudioSource(AudioSource &&other) noexcept = default;

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
                buffer = std::move(other.buffer);
                sound = std::move(other.sound);
            }
            return *this;
        }

        AudioSource(const AudioSourceData &componentData)
            : playOnStart(componentData.playOnStart),
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
            if (auto loaded = SetClip(componentData.clip.Get()); !loaded)
                VKE_FATAL("Cannot construct AudioSource: {}", loaded.error())
        }

        ~AudioSource() = default;

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
            sound.reset();
            buffer.reset();
        }

        vke_common::Result<void> SetClip(std::shared_ptr<vke_audio::AudioClip> newClip)
        {
            if (!newClip)
            {
                Unload();
                clip.reset();
                return {};
            }

            const std::string context = "audio clip " + std::to_string(newClip->handle) + ": ";
            auto *engine = vke_audio::AudioManager::GetEngine();
            if (!engine) return std::unexpected(context + "audio engine is unavailable");

            auto bufferStorage = std::make_unique<ma_audio_buffer>();
            auto config = ma_audio_buffer_config_init(ma_format_f32, newClip->channels,
                                                      newClip->FrameCount(), newClip->samples.get(), nullptr);
            config.sampleRate = newClip->sampleRate;
            auto result = ma_audio_buffer_init(&config, bufferStorage.get());
            if (result != MA_SUCCESS)
                return std::unexpected(context + "cannot create audio buffer: " + ma_result_description(result));
            std::unique_ptr<ma_audio_buffer, BufferDeleter> newBuffer(bufferStorage.release());

            ma_uint32 flags = 0;
            if (!spatializationEnabled)
                flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;

            auto soundStorage = std::make_unique<ma_sound>();
            result = ma_sound_init_from_data_source(engine, newBuffer.get(), flags, nullptr, soundStorage.get());
            if (result != MA_SUCCESS)
                return std::unexpected(context + "cannot create sound: " + ma_result_description(result));
            std::unique_ptr<ma_sound, SoundDeleter> newSound(soundStorage.release());

            ma_sound_set_looping(newSound.get(), looping);
            ma_sound_set_volume(newSound.get(), volume);
            ma_sound_set_pitch(newSound.get(), pitch);
            if (spatializationEnabled)
            {
                ma_sound_set_attenuation_model(newSound.get(), (ma_attenuation_model)attenuationModel);
                ma_sound_set_rolloff(newSound.get(), rolloff);
                ma_sound_set_min_distance(newSound.get(), minDistance);
                ma_sound_set_max_distance(newSound.get(), maxDistance);
                ma_sound_set_doppler_factor(newSound.get(), dopplerFactor);
            }

            Unload();
            clip = std::move(newClip);
            buffer = std::move(newBuffer);
            sound = std::move(newSound);
            return {};
        }

        void Play()
        {
            if (sound != nullptr)
                ma_sound_start(sound.get());
        }

        void Replay()
        {
            if (sound != nullptr)
            {
                ma_sound_stop(sound.get());
                ma_sound_seek_to_pcm_frame(sound.get(), 0);
                ma_sound_start(sound.get());
            }
        }

        void Stop()
        {
            if (sound != nullptr)
            {
                ma_sound_stop(sound.get());
                ma_sound_seek_to_pcm_frame(sound.get(), 0);
            }
        }

        void Pause()
        {
            if (sound != nullptr)
                ma_sound_stop(sound.get());
        }

        bool IsPlaying() const
        {
            return sound != nullptr && ma_sound_is_playing(sound.get());
        }

        void SetLooping(bool loop)
        {
            looping = loop;
            if (sound != nullptr)
                ma_sound_set_looping(sound.get(), loop);
        }

        void SetVolume(float vol)
        {
            volume = vol;
            if (sound != nullptr)
                ma_sound_set_volume(sound.get(), vol);
        }

        void SetPitch(float p)
        {
            pitch = p;
            if (sound != nullptr)
                ma_sound_set_pitch(sound.get(), p);
        }

        void SetSpatializationEnabled(bool enabled)
        {
            spatializationEnabled = enabled;
            if (sound != nullptr)
                ma_sound_set_spatialization_enabled(sound.get(), enabled);
        }

        void SetAttenuationModel(int model)
        {
            attenuationModel = model;
            if (sound != nullptr)
                ma_sound_set_attenuation_model(sound.get(), (ma_attenuation_model)model);
        }

        void SetRolloff(float factor)
        {
            rolloff = factor;
            if (sound != nullptr)
                ma_sound_set_rolloff(sound.get(), factor);
        }

        void SetMinDistance(float distance)
        {
            minDistance = distance;
            if (sound != nullptr)
                ma_sound_set_min_distance(sound.get(), distance);
        }

        void SetMaxDistance(float distance)
        {
            maxDistance = distance;
            if (sound != nullptr)
                ma_sound_set_max_distance(sound.get(), distance);
        }

        void SetDopplerFactor(float factor)
        {
            dopplerFactor = factor;
            if (sound != nullptr)
                ma_sound_set_doppler_factor(sound.get(), factor);
        }

        float GetTime() const
        {
            if (sound == nullptr)
                return 0.0f;

            ma_uint64 frames;
            if (ma_sound_get_cursor_in_pcm_frames(sound.get(), &frames) == MA_SUCCESS)
            {
                return (float)frames / clip->sampleRate;
            }
            return 0.0f;
        }

        void SetTime(float seconds)
        {
            if (sound == nullptr || seconds < 0.0f)
                return;

            const float duration = (float)clip->FrameCount() / (float)clip->sampleRate;
            const ma_uint64 frame = std::min<ma_uint64>(
                (ma_uint64)(std::min(seconds, duration) * clip->sampleRate), clip->FrameCount());
            ma_sound_seek_to_pcm_frame(sound.get(), frame);
        }

    };
}

#endif
