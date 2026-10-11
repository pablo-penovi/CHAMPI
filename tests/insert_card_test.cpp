// Insert card: card.toml, the card champi starts with, the folder picker's keys and clicks, and the
// flow from choosing a folder to the power cycle, with the picker and the power cycle faked.
#include <linux/input-event-codes.h>

#include <gtest/gtest.h>

#include "card_settings.h"
#include "folder_picker.h"
#include "insert_card.h"
#include "sd_card.h"
#include "test_util.h"

namespace fs = std::filesystem;
using namespace champi;

namespace
{
using Stage  = InsertCard::Stage;
using Choice = InsertCard::Choice;
using Mode   = FolderBrowser::Mode;

// A small card that passes the check.
void MakeCard(const fs::path& dir)
{
    WriteHostFile(dir / "jammi_a1.wav", MakeWav());
}

// A card that doesn't.
void MakeBadCard(const fs::path& dir)
{
    WriteHostFile(dir / "kick.wav", MakeWav());
}

class FakePicker : public InsertCard::Picker
{
  public:
    void Open(Mode m, const fs::path& s, const std::string& n) override
    {
        open  = true;
        mode  = m;
        start = s;
        name  = n;
    }
    void Close() override { open = false; }

    bool        open = false;
    Mode        mode = Mode::kSelect;
    fs::path    start;
    std::string name;
};

class FakeCards : public InsertCard::Cards
{
  public:
    fs::path Current() const override { return current; }
    fs::path Previous() const override { return saved.previous; }
    void     Remember(const fs::path& c, const fs::path& p) override
    {
        saved = {c, p};
        saves++;
    }
    std::vector<CardProblem> PowerCycle(const fs::path& card, const fs::path& fallback) override
    {
        cycles++;
        cycled_with = card;
        cycled_back = fallback;
        if(fail)
            throw std::runtime_error("the firmware library couldn't be unloaded");
        std::vector<CardProblem> problems = CheckCard(card);
        current                           = problems.empty() ? card : fallback;
        return problems;
    }

    fs::path     current;
    CardSettings saved;
    int          saves  = 0;
    int          cycles = 0;
    fs::path     cycled_with, cycled_back;
    bool         fail = false;
};

struct Flow : ::testing::Test
{
    Flow()
    {
        MakeCard(dir / "cards/default");
        cards.current = dir / "cards/default";
        cards.saved   = {dir / "cards/default", dir / "cards/older"};
    }

    TempDir    dir;
    FakePicker picker;
    FakeCards  cards;
    InsertCard flow{picker, cards, dir / "cards", FactoryCardDir()};
};

} // namespace

// ---- card.toml ---------------------------------------------------------------------------------

TEST(CardSettings, RoundTrips)
{
    TempDir      dir;
    CardSettings s{"/cards/live \"set\"", "/cards/default"};
    s.Save(dir / "config/card.toml");
    EXPECT_EQ(CardSettings::Load(dir / "config/card.toml"), s);
    EXPECT_EQ(CardSettings::Load(dir / "missing.toml"), CardSettings{});
}

TEST(CardSettings, RefusesWhatItDoesntKnow)
{
    EXPECT_THROW(CardSettings::Parse("current = 3"), std::runtime_error);
    EXPECT_THROW(CardSettings::Parse("card = \"/a\""), std::runtime_error);
    EXPECT_THROW(CardSettings::Parse("current = \"/a\"\ncurrent = \"/b\""), std::runtime_error);
    EXPECT_EQ(CardSettings::Parse("# nothing yet\n"), CardSettings{});
}

// ---- The card champi starts with ---------------------------------------------------------------

TEST(StartCard, TakesTheCurrentCard)
{
    TempDir dir;
    MakeCard(dir / "live");
    const StartCard start = ChooseStartCard({dir / "live", dir / "old"}, dir / "default", FactoryCardDir());
    EXPECT_EQ(start.dir, dir / "live");
    EXPECT_TRUE(start.skipped.empty());
    EXPECT_EQ(start.settings, (CardSettings{dir / "live", dir / "old"})) << "nothing to save";
    EXPECT_FALSE(fs::exists(dir / "default")) << "the default card isn't made when it isn't needed";
}

TEST(StartCard, FallsBackToThePreviousCardThenTheDefault)
{
    TempDir dir;
    MakeBadCard(dir / "live");
    MakeCard(dir / "old");
    StartCard start = ChooseStartCard({dir / "live", dir / "old"}, dir / "default", FactoryCardDir());
    EXPECT_EQ(start.dir, dir / "old");
    ASSERT_EQ(start.skipped.size(), 1u);
    EXPECT_EQ(start.skipped[0].dir, dir / "live");
    EXPECT_EQ(start.skipped[0].problems[0].path, fs::path("kick.wav"));
    EXPECT_EQ(start.settings, (CardSettings{dir / "old", {}})) << "the card fallen back to is current";

    fs::remove_all(dir / "old");
    start = ChooseStartCard({dir / "live", dir / "old"}, dir / "default", FactoryCardDir());
    EXPECT_EQ(start.dir, dir / "default");
    ASSERT_EQ(start.skipped.size(), 2u);
    EXPECT_EQ(start.skipped[1].problems[0].reason, "the folder doesn't exist.");
    EXPECT_TRUE(fs::exists(dir / "default/presets.json")) << "a missing default card is created";
    EXPECT_EQ(start.settings, (CardSettings{dir / "default", {}}));
}

TEST(StartCard, FirstStartCreatesTheDefaultCard)
{
    TempDir         dir;
    const StartCard start = ChooseStartCard({}, dir / "cards/default", FactoryCardDir());
    EXPECT_EQ(start.dir, dir / "cards/default");
    EXPECT_TRUE(start.skipped.empty());
    EXPECT_EQ(start.settings.current, dir / "cards/default");
}

TEST(StartCard, NoCardPasses)
{
    TempDir dir;
    MakeBadCard(dir / "default");
    const StartCard start = ChooseStartCard({}, dir / "default", FactoryCardDir());
    EXPECT_TRUE(start.dir.empty());
    ASSERT_EQ(start.skipped.size(), 1u);
    EXPECT_EQ(start.skipped[0].dir, dir / "default");
}

// ---- The flow ----------------------------------------------------------------------------------

TEST_F(Flow, SelectsAValidFolder)
{
    MakeCard(dir / "cards/live");
    flow.Open();
    EXPECT_EQ(flow.GetStage(), Stage::kChoose);
    flow.Choose(Choice::kSelect);
    EXPECT_EQ(flow.GetStage(), Stage::kPick);
    EXPECT_TRUE(picker.open);
    EXPECT_EQ(picker.mode, Mode::kSelect);
    EXPECT_EQ(picker.start, dir / "cards");

    EXPECT_EQ(flow.Picked(dir / "cards/live"), "");
    EXPECT_FALSE(picker.open);
    EXPECT_EQ(flow.GetStage(), Stage::kConfirm);
    EXPECT_EQ(flow.Chosen(), dir / "cards/live");
    EXPECT_EQ(cards.cycles, 0) << "nothing happens before the go-ahead";

    flow.Confirm();
    EXPECT_EQ(flow.GetStage(), Stage::kInserting);
    flow.Insert();
    EXPECT_EQ(flow.GetStage(), Stage::kClosed);
    EXPECT_EQ(cards.cycles, 1);
    EXPECT_EQ(cards.cycled_with, dir / "cards/live");
    EXPECT_EQ(cards.cycled_back, dir / "cards/default");
    EXPECT_EQ(cards.saved, (CardSettings{dir / "cards/live", dir / "cards/default"}));
}

TEST_F(Flow, AnInvalidFolderChangesNothing)
{
    MakeBadCard(dir / "cards/bad");
    flow.Open();
    flow.Choose(Choice::kSelect);
    EXPECT_EQ(flow.Picked(dir / "cards/bad"), "");
    EXPECT_EQ(flow.GetStage(), Stage::kReport);
    const InsertCard::Report& report = flow.GetReport();
    EXPECT_EQ(report.title, "This folder can't be a card");
    ASSERT_EQ(report.cards.size(), 1u);
    EXPECT_EQ(report.cards[0].dir, dir / "cards/bad");
    EXPECT_EQ(report.cards[0].problems[0].path, fs::path("kick.wav"));

    flow.Close();
    EXPECT_EQ(cards.cycles, 0);
    EXPECT_EQ(cards.saves, 0);
    EXPECT_EQ(cards.current, dir / "cards/default");
}

TEST_F(Flow, CreatesANewFolder)
{
    flow.Open();
    flow.Move(1);
    EXPECT_EQ(flow.Selected(), Choice::kCreate);
    flow.Choose(flow.Selected());
    EXPECT_EQ(picker.mode, Mode::kCreate);
    EXPECT_EQ(picker.name, "new card");

    EXPECT_EQ(flow.Picked(dir / "cards/new card"), "");
    EXPECT_TRUE(fs::exists(dir / "cards/new card/presets.json")) << "seeded from the factory card";
    EXPECT_EQ(flow.GetStage(), Stage::kConfirm);
    flow.Confirm();
    flow.Insert();
    EXPECT_EQ(cards.current, dir / "cards/new card");
}

TEST_F(Flow, CreateRefusesAFolderThatExists)
{
    WriteHostFile(dir / "cards/taken/notes.txt", "mine");
    WriteHostFile(dir / "cards/a file", "x");
    flow.Open();
    flow.Choose(Choice::kCreate);
    EXPECT_EQ(flow.Picked(dir / "cards/taken"), "taken already exists; choose another name or select it instead.");
    EXPECT_EQ(flow.Picked(dir / "cards/a file"), "a file is a file; choose another name.");
    EXPECT_EQ(flow.GetStage(), Stage::kPick) << "the picker stays open for another name";
    EXPECT_TRUE(picker.open);
    EXPECT_EQ(ReadHostFile(dir / "cards/taken/notes.txt"), "mine");

    // An empty folder is fine to fill.
    fs::create_directories(dir / "cards/empty");
    EXPECT_EQ(flow.Picked(dir / "cards/empty"), "");
    EXPECT_EQ(flow.GetStage(), Stage::kConfirm);
}

TEST_F(Flow, SelectingTheCurrentCardDoesNothing)
{
    flow.Open();
    flow.Choose(Choice::kSelect);
    EXPECT_EQ(flow.Picked(dir / "cards/./default/"), "");
    EXPECT_EQ(flow.GetStage(), Stage::kReport);
    EXPECT_EQ(flow.GetReport().title, "Nothing to do");
    EXPECT_EQ(cards.cycles, 0);
}

TEST_F(Flow, CancellingLeavesTheCardIn)
{
    flow.Open();
    flow.Choose(Choice::kSelect);
    flow.PickCancelled();
    EXPECT_EQ(flow.GetStage(), Stage::kClosed);
    EXPECT_FALSE(picker.open);

    MakeCard(dir / "cards/live");
    flow.Open();
    flow.Choose(Choice::kSelect);
    flow.Picked(dir / "cards/live");
    flow.Close(); // Escape at the confirmation
    EXPECT_EQ(cards.cycles, 0);
}

TEST_F(Flow, ACardThatChangedSinceTheCheckPutsTheOldOneBack)
{
    MakeCard(dir / "cards/live");
    flow.Open();
    flow.Choose(Choice::kSelect);
    flow.Picked(dir / "cards/live");
    flow.Confirm();
    WriteHostFile(dir / "cards/live/notes.txt", "added meanwhile");
    flow.Insert();
    EXPECT_EQ(flow.GetStage(), Stage::kReport);
    EXPECT_EQ(flow.GetReport().title, "The card changed");
    EXPECT_EQ(cards.current, dir / "cards/default") << "the card that was in went back in";
    EXPECT_EQ(cards.saved, (CardSettings{dir / "cards/default", dir / "cards/older"})) << "card.toml as it was";
}

TEST_F(Flow, AFailedPowerCycleOffersToQuit)
{
    MakeCard(dir / "cards/live");
    cards.fail = true;
    flow.Open();
    flow.Choose(Choice::kSelect);
    flow.Picked(dir / "cards/live");
    flow.Confirm();
    flow.Insert();
    EXPECT_EQ(flow.GetStage(), Stage::kFailed);
    EXPECT_EQ(flow.Failure(), "the firmware library couldn't be unloaded");
    EXPECT_EQ(cards.saved.current, dir / "cards/default");
}

TEST_F(Flow, StartUpSaysWhyItPassedCardsOver)
{
    flow.ShowStartup({}, dir / "cards/default");
    EXPECT_EQ(flow.GetStage(), Stage::kClosed) << "nothing to say";

    flow.ShowStartup({{dir / "live", {{"kick.wav", "no"}}}}, dir / "cards/default");
    EXPECT_EQ(flow.GetStage(), Stage::kReport);
    EXPECT_EQ(flow.GetReport().title, "Started with another card");
    flow.Close();

    flow.ShowStartup({{dir / "live", {{"kick.wav", "no"}}}}, {});
    EXPECT_EQ(flow.GetReport().title, "No card");
}

TEST_F(Flow, InsertsTheFirstCardWhenNoneIsIn)
{
    cards.current.clear();
    MakeCard(dir / "cards/live");
    flow.Open();
    flow.Choose(Choice::kSelect);
    flow.Picked(dir / "cards/live");
    flow.Confirm();
    flow.Insert();
    EXPECT_EQ(cards.current, dir / "cards/live");
    EXPECT_TRUE(cards.cycled_back.empty());
    EXPECT_EQ(cards.saved, (CardSettings{dir / "cards/live", dir / "cards/older"}));
}

// ---- The folder picker -------------------------------------------------------------------------

namespace
{
struct Picker : ::testing::Test
{
    Picker()
    {
        fs::create_directories(dir / "cards/default");
        fs::create_directories(dir / "cards/live");
        picker.Open(Mode::kSelect, dir / "cards", "");
    }

    FolderPicker::Result Click(int entry, int ms)
    {
        const layout::Rect r = picker.EntryRect(entry);
        return picker.Click(r.x + 5, r.y + r.h / 2, t0 + std::chrono::milliseconds(ms));
    }

    using Kind = FolderPicker::Result::Kind;
    using Focus = FolderPicker::Focus;
    TempDir                      dir;
    FolderPicker                 picker;
    FolderPicker::Clock::time_point t0 = FolderPicker::Clock::now();
};
} // namespace

TEST_F(Picker, ShowsTheFolderInThePathField)
{
    EXPECT_TRUE(picker.IsOpen());
    EXPECT_EQ(picker.PathField().Text(), (dir / "cards").string());
    EXPECT_EQ(picker.Browser().Selected(), 1);
}

TEST_F(Picker, KeysMoveOpenAndGoUp)
{
    picker.Key(KEY_DOWN, false);
    EXPECT_EQ(picker.Browser().Selected(), 2);
    picker.Key(KEY_ENTER, false);
    EXPECT_EQ(picker.Browser().Path(), dir / "cards/live");
    EXPECT_EQ(picker.PathField().Text(), (dir / "cards/live").string());
    picker.Key(KEY_BACKSPACE, false);
    EXPECT_EQ(picker.Browser().Path(), dir / "cards");

    const FolderPicker::Result chosen = picker.Key(KEY_ENTER, true);
    EXPECT_EQ(chosen.kind, Kind::kChosen) << "Ctrl+Enter uses the folder shown";
    EXPECT_EQ(chosen.path, dir / "cards");
    EXPECT_EQ(picker.Key(KEY_ESC, false).kind, Kind::kCancelled);
}

TEST_F(Picker, TypingAPathGoesThere)
{
    picker.Text("/");
    EXPECT_EQ(picker.GetFocus(), Focus::kPath);
    picker.Text((dir / "cards/live").string().substr(1));
    picker.Key(KEY_ENTER, false);
    EXPECT_EQ(picker.Browser().Path(), dir / "cards/live");
    EXPECT_EQ(picker.GetFocus(), Focus::kList);

    picker.Key(KEY_TAB, false);
    EXPECT_EQ(picker.GetFocus(), Focus::kPath);
    picker.Text("/missing");
    picker.Key(KEY_ENTER, false);
    EXPECT_EQ(picker.Browser().Path(), dir / "cards/live") << "a path that isn't there goes nowhere";
    EXPECT_NE(picker.Error().find("doesn't exist"), std::string::npos);
}

TEST_F(Picker, AClickOpensAndADoubleClickChooses)
{
    EXPECT_EQ(Click(2, 0).kind, Kind::kNone);
    EXPECT_EQ(picker.Browser().Selected(), 2);
    EXPECT_EQ(picker.Browser().Path(), dir / "cards") << "not yet: it may be a double-click";
    const FolderPicker::Result chosen = Click(2, 200);
    EXPECT_EQ(chosen.kind, Kind::kChosen);
    EXPECT_EQ(chosen.path, dir / "cards/live");

    Click(1, 1000);
    picker.Tick(t0 + std::chrono::milliseconds(1100));
    EXPECT_EQ(picker.Browser().Path(), dir / "cards") << "still inside the double-click time";
    picker.Tick(t0 + std::chrono::milliseconds(1400));
    EXPECT_EQ(picker.Browser().Path(), dir / "cards/default") << "a single click opens it";
}

TEST_F(Picker, CreateModeNamesTheNewFolder)
{
    picker.Open(Mode::kCreate, dir / "cards", "new card");
    EXPECT_EQ(picker.NameField().Text(), "new card");
    EXPECT_EQ(picker.ButtonLabel(), "Create");

    const FolderPicker::Result made = picker.Click(FolderPicker::kButton.x + 1, FolderPicker::kButton.y + 1, t0);
    EXPECT_EQ(made.kind, Kind::kChosen);
    EXPECT_EQ(made.path, dir / "cards/new card");

    picker.Text("x"); // typing in the list goes to the name, starting it over
    EXPECT_EQ(picker.GetFocus(), Focus::kName);
    EXPECT_EQ(picker.NameField().Text(), "x");
    picker.Key(KEY_BACKSPACE, false);
    picker.Text("a/b");
    EXPECT_EQ(picker.Key(KEY_ENTER, false).kind, Kind::kNone);
    EXPECT_EQ(picker.Error(), "A folder's name can't hold /.");

    picker.SetError("live already exists; choose another name or select it instead.");
    EXPECT_FALSE(picker.Error().empty());
    picker.Key(KEY_LEFT, false);
    EXPECT_TRUE(picker.Error().empty()) << "the next key clears it";
}

TEST_F(Picker, AClickOutsideCancels)
{
    EXPECT_EQ(picker.Click(1, 1, t0).kind, Kind::kCancelled);
}
