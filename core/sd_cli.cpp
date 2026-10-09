#include "sd_cli.h"

#include <cstdio>
#include <stdexcept>

#include "sd_card.h"

namespace fs = std::filesystem;

namespace champi
{
const char* const kSdUsage = "  --sd-image <file>   card image (default: ~/.local/share/champi/sdcard.img)\n"
                             "  --sd-reset          replace the card with the factory card\n"
                             "  --sd-import <path>  copy a file, or a directory's contents, to the card\n"
                             "  --sd-export <dir>   copy the whole card into a directory\n";

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

        if(arg == "--sd-image")
            options.image = value();
        else if(arg == "--sd-import")
            options.imports.push_back(value());
        else if(arg == "--sd-export")
            options.export_dir = value();
        else if(arg == "--sd-reset" && !inline_value)
            options.reset = true;
        else
            rest.push_back(args[i]);
    }

    if(options.image.empty())
        options.image = DefaultSdImagePath();
    args = std::move(rest);
    return options;
}

void RunSdCommands(const SdOptions& options)
{
    const fs::path factory = FactoryCardDir();
    if(options.reset)
    {
        std::printf("Resetting %s to the factory card\n", options.image.c_str());
        CreateCard(options.image, factory);
    }
    else if(EnsureCard(options.image, factory))
        std::printf("Created %s from the factory card\n", options.image.c_str());

    for(const auto& path : options.imports)
    {
        std::printf("Importing %s\n", path.c_str());
        ImportToCard(options.image, path);
    }
    if(options.export_dir)
    {
        std::printf("Exporting to %s\n", options.export_dir->c_str());
        ExportFromCard(options.image, *options.export_dir);
    }
}

} // namespace champi
