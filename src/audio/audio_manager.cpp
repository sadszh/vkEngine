#include <audio/audio_manager.hpp>
#include <audio/audio_clip.hpp>
#include <miniaudio/miniaudio.h>
#include <scene.hpp>
#include <component/audio_source.hpp>
#include <component/audio_listener.hpp>
#include <logger.hpp>

namespace vke_audio
{
    AudioManager *AudioManager::instance = nullptr;

    AudioManager::AudioManager()
        : engine(nullptr)
    {
        ma_engine_config config = ma_engine_config_init();
        config.channels = 2;
        config.sampleRate = 48000;

        engine = new ma_engine;
        ma_result result = ma_engine_init(&config, engine);
        if (result != MA_SUCCESS)
        {
            VKE_LOG_ERROR("AudioManager: failed to initialize ma_engine (error code {})", (int)result);
            delete engine;
            engine = nullptr;
        }
        else
        {
            VKE_LOG_INFO("AudioManager initialized (channels=2, sampleRate=48000)");
        }
    }

    AudioManager::~AudioManager()
    {
        for (auto *sound : sounds)
        {
            ma_sound_uninit(sound);
            delete sound;
        }
        if (engine)
        {
            ma_engine_uninit(engine);
            delete engine;
            engine = nullptr;
        }
    }

    AudioManager *AudioManager::Init()
    {
        if (instance == nullptr)
            instance = new AudioManager();
        return instance;
    }

    void AudioManager::Dispose()
    {
        delete instance;
        instance = nullptr;
    }

    ma_sound *AudioManager::LoadSound(const char *path, uint32_t flags)
    {
        if (GetEngine() == nullptr)
            return nullptr;
        auto *sound = new ma_sound;
        const ma_result result = ma_sound_init_from_file_w(instance->engine, vke_common::PathFromUTF8(path).c_str(), flags, nullptr, nullptr, sound);
        if (result != MA_SUCCESS)
        {
            VKE_LOG_ERROR("AudioManager: failed to load '{}' (error {})", path, (int)result);
            delete sound;
            return nullptr;
        }
        instance->sounds.insert(sound);
        return sound;
    }

    void AudioManager::ReleaseSound(ma_sound *sound)
    {
        if (instance == nullptr || instance->sounds.erase(sound) == 0)
            return;
        ma_sound_uninit(sound);
        delete sound;
    }

    void AudioManager::Reset()
    {
        if (instance == nullptr || instance->engine == nullptr)
            return;
        auto *engine = instance->engine;
        ma_engine_stop(engine);
        for (auto *sound : instance->sounds)
        {
            ma_sound_uninit(sound);
            delete sound;
        }
        instance->sounds.clear();
        ma_engine_set_time_in_pcm_frames(engine, 0);
        ma_engine_set_volume(engine, 1.0f);
        for (ma_uint32 index = 0; index < ma_engine_get_listener_count(engine); ++index)
        {
            ma_engine_listener_set_position(engine, index, 0, 0, 0);
            ma_engine_listener_set_direction(engine, index, 0, 0, -1);
            ma_engine_listener_set_velocity(engine, index, 0, 0, 0);
            ma_engine_listener_set_world_up(engine, index, 0, 1, 0);
            ma_engine_listener_set_enabled(engine, index, MA_TRUE);
        }
        ma_engine_start(engine);
    }

    void AudioManager::Update(float deltaTime)
    {
        if (instance == nullptr || instance->engine == nullptr)
            return;

        vke_common::SceneManager *scene = vke_common::SceneManager::GetInstance();

        auto srcView = scene->registry.view<vke_common::Transform, vke_component::AudioSource>();
        for (auto [entity, transform, src] : srcView.each())
        {
            if (!src.soundInitialized)
                continue;

            if (src.spatializationEnabled)
            {
                glm::vec3 pos = transform.GetGlobalPosition();
                ma_sound_set_position(src.sound, pos.x, pos.y, pos.z);
            }
        }

        auto lisView = scene->registry.view<vke_common::Transform, vke_component::AudioListener>();
        for (auto [entity, transform, listener] : lisView.each())
        {
            if (!listener.enabled || !listener.listenerLoaded)
                continue;

            glm::vec3 pos = transform.GetGlobalPosition();
            glm::quat rot = transform.GetGlobalRotation();
            glm::vec3 forward = rot * glm::vec3(0.0f, 0.0f, -1.0f);
            glm::vec3 up = rot * glm::vec3(0.0f, 1.0f, 0.0f);

            ma_engine_listener_set_position(instance->engine, listener.listenerIndex, pos.x, pos.y, pos.z);
            ma_engine_listener_set_direction(instance->engine, listener.listenerIndex, forward.x, forward.y, forward.z);
            ma_engine_listener_set_world_up(instance->engine, listener.listenerIndex, up.x, up.y, up.z);
        }
    }
}
