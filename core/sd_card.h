// The virtual SD card: a folder on the host, seeded with a CHOMPI factory card profile.
//
// The firmware reads and writes the folder's files while it runs (daisycola::SdInsert), so these
// must not change a card the firmware has in. Errors are thrown as std::runtime_error or
// std::filesystem::filesystem_error.
#pragma once

#include <filesystem>
#include <string_view>

namespace champi
{
/** $XDG_DATA_HOME/champi/cards, or ~/.local/share/champi/cards: where the Insert card dialogs
 *  start. Cards can live anywhere. */
std::filesystem::path CardsDir();

/** CardsDir() / "default": the card CHAMPI creates on first start. */
std::filesystem::path DefaultCardDir();

/** The factory card profile for the firmware this build runs: card-profiles/tape-2.0 next to the
 *  executable if it's there, as in a release, else the one in the CHOMPI checkout. */
std::filesystem::path FactoryCardDir();

/** Creates `dir`, which must not exist or must be empty, and copies in the factory profile's
 *  files, like preparing a real card. Creates the parent folders if needed. On a failure, a
 *  folder it created is removed again. */
void CreateCard(const std::filesystem::path& dir, const std::filesystem::path& factory);

/** Re-copies the factory profile's files into `dir`, overwriting those names only: the factory
 *  samples, options.json, presets.json and the firmware .bin. Files the user added stay. A file
 *  whose name differs from a factory one only by case is replaced, as it's the same file on FAT. */
void RestoreFactoryFiles(const std::filesystem::path& dir, const std::filesystem::path& factory);

/** The file in `dir` named `name` without regard to case, as TAPE opens it on FAT: `name` itself
 *  if that exists, else the first match. Empty if there's none. */
std::filesystem::path FindCardFile(const std::filesystem::path& dir, std::string_view name);

/** The MIDI in channel, 0-15, that TAPE's options.json sets ("Midi In Channel", from 1); 0 if it
 *  doesn't set one, as TAPE does. */
int MidiInChannelFromOptions(std::string_view json);

/** MidiInChannelFromOptions on the card's options.json; 0 without one. */
int MidiInChannelOfCard(const std::filesystem::path& dir);

} // namespace champi
