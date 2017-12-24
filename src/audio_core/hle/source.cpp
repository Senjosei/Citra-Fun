// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include "audio_core/codec.h"
#include "audio_core/hle/common.h"
#include "audio_core/hle/source.h"
#include "audio_core/interpolate.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "core/memory.h"

namespace AudioCore {
namespace HLE {

SourceStatus::Status Source::Tick(SourceConfiguration::Configuration& config,
                                  const s16_le (&adpcm_coeffs)[16]) {
    ParseConfig(config, adpcm_coeffs);

    if (state.enabled) {
        GenerateFrame();
    }

    return GetCurrentStatus();
}

void Source::MixInto(QuadFrame32& dest, size_t intermediate_mix_id) const {
    if (!state.enabled)
        return;

    const std::array<float, 4>& gains = state.gain.at(intermediate_mix_id);
    for (size_t samplei = 0; samplei < samples_per_frame; samplei++) {
        // Conversion from stereo (current_frame) to quadraphonic (dest) occurs here.
        dest[samplei][0] += static_cast<s32>(gains[0] * current_frame[samplei][0]);
        dest[samplei][1] += static_cast<s32>(gains[1] * current_frame[samplei][1]);
        dest[samplei][2] += static_cast<s32>(gains[2] * current_frame[samplei][0]);
        dest[samplei][3] += static_cast<s32>(gains[3] * current_frame[samplei][1]);
    }
}

void Source::Reset() {
    current_frame.fill({});
    state = {};
}

void Source::ParseConfig(SourceConfiguration::Configuration& config,
                         const s16_le (&adpcm_coeffs)[16]) {
    if (!config.dirty_raw) {
        return;
    }

    if (config.reset_flag) {
        config.reset_flag.Assign(0);
        Reset();
        LOG_TRACE(Audio_DSP, "source_id=%zu reset", source_id);
    }

    if (config.partial_reset_flag) {
        config.partial_reset_flag.Assign(0);
        state.input_queue = std::priority_queue<Buffer, std::vector<Buffer>, BufferOrder>{};
        LOG_TRACE(Audio_DSP, "source_id=%zu partial_reset", source_id);
    }

    if (config.enable_dirty) {
        config.enable_dirty.Assign(0);
        state.enabled = config.enable != 0;
        LOG_TRACE(Audio_DSP, "source_id=%zu enable=%d", source_id, state.enabled);
    }

    if (config.sync_dirty) {
        config.sync_dirty.Assign(0);
        state.sync = config.sync;
        LOG_TRACE(Audio_DSP, "source_id=%zu sync=%u", source_id, state.sync);
    }

    if (config.rate_multiplier_dirty) {
        config.rate_multiplier_dirty.Assign(0);
        state.rate_multiplier = config.rate_multiplier;
        LOG_TRACE(Audio_DSP, "source_id=%zu rate=%f", source_id, state.rate_multiplier);

        if (state.rate_multiplier <= 0) {
            LOG_ERROR(Audio_DSP, "Was given an invalid rate multiplier: source_id=%zu rate=%f",
                      source_id, state.rate_multiplier);
            state.rate_multiplier = 1.0f;
            // Note: Actual firmware starts producing garbage if this occurs.
        }
    }

    if (config.adpcm_coefficients_dirty) {
        config.adpcm_coefficients_dirty.Assign(0);
        std::transform(adpcm_coeffs, adpcm_coeffs + state.adpcm_coeffs.size(),
                       state.adpcm_coeffs.begin(),
                       [](const auto& coeff) { return static_cast<s16>(coeff); });
        LOG_TRACE(Audio_DSP, "source_id=%zu adpcm update", source_id);
    }

    if (config.gain_0_dirty) {
        config.gain_0_dirty.Assign(0);
        std::transform(config.gain[0], config.gain[0] + state.gain[0].size(), state.gain[0].begin(),
                       [](const auto& coeff) { return static_cast<float>(coeff); });
        LOG_TRACE(Audio_DSP, "source_id=%zu gain 0 update", source_id);
    }

    if (config.gain_1_dirty) {
        config.gain_1_dirty.Assign(0);
        std::transform(config.gain[1], config.gain[1] + state.gain[1].size(), state.gain[1].begin(),
                       [](const auto& coeff) { return static_cast<float>(coeff); });
        LOG_TRACE(Audio_DSP, "source_id=%zu gain 1 update", source_id);
    }

    if (config.gain_2_dirty) {
        config.gain_2_dirty.Assign(0);
        std::transform(config.gain[2], config.gain[2] + state.gain[2].size(), state.gain[2].begin(),
                       [](const auto& coeff) { return static_cast<float>(coeff); });
        LOG_TRACE(Audio_DSP, "source_id=%zu gain 2 update", source_id);
    }

    if (config.filters_enabled_dirty) {
        config.filters_enabled_dirty.Assign(0);
        state.filters.Enable(config.simple_filter_enabled.ToBool(),
                             config.biquad_filter_enabled.ToBool());
        LOG_TRACE(Audio_DSP, "source_id=%zu enable_simple=%hu enable_biquad=%hu", source_id,
                  config.simple_filter_enabled.Value(), config.biquad_filter_enabled.Value());
    }

    if (config.simple_filter_dirty) {
        config.simple_filter_dirty.Assign(0);
        state.filters.Configure(config.simple_filter);
        LOG_TRACE(Audio_DSP, "source_id=%zu simple filter update", source_id);
    }

    if (config.biquad_filter_dirty) {
        config.biquad_filter_dirty.Assign(0);
        state.filters.Configure(config.biquad_filter);
        LOG_TRACE(Audio_DSP, "source_id=%zu biquad filter update", source_id);
    }

    if (config.interpolation_dirty) {
        config.interpolation_dirty.Assign(0);
        state.interpolation_mode = config.interpolation_mode;
        LOG_TRACE(Audio_DSP, "source_id=%zu interpolation_mode=%zu", source_id,
                  static_cast<size_t>(state.interpolation_mode));
    }

    if (config.format_dirty || config.embedded_buffer_dirty) {
        config.format_dirty.Assign(0);
        state.format = config.format;
        LOG_TRACE(Audio_DSP, "source_id=%zu format=%zu", source_id,
                  static_cast<size_t>(state.format));
    }

    if (config.mono_or_stereo_dirty || config.embedded_buffer_dirty) {
        config.mono_or_stereo_dirty.Assign(0);
        state.mono_or_stereo = config.mono_or_stereo;
        LOG_TRACE(Audio_DSP, "source_id=%zu mono_or_stereo=%zu", source_id,
                  static_cast<size_t>(state.mono_or_stereo));
    }

    u32_dsp play_position = {};
    if (config.play_position_dirty && config.play_position != 0) {
        config.play_position_dirty.Assign(0);
        play_position = config.play_position;
        // play_position applies only to the embedded buffer, and defaults to 0 w/o a dirty bit
        // This will be the starting sample for the first time the buffer is played.
    }

    if (config.embedded_buffer_dirty) {
        config.embedded_buffer_dirty.Assign(0);
        state.input_queue.emplace(Buffer{
            config.physical_address,
            config.length,
            static_cast<u8>(config.adpcm_ps),
            {config.adpcm_yn[0], config.adpcm_yn[1]},
            config.adpcm_dirty.ToBool(),
            config.is_looping.ToBool(),
            config.buffer_id,
            state.mono_or_stereo,
            state.format,
            false,
            play_position,
            false,
        });
        LOG_TRACE(Audio_DSP, "enqueuing embedded addr=0x%08x len=%u id=%hu start=%u",
                  config.physical_address, config.length, config.buffer_id,
                  static_cast<u32>(config.play_position));
    }

    if (config.loop_related_dirty && config.loop_related != 0) {
        config.loop_related_dirty.Assign(0);
        LOG_WARNING(Audio_DSP, "Unhandled complex loop with loop_related=0x%08x",
                    static_cast<u32>(config.loop_related));
    }

    if (config.buffer_queue_dirty) {
        config.buffer_queue_dirty.Assign(0);
        for (size_t i = 0; i < 4; i++) {
            if (config.buffers_dirty & (1 << i)) {
                const auto& b = config.buffers[i];
                state.input_queue.emplace(Buffer{
                    b.physical_address,
                    b.length,
                    static_cast<u8>(b.adpcm_ps),
                    {b.adpcm_yn[0], b.adpcm_yn[1]},
                    b.adpcm_dirty != 0,
                    b.is_looping != 0,
                    b.buffer_id,
                    state.mono_or_stereo,
                    state.format,
                    true,
                    {}, // 0 in u32_dsp
                    false,
                });
                LOG_TRACE(Audio_DSP, "enqueuing queued %zu addr=0x%08x len=%u id=%hu", i,
                          b.physical_address, b.length, b.buffer_id);
            }
        }
        config.buffers_dirty = 0;
    }

    if (config.dirty_raw) {
        LOG_DEBUG(Audio_DSP, "source_id=%zu remaining_dirty=%x", source_id, config.dirty_raw);
    }

    config.dirty_raw = 0;
}

void Source::GenerateFrame() {
    current_frame.fill({});

    if (state.current_buffer.empty() && !DequeueBuffer()) {
        state.enabled = false;
        state.buffer_update = true;
        state.current_buffer_id = 0;
        return;
    }

    size_t frame_position = 0;

    state.current_sample_number = state.next_sample_number;
    while (frame_position < current_frame.size()) {
        if (state.current_buffer.empty() && !DequeueBuffer()) {
            break;
        }

        switch (state.interpolation_mode) {
        case InterpolationMode::None:
            AudioInterp::None(state.interp_state, state.current_buffer, state.rate_multiplier,
                              current_frame, frame_position);
            break;
        case InterpolationMode::Linear:
            AudioInterp::Linear(state.interp_state, state.current_buffer, state.rate_multiplier,
                                current_frame, frame_position);
            break;
        case InterpolationMode::Polyphase:
            // TODO(merry): Implement polyphase interpolation
            LOG_DEBUG(Audio_DSP, "Polyphase interpolation unimplemented; falling back to linear");
            AudioInterp::Linear(state.interp_state, state.current_buffer, state.rate_multiplier,
                                current_frame, frame_position);
            break;
        default:
            UNIMPLEMENTED();
            break;
        }
    }
    state.next_sample_number += static_cast<u32>(frame_position);

    state.filters.ProcessFrame(current_frame);
}

bool Source::DequeueBuffer() {
    ASSERT_MSG(state.current_buffer.empty(),
               "Shouldn't dequeue; we still have data in current_buffer");

    if (state.input_queue.empty())
        return false;

    Buffer buf = state.input_queue.top();
    state.input_queue.pop();

    if (buf.adpcm_dirty) {
        state.adpcm_state.yn1 = buf.adpcm_yn[0];
        state.adpcm_state.yn2 = buf.adpcm_yn[1];
    }

    const u8* const memory = Memory::GetPhysicalPointer(buf.physical_address);
    if (memory) {
        const unsigned num_channels = buf.mono_or_stereo == MonoOrStereo::Stereo ? 2 : 1;
        switch (buf.format) {
        case Format::PCM8:
            state.current_buffer = Codec::DecodePCM8(num_channels, memory, buf.length);
            break;
        case Format::PCM16:
            state.current_buffer = Codec::DecodePCM16(num_channels, memory, buf.length);
            break;
        case Format::ADPCM:
            DEBUG_ASSERT(num_channels == 1);
            state.current_buffer =
                Codec::DecodeADPCM(memory, buf.length, state.adpcm_coeffs, state.adpcm_state);
            break;
        default:
            UNIMPLEMENTED();
            break;
        }
    } else {
        LOG_WARNING(Audio_DSP,
                    "source_id=%zu buffer_id=%hu length=%u: Invalid physical address 0x%08X",
                    source_id, buf.buffer_id, buf.length, buf.physical_address);
        state.current_buffer.clear();
        return true;
    }

    // the first playthrough starts at play_position, loops start at the beginning of the buffer
    state.current_sample_number = (!buf.has_played) ? buf.play_position : 0;
    state.next_sample_number = state.current_sample_number;
    state.current_buffer_id = buf.buffer_id;
    state.buffer_update = buf.from_queue && !buf.has_played;

    if (buf.is_looping) {
        buf.has_played = true;
        state.input_queue.push(buf);
    }

    LOG_TRACE(Audio_DSP, "source_id=%zu buffer_id=%hu from_queue=%s current_buffer.size()=%zu",
              source_id, buf.buffer_id, buf.from_queue ? "true" : "false",
              state.current_buffer.size());
    return true;
}

SourceStatus::Status Source::GetCurrentStatus() {
    SourceStatus::Status ret;

    // Applications depend on the correct emulation of
    // current_buffer_id_dirty and current_buffer_id to synchronise
    // audio with video.
    ret.is_enabled = state.enabled;
    ret.current_buffer_id_dirty = state.buffer_update ? 1 : 0;
    state.buffer_update = false;
    ret.current_buffer_id = state.current_buffer_id;
    ret.buffer_position = state.current_sample_number;
    ret.sync = state.sync;

    return ret;
}

} // namespace HLE
} // namespace AudioCore
