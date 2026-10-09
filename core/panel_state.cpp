#include "panel_state.h"

#include <stdexcept>
#include <string>

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

const daisy::Pin kEnc5Push   = seed::D10;
const daisy::Pin kJackDetect = seed::D21; // high with a plug in

int CheckIndex(int n, int count, const char* what)
{
    if(n < 1 || n > count)
        throw std::out_of_range(std::string("no ") + what + " " + std::to_string(n));
    return n - 1;
}

} // namespace

void PanelState::Attach()
{
    button_chain_  = daisycola::AttachSr4021(seed::D8, seed::D7, seed::D9, 5);
    encoder_chain_ = daisycola::AttachSr4021(seed::D22, seed::D23, seed::D19, 1);

    // ENC1-4 have A and B on encoder-chain inputs 2(n-1) and 2(n-1)+1; ENC5 and ENC6 use GPIOs.
    for(int i = 0; i < 4; i++)
        encoders_[i] = daisycola::AttachEncoder(EncoderLine::OnSr(encoder_chain_, 2 * i),
                                                EncoderLine::OnSr(encoder_chain_, 2 * i + 1));
    encoders_[4] = daisycola::AttachEncoder(EncoderLine::OnPin(seed::D0), EncoderLine::OnPin(seed::D20));
    encoders_[5] = daisycola::AttachEncoder(EncoderLine::OnPin(seed::D15), EncoderLine::OnPin(seed::D17));

    daisycola::SetSrInputs(button_chain_, ~0ull);
    daisycola::SetPin(kEnc5Push, true);
    SetToggle(true);
    SetLineIn(false);
}

void PanelState::SetKey(int key, bool pressed)
{
    daisycola::SetSrInput(button_chain_, kKeyBits[CheckIndex(key, kNumKeys, "key")], !pressed);
}

bool PanelState::KeyPressed(int key) const
{
    const int bit = kKeyBits[CheckIndex(key, kNumKeys, "key")];
    return !(daisycola::GetSrInputs(button_chain_) >> bit & 1);
}

void PanelState::TurnEncoder(int encoder, int detents)
{
    daisycola::QueueDetents(encoders_[CheckIndex(encoder, kNumEncoders, "encoder")], detents);
}

int PanelState::PendingDetents(int encoder) const
{
    return daisycola::PendingDetents(encoders_[CheckIndex(encoder, kNumEncoders, "encoder")]);
}

void PanelState::SetEncoderPushed(int encoder, bool pushed)
{
    const int bit = kPushBits[CheckIndex(encoder, kNumEncoders, "encoder")];
    if(bit < 0)
        daisycola::SetPin(kEnc5Push, !pushed);
    else
        daisycola::SetSrInput(button_chain_, bit, !pushed);
}

bool PanelState::EncoderPushed(int encoder) const
{
    const int bit = kPushBits[CheckIndex(encoder, kNumEncoders, "encoder")];
    if(bit < 0)
        return !daisycola::GetPin(kEnc5Push);
    return !(daisycola::GetSrInputs(button_chain_) >> bit & 1);
}

void PanelState::SetToggle(bool on)
{
    daisycola::SetSrInput(button_chain_, kToggle, !on);
}

bool PanelState::Toggle() const
{
    return !(daisycola::GetSrInputs(button_chain_) >> kToggle & 1);
}

void PanelState::SetLineIn(bool plugged)
{
    daisycola::SetPin(kJackDetect, plugged);
}

bool PanelState::LineIn() const
{
    return daisycola::GetPin(kJackDetect);
}

} // namespace champi
