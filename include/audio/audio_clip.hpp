#ifndef AUDIO_CLIP_H
#define AUDIO_CLIP_H

#include <cstdint>
#include <memory>
#include <miniaudio/miniaudio.h>
#include <common.hpp>

namespace vke_audio
{
    class AudioClip
    {
    private:
        struct PCMDeleter
        {
            void operator()(float *samples) const
            {
                ma_free(samples, nullptr);
            }
        };

    public:
        vke_common::AssetHandle handle = 0;

        std::unique_ptr<float[], PCMDeleter> samples;
        uint64_t frameCount = 0;
        uint32_t channels = 0;
        uint32_t sampleRate = 0;

        AudioClip() = default;
        explicit AudioClip(vke_common::AssetHandle h) : handle(h) {}

        uint64_t FrameCount() const { return frameCount; }
    };
}

#endif
