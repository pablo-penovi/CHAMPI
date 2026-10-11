// The folder picker's model: listing, moving about, typed paths, new names, and the text fields.
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include "folder_browser.h"
#include "sd_card.h"
#include "test_util.h"

namespace fs = std::filesystem;
using namespace champi;

namespace
{
using Mode = FolderBrowser::Mode;

std::vector<std::string> Names(const FolderBrowser& b)
{
    std::vector<std::string> names;
    for(const FolderBrowser::Entry& e : b.Entries())
        names.push_back(e.name);
    return names;
}

// A tree to browse.
void MakeTree(const TempDir& dir)
{
    fs::create_directories(dir / "cards/zeta");
    fs::create_directories(dir / "cards/Alpha");
    fs::create_directories(dir / "cards/beta/inner");
    fs::create_directories(dir / "cards/.hidden");
    WriteHostFile(dir / "cards/readme.txt", "a file");
}
} // namespace

TEST(FolderBrowser, ListsSubfoldersOnlySortedWithoutHiddenOnes)
{
    TempDir dir;
    MakeTree(dir);
    FolderBrowser b;
    b.Open(Mode::kSelect, dir / "cards");
    EXPECT_EQ(b.Path(), dir / "cards");
    EXPECT_EQ(Names(b), (std::vector<std::string>{"..", "Alpha", "beta", "zeta"}));
    EXPECT_TRUE(b.Entries()[0].up);
    EXPECT_EQ(b.Selected(), 1) << "the first subfolder, not ..";
    EXPECT_TRUE(b.Error().empty());
}

TEST(FolderBrowser, NoUpEntryAtTheRoot)
{
    FolderBrowser b;
    b.Open(Mode::kSelect, "/");
    ASSERT_FALSE(b.Entries().empty());
    EXPECT_NE(b.Entries()[0].name, "..");
    b.Up();
    EXPECT_EQ(b.Path(), fs::path("/"));
}

TEST(FolderBrowser, StartsAtTheNearestFolderThatExists)
{
    TempDir dir;
    MakeTree(dir);
    FolderBrowser b;
    b.Open(Mode::kCreate, dir / "cards/not/yet/here");
    EXPECT_EQ(b.Path(), dir / "cards");
    EXPECT_EQ(b.GetMode(), Mode::kCreate);
}

TEST(FolderBrowser, StartsInTheCardsFolder)
{
    TempDir     dir;
    const char* old = std::getenv("XDG_DATA_HOME");
    const std::string saved = old ? old : "";
    setenv("XDG_DATA_HOME", dir.path().c_str(), 1);
    fs::create_directories(CardsDir() / "default");

    FolderBrowser b;
    b.Open(Mode::kSelect, CardsDir());
    EXPECT_EQ(b.Path(), dir / "champi/cards");
    EXPECT_EQ(Names(b), (std::vector<std::string>{"..", "default"}));

    old ? setenv("XDG_DATA_HOME", saved.c_str(), 1) : unsetenv("XDG_DATA_HOME");
}

TEST(FolderBrowser, GoesIntoAndUp)
{
    TempDir dir;
    MakeTree(dir);
    FolderBrowser b;
    b.Open(Mode::kSelect, dir / "cards");
    EXPECT_EQ(b.EntryPath(2), dir / "cards/beta");
    EXPECT_EQ(b.EntryPath(0), dir.path());

    b.Open(2);
    EXPECT_EQ(b.Path(), dir / "cards/beta");
    EXPECT_EQ(Names(b), (std::vector<std::string>{"..", "inner"}));

    b.Open(0); // ".."
    EXPECT_EQ(b.Path(), dir / "cards");
    EXPECT_EQ(b.Selected(), 2) << "the folder just left is selected";

    b.Up();
    EXPECT_EQ(b.Path(), dir.path());
}

TEST(FolderBrowser, MovesTheSelectionWithinTheList)
{
    TempDir dir;
    MakeTree(dir);
    FolderBrowser b;
    b.Open(Mode::kSelect, dir / "cards");
    b.Move(1);
    EXPECT_EQ(b.Selected(), 2);
    b.Move(10);
    EXPECT_EQ(b.Selected(), 3);
    b.Move(-10);
    EXPECT_EQ(b.Selected(), 0);
    b.Select(7);
    EXPECT_EQ(b.Selected(), 0) << "out of range is ignored";
}

TEST(FolderBrowser, GoesToATypedPath)
{
    TempDir dir;
    MakeTree(dir);
    FolderBrowser b;
    b.Open(Mode::kSelect, dir.path());

    EXPECT_TRUE(b.GoTo((dir / "cards/beta/").string()));
    EXPECT_EQ(b.Path(), dir / "cards/beta");
    EXPECT_TRUE(b.GoTo("inner"));
    EXPECT_EQ(b.Path(), dir / "cards/beta/inner") << "relative to the folder shown";
    EXPECT_TRUE(b.GoTo("../../zeta/."));
    EXPECT_EQ(b.Path(), dir / "cards/zeta");

    EXPECT_FALSE(b.GoTo((dir / "missing").string()));
    EXPECT_EQ(b.Error(), (dir / "missing").string() + " doesn't exist.");
    EXPECT_EQ(b.Path(), dir / "cards/zeta") << "it stays where it was";

    EXPECT_FALSE(b.GoTo((dir / "cards/readme.txt").string()));
    EXPECT_EQ(b.Error(), (dir / "cards/readme.txt").string() + " is a file, not a folder.");

    const char* home = std::getenv("HOME");
    if(home && fs::is_directory(home))
    {
        EXPECT_TRUE(b.GoTo("~"));
        EXPECT_EQ(b.Path(), fs::path(home).lexically_normal());
        EXPECT_TRUE(b.Error().empty());
    }
}

TEST(FolderBrowser, AFolderThatCantBeReadShowsItsError)
{
    if(geteuid() == 0)
        GTEST_SKIP() << "root reads everything";
    TempDir dir;
    MakeTree(dir);
    ASSERT_EQ(chmod((dir / "cards/beta").c_str(), 0), 0);
    FolderBrowser b;
    b.Open(Mode::kSelect, dir / "cards");
    EXPECT_TRUE(b.GoTo((dir / "cards/beta").string()));
    EXPECT_EQ(b.Path(), dir / "cards/beta");
    EXPECT_EQ(Names(b), (std::vector<std::string>{".."})) << "the way back up is still there";
    EXPECT_NE(b.Error().find("Can't read this folder"), std::string::npos) << b.Error();
    chmod((dir / "cards/beta").c_str(), 0755);
}

TEST(FolderBrowser, AFolderThatWentAwayShowsItsError)
{
    TempDir dir;
    MakeTree(dir);
    FolderBrowser b;
    b.Open(Mode::kSelect, dir / "cards/zeta");
    fs::remove(dir / "cards/zeta");
    b.Refresh();
    EXPECT_FALSE(b.Error().empty());
    b.Up();
    EXPECT_EQ(Names(b), (std::vector<std::string>{"..", "Alpha", "beta"}));
}

TEST(FolderBrowser, ChecksNewNames)
{
    EXPECT_EQ(FolderBrowser::CheckNewName("new card"), "");
    EXPECT_EQ(FolderBrowser::CheckNewName("live set — 2026"), "");
    EXPECT_EQ(FolderBrowser::CheckNewName(""), "Type a name for the new folder.");
    EXPECT_EQ(FolderBrowser::CheckNewName("."), "\".\" can't be a folder's name.");
    EXPECT_EQ(FolderBrowser::CheckNewName(".."), "\"..\" can't be a folder's name.");
    EXPECT_EQ(FolderBrowser::CheckNewName("a/b"), "A folder's name can't hold /.");
    EXPECT_EQ(FolderBrowser::CheckNewName(std::string("a\0b", 3)), "A folder's name can't hold a NUL character.");

    TempDir dir;
    FolderBrowser b;
    b.Open(Mode::kCreate, dir.path());
    std::string error;
    EXPECT_EQ(b.NewFolder("new card", error), dir / "new card");
    EXPECT_EQ(error, "");
    EXPECT_EQ(b.NewFolder("..", error), fs::path());
    EXPECT_FALSE(error.empty());
}

TEST(TextField, EditsWholeCharacters)
{
    TextField f;
    f.Set("café");
    EXPECT_EQ(f.Cursor(), f.Text().size());
    f.Backspace();
    EXPECT_EQ(f.Text(), "caf") << "é is two bytes, one character";
    f.Insert("é!");
    f.Left();
    f.Left();
    f.Insert("x");
    EXPECT_EQ(f.Text(), "cafxé!");
    f.Home();
    f.Delete();
    EXPECT_EQ(f.Text(), "afxé!");
    f.End();
    f.Right();
    EXPECT_EQ(f.Cursor(), f.Text().size());
    f.Insert("\n\tpasted\r");
    EXPECT_EQ(f.Text(), "afxé!pasted") << "control characters are dropped";
}
