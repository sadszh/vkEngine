#ifndef AUDIO_LISTENER_H
#define AUDIO_LISTENER_H

#include <json_validation.hpp>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace vke_component
{
    struct AudioListenerData
    {
        bool enabled = true;

        // Call before constructing from JSON; asset checks happen in LoadAssets where applicable.
        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            return Object(json).Booleans({"enabled"}).Result();
        }

        AudioListenerData() = default;
        AudioListenerData(const nlohmann::json &json) : enabled(json.value("enabled", true)) {}

        nlohmann::json ToJSON() const
        {
            return {{"type", "audioListener"}, {"enabled", enabled}};
        }
    };

    class AudioListener
    {
    public:
        int32_t listenerIndex = 0;
        bool enabled = true;

        bool listenerLoaded = false;

        AudioListener(const AudioListener &) = delete;
        AudioListener &operator=(const AudioListener &) = delete;
        AudioListener(AudioListener &&) noexcept = default;
        AudioListener &operator=(AudioListener &&) noexcept = default;

        AudioListener(const AudioListenerData &componentData) : enabled(componentData.enabled), listenerLoaded(true) {}

        void FillData(AudioListenerData &data) const
        {
            data.enabled = enabled;
        }

        void Unload()
        {
            listenerLoaded = false;
        }
    };
}

#endif
