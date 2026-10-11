// The virtual SD card: where cards live, creating one from the factory card, and restoring the
// factory files.
#include <algorithm>
#include <cstdlib>
#include <gtest/gtest.h>

#include "card_check.h"
#include "sd_card.h"
#include "test_util.h"

namespace fs = std::filesystem;
using namespace champi;

namespace
{
std::vector<std::string> SortedNames(const fs::path& dir)
{
    std::vector<std::string> names;
    for(const auto& e : fs::directory_iterator(dir))
        names.push_back(e.path().filename().string());
    std::sort(names.begin(), names.end());
    return names;
}

// A small stand-in for a card profile.
fs::path MakeProfile(const TempDir& dir)
{
    const fs::path profile = dir / "profile";
    WriteHostFile(profile / "options.json", "{\"chompi\": []}");
    WriteHostFile(profile / "presets.json", "[]");
    WriteHostFile(profile / "jammi_a1.wav", std::string(5000, 'w'));
    WriteHostFile(profile / "FIRMWARE.bin", "bin");
    return profile;
}

// Sets an environment variable for the life of the object, then puts it back.
class Env
{
  public:
    Env(const char* name, const char* value) : name_(name)
    {
        const char* old = std::getenv(name);
        had_            = old != nullptr;
        if(had_)
            old_ = old;
        value ? setenv(name, value, 1) : unsetenv(name);
    }
    ~Env() { had_ ? setenv(name_, old_.c_str(), 1) : unsetenv(name_); }

  private:
    const char* name_;
    bool        had_ = false;
    std::string old_;
};

} // namespace

TEST(SdCard, CardsDirFollowsXdg)
{
    {
        Env xdg("XDG_DATA_HOME", "/xdg/data");
        EXPECT_EQ(CardsDir(), fs::path("/xdg/data/champi/cards"));
        EXPECT_EQ(DefaultCardDir(), fs::path("/xdg/data/champi/cards/default"));
    }
    Env xdg("XDG_DATA_HOME", nullptr);
    Env home("HOME", "/home/someone");
    EXPECT_EQ(CardsDir(), fs::path("/home/someone/.local/share/champi/cards"));
    EXPECT_EQ(DefaultCardDir(), fs::path("/home/someone/.local/share/champi/cards/default"));
    {
        Env no_home("HOME", nullptr);
        EXPECT_THROW(CardsDir(), std::runtime_error);
    }
}

TEST(SdCard, CreatesACardFromTheFactoryCard)
{
    TempDir        dir;
    const fs::path factory = FactoryCardDir();
    ASSERT_TRUE(fs::is_directory(factory)) << factory;

    const fs::path card = dir / "cards/new card"; // the parent is made too
    CreateCard(card, factory);
    EXPECT_EQ(SortedNames(card), SortedNames(factory));
    EXPECT_EQ(ReadHostFile(card / "presets.json"), ReadHostFile(factory / "presets.json"));
    EXPECT_EQ(CheckCard(card), std::vector<CardProblem>{}) << "the factory card passes the check";

    // An empty folder is fine too.
    fs::create_directories(dir / "empty");
    CreateCard(dir / "empty", factory);
    EXPECT_EQ(SortedNames(dir / "empty"), SortedNames(factory));
}

TEST(SdCard, CreateRefusesAFolderThatIsntEmpty)
{
    TempDir        dir;
    const fs::path profile = MakeProfile(dir);
    WriteHostFile(dir / "taken/notes.txt", "mine");

    try
    {
        CreateCard(dir / "taken", profile);
        FAIL() << "a folder with files in it isn't a new card";
    }
    catch(const std::runtime_error& e)
    {
        EXPECT_NE(std::string(e.what()).find("already exists"), std::string::npos) << e.what();
    }
    EXPECT_EQ(SortedNames(dir / "taken"), (std::vector<std::string>{"notes.txt"})) << "left as it was";

    WriteHostFile(dir / "a file", "x");
    EXPECT_THROW(CreateCard(dir / "a file", profile), std::runtime_error);
    EXPECT_THROW(CreateCard(dir / "card", dir / "missing"), std::runtime_error);
    EXPECT_FALSE(fs::exists(dir / "card")) << "nothing made from a missing profile";
}

TEST(SdCard, RestoreFactoryFilesTouchesFactoryNamesOnly)
{
    TempDir        dir;
    const fs::path profile = MakeProfile(dir);
    const fs::path card    = dir / "card";
    WriteHostFile(card / "jammi_a1.wav", "my own sample");
    WriteHostFile(card / "OPTIONS.JSON", "{}"); // the same file as options.json on FAT
    WriteHostFile(card / "cubbi_b2.wav", "mine too");
    WriteHostFile(card / "presets.json", "[1]");

    RestoreFactoryFiles(card, profile);
    EXPECT_EQ(SortedNames(card), (std::vector<std::string>{"FIRMWARE.bin", "cubbi_b2.wav", "jammi_a1.wav",
                                                           "options.json", "presets.json"}));
    EXPECT_EQ(ReadHostFile(card / "jammi_a1.wav"), std::string(5000, 'w'));
    EXPECT_EQ(ReadHostFile(card / "options.json"), "{\"chompi\": []}");
    EXPECT_EQ(ReadHostFile(card / "presets.json"), "[]");
    EXPECT_EQ(ReadHostFile(card / "cubbi_b2.wav"), "mine too") << "not a factory name";

    EXPECT_THROW(RestoreFactoryFiles(dir / "missing", profile), std::runtime_error);
}

TEST(SdCard, FindsCardFilesWithoutRegardToCase)
{
    TempDir dir;
    WriteHostFile(dir / "Options.JSON", "{}");
    EXPECT_EQ(FindCardFile(dir.path(), "options.json"), dir / "Options.JSON");
    EXPECT_EQ(FindCardFile(dir.path(), "presets.json"), fs::path());
    EXPECT_EQ(FindCardFile(dir / "missing", "options.json"), fs::path());
}

TEST(SdCard, ReadsTapesMidiInChannelFromItsOptions)
{
    const std::string factory = ReadHostFile(champi::FactoryCardDir() / "options.json");
    EXPECT_EQ(champi::MidiInChannelFromOptions(factory), 0);
    std::string ten = factory;
    const size_t at = ten.find("\"Midi In Channel\",\n\t\t\t\"value\": 1");
    ASSERT_NE(at, std::string::npos);
    ten.replace(at + std::string("\"Midi In Channel\",\n\t\t\t\"value\": ").size(), 1, "10");
    EXPECT_EQ(champi::MidiInChannelFromOptions(ten), 9);
    // The out channel isn't the in channel, and nonsense is channel 1, as in TAPE.
    EXPECT_EQ(champi::MidiInChannelFromOptions(R"({"chompi":[{"name":"Midi Out Channel","value":5}]})"), 0);
    EXPECT_EQ(champi::MidiInChannelFromOptions(R"([{"name": "Midi In Channel", "value": 17}])"), 0);
    EXPECT_EQ(champi::MidiInChannelFromOptions(R"([{"name": "Midi In Channel", "value": 16}])"), 15);
    EXPECT_EQ(champi::MidiInChannelFromOptions(""), 0);

    // From the card's own file, whatever its case; none is channel 1.
    TempDir dir;
    EXPECT_EQ(MidiInChannelOfCard(dir.path()), 0);
    WriteHostFile(dir / "OPTIONS.json", ten);
    EXPECT_EQ(MidiInChannelOfCard(dir.path()), 9);
}
