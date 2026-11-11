#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

struct ma_engine;


namespace vke_audio
{
    class AudioManager
    {
    private:
        static AudioManager *instance;
        ma_engine *engine;

        AudioManager();
        ~AudioManager();
        AudioManager(const AudioManager &) = delete;
        AudioManager &operator=(const AudioManager &) = delete;

    public:
        static AudioManager *GetInstance() { return instance; }
        static AudioManager *Init();
        static void Dispose();
        static void Reset();

        static void Update(float deltaTime);

        static ma_engine *GetEngine() { return instance ? instance->engine : nullptr; }
    };
}

#endif
