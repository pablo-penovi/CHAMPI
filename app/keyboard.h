// The computer keyboard on the panel: a keymap from physical keys to panel controls, read from
// keymap.toml, and KeyboardControl, which plays it through PanelState.
//
// Keys are physical, so the mapping doesn't depend on the keyboard layout. They're Linux evdev
// codes (linux/input-event-codes.h), named as on a US keyboard: on an AZERTY keyboard "Q" is the
// key labelled A. X11 keycodes are these plus 8.
#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mp2722.h"
#include "panel_state.h"

namespace champi
{
using Scancode = int;

/** "Z", "F1", "Space", "LeftBracket", "KP5"...; any case. Empty if there's no such key. */
std::optional<Scancode> ScancodeFromName(std::string_view name);

/** The key's name, or its number if it has none. */
std::string ScancodeName(Scancode code);

/** What a key does on the panel. */
struct Action
{
    enum class Kind
    {
        kNone,
        kKey,           // plays KEYn while held
        kSelectEncoder, // makes ENCn the one the turn and push keys work
        kTurnLeft,      // turns the selected encoder anticlockwise, repeating while held
        kTurnRight,     // and clockwise
        kPush,          // pushes the selected encoder while held
        kToggle,        // flips the toggle switch
        kLineIn,        // plugs or unplugs the line-in jack
        kPhones,        // plugs or unplugs the headphone jack
        kUsb,           // plugs or unplugs USB power
        kInsertCard,    // opens Insert card (the window handles it)
        kConnections,   // opens and closes the connections menu (the window handles it)
    };
    Kind kind  = Kind::kNone;
    int  index = 0; // KEYn or ENCn, numbered from 1

    bool operator==(const Action& o) const { return kind == o.kind && index == o.index; }
    bool operator!=(const Action& o) const { return !(*this == o); }
};

/** The action's name in keymap.toml: "key_1", "chompi", "encoder_4", "turn_left"... */
std::string ActionName(const Action& action);
std::optional<Action> ActionFromName(std::string_view name);

/**
 * Which physical key does what. A key does one thing; an action can have any number of keys.
 *
 * keymap.toml sets actions to a key name, a scancode number, or a list of them:
 *
 *     play = "Space"
 *     turn_left = ["Left", "LeftBracket"]
 *     line_in = []          # no key
 *
 * Each action it names loses its default keys, and each key it names leaves whatever it did
 * before. Actions it doesn't name keep their defaults. sd_card, insert_card's name before 1.3,
 * still loads, until 2.0.
 */
class Keymap
{
  public:
    /** Two-octave tracker-style keys, F1-F6 for the encoders left to right, and so on. */
    static Keymap Defaults();

    /** Applies keymap.toml's text over this keymap. `source` names it in error messages.
     *  Throws std::runtime_error on anything it doesn't understand, leaving the keymap as it was. */
    void Apply(std::string_view toml, const std::string& source = "keymap");

    /** Reads and applies a keymap.toml file. */
    void Load(const std::string& path);

    void Bind(Scancode code, const Action& action) { keys_[code] = action; }
    void Unbind(const Action& action);

    /** What the key does; kNone if it's not mapped. */
    Action Lookup(Scancode code) const;

    /** The keys that do it, in scancode order. */
    std::vector<Scancode> KeysFor(const Action& action) const;

    /** The whole keymap as keymap.toml. */
    std::string ToToml() const;

  private:
    std::map<Scancode, Action> keys_;
};

/**
 * The keyboard on the panel, played through PanelState.
 *
 * - Panel keys play while held. Two computer keys on one panel key hold it until both are up.
 * - The select keys pick an encoder. The turn keys turn it a detent, and after kRepeatDelay keep
 *   turning it every kRepeatEvery while held (the window's own key repeat should be off), by more
 *   each time the longer they're held. The push key holds it pushed; it stays on the encoder it
 *   pushed even if another is selected meanwhile.
 * - The toggle, line-in, headphone and USB keys flip on a press.
 *
 * Times come from the caller, so it can be driven without a clock.
 */
class KeyboardControl
{
  public:
    using Clock = std::chrono::steady_clock;

    static constexpr auto kRepeatDelay = std::chrono::milliseconds(300);
    static constexpr auto kRepeatEvery = std::chrono::milliseconds(50);
    /** Held this long, a repeat turns 2 detents, then 4: 20, 40 and 80 detents a second. */
    static constexpr auto kRepeatFaster  = std::chrono::milliseconds(1300);
    static constexpr auto kRepeatFastest = std::chrono::milliseconds(2300);

    /** ENC4 is selected at first: the speed knob, leftmost. */
    static constexpr int kFirstSelected = 4;

    KeyboardControl(PanelState& panel, Mp2722& charger, Keymap keymap)
        : panel_(panel), charger_(charger), keymap_(std::move(keymap))
    {
    }

    /** A key went down. Returns false if it isn't mapped to the panel (the connections and Insert
     *  card keys aren't: the window handles them).
     *  Repeats of a held key are ignored. */
    bool Press(Scancode code, Clock::time_point now);
    /** A key went up. Returns false if it wasn't held. */
    bool Release(Scancode code);
    /** Lets go of everything: the window lost the keyboard, so no release will come. */
    void ReleaseAll();
    /** Call regularly: held turn keys repeat on time. */
    void Tick(Clock::time_point now);

    /** The encoder the turn and push keys work. */
    int Selected() const { return selected_; }
    /** Whether any mapped key has been pressed yet, to show the selection only once it's used. */
    bool Used() const { return used_; }
    /** Detents ENCn has been turned from the keyboard, for drawing its knob. */
    int Turned(int encoder) const { return turned_[encoder - 1]; }

    const Keymap& keymap() const { return keymap_; }

  private:
    struct Held
    {
        Action action;
        int    encoder = 0; // the encoder a push key pushed
    };

    void Turn(int detents);

    PanelState&                 panel_;
    Mp2722&                     charger_;
    Keymap                      keymap_;
    std::map<Scancode, Held>    held_;
    int                         key_holds_[kNumKeys] = {};
    int                         selected_            = kFirstSelected;
    bool                        used_                = false;
    int                         turned_[kNumEncoders] = {};

    // The turn key that repeats: the last one pressed and still held.
    Scancode          repeat_key_ = -1;
    int               repeat_dir_ = 0;
    Clock::time_point repeat_pressed_{};
    Clock::time_point next_repeat_{};
};

} // namespace champi
