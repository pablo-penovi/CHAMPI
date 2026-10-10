// The CHOMPI front panel as the firmware sees it: keys, encoders, the toggle switch and the line-in
// jack, wired to daisycola's shift registers, encoders and pins as on the main board. And the
// headphone jack, which the firmware doesn't see: the host plays master or phones by it.
//
// Keys and encoders are numbered as printed on the board: KEY1-KEY28 and ENC1-ENC6. KEY1-15 are
// the white row, KEY16-25 the black row, KEY26 the CHOMPI key, KEY27 play and KEY28 loop.
//
// Attach wires everything once per process, before the firmware starts. The other calls may be
// made from any host thread; the levels live in daisycola, so there is no state to keep in sync,
// bar the headphone jack's.
#pragma once

#include <atomic>

namespace champi
{
constexpr int kNumKeys     = 28;
constexpr int kNumEncoders = 6;

constexpr int kChompiKey = 26;
constexpr int kPlayKey   = 27;
constexpr int kLoopKey   = 28;

class PanelState
{
  public:
    /** Wires the panel to daisycola with every key and push released, the toggle on and no
     *  line-in or headphone plug. Once per process, before the firmware starts. */
    void Attach();

    /** Presses or releases KEYn (1 to 28). */
    void SetKey(int key, bool pressed);
    bool KeyPressed(int key) const;

    /** Turns ENCn (1 to 6) by `detents` clicks. Positive is what the firmware counts as +1. The
     *  detents play out one Gray-code state every 3 ms, so a turn takes a while to arrive. */
    void TurnEncoder(int encoder, int detents);

    /** Detents queued on ENCn that the firmware hasn't seen yet. */
    int PendingDetents(int encoder) const;

    /** Pushes or releases ENCn's built-in switch. */
    void SetEncoderPushed(int encoder, bool pushed);
    bool EncoderPushed(int encoder) const;

    /** The toggle switch. On is what the firmware's GetToggleState reports as true (the switch
     *  input pulled low); the firmware lights the encoder LEDs and opens the shift menu only then. */
    void SetToggle(bool on);
    bool Toggle() const;

    /** The line-in jack: with a plug in, the firmware records from line in instead of the mic. */
    void SetLineIn(bool plugged);
    bool LineIn() const;

    /** The headphone jack: with a plug in, the host plays the phones outputs and silences master;
     *  without one, the other way round. */
    void SetPhones(bool plugged) { phones_.store(plugged, std::memory_order_relaxed); }
    bool Phones() const { return phones_.load(std::memory_order_relaxed); }

  private:
    int button_chain_  = -1; // five CD4021s: keys, toggle and the pushes of ENC1-4 and ENC6
    int encoder_chain_ = -1; // one CD4021: A and B of ENC1-4
    int encoders_[kNumEncoders] = {};
    std::atomic<bool> phones_{false};
};

} // namespace champi
