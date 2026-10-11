// champi: the CHOMPI TAPE firmware as a standalone JACK app.
//
// DPF's standalone has its own main(), compiled here as dpf_jack_main. This one handles CHAMPI's
// options first: --sd-reset runs and exits, as in champi-headless; otherwise it picks the card to
// start with (--sd-dir, or card.toml's, or the default card, created if needed) and the app
// starts on it.
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "DistrhoPluginInfo.h"
#include "app.h"
#include "card_check.h"
#include "card_settings.h"
#include "jack_monitor.h"
#include "routing.h"
#include "sd_card.h"
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
                "SD card (default: the last card inserted, or ~/.local/share/champi/cards/default):\n%s",
                kSdUsage);
}

// The card to start with, and card.toml as it should be. --sd-dir must pass the check; card.toml's
// cards and the default card are tried in turn, and the window says why any was passed over.
void ChooseCard(const SdOptions& sd, const std::filesystem::path& config)
{
    if(!sd.dir.empty())
    {
        const std::vector<CardProblem> problems = CheckCard(sd.dir);
        if(!problems.empty())
        {
            std::string message = sd.dir.string() + " can't be a card:";
            for(const CardProblem& p : problems)
                message += "\n  " + ToString(p);
            throw std::runtime_error(message);
        }
        Options().card_dir = sd.dir;
        return;
    }

    CardSettings settings;
    if(!config.empty())
    {
        Options().card_settings_path = config / "card.toml";
        try
        {
            settings = CardSettings::Load(Options().card_settings_path);
        }
        catch(const std::exception& e)
        {
            std::fprintf(stderr, "champi: %s; starting with the default card\n", e.what());
        }
    }
    const StartCard start   = ChooseStartCard(settings, DefaultCardDir(), FactoryCardDir());
    Options().card_dir      = start.dir;
    Options().skipped_cards = start.skipped;
    for(const SkippedCard& s : start.skipped)
        std::fprintf(stderr, "champi: passed over the card %s: %s\n", s.dir.c_str(),
                     ToString(s.problems.front()).c_str());
    if(!(start.settings == settings) && !Options().card_settings_path.empty())
        try
        {
            start.settings.Save(Options().card_settings_path);
        }
        catch(const std::exception& e)
        {
            std::fprintf(stderr, "champi: can't save the card in use: %s\n", e.what());
        }
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
            // input_levels.toml held the input volumes before the outputs had any.
            Options().audio_levels_path = config / "audio_levels.toml";
            auto levels                 = AudioLevels::Load(Options().audio_levels_path);
            if(!levels)
                levels = AudioLevels::Load(config / "input_levels.toml");
            if(levels)
                Options().audio_levels = *levels;

            Options().midi_mappings_dir = MidiMappingsDir(config);
            Options().midi_mappings     = MidiMappings::Load(Options().midi_mappings_dir);
        }

        const SdOptions sd = ParseSdOptions(args);
        if(sd.reset)
        {
            ResetCard(sd.dir);
            return 0;
        }
        ChooseCard(sd, config);
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

    // TAPE reads its keys once per audio block and debounces them by the millisecond, but a host
    // period's blocks run back to back. At PipeWire's usual 1024 frames a key has to be held for
    // 100 ms or more before TAPE sees it. Ask for a short period, unless the user picked one; a
    // JACK server keeps its own.
    setenv("PIPEWIRE_LATENCY", "128/48000", 0);

    JackMonitor monitor;
    if(monitor.Open(DISTRHO_PLUGIN_NAME, connect, connections, std::move(saved)))
        Options().routing = &monitor;
    return dpf_jack_main(int(dpf_argv.size() - 1), dpf_argv.data());
}
