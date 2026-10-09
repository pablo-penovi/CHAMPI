// Settings the champi command line hands to the plugin and the UI.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>

#include "keyboard.h"

namespace champi
{
struct AppOptions
{
    std::filesystem::path sd_image;
    std::filesystem::path skin_dir; // PNGs that replace the panel's logo and key glyphs
    Keymap                keymap = Keymap::Defaults();
};

/** $XDG_CONFIG_HOME/champi, or ~/.config/champi; empty if neither is set. */
std::filesystem::path ConfigDir();

/** The process's options, filled in by main before DPF starts. */
AppOptions& Options();

/** xruns JACK has reported since the app started (see JackMonitor). */
extern std::atomic<uint64_t> g_xruns;

} // namespace champi
