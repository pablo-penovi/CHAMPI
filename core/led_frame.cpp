#include "led_frame.h"

namespace champi
{
namespace
{
constexpr int kNumSmtLeds = 25;
constexpr int kNumPthLeds = 10;

// The chain positions, from the led_map tables in TAPE's NormalPage.h and TestPage.h. The SMT chain
// runs along the black row from KEY16 to KEY25, then back along the white row from KEY15 to KEY1.
// KEYn's LED is SMT LED kSmtOfKey[n - 1].
constexpr int kSmtOfKey[kNumSmtLeds] = {
    24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, // KEY1-15
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,                      // KEY16-25
};

// What each PTH LED lights, in chain order.
enum class Pth
{
    kChompi,
    kEnc4,
    kEnc1,
    kEnc2,
    kEnc3,
    kEnc5,
    kEnc5Second,
    kPlay,
    kLoop,
    kEnc6,
};

// The newest transfer on a chain, decoded. Returns its sequence number, or 0 if none was sent yet.
uint64_t Decode(int timer, int channel, daisycola::ColorOrder order, daisycola::Rgb* leds, size_t count)
{
    // A DmaFrame is 16 KB: too much for the stack of whichever thread draws the panel.
    static thread_local daisycola::DmaFrame frame;
    for(size_t i = 0; i < count; i++)
        leds[i] = {};
    if(!daisycola::GetDmaFrame(timer, channel, frame))
        return 0;
    daisycola::Ws2812Decode(frame, order, leds, count);
    return frame.sequence;
}

} // namespace

bool ReadLedFrame(LedFrame& frame)
{
    daisycola::Rgb smt[kNumSmtLeds], pth[kNumPthLeds];
    frame.smt_sequence = Decode(3, 2, daisycola::ColorOrder::kGrb, smt, kNumSmtLeds);
    frame.pth_sequence = Decode(5, 4, daisycola::ColorOrder::kRgb, pth, kNumPthLeds);

    for(int i = 0; i < kNumSmtLeds; i++)
        frame.key[i] = smt[kSmtOfKey[i]];
    frame.key[kChompiKey - 1]  = pth[int(Pth::kChompi)];
    frame.key[kPlayKey - 1]    = pth[int(Pth::kPlay)];
    frame.key[kLoopKey - 1]    = pth[int(Pth::kLoop)];
    frame.encoder[0]           = pth[int(Pth::kEnc1)];
    frame.encoder[1]           = pth[int(Pth::kEnc2)];
    frame.encoder[2]           = pth[int(Pth::kEnc3)];
    frame.encoder[3]           = pth[int(Pth::kEnc4)];
    frame.encoder[4]           = pth[int(Pth::kEnc5)];
    frame.encoder[5]           = pth[int(Pth::kEnc6)];
    frame.encoder5_second      = pth[int(Pth::kEnc5Second)];
    return frame.smt_sequence != 0 && frame.pth_sequence != 0;
}

} // namespace champi
