// champi: the CHOMPI TAPE firmware as a standalone JACK app.
//
// DPF's standalone has its own main(), compiled here as dpf_jack_main. This one handles CHAMPI's
// options first: the SD-card commands run and exit, as in champi-headless; otherwise the card is
// created if needed and the app starts on it.
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "DistrhoPluginInfo.h"
#include "app.h"
#include "jack_monitor.h"
#include "routing.h"
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

std::filesystem::path ConfigDir()
{
    if(const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return std::filesystem::path(xdg) / "champi";
    if(const char* home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".config/champi";
    return {};
}

namespace
{
void PrintUsage()
{
    std::printf("Usage: champi [options]\n\n"
                "  --no-connect        don't connect anything on start, saved connections\n"
                "                      included (the F8 menu still works)\n"
                "  --connections <file>\n"
                "                      the connections the F8 menu saves and restores\n"
                "                      (default ~/.config/champi/connections.toml)\n"
                "  --skin <dir>        panel art: logo.png, chompi.png, play.png, loop.png\n"
                "                      (default ~/.config/champi/skin; each file is optional)\n"
                "  --keymap <file>     computer keys for the panel, over the defaults\n"
                "                      (default ~/.config/champi/keymap.toml, if it exists)\n"
                "  --print-keymap      print the keymap in use as keymap.toml, and exit\n"
                "  --test-mode         start in TAPE's factory test, as when ENC6 is held at\n"
                "                      power-on\n\n"
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
    const std::filesystem::path config = ConfigDir();
    if(!config.empty())
        Options().skin_dir = config / "skin";
    std::filesystem::path keymap;
    bool                  print_keymap = false;
    std::filesystem::path connections;
    std::optional<SavedRouting> saved;
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
            else if(arg == "--keymap")
            {
                if(++i == args.size())
                    throw std::invalid_argument("--keymap needs a file");
                keymap = args[i];
            }
            else if(arg == "--connections")
            {
                if(++i == args.size())
                    throw std::invalid_argument("--connections needs a file");
                connections = args[i];
            }
            else if(arg == "--print-keymap")
                print_keymap = true;
            else if(arg == "--test-mode")
                Options().test_mode = true;
            else
                rest.push_back(arg);
        }
        args = std::move(rest);

        // A keymap given by name must exist; the default one only if it's there.
        if(keymap.empty() && !config.empty() && std::filesystem::exists(config / "keymap.toml"))
            keymap = config / "keymap.toml";
        if(!keymap.empty())
            Options().keymap.Load(keymap.string());
        if(print_keymap)
        {
            std::fputs(Options().keymap.ToToml().c_str(), stdout);
            return 0;
        }

        // A file the menu hasn't written yet means the defaults, given by name or not.
        if(connections.empty() && !config.empty())
            connections = config / "connections.toml";
        if(!connections.empty())
            saved = SavedRouting::Load(connections);

        if(!config.empty())
        {
            Options().input_levels_path = config / "input_levels.toml";
            if(auto levels = InputLevels::Load(Options().input_levels_path))
                Options().input_levels = *levels;
        }

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
    if(monitor.Open(DISTRHO_PLUGIN_NAME, connect, connections, std::move(saved)))
        Options().routing = &monitor;
    return dpf_jack_main(int(dpf_argv.size() - 1), dpf_argv.data());
}
