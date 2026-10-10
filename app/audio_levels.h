// The volume of CHAMPI's audio ports, mic and line in L and R, master and phones L and R, set in
// the connections menu and kept in audio_levels.toml.
#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "routing.h"

namespace champi
{
/**
 * Each audio port's volume, from 0 to 100 percent. Volumes follow a cubic curve, as PipeWire's do,
 * so halving one is about -18 dB. An input passes as it is at 100. An output does at 50, and 100
 * is +18 dB: the firmware divides its voices by eight to leave room for all of them, which leaves
 * a note or two far quieter than other programs.
 *
 *     mic = 60
 *     line_l = 100
 *     master_l = 80
 *     master_r = 80
 *
 * Ports the file doesn't name are at their Defaults. Indices are CHAMPI's ports; the MIDI ports'
 * stay at 100 and mean nothing.
 */
struct AudioLevels
{
    static constexpr int kMax         = 100;
    static constexpr int kOutputUnity = 50; // the output volume that passes the firmware's as it is
    static constexpr int kStep        = 5;  // an arrow key's worth

    std::array<int, kNumChampiPorts> percent = Defaults();

    /** Every port as it is: inputs at 100, outputs at kOutputUnity. */
    static constexpr std::array<int, kNumChampiPorts> Defaults()
    {
        std::array<int, kNumChampiPorts> all{};
        for(int i = 0; i < kNumChampiPorts; i++)
            all[i] = i >= kMasterL && i <= kPhonesR ? kOutputUnity : kMax;
        return all;
    }

    /** Whether CHAMPI's port has a volume: its audio ports. */
    static bool Has(int port) { return kChampiPorts[port].type == PortType::kAudio; }

    /** The gain for CHAMPI's port `port` at a volume. */
    static float Gain(int port, int percent)
    {
        const float v = float(percent) / (kChampiPorts[port].input ? kMax : kOutputUnity);
        return v * v * v;
    }

    /** Parses audio_levels.toml. Throws std::runtime_error with the line on anything it doesn't
     *  understand. */
    static AudioLevels Parse(std::string_view toml, const std::string& source = "audio_levels");
    /** Reads a file; empty if it doesn't exist. Throws as Parse, or if it can't be read. */
    static std::optional<AudioLevels> Load(const std::filesystem::path& path);

    std::string ToToml() const;
    /** Writes the file, creating its directory, by way of a temporary file. Throws on failure. */
    void Save(const std::filesystem::path& path) const;

    bool operator==(const AudioLevels& o) const { return percent == o.percent; }
    bool operator!=(const AudioLevels& o) const { return !(*this == o); }
};

} // namespace champi
