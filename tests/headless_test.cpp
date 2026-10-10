// Golden tests: TAPE running in champi-headless, driven by scripts, checked through the WAV and the
// LED log it writes. Each run is its own champi-headless process, since a firmware runs once per
// process.
//
// After boot TAPE is in JAMMI mode on slot 15, which holds its built-in sample: a C4 sine
// (261.63 Hz) on the right channel. A key plays it transposed by its note in key_map minus 60:
// KEY8 is note 60, KEY1 note 48. That makes pitches easy to check exactly.
//
// The shift menu works on the card's files, so those tests check the card afterwards. White key n
// is slot n there; KEY16 and KEY17 pick JAMMI and CUBBI (again for the next bank), and KEY23-25
// erase, copy and save. Each is: hold CHOMPI, press the menu keys, let go, press CHOMPI to confirm.
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

// The CHOMPI key's LED, as numbered in LedBefore.
constexpr int kChompiLed = 26;

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

    // The index of the first event from `from` on whose text is `text`.
    size_t IndexOf(const std::string& text, size_t from = 0) const
    {
        for(size_t i = from; i < log.size(); i++)
            if(log[i].text == text)
                return i;
        ADD_FAILURE() << "no \"" << text << "\" in the log";
        return log.size();
    }

    // The frame of the first event from `from` on whose text is `text`.
    uint64_t FrameOf(const std::string& text, size_t from = 0) const
    {
        const size_t i = IndexOf(text, from);
        return i < log.size() ? log[i].frame : 0;
    }

    // LED `led` (1-28 for the keys, 29-34 for ENC1-6, 35 for ENC5's second) as rrggbb, as the
    // log last showed it before the event at `index`.
    std::string LedBefore(size_t index, int led) const
    {
        for(size_t i = std::min(index, log.size()); i-- > 0;)
            if(log[i].text.rfind("leds ", 0) == 0)
                return log[i].text.substr(5 + 7 * (led - 1), 6);
        return "";
    }

    // Every MIDI message the firmware sent, as logged.
    std::vector<std::string> MidiOut() const
    {
        std::vector<std::string> out;
        for(const Event& e : log)
            if(e.text.rfind("midiout ", 0) == 0)
                out.push_back(e.text.substr(8));
        return out;
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

    // The RMS with the DC offset taken out.
    double AcRms(uint64_t start, size_t frames) const
    {
        double sum = 0, sum2 = 0;
        size_t n   = 0;
        for(size_t i = start; i < start + frames && i < right.size(); i++, n++)
        {
            sum += right[i];
            sum2 += double(right[i]) * right[i];
        }
        return n ? std::sqrt(std::max(0.0, sum2 / n - (sum / n) * (sum / n))) : 0;
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

// Runs champi-headless's SD-card commands on `card`, such as --sd-import.
int RunSdCommand(const std::filesystem::path& card, const std::string& args)
{
    const std::string command = std::string("'") + CHAMPI_HEADLESS + "' --sd-image '" + card.string()
                                + "' " + args + " > /dev/null";
    const int status = std::system(command.c_str());
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// The file names in the card's root.
std::vector<std::string> CardFiles(const std::filesystem::path& card)
{
    daisycola::SdOpenImage(card.string());
    std::vector<std::string> names;
    for(const auto& e : daisycola::SdList("/"))
        names.push_back(e.name);
    daisycola::SdCloseImage();
    return names;
}

bool Contains(const std::vector<std::string>& names, const std::string& name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

std::string FactoryFile(const std::string& name)
{
    return ReadHostFile(std::filesystem::path(CHAMPI_FACTORY_CARD_DIR) / name);
}

// A shift-menu action: hold CHOMPI, press `keys` in turn, let go, then press CHOMPI to confirm.
std::string ShiftMenu(const std::vector<int>& keys, bool confirm = true)
{
    std::string s = "key 26 down\nwait 300ms\n";
    for(int k : keys)
        s += "key " + std::to_string(k) + " down\nwait 100ms\nkey " + std::to_string(k)
             + " up\nwait 200ms\n";
    s += "key 26 up\nwait 300ms\n";
    if(confirm)
        s += "key 26 down\nwait 100ms\nkey 26 up\n";
    return s;
}

// A short press.
std::string Tap(int key)
{
    return "key " + std::to_string(key) + " down\nwait 100ms\nkey " + std::to_string(key) + " up\n";
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

TEST(Headless, RecordsASampleFromLineInAndFromTheMic)
{
    // With the toggle off, the CHOMPI key records into slot 15 from line in if a plug is in, or
    // from the mic. Toggled back on, the keys play the recording: KEY8 at its own pitch.
    TempDir      dir;
    const Result run = RunHeadless(dir, dir / "card.img",
                                   std::string(kBoot)
                                       + "input line sine 440\ninput mic sine 330\n"
                                         "linein on\ntoggle off\nwait 300ms\n"
                                         "key 26 down\nwait 1s\nkey 26 up\nwait 300ms\n"
                                         "toggle on\nwait 500ms\n"
                                         "key 8 down\nwait 1s\nkey 8 up\nwait 500ms\n"
                                         "linein off\ntoggle off\nwait 300ms\n"
                                         "key 26 down\nwait 1s\nkey 26 up\nwait 300ms\n"
                                         "toggle on\nwait 500ms\n"
                                         "key 8 down\nwait 1s\nkey 8 up\nwait 500ms\n");
    ASSERT_EQ(run.exit_code, 0);

    const size_t second = run.IndexOf("linein off");
    EXPECT_NEAR(run.PitchAt("key 8 down"), 440, 440 * 0.005) << "line in";
    EXPECT_NEAR(run.Pitch(run.FrameOf("key 8 down", second) + 12000, 24000), 330, 330 * 0.005) << "mic";

    // The CHOMPI key's LED is red while it records.
    EXPECT_EQ(run.LedBefore(run.IndexOf("key 26 up"), kChompiLed), "170000");
}

TEST(Headless, TheLooperRecordsOverdubsAndPauses)
{
    // Loop starts the first recording; the second press closes the loop and overdubs, the third
    // stops overdubbing. Play pauses and resumes. With the factory options' tape slew, a pause is a
    // tape stop: the loop slows down over about a second, then holds its last sample (a small DC
    // offset, which a real CHOMPI's output capacitors would block).
    TempDir      dir;
    const Result run = RunHeadless(dir, dir / "card.img",
                                   std::string(kBoot) + Tap(28) + "wait 200ms\n"
                                       + "key 8 down\nwait 1s\nkey 8 up\nwait 300ms\n" + Tap(28)
                                       + "wait 1500ms\n" + Tap(28) + "wait 1500ms\nmark looping\nwait 1s\n"
                                       + Tap(27) + "wait 2s\nmark paused\nwait 500ms\n" + Tap(27)
                                       + "wait 1s\nmark resumed\nwait 1s\n");
    ASSERT_EQ(run.exit_code, 0);

    // No key is held from here on: what plays is the loop.
    for(const char* when : {"mark looping", "mark resumed"})
    {
        EXPECT_GT(run.Rms(run.FrameOf(when), 24000), 5e-3) << when;
        EXPECT_NEAR(run.Pitch(run.FrameOf(when), 24000), kC4, kC4 * 0.005) << when;
    }
    EXPECT_LT(run.AcRms(run.FrameOf("mark paused"), 24000), 3e-4) << "paused";
}

TEST(Headless, TheShiftMenuCopiesErasesAndSwitchesBanksAndModes)
{
    TempDir    dir;
    const auto card = dir / "card.img";
    const Result run = RunHeadless(dir, card,
                                   std::string(kBoot)
                                       + ShiftMenu({24, 1, 2}) + "wait 4s\n"  // copy slot 1 to 2
                                       + ShiftMenu({23, 3}) + "wait 3s\n"     // erase slot 3
                                       + ShiftMenu({16, 23, 4}) + "wait 3s\n" // bank B, erase slot 4
                                       + ShiftMenu({17, 23, 5}) + "wait 3s\n" // CUBBI, erase slot 5
    );
    ASSERT_EQ(run.exit_code, 0);

    EXPECT_EQ(ReadCardFile(card, "/jammi_a2.wav"), FactoryFile("jammi_a1.wav")) << "copied";
    EXPECT_EQ(ReadCardFile(card, "/jammi_a1.wav"), FactoryFile("jammi_a1.wav")) << "the source stays";

    const std::vector<std::string> files = CardFiles(card);
    for(const char* gone : {"jammi_a3.wav", "jammi_a3_double.wav", "jammi_b4.wav", "cubbi_a5.wav"})
        EXPECT_FALSE(Contains(files, gone)) << gone << " erased";
    for(const char* kept : {"jammi_a4.wav", "jammi_b3.wav", "jammi_b5.wav", "jammi_a5.wav", "cubbi_b5.wav"})
        EXPECT_TRUE(Contains(files, kept)) << kept << " kept";
}

TEST(Headless, OptionsOnTheCardSetMidiChannelsAndRecordLatch)
{
    TempDir    dir;
    const auto card = dir / "card.img";

    // The factory options with MIDI in on channel 2, out on channel 3 and record latch on.
    std::string options = FactoryFile("options.json");
    auto        set     = [&](const std::string& name, const std::string& from, const std::string& to) {
        const size_t at = options.find(from, options.find(name));
        ASSERT_NE(at, std::string::npos) << name;
        options.replace(at, from.size(), to);
    };
    set("Record Latch", "false", "true");
    set("Midi In Channel", "1", "2");
    set("Midi Out Channel", "1", "3");
    WriteHostFile(dir / "options" / "options.json", options);
    ASSERT_EQ(RunSdCommand(card, "--sd-import '" + (dir / "options").string() + "'"), 0);

    const Result run = RunHeadless(dir, card,
                                   std::string(kBoot)
                                       + "mark channel 1\nmidi 90 3c 7f\nwait 500ms\nmidi 80 3c 00\nwait 500ms\n"
                                         "mark channel 2\nmidi 91 3c 7f\nwait 500ms\nmidi 81 3c 00\nwait 500ms\n"
                                         "key 8 down\nwait 300ms\nkey 8 up\nwait 300ms\n"
                                         "toggle off\nwait 300ms\n"
                                       + Tap(26) + "wait 1s\nmark latched\n" + Tap(26) + "wait 300ms\nmark stopped\n");
    ASSERT_EQ(run.exit_code, 0);

    EXPECT_LT(run.Rms(run.FrameOf("mark channel 1") + 6000, 12000), 1e-4) << "channel 1 is ignored";
    EXPECT_NEAR(run.Pitch(run.FrameOf("mark channel 2") + 6000, 12000), kC4, kC4 * 0.005) << "channel 2 plays";
    const std::vector<std::string> out = run.MidiOut();
    EXPECT_TRUE(std::find(out.begin(), out.end(), "92 3c 7f") != out.end()) << "KEY8 sends on channel 3";

    // With latch, a tap starts recording and the next one stops it.
    EXPECT_EQ(run.LedBefore(run.IndexOf("mark latched"), kChompiLed), "170000") << "still recording";
    EXPECT_NE(run.LedBefore(run.IndexOf("mark stopped"), kChompiLed), "170000") << "stopped";

    // TAPE writes options.json back at boot, keeping what it read.
    EXPECT_EQ(ReadCardFile(card, "/options.json"), options);
}

TEST(Headless, TestModeRunsTheFactoryTest)
{
    // Holding ENC6 at power-on opens the test page: with the toggle on, every output plays a
    // 100 Hz test tone at 0.2. Pressing CHOMPI closes it, but only once every check has passed:
    // each key and push pressed, each encoder turned both ways, the toggle flipped both ways, a
    // line-in plug, USB plugged in again, the SD card read and written, and 20 notes in a row
    // looped from MIDI out back to MIDI in.
    // TAPE counts ENC6 over 5000 polls of 100 us after it starts, which takes longer under the
    // sanitizers: hold it until it has booted, as champi --test-mode does.
    std::string script = "push 6 down\nboot\npush 6 up\nwait 2s\n";
    for(int k = 1; k <= 28; k++)
        if(k != 26)
            script += "key " + std::to_string(k) + " down\nwait 30ms\nkey " + std::to_string(k) + " up\nwait 30ms\n";
    for(int e = 1; e <= 6; e++)
        script += "push " + std::to_string(e) + " down\nwait 50ms\npush " + std::to_string(e) + " up\nwait 50ms\n";
    for(int e = 1; e <= 6; e++)
        script += "turn " + std::to_string(e) + " +8\nwait 150ms\nturn " + std::to_string(e) + " -8\nwait 150ms\n";
    script += "toggle off\nwait 100ms\ntoggle on\nwait 100ms\nlinein on\nwait 100ms\n"
              "usb off\nwait 200ms\nusb on\nwait 200ms\n"
              "mark too soon\n"
            + Tap(26) + "wait 500ms\n"
              "midiloop on\nwait 4s\nmark done\n"
            + Tap(26) + "wait 1s\nmark closed\nwait 500ms\n";

    TempDir      dir;
    const Result run = RunHeadless(dir, dir / "card.img", script);
    ASSERT_EQ(run.exit_code, 0);

    EXPECT_GT(run.FrameOf("mark too soon"), 0u);
    const double tone = run.Pitch(run.FrameOf("mark too soon") - 24000, 24000);
    EXPECT_NEAR(tone, 100, 0.5) << "the test tone";
    EXPECT_NEAR(run.Rms(run.FrameOf("mark too soon") - 24000, 24000), 0.2 / std::sqrt(2.0), 0.005);
    EXPECT_NEAR(run.Pitch(run.FrameOf("mark done") - 24000, 24000), 100, 0.5)
        << "CHOMPI doesn't close the test before the MIDI check passes";
    EXPECT_LT(run.Rms(run.FrameOf("mark closed"), 12000), 1e-4) << "closed once it passed";
    EXPECT_FALSE(run.MidiOut().empty()) << "the test sends notes";
}

TEST(Headless, PullingTheCardFallsBackToTheBuiltInSample)
{
    // Without its card TAPE blinks every LED red for 3 s, then plays only its built-in sample:
    // the shift menu won't pick slot 2, and putting the card back needs a restart.
    TempDir      dir;
    const Result run = RunHeadless(dir, dir / "card.img",
                                   std::string(kBoot) + "sd out\nwait 5s\n" + ShiftMenu({2}, false)
                                       + "wait 1s\nkey 8 down\nwait 1s\nkey 8 up\nwait 300ms\n"
                                         "sd in\nwait 3s\nmark back in\nkey 8 down\nwait 1s\nkey 8 up\n");
    ASSERT_EQ(run.exit_code, 0);

    // Every LED red at once, then dark, then red again.
    const size_t pulled = run.IndexOf("sd out");
    int          flashes = 0;
    bool         lit     = false;
    for(size_t i = pulled; i < run.log.size() && run.log[i].ms < run.log[pulled].ms + 3000; i++)
        if(run.log[i].text.rfind("leds ", 0) == 0)
        {
            const std::string& t   = run.log[i].text;
            bool               red = true;
            for(int led = 1; led <= 35; led++)
            {
                const std::string c = t.substr(5 + 7 * (led - 1), 6);
                red &= c != "000000" && c.substr(2) == "0000";
            }
            if(red && !lit)
                flashes++;
            lit = red;
        }
    EXPECT_GE(flashes, 3) << "the no-card blink";

    EXPECT_NEAR(run.PitchAt("key 8 down"), kC4, kC4 * 0.005) << "slot 2 wasn't picked";
    EXPECT_NEAR(run.Pitch(run.FrameOf("key 8 down", run.IndexOf("mark back in")) + 12000, 24000), kC4,
                kC4 * 0.005)
        << "still the built-in sample";
}

TEST(Headless, TheFirstPressOfEachCubbiVoicePlays)
{
    // A cubbi voice sets its play window before its file has opened, while TAPE still sees a file
    // size of 0. On the chip that arithmetic saturates and the window is accepted; daisycola's
    // ff.h makes the host do the same. Without it each voice's first press was silent.
    TempDir      dir;
    const Result run = RunHeadless(dir, dir / "card.img",
                                   std::string(kBoot) + ShiftMenu({17}, false)
                                       + "wait 1s\n"
                                         "key 8 down\nwait 1s\nkey 8 up\nwait 500ms\n"
                                         "key 9 down\nwait 1s\nkey 9 up\nwait 500ms\n");
    ASSERT_EQ(run.exit_code, 0);

    EXPECT_GT(run.Rms(run.FrameOf("key 8 down") + 12000, 24000), 5e-3) << "KEY8's first press";
    EXPECT_GT(run.Rms(run.FrameOf("key 9 down") + 12000, 24000), 5e-3) << "KEY9's first press";
}
