// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include <cmath>
#include <vector>
#include <SoundTouch.h>
#include "audio_core/audio_types.h"
#include "audio_core/time_stretch.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "common/math_util.h"

using steady_clock = std::chrono::steady_clock;

namespace AudioCore {

constexpr double MIN_RATIO = 0.1;
constexpr double MAX_RATIO = 100.0;

static double ClampRatio(double ratio) {
    return MathUtil::Clamp(ratio, MIN_RATIO, MAX_RATIO);
}

constexpr double MIN_DELAY_TIME = 0.05;            // Units: seconds
constexpr double MAX_DELAY_TIME = 0.25;            // Units: seconds
constexpr size_t DROP_FRAMES_SAMPLE_DELAY = 16000; // Units: samples

constexpr double SMOOTHING_FACTOR = 0.007;

struct TimeStretcher::Impl {
    soundtouch::SoundTouch soundtouch;

    steady_clock::time_point frame_timer = steady_clock::now();
    size_t samples_queued = 0;

    double smoothed_ratio = 1.0;

    double sample_rate = static_cast<double>(native_sample_rate);
};

std::vector<s16> TimeStretcher::Process(size_t samples_in_queue) {
    // This is a very simple algorithm without any fancy control theory. It works and is stable.

    double ratio = CalculateCurrentRatio();
    ratio = CorrectForUnderAndOverflow(ratio, samples_in_queue);
    impl->smoothed_ratio =
        (1.0 - SMOOTHING_FACTOR) * impl->smoothed_ratio + SMOOTHING_FACTOR * ratio;
    impl->smoothed_ratio = ClampRatio(impl->smoothed_ratio);

    // SoundTouch's tempo definition the inverse of our ratio definition.
    impl->soundtouch.setTempo(1.0 / impl->smoothed_ratio);

    std::vector<s16> samples = GetSamples();
    if (samples_in_queue >= DROP_FRAMES_SAMPLE_DELAY) {
        samples.clear();
        LOG_DEBUG(Audio, "Dropping frames!");
    }
    return samples;
}

TimeStretcher::TimeStretcher() : impl(std::make_unique<Impl>()) {
    impl->soundtouch.setPitch(1.0);
    impl->soundtouch.setChannels(2);
    impl->soundtouch.setSampleRate(native_sample_rate);
    Reset();
}

TimeStretcher::~TimeStretcher() {
    impl->soundtouch.clear();
}

void TimeStretcher::SetOutputSampleRate(unsigned int sample_rate) {
    impl->sample_rate = static_cast<double>(sample_rate);
    impl->soundtouch.setRate(static_cast<double>(native_sample_rate) / impl->sample_rate);
}

void TimeStretcher::AddSamples(const s16* buffer, size_t num_samples) {
    impl->soundtouch.putSamples(buffer, static_cast<uint>(num_samples));
    impl->samples_queued += num_samples;
}

void TimeStretcher::Flush() {
    impl->soundtouch.flush();
}

void TimeStretcher::Reset() {
    impl->soundtouch.setTempo(1.0);
    impl->soundtouch.clear();
    impl->smoothed_ratio = 1.0;
    impl->frame_timer = steady_clock::now();
    impl->samples_queued = 0;
    SetOutputSampleRate(native_sample_rate);
}

double TimeStretcher::CalculateCurrentRatio() {
    const steady_clock::time_point now = steady_clock::now();
    const std::chrono::duration<double> duration = now - impl->frame_timer;

    const double expected_time =
        static_cast<double>(impl->samples_queued) / static_cast<double>(native_sample_rate);
    const double actual_time = duration.count();

    double ratio;
    if (expected_time != 0) {
        ratio = ClampRatio(actual_time / expected_time);
    } else {
        ratio = impl->smoothed_ratio;
    }

    impl->frame_timer = now;
    impl->samples_queued = 0;

    return ratio;
}

double TimeStretcher::CorrectForUnderAndOverflow(double ratio, size_t sample_delay) const {
    const size_t min_sample_delay = static_cast<size_t>(MIN_DELAY_TIME * impl->sample_rate);
    const size_t max_sample_delay = static_cast<size_t>(MAX_DELAY_TIME * impl->sample_rate);

    if (sample_delay < min_sample_delay) {
        // Make the ratio bigger.
        ratio = ratio > 1.0 ? ratio * ratio : sqrt(ratio);
    } else if (sample_delay > max_sample_delay) {
        // Make the ratio smaller.
        ratio = ratio > 1.0 ? sqrt(ratio) : ratio * ratio;
    }

    return ClampRatio(ratio);
}

std::vector<s16> TimeStretcher::GetSamples() {
    uint available = impl->soundtouch.numSamples();

    std::vector<s16> output(static_cast<size_t>(available) * 2);

    impl->soundtouch.receiveSamples(output.data(), available);

    return output;
}

} // namespace AudioCore
