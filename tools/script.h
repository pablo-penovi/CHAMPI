// champi-headless scripts: what to do to the panel, and when.
//
// One command per line; `#` starts a comment. Commands run in order, as soon as the one before is
// done, so time only passes in `wait` and `boot`:
//
//   boot [<timeout>]          wait until TAPE has booted (default timeout 60s)
//   wait <duration>           let the firmware run, e.g. 250ms, 2s, 1.5s
//   key <1-28> down|up        press or release KEYn
//   push <1-6> down|up        push or release ENCn
//   turn <1-6> <detents>      turn ENCn, e.g. +10 or -3 (positive is clockwise for the firmware)
//   toggle on|off             the toggle switch
//   linein on|off             plug a cable into the line-in jack, or pull it out
//   midi <byte>...            send bytes to the TRS MIDI input, in hex: midi 90 3c 7f
//   usb on|off                USB power to the charger
//   battery <millivolts>      the battery voltage
//   mark <text>               write a marker line into the log
#pragma once

#include <cstdint>
#include <istream>
#include <stdexcept>
#include <string>
#include <vector>

namespace champi
{
struct Command
{
    enum class Type
    {
        kBoot,
        kWait,
        kKey,
        kPush,
        kTurn,
        kToggle,
        kLineIn,
        kMidi,
        kUsb,
        kBattery,
        kMark,
    };

    Type                 type;
    int                  line   = 0; // in the script, from 1
    int                  target = 0; // KEYn or ENCn
    int                  value  = 0; // 1/0 for down/up and on/off, detents, millivolts
    uint32_t             ms     = 0; // boot and wait
    std::vector<uint8_t> bytes;      // midi
    std::string          text;       // the command as written, for the log
};

/** A script line that doesn't parse. The message names the line. */
class ScriptError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/** Parses a whole script. Throws ScriptError on the first bad line. */
std::vector<Command> ParseScript(std::istream& in);

/** Parses a duration: a number followed by ms or s. Throws std::invalid_argument. */
uint32_t ParseDuration(const std::string& text);

} // namespace champi
