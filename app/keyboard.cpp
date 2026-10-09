#include "keyboard.h"

#include <linux/input-event-codes.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace champi
{
namespace
{
struct NamedKey
{
    const char* name;
    Scancode    code;
};

// The names keymap.toml knows, as on a US keyboard. Anything else can be given by number.
constexpr NamedKey kKeyNames[] = {
    {"A", KEY_A}, {"B", KEY_B}, {"C", KEY_C}, {"D", KEY_D}, {"E", KEY_E}, {"F", KEY_F}, {"G", KEY_G},
    {"H", KEY_H}, {"I", KEY_I}, {"J", KEY_J}, {"K", KEY_K}, {"L", KEY_L}, {"M", KEY_M}, {"N", KEY_N},
    {"O", KEY_O}, {"P", KEY_P}, {"Q", KEY_Q}, {"R", KEY_R}, {"S", KEY_S}, {"T", KEY_T}, {"U", KEY_U},
    {"V", KEY_V}, {"W", KEY_W}, {"X", KEY_X}, {"Y", KEY_Y}, {"Z", KEY_Z},

    {"1", KEY_1}, {"2", KEY_2}, {"3", KEY_3}, {"4", KEY_4}, {"5", KEY_5}, {"6", KEY_6}, {"7", KEY_7},
    {"8", KEY_8}, {"9", KEY_9}, {"0", KEY_0},

    {"F1", KEY_F1}, {"F2", KEY_F2}, {"F3", KEY_F3}, {"F4", KEY_F4}, {"F5", KEY_F5}, {"F6", KEY_F6},
    {"F7", KEY_F7}, {"F8", KEY_F8}, {"F9", KEY_F9}, {"F10", KEY_F10}, {"F11", KEY_F11}, {"F12", KEY_F12},

    {"Escape", KEY_ESC}, {"Grave", KEY_GRAVE}, {"Minus", KEY_MINUS}, {"Equal", KEY_EQUAL},
    {"Backspace", KEY_BACKSPACE}, {"Tab", KEY_TAB}, {"LeftBracket", KEY_LEFTBRACE},
    {"RightBracket", KEY_RIGHTBRACE}, {"Backslash", KEY_BACKSLASH}, {"CapsLock", KEY_CAPSLOCK},
    {"Semicolon", KEY_SEMICOLON}, {"Apostrophe", KEY_APOSTROPHE}, {"Enter", KEY_ENTER},
    {"IntlBackslash", KEY_102ND}, {"Comma", KEY_COMMA}, {"Period", KEY_DOT}, {"Slash", KEY_SLASH},
    {"Space", KEY_SPACE},

    {"LeftShift", KEY_LEFTSHIFT}, {"RightShift", KEY_RIGHTSHIFT}, {"LeftCtrl", KEY_LEFTCTRL},
    {"RightCtrl", KEY_RIGHTCTRL}, {"LeftAlt", KEY_LEFTALT}, {"RightAlt", KEY_RIGHTALT},
    {"LeftSuper", KEY_LEFTMETA}, {"RightSuper", KEY_RIGHTMETA}, {"Menu", KEY_COMPOSE},

    {"Insert", KEY_INSERT}, {"Delete", KEY_DELETE}, {"Home", KEY_HOME}, {"End", KEY_END},
    {"PageUp", KEY_PAGEUP}, {"PageDown", KEY_PAGEDOWN}, {"Up", KEY_UP}, {"Down", KEY_DOWN},
    {"Left", KEY_LEFT}, {"Right", KEY_RIGHT},

    {"NumLock", KEY_NUMLOCK}, {"ScrollLock", KEY_SCROLLLOCK}, {"KP0", KEY_KP0}, {"KP1", KEY_KP1},
    {"KP2", KEY_KP2}, {"KP3", KEY_KP3}, {"KP4", KEY_KP4}, {"KP5", KEY_KP5}, {"KP6", KEY_KP6},
    {"KP7", KEY_KP7}, {"KP8", KEY_KP8}, {"KP9", KEY_KP9}, {"KPDot", KEY_KPDOT},
    {"KPEnter", KEY_KPENTER}, {"KPPlus", KEY_KPPLUS}, {"KPMinus", KEY_KPMINUS},
    {"KPAsterisk", KEY_KPASTERISK}, {"KPSlash", KEY_KPSLASH},
};

constexpr Scancode kMaxScancode = KEY_MAX;

bool SameName(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower((unsigned char)x) == std::tolower((unsigned char)y);
           });
}

// Every action, in the order ToToml writes them.
std::vector<Action> AllActions()
{
    using K = Action::Kind;
    std::vector<Action> all;
    for(int k = 1; k <= kNumKeys; k++)
        all.push_back({K::kKey, k});
    all.push_back({K::kToggle, 0});
    all.push_back({K::kLineIn, 0});
    for(int e = 1; e <= kNumEncoders; e++)
        all.push_back({K::kSelectEncoder, e});
    all.push_back({K::kTurnLeft, 0});
    all.push_back({K::kTurnRight, 0});
    all.push_back({K::kPush, 0});
    return all;
}

// Just enough TOML for a keymap: `name = value` lines, where a value is a string, an integer or
// an array of them (which may span lines), and # comments.
class KeymapParser
{
  public:
    KeymapParser(std::string_view text, const std::string& source) : text_(text), source_(source) {}

    // Calls set(action name, keys) for each line.
    template <typename F>
    void Parse(F&& set)
    {
        for(;;)
        {
            SkipBlank(true);
            if(AtEnd())
                return;
            if(Peek() == '[')
                Fail("tables aren't used in a keymap; write `action = \"Key\"` lines");
            const int         line = line_;
            const std::string name = BareKey();
            SkipBlank(false);
            Expect('=');
            SkipBlank(false);
            std::vector<Scancode> keys;
            if(Peek() == '[')
            {
                Next();
                for(;;)
                {
                    SkipBlank(true);
                    if(Peek() == ']')
                        break;
                    keys.push_back(Key());
                    SkipBlank(true);
                    if(Peek() != ',')
                        break;
                    Next();
                }
                SkipBlank(true);
                Expect(']');
            }
            else
                keys.push_back(Key());
            SkipBlank(false);
            if(!AtEnd() && Peek() != '\n')
                Fail("expected the end of the line");
            set(name, keys, line);
        }
    }

    [[noreturn]] void Fail(const std::string& what, int line = 0) const
    {
        throw std::runtime_error(source_ + ":" + std::to_string(line ? line : line_) + ": " + what);
    }

  private:
    bool AtEnd() const { return pos_ >= text_.size(); }
    char Peek() const { return AtEnd() ? '\0' : text_[pos_]; }
    char Next()
    {
        const char c = text_[pos_++];
        if(c == '\n')
            line_++;
        return c;
    }

    void Expect(char c)
    {
        if(Peek() != c)
            Fail(std::string("expected '") + c + "'");
        Next();
    }

    // Spaces, tabs and comments, and newlines too if `newlines`.
    void SkipBlank(bool newlines)
    {
        while(!AtEnd())
        {
            const char c = Peek();
            if(c == '#')
                while(!AtEnd() && Peek() != '\n')
                    Next();
            else if(c == ' ' || c == '\t' || c == '\r' || (newlines && c == '\n'))
                Next();
            else
                return;
        }
    }

    std::string BareKey()
    {
        std::string name;
        while(!AtEnd() && (std::isalnum((unsigned char)Peek()) || Peek() == '_' || Peek() == '-'))
            name += Next();
        if(name.empty())
            Fail("expected an action name");
        return name;
    }

    Scancode Key()
    {
        const char c = Peek();
        if(c == '"' || c == '\'')
        {
            Next();
            std::string name;
            while(!AtEnd() && Peek() != c && Peek() != '\n')
                name += Next();
            Expect(c);
            const auto code = ScancodeFromName(name);
            if(!code)
                Fail("no key is called \"" + name + "\"");
            return *code;
        }
        if(std::isdigit((unsigned char)c))
        {
            long code = 0;
            while(std::isdigit((unsigned char)Peek()))
            {
                code = code * 10 + (Next() - '0');
                if(code > kMaxScancode)
                    Fail("scancodes go up to " + std::to_string(kMaxScancode));
            }
            if(code == 0)
                Fail("scancode 0 isn't a key");
            return Scancode(code);
        }
        Fail("expected a key name in quotes, a scancode, or a [list] of them");
    }

    std::string_view   text_;
    const std::string& source_;
    size_t             pos_  = 0;
    int                line_ = 1;
};
} // namespace

std::optional<Scancode> ScancodeFromName(std::string_view name)
{
    for(const NamedKey& k : kKeyNames)
        if(SameName(name, k.name))
            return k.code;
    return std::nullopt;
}

std::string ScancodeName(Scancode code)
{
    for(const NamedKey& k : kKeyNames)
        if(k.code == code)
            return k.name;
    return std::to_string(code);
}

std::string ActionName(const Action& a)
{
    using K = Action::Kind;
    switch(a.kind)
    {
        case K::kKey:
            switch(a.index)
            {
                case kChompiKey: return "chompi";
                case kPlayKey: return "play";
                case kLoopKey: return "loop";
                default: return "key_" + std::to_string(a.index);
            }
        case K::kSelectEncoder: return "encoder_" + std::to_string(a.index);
        case K::kTurnLeft: return "turn_left";
        case K::kTurnRight: return "turn_right";
        case K::kPush: return "push";
        case K::kToggle: return "toggle";
        case K::kLineIn: return "line_in";
        case K::kNone: break;
    }
    return "none";
}

std::optional<Action> ActionFromName(std::string_view name)
{
    for(const Action& a : AllActions())
        if(ActionName(a) == name)
            return a;
    return std::nullopt;
}

Keymap Keymap::Defaults()
{
    using K = Action::Kind;
    Keymap m;
    // Two octaves, tracker style: the lower one on the bottom letter row with its black keys on the
    // home row, the upper one on the top letter row with its black keys on the digits.
    const Scancode white[15] = {KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M, KEY_Q,
                                KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I};
    const Scancode black[10] = {KEY_S, KEY_D, KEY_G, KEY_H, KEY_J, KEY_2, KEY_3, KEY_5, KEY_6, KEY_7};
    for(int i = 0; i < 15; i++)
        m.Bind(white[i], {K::kKey, 1 + i});
    for(int i = 0; i < 10; i++)
        m.Bind(black[i], {K::kKey, 16 + i});
    m.Bind(KEY_TAB, {K::kKey, kChompiKey});
    m.Bind(KEY_SPACE, {K::kKey, kPlayKey});
    m.Bind(KEY_ENTER, {K::kKey, kLoopKey});
    m.Bind(KEY_GRAVE, {K::kToggle, 0});
    m.Bind(KEY_F12, {K::kLineIn, 0});

    // F1-F6 pick the encoders in the order they sit on the panel, left to right.
    const int      left_to_right[kNumEncoders] = {4, 1, 2, 3, 5, 6};
    const Scancode select[kNumEncoders]        = {KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6};
    for(int i = 0; i < kNumEncoders; i++)
        m.Bind(select[i], {K::kSelectEncoder, left_to_right[i]});
    m.Bind(KEY_LEFT, {K::kTurnLeft, 0});
    m.Bind(KEY_LEFTBRACE, {K::kTurnLeft, 0});
    m.Bind(KEY_RIGHT, {K::kTurnRight, 0});
    m.Bind(KEY_RIGHTBRACE, {K::kTurnRight, 0});
    m.Bind(KEY_BACKSLASH, {K::kPush, 0});
    return m;
}

void Keymap::Apply(std::string_view toml, const std::string& source)
{
    Keymap                    result = *this;
    std::set<std::string>     actions_seen;
    std::map<Scancode, int>   keys_seen; // and the line they were set on
    KeymapParser              parser(toml, source);
    parser.Parse([&](const std::string& name, const std::vector<Scancode>& keys, int line) {
        const auto action = ActionFromName(name);
        if(!action)
            parser.Fail("no action is called \"" + name + "\"", line);
        if(!actions_seen.insert(name).second)
            parser.Fail(name + " is set twice", line);
        result.Unbind(*action);
        for(Scancode code : keys)
        {
            if(const auto it = keys_seen.find(code); it != keys_seen.end())
                parser.Fail(ScancodeName(code) + " is already set on line " + std::to_string(it->second), line);
            keys_seen[code] = line;
            result.Bind(code, *action);
        }
    });
    *this = std::move(result);
}

void Keymap::Load(const std::string& path)
{
    std::ifstream in(path);
    if(!in)
        throw std::runtime_error("can't read " + path);
    std::stringstream text;
    text << in.rdbuf();
    Apply(text.str(), path);
}

void Keymap::Unbind(const Action& action)
{
    for(auto it = keys_.begin(); it != keys_.end();)
        it = it->second == action ? keys_.erase(it) : std::next(it);
}

Action Keymap::Lookup(Scancode code) const
{
    const auto it = keys_.find(code);
    return it == keys_.end() ? Action{} : it->second;
}

std::vector<Scancode> Keymap::KeysFor(const Action& action) const
{
    std::vector<Scancode> keys;
    for(const auto& [code, a] : keys_)
        if(a == action)
            keys.push_back(code);
    return keys;
}

std::string Keymap::ToToml() const
{
    std::string out = "# CHAMPI keymap: which computer key plays what on the panel.\n"
                      "# Keys are physical and named as on a US keyboard, whatever your layout; a\n"
                      "# number is a Linux scancode. An action can take a [list] of keys, or [].\n"
                      "# key_1-15 are the white keys, key_16-25 the black ones. encoder_n selects\n"
                      "# ENCn (ENC4 is the leftmost) for turn_left, turn_right and push.\n\n";
    for(const Action& a : AllActions())
    {
        const auto keys = KeysFor(a);
        auto       key  = [](Scancode c) {
            const std::string name = ScancodeName(c);
            return ScancodeFromName(name) ? "\"" + name + "\"" : name;
        };
        out += ActionName(a) + " = ";
        if(keys.size() == 1)
            out += key(keys[0]);
        else
        {
            out += "[";
            for(size_t i = 0; i < keys.size(); i++)
                out += (i ? ", " : "") + key(keys[i]);
            out += "]";
        }
        out += "\n";
    }
    return out;
}

bool KeyboardControl::Press(Scancode code, Clock::time_point now)
{
    using K = Action::Kind;
    if(held_.count(code))
        return true; // a repeat
    const Action action = keymap_.Lookup(code);
    if(action.kind == K::kNone)
        return false;
    used_       = true;
    Held& held  = held_[code];
    held.action = action;
    switch(action.kind)
    {
        case K::kKey:
            if(key_holds_[action.index - 1]++ == 0)
                panel_.SetKey(action.index, true);
            break;
        case K::kSelectEncoder:
            selected_ = action.index;
            break;
        case K::kTurnLeft:
        case K::kTurnRight:
            repeat_key_  = code;
            repeat_dir_  = action.kind == K::kTurnRight ? 1 : -1;
            next_repeat_ = now + kRepeatDelay;
            Turn(repeat_dir_);
            break;
        case K::kPush:
            held.encoder = selected_;
            panel_.SetEncoderPushed(selected_, true);
            break;
        case K::kToggle:
            panel_.SetToggle(!panel_.Toggle());
            break;
        case K::kLineIn:
            panel_.SetLineIn(!panel_.LineIn());
            break;
        case K::kNone:
            break;
    }
    return true;
}

bool KeyboardControl::Release(Scancode code)
{
    using K = Action::Kind;
    const auto it = held_.find(code);
    if(it == held_.end())
        return false;
    const Held held = it->second;
    held_.erase(it);
    switch(held.action.kind)
    {
        case K::kKey:
            if(--key_holds_[held.action.index - 1] == 0)
                panel_.SetKey(held.action.index, false);
            break;
        case K::kPush:
            // Another push key may still hold the same encoder.
            for(const auto& [other, h] : held_)
                if(h.action.kind == K::kPush && h.encoder == held.encoder)
                    return true;
            panel_.SetEncoderPushed(held.encoder, false);
            break;
        default:
            break;
    }
    if(code == repeat_key_)
        repeat_key_ = -1;
    return true;
}

void KeyboardControl::ReleaseAll()
{
    while(!held_.empty())
        Release(held_.begin()->first);
}

void KeyboardControl::Tick(Clock::time_point now)
{
    if(repeat_key_ < 0 || now < next_repeat_)
        return;
    Turn(repeat_dir_);
    // A late tick turns once, not by every repeat it missed.
    next_repeat_ = std::max(next_repeat_ + kRepeatEvery, now);
}

void KeyboardControl::Turn(int detents)
{
    panel_.TurnEncoder(selected_, detents);
    turned_[selected_ - 1] += detents;
}

} // namespace champi
