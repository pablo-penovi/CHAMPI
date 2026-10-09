// champi-headless: runs CHAMPI without a UI. For now it only manages the SD card; running the
// firmware from a script comes with chunk 4.
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#include "sd_cli.h"

namespace
{
void PrintUsage()
{
    std::printf("Usage: champi-headless [options]\n\nSD card:\n%s", champi::kSdUsage);
}

} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    try
    {
        for(const auto& arg : args)
            if(arg == "-h" || arg == "--help")
            {
                PrintUsage();
                return 0;
            }

        const champi::SdOptions sd = champi::ParseSdOptions(args);
        if(!args.empty())
        {
            std::fprintf(stderr, "champi-headless: unknown argument %s\n", args.front().c_str());
            return 2;
        }
        if(!sd.HasCommands())
        {
            std::fprintf(stderr, "champi-headless: running the firmware isn't implemented yet\n");
            return 2;
        }
        champi::RunSdCommands(sd);
        return 0;
    }
    catch(const std::invalid_argument& e)
    {
        std::fprintf(stderr, "champi-headless: %s\n", e.what());
        return 2;
    }
    catch(const std::exception& e)
    {
        std::fprintf(stderr, "champi-headless: %s\n", e.what());
        return 1;
    }
}
