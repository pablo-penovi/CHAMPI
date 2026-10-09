// The bridge between a host's audio callback and the firmware's audio, which runs at 48 kHz in
// 24-frame blocks on daisycola's host clock.
//
// It maps the host's channels onto the firmware's, converts the sample rate with libsamplerate
// when the host doesn't run at 48 kHz, and measures how much of each cycle the firmware takes.
//
// Host inputs are mic, line L and line R; the firmware sees them as its inputs 1, 3 and 4 (its
// input 2 is unused). Host outputs are master L/R and phones L/R, which are the firmware's
// outputs 3/4 and 1/2.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

typedef struct SRC_STATE_tag SRC_STATE;

namespace champi
{
constexpr size_t kHostInputs  = 3;
constexpr size_t kHostOutputs = 4;

struct AudioLoad
{
    float    average;     // share of the cycle spent in Run, smoothed over about 20 cycles
    float    peak;        // highest share since the last Load call
    uint64_t late_blocks; // cycles the firmware didn't finish in time, once it was running
    uint64_t dropouts;    // cycles the resampled output came up short
};

class HostAudio
{
  public:
    static constexpr double kFirmwareRate = 48000.0;

    /** Runs firmware audio: daisycola::ProcessAudio, or a stand-in for tests. */
    using Process = bool (*)(const float* const* in, float* const* out, size_t frames);

    explicit HostAudio(Process process);
    ~HostAudio();
    HostAudio(const HostAudio&)            = delete;
    HostAudio& operator=(const HostAudio&) = delete;

    /** Sets the host's rate and largest buffer. Allocates, so not from the audio thread. */
    void Prepare(double host_rate, size_t max_frames);

    /** True if the host rate isn't 48 kHz. */
    bool Resampling() const { return in_src_ != nullptr; }

    /** One host cycle: kHostInputs input and kHostOutputs output channels of `frames` each.
     *  A null input is silence. Call from the audio thread only. */
    void Run(const float* const* in, float* const* out, size_t frames);

    /** The load figures. Any thread; resets the peak. */
    AudioLoad Load();

  private:
    void RunResampled(const float* const* in, float* const* out, size_t host_frames);
    void Release();

    Process process_;
    double  host_rate_  = kFirmwareRate;
    size_t  max_frames_ = 0;
    bool    ever_ok_    = false;

    // Rate conversion: host inputs to 48 kHz, firmware outputs back to the host rate.
    SRC_STATE*         in_src_  = nullptr;
    SRC_STATE*         out_src_ = nullptr;
    size_t             fw_cap_  = 0; // most firmware frames one host cycle can make
    std::vector<float> host_in_;     // interleaved, kHostInputs
    std::vector<float> fw_in_;       // interleaved, kHostInputs
    std::vector<float> fw_planar_;   // 2 * kMaxAudioChannels planar channels of fw_cap_
    std::vector<float> fw_out_;      // interleaved, kHostOutputs
    std::vector<float> resampled_;   // interleaved, kHostOutputs

    // Resampled output waiting for the host, interleaved kHostOutputs.
    std::vector<float> fifo_;
    size_t             fifo_frames_ = 0; // capacity
    size_t             fifo_read_   = 0;
    size_t             fifo_fill_   = 0;
    bool               primed_      = false;

    std::atomic<float>    average_{0.f};
    std::atomic<float>    peak_{0.f};
    std::atomic<uint64_t> late_{0};
    std::atomic<uint64_t> dropouts_{0};
};

} // namespace champi
