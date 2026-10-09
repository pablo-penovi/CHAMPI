// champi-headless script parsing.
#include <gtest/gtest.h>
#include <sstream>

#include "script.h"

using champi::Command;
using champi::ParseDuration;
using champi::ScriptError;
using Type = Command::Type;

namespace
{
std::vector<Command> Parse(const std::string& text)
{
    std::istringstream in(text);
    return champi::ParseScript(in);
}
} // namespace

TEST(Script, ParsesEveryCommand)
{
    const auto c = Parse("# a comment\n"
                         "boot\n"
                         "boot 30s\n"
                         "\n"
                         "wait 250ms   # trailing comment\n"
                         "key 28 down\n"
                         "key 1 up\n"
                         "push 5 down\n"
                         "turn 4 +10\n"
                         "turn 1 -3\n"
                         "toggle off\n"
                         "linein on\n"
                         "midi 90 3C 7f\n"
                         "usb off\n"
                         "battery 3100\n"
                         "mark  KEY1   now\n");
    ASSERT_EQ(c.size(), 14u);
    EXPECT_EQ(c[0].type, Type::kBoot);
    EXPECT_EQ(c[0].ms, 60000u);
    EXPECT_EQ(c[0].line, 2);
    EXPECT_EQ(c[1].ms, 30000u);
    EXPECT_EQ(c[2].type, Type::kWait);
    EXPECT_EQ(c[2].ms, 250u);
    EXPECT_EQ(c[2].line, 5);
    EXPECT_EQ(c[3].type, Type::kKey);
    EXPECT_EQ(c[3].target, 28);
    EXPECT_EQ(c[3].value, 1);
    EXPECT_EQ(c[4].value, 0);
    EXPECT_EQ(c[5].type, Type::kPush);
    EXPECT_EQ(c[5].target, 5);
    EXPECT_EQ(c[6].type, Type::kTurn);
    EXPECT_EQ(c[6].value, 10);
    EXPECT_EQ(c[7].value, -3);
    EXPECT_EQ(c[8].type, Type::kToggle);
    EXPECT_EQ(c[8].value, 0);
    EXPECT_EQ(c[9].type, Type::kLineIn);
    EXPECT_EQ(c[9].value, 1);
    EXPECT_EQ(c[10].type, Type::kMidi);
    EXPECT_EQ(c[10].bytes, (std::vector<uint8_t>{0x90, 0x3c, 0x7f}));
    EXPECT_EQ(c[11].type, Type::kUsb);
    EXPECT_EQ(c[12].type, Type::kBattery);
    EXPECT_EQ(c[12].value, 3100);
    EXPECT_EQ(c[13].type, Type::kMark);
    EXPECT_EQ(c[13].text, "mark KEY1 now");
}

TEST(Script, ParsesDurations)
{
    EXPECT_EQ(ParseDuration("0ms"), 0u);
    EXPECT_EQ(ParseDuration("15ms"), 15u);
    EXPECT_EQ(ParseDuration("2s"), 2000u);
    EXPECT_EQ(ParseDuration("1.5s"), 1500u);
    for(const char* bad : {"", "5", "ms", "s", "-1s", "1.5m", "abc", "2 s", "1e9s"})
        EXPECT_THROW(ParseDuration(bad), std::invalid_argument) << bad;
}

TEST(Script, RejectsBadLinesWithTheirNumber)
{
    const char* bad[] = {
        "jump\n",          "key 29 down\n", "key 0 down\n", "key 1\n",      "key 1 pressed\n",
        "push 7 down\n",   "turn 4\n",      "turn 4 x\n",   "toggle up\n",  "midi\n",
        "midi 100\n",      "midi zz\n",     "wait\n",       "wait 5\n",     "battery -1\n",
        "mark\n",          "boot 1s 2s\n",  "key 1x down\n",
    };
    for(const char* line : bad)
    {
        try
        {
            Parse(std::string("boot\n") + line);
            ADD_FAILURE() << "accepted " << line;
        }
        catch(const ScriptError& e)
        {
            EXPECT_EQ(std::string(e.what()).rfind("line 2: ", 0), 0u) << e.what();
        }
    }
}
