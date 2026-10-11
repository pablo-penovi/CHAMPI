// Which card is in: card.toml, and the card champi starts with.
//
// card.toml, in the config folder, holds the card in use and the one before it:
//
//     current = "/home/me/.local/share/champi/cards/live set"
//     previous = "/home/me/.local/share/champi/cards/default"
//
// Inserting a card writes it. At start, champi takes the first of these that exists and passes
// the card check (StartCard): current, previous, then the default card, created if it isn't
// there. --sd-dir comes before all of them, and isn't saved.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "card_check.h"

namespace champi
{
struct CardSettings
{
    std::filesystem::path current;  // the card in use
    std::filesystem::path previous; // the last card that passed the check before it

    /** Parses card.toml's text. Throws std::runtime_error as "source:line: message". */
    static CardSettings Parse(std::string_view toml, const std::string& source = "card.toml");

    /** Reads card.toml; empty settings if the file isn't there. Throws std::runtime_error. */
    static CardSettings Load(const std::filesystem::path& path);

    std::string ToToml() const;

    /** Writes card.toml, creating its folder if needed. Throws std::runtime_error. */
    void Save(const std::filesystem::path& path) const;

    bool operator==(const CardSettings& o) const { return current == o.current && previous == o.previous; }
};

/** A card start-up passed over, and why. */
struct SkippedCard
{
    std::filesystem::path    dir;
    std::vector<CardProblem> problems;
};

struct StartCard
{
    std::filesystem::path    dir;      // the card to start with; empty if none passed
    std::vector<SkippedCard> skipped;  // cards tried first, in order, with their problems
    CardSettings             settings; // card.toml as it should be now
};

/** Chooses the card to start with from card.toml's cards and the default card, creating the
 *  default card from `factory` if it doesn't exist. Each must exist and pass CheckCard. The card
 *  chosen becomes `current` in the settings returned. */
StartCard ChooseStartCard(const CardSettings& settings, const std::filesystem::path& default_dir,
                          const std::filesystem::path& factory);

} // namespace champi
