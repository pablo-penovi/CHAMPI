#include "card_settings.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include "sd_card.h"
#include "toml_lines.h"

namespace fs = std::filesystem;

namespace champi
{
CardSettings CardSettings::Parse(std::string_view toml, const std::string& source)
{
    CardSettings settings;
    bool         seen[2] = {};
    const auto   lines   = ParseTomlLines(toml, source, "a folder in quotes",
                                          "tables aren't used in card.toml; write `current = \"folder\"` lines");
    for(const TomlLine& l : lines)
    {
        const int which = l.name == "current" ? 0 : l.name == "previous" ? 1 : -1;
        if(which < 0)
            TomlFail(source, l.line, "card.toml holds current and previous, not " + l.name);
        if(seen[which])
            TomlFail(source, l.line, l.name + " is set twice");
        seen[which] = true;
        if(l.is_array || !l.values[0].is_string)
            TomlFail(source, l.line, l.name + " is a folder, in quotes");
        (which == 0 ? settings.current : settings.previous) = l.values[0].text;
    }
    return settings;
}

CardSettings CardSettings::Load(const fs::path& path)
{
    std::error_code ec;
    if(!fs::exists(path, ec))
        return {};
    std::ifstream in(path);
    if(!in)
        throw std::runtime_error("can't read " + path.string());
    std::stringstream text;
    text << in.rdbuf();
    return Parse(text.str(), path.string());
}

std::string CardSettings::ToToml() const
{
    std::string out = "# The SD card in use, and the one before it. Insert card (F9) writes this file.\n\n";
    if(!current.empty())
        out += "current = " + TomlQuote(current.string()) + "\n";
    if(!previous.empty())
        out += "previous = " + TomlQuote(previous.string()) + "\n";
    return out;
}

void CardSettings::Save(const fs::path& path) const
{
    if(path.has_parent_path())
        fs::create_directories(path.parent_path());
    const fs::path tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << ToToml();
        if(!out.flush())
            throw std::runtime_error("can't write " + tmp.string());
    }
    fs::rename(tmp, path);
}

StartCard ChooseStartCard(const CardSettings& settings, const fs::path& default_dir, const fs::path& factory)
{
    StartCard start;
    start.settings = settings;

    std::vector<fs::path> candidates;
    for(const fs::path& dir : {settings.current, settings.previous, default_dir})
        if(!dir.empty() && std::find(candidates.begin(), candidates.end(), dir) == candidates.end())
            candidates.push_back(dir);

    for(const fs::path& dir : candidates)
    {
        if(dir == default_dir && !fs::exists(dir))
            try
            {
                CreateCard(dir, factory);
            }
            catch(const std::exception& e)
            {
                start.skipped.push_back({dir, {{{}, std::string("can't be created: ") + e.what() + "."}}});
                continue;
            }
        std::vector<CardProblem> problems = CheckCard(dir);
        if(!problems.empty())
        {
            start.skipped.push_back({dir, std::move(problems)});
            continue;
        }
        start.dir = dir;
        break;
    }

    // The card chosen is the current one now. A previous card that's now current, or that failed,
    // isn't worth going back to.
    if(!start.dir.empty() && start.dir != settings.current)
    {
        start.settings.current = start.dir;
        if(start.settings.previous == start.dir)
            start.settings.previous.clear();
    }
    for(const SkippedCard& s : start.skipped)
        if(start.settings.previous == s.dir)
            start.settings.previous.clear();
    return start;
}

} // namespace champi
