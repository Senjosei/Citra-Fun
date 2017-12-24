// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <list>
#include <numeric>
#include <SDL.h>
#include "audio_core/audio_types.h"
#include "audio_core/sdl2_sink.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "core/settings.h"

namespace AudioCore {

struct SDL2Sink::Impl {
    unsigned int sample_rate = 0;

    SDL_AudioDeviceID audio_device_id = 0;

    std::list<std::vector<s16>> queue;

    static void Callback(void* impl_, u8* buffer, int buffer_size_in_bytes);
};

SDL2Sink::SDL2Sink() : impl(std::make_unique<Impl>()) {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        LOG_CRITICAL(Audio_Sink, "SDL_Init(SDL_INIT_AUDIO) failed with: %s", SDL_GetError());
        impl->audio_device_id = 0;
        return;
    }

    SDL_AudioSpec desired_audiospec;
    SDL_zero(desired_audiospec);
    desired_audiospec.format = AUDIO_S16;
    desired_audiospec.channels = 2;
    desired_audiospec.freq = native_sample_rate;
    desired_audiospec.samples = 512;
    desired_audiospec.userdata = impl.get();
    desired_audiospec.callback = &Impl::Callback;

    SDL_AudioSpec obtained_audiospec;
    SDL_zero(obtained_audiospec);

    int device_count = SDL_GetNumAudioDevices(0);
    device_list.clear();
    for (int i = 0; i < device_count; ++i) {
        device_list.push_back(SDL_GetAudioDeviceName(i, 0));
    }

    const char* device = nullptr;

    if (device_count >= 1 && Settings::values.audio_device_id != "auto" &&
        !Settings::values.audio_device_id.empty()) {
        device = Settings::values.audio_device_id.c_str();
    }

    impl->audio_device_id = SDL_OpenAudioDevice(
        device, false, &desired_audiospec, &obtained_audiospec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (impl->audio_device_id <= 0) {
        LOG_CRITICAL(Audio_Sink, "SDL_OpenAudioDevice failed with code %d for device \"%s\"",
                     impl->audio_device_id, Settings::values.audio_device_id.c_str());
        return;
    }

    impl->sample_rate = obtained_audiospec.freq;

    // SDL2 audio devices start out paused, unpause it:
    SDL_PauseAudioDevice(impl->audio_device_id, 0);
}

SDL2Sink::~SDL2Sink() {
    if (impl->audio_device_id <= 0)
        return;

    SDL_CloseAudioDevice(impl->audio_device_id);
}

unsigned int SDL2Sink::GetNativeSampleRate() const {
    if (impl->audio_device_id <= 0)
        return native_sample_rate;

    return impl->sample_rate;
}

std::vector<std::string> SDL2Sink::GetDeviceList() const {
    return device_list;
}

void SDL2Sink::EnqueueSamples(const s16* samples, size_t sample_count) {
    if (impl->audio_device_id <= 0)
        return;

    SDL_LockAudioDevice(impl->audio_device_id);
    impl->queue.emplace_back(samples, samples + sample_count * 2);
    SDL_UnlockAudioDevice(impl->audio_device_id);
}

size_t SDL2Sink::SamplesInQueue() const {
    if (impl->audio_device_id <= 0)
        return 0;

    SDL_LockAudioDevice(impl->audio_device_id);

    size_t total_size = std::accumulate(impl->queue.begin(), impl->queue.end(),
                                        static_cast<size_t>(0), [](size_t sum, const auto& buffer) {
                                            // Division by two because each stereo sample is made of
                                            // two s16.
                                            return sum + buffer.size() / 2;
                                        });

    SDL_UnlockAudioDevice(impl->audio_device_id);

    return total_size;
}

void SDL2Sink::SetDevice(int device_id) {
    this->device_id = device_id;
}

void SDL2Sink::Impl::Callback(void* impl_, u8* buffer, int buffer_size_in_bytes) {
    Impl* impl = reinterpret_cast<Impl*>(impl_);

    size_t remaining_size = static_cast<size_t>(buffer_size_in_bytes) /
                            sizeof(s16); // Keep track of size in 16-bit increments.

    while (remaining_size > 0 && !impl->queue.empty()) {
        if (impl->queue.front().size() <= remaining_size) {
            memcpy(buffer, impl->queue.front().data(), impl->queue.front().size() * sizeof(s16));
            buffer += impl->queue.front().size() * sizeof(s16);
            remaining_size -= impl->queue.front().size();
            impl->queue.pop_front();
        } else {
            memcpy(buffer, impl->queue.front().data(), remaining_size * sizeof(s16));
            buffer += remaining_size * sizeof(s16);
            impl->queue.front().erase(impl->queue.front().begin(),
                                      impl->queue.front().begin() + remaining_size);
            remaining_size = 0;
        }
    }

    if (remaining_size > 0) {
        memset(buffer, 0, remaining_size * sizeof(s16));
    }
}

} // namespace AudioCore
