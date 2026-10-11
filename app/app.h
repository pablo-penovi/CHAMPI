// Settings the champi command line hands to the plugin and the UI.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "audio_levels.h"
#include "card_settings.h"
#include "keyboard.h"
#include "midi_map.h"

namespace champi
{
class RoutingService;

struct AppOptions
{
    std::filesystem::path    card_dir;      // the card TAPE starts with; empty if none passed the check
    std::vector<SkippedCard> skipped_cards; // cards start-up passed over, shown once the window is up
    std::filesystem::path    card_settings_path; // card.toml, which Insert card writes; empty for nowhere
    std::filesystem::path skin_dir; // PNGs that replace the panel's logo and key glyphs
    Keymap                keymap = Keymap::Defaults();
    bool                  test_mode = false; // ENC6 held at power-on: TAPE's factory test
    RoutingService*       routing   = nullptr; // the connections menu's; none without JACK
    AudioLevels           audio_levels;
    std::filesystem::path audio_levels_path; // where the menu saves them; empty for nowhere
    MidiMappings          midi_mappings;
    std::filesystem::path midi_mappings_dir; // where the menu saves them; empty for nowhere
};

/** $XDG_CONFIG_HOME/champi, or ~/.config/champi; empty if neither is set. */
std::filesystem::path ConfigDir();

/** The process's options, filled in by main before DPF starts. */
AppOptions& Options();

/** xruns JACK has reported since the app started (see JackMonitor). */
extern std::atomic<uint64_t> g_xruns;

} // namespace champi
