// champi: the CHOMPI TAPE firmware as a standalone JACK app.
//
// DPF's standalone has its own main(), compiled here as dpf_jack_main. This one handles CHAMPI's
// options first: the SD-card commands run and exit, as in champi-headless; otherwise the card is
// created if needed and the app starts on it.
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

#include "DistrhoPluginInfo.h"
#include "app.h"
#include "jack_monitor.h"
#include "sd_cli.h"

int dpf_jack_main(int argc, char* argv[]);

namespace champi
{
AppOptions& Options()
{
    static AppOptions options;
    return options;
}

std::atomic<uint64_t> g_xruns{0};

std::filesystem::path DefaultSkinDir()
{
    if(const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return std::filesystem::path(xdg) / "champi/skin";
    if(const char* home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".config/champi/skin";
    return {};
}

namespace
{
void PrintUsage()
{
    std::printf("Usage: champi [options]\n\n"
                "  --no-connect        don't connect master out and MIDI controllers\n"
                "  --skin <dir>        panel art: logo.png, chompi.png, play.png, loop.png\n"
                "                      (default ~/.config/champi/skin; each file is optional)\n\n"
                "SD card (--sd-reset, --sd-import and --sd-export run and exit):\n%s",
                kSdUsage);
}
} // namespace
} // namespace champi

int main(int argc, char** argv)
{
    using namespace champi;
    std::vector<std::string> args(argv + 1, argv + argc);
    bool                     connect = true;
    Options().skin_dir               = DefaultSkinDir();
    try
    {
        std::vector<std::string> rest;
        for(size_t i = 0; i < args.size(); i++)
        {
            const std::string& arg = args[i];
            if(arg == "-h" || arg == "--help")
            {
                PrintUsage();
                return 0;
            }
            if(arg == "--no-connect")
                connect = false;
            else if(arg == "--skin")
            {
                if(++i == args.size())
                    throw std::invalid_argument("--skin needs a directory");
                Options().skin_dir = args[i];
            }
            else
                rest.push_back(arg);
        }
        args = std::move(rest);

        const SdOptions sd = ParseSdOptions(args);
        RunSdCommands(sd);
        if(sd.HasCommands())
            return 0;
        Options().sd_image = sd.image;
    }
    catch(const std::invalid_argument& e)
    {
        std::fprintf(stderr, "champi: %s\n", e.what());
        return 2;
    }
    catch(const std::exception& e)
    {
        std::fprintf(stderr, "champi: %s\n", e.what());
        return 1;
    }

    // What's left goes to DPF (it knows "embed <window id>" and "selftest").
    std::vector<char*> dpf_argv{argv[0]};
    for(auto& arg : args)
        dpf_argv.push_back(arg.data());
    dpf_argv.push_back(nullptr);

    JackMonitor monitor;
    monitor.Open(DISTRHO_PLUGIN_NAME, connect);
    return dpf_jack_main(int(dpf_argv.size() - 1), dpf_argv.data());
}
