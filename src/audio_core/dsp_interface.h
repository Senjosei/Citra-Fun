// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <vector>
#include "audio_core/audio_types.h"
#include "audio_core/time_stretch.h"
#include "common/common_types.h"
#include "core/memory.h"

namespace AudioCore {

class Sink;

class DspInterface {
public:
    DspInterface();
    virtual ~DspInterface();

    DspInterface(const DspInterface&) = delete;
    DspInterface(DspInterface&&) = delete;
    DspInterface& operator=(const DspInterface&) = delete;
    DspInterface& operator=(DspInterface&&) = delete;

    /// Get the state of the DSP
    virtual DspState GetDspState() const = 0;

    /**
     * Reads `length` bytes from the DSP pipe identified with `pipe_number`.
     * @note Can read up to the maximum value of a u16 in bytes (65,535).
     * @note IF an error is encoutered with either an invalid `pipe_number` or `length` value, an
     * empty vector will be returned.
     * @note IF `length` is set to 0, an empty vector will be returned.
     * @note IF `length` is greater than the amount of data available, this function will only read
     * the available amount.
     * @param pipe_number a `DspPipe`
     * @param length the number of bytes to read. The max is 65,535 (max of u16).
     * @returns a vector of bytes from the specified pipe. On error, will be empty.
     */
    virtual std::vector<u8> PipeRead(DspPipe pipe_number, u32 length) = 0;

    /**
     * How much data is left in pipe
     * @param pipe_number The Pipe ID
     * @return The amount of data remaning in the pipe. This is the maximum length PipeRead will
     * return.
     */
    virtual size_t GetPipeReadableSize(DspPipe pipe_number) const = 0;

    /**
     * Write to a DSP pipe.
     * @param pipe_number The Pipe ID
     * @param buffer The data to write to the pipe.
     */
    virtual void PipeWrite(DspPipe pipe_number, const std::vector<u8>& buffer) = 0;

    /// Returns a reference to the array backing DSP memory
    virtual std::array<u8, Memory::DSP_RAM_SIZE>& GetDspMemory() = 0;

    /// Select the sink to use based on sink id.
    void SetSink(const std::string& sink_id);
    /// Get the current sink
    Sink& GetSink();
    /// Enable/Disable audio stretching.
    void EnableStretching(bool enable);

protected:
    void OutputFrame(const StereoFrame16& frame);

private:
    void FlushResidualStretcherAudio();

    std::unique_ptr<Sink> sink;
    bool perform_time_stretching = false;
    TimeStretcher time_stretcher;
};

} // namespace AudioCore
