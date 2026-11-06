#ifndef TIME_H
#define TIME_H

#include <render/render_common.hpp>

namespace vke_common
{
    class TimeManager
    {
    private:
        static TimeManager *instance;
        TimeManager()
            : frameStartTime(static_cast<float>(glfwGetTime())),
              prevFrameStartTime(frameStartTime),
              deltaTime(0.0f)
        {
        }
        ~TimeManager() {}
        TimeManager(const TimeManager &);
        TimeManager &operator=(const TimeManager);

    public:
        float frameStartTime;
        float prevFrameStartTime;
        float deltaTime;

        static TimeManager *GetInstance()
        {
            return instance;
        }

        static TimeManager *Init()
        {
            if (instance == nullptr)
                instance = new TimeManager();
            return instance;
        }

        static void Dispose()
        {
            if (instance == nullptr)
                return;
            delete instance;
            instance = nullptr;
        }

        static void Update()
        {
            instance->prevFrameStartTime = instance->frameStartTime;
            instance->frameStartTime = static_cast<float>(glfwGetTime());
            instance->deltaTime = instance->frameStartTime - instance->prevFrameStartTime;
        }

        static void Reset()
        {
            instance->frameStartTime = static_cast<float>(glfwGetTime());
            instance->prevFrameStartTime = instance->frameStartTime;
            instance->deltaTime = 0.0f;
        }

        static float GetTime()
        {
            return instance->frameStartTime;
        }

        static float GetDeltaTime()
        {
            return instance->deltaTime;
        }

        static float GetPreviousFrameTime()
        {
            return instance->prevFrameStartTime;
        }
    };
}

#endif
