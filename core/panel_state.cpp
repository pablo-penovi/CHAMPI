#include "panel_state.h"

#include <stdexcept>
#include <string>
#include <thread>

#include "daisy_seed.h"
#include "daisycola/host.h"

using daisycola::EncoderLine;
namespace seed = daisy::seed;

namespace champi
{
namespace
{
// The main board's wiring, from TAPE's hardware.h. Keys and switches pull their input low when
// pressed; released inputs rest high.

// Input bits of the five-chip button chain, in the firmware's SwId order.
enum ButtonBit
{
    kEnc1Push = 0,
    kEnc2Push = 1,
    kEnc3Push = 2,
    kEnc4Push = 3,
    kToggle   = 6,
    kEnc6Push = 32,
};

// KEYn's input bit on the button chain is kKeyBits[n - 1].
constexpr int kKeyBits[kNumKeys] = {
    15, 8,  9,  10, 11, 16, 17, 18, 19, 20, // KEY1-10
    24, 25, 26, 27, 28, 7,  12, 13, 14, 21, // KEY11-20
    22, 23, 29, 30, 31, 5,  33, 34,         // KEY21-28
};

// The ENCn push on the button chain, or -1 for ENC5, whose push is a GPIO.
constexpr int kPushBits[kNumEncoders] = {kEnc1Push, kEnc2Push, kEnc3Push, kEnc4Push, -1, kEnc6Push};

// The firmware debounces the button chain over 8 reads in different milliseconds, but with the
// host's audio clock it reads in bursts, one per host period, so a tap can come and go between
// two bursts. Holding each level for 10 such reads lets every tap through, with 2 to spare for a
// read that falls on a millisecond boundary.
constexpr int kButtonHoldReads = 10;

const daisy::Pin kEnc5Push   = seed::D10;
const daisy::Pin kJackDetect = seed::D21; // high with a plug in

int CheckIndex(int n, int count, const char* what)
{
    if(n < 1 || n > count)
        throw std::out_of_range(std::string("no ") + what + " " + std::to_string(n));
    return n - 1;
}

} // namespace

PanelState::Use::Use(const PanelState& panel) : panel_(panel)
{
    // With Detach's store and load, sequentially consistent: either Detach sees this call and
    // waits for it, or the call sees the board detached.
    panel_.users_.fetch_add(1);
    attached_ = panel_.attached_.load();
}

PanelState::Use::~Use()
{
    panel_.users_.fetch_sub(1);
}

void PanelState::Detach()
{
    attached_.store(false);
    while(users_.load() > 0)
        std::this_thread::yield();
}

void PanelState::Attach()
{
    button_chain_  = daisycola::AttachSr4021(seed::D8, seed::D7, seed::D9, 5, kButtonHoldReads);
    encoder_chain_ = daisycola::AttachSr4021(seed::D22, seed::D23, seed::D19, 1);

    // ENC1-4 have A and B on encoder-chain inputs 2(n-1) and 2(n-1)+1; ENC5 and ENC6 use GPIOs.
    for(int i = 0; i < 4; i++)
        encoders_[i] = daisycola::AttachEncoder(EncoderLine::OnSr(encoder_chain_, 2 * i),
                                                EncoderLine::OnSr(encoder_chain_, 2 * i + 1));
    encoders_[4] = daisycola::AttachEncoder(EncoderLine::OnPin(seed::D0), EncoderLine::OnPin(seed::D20));
    encoders_[5] = daisycola::AttachEncoder(EncoderLine::OnPin(seed::D15), EncoderLine::OnPin(seed::D17));

    daisycola::SetSrInputs(button_chain_, ~0ull);
    daisycola::SetPin(kEnc5Push, true);
    daisycola::SetSrInput(button_chain_, kToggle, !toggle_.load());
    daisycola::SetPin(kJackDetect, line_in_.load());
    attached_.store(true);
}

void PanelState::SetKey(int key, bool pressed)
{
    const int bit = kKeyBits[CheckIndex(key, kNumKeys, "key")];
    if(Use use{*this})
        daisycola::SetSrInput(button_chain_, bit, !pressed);
}

bool PanelState::KeyPressed(int key) const
{
    const int bit = kKeyBits[CheckIndex(key, kNumKeys, "key")];
    const Use use{*this};
    return use && !(daisycola::GetSrInputs(button_chain_) >> bit & 1);
}

void PanelState::TurnEncoder(int encoder, int detents)
{
    const int index = CheckIndex(encoder, kNumEncoders, "encoder");
    if(Use use{*this})
        daisycola::QueueDetents(encoders_[index], detents);
}

int PanelState::PendingDetents(int encoder) const
{
    const int index = CheckIndex(encoder, kNumEncoders, "encoder");
    const Use use{*this};
    return use ? daisycola::PendingDetents(encoders_[index]) : 0;
}

void PanelState::SetEncoderPushed(int encoder, bool pushed)
{
    const int bit = kPushBits[CheckIndex(encoder, kNumEncoders, "encoder")];
    const Use use{*this};
    if(!use)
        return;
    if(bit < 0)
        daisycola::SetPin(kEnc5Push, !pushed);
    else
        daisycola::SetSrInput(button_chain_, bit, !pushed);
}

bool PanelState::EncoderPushed(int encoder) const
{
    const int bit = kPushBits[CheckIndex(encoder, kNumEncoders, "encoder")];
    const Use use{*this};
    if(!use)
        return false;
    if(bit < 0)
        return !daisycola::GetPin(kEnc5Push);
    return !(daisycola::GetSrInputs(button_chain_) >> bit & 1);
}

void PanelState::SetToggle(bool on)
{
    toggle_.store(on);
    if(Use use{*this})
        daisycola::SetSrInput(button_chain_, kToggle, !on);
}

bool PanelState::Toggle() const
{
    return toggle_.load();
}

void PanelState::SetLineIn(bool plugged)
{
    line_in_.store(plugged);
    if(Use use{*this})
        daisycola::SetPin(kJackDetect, plugged);
}

bool PanelState::LineIn() const
{
    return line_in_.load();
}

} // namespace champi
