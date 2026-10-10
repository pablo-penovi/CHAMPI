// MIDI controllers mapped onto the panel: which control on a controller works which control on
// CHAMPI, kept per controller in midi-mappings/<controller>.toml and learnt in the connections
// menu, and MidiMapper, which plays mapped messages into PanelState from the audio thread.
//
// Every controller arrives merged on CHAMPI's one MIDI input, so a message can't be told apart by
// the controller that sent it. A mapping belongs to the port connected to MIDI in, and the
// mappings of every controller connected to it apply together. Messages none of them maps go on to
// TAPE as before, so its own MIDI (notes on the keys, CC20-27) still works.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "panel.h"
#include "panel_state.h"
#include "routing.h"

namespace champi
{
/** A control on the panel that a MIDI control can work. */
struct MidiTarget
{
    enum class Kind
    {
        kKnobTurn, // turns ENCn
        kKnobPush, // pushes ENCn while held
        kKey,      // plays KEYn while held: the 25 keys, CHOMPI, play and loop
        kToggle,   // flips the toggle switch on each press
    };
    Kind kind  = Kind::kKey;
    int  index = 0; // ENCn or KEYn, numbered from 1

    bool operator==(const MidiTarget& o) const { return kind == o.kind && index == o.index; }
};

/** Each knob's rotation and press, the CHOMPI, play and loop keys, the toggle and KEY1-25. */
constexpr int kNumMidiTargets = 2 * kNumEncoders + 3 + 1 + 25;

/** Target i, in the menu's order: knob 1's rotation and press, knob 2's..., the CHOMPI, play and
 *  loop keys, the toggle, then KEY1-25. Knobs are numbered left to right as they sit on the panel,
 *  so knob 1 is ENC4, knobs 2-4 are ENC1-3, and knobs 5 and 6 are ENC5 and ENC6. */
MidiTarget MidiTargetAt(int i);

/** Its name in a mapping file: "knob_1", "knob_1_push", "chompi", "toggle", "key_1"... */
std::string MidiTargetName(int i);
std::optional<int> MidiTargetFromName(std::string_view name);

/** In the menu: "Knob 1 rotation", "CHAMPI button", "Key 16"..., and what it is on the panel
 *  ("speed", "black"), empty if nothing more needs saying. */
std::string MidiTargetLabel(int i);
std::string MidiTargetHint(int i);

/** How a knob reads a CC's values. */
enum class KnobMode : uint8_t
{
    kAbsolute,       // a knob or fader with ends: the value is where it is
    kRelativeOffset, // an endless encoder sending 64 + n (65 is one step clockwise, 63 back)
    kRelativeTwos,   // n in two's complement (1 is one step clockwise, 127 back)
    kRelativeSigned, // n with 64 as its sign (1 is one step clockwise, 65 back)
};
constexpr int kNumKnobModes = 4;

/** The steps a relative value means, clockwise positive; 0 for kAbsolute. */
int RelativeSteps(KnobMode mode, uint8_t value);

/** What a target is mapped to: a note or a CC, on a channel. */
struct MidiBinding
{
    enum class Type : uint8_t
    {
        kNone,
        kNote,
        kCc,
    };
    Type     type    = Type::kNone;
    uint8_t  channel = 0; // 0-15
    uint8_t  number  = 0; // the note or the controller, 0-127
    KnobMode mode    = KnobMode::kAbsolute; // knob rotations only

    bool Bound() const { return type != Type::kNone; }
    /** Whether both are the same message, whatever the mode. */
    bool SameMessage(const MidiBinding& o) const
    {
        return type == o.type && channel == o.channel && number == o.number;
    }
    bool operator==(const MidiBinding& o) const { return SameMessage(o) && mode == o.mode; }
    bool operator!=(const MidiBinding& o) const { return !(*this == o); }
};

/** The binding as a mapping file writes it: "cc 21 ch 1 absolute", "note 36 ch 10". */
std::string MidiBindingText(const MidiBinding& binding, bool turn);
/** As the menu shows it: "CC 21  ch 1  absolute", "Note 36 (C2)  ch 10". */
std::string MidiBindingLabel(const MidiBinding& binding, bool turn);
/** A knob mode as the menu shows it, and as a mapping file names it. */
const char* KnobModeLabel(KnobMode mode);
const char* KnobModeName(KnobMode mode);

using MidiMapping = std::array<MidiBinding, kNumMidiTargets>;

/**
 * One controller's mapping, in its own file:
 *
 *     controller = "Midi-Bridge:KeyStep 32 (capture)"
 *     knob_1 = "cc 21 ch 1 absolute"
 *     knob_5 = "cc 74 ch 1 relative-64"
 *     knob_1_push = "note 36 ch 10"
 *     play = "cc 51 ch 1"
 *
 * Targets the file doesn't name aren't mapped. A knob's rotation takes a CC; everything else
 * takes a note or a CC, which presses at 64 and above.
 */
struct MidiProfile
{
    std::string           controller; // the port connected to MIDI in, as connections.toml names it
    MidiMapping           bindings{};
    std::filesystem::path file; // where it's kept; empty until it's saved

    int Count() const;

    /** Parses a mapping file. Throws std::runtime_error with the line on anything it doesn't
     *  understand. */
    static MidiProfile Parse(std::string_view toml, const std::string& source = "midi mapping");
    std::string        ToToml() const;

    /** Binds `binding` to target i, and unbinds any other target it was bound to. */
    void Bind(int target, const MidiBinding& binding);
};

/** The mapping files' directory: midi-mappings in `config`. */
std::filesystem::path MidiMappingsDir(const std::filesystem::path& config);

/** Every controller's mapping. */
class MidiMappings
{
  public:
    /** Reads every .toml file in `dir`; none if it doesn't exist. Throws as MidiProfile::Parse,
     *  or if two files are for one controller. */
    static MidiMappings Load(const std::filesystem::path& dir);

    /** Writes a controller's file in `dir`, creating it, by way of a temporary file, and keeps
     *  where it went. A new controller's file is named after it. Throws on failure. */
    void Save(const std::string& controller, const std::filesystem::path& dir);

    const std::vector<MidiProfile>& Profiles() const { return profiles_; }

    /** The profile kept for `controller`, if any. */
    const MidiProfile* Find(const std::string& controller) const;
    /** The profile kept for `controller`, made empty if there's none. */
    MidiProfile& Get(const std::string& controller);

    /** The profile for a port on the graph: the one kept under its name, else one whose name
     *  resolves to it (see Resolve), as a device's name can change between sessions. */
    const MidiProfile* ForPort(const Graph& graph, const std::string& port) const;
    /** The name a port's profile is kept under: its own, or the one that resolves to it. */
    std::string KeyForPort(const Graph& graph, const std::string& port) const;

    bool operator==(const MidiMappings& o) const;
    bool operator!=(const MidiMappings& o) const { return !(*this == o); }

  private:
    std::vector<MidiProfile> profiles_;
};

/** The ports connected to CHAMPI's MIDI input, in the server's order. */
std::vector<std::string> MidiControllers(const Graph& graph);

/** What applies now: the mappings of every controller connected to MIDI in, together. Where two
 *  map one target, or one message, the first controller's wins. */
MidiMapping ActiveMapping(const MidiMappings& mappings, const Graph& graph);

/**
 * Learns what a controller's control sends, from the messages it hears.
 *
 * A press is learnt from its first note-on or CC. A rotation takes CCs only, kTurnMessages of one
 * controller, and how they vary tells a knob with ends from an endless encoder: a knob's values
 * move, an encoder's keep repeating the same step or two. An encoder's encoding is told by the
 * values it uses, and two's complement is assumed until a step back says otherwise.
 */
class MidiLearner
{
  public:
    static constexpr int kTurnMessages = 6;

    explicit MidiLearner(bool turn = false) : turn_(turn) {}

    /** Takes a message (status and two data bytes). Returns the binding once it's learnt. */
    std::optional<MidiBinding> Feed(const uint8_t message[3]);

    /** The CCs heard so far for a rotation, and the binding they're for. */
    int                Heard() const { return int(values_.size()); }
    const MidiBinding& Hearing() const { return hearing_; }

  private:
    bool                 turn_;
    MidiBinding          hearing_;
    std::vector<uint8_t> values_;
};

/** How the values of an endless encoder or a knob read: kAbsolute unless they keep repeating. */
KnobMode GuessKnobMode(const std::vector<uint8_t>& values);

/**
 * Plays the active mapping from the audio thread: a mapped message works its panel control, and
 * everything else goes on to the firmware.
 *
 * - Keys, the CHOMPI, play and loop keys and the knob presses are held from a note-on to its
 *   note-off, or while a CC is 64 or above. The toggle flips on each press.
 * - An endless encoder turns its knob a detent a step, faster when turned fast, as the mouse does
 *   (see TurnGain and QueueTurn).
 * - A knob with ends becomes TAPE's own CC for that knob (CC20-25) on its MIDI in channel, which
 *   sets the knob's value, as a CC from a controller set up for TAPE does.
 * - While learning, notes and CCs are kept for the menu instead (releases still let go), and
 *   don't reach the panel or the firmware.
 *
 * The mapping, the channel and learning are set from the UI thread; Process runs on the audio
 * thread and doesn't lock or allocate.
 */
class MidiMapper
{
  public:
    enum class Result
    {
        kPass,     // send the message on to the firmware as it is
        kConsumed, // it did its job; nothing goes to the firmware
        kFirmware, // send `out` to the firmware instead
    };

    MidiMapper();

    /** UI thread. */
    void SetMapping(const MidiMapping& mapping);
    void SetFirmwareChannel(int channel) { firmware_channel_.store(channel & 15, std::memory_order_relaxed); }
    void SetLearning(bool learning);
    bool Learning() const { return learning_.load(std::memory_order_relaxed); }
    /** The oldest message heard while learning, if any. */
    bool PopHeard(uint8_t message[3]);
    /** Detents ENCn has been turned from MIDI, for drawing its knob. */
    int Turned(int encoder) const { return turned_[encoder - 1].load(std::memory_order_relaxed); }

    /** Audio thread. */
    Result Process(const uint8_t* data, size_t size, PanelState& panel, uint8_t out[3]);

    /** TAPE's CC for ENCn: its knobs are numbered ENC4, ENC1, ENC2, ENC3, ENC5, ENC6 from CC20. */
    static uint8_t FirmwareCc(int encoder);

  private:
    static constexpr int    kRing      = 64;
    static constexpr size_t kTableSize = 2 * 16 * 128; // note or CC, channel, number

    static size_t TableIndex(MidiBinding::Type type, int channel, int number);
    void          Press(int target, bool down, PanelState& panel);

    // Each message's target + 1 (0 for none) and its knob mode in the high byte.
    std::array<std::atomic<uint16_t>, kTableSize> table_;
    std::atomic<int>                              firmware_channel_{0};
    std::atomic<bool>                             learning_{false};
    std::array<std::atomic<uint32_t>, kRing>      heard_;
    std::atomic<uint32_t>                         heard_head_{0}, heard_tail_{0};
    std::array<std::atomic<int>, kNumEncoders>    turned_;
    // Audio thread only.
    bool     down_[kNumMidiTargets] = {};
    TurnRate turn_rate_[kNumEncoders];
    float    turn_carry_[kNumEncoders] = {}; // detents not yet whole
};

} // namespace champi
