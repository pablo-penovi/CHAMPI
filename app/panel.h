// The panel as it's drawn and played on screen, apart from the drawing itself: the sizes the board
// files don't give, what the mouse is over, how the LEDs look, and what the mouse does.
//
// Everything is in panel millimetres, as in panel_layout.h: the origin is the top-left corner of
// the outline and y points down.
#pragma once

#include <chrono>

#include "daisycola/host.h"
#include "panel_layout.h"
#include "panel_state.h"

namespace champi
{
// Sizes that aren't in the board files.
constexpr float kKeyCap         = 18.0f; // MX keycaps
constexpr float kKnobRing       = 16.4f; // the gold ring round the small encoders
constexpr float kKnob           = 12.0f; // the small encoders' white knobs
constexpr float kScrubWheel     = 33.0f; // ENC5's knob
constexpr float kJack           = 6.0f;  // the jack sockets drawn at the right edge
constexpr int   kDetentsPerTurn = 24;    // PEC11 encoders, for drawing the knobs

/** What a point on the panel is over. */
struct Hit
{
    enum class Kind
    {
        kNone,
        kKey,
        kEncoder,
        kToggle,
        kLineIn,
    };
    Kind kind  = Kind::kNone;
    int  index = 0; // KEYn or ENCn, numbered from 1

    bool operator==(const Hit& o) const { return kind == o.kind && index == o.index; }
};

Hit HitTest(float x, float y);

/** KEYn's cap. */
layout::Rect KeyCap(int key);

/** ENCn's knob, as far as the mouse goes: the gold ring on the small ones. */
layout::Circle Knob(int encoder);

/** The line-in jack socket, drawn at the right edge level with the real one. */
layout::Circle LineInJack();

/** An LED's colour on screen, each channel from 0 to 1. */
struct Light
{
    float r, g, b;

    float Brightness() const;
};

/** How bright a decoded LED looks. The firmware sends a quarter of what it asks for to the key
 *  LEDs and an eleventh to the PTH ones; `divisor` undoes that. The LEDs' light is linear in their
 *  PWM values, so it's gamma-encoded for the screen. */
Light LedLight(const daisycola::Rgb& led, int divisor);

constexpr int kSmtDivisor = 4;
constexpr int kPthDivisor = 11;

/**
 * The mouse on the panel, played through PanelState.
 *
 * - Keys play while the button is held.
 * - Encoders turn with a vertical drag (up is clockwise) or the scroll wheel. A click pushes one
 *   briefly; holding the button still pushes it until release, and dragging then turns it while
 *   pushed.
 * - The toggle switch and the line-in jack flip with a click.
 *
 * Times come from the caller, so it can be driven without a clock.
 */
class MouseControl
{
  public:
    using Clock = std::chrono::steady_clock;

    /** How far to drag for a detent, and before a press on an encoder counts as a drag. */
    static constexpr float kMmPerDetent = 2.0f;
    static constexpr float kDragStart   = 1.0f;
    /** How long a still press on an encoder takes to push it, and how long a click pushes it. */
    static constexpr auto kHoldToPush = std::chrono::milliseconds(300);
    static constexpr auto kClickPush  = std::chrono::milliseconds(80);

    explicit MouseControl(PanelState& panel) : panel_(panel) {}

    /** The left button went down. Returns false if it wasn't over anything. */
    bool Press(float x, float y, Clock::time_point now);
    void Move(float x, float y, Clock::time_point now);
    void Release(Clock::time_point now);
    /** The wheel turned by `steps` (up is positive) at x, y. Returns false if not over an encoder. */
    bool Scroll(float x, float y, float steps);
    /** Call regularly: holds and clicks on the encoders end on time. */
    void Tick(Clock::time_point now);

    /** Detents ENCn has been turned by the mouse, for drawing its knob. */
    int Turned(int encoder) const { return turned_[encoder - 1]; }

  private:
    void Turn(int encoder, int detents);
    void EndClick();

    PanelState& panel_;
    int         turned_[kNumEncoders] = {};
    int         key_                  = 0; // the key held down, if any
    float       scroll_               = 0; // wheel steps not yet a whole detent

    // The encoder the button went down on, and what the press has done so far.
    int               encoder_  = 0;
    float             start_y_  = 0;
    int               dragged_  = 0; // detents sent since the drag started
    bool              dragging_ = false;
    bool              pushed_   = false;
    Clock::time_point pressed_at_{};

    // A click's push, released at click_ends_.
    int               click_encoder_ = 0;
    Clock::time_point click_ends_{};
};

} // namespace champi
