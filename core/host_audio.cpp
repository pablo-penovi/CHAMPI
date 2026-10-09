#include "host_audio.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

#include <samplerate.h>

#include "daisycola/host.h"

namespace champi
{
namespace
{
constexpr size_t kFwChannels = daisycola::kMaxAudioChannels;

// Frames the output FIFO keeps beyond one host buffer, for the jitter in how many frames each
// conversion makes.
constexpr size_t kSlack = 8;

// Feeds `frames` interleaved frames through a converter and appends what comes out to `out`,
// which has room for `cap` frames. Returns the frames written.
size_t Convert(SRC_STATE* src, double ratio, const float* in, size_t frames, float* out, size_t cap)
{
    size_t made = 0;
    while(true)
    {
        SRC_DATA data{};
        data.data_in       = in;
        data.input_frames  = long(frames);
        data.data_out      = out;
        data.output_frames = long(cap - made);
        data.src_ratio     = ratio;
        if(src_process(src, &data) != 0)
            return made;
        made += size_t(data.output_frames_gen);
        const int ch = src_get_channels(src);
        in += data.input_frames_used * ch;
        out += data.output_frames_gen * ch;
        frames -= size_t(data.input_frames_used);
        if(frames == 0 || made >= cap || (data.input_frames_used == 0 && data.output_frames_gen == 0))
            return made;
    }
}
} // namespace

HostAudio::HostAudio(Process process) : process_(process) {}

HostAudio::~HostAudio()
{
    Release();
}

void HostAudio::Release()
{
    if(in_src_)
        src_delete(in_src_);
    if(out_src_)
        src_delete(out_src_);
    in_src_  = nullptr;
    out_src_ = nullptr;
}

void HostAudio::Prepare(double host_rate, size_t max_frames)
{
    Release();
    host_rate_  = host_rate;
    max_frames_ = max_frames;
    primed_     = false;
    if(host_rate == kFirmwareRate)
        return;

    int error = 0;
    in_src_   = src_new(SRC_SINC_FASTEST, int(kHostInputs), &error);
    if(in_src_)
        out_src_ = src_new(SRC_SINC_FASTEST, int(kHostOutputs), &error);
    if(!out_src_)
    {
        Release();
        throw std::runtime_error(std::string("libsamplerate: ") + src_strerror(error));
    }

    const double in_ratio = kFirmwareRate / host_rate;
    fw_cap_               = size_t(std::ceil(max_frames * in_ratio)) + 16;
    host_in_.assign(max_frames * kHostInputs, 0.f);
    fw_in_.assign(fw_cap_ * kHostInputs, 0.f);
    fw_planar_.assign(2 * kFwChannels * fw_cap_, 0.f);
    fw_out_.assign(fw_cap_ * kHostOutputs, 0.f);
    resampled_.assign((size_t(std::ceil(fw_cap_ / in_ratio)) + 16) * kHostOutputs, 0.f);
    fifo_frames_ = 4 * max_frames + 64;
    fifo_.assign(fifo_frames_ * kHostOutputs, 0.f);
    fifo_read_ = fifo_fill_ = 0;
}

void HostAudio::Run(const float* const* in, float* const* out, size_t frames)
{
    const auto start = std::chrono::steady_clock::now();

    if(Resampling())
        RunResampled(in, out, frames);
    else
    {
        // The firmware's channel order: mic, unused, line L, line R in; phones, master out.
        const float* fw_in[kFwChannels]  = {in ? in[0] : nullptr, nullptr, in ? in[1] : nullptr,
                                            in ? in[2] : nullptr};
        float*       fw_out[kFwChannels] = {out[2], out[3], out[0], out[1]};
        const bool   ok                  = process_(fw_in, fw_out, frames);
        if(ok)
            ever_ok_ = true;
        else if(ever_ok_)
            late_.fetch_add(1, std::memory_order_relaxed);
    }

    const double spent = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const float  load  = float(spent * host_rate_ / double(std::max<size_t>(frames, 1)));
    const float  avg   = average_.load(std::memory_order_relaxed);
    average_.store(avg + 0.05f * (load - avg), std::memory_order_relaxed);
    float peak = peak_.load(std::memory_order_relaxed);
    while(load > peak && !peak_.compare_exchange_weak(peak, load, std::memory_order_relaxed))
    {
    }
}

void HostAudio::RunResampled(const float* const* in, float* const* out, size_t host_frames)
{
    const size_t frames = std::min(host_frames, max_frames_);

    // Host input to 48 kHz.
    for(size_t i = 0; i < frames; i++)
        for(size_t c = 0; c < kHostInputs; c++)
            host_in_[i * kHostInputs + c] = in && in[c] ? in[c][i] : 0.f;
    const size_t n = Convert(in_src_, kFirmwareRate / host_rate_, host_in_.data(), frames,
                             fw_in_.data(), fw_cap_);

    // The firmware, in its own channel order.
    float* const planar = fw_planar_.data();
    const float* fw_in[kFwChannels];
    float*       fw_out[kFwChannels];
    for(size_t c = 0; c < kFwChannels; c++)
    {
        fw_in[c]  = planar + c * fw_cap_;
        fw_out[c] = planar + (kFwChannels + c) * fw_cap_;
    }
    for(size_t i = 0; i < n; i++)
    {
        planar[0 * fw_cap_ + i] = fw_in_[i * kHostInputs + 0];
        planar[1 * fw_cap_ + i] = 0.f;
        planar[2 * fw_cap_ + i] = fw_in_[i * kHostInputs + 1];
        planar[3 * fw_cap_ + i] = fw_in_[i * kHostInputs + 2];
    }
    if(n > 0)
    {
        const bool ok = process_(fw_in, fw_out, n);
        if(ok)
            ever_ok_ = true;
        else if(ever_ok_)
            late_.fetch_add(1, std::memory_order_relaxed);
    }

    // Back to the host rate, in the host's order: master L/R, phones L/R.
    static constexpr size_t kOrder[kHostOutputs] = {2, 3, 0, 1};
    for(size_t i = 0; i < n; i++)
        for(size_t c = 0; c < kHostOutputs; c++)
            fw_out_[i * kHostOutputs + c] = fw_out[kOrder[c]][i];
    const size_t m = Convert(out_src_, host_rate_ / kFirmwareRate, fw_out_.data(), n,
                             resampled_.data(), resampled_.size() / kHostOutputs);

    for(size_t i = 0; i < m && fifo_fill_ < fifo_frames_; i++, fifo_fill_++)
    {
        const size_t at = (fifo_read_ + fifo_fill_) % fifo_frames_;
        std::memcpy(&fifo_[at * kHostOutputs], &resampled_[i * kHostOutputs],
                    kHostOutputs * sizeof(float));
    }

    // Wait until the converters have filled the pipe before playing, then keep it flowing.
    if(!primed_ && fifo_fill_ >= frames + kSlack)
        primed_ = true;
    size_t played = 0;
    if(primed_)
    {
        for(; played < frames && fifo_fill_ > 0; played++, fifo_fill_--)
        {
            for(size_t c = 0; c < kHostOutputs; c++)
                out[c][played] = fifo_[fifo_read_ * kHostOutputs + c];
            fifo_read_ = (fifo_read_ + 1) % fifo_frames_;
        }
        if(played < frames)
            dropouts_.fetch_add(1, std::memory_order_relaxed);
    }
    for(size_t c = 0; c < kHostOutputs; c++)
        std::fill(out[c] + played, out[c] + host_frames, 0.f);
}

AudioLoad HostAudio::Load()
{
    return {average_.load(std::memory_order_relaxed), peak_.exchange(0.f, std::memory_order_relaxed),
            late_.load(std::memory_order_relaxed), dropouts_.load(std::memory_order_relaxed)};
}

} // namespace champi
