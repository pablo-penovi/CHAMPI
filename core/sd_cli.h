// The SD-card command-line options shared by champi and champi-headless:
//   --sd-image <file>   use this card image instead of the default
//   --sd-reset          replace the card with a fresh copy of the factory card
//   --sd-import <path>  copy a file, or a directory's contents, to the card root
//   --sd-export <dir>   copy the whole card into a directory
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace champi
{
struct SdOptions
{
    std::filesystem::path                image;
    bool                                 reset = false;
    std::vector<std::filesystem::path>   imports;
    std::optional<std::filesystem::path> export_dir;

    /** True if a reset, import or export was asked for. */
    bool HasCommands() const { return reset || !imports.empty() || export_dir; }
};

/** Help text for the options above, one per line. */
extern const char* const kSdUsage;

/**
 * Takes the SD options out of `args` and returns them; the other arguments are left in `args`.
 * Accepts both "--opt value" and "--opt=value". Throws std::invalid_argument on a missing value.
 */
SdOptions ParseSdOptions(std::vector<std::string>& args);

/**
 * Makes sure the card exists (creating it from the factory card on first use), then runs the
 * commands in a fixed order: reset, imports in the order given, export. Progress goes to stdout.
 */
void RunSdCommands(const SdOptions& options);

} // namespace champi
