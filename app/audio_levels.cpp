#include "audio_levels.h"

#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include "toml_lines.h"

namespace champi
{
AudioLevels AudioLevels::Parse(std::string_view toml, const std::string& source)
{
    AudioLevels           levels;
    std::set<std::string> seen;
    const auto lines = ParseTomlLines(toml, source, "a volume from 0 to 100",
                                      "tables aren't used in audio_levels.toml; write `port = volume` lines");
    for(const TomlLine& l : lines)
    {
        const int port = ChampiPortByName(l.name);
        if(port < 0 || !Has(port))
            TomlFail(source, l.line, "CHAMPI has no audio port called \"" + l.name + "\"");
        if(!seen.insert(l.name).second)
            TomlFail(source, l.line, l.name + " is set twice");
        if(l.is_array || l.values[0].is_string || l.values[0].number > kMax)
            TomlFail(source, l.line, "a volume is a number from 0 to 100");
        levels.percent[port] = int(l.values[0].number);
    }
    return levels;
}

std::optional<AudioLevels> AudioLevels::Load(const std::filesystem::path& path)
{
    std::error_code ec;
    if(!std::filesystem::exists(path, ec))
        return std::nullopt;
    std::ifstream in(path);
    if(!in)
        throw std::runtime_error("can't read " + path.string());
    std::stringstream text;
    text << in.rdbuf();
    return Parse(text.str(), path.string());
}

std::string AudioLevels::ToToml() const
{
    std::string out = "# The volume of CHAMPI's audio ports, from 0 to 100, written by the connections\n"
                      "# menu (F8). 100 passes an input as it is, and 50 an output.\n\n";
    for(int i = 0; i < kNumChampiPorts; i++)
        if(Has(i))
            out += std::string(kChampiPorts[i].name) + " = " + std::to_string(percent[i]) + "\n";
    return out;
}

void AudioLevels::Save(const std::filesystem::path& path) const
{
    if(path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());
    const std::filesystem::path tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << ToToml();
        if(!out.flush())
            throw std::runtime_error("can't write " + tmp.string());
    }
    std::filesystem::rename(tmp, path);
}

} // namespace champi
