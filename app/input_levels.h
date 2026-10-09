// The volume of CHAMPI's audio inputs, mic and line in L and R, set in the connections menu and
// kept in input_levels.toml.
#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace champi
{
/**
 * Each input's volume, from 0 to 100 percent: 100 passes it as it is. Volumes follow a cubic
 * curve, as PipeWire's do, so 50% is about -18 dB.
 *
 *     mic = 60
 *     line_l = 100
 *     line_r = 100
 *
 * Inputs the file doesn't name are at 100. Indices are the host's inputs, which are also CHAMPI's
 * ports kMic, kLineL and kLineR.
 */
struct InputLevels
{
    static constexpr int kInputs = 3;
    static constexpr int kMax    = 100;
    static constexpr int kStep   = 5; // an arrow key's worth

    std::array<int, kInputs> percent{kMax, kMax, kMax};

    /** The gain for a volume. */
    static float Gain(int percent)
    {
        const float v = float(percent) / kMax;
        return v * v * v;
    }

    /** Parses input_levels.toml. Throws std::runtime_error with the line on anything it doesn't
     *  understand. */
    static InputLevels Parse(std::string_view toml, const std::string& source = "input_levels");
    /** Reads a file; empty if it doesn't exist. Throws as Parse, or if it can't be read. */
    static std::optional<InputLevels> Load(const std::filesystem::path& path);

    std::string ToToml() const;
    /** Writes the file, creating its directory, by way of a temporary file. Throws on failure. */
    void Save(const std::filesystem::path& path) const;

    bool operator==(const InputLevels& o) const { return percent == o.percent; }
    bool operator!=(const InputLevels& o) const { return !(*this == o); }
};

} // namespace champi
