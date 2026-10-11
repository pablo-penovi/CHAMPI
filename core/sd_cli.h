// The SD-card command-line options shared by champi and champi-headless:
//   --sd-dir <folder>   use this folder as the card
//   --sd-reset --yes    restore the factory files on the card (that folder, or the default card)
//
// The options of the disk-image card that came before (--sd-image, --sd-import, --sd-export) are
// refused with a message naming what replaced them.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace champi
{
struct SdOptions
{
    std::filesystem::path dir; // empty if not given
    bool                  reset = false;
    bool                  yes   = false; // --yes: the reset is meant
};

/** Help text for the options above, one per line. */
extern const char* const kSdUsage;

/**
 * Takes the SD options out of `args` and returns them; the other arguments are left in `args`.
 * Accepts both "--opt value" and "--opt=value". Throws std::invalid_argument on a missing value,
 * on --sd-reset without --yes, and on the removed options.
 */
SdOptions ParseSdOptions(std::vector<std::string>& args);

/** Restores the factory files on `dir`, or on the default card if it's empty, as --sd-reset does.
 *  Progress goes to stdout. */
void ResetCard(const std::filesystem::path& dir);

} // namespace champi
