// Insert card: the only thing to do with the SD card. A click on the SD slot or F9 opens it.
//
//   1. Choose: select an existing folder, or create a new one filled from the factory card. Both
//      open the folder picker in the cards folder.
//   2. Check: CheckCard on the folder. Problems are listed and nothing changes: the card that's in
//      stays in and TAPE carries on. The card that's in already needs nothing doing.
//   3. Confirm: TAPE restarts to read the new card, as the CHOMPI does after a power cycle.
//   4. Insert: card.toml is updated and TAPE is power-cycled with the new card.
//
// There's no pulling the card out: the card in stays in until another passes the check.
//
// This is the flow's logic only. The window draws each stage and feeds it the keys and clicks;
// the folder picker and the power cycle sit behind the interfaces below, so the tests run it
// without either.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "card_check.h"
#include "card_settings.h"
#include "folder_browser.h"

namespace champi
{
class InsertCard
{
  public:
    enum class Stage
    {
        kClosed,
        kChoose,    // select or create
        kPick,      // the folder picker is open
        kReport,    // problems, or a message, until closed
        kConfirm,   // a card that passed, waiting for the go-ahead
        kInserting, // the window shows "Inserting…", then calls Insert
        kFailed,    // the power cycle failed: offer to quit
    };

    enum class Choice
    {
        kSelect,
        kCreate,
    };
    static constexpr int kNumChoices = 2;

    /** The folder picker. Picked or PickCancelled answers it. */
    class Picker
    {
      public:
        virtual ~Picker() = default;
        /** Opens in `mode` at `start`; in create mode the name field starts as `name`. */
        virtual void Open(FolderBrowser::Mode mode, const std::filesystem::path& start, const std::string& name) = 0;
        virtual void Close() = 0;
    };

    /** The card that's in, card.toml, and the power cycle. */
    class Cards
    {
      public:
        virtual ~Cards() = default;
        /** The card in; empty if there's none. */
        virtual std::filesystem::path Current() const = 0;
        /** card.toml's previous card. */
        virtual std::filesystem::path Previous() const = 0;
        /** Saves card.toml. */
        virtual void Remember(const std::filesystem::path& current, const std::filesystem::path& previous) = 0;
        /** Power-cycles TAPE with `card`, as Runtime::PowerCycle: problems if `card` failed its
         *  last check and `fallback` went in. Throws if the power cycle failed. */
        virtual std::vector<CardProblem> PowerCycle(const std::filesystem::path& card,
                                                    const std::filesystem::path& fallback) = 0;
    };

    /** A dialog's text: a title, a message, and the cards with problems. */
    struct Report
    {
        std::string              title;
        std::string              text;
        std::vector<SkippedCard> cards;
    };

    /** The name a new card's field starts with. */
    static constexpr const char* kNewCardName = "new card";

    InsertCard(Picker& picker, Cards& cards, std::filesystem::path cards_dir, std::filesystem::path factory);

    /** Opens the choice. */
    void Open();
    /** Closes whatever is open, the picker included. The card in stays in. */
    void Close();

    Stage GetStage() const { return stage_; }
    bool  IsOpen() const { return stage_ != Stage::kClosed; }

    // ---- Choose ----

    Choice Selected() const { return selected_; }
    void   Select(Choice choice) { selected_ = choice; }
    void   Move(int delta);
    /** Opens the folder picker for a choice. */
    void Choose(Choice choice);

    // ---- Pick ----

    /** The picker chose `dir`: a folder to select, or in create mode the new folder to make.
     *  Returns an error for the picker to show, or empty if the flow moved on. */
    std::string Picked(const std::filesystem::path& dir);
    /** The picker was cancelled: everything closes. */
    void PickCancelled() { Close(); }

    // ---- Report, confirm, insert ----

    const Report& GetReport() const { return report_; }

    /** The card waiting to go in. */
    const std::filesystem::path& Chosen() const { return chosen_; }
    /** Goes ahead with the card chosen. */
    void Confirm();

    /** Inserts the card: card.toml, then the power cycle. Ends closed, with a report if the card
     *  changed since it was checked, or failed. */
    void Insert();
    /** Why the power cycle failed. */
    const std::string& Failure() const { return failure_; }

    /** Shows why start-up passed over cards, if it did. `chosen` is the card it started with,
     *  empty if none. */
    void ShowStartup(const std::vector<SkippedCard>& skipped, const std::filesystem::path& chosen);

  private:
    // Checks a folder the picker gave and moves on to the report or the confirmation.
    void Check(const std::filesystem::path& dir);
    void ShowReport(Report report);

    Picker&               picker_;
    Cards&                cards_;
    std::filesystem::path cards_dir_;
    std::filesystem::path factory_;
    Stage                 stage_    = Stage::kClosed;
    Choice                selected_ = Choice::kSelect;
    FolderBrowser::Mode   mode_     = FolderBrowser::Mode::kSelect;
    std::filesystem::path chosen_;
    Report                report_;
    std::string           failure_;
};

/** True if two paths name the same folder, following links. */
bool SameFolder(const std::filesystem::path& a, const std::filesystem::path& b);

} // namespace champi
