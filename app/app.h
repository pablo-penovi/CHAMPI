// Settings the champi command line hands to the plugin and the UI.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>

namespace champi
{
struct AppOptions
{
    std::filesystem::path sd_image;
};

/** The process's options, filled in by main before DPF starts. */
AppOptions& Options();

/** xruns JACK has reported since the app started (see JackMonitor). */
extern std::atomic<uint64_t> g_xruns;

} // namespace champi
