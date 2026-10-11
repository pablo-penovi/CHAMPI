// What TAPE expects on its card, checked before a card goes in.
//
// TAPE never checks what it reads: a sample in the wrong format plays as noise, a misnamed one is
// never found, and a JSON file it can't read is silently replaced with its defaults. CheckCard
// finds those before TAPE does, so CHAMPI can refuse the card and say how to fix it.
//
// Rules, from chompi-tape/code/src and the bootloader (see card_check.cpp for where each comes
// from). Every entry at the card root must be one of:
//
//   - jammi_<bank><slot>.wav or cubbi_<bank><slot>.wav, bank a-e and slot 1-14 with no leading
//     zero: a 48 kHz, 16-bit, stereo PCM WAV;
//   - the same with _double before .wav, next to the file it doubles;
//   - options.json and presets.json, in the shape TAPE writes them and within its read buffers;
//   - files TAPE creates itself (presets_temp.json, temp_rec.wav, test_file.txt, .batt_log.txt);
//   - at most one .bin, a firmware update for the bootloader, which CHAMPI ignores;
//   - Mac and Windows metadata, which is ignored.
//
// Names are compared without regard to case, as on FAT, and two names that differ only by case
// are a problem. Symlinks are followed.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace champi
{
struct CardProblem
{
    std::filesystem::path path;   // relative to the card folder; empty for the folder itself
    std::string           reason; // one sentence: what's wrong and what TAPE expects instead

    bool operator==(const CardProblem& o) const { return path == o.path && reason == o.reason; }
};

/** Checks `dir` against what TAPE expects on its card. Empty means the card is good. Reports
 *  every problem, not just the first, in name order. Reads only WAV headers and the two JSON
 *  files. */
std::vector<CardProblem> CheckCard(const std::filesystem::path& dir);

/** "path: reason", or the reason alone for the folder itself. */
std::string ToString(const CardProblem& problem);

} // namespace champi
