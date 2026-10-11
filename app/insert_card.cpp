#include "insert_card.h"

#include <exception>
#include <system_error>

#include "sd_card.h"

namespace fs = std::filesystem;

namespace champi
{
bool SameFolder(const fs::path& a, const fs::path& b)
{
    if(a.empty() || b.empty())
        return false;
    std::error_code ec;
    const bool      same = fs::equivalent(a, b, ec);
    if(!ec)
        return same;
    return fs::weakly_canonical(a, ec).lexically_normal() == fs::weakly_canonical(b, ec).lexically_normal();
}

InsertCard::InsertCard(Picker& picker, Cards& cards, fs::path cards_dir, fs::path factory)
    : picker_(picker), cards_(cards), cards_dir_(std::move(cards_dir)), factory_(std::move(factory))
{
}

void InsertCard::Open()
{
    stage_    = Stage::kChoose;
    selected_ = Choice::kSelect;
}

void InsertCard::Close()
{
    if(stage_ == Stage::kPick)
        picker_.Close();
    stage_ = Stage::kClosed;
}

void InsertCard::Move(int delta)
{
    const int i = (int(selected_) + delta % kNumChoices + kNumChoices) % kNumChoices;
    selected_   = Choice(i);
}

void InsertCard::Choose(Choice choice)
{
    selected_ = choice;
    mode_     = choice == Choice::kCreate ? FolderBrowser::Mode::kCreate : FolderBrowser::Mode::kSelect;
    stage_    = Stage::kPick;
    picker_.Open(mode_, cards_dir_, choice == Choice::kCreate ? kNewCardName : "");
}

std::string InsertCard::Picked(const fs::path& dir)
{
    if(stage_ != Stage::kPick)
        return "";
    if(mode_ == FolderBrowser::Mode::kCreate)
    {
        std::error_code       ec;
        const fs::file_status status = fs::status(dir, ec);
        if(fs::exists(status) && !fs::is_directory(status))
            return dir.filename().string() + " is a file; choose another name.";
        if(fs::is_directory(status) && !fs::is_empty(dir, ec))
            return dir.filename().string() + " already exists; choose another name or select it instead.";
        try
        {
            CreateCard(dir, factory_);
        }
        catch(const std::exception& e)
        {
            return std::string("Can't create the card: ") + e.what();
        }
    }
    picker_.Close();
    Check(dir);
    return "";
}

void InsertCard::Check(const fs::path& dir)
{
    if(SameFolder(dir, cards_.Current()))
    {
        ShowReport({"Nothing to do", dir.string() + " is the card that's in already.", {}});
        return;
    }
    std::vector<CardProblem> problems = CheckCard(dir);
    if(!problems.empty())
    {
        ShowReport({"This folder can't be a card",
                    "TAPE wouldn't read it as it is. Nothing has changed: the card that was in is still in.",
                    {{dir, std::move(problems)}}});
        return;
    }
    chosen_ = dir;
    stage_  = Stage::kConfirm;
}

void InsertCard::Confirm()
{
    if(stage_ == Stage::kConfirm)
        stage_ = Stage::kInserting;
}

void InsertCard::Insert()
{
    if(stage_ != Stage::kInserting)
        return;
    const fs::path before   = cards_.Current();
    const fs::path previous = cards_.Previous();
    auto           remember = [&](const fs::path& current, const fs::path& prev) {
        try
        {
            cards_.Remember(current, prev);
        }
        catch(const std::exception&)
        {
            // Not remembering the card loses nothing now; the next start takes the old one.
        }
    };

    remember(chosen_, before.empty() ? previous : before);
    try
    {
        std::vector<CardProblem> problems = cards_.PowerCycle(chosen_, before);
        if(problems.empty())
        {
            stage_ = Stage::kClosed;
            return;
        }
        remember(before, previous);
        ShowReport({"The card changed",
                    chosen_.string() + " changed after it was checked, and no longer passes. "
                        + (before.empty() ? std::string("No card is in.") : "The card that was in went back in."),
                    {{chosen_, std::move(problems)}}});
    }
    catch(const std::exception& e)
    {
        remember(before, previous);
        failure_ = e.what();
        stage_   = Stage::kFailed;
    }
}

void InsertCard::ShowStartup(const std::vector<SkippedCard>& skipped, const fs::path& chosen)
{
    if(skipped.empty())
        return;
    if(chosen.empty())
        ShowReport({"No card",
                    "No card passed the check, so TAPE isn't running. Fix one, or insert another with F9 or a "
                    "click on the SD slot.",
                    skipped});
    else
        ShowReport({"Started with another card",
                    "CHAMPI started with " + chosen.string() + ", because the cards before it didn't pass the check.",
                    skipped});
}

void InsertCard::ShowReport(Report report)
{
    report_ = std::move(report);
    stage_  = Stage::kReport;
}

} // namespace champi
