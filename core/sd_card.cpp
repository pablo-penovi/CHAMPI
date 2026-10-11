#include "sd_card.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace champi
{
namespace
{
bool SameName(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower((unsigned char)x) == std::tolower((unsigned char)y);
           });
}

// The profile's files: everything at its root that's a regular file.
std::vector<fs::path> FactoryFiles(const fs::path& factory)
{
    if(!fs::is_directory(factory))
        throw std::runtime_error("SD card: card profile " + factory.string() + " not found");
    std::vector<fs::path> files;
    for(const fs::directory_entry& e : fs::directory_iterator(factory))
        if(e.is_regular_file())
            files.push_back(e.path());
    return files;
}

} // namespace

fs::path CardsDir()
{
    fs::path data_home;
    if(const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
        data_home = xdg;
    else if(const char* home = std::getenv("HOME"); home && *home)
        data_home = fs::path(home) / ".local/share";
    else
        throw std::runtime_error("SD card: neither XDG_DATA_HOME nor HOME is set");
    return data_home / "champi" / "cards";
}

fs::path DefaultCardDir()
{
    return CardsDir() / "default";
}

fs::path FactoryCardDir()
{
    // A release ships the profile in card-profiles/ next to the executable; a build from source
    // uses the checkout's.
    const fs::path source = CHAMPI_FACTORY_CARD_DIR;
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if(!ec)
    {
        const fs::path shipped = exe.parent_path() / "card-profiles" / source.filename();
        if(fs::is_directory(shipped, ec))
            return shipped;
    }
    return source;
}

void CreateCard(const fs::path& dir, const fs::path& factory)
{
    const std::vector<fs::path> files = FactoryFiles(factory);
    const bool                  existed = fs::exists(dir);
    if(existed && (!fs::is_directory(dir) || !fs::is_empty(dir)))
        throw std::runtime_error(dir.string() + " already exists; choose another name or select it instead");

    try
    {
        fs::create_directories(dir);
        for(const fs::path& f : files)
            fs::copy_file(f, dir / f.filename());
    }
    catch(...)
    {
        std::error_code ignored;
        if(existed)
            for(const fs::path& f : files)
                fs::remove(dir / f.filename(), ignored);
        else
            fs::remove_all(dir, ignored);
        throw;
    }
}

void RestoreFactoryFiles(const fs::path& dir, const fs::path& factory)
{
    if(!fs::is_directory(dir))
        throw std::runtime_error("SD card: " + dir.string() + " isn't a folder");
    for(const fs::path& f : FactoryFiles(factory))
    {
        const std::string name = f.filename().string();
        for(const fs::directory_entry& e : fs::directory_iterator(dir))
            if(e.path().filename() != name && SameName(e.path().filename().string(), name))
                fs::remove(e.path());
        fs::copy_file(f, dir / name, fs::copy_options::overwrite_existing);
    }
}

fs::path FindCardFile(const fs::path& dir, std::string_view name)
{
    std::error_code ec;
    if(fs::exists(dir / name, ec))
        return dir / name;
    for(fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if(SameName(it->path().filename().string(), name))
            return it->path();
    return {};
}

int MidiInChannelFromOptions(std::string_view json)
{
    // Each option is {"name": "...", "value": ...}: the first value after the name is its own.
    size_t at = json.find("\"Midi In Channel\"");
    if(at == std::string_view::npos || (at = json.find("\"value\"", at)) == std::string_view::npos
       || (at = json.find(':', at)) == std::string_view::npos)
        return 0;
    at++;
    while(at < json.size() && std::isspace((unsigned char)json[at]))
        at++;
    int channel = 0;
    for(int digits = 0; at < json.size() && std::isdigit((unsigned char)json[at]) && digits < 3; at++, digits++)
        channel = channel * 10 + (json[at] - '0');
    return channel >= 1 && channel <= 16 ? channel - 1 : 0;
}

int MidiInChannelOfCard(const fs::path& dir)
{
    const fs::path options = FindCardFile(dir, "options.json");
    if(options.empty())
        return 0; // TAPE writes one with channel 1
    std::ifstream in(options, std::ios::binary);
    const std::string json(std::istreambuf_iterator<char>(in), {});
    return MidiInChannelFromOptions(json);
}

} // namespace champi
