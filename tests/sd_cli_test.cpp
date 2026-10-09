// The SD-card command-line options.
#include <gtest/gtest.h>

#include "sd_cli.h"
#include "test_util.h"

namespace fs = std::filesystem;
using namespace champi;

TEST(SdCli, TakesOutOnlyTheSdOptions)
{
    std::vector<std::string> args = {"--sd-image", "/tmp/card.img", "--script", "s.txt",
                                     "--sd-import=/a", "--sd-import", "/b", "--sd-export", "/out",
                                     "--sd-reset"};
    const SdOptions          sd   = ParseSdOptions(args);

    EXPECT_EQ(sd.image, fs::path("/tmp/card.img"));
    EXPECT_EQ(sd.imports, (std::vector<fs::path>{"/a", "/b"}));
    EXPECT_EQ(sd.export_dir, fs::path("/out"));
    EXPECT_TRUE(sd.reset);
    EXPECT_TRUE(sd.HasCommands());
    EXPECT_EQ(args, (std::vector<std::string>{"--script", "s.txt"}));
}

TEST(SdCli, DefaultsToTheUserCard)
{
    std::vector<std::string> args;
    const SdOptions          sd = ParseSdOptions(args);
    EXPECT_EQ(sd.image.filename(), "sdcard.img");
    EXPECT_FALSE(sd.HasCommands());
}

TEST(SdCli, RejectsAMissingValue)
{
    std::vector<std::string> args = {"--sd-export"};
    EXPECT_THROW(ParseSdOptions(args), std::invalid_argument);
}

TEST(SdCli, RunsCommandsInOrder)
{
    TempDir dir;
    WriteHostFile(dir / "a/presets.json", "[\"a\"]");
    WriteHostFile(dir / "b/presets.json", "[\"b\"]");

    SdOptions sd;
    sd.image      = dir / "sdcard.img";
    sd.reset      = true;
    sd.imports    = {dir / "a", dir / "b"};
    sd.export_dir = dir / "out";
    RunSdCommands(sd); // creates the full factory card first

    EXPECT_EQ(ReadHostFile(dir / "out/presets.json"), "[\"b\"]");
    EXPECT_TRUE(fs::exists(dir / "out/options.json"));
}
