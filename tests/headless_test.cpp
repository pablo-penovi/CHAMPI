// Golden tests: TAPE running in champi-headless, driven by scripts, checked through the WAV and the
// LED log it writes. Each run is its own champi-headless process, since a firmware runs once per
// process.
//
// After boot TAPE is in JAMMI mode on slot 15, which holds its built-in sample: a C4 sine
// (261.63 Hz) on the right channel. A key plays it transposed by its note in key_map minus 60:
// KEY8 is note 60, KEY1 note 48. That makes pitches easy to check exactly.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <vector>

#include "daisycola/host.h"
#include "test_util.h"

namespace
{
constexpr double kSampleRate = 48000;
constexpr double kC4         = 261.6256; // FillDefaultSample's right channel
constexpr double kC3         = kC4 / 2;

// The boot rainbow ignores the panel until it closes, about 5 s after boot.
constexpr const char* kBoot = "boot\nwait 6s\n";

struct Event
{
    uint64_t    ms;
    uint64_t    frame;
    std::string text;
};

struct Result
{
    int                exit_code;
    std::vector<float> right; // master R
    std::vector<Event> log;

    // The frame of the first event after `from` whose text is `text`.
    uint64_t FrameOf(const std::string& text, size_t from = 0) const
    {
        for(size_t i = from; i < log.size(); i++)
            if(log[i].text == text)
                return log[i].frame;
        ADD_FAILURE() << "no \"" << text << "\" in the log";
        return 0;
    }

    // Positive-going zero crossings over [start, start + frames), with hysteresis.
    double Pitch(uint64_t start, size_t frames) const
    {
        const float threshold = 1e-3f;
        double      first = -1, last = -1;
        int         crossings = 0;
        bool        low       = false;
        for(size_t i = start + 1; i < start + frames && i < right.size(); i++)
        {
            if(right[i] < -threshold)
                low = true;
            else if(low && right[i - 1] < 0 && right[i] >= 0)
            {
                const double t = double(i - 1) + right[i - 1] / double(right[i - 1] - right[i]);
                if(first < 0)
                    first = t;
                last = t;
                crossings++;
                low = false;
            }
        }
        return crossings < 2 ? 0 : (crossings - 1) * kSampleRate / (last - first);
    }

    double Rms(uint64_t start, size_t frames) const
    {
        double sum = 0;
        size_t n   = 0;
        for(size_t i = start; i < start + frames && i < right.size(); i++, n++)
            sum += double(right[i]) * right[i];
        return n ? std::sqrt(sum / n) : 0;
    }

    // The pitch of the key pressed at `event`, measured once the note has settled.
    double PitchAt(const std::string& event) const
    {
        return Pitch(FrameOf(event) + uint64_t(0.25 * kSampleRate), size_t(0.5 * kSampleRate));
    }
};

// TAPE's playback speed for the speed knob at `value` (Engine::SetGlobalPitchFree).
double SpeedRatio(double value)
{
    double val = value < .5 ? (.5 - value) * -2 : (value - .5) * 2;
    double inv = val < 0 ? -1 : 1;
    if(std::fabs(val) < .33)
        return std::fabs(val * 1.484848 + .01 * inv);
    if(std::fabs(val) < .66)
        return std::fabs((val - .33 * inv) * 1.515151 + .5 * inv);
    return std::fabs((val - .66 * inv) * 2.941176 + 1 * inv);
}

std::vector<float> ReadMasterRight(const std::filesystem::path& wav)
{
    const std::string data = ReadHostFile(wav);
    std::vector<float> right;
    if(data.size() < 44)
        return right;
    const size_t frames = (data.size() - 44) / 8;
    right.resize(frames);
    for(size_t i = 0; i < frames; i++)
        std::memcpy(&right[i], data.data() + 44 + i * 8 + 4, 4);
    return right;
}

std::vector<Event> ReadLog(const std::filesystem::path& path)
{
    std::vector<Event> events;
    std::ifstream      in(path);
    for(std::string line; std::getline(in, line);)
    {
        std::istringstream words(line);
        Event              e;
        words >> e.ms >> e.frame;
        std::getline(words >> std::ws, e.text);
        events.push_back(e);
    }
    return events;
}

// Runs champi-headless on `card` with a script.
Result RunHeadless(const TempDir& dir, const std::filesystem::path& card, const std::string& script)
{
    WriteHostFile(dir / "script.txt", script);
    const std::string command = std::string("'") + CHAMPI_HEADLESS + "' --sd-image '" + card.string()
                                + "' --script '" + (dir / "script.txt").string() + "' --wav '"
                                + (dir / "out.wav").string() + "' --log '" + (dir / "out.log").string()
                                + "' > /dev/null";
    const int status = std::system(command.c_str());
    Result    run;
    run.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    run.right     = ReadMasterRight(dir / "out.wav");
    run.log       = ReadLog(dir / "out.log");
    return run;
}

// Lit LEDs in a `leds` log line, and how many different colours they show.
std::pair<int, int> LitLeds(const std::string& text)
{
    std::istringstream       words(text.substr(4));
    std::vector<std::string> colours;
    int                      lit = 0;
    for(std::string c; words >> c;)
        if(c != "000000")
        {
            lit++;
            if(std::find(colours.begin(), colours.end(), c) == colours.end())
                colours.push_back(c);
        }
    return {lit, int(colours.size())};
}

std::string ReadCardFile(const std::filesystem::path& card, const std::string& name)
{
    daisycola::SdOpenImage(card.string());
    const std::vector<uint8_t> data = daisycola::SdReadFile(name);
    daisycola::SdCloseImage();
    return std::string(data.begin(), data.end());
}

// The first number in presets.json: JAMMI, bank A, slot 1, the speed knob, times 1000.
int SlotOneSpeed(const std::string& presets)
{
    return std::atoi(presets.c_str() + presets.find_first_not_of('['));
}

} // namespace

TEST(Headless, BootsAndPlaysTheRainbow)
{
    TempDir   dir;
    const Result run = RunHeadless(dir, dir / "card.img", kBoot);
    ASSERT_EQ(run.exit_code, 0);

    size_t booted = 0;
    while(booted < run.log.size() && run.log[booted].text != "booted")
        booted++;
    ASSERT_LT(booted, run.log.size()) << "no boot in the log";

    // The rainbow lights all 35 LEDs in many colours, then gives way to the normal page.
    bool        rainbow = false;
    std::string last;
    for(size_t i = booted; i < run.log.size(); i++)
        if(run.log[i].text.rfind("leds", 0) == 0)
        {
            const auto [lit, colours] = LitLeds(run.log[i].text);
            rainbow |= lit == 35 && colours >= 10;
            last = run.log[i].text;
        }
    EXPECT_TRUE(rainbow) << "no rainbow after boot";
    EXPECT_LT(LitLeds(last).first, 35) << "the rainbow didn't end";
    EXPECT_GT(run.right.size(), size_t(6 * kSampleRate)) << "audio runs";
}

TEST(Headless, KeysPlayTheBuiltInSampleAtTheirPitch)
{
    TempDir   dir;
    const Result run = RunHeadless(dir, dir / "card.img",
                                std::string(kBoot)
                                    + "key 8 down\nwait 1s\nkey 8 up\nwait 500ms\n"
                                      "key 1 down\nwait 1s\nkey 1 up\nwait 500ms\n");
    ASSERT_EQ(run.exit_code, 0);

    const uint64_t key8 = run.FrameOf("key 8 down");
    EXPECT_LT(run.Rms(key8 - 24000, 24000), 1e-4) << "silent before the first key";
    EXPECT_GT(run.Rms(key8 + 12000, 24000), 5e-3) << "KEY8 plays";
    EXPECT_NEAR(run.PitchAt("key 8 down"), kC4, kC4 * 0.005);
    EXPECT_NEAR(run.PitchAt("key 1 down"), kC3, kC3 * 0.005) << "KEY1 is an octave down";
}

TEST(Headless, TheSpeedKnobChangesThePitch)
{
    // ENC4 is the speed knob. It starts at 0.83 (1x) and moves 0.003 per detent.
    TempDir   dir;
    const Result run = RunHeadless(dir, dir / "card.img",
                                std::string(kBoot)
                                    + "turn 4 +20\nwait 500ms\n"
                                      "key 8 down\nwait 1s\nkey 8 up\nwait 300ms\n"
                                      "turn 4 -40\nwait 500ms\n"
                                      "key 8 down\nwait 1s\nkey 8 up\nwait 300ms\n");
    ASSERT_EQ(run.exit_code, 0);

    const double up   = kC4 * SpeedRatio(0.83 + 20 * 0.003);
    const double down = kC4 * SpeedRatio(0.83 - 20 * 0.003);
    const size_t second = std::find_if(run.log.begin(), run.log.end(),
                                       [](const Event& e) { return e.text == "turn 4 -40"; })
                          - run.log.begin();
    EXPECT_NEAR(run.Pitch(run.FrameOf("key 8 down") + 12000, 24000), up, up * 0.005);
    EXPECT_NEAR(run.Pitch(run.FrameOf("key 8 down", second) + 12000, 24000), down, down * 0.005);
}

TEST(Headless, APresetSavedInTheShiftMenuSurvivesARestart)
{
    TempDir   dir;
    const auto card = dir / "card.img";

    // Raise the speed of the built-in sample, then save it to slot 1 (KEY1) from the shift menu:
    // hold CHOMPI, press save (KEY25), pick the slot, let go, and press CHOMPI again to confirm.
    // The presets are written to the card within about 5 s.
    const Result save = RunHeadless(dir, card,
                                 std::string(kBoot)
                                     + "turn 4 +20\nwait 500ms\n"
                                       "key 26 down\nwait 300ms\n"
                                       "key 25 down\nwait 100ms\nkey 25 up\nwait 200ms\n"
                                       "key 1 down\nwait 100ms\nkey 1 up\nwait 200ms\n"
                                       "key 26 up\nwait 300ms\n"
                                       "key 26 down\nwait 100ms\nkey 26 up\n"
                                       "wait 9s\n");
    ASSERT_EQ(save.exit_code, 0);
    const int speed = SlotOneSpeed(ReadCardFile(card, "/presets.json"));
    EXPECT_EQ(speed, 890) << "slot 1 has the saved speed (factory: 830)";
    EXPECT_NE(ReadCardFile(card, "/jammi_a1.wav"),
              ReadHostFile(std::filesystem::path(CHAMPI_FACTORY_CARD_DIR) / "jammi_a1.wav"))
        << "slot 1 has the built-in sample";

    // A new firmware run on the same card: pick slot 1 in the shift menu and play it.
    const Result load = RunHeadless(dir, card,
                                 std::string(kBoot)
                                     + "key 26 down\nwait 300ms\n"
                                       "key 1 down\nwait 100ms\nkey 1 up\nwait 200ms\n"
                                       "key 26 up\nwait 1500ms\n"
                                       "key 8 down\nwait 1s\nkey 8 up\nwait 300ms\n");
    ASSERT_EQ(load.exit_code, 0);
    const double expected = kC4 * SpeedRatio(speed / 1000.0);
    EXPECT_NEAR(load.PitchAt("key 8 down"), expected, expected * 0.005);
}
