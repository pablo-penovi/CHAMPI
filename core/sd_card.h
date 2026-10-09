// The virtual SD card: a FAT32 disk-image file seeded with a CHOMPI factory card profile.
//
// These run on the calling thread through daisycola's SD helpers, so they must not be called while
// the firmware is running. Errors are thrown as daisycola::SdError or std::runtime_error.
#pragma once

#include <cstdint>
#include <filesystem>

namespace champi
{
/** Size of a new card image. The file is sparse, so only the data written takes disk space. */
constexpr uint64_t kSdImageSize = uint64_t(4) << 30;

/** $XDG_DATA_HOME/champi/sdcard.img, or ~/.local/share/champi/sdcard.img. */
std::filesystem::path DefaultSdImagePath();

/** The factory card profile for the firmware this build runs (card-profiles/tape-2.0). */
std::filesystem::path FactoryCardDir();

/**
 * Formats a new image and copies the contents of `card_dir` to its root, like preparing a real
 * card. The image is built next to `image` and renamed into place, so a failure leaves any old
 * image as it was. Creates the parent directory if needed.
 */
void CreateCard(const std::filesystem::path& image,
                const std::filesystem::path& card_dir,
                uint64_t                     size = kSdImageSize);

/** Creates the card from `card_dir` if `image` doesn't exist yet. Returns true if it did. */
bool EnsureCard(const std::filesystem::path& image, const std::filesystem::path& card_dir);

/** Copies a host file, or the contents of a host directory, to the card root, overwriting. */
void ImportToCard(const std::filesystem::path& image, const std::filesystem::path& host_path);

/** Copies everything on the card into `host_dir`, creating it if needed. */
void ExportFromCard(const std::filesystem::path& image, const std::filesystem::path& host_dir);

} // namespace champi
