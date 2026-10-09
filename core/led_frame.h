// The panel LEDs, decoded from the two WS2812 chains the firmware drives.
//
// The SMT chain (TIM3 channel 2, GRB) has one LED under each of KEY1-25. The PTH chain (TIM5
// channel 4, RGB) has the CHOMPI, play and loop keys and one LED per encoder, two for ENC5.
// Colours are what the LEDs receive: the firmware stores key LEDs at a quarter and PTH LEDs at an
// eleventh of the value it asks for, and that scaling is left in.
#pragma once

#include <cstdint>

#include "daisycola/host.h"
#include "panel_state.h"

namespace champi
{
struct LedFrame
{
    daisycola::Rgb key[kNumKeys];         // key[n - 1] is KEYn's LED
    daisycola::Rgb encoder[kNumEncoders]; // encoder[n - 1] is ENCn's LED (the first of ENC5's)
    daisycola::Rgb encoder5_second;       // ENC5's other LED
    uint64_t       smt_sequence;          // the decoded transfer on each chain; 0 = none yet
    uint64_t       pth_sequence;
};

/** Decodes the newest transfer on each chain into `frame`. LEDs on a chain that hasn't sent
 *  anything yet are black. Returns true if both chains have sent at least once. */
bool ReadLedFrame(LedFrame& frame);

} // namespace champi
