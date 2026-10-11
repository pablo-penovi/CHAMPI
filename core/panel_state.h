// The CHOMPI front panel as the firmware sees it: keys, encoders, the toggle switch and the line-in
// jack, wired to daisycola's shift registers, encoders and pins as on the main board. And the
// headphone jack, which the firmware doesn't see: the host plays master or phones by it.
//
// Keys and encoders are numbered as printed on the board: KEY1-KEY28 and ENC1-ENC6. KEY1-15 are
// the white row, KEY16-25 the black row, KEY26 the CHOMPI key, KEY27 play and KEY28 loop.
//
// Attach wires everything before the firmware starts, and again after each power cycle, with the
// toggle and the jacks where the player left them. The other calls may be made from any host
// thread. Between Detach and the next Attach, while the firmware library is being swapped, they
// do nothing and read everything as released: a MIDI controller can keep playing meanwhile.
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
    /** Wires the panel to daisycola with every key and push released, and the toggle and the
     *  line-in jack as last set (at first, the toggle on and no plug). Before the firmware starts,
     *  from the thread that loads it. */
    void Attach();

    /** Stops using daisycola's board until the next Attach, waiting for calls in progress on
     *  other threads. Before a power cycle, from the thread that runs it. */
    void Detach();

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
    // Counts a host call in use of the board for Detach to wait on. Stays false if the board isn't
    // attached, and the call does nothing then.
    class Use
    {
      public:
        explicit Use(const PanelState& panel);
        ~Use();
        explicit operator bool() const { return attached_; }

      private:
        const PanelState& panel_;
        bool              attached_;
    };

    int button_chain_  = -1; // five CD4021s: keys, toggle and the pushes of ENC1-4 and ENC6
    int encoder_chain_ = -1; // one CD4021: A and B of ENC1-4
    int encoders_[kNumEncoders] = {};
    std::atomic<bool> attached_{false};
    mutable std::atomic<int> users_{0};
    std::atomic<bool> toggle_{true};
    std::atomic<bool> line_in_{false};
    std::atomic<bool> phones_{false};
};

} // namespace champi
