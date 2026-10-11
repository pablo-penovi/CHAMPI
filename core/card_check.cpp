#include "card_check.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <utility>

namespace fs = std::filesystem;

namespace champi
{
namespace
{
// Where the limits come from, in chompi-tape/code/src:
//
// - Samples: DSPEngine.h GetFileNameForSlot names them <mode>_<bank><slot>[_double].wav, with
//   banks a-e (bank % 5) and slots 1-14 (kMaxSlots). FileCopier.h writes them as 48 kHz, 16-bit
//   stereo PCM, and nothing checks the format of a file it reads. At boot FileCopier rewrites a
//   file with a larger header or a footer, so those are fine.
// - options.json: OptionsManager.h reads at most kOptFileSize (4096) bytes into a buffer it then
//   treats as a C string, so 4095 is the most that's read intact. It knows kNumOptions (7) names.
// - presets.json: ui.h reads at most kPreFileSize (8192) bytes, the buffer isn't null-terminated,
//   so 8191. PresetManager.h writes [jammi, cubbi, 2]: per mode 5 banks of 14 slots, each 9
//   controls times 1000 and a "valid" boolean, then the version. Without the version (version 1)
//   it reads 7 controls.
// - TAPE's own files: presets_temp.json (ui.h WritePresets), temp_rec.wav (recording),
//   test_file.txt (TestPage.h), .batt_log.txt and ._.batt_log.txt (deleted at boot).
// - The bootloader (chompi-bootloader-v6.4-beta, shared/bootloader.cpp) flashes any file whose
//   name contains .bin.
constexpr uintmax_t kOptionsMaxBytes = 4095;
constexpr uintmax_t kPresetsMaxBytes = 8191;
constexpr int       kModes           = 2;
constexpr int       kBanks           = 5;
constexpr int       kSlots           = 14;
constexpr int       kPresetVersion   = 2;

// At most this many problems are listed for one JSON file; a broken file would give hundreds.
constexpr size_t kMaxProblemsPerFile = 8;

const char* const kSampleNames = "TAPE only reads samples named jammi_<bank><slot>.wav or "
                                 "cubbi_<bank><slot>.wav, with bank a–e and slot 1–14.";

std::string Lower(std::string s)
{
    for(char& c : s)
        c = char(std::tolower((unsigned char)c));
    return s;
}

bool EndsWith(const std::string& s, const std::string& end)
{
    return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
}

// Mac and Windows leave these on any card they touch. Lower case.
bool IsMetadata(const std::string& lname, bool is_dir)
{
    static const std::set<std::string> kDirs  = {".spotlight-v100", ".fseventsd", ".trashes", ".temporaryitems",
                                                 "system volume information", "$recycle.bin"};
    static const std::set<std::string> kFiles = {".ds_store", "desktop.ini", "thumbs.db"};
    if(lname.rfind("._", 0) == 0 && lname != "._.batt_log.txt")
        return true;
    return is_dir ? kDirs.count(lname) > 0 : kFiles.count(lname) > 0;
}

bool IsTapeOwnFile(const std::string& lname)
{
    static const std::set<std::string> kOwn = {"presets_temp.json", "temp_rec.wav", "test_file.txt", ".batt_log.txt",
                                               "._.batt_log.txt"};
    return kOwn.count(lname) > 0;
}

// ---- Samples ------------------------------------------------------------------------------------

struct SampleName
{
    std::string mode; // jammi or cubbi
    char        bank = 0;
    std::string slot; // as written
    bool        dbl  = false;
};

// Anything shaped like a sample name, whether its bank and slot exist or not. `lname` is lower case.
std::optional<SampleName> ParseSampleName(const std::string& lname)
{
    static const std::regex kPattern("(jammi|cubbi)_([a-z])([0-9]+)(_double)?\\.wav");
    std::smatch m;
    if(!std::regex_match(lname, m, kPattern))
        return std::nullopt;
    return SampleName{m[1], m[2].str()[0], m[3], m[4].matched};
}

// Why the name isn't one TAPE reads, or empty if it is.
std::string SampleNameProblem(const SampleName& s)
{
    if(s.bank < 'a' || s.bank > 'e')
        return std::string("bank ") + s.bank + " doesn't exist; banks are a–e.";
    if(s.slot.size() > 1 && s.slot[0] == '0')
    {
        const std::string slot = s.slot.substr(s.slot.find_first_not_of('0') == std::string::npos
                                                   ? s.slot.size() - 1
                                                   : s.slot.find_first_not_of('0'));
        return "TAPE writes slot numbers without a leading zero. Rename it to " + s.mode + "_" + s.bank + slot
               + (s.dbl ? "_double" : "") + ".wav.";
    }
    const long slot = s.slot.size() > 3 ? 1000 : std::stol(s.slot);
    if(slot < 1 || slot > kSlots)
        return "slot " + s.slot + " doesn't exist; slots are 1–14.";
    return "";
}

uint32_t Le32(const unsigned char* p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

uint16_t Le16(const unsigned char* p)
{
    return uint16_t(p[0] | p[1] << 8);
}

// The problems with a sample's WAV header.
std::vector<std::string> CheckWav(const fs::path& path, const std::string& name)
{
    std::vector<std::string> problems;
    std::error_code          ec;
    const uintmax_t          file_size = fs::file_size(path, ec);
    std::ifstream            in(path, std::ios::binary);
    if(ec || !in)
        return {"can't be read: " + (ec ? ec.message() : std::string("it won't open")) + "."};

    unsigned char head[12];
    if(!in.read((char*)head, sizeof head) || std::memcmp(head, "RIFF", 4) != 0 || std::memcmp(head + 8, "WAVE", 4) != 0)
        return {"not a WAV file (it doesn't start with RIFF/WAVE)."};

    // Chunks up to the RIFF size, or the end of the file if the size says more: the factory card's
    // headers claim 8 bytes more than their files hold, and TAPE never looks. Anything after the
    // RIFF size is a footer, which TAPE strips at boot.
    const uintmax_t riff_end = 8 + uintmax_t(Le32(head + 4));
    const uintmax_t end      = std::min(riff_end, file_size);
    bool            have_fmt = false, have_data = false, cut = false;
    uint16_t        format = 0, channels = 0, block_align = 0, bits = 0;
    uint32_t        rate = 0, data_size = 0;
    uintmax_t       pos  = 12;
    while(pos + 8 <= end)
    {
        unsigned char chunk[8];
        in.seekg(std::streamoff(pos));
        if(!in.read((char*)chunk, sizeof chunk))
            break;
        const std::string id(chunk, chunk + 4);
        const uint32_t    size = Le32(chunk + 4);
        if(pos + 8 + size > file_size)
        {
            problems.push_back("its " + std::string(id == "data" ? "data" : "\"" + id + "\"") + " chunk claims "
                               + std::to_string(size) + " bytes but only " + std::to_string(file_size - pos - 8)
                               + " follow: the file is cut short.");
            cut = true;
            break;
        }
        if(id == "fmt " && !have_fmt && size >= 16)
        {
            unsigned char fmt[16];
            if(!in.read((char*)fmt, sizeof fmt))
                break;
            have_fmt    = true;
            format      = Le16(fmt);
            channels    = Le16(fmt + 2);
            rate        = Le32(fmt + 4);
            block_align = Le16(fmt + 12);
            bits        = Le16(fmt + 14);
        }
        else if(id == "data" && !have_data)
        {
            have_data = true;
            data_size = size;
        }
        pos += 8 + uintmax_t(size) + (size & 1);
    }

    const std::string sox = "Convert it, for example with `sox " + name + " -r 48000 -c 2 -b 16 out.wav`.";
    if(cut)
        return problems; // what's missing is likely in what was cut off
    if(!have_fmt)
        problems.push_back("it has no fmt chunk, so its format is unknown; TAPE needs 48000 Hz, 16-bit, stereo PCM.");
    else
    {
        if(format == 3)
            problems.push_back(std::to_string(bits) + "-bit float; TAPE needs 16-bit PCM. " + sox);
        else if(format == 0xfffe)
            problems.push_back("an extensible WAV (format 0xFFFE); TAPE needs a plain PCM WAV (format 1). " + sox);
        else if(format != 1)
            problems.push_back("compressed (WAV format " + std::to_string(format) + "); TAPE needs 16-bit PCM. "
                               + sox);
        if(channels == 1)
            problems.push_back("mono; TAPE needs stereo. Convert it, for example with `sox " + name
                               + " -c 2 out.wav`.");
        else if(channels != 2)
            problems.push_back(std::to_string(channels) + " channels; TAPE needs stereo. " + sox);
        if(rate != 48000)
            problems.push_back(std::to_string(rate) + " Hz; TAPE needs 48000 Hz. Convert it, for example with `sox "
                               + name + " -r 48000 out.wav`.");
        if(format == 1 && bits != 16)
            problems.push_back(std::to_string(bits) + "-bit; TAPE needs 16-bit PCM. Convert it, for example with `sox "
                               + name + " -b 16 out.wav`.");
        if(format == 1 && channels == 2 && bits == 16 && block_align != 4)
            problems.push_back("its block align is " + std::to_string(block_align)
                               + "; 16-bit stereo has 4, so the header is broken.");
    }
    // A trailing half frame is fine: TAPE wrote some of the factory card's _double files that way.
    if(!have_data)
        problems.push_back("it has no data chunk, so it holds no sound.");
    else if(data_size < 4)
        problems.push_back("its data chunk is empty.");
    return problems;
}

// ---- JSON ---------------------------------------------------------------------------------------
//
// Strict JSON, as TAPE's coreJSON validates it, keeping each value's line for the messages.

struct Json
{
    enum class Type
    {
        kNull,
        kBool,
        kNumber,
        kString,
        kArray,
        kObject,
    };
    Type                                     type = Type::kNull;
    int                                      line = 1;
    bool                                     boolean = false;
    double                                   number  = 0;
    bool                                     integer = false; // a number written without . or e
    std::string                              text;            // a string's contents, or a scalar as written
    std::vector<Json>                        items;
    std::vector<std::pair<std::string, Json>> members;

    const Json* Member(const std::string& name) const
    {
        for(const auto& [n, v] : members)
            if(n == name)
                return &v;
        return nullptr;
    }
};

struct JsonError
{
    int         line;
    std::string message;
};

class JsonParser
{
  public:
    explicit JsonParser(const std::string& text) : s_(text) {}

    Json Parse()
    {
        Json v = Value(0);
        Space();
        if(at_ != s_.size())
            Fail("there's more after the end of the JSON");
        return v;
    }

  private:
    [[noreturn]] void Fail(const std::string& message) { throw JsonError{line_, message}; }

    void Space()
    {
        while(at_ < s_.size() && (s_[at_] == ' ' || s_[at_] == '\t' || s_[at_] == '\n' || s_[at_] == '\r'))
            if(s_[at_++] == '\n')
                line_++;
    }

    bool Eat(char c)
    {
        Space();
        if(at_ < s_.size() && s_[at_] == c)
        {
            at_++;
            return true;
        }
        return false;
    }

    void Expect(char c, const char* what)
    {
        if(!Eat(c))
            Fail(std::string("expected ") + what);
    }

    Json Value(int depth)
    {
        if(depth > 32)
            Fail("nested too deep");
        Space();
        Json v;
        v.line = line_;
        if(at_ >= s_.size())
            Fail("the file ends where a value should be");
        const char c = s_[at_];
        if(c == '{')
        {
            at_++;
            v.type = Json::Type::kObject;
            if(Eat('}'))
                return v;
            do
            {
                Space();
                if(at_ >= s_.size() || s_[at_] != '"')
                    Fail("expected a name in quotes");
                std::string name = String();
                Expect(':', ":");
                v.members.emplace_back(std::move(name), Value(depth + 1));
            } while(Eat(','));
            Expect('}', ", or }");
        }
        else if(c == '[')
        {
            at_++;
            v.type = Json::Type::kArray;
            if(Eat(']'))
                return v;
            do
                v.items.push_back(Value(depth + 1));
            while(Eat(','));
            Expect(']', ", or ]");
        }
        else if(c == '"')
        {
            v.type = Json::Type::kString;
            v.text = String();
        }
        else if(Word("true"))
            v.type = Json::Type::kBool, v.boolean = true, v.text = "true";
        else if(Word("false"))
            v.type = Json::Type::kBool, v.text = "false";
        else if(Word("null"))
            v.text = "null";
        else if(c == '-' || std::isdigit((unsigned char)c))
            Number(v);
        else
            Fail(std::string("unexpected ") + (std::isprint((unsigned char)c) ? std::string(1, c) : "character"));
        return v;
    }

    bool Word(const char* word)
    {
        const size_t n = std::strlen(word);
        if(s_.compare(at_, n, word) != 0)
            return false;
        at_ += n;
        return true;
    }

    std::string String()
    {
        std::string out;
        at_++; // the opening quote
        while(true)
        {
            if(at_ >= s_.size())
                Fail("a string isn't closed");
            const char c = s_[at_++];
            if(c == '"')
                return out;
            if((unsigned char)c < 0x20)
                Fail("a string runs over the end of the line");
            if(c != '\\')
            {
                out += c;
                continue;
            }
            if(at_ >= s_.size())
                Fail("a string isn't closed");
            const char e = s_[at_++];
            switch(e)
            {
                case '"':
                case '\\':
                case '/': out += e; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                    for(int i = 0; i < 4; i++)
                        if(at_ >= s_.size() || !std::isxdigit((unsigned char)s_[at_++]))
                            Fail("a \\u escape needs four hex digits");
                    out += '?'; // only option names are compared, and they're plain ASCII
                    break;
                default: Fail(std::string("unknown escape \\") + e);
            }
        }
    }

    void Number(Json& v)
    {
        const size_t start = at_;
        auto digits = [&] {
            const size_t from = at_;
            while(at_ < s_.size() && std::isdigit((unsigned char)s_[at_]))
                at_++;
            return at_ - from;
        };
        if(s_[at_] == '-')
            at_++;
        const size_t first = at_;
        if(digits() == 0)
            Fail("a number needs digits");
        if(s_[first] == '0' && at_ - first > 1)
            Fail("a number can't start with 0");
        v.integer = true;
        if(at_ < s_.size() && s_[at_] == '.')
        {
            at_++;
            v.integer = false;
            if(digits() == 0)
                Fail("a number needs digits after the point");
        }
        if(at_ < s_.size() && (s_[at_] == 'e' || s_[at_] == 'E'))
        {
            at_++;
            v.integer = false;
            if(at_ < s_.size() && (s_[at_] == '+' || s_[at_] == '-'))
                at_++;
            if(digits() == 0)
                Fail("a number needs digits in its exponent");
        }
        v.type   = Json::Type::kNumber;
        v.text   = s_.substr(start, at_ - start);
        v.number = std::strtod(v.text.c_str(), nullptr);
    }

    const std::string& s_;
    size_t             at_   = 0;
    int                line_ = 1;
};

std::string Line(const Json& v)
{
    return "line " + std::to_string(v.line) + ": ";
}

// What a value is, for a message: the value as written, or its kind.
std::string Shown(const Json& v)
{
    switch(v.type)
    {
        case Json::Type::kArray: return "a list";
        case Json::Type::kObject: return "an object";
        case Json::Type::kString: return "\"" + v.text + "\"";
        default: return v.text;
    }
}

// Reads a JSON file of at most `max_bytes`, or says why it can't.
std::optional<Json> ReadJson(const fs::path& path, uintmax_t max_bytes, std::vector<std::string>& problems)
{
    std::error_code ec;
    const uintmax_t size = fs::file_size(path, ec);
    if(ec)
    {
        problems.push_back("can't be read: " + ec.message() + ".");
        return std::nullopt;
    }
    if(size > max_bytes)
    {
        problems.push_back(std::to_string(size) + " bytes; TAPE reads at most " + std::to_string(max_bytes)
                           + ", so the end would be lost.");
        return std::nullopt;
    }
    std::ifstream     in(path, std::ios::binary);
    const std::string text(std::istreambuf_iterator<char>(in), {});
    if(text.find('\0') != std::string::npos)
    {
        problems.push_back("it holds a NUL byte, where TAPE would stop reading.");
        return std::nullopt;
    }
    try
    {
        return JsonParser(text).Parse();
    }
    catch(const JsonError& e)
    {
        problems.push_back("line " + std::to_string(e.line) + ": " + e.message
                           + "; this isn't valid JSON, so TAPE would ignore it and write its defaults over it.");
        return std::nullopt;
    }
}

// ---- options.json -------------------------------------------------------------------------------

enum class OptionKind
{
    kBool,
    kChannel,
    kMonitor,
};

struct Option
{
    const char* name;
    OptionKind  kind;
};

// OptionsManager.h Parse: the booleans compare against true/false, the channels take 1-16 and the
// monitor position 1-3.
constexpr Option kOptions[] = {
    {"Record Latch", OptionKind::kBool},     {"Midi In Channel", OptionKind::kChannel},
    {"Midi Out Channel", OptionKind::kChannel}, {"Tape Slew On", OptionKind::kBool},
    {"Monitor Position", OptionKind::kMonitor}, {"Pitch Quantize In Shift Menu", OptionKind::kBool},
    {"Split Delay", OptionKind::kBool},
};

std::vector<std::string> CheckOptions(const fs::path& path)
{
    std::vector<std::string> problems;
    const std::optional<Json> json = ReadJson(path, kOptionsMaxBytes, problems);
    if(!json)
        return problems;

    const char* const shape = "TAPE expects {\"chompi\": [{\"name\": …, \"value\": …}, …]}.";
    const Json*       list  = json->type == Json::Type::kObject ? json->Member("chompi") : nullptr;
    if(!list || list->type != Json::Type::kArray)
        return {Line(list ? *list : *json) + shape};

    std::set<std::string> seen;
    for(const Json& item : list->items)
    {
        const Json* name  = item.type == Json::Type::kObject ? item.Member("name") : nullptr;
        const Json* value = item.type == Json::Type::kObject ? item.Member("value") : nullptr;
        if(!name || !value || name->type != Json::Type::kString)
        {
            problems.push_back(Line(item) + "each option is {\"name\": …, \"value\": …}, with the name in quotes.");
            continue;
        }
        const Option* option = nullptr;
        for(const Option& o : kOptions)
            if(name->text == o.name)
                option = &o;
        if(!option)
        {
            problems.push_back(Line(*name) + "no option is called \"" + name->text
                               + "\"; TAPE knows Record Latch, Midi In Channel, Midi Out Channel, Tape Slew On, "
                                 "Monitor Position, Pitch Quantize In Shift Menu and Split Delay.");
            continue;
        }
        if(!seen.insert(option->name).second)
        {
            problems.push_back(Line(*name) + "\"" + name->text + "\" is set twice; TAPE takes each option once.");
            continue;
        }
        const std::string is = Line(*value) + "\"" + option->name + "\" is " + Shown(*value) + "; it must be ";
        switch(option->kind)
        {
            case OptionKind::kBool:
                if(value->type != Json::Type::kBool)
                    problems.push_back(is + "true or false.");
                break;
            case OptionKind::kChannel:
                if(value->type != Json::Type::kNumber || !value->integer || value->number < 1 || value->number > 16)
                    problems.push_back(is + "1–16.");
                break;
            case OptionKind::kMonitor:
                if(value->type != Json::Type::kNumber || !value->integer || value->number < 1 || value->number > 3)
                    problems.push_back(is + "1–3.");
                break;
        }
    }
    return problems;
}

// ---- presets.json -------------------------------------------------------------------------------

// PresetManager.h's controls, in order. Each is a 0-1 value times 1000: the encoders clip to 0-1
// (NormalPage.h and MenuPage.h, fclamp), and so does pan (SampleReader.h SetPan); auto loop and
// sustain are booleans stored as 0 or 1.
const char* const kControls[] = {"pitch", "start", "end", "attack", "decay", "auto loop", "sustain", "gain", "pan"};

std::vector<std::string> CheckPresets(const fs::path& path)
{
    std::vector<std::string>  problems;
    const std::optional<Json> json = ReadJson(path, kPresetsMaxBytes, problems);
    if(!json)
        return problems;

    const bool version2 = json->type == Json::Type::kArray && json->items.size() == kModes + 1
                          && json->items[kModes].type == Json::Type::kNumber && json->items[kModes].integer
                          && json->items[kModes].number == kPresetVersion;
    const bool version1 = json->type == Json::Type::kArray && json->items.size() == kModes;
    if(!version1 && !version2)
        return {Line(*json) + "TAPE expects a list of the JAMMI presets, the CUBBI presets and the version, 2."};
    const size_t controls = version2 ? 9 : 7;

    const char* const modes[kModes] = {"JAMMI", "CUBBI"};
    for(int mode = 0; mode < kModes; mode++)
    {
        const Json& banks = json->items[mode];
        if(banks.type != Json::Type::kArray || banks.items.size() != kBanks)
        {
            problems.push_back(Line(banks) + "the " + modes[mode] + " presets must be a list of 5 banks, a–e.");
            continue;
        }
        for(int bank = 0; bank < kBanks; bank++)
        {
            const Json&       slots = banks.items[bank];
            const std::string where = std::string(modes[mode]) + " bank " + char('a' + bank);
            if(slots.type != Json::Type::kArray || slots.items.size() != kSlots)
            {
                problems.push_back(Line(slots) + where + " must be a list of 14 slots.");
                continue;
            }
            for(int slot = 0; slot < kSlots; slot++)
            {
                const Json&       entry = slots.items[slot];
                const std::string name  = std::string(modes[mode]) + " " + char('a' + bank) + std::to_string(slot + 1);
                if(entry.type != Json::Type::kArray || entry.items.size() != controls + 1
                   || entry.items[controls].type != Json::Type::kBool)
                {
                    problems.push_back(Line(entry) + name + " must be " + std::to_string(controls)
                                       + " numbers followed by true or false.");
                    continue;
                }
                for(size_t c = 0; c < controls; c++)
                {
                    const Json& v = entry.items[c];
                    if(v.type != Json::Type::kNumber || v.number < 0 || v.number > 1000)
                        problems.push_back(Line(v) + name + ": " + kControls[c] + " is " + Shown(v)
                                           + "; it must be 0–1000.");
                }
            }
        }
    }
    return problems;
}

// Keeps a file's list short, saying how many more there were.
void Add(std::vector<CardProblem>& problems, const fs::path& path, std::vector<std::string> reasons,
         size_t max = SIZE_MAX)
{
    const size_t shown = std::min(reasons.size(), max);
    for(size_t i = 0; i < shown; i++)
        problems.push_back({path, std::move(reasons[i])});
    if(reasons.size() > shown)
        problems.push_back({path, "and " + std::to_string(reasons.size() - shown) + " more problems like these."});
}

} // namespace

std::string ToString(const CardProblem& problem)
{
    return problem.path.empty() ? problem.reason : problem.path.string() + ": " + problem.reason;
}

std::vector<CardProblem> CheckCard(const fs::path& dir)
{
    std::vector<CardProblem> problems;
    std::error_code          ec;
    const fs::file_status    status = fs::status(dir, ec);
    if(!fs::exists(status))
        return {{{}, "the folder doesn't exist."}};
    if(!fs::is_directory(status))
        return {{{}, "it's a file, not a folder; a card is a folder."}};

    std::vector<fs::directory_entry> entries;
    for(fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        entries.push_back(*it);
    if(ec)
        return {{{}, "the folder can't be read: " + ec.message() + "."}};
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.path().filename() < b.path().filename(); });

    // Lower-case names of the regular files, for the _double check, and of everything, for case
    // clashes.
    std::set<std::string>              files;
    std::map<std::string, std::string> names;
    for(const fs::directory_entry& e : entries)
        if(e.is_regular_file(ec))
            files.insert(Lower(e.path().filename().string()));

    std::optional<fs::path> first_bin;
    for(const fs::directory_entry& e : entries)
    {
        const fs::path    rel   = e.path().filename();
        const std::string name  = rel.string();
        const std::string lname = Lower(name);
        const bool        is_dir = e.is_directory(ec);
        if(IsMetadata(lname, is_dir))
            continue;
        if(const auto [it, added] = names.emplace(lname, name); !added)
        {
            problems.push_back({rel, "differs from " + it->second
                                         + " only by case, so on a FAT card they'd be the same file. Remove or "
                                           "rename one."});
            continue;
        }
        if(e.is_symlink(ec) && !fs::exists(e.path(), ec))
        {
            problems.push_back({rel, "a broken link: what it points to doesn't exist."});
            continue;
        }
        if(is_dir)
        {
            problems.push_back({rel, "TAPE only reads files at the card root. Move the files up, or remove the folder."});
            continue;
        }
        if(!e.is_regular_file(ec))
        {
            problems.push_back({rel, "not a regular file; TAPE only reads files."});
            continue;
        }

        if(const std::optional<SampleName> sample = ParseSampleName(lname))
        {
            if(const std::string bad = SampleNameProblem(*sample); !bad.empty())
            {
                problems.push_back({rel, bad});
                continue;
            }
            Add(problems, rel, CheckWav(e.path(), name));
            const std::string base = sample->mode + "_" + sample->bank + sample->slot + ".wav";
            if(sample->dbl && !files.count(base))
                problems.push_back({rel, "there's no " + base + " for it to double. Remove it; TAPE makes _double "
                                         "files itself."});
        }
        else if(lname == "options.json")
            Add(problems, rel, CheckOptions(e.path()), kMaxProblemsPerFile);
        else if(lname == "presets.json")
            Add(problems, rel, CheckPresets(e.path()), kMaxProblemsPerFile);
        else if(IsTapeOwnFile(lname))
            continue;
        else if(lname.find(".bin") != std::string::npos)
        {
            if(first_bin)
                problems.push_back({rel, "a second firmware file next to " + first_bin->string()
                                             + "; the bootloader flashes any .bin, so keep one at most."});
            else
                first_bin = rel;
        }
        else if(EndsWith(lname, ".wav"))
            problems.push_back({rel, std::string(kSampleNames) + " Rename it, for example to jammi_a1.wav."});
        else
            problems.push_back({rel, "TAPE doesn't use this file. Remove it."});
    }
    return problems;
}

} // namespace champi
