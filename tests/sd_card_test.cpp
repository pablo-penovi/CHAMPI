// The virtual SD card: creating, seeding, importing and exporting.
#include <algorithm>
#include <cstdlib>
#include <gtest/gtest.h>
#include <sys/stat.h>

#include "daisycola/host.h"
#include "sd_card.h"
#include "test_util.h"

namespace fs = std::filesystem;
using namespace champi;

namespace
{
constexpr uint64_t kMiB = 1024 * 1024;

std::vector<std::string> SortedNames(const std::vector<daisycola::SdEntry>& entries)
{
    std::vector<std::string> names;
    for(const auto& e : entries)
        names.push_back(e.name);
    std::sort(names.begin(), names.end());
    return names;
}

// A small stand-in for a card profile.
fs::path MakeProfile(const TempDir& dir)
{
    const fs::path profile = dir / "profile";
    WriteHostFile(profile / "options.json", "{\"midi_ch_in\": 1}");
    WriteHostFile(profile / "presets.json", "[]");
    WriteHostFile(profile / "jammi_a1.wav", std::string(5000, 'w'));
    return profile;
}

} // namespace

// The chunk's "done when" test: format a full-size image, seed it with the factory card, read it
// back, and the files match the card profile.
TEST(SdCard, SeedsTheFactoryCard)
{
    TempDir        dir;
    const fs::path image   = dir / "sdcard.img";
    const fs::path factory = FactoryCardDir();
    ASSERT_TRUE(fs::is_directory(factory)) << factory;

    CreateCard(image, factory);
    EXPECT_EQ(fs::file_size(image), kSdImageSize);
    EXPECT_FALSE(fs::exists(dir / "sdcard.img.new"));

    // Sparse: only the formatted structures and the card's data use disk space.
    struct stat st = {};
    ASSERT_EQ(stat(image.c_str(), &st), 0);
    EXPECT_LT(uint64_t(st.st_blocks) * 512, 1024 * kMiB);

    std::vector<std::string> expected;
    for(const auto& e : fs::directory_iterator(factory))
        expected.push_back(e.path().filename().string());
    std::sort(expected.begin(), expected.end());

    daisycola::SdOpenImage(image.string());
    const auto listed = daisycola::SdList("/");
    for(const auto& e : listed)
    {
        EXPECT_FALSE(e.is_dir) << e.name;
        EXPECT_EQ(e.size, fs::file_size(factory / e.name)) << e.name;
    }
    EXPECT_EQ(SortedNames(listed), expected);
    const auto presets = daisycola::SdReadFile("/presets.json");
    EXPECT_EQ(std::string(presets.begin(), presets.end()), ReadHostFile(factory / "presets.json"));
    daisycola::SdCloseImage();

    ExportFromCard(image, dir / "out");
    for(const auto& name : expected)
        EXPECT_TRUE(ReadHostFile(dir / "out" / name) == ReadHostFile(factory / name)) << name;
}

TEST(SdCard, EnsureCardCreatesOnlyOnce)
{
    TempDir        dir;
    const fs::path image = dir / "data/champi/sdcard.img";

    EXPECT_TRUE(EnsureCard(image, MakeProfile(dir)));
    ASSERT_TRUE(fs::exists(image));

    WriteHostFile(dir / "extra.txt", "kept");
    ImportToCard(image, dir / "extra.txt");
    EXPECT_FALSE(EnsureCard(image, dir / "profile"));

    daisycola::SdOpenImage(image.string());
    EXPECT_EQ(SortedNames(daisycola::SdList("/")),
              (std::vector<std::string>{"extra.txt", "jammi_a1.wav", "options.json", "presets.json"}));
    daisycola::SdCloseImage();
}

TEST(SdCard, ImportsAndExportsDirectories)
{
    TempDir        dir;
    const fs::path image = dir / "sdcard.img";
    CreateCard(image, MakeProfile(dir), 64 * kMiB);

    // A directory's contents land in the card root, overwriting what is there.
    WriteHostFile(dir / "in/presets.json", "[1]");
    WriteHostFile(dir / "in/samples/cubbi_b2.wav", std::string(3000, 'c'));
    ImportToCard(image, dir / "in");

    ExportFromCard(image, dir / "out");
    EXPECT_EQ(ReadHostFile(dir / "out/presets.json"), "[1]");
    EXPECT_EQ(ReadHostFile(dir / "out/samples/cubbi_b2.wav"), std::string(3000, 'c'));
    EXPECT_EQ(ReadHostFile(dir / "out/options.json"), "{\"midi_ch_in\": 1}");
}

TEST(SdCard, ResetReplacesTheCard)
{
    TempDir        dir;
    const fs::path image = dir / "sdcard.img";
    CreateCard(image, MakeProfile(dir), 64 * kMiB);
    WriteHostFile(dir / "extra.txt", "gone after reset");
    ImportToCard(image, dir / "extra.txt");

    CreateCard(image, dir / "profile", 64 * kMiB);
    daisycola::SdOpenImage(image.string());
    EXPECT_EQ(SortedNames(daisycola::SdList("/")),
              (std::vector<std::string>{"jammi_a1.wav", "options.json", "presets.json"}));
    daisycola::SdCloseImage();
}

TEST(SdCard, FailedCreateKeepsTheOldCard)
{
    TempDir        dir;
    const fs::path image = dir / "sdcard.img";
    CreateCard(image, MakeProfile(dir), 64 * kMiB);
    const std::string before = ReadHostFile(image);

    // A profile that doesn't fit on the card. The file is sparse, so this is cheap.
    fs::create_directories(dir / "big");
    std::ofstream(dir / "big/huge.wav");
    fs::resize_file(dir / "big/huge.wav", 80 * kMiB);

    EXPECT_THROW(CreateCard(image, dir / "big", 64 * kMiB), daisycola::SdError);
    EXPECT_FALSE(fs::exists(dir / "sdcard.img.new"));
    EXPECT_TRUE(ReadHostFile(image) == before);

    EXPECT_THROW(CreateCard(image, dir / "missing"), std::runtime_error);
    EXPECT_THROW(ImportToCard(image, dir / "missing"), std::runtime_error);
}

TEST(SdCard, DefaultPathFollowsXdg)
{
    const char*       old_xdg  = std::getenv("XDG_DATA_HOME");
    const std::string saved    = old_xdg ? old_xdg : "";
    const char*       old_home = std::getenv("HOME");
    const std::string home     = old_home ? old_home : "";

    setenv("XDG_DATA_HOME", "/xdg/data", 1);
    EXPECT_EQ(DefaultSdImagePath(), fs::path("/xdg/data/champi/sdcard.img"));
    unsetenv("XDG_DATA_HOME");
    setenv("HOME", "/home/someone", 1);
    EXPECT_EQ(DefaultSdImagePath(), fs::path("/home/someone/.local/share/champi/sdcard.img"));

    setenv("HOME", home.c_str(), 1);
    if(old_xdg)
        setenv("XDG_DATA_HOME", saved.c_str(), 1);
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
}
