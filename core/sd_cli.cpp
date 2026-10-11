#include "sd_cli.h"

#include <cstdio>
#include <optional>
#include <stdexcept>

#include "sd_card.h"

namespace fs = std::filesystem;

namespace champi
{
const char* const kSdUsage = "  --sd-dir <folder>   the folder to use as the card\n"
                             "  --sd-reset --yes    restore the factory samples, options.json,\n"
                             "                      presets.json and firmware file on the card\n"
                             "                      (--sd-dir, or ~/.local/share/champi/cards/default),\n"
                             "                      leaving other files alone, and exit\n";

SdOptions ParseSdOptions(std::vector<std::string>& args)
{
    SdOptions                options;
    std::vector<std::string> rest;

    for(size_t i = 0; i < args.size(); i++)
    {
        std::string                arg = args[i];
        std::optional<std::string> inline_value;
        if(const auto eq = arg.find('='); arg.rfind("--sd-", 0) == 0 && eq != std::string::npos)
        {
            inline_value = arg.substr(eq + 1);
            arg.resize(eq);
        }
        auto value = [&]() -> std::string {
            if(inline_value)
                return *inline_value;
            if(i + 1 >= args.size())
                throw std::invalid_argument(arg + " needs a value");
            return args[++i];
        };

        if(arg == "--sd-dir")
            options.dir = value();
        else if(arg == "--sd-reset" && !inline_value)
            options.reset = true;
        else if(arg == "--yes")
            options.yes = true;
        else if(arg == "--sd-image")
            throw std::invalid_argument("--sd-image was removed in 1.3: the card is a folder now. Use --sd-dir "
                                        "<folder>");
        else if(arg == "--sd-import" || arg == "--sd-export")
            throw std::invalid_argument(arg + " was removed in 1.3: the card is a folder now, so copy files in and "
                                              "out of it with your file manager. Use --sd-dir <folder> to choose it");
        else
            rest.push_back(args[i]);
    }

    if(options.reset && !options.yes)
        throw std::invalid_argument("--sd-reset overwrites the card's factory files; add --yes to go ahead");
    if(options.yes && !options.reset)
        throw std::invalid_argument("--yes goes with --sd-reset");
    args = std::move(rest);
    return options;
}

void ResetCard(const fs::path& dir)
{
    const fs::path card    = dir.empty() ? DefaultCardDir() : dir;
    const fs::path factory = FactoryCardDir();
    if(!fs::exists(card))
    {
        std::printf("Creating %s from the factory card\n", card.c_str());
        CreateCard(card, factory);
        return;
    }
    std::printf("Restoring the factory files on %s\n", card.c_str());
    RestoreFactoryFiles(card, factory);
}

} // namespace champi
