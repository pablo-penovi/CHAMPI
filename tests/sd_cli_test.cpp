// The SD-card command-line options.
#include <gtest/gtest.h>

#include "sd_card.h"
#include "sd_cli.h"
#include "test_util.h"

namespace fs = std::filesystem;
using namespace champi;

namespace
{
// The message ParseSdOptions refuses `args` with.
std::string Refusal(std::vector<std::string> args)
{
    try
    {
        ParseSdOptions(args);
    }
    catch(const std::invalid_argument& e)
    {
        return e.what();
    }
    return "";
}
} // namespace

TEST(SdCli, TakesOutOnlyTheSdOptions)
{
    std::vector<std::string> args = {"--sd-dir", "/cards/live", "--script", "s.txt"};
    const SdOptions          sd   = ParseSdOptions(args);
    EXPECT_EQ(sd.dir, fs::path("/cards/live"));
    EXPECT_FALSE(sd.reset);
    EXPECT_EQ(args, (std::vector<std::string>{"--script", "s.txt"}));

    args = {"--sd-dir=/cards/other", "--sd-reset", "--yes"};
    const SdOptions reset = ParseSdOptions(args);
    EXPECT_EQ(reset.dir, fs::path("/cards/other"));
    EXPECT_TRUE(reset.reset);
    EXPECT_TRUE(args.empty());
}

TEST(SdCli, NoCardGivenIsEmpty)
{
    std::vector<std::string> args;
    const SdOptions          sd = ParseSdOptions(args);
    EXPECT_TRUE(sd.dir.empty());
    EXPECT_FALSE(sd.reset);
}

TEST(SdCli, RejectsAMissingValue)
{
    EXPECT_EQ(Refusal({"--sd-dir"}), "--sd-dir needs a value");
}

TEST(SdCli, ResetNeedsYes)
{
    EXPECT_NE(Refusal({"--sd-reset"}).find("add --yes"), std::string::npos);
    EXPECT_NE(Refusal({"--yes"}).find("--sd-reset"), std::string::npos);
}

TEST(SdCli, TheImageOptionsNameTheirReplacement)
{
    EXPECT_NE(Refusal({"--sd-image", "card.img"}).find("removed in 1.3"), std::string::npos);
    EXPECT_NE(Refusal({"--sd-image=card.img"}).find("--sd-dir"), std::string::npos);
    for(const char* option : {"--sd-import", "--sd-export"})
    {
        const std::string message = Refusal({option, "/somewhere"});
        EXPECT_NE(message.find("removed in 1.3"), std::string::npos) << message;
        EXPECT_NE(message.find("file manager"), std::string::npos) << message;
        EXPECT_NE(message.find("--sd-dir"), std::string::npos) << message;
    }
}

TEST(SdCli, ResetRestoresTheFactoryFiles)
{
    TempDir dir;
    WriteHostFile(dir / "card/presets.json", "[\"mine\"]");
    WriteHostFile(dir / "card/my_notes.txt", "keep");
    ResetCard(dir / "card");
    EXPECT_EQ(ReadHostFile(dir / "card/presets.json"), ReadHostFile(FactoryCardDir() / "presets.json"));
    EXPECT_TRUE(fs::exists(dir / "card/jammi_a1.wav"));
    EXPECT_EQ(ReadHostFile(dir / "card/my_notes.txt"), "keep");

    // A card that isn't there yet is created.
    ResetCard(dir / "fresh");
    EXPECT_TRUE(fs::exists(dir / "fresh/options.json"));
}
