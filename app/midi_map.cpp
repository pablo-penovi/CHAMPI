#include "midi_map.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include "panel.h"
#include "toml_lines.h"

namespace fs = std::filesystem;

namespace champi
{
namespace
{
constexpr int kFirstButton = 2 * kNumEncoders; // the CHOMPI key, then play, loop and the toggle
constexpr int kToggleIndex = kFirstButton + 3;
constexpr int kFirstKey    = kToggleIndex + 1; // KEY1
static_assert(kFirstKey + 25 == kNumMidiTargets);

// The knobs are numbered as they sit on the panel, left to right, which isn't the board's ENCn
// order: knob 1 is ENC4. And what each does on TAPE's first page, as the panel art has it.
constexpr int     kKnobEncoders[kNumEncoders] = {4, 1, 2, 3, 5, 6};
const char* const kKnobHints[kNumEncoders]    = {"speed", "start", "end", "effect", "scrub wheel", "volume"};

const char* const kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// "KeyStep 32 (capture)" as "KeyStep_32_capture": a file name that's safe anywhere.
std::string FileStem(const std::string& controller)
{
    std::string stem;
    for(char c : controller)
    {
        const bool keep = std::isalnum((unsigned char)c) || c == '-' || c == '_';
        if(keep)
            stem += c;
        else if(!stem.empty() && stem.back() != '_')
            stem += '_';
    }
    while(!stem.empty() && stem.back() == '_')
        stem.pop_back();
    return stem.empty() ? "controller" : stem;
}

// "cc 21 ch 1", and a knob mode after it if `has_mode` is set.
std::optional<MidiBinding> ParseBinding(std::string_view text, bool& has_mode)
{
    std::vector<std::string> words;
    std::istringstream       in{std::string(text)};
    for(std::string w; in >> w;)
    {
        for(char& c : w)
            c = char(std::tolower((unsigned char)c));
        words.push_back(w);
    }
    if(words.size() < 4 || words.size() > 5 || words[2] != "ch")
        return std::nullopt;
    MidiBinding b;
    if(words[0] == "note")
        b.type = MidiBinding::Type::kNote;
    else if(words[0] == "cc")
        b.type = MidiBinding::Type::kCc;
    else
        return std::nullopt;
    auto number = [](const std::string& w, int lo, int hi) -> std::optional<int> {
        if(w.empty() || w.size() > 3 || !std::all_of(w.begin(), w.end(), ::isdigit))
            return std::nullopt;
        const int n = std::stoi(w);
        return n >= lo && n <= hi ? std::optional<int>(n) : std::nullopt;
    };
    const auto num = number(words[1], 0, 127), channel = number(words[3], 1, 16);
    if(!num || !channel)
        return std::nullopt;
    b.number  = uint8_t(*num);
    b.channel = uint8_t(*channel - 1);
    has_mode  = words.size() == 5;
    if(has_mode)
    {
        bool known = false;
        for(int m = 0; m < kNumKnobModes; m++)
            if(words[4] == KnobModeName(KnobMode(m)))
            {
                b.mode = KnobMode(m);
                known  = true;
            }
        if(!known)
            return std::nullopt;
    }
    return b;
}
} // namespace

// ---- Targets -----------------------------------------------------------------------------------

MidiTarget MidiTargetAt(int i)
{
    using K = MidiTarget::Kind;
    if(i < kFirstButton)
        return {i % 2 ? K::kKnobPush : K::kKnobTurn, kKnobEncoders[i / 2]};
    if(i < kToggleIndex)
        return {K::kKey, kChompiKey + (i - kFirstButton)};
    if(i == kToggleIndex)
        return {K::kToggle, 0};
    return {K::kKey, 1 + (i - kFirstKey)};
}

std::string MidiTargetName(int i)
{
    const MidiTarget t    = MidiTargetAt(i);
    const int        knob = i / 2 + 1; // knobs only: its place from the left
    switch(t.kind)
    {
        case MidiTarget::Kind::kKnobTurn: return "knob_" + std::to_string(knob);
        case MidiTarget::Kind::kKnobPush: return "knob_" + std::to_string(knob) + "_push";
        case MidiTarget::Kind::kToggle: return "toggle";
        case MidiTarget::Kind::kKey: break;
    }
    switch(t.index)
    {
        case kChompiKey: return "chompi";
        case kPlayKey: return "play";
        case kLoopKey: return "loop";
        default: return "key_" + std::to_string(t.index);
    }
}

std::optional<int> MidiTargetFromName(std::string_view name)
{
    for(int i = 0; i < kNumMidiTargets; i++)
        if(MidiTargetName(i) == name)
            return i;
    return std::nullopt;
}

std::string MidiTargetLabel(int i)
{
    const MidiTarget t    = MidiTargetAt(i);
    const int        knob = i / 2 + 1; // knobs only: its place from the left
    switch(t.kind)
    {
        case MidiTarget::Kind::kKnobTurn: return "Knob " + std::to_string(knob) + " rotation";
        case MidiTarget::Kind::kKnobPush: return "Knob " + std::to_string(knob) + " press";
        case MidiTarget::Kind::kToggle: return "Toggle switch";
        case MidiTarget::Kind::kKey: break;
    }
    switch(t.index)
    {
        case kChompiKey: return "CHAMPI button";
        case kPlayKey: return "Play button";
        case kLoopKey: return "Loop button";
        default: return "Key " + std::to_string(t.index);
    }
}

std::string MidiTargetHint(int i)
{
    const MidiTarget t = MidiTargetAt(i);
    switch(t.kind)
    {
        case MidiTarget::Kind::kKnobTurn:
        case MidiTarget::Kind::kKnobPush: return kKnobHints[i / 2];
        case MidiTarget::Kind::kToggle: return "up records, down plays";
        case MidiTarget::Kind::kKey: break;
    }
    if(t.index >= kChompiKey)
        return "";
    return t.index <= 15 ? "white" : "black";
}

// ---- Bindings ----------------------------------------------------------------------------------

int RelativeSteps(KnobMode mode, uint8_t value)
{
    value &= 127;
    switch(mode)
    {
        case KnobMode::kAbsolute: return 0;
        case KnobMode::kRelativeOffset: return int(value) - 64;
        case KnobMode::kRelativeTwos: return value < 64 ? int(value) : int(value) - 128;
        case KnobMode::kRelativeSigned: return value & 64 ? -int(value & 63) : int(value);
    }
    return 0;
}

const char* KnobModeLabel(KnobMode mode)
{
    switch(mode)
    {
        case KnobMode::kAbsolute: return "absolute";
        case KnobMode::kRelativeOffset: return "relative, 64 is still";
        case KnobMode::kRelativeTwos: return "relative, two's complement";
        case KnobMode::kRelativeSigned: return "relative, sign bit";
    }
    return "";
}

const char* KnobModeName(KnobMode mode)
{
    switch(mode)
    {
        case KnobMode::kAbsolute: return "absolute";
        case KnobMode::kRelativeOffset: return "relative-64";
        case KnobMode::kRelativeTwos: return "relative-twos";
        case KnobMode::kRelativeSigned: return "relative-signed";
    }
    return "";
}

std::string MidiBindingText(const MidiBinding& b, bool turn)
{
    if(!b.Bound())
        return "";
    std::string text = std::string(b.type == MidiBinding::Type::kNote ? "note " : "cc ") + std::to_string(b.number)
                       + " ch " + std::to_string(b.channel + 1);
    if(turn)
        text += std::string(" ") + KnobModeName(b.mode);
    return text;
}

std::string MidiBindingLabel(const MidiBinding& b, bool turn)
{
    if(!b.Bound())
        return "";
    std::string label;
    if(b.type == MidiBinding::Type::kNote)
        label = "Note " + std::to_string(b.number) + " (" + kNoteNames[b.number % 12]
                + std::to_string(b.number / 12 - 1) + ")";
    else
        label = "CC " + std::to_string(b.number);
    label += "  ch " + std::to_string(b.channel + 1);
    if(turn)
        label += std::string("  ") + KnobModeLabel(b.mode);
    return label;
}

// ---- Profiles ----------------------------------------------------------------------------------

int MidiProfile::Count() const
{
    return int(std::count_if(bindings.begin(), bindings.end(), [](const MidiBinding& b) { return b.Bound(); }));
}

void MidiProfile::Bind(int target, const MidiBinding& binding)
{
    if(binding.Bound())
        for(MidiBinding& other : bindings)
            if(other.SameMessage(binding))
                other = {};
    bindings[target] = binding;
}

MidiProfile MidiProfile::Parse(std::string_view toml, const std::string& source)
{
    MidiProfile           profile;
    std::set<std::string> seen;
    std::map<int, int>    messages; // each binding's target, by message, to catch one used twice
    bool                  has_controller = false;
    const auto lines = ParseTomlLines(toml, source, "a mapping in quotes, as \"cc 21 ch 1\"",
                                      "tables aren't used in a MIDI mapping; write `control = \"cc 21 ch 1\"` lines");
    for(const TomlLine& l : lines)
    {
        if(!seen.insert(l.name).second)
            TomlFail(source, l.line, l.name + " is set twice");
        if(l.is_array || !l.values[0].is_string)
            TomlFail(source, l.line, l.name + " takes one value in quotes");
        const std::string& text = l.values[0].text;
        if(l.name == "controller")
        {
            if(text.empty())
                TomlFail(source, l.line, "the controller has no name");
            profile.controller = text;
            has_controller     = true;
            continue;
        }
        const auto target = MidiTargetFromName(l.name);
        if(!target)
            TomlFail(source, l.line, "no control is called \"" + l.name + "\"");
        const bool turn    = MidiTargetAt(*target).kind == MidiTarget::Kind::kKnobTurn;
        bool       has_mode = false;
        const auto binding  = ParseBinding(text, has_mode);
        if(!binding || has_mode != turn)
            TomlFail(source, l.line,
                     "\"" + text + "\" isn't a mapping: write \"note 36 ch 10\" or \"cc 21 ch 1\""
                         + (turn ? ", then absolute, relative-64, relative-twos or relative-signed" : ""));
        if(turn && binding->type != MidiBinding::Type::kCc)
            TomlFail(source, l.line, "a knob's rotation takes a CC");
        const int key = int(binding->type) << 16 | binding->channel << 8 | binding->number;
        if(const auto it = messages.find(key); it != messages.end())
            TomlFail(source, l.line, "\"" + MidiBindingText(*binding, false) + "\" is already mapped on line "
                                         + std::to_string(it->second));
        messages[key]              = l.line;
        profile.bindings[*target] = *binding;
    }
    if(!has_controller)
        TomlFail(source, 1, "no controller = \"...\" line says which controller it's for");
    return profile;
}

std::string MidiProfile::ToToml() const
{
    std::string out = "# CHAMPI's MIDI mapping for one controller, written by the connections menu (F8).\n"
                      "# A control takes \"note <n> ch <1-16>\" or \"cc <n> ch <1-16>\"; a knob's rotation\n"
                      "# takes a CC and then absolute, relative-64, relative-twos or relative-signed.\n\n";
    out += "controller = " + TomlQuote(controller) + "\n";
    for(int i = 0; i < kNumMidiTargets; i++)
        if(bindings[i].Bound())
            out += MidiTargetName(i) + " = "
                   + TomlQuote(MidiBindingText(bindings[i], MidiTargetAt(i).kind == MidiTarget::Kind::kKnobTurn))
                   + "\n";
    return out;
}

fs::path MidiMappingsDir(const fs::path& config)
{
    return config / "midi-mappings";
}

MidiMappings MidiMappings::Load(const fs::path& dir)
{
    MidiMappings    mappings;
    std::error_code ec;
    if(!fs::is_directory(dir, ec))
        return mappings;
    std::vector<fs::path> files;
    for(const auto& entry : fs::directory_iterator(dir))
        if(entry.is_regular_file() && entry.path().extension() == ".toml")
            files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    for(const fs::path& path : files)
    {
        std::ifstream in(path);
        if(!in)
            throw std::runtime_error("can't read " + path.string());
        std::stringstream text;
        text << in.rdbuf();
        MidiProfile profile = MidiProfile::Parse(text.str(), path.string());
        if(const MidiProfile* other = mappings.Find(profile.controller))
            throw std::runtime_error(path.string() + ": " + profile.controller + " is mapped in "
                                     + other->file.string() + " already");
        profile.file = path;
        mappings.profiles_.push_back(std::move(profile));
    }
    return mappings;
}

void MidiMappings::Save(const std::string& controller, const fs::path& dir)
{
    MidiProfile& profile = Get(controller);
    if(profile.file.empty())
    {
        // Named after the controller, unless another has that name already.
        const std::string stem = FileStem(controller);
        for(int n = 1;; n++)
        {
            const fs::path path  = dir / (stem + (n > 1 ? "-" + std::to_string(n) : "") + ".toml");
            const bool     taken = fs::exists(path) || std::any_of(profiles_.begin(), profiles_.end(), [&](const MidiProfile& p) {
                                   return p.file == path;
                               });
            if(!taken)
            {
                profile.file = path;
                break;
            }
        }
    }
    fs::create_directories(profile.file.parent_path());
    const fs::path tmp = profile.file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << profile.ToToml();
        if(!out.flush())
            throw std::runtime_error("can't write " + tmp.string());
    }
    fs::rename(tmp, profile.file);
}

const MidiProfile* MidiMappings::Find(const std::string& controller) const
{
    for(const MidiProfile& p : profiles_)
        if(p.controller == controller)
            return &p;
    return nullptr;
}

MidiProfile& MidiMappings::Get(const std::string& controller)
{
    for(MidiProfile& p : profiles_)
        if(p.controller == controller)
            return p;
    MidiProfile& p = profiles_.emplace_back();
    p.controller   = controller;
    return p;
}

const MidiProfile* MidiMappings::ForPort(const Graph& graph, const std::string& port) const
{
    if(const MidiProfile* exact = Find(port))
        return exact;
    for(const MidiProfile& p : profiles_)
        if(const PeerPort* resolved = Resolve(graph, kEventsIn, p.controller); resolved && resolved->name == port)
            return &p;
    return nullptr;
}

std::string MidiMappings::KeyForPort(const Graph& graph, const std::string& port) const
{
    const MidiProfile* p = ForPort(graph, port);
    return p ? p->controller : port;
}

bool MidiMappings::operator==(const MidiMappings& o) const
{
    // In any order: files load in name order, and new controllers go last.
    return profiles_.size() == o.profiles_.size()
           && std::all_of(profiles_.begin(), profiles_.end(), [&](const MidiProfile& p) {
                  const MidiProfile* other = o.Find(p.controller);
                  return other && other->bindings == p.bindings;
              });
}

std::vector<std::string> MidiControllers(const Graph& graph)
{
    std::vector<std::string> controllers;
    for(const PeerPort& p : graph.ports)
        if(graph.Connected(kEventsIn, p.name))
            controllers.push_back(p.name);
    return controllers;
}

MidiMapping ActiveMapping(const MidiMappings& mappings, const Graph& graph)
{
    MidiMapping active{};
    for(const std::string& port : MidiControllers(graph))
    {
        const MidiProfile* profile = mappings.ForPort(graph, port);
        if(!profile)
            continue;
        for(int i = 0; i < kNumMidiTargets; i++)
        {
            const MidiBinding& b = profile->bindings[i];
            if(!b.Bound() || active[i].Bound())
                continue;
            if(std::none_of(active.begin(), active.end(), [&](const MidiBinding& a) { return a.SameMessage(b); }))
                active[i] = b;
        }
    }
    return active;
}

// ---- Learning ----------------------------------------------------------------------------------

KnobMode GuessKnobMode(const std::vector<uint8_t>& values)
{
    // A knob with ends sends a new value each step; an encoder repeats its step or two.
    const std::set<uint8_t> distinct(values.begin(), values.end());
    if(values.empty() || distinct.size() * 2 > values.size())
        return KnobMode::kAbsolute;
    auto any = [&](int lo, int hi) {
        return std::any_of(values.begin(), values.end(), [&](uint8_t v) { return v >= lo && v <= hi; });
    };
    if(!any(0, 47) && !any(81, 127))
        return KnobMode::kRelativeOffset; // all round 64
    if(any(96, 127))
        return KnobMode::kRelativeTwos; // a step back near 127
    if(any(65, 95))
        return KnobMode::kRelativeSigned; // a step back just over 64
    return KnobMode::kRelativeTwos; // only steps forward: two's complement is the commonest
}

std::optional<MidiBinding> MidiLearner::Feed(const uint8_t message[3])
{
    const uint8_t status = message[0] & 0xf0, channel = message[0] & 0x0f;
    const bool    cc = status == 0xb0, note_on = status == 0x90 && message[2] > 0;
    if(!turn_)
    {
        if(!cc && !note_on)
            return std::nullopt;
        MidiBinding b;
        b.type    = cc ? MidiBinding::Type::kCc : MidiBinding::Type::kNote;
        b.channel = channel;
        b.number  = message[1] & 127;
        return b;
    }
    if(!cc)
        return std::nullopt;
    MidiBinding heard;
    heard.type    = MidiBinding::Type::kCc;
    heard.channel = channel;
    heard.number  = message[1] & 127;
    if(values_.empty() || !heard.SameMessage(hearing_))
    {
        // Another knob: start again with it.
        hearing_ = heard;
        values_.clear();
    }
    values_.push_back(message[2] & 127);
    if(int(values_.size()) < kTurnMessages)
        return std::nullopt;
    hearing_.mode = GuessKnobMode(values_);
    return hearing_;
}

// ---- Playing -----------------------------------------------------------------------------------

MidiMapper::MidiMapper()
{
    for(auto& e : table_)
        e.store(0, std::memory_order_relaxed);
    for(auto& h : heard_)
        h.store(0, std::memory_order_relaxed);
    for(auto& t : turned_)
        t.store(0, std::memory_order_relaxed);
}

uint8_t MidiMapper::FirmwareCc(int encoder)
{
    static constexpr uint8_t kCc[kNumEncoders] = {21, 22, 23, 20, 24, 25};
    return kCc[encoder - 1];
}

size_t MidiMapper::TableIndex(MidiBinding::Type type, int channel, int number)
{
    return size_t(type == MidiBinding::Type::kCc) * 16 * 128 + size_t(channel & 15) * 128 + size_t(number & 127);
}

void MidiMapper::SetMapping(const MidiMapping& mapping)
{
    std::array<uint16_t, kTableSize> table{};
    for(int i = 0; i < kNumMidiTargets; i++)
    {
        const MidiBinding& b = mapping[i];
        if(b.Bound())
            table[TableIndex(b.type, b.channel, b.number)] = uint16_t((i + 1) | int(b.mode) << 8);
    }
    for(size_t i = 0; i < kTableSize; i++)
        table_[i].store(table[i], std::memory_order_relaxed);
}

void MidiMapper::SetLearning(bool learning)
{
    // Only the UI thread reads, so it can drop what's left from last time.
    if(learning)
        heard_tail_.store(heard_head_.load(std::memory_order_acquire), std::memory_order_relaxed);
    learning_.store(learning, std::memory_order_relaxed);
}

bool MidiMapper::PopHeard(uint8_t message[3])
{
    const uint32_t tail = heard_tail_.load(std::memory_order_relaxed);
    if(tail == heard_head_.load(std::memory_order_acquire))
        return false;
    const uint32_t packed = heard_[tail % kRing].load(std::memory_order_relaxed);
    message[0]            = uint8_t(packed >> 16);
    message[1]            = uint8_t(packed >> 8);
    message[2]            = uint8_t(packed);
    heard_tail_.store(tail + 1, std::memory_order_release);
    return true;
}

MidiMapper::Result MidiMapper::Process(const uint8_t* data, size_t size, PanelState& panel, uint8_t out[3])
{
    if(size != 3)
        return Result::kPass;
    const uint8_t status = data[0] & 0xf0;
    if(status != 0x80 && status != 0x90 && status != 0xb0)
        return Result::kPass;
    const bool    cc = status == 0xb0, release = status == 0x80 || (status == 0x90 && data[2] == 0);
    const uint8_t number = data[1] & 127, value = data[2] & 127;

    if(learning_.load(std::memory_order_relaxed))
    {
        const uint32_t head = heard_head_.load(std::memory_order_relaxed);
        if(head - heard_tail_.load(std::memory_order_acquire) < kRing)
        {
            heard_[head % kRing].store(uint32_t(data[0]) << 16 | uint32_t(number) << 8 | value,
                                       std::memory_order_relaxed);
            heard_head_.store(head + 1, std::memory_order_release);
        }
        // Releases still let go of what they hold.
        if(!release)
            return Result::kConsumed;
    }

    const uint16_t entry = table_[TableIndex(cc ? MidiBinding::Type::kCc : MidiBinding::Type::kNote, data[0], number)]
                               .load(std::memory_order_relaxed);
    if(!entry)
        return Result::kPass;
    const int        target = (entry & 0xff) - 1;
    const KnobMode   mode   = KnobMode(entry >> 8);
    const MidiTarget t      = MidiTargetAt(target);
    if(t.kind != MidiTarget::Kind::kKnobTurn)
    {
        Press(target, cc ? value >= 64 : !release, panel);
        return Result::kConsumed;
    }
    if(mode == KnobMode::kAbsolute)
    {
        out[0] = uint8_t(0xb0 | firmware_channel_.load(std::memory_order_relaxed));
        out[1] = FirmwareCc(t.index);
        out[2] = value;
        return Result::kFirmware;
    }
    if(const int steps = RelativeSteps(mode, value))
    {
        // Faster than 1:1 when turned fast, as the mouse is (see TurnGain).
        const int   e     = t.index - 1;
        const float rate  = turn_rate_[e].Update(float(steps), TurnRate::Clock::now());
        const float whole = steps * TurnGain(rate) + turn_carry_[e];
        const int   turn  = int(std::trunc(whole));
        turn_carry_[e]    = whole - float(turn);
        turned_[e].fetch_add(QueueTurn(panel, t.index, turn), std::memory_order_relaxed);
    }
    return Result::kConsumed;
}

void MidiMapper::Press(int target, bool down, PanelState& panel)
{
    if(down_[target] == down)
        return;
    down_[target]      = down;
    const MidiTarget t = MidiTargetAt(target);
    switch(t.kind)
    {
        case MidiTarget::Kind::kKey: panel.SetKey(t.index, down); break;
        case MidiTarget::Kind::kKnobPush: panel.SetEncoderPushed(t.index, down); break;
        case MidiTarget::Kind::kToggle:
            if(down)
                panel.SetToggle(!panel.Toggle());
            break;
        case MidiTarget::Kind::kKnobTurn: break;
    }
}

} // namespace champi
