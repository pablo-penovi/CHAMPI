#include "script.h"

#include <cmath>
#include <sstream>

#include "panel_state.h"

namespace champi
{
namespace
{
// A number in [lo, hi], written in decimal with an optional sign.
int ParseInt(const std::string& text, int lo, int hi, const char* what)
{
    size_t used  = 0;
    long   value = 0;
    try
    {
        value = std::stol(text, &used, 10);
    }
    catch(const std::exception&)
    {
        used = 0;
    }
    if(used == 0 || used != text.size() || value < lo || value > hi)
        throw std::invalid_argument(std::string(what) + " must be " + std::to_string(lo) + " to "
                                    + std::to_string(hi) + ", not " + text);
    return int(value);
}

// A decimal number in [lo, hi].
double ParseNumber(const std::string& text, double lo, double hi, const char* what)
{
    size_t used  = 0;
    double value = 0;
    try
    {
        value = std::stod(text, &used);
    }
    catch(const std::exception&)
    {
        used = 0;
    }
    if(used == 0 || used != text.size() || !(value >= lo && value <= hi))
    {
        std::ostringstream message;
        message << what << " must be " << lo << " to " << hi << ", not " << text;
        throw std::invalid_argument(message.str());
    }
    return value;
}

// "down"/"up" or "on"/"off".
int ParseSwitch(const std::string& text, const char* on, const char* off)
{
    if(text == on)
        return 1;
    if(text == off)
        return 0;
    throw std::invalid_argument(std::string("expected ") + on + " or " + off + ", not " + text);
}

Command ParseCommand(const std::vector<std::string>& words)
{
    const std::string& name = words[0];
    auto               args = [&](size_t lo, size_t hi) {
        const size_t n = words.size() - 1;
        if(n < lo || n > hi)
            throw std::invalid_argument(name + " takes " + std::to_string(lo)
                                        + (hi > lo ? " to " + std::to_string(hi) : std::string())
                                        + " argument" + (hi == 1 ? "" : "s"));
    };

    Command c{};
    if(name == "boot")
    {
        args(0, 1);
        c.type = Command::Type::kBoot;
        c.ms   = words.size() > 1 ? ParseDuration(words[1]) : 60000;
    }
    else if(name == "wait")
    {
        args(1, 1);
        c.type = Command::Type::kWait;
        c.ms   = ParseDuration(words[1]);
    }
    else if(name == "key")
    {
        args(2, 2);
        c.type   = Command::Type::kKey;
        c.target = ParseInt(words[1], 1, kNumKeys, "key");
        c.value  = ParseSwitch(words[2], "down", "up");
    }
    else if(name == "push")
    {
        args(2, 2);
        c.type   = Command::Type::kPush;
        c.target = ParseInt(words[1], 1, kNumEncoders, "encoder");
        c.value  = ParseSwitch(words[2], "down", "up");
    }
    else if(name == "turn")
    {
        args(2, 2);
        c.type   = Command::Type::kTurn;
        c.target = ParseInt(words[1], 1, kNumEncoders, "encoder");
        c.value  = ParseInt(words[2], -1000, 1000, "detents");
    }
    else if(name == "toggle" || name == "linein" || name == "usb")
    {
        args(1, 1);
        c.type  = name == "toggle"   ? Command::Type::kToggle
                  : name == "linein" ? Command::Type::kLineIn
                                     : Command::Type::kUsb;
        c.value = ParseSwitch(words[1], "on", "off");
    }
    else if(name == "midi")
    {
        if(words.size() < 2)
            throw std::invalid_argument("midi needs at least one byte");
        c.type = Command::Type::kMidi;
        for(size_t i = 1; i < words.size(); i++)
        {
            size_t        used = 0;
            unsigned long byte = 0;
            try
            {
                byte = std::stoul(words[i], &used, 16);
            }
            catch(const std::exception&)
            {
                used = 0;
            }
            if(used == 0 || used != words[i].size() || byte > 0xff)
                throw std::invalid_argument("not a hex byte: " + words[i]);
            c.bytes.push_back(uint8_t(byte));
        }
    }
    else if(name == "battery")
    {
        args(1, 1);
        c.type  = Command::Type::kBattery;
        c.value = ParseInt(words[1], 0, 5000, "millivolts");
    }
    else if(name == "input")
    {
        if(words.size() < 3)
            throw std::invalid_argument("input takes mic or line, then sine <hz> [<level>] or off");
        c.type   = Command::Type::kInput;
        c.target = int(ParseSwitch(words[1], "line", "mic") ? Input::kLine : Input::kMic);
        if(words[2] == "off")
            args(2, 2);
        else if(words[2] == "sine")
        {
            args(3, 4);
            c.hz    = ParseNumber(words[3], 1, 20000, "hz");
            c.level = words.size() > 4 ? ParseNumber(words[4], 0, 1, "level") : 0.5;
        }
        else
            throw std::invalid_argument("expected sine or off, not " + words[2]);
    }
    else if(name == "midiloop")
    {
        args(1, 1);
        c.type  = Command::Type::kMidiLoop;
        c.value = ParseSwitch(words[1], "on", "off");
    }
    else if(name == "sd")
        throw std::invalid_argument("sd out|in was removed in 1.3: the card can't be pulled out any more. Run "
                                    "with --sd-dir to choose the card");
    else if(name == "mark")
    {
        if(words.size() < 2)
            throw std::invalid_argument("mark needs some text");
        c.type = Command::Type::kMark;
    }
    else
        throw std::invalid_argument("unknown command " + name);
    return c;
}

} // namespace

uint32_t ParseDuration(const std::string& text)
{
    double scale = 0;
    size_t unit  = 0;
    if(text.size() > 2 && text.compare(text.size() - 2, 2, "ms") == 0)
        scale = 1, unit = 2;
    else if(text.size() > 1 && text.back() == 's')
        scale = 1000, unit = 1;
    else
        throw std::invalid_argument("a duration needs ms or s: " + text);

    const std::string number = text.substr(0, text.size() - unit);
    size_t            used   = 0;
    double            value  = -1;
    try
    {
        value = std::stod(number, &used);
    }
    catch(const std::exception&)
    {
        used = 0;
    }
    if(used == 0 || used != number.size() || !(value >= 0) || value * scale > 24 * 3600e3)
        throw std::invalid_argument("not a duration: " + text);
    return uint32_t(std::lround(value * scale));
}

std::vector<Command> ParseScript(std::istream& in)
{
    std::vector<Command> commands;
    std::string          line;
    for(int number = 1; std::getline(in, line); number++)
    {
        if(const auto hash = line.find('#'); hash != std::string::npos)
            line.resize(hash);

        std::istringstream       words_in(line);
        std::vector<std::string> words;
        for(std::string w; words_in >> w;)
            words.push_back(w);
        if(words.empty())
            continue;

        try
        {
            Command c = ParseCommand(words);
            c.line    = number;
            for(const auto& w : words)
                c.text += (c.text.empty() ? "" : " ") + w;
            commands.push_back(std::move(c));
        }
        catch(const std::invalid_argument& e)
        {
            throw ScriptError("line " + std::to_string(number) + ": " + e.what());
        }
    }
    return commands;
}

} // namespace champi
