// The host-side pieces of the app: the MIDI splitter and the audio adapter, with a stand-in for
// the firmware's audio.
#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "host_audio.h"
#include "midi_splitter.h"

namespace champi
{
namespace
{
std::vector<std::vector<uint8_t>> Split(const std::vector<uint8_t>& bytes)
{
    MidiSplitter                      splitter;
    std::vector<std::vector<uint8_t>> messages;
    for(uint8_t b : bytes)
        if(splitter.Feed(b))
            messages.emplace_back(splitter.Data(), splitter.Data() + splitter.Size());
    return messages;
}

using Messages = std::vector<std::vector<uint8_t>>;

TEST(MidiSplitter, SplitsChannelMessages)
{
    EXPECT_EQ(Split({0x90, 60, 100, 0x80, 60, 0, 0xb3, 20, 64, 0xc0, 5}),
              (Messages{{0x90, 60, 100}, {0x80, 60, 0}, {0xb3, 20, 64}, {0xc0, 5}}));
}

TEST(MidiSplitter, ExpandsRunningStatus)
{
    EXPECT_EQ(Split({0x90, 60, 100, 62, 100, 60, 0}),
              (Messages{{0x90, 60, 100}, {0x90, 62, 100}, {0x90, 60, 0}}));
}

TEST(MidiSplitter, LetsRealTimeBytesThrough)
{
    EXPECT_EQ(Split({0x90, 0xf8, 60, 0xfe, 100, 62, 0xf8, 100}),
              (Messages{{0xf8}, {0xfe}, {0x90, 60, 100}, {0xf8}, {0x90, 62, 100}}));
}

TEST(MidiSplitter, DropsSysExAndStrayData)
{
    EXPECT_EQ(Split({60, 100, 0xf0, 1, 2, 3, 0xf7, 61, 0x90, 62, 100}), (Messages{{0x90, 62, 100}}));
}

TEST(MidiSplitter, SystemCommonCancelsRunningStatus)
{
    EXPECT_EQ(Split({0x90, 60, 100, 0xf3, 2, 61, 100, 0xf6, 0xb0, 1, 2}),
              (Messages{{0x90, 60, 100}, {0xf3, 2}, {0xf6}, {0xb0, 1, 2}}));
}

// ---- HostAudio --------------------------------------------------------------------------------

constexpr size_t kFw = 4;

// Stand-ins for daisycola::ProcessAudio.
const float* g_seen_in[kFw];
size_t       g_frames_processed = 0;
bool         g_ok               = true;

// Each firmware output channel c carries c + 1, and the inputs are remembered.
bool Constants(const float* const* in, float* const* out, size_t frames)
{
    for(size_t c = 0; c < kFw; c++)
    {
        g_seen_in[c] = in[c];
        for(size_t i = 0; i < frames; i++)
            out[c][i] = float(c + 1);
    }
    g_frames_processed += frames;
    return g_ok;
}

// The mic goes to master L.
bool MicToMaster(const float* const* in, float* const* out, size_t frames)
{
    for(size_t c = 0; c < kFw; c++)
        for(size_t i = 0; i < frames; i++)
            out[c][i] = c == 2 ? in[0][i] : 0.f;
    g_frames_processed += frames;
    return true;
}

struct Buffers
{
    explicit Buffers(size_t frames)
        : in(kHostInputs, std::vector<float>(frames)), out(kHostOutputs, std::vector<float>(frames))
    {
        for(size_t c = 0; c < kHostInputs; c++)
            in_ptr[c] = in[c].data();
        for(size_t c = 0; c < kHostOutputs; c++)
            out_ptr[c] = out[c].data();
    }
    std::vector<std::vector<float>> in, out;
    const float*                    in_ptr[kHostInputs];
    float*                          out_ptr[kHostOutputs];
};

TEST(HostAudio, MapsChannelsAt48k)
{
    HostAudio audio(Constants);
    audio.Prepare(48000, 64);
    EXPECT_FALSE(audio.Resampling());

    Buffers b(64);
    g_ok = true;
    audio.Run(b.in_ptr, b.out_ptr, 64);

    EXPECT_EQ(g_seen_in[0], b.in_ptr[0]); // mic
    EXPECT_EQ(g_seen_in[1], nullptr);     // unused
    EXPECT_EQ(g_seen_in[2], b.in_ptr[1]); // line L
    EXPECT_EQ(g_seen_in[3], b.in_ptr[2]); // line R
    EXPECT_EQ(b.out[0][10], 3.f);         // master L is the firmware's output 3
    EXPECT_EQ(b.out[1][10], 4.f);
    EXPECT_EQ(b.out[2][10], 1.f); // phones
    EXPECT_EQ(b.out[3][10], 2.f);
}

TEST(HostAudio, CountsLateBlocksOnceRunning)
{
    HostAudio audio(Constants);
    audio.Prepare(48000, 64);
    Buffers b(64);

    g_ok = false; // the firmware hasn't started audio yet
    audio.Run(b.in_ptr, b.out_ptr, 64);
    EXPECT_EQ(audio.Load().late_blocks, 0u);
    g_ok = true;
    audio.Run(b.in_ptr, b.out_ptr, 64);
    g_ok = false;
    audio.Run(b.in_ptr, b.out_ptr, 64);
    EXPECT_EQ(audio.Load().late_blocks, 1u);
    g_ok = true;
}

TEST(HostAudio, ResamplesAtSteadyRate)
{
    HostAudio audio(Constants);
    audio.Prepare(44100, 64);
    ASSERT_TRUE(audio.Resampling());

    Buffers b(64);
    g_frames_processed   = 0;
    constexpr int cycles = 4000;
    for(int i = 0; i < cycles; i++)
        audio.Run(b.in_ptr, b.out_ptr, 64);

    // The firmware sees 48 kHz worth of frames, and the output settles on the constants.
    EXPECT_NEAR(double(g_frames_processed), cycles * 64 * 48000.0 / 44100.0, 64);
    EXPECT_EQ(audio.Load().dropouts, 0u);
    EXPECT_NEAR(b.out[0][32], 3.f, 1e-3);
    EXPECT_NEAR(b.out[1][32], 4.f, 1e-3);
    EXPECT_NEAR(b.out[2][32], 1.f, 1e-3);
    EXPECT_NEAR(b.out[3][32], 2.f, 1e-3);
}

TEST(HostAudio, ResampledSineKeepsItsPitch)
{
    HostAudio audio(MicToMaster);
    audio.Prepare(44100, 100); // not a multiple of anything the firmware uses
    Buffers b(100);

    std::vector<float> out;
    double             phase = 0;
    for(int cycle = 0; cycle < 441; cycle++) // 1 s
    {
        for(auto& x : b.in[0])
        {
            x = float(0.5 * std::sin(phase));
            phase += 2 * M_PI * 1000.0 / 44100.0;
        }
        audio.Run(b.in_ptr, b.out_ptr, 100);
        out.insert(out.end(), b.out[0].begin(), b.out[0].end());
    }

    // Skip the first 0.1 s while the converters fill up.
    int    crossings = 0;
    size_t first = 0, last = 0;
    float  peak = 0;
    for(size_t i = 4410; i + 1 < out.size(); i++)
    {
        peak = std::max(peak, std::fabs(out[i]));
        if(out[i] <= 0 && out[i + 1] > 0)
        {
            if(!crossings++)
                first = i;
            last = i;
        }
    }
    ASSERT_GT(crossings, 100);
    EXPECT_NEAR((crossings - 1) * 44100.0 / double(last - first), 1000.0, 1.0);
    EXPECT_NEAR(peak, 0.5f, 0.02f);
    EXPECT_EQ(audio.Load().dropouts, 0u);
}

} // namespace
} // namespace champi
