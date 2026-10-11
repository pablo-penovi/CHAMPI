// The card check: every rule of what TAPE expects on its card, and the messages.
#include <gtest/gtest.h>

#include "card_check.h"
#include "sd_card.h"
#include "test_util.h"

namespace fs = std::filesystem;
using namespace champi;

namespace
{
// The problems as "path: reason" lines.
std::vector<std::string> Lines(const fs::path& dir)
{
    std::vector<std::string> lines;
    for(const CardProblem& p : CheckCard(dir))
        lines.push_back(ToString(p));
    return lines;
}

// The only problem with a card holding one file, or "" if it passes.
std::string OnlyProblem(const std::string& name, const std::string& contents)
{
    TempDir dir;
    WriteHostFile(dir / name, contents);
    const std::vector<std::string> lines = Lines(dir.path());
    EXPECT_LE(lines.size(), 1u) << ::testing::PrintToString(lines);
    return lines.empty() ? "" : lines[0];
}

// Every problem with a card holding one file.
std::vector<std::string> Problems(const std::string& name, const std::string& contents)
{
    TempDir dir;
    WriteHostFile(dir / name, contents);
    return Lines(dir.path());
}

std::string Options(const std::string& entries)
{
    return "{\n\t\"chompi\": [\n" + entries + "\n\t]\n}";
}

std::string Option(const std::string& name, const std::string& value)
{
    return "\t\t{\n\t\t\t\"name\": \"" + name + "\",\n\t\t\t\"value\": " + value + "\n\t\t}";
}

// presets.json as TAPE writes it: [jammi, cubbi, 2], with `entry` in every slot.
std::string Presets(const std::string& entry = "[830,0,1000,0,0,1000,1000,704,500,false]", int banks = 5,
                    int slots = 14, const std::string& version = ",2")
{
    std::string out = "[";
    for(int mode = 0; mode < 2; mode++)
    {
        out += mode ? ",[" : "[";
        for(int b = 0; b < banks; b++)
        {
            out += b ? ",[" : "[";
            for(int s = 0; s < slots; s++)
                out += (s ? "," : "") + entry;
            out += "]";
        }
        out += "]";
    }
    return out + version + "]";
}

} // namespace

TEST(CardCheck, TheFactoryCardPasses)
{
    ASSERT_TRUE(fs::is_directory(FactoryCardDir()));
    EXPECT_EQ(Lines(FactoryCardDir()), std::vector<std::string>{});
}

TEST(CardCheck, AnEmptyFolderPasses)
{
    TempDir dir; // TAPE creates options.json and presets.json, and plays its built-in sample
    EXPECT_EQ(Lines(dir.path()), std::vector<std::string>{});
}

TEST(CardCheck, TheFolderMustExist)
{
    TempDir dir;
    EXPECT_EQ(Lines(dir / "missing"), std::vector<std::string>{"the folder doesn't exist."});
    WriteHostFile(dir / "file", "x");
    EXPECT_EQ(Lines(dir / "file"), std::vector<std::string>{"it's a file, not a folder; a card is a folder."});
}

// ---- Sample names ------------------------------------------------------------------------------

TEST(CardCheck, SamplesInEveryBankAndSlotPass)
{
    TempDir dir;
    for(const char* mode : {"jammi", "cubbi"})
        for(char bank = 'a'; bank <= 'e'; bank++)
            for(int slot = 1; slot <= 14; slot++)
            {
                const std::string base = std::string(mode) + "_" + bank + std::to_string(slot);
                WriteHostFile(dir / (base + ".wav"), MakeWav());
                WriteHostFile(dir / (base + "_double.wav"), MakeWav());
            }
    EXPECT_EQ(Lines(dir.path()), std::vector<std::string>{});
}

TEST(CardCheck, NamesAreComparedWithoutRegardToCase)
{
    EXPECT_EQ(OnlyProblem("JAMMI_A1.WAV", MakeWav()), "");
    EXPECT_EQ(OnlyProblem("Cubbi_E14_Double.wav", MakeWav()), "Cubbi_E14_Double.wav: there's no cubbi_e14.wav for "
                                                                "it to double. Remove it; TAPE makes _double files "
                                                                "itself.");
}

TEST(CardCheck, NamesOutsideTheBanksAndSlots)
{
    EXPECT_EQ(OnlyProblem("kick.wav", MakeWav()),
              "kick.wav: TAPE only reads samples named jammi_<bank><slot>.wav or cubbi_<bank><slot>.wav, with bank "
              "a–e and slot 1–14. Rename it, for example to jammi_a1.wav.");
    EXPECT_EQ(OnlyProblem("jammi_f2.wav", MakeWav()), "jammi_f2.wav: bank f doesn't exist; banks are a–e.");
    EXPECT_EQ(OnlyProblem("jammi_a15.wav", MakeWav()), "jammi_a15.wav: slot 15 doesn't exist; slots are 1–14.");
    EXPECT_EQ(OnlyProblem("cubbi_c0.wav", MakeWav()), "cubbi_c0.wav: slot 0 doesn't exist; slots are 1–14.");
    EXPECT_EQ(OnlyProblem("jammi_a01.wav", MakeWav()),
              "jammi_a01.wav: TAPE writes slot numbers without a leading zero. Rename it to jammi_a1.wav.");
    EXPECT_EQ(OnlyProblem("cubbi_b007_double.wav", MakeWav()),
              "cubbi_b007_double.wav: TAPE writes slot numbers without a leading zero. Rename it to "
              "cubbi_b7_double.wav.");
    EXPECT_EQ(OnlyProblem("jammi_a99999999999999.wav", MakeWav()),
              "jammi_a99999999999999.wav: slot 99999999999999 doesn't exist; slots are 1–14.");
    EXPECT_NE(OnlyProblem("tape_a1.wav", MakeWav()).find("Rename it"), std::string::npos);
}

TEST(CardCheck, NamesThatDifferOnlyByCaseClash)
{
    TempDir dir;
    WriteHostFile(dir / "jammi_a1.wav", MakeWav());
    WriteHostFile(dir / "JAMMI_A1.WAV", MakeWav());
    EXPECT_EQ(Lines(dir.path()), std::vector<std::string>{"jammi_a1.wav: differs from JAMMI_A1.WAV only by case, so on "
                                                          "a FAT card they'd be the same file. Remove or rename one."});
}

TEST(CardCheck, ADoubleNeedsItsBase)
{
    EXPECT_EQ(OnlyProblem("jammi_a9_double.wav", MakeWav()),
              "jammi_a9_double.wav: there's no jammi_a9.wav for it to double. Remove it; TAPE makes _double files "
              "itself.");
    TempDir dir;
    WriteHostFile(dir / "jammi_a9_double.wav", MakeWav());
    WriteHostFile(dir / "Jammi_A9.wav", MakeWav());
    EXPECT_EQ(Lines(dir.path()), std::vector<std::string>{});
}

// ---- WAV format --------------------------------------------------------------------------------

TEST(CardCheck, SampleFormats)
{
    WavSpec rate;
    rate.rate = 44100;
    EXPECT_EQ(OnlyProblem("cubbi_b3.wav", MakeWav(rate)),
              "cubbi_b3.wav: 44100 Hz; TAPE needs 48000 Hz. Convert it, for example with `sox cubbi_b3.wav -r 48000 "
              "out.wav`.");

    WavSpec mono;
    mono.channels = 1;
    EXPECT_EQ(OnlyProblem("jammi_c7.wav", MakeWav(mono)),
              "jammi_c7.wav: mono; TAPE needs stereo. Convert it, for example with `sox jammi_c7.wav -c 2 out.wav`.");

    WavSpec deep;
    deep.bits = 24;
    deep.data = 600;
    EXPECT_EQ(OnlyProblem("jammi_a2.wav", MakeWav(deep)),
              "jammi_a2.wav: 24-bit; TAPE needs 16-bit PCM. Convert it, for example with `sox jammi_a2.wav -b 16 "
              "out.wav`.");

    WavSpec floats;
    floats.format = 3;
    floats.bits   = 32;
    EXPECT_EQ(OnlyProblem("jammi_a3.wav", MakeWav(floats)),
              "jammi_a3.wav: 32-bit float; TAPE needs 16-bit PCM. Convert it, for example with `sox jammi_a3.wav -r "
              "48000 -c 2 -b 16 out.wav`.");

    WavSpec compressed;
    compressed.format = 2;
    EXPECT_NE(OnlyProblem("jammi_a5.wav", MakeWav(compressed)).find("compressed (WAV format 2)"), std::string::npos);

    EXPECT_EQ(OnlyProblem("jammi_a4.wav", "ID3 an mp3, renamed"),
              "jammi_a4.wav: not a WAV file (it doesn't start with RIFF/WAVE).");
    EXPECT_EQ(OnlyProblem("jammi_a6.wav", ""), "jammi_a6.wav: not a WAV file (it doesn't start with RIFF/WAVE).");
}

TEST(CardCheck, EveryFormatProblemIsListed)
{
    WavSpec bad;
    bad.channels = 1;
    bad.rate     = 22050;
    bad.bits     = 8;
    EXPECT_EQ(Problems("jammi_a1.wav", MakeWav(bad)).size(), 3u);
}

TEST(CardCheck, TheDataChunk)
{
    WavSpec cut;
    cut.data_claims = 4000;
    EXPECT_EQ(OnlyProblem("jammi_a1.wav", MakeWav(cut)),
              "jammi_a1.wav: its data chunk claims 4000 bytes but only 400 follow: the file is cut short.");

    WavSpec empty;
    empty.data = 0;
    EXPECT_EQ(OnlyProblem("jammi_a1.wav", MakeWav(empty)), "jammi_a1.wav: its data chunk is empty.");

    std::string no_data = MakeWav();
    no_data             = no_data.substr(0, 36); // RIFF header and fmt only
    no_data.replace(4, 4, Le(28, 4));
    EXPECT_EQ(OnlyProblem("jammi_a1.wav", no_data), "jammi_a1.wav: it has no data chunk, so it holds no sound.");

    std::string no_fmt = "WAVE" + std::string("data") + Le(8, 4) + std::string(8, '\0');
    no_fmt             = "RIFF" + Le(uint32_t(no_fmt.size()), 4) + no_fmt;
    EXPECT_EQ(OnlyProblem("jammi_a1.wav", no_fmt),
              "jammi_a1.wav: it has no fmt chunk, so its format is unknown; TAPE needs 48000 Hz, 16-bit, stereo PCM.");

    // A trailing half frame passes: TAPE wrote some of the factory card's _double files so.
    WavSpec odd;
    odd.data = 402;
    EXPECT_EQ(OnlyProblem("jammi_a1.wav", MakeWav(odd)), "");
}

TEST(CardCheck, ExtraChunksAndFootersPass)
{
    WavSpec extra;
    extra.before_data = "LIST" + Le(10, 4) + "INFOabcdef" + "bext" + Le(3, 4) + "xyz" + std::string(1, '\0');
    EXPECT_EQ(OnlyProblem("jammi_a1.wav", MakeWav(extra)), "");
    EXPECT_EQ(OnlyProblem("jammi_a1.wav", MakeWav() + "ID3\x03\x00 a footer after the RIFF chunk"), "");

    WavSpec liar;
    liar.before_data = "junk" + Le(100000, 4);
    EXPECT_NE(OnlyProblem("jammi_a1.wav", MakeWav(liar)).find("\"junk\" chunk claims 100000 bytes"),
              std::string::npos);
}

// ---- Other entries -----------------------------------------------------------------------------

TEST(CardCheck, TapesOwnFilesAndOneFirmwareFilePass)
{
    TempDir dir;
    for(const char* name : {"presets_temp.json", "temp_rec.wav", "test_file.txt", ".batt_log.txt", "._.batt_log.txt",
                            "CHOMPI_TAPEv2_0.bin"})
        WriteHostFile(dir / name, "anything");
    EXPECT_EQ(Lines(dir.path()), std::vector<std::string>{});

    WriteHostFile(dir / "older.BIN", "x");
    EXPECT_EQ(Lines(dir.path()), std::vector<std::string>{"older.BIN: a second firmware file next to "
                                                          "CHOMPI_TAPEv2_0.bin; the bootloader flashes any .bin, so "
                                                          "keep one at most."});
}

TEST(CardCheck, MacAndWindowsMetadataIsIgnored)
{
    TempDir dir;
    for(const char* name : {".DS_Store", "._jammi_a1.wav", "desktop.ini", "Thumbs.db"})
        WriteHostFile(dir / name, "metadata");
    for(const char* name : {".Spotlight-V100", ".fseventsd", ".Trashes", ".TemporaryItems", "System Volume Information",
                            "$RECYCLE.BIN"})
        WriteHostFile(dir / name / "inside", "metadata");
    EXPECT_EQ(Lines(dir.path()), std::vector<std::string>{});
}

TEST(CardCheck, SubfoldersAndStrayFiles)
{
    TempDir dir;
    WriteHostFile(dir / "samples/jammi_a1.wav", MakeWav());
    WriteHostFile(dir / "notes.txt", "remember the milk");
    EXPECT_EQ(Lines(dir.path()), (std::vector<std::string>{
                                     "notes.txt: TAPE doesn't use this file. Remove it.",
                                     "samples: TAPE only reads files at the card root. Move the files up, or remove "
                                     "the folder.",
                                 }));
}

TEST(CardCheck, LinksAreFollowed)
{
    TempDir dir;
    WriteHostFile(dir / "elsewhere/sample.wav", MakeWav());
    fs::create_directories(dir / "card");
    fs::create_symlink(dir / "elsewhere/sample.wav", dir / "card/jammi_a1.wav");
    EXPECT_EQ(Lines(dir / "card"), std::vector<std::string>{});

    fs::create_symlink(dir / "elsewhere/gone.wav", dir / "card/jammi_a2.wav");
    EXPECT_EQ(Lines(dir / "card"),
              std::vector<std::string>{"jammi_a2.wav: a broken link: what it points to doesn't exist."});

    // A card folder that's a link is fine too.
    fs::create_directory_symlink(dir / "card", dir / "linked");
    EXPECT_EQ(Lines(dir / "linked").size(), 1u);
}

// ---- options.json ------------------------------------------------------------------------------

TEST(CardCheck, OptionsAsTapeWritesThemPass)
{
    EXPECT_EQ(OnlyProblem("options.json", ReadHostFile(FactoryCardDir() / "options.json")), "");
    // Missing options take TAPE's defaults.
    EXPECT_EQ(OnlyProblem("options.json", Options(Option("Midi In Channel", "16"))), "");
    EXPECT_EQ(OnlyProblem("Options.Json", Options("")), "");
}

TEST(CardCheck, OptionsRanges)
{
    EXPECT_EQ(OnlyProblem("options.json", Options(Option("Record Latch", "true") + ",\n" + Option("Midi In Channel", "17"))),
              "options.json: line 9: \"Midi In Channel\" is 17; it must be 1–16.");
    EXPECT_EQ(OnlyProblem("options.json", Options(Option("Midi Out Channel", "0"))),
              "options.json: line 5: \"Midi Out Channel\" is 0; it must be 1–16.");
    EXPECT_EQ(OnlyProblem("options.json", Options(Option("Midi Out Channel", "1.5"))),
              "options.json: line 5: \"Midi Out Channel\" is 1.5; it must be 1–16.");
    EXPECT_EQ(OnlyProblem("options.json", Options(Option("Monitor Position", "4"))),
              "options.json: line 5: \"Monitor Position\" is 4; it must be 1–3.");
    EXPECT_EQ(OnlyProblem("options.json", Options(Option("Split Delay", "\"yes\""))),
              "options.json: line 5: \"Split Delay\" is \"yes\"; it must be true or false.");
    EXPECT_EQ(OnlyProblem("options.json", Options(Option("Tape Slew On", "1"))),
              "options.json: line 5: \"Tape Slew On\" is 1; it must be true or false.");
}

TEST(CardCheck, OptionsNames)
{
    EXPECT_EQ(OnlyProblem("options.json", Options(Option("Reverb", "true"))),
              "options.json: line 4: no option is called \"Reverb\"; TAPE knows Record Latch, Midi In Channel, Midi "
              "Out Channel, Tape Slew On, Monitor Position, Pitch Quantize In Shift Menu and Split Delay.");
    EXPECT_EQ(OnlyProblem("options.json", Options(Option("Split Delay", "true") + ",\n" + Option("Split Delay", "false"))),
              "options.json: line 8: \"Split Delay\" is set twice; TAPE takes each option once.");
    EXPECT_EQ(OnlyProblem("options.json", Options("{\"value\": 1}")),
              "options.json: line 3: each option is {\"name\": …, \"value\": …}, with the name in quotes.");
}

TEST(CardCheck, OptionsShapeAndSize)
{
    EXPECT_EQ(OnlyProblem("options.json", "[1, 2]"),
              "options.json: line 1: TAPE expects {\"chompi\": [{\"name\": …, \"value\": …}, …]}.");
    EXPECT_EQ(OnlyProblem("options.json", "{\n\"chompi\": 3}"),
              "options.json: line 2: TAPE expects {\"chompi\": [{\"name\": …, \"value\": …}, …]}.");
    EXPECT_EQ(OnlyProblem("options.json", "{\n\t\"chompi\": [\n\t\t{\"name\": \"Split Delay\" \"value\": true}\n]}"),
              "options.json: line 3: expected , or }; this isn't valid JSON, so TAPE would ignore it and write its "
              "defaults over it.");
    EXPECT_EQ(OnlyProblem("options.json", "{\"chompi\": []}" + std::string(4082, ' ')),
              "options.json: 4096 bytes; TAPE reads at most 4095, so the end would be lost.");
    EXPECT_EQ(OnlyProblem("options.json", "{\"chompi\": []}" + std::string(4081, ' ')), "");
}

// ---- presets.json ------------------------------------------------------------------------------

TEST(CardCheck, PresetsAsTapeWritesThemPass)
{
    EXPECT_EQ(OnlyProblem("presets.json", ReadHostFile(FactoryCardDir() / "presets.json")), "");
    EXPECT_EQ(OnlyProblem("presets.json", Presets("[0,1000,1000,0,0,1000,0,1000,0,true]")), "");
    // Version 1, without the version: 7 controls.
    EXPECT_EQ(OnlyProblem("presets.json", Presets("[830,0,1000,0,0,1000,1000,false]", 5, 14, "")), "");
}

TEST(CardCheck, PresetsShape)
{
    EXPECT_EQ(OnlyProblem("presets.json", "{}"),
              "presets.json: line 1: TAPE expects a list of the JAMMI presets, the CUBBI presets and the version, 2.");
    EXPECT_EQ(OnlyProblem("presets.json", Presets("[0,0,0,0,0,0,0,0,0,false]", 5, 14, ",3")),
              "presets.json: line 1: TAPE expects a list of the JAMMI presets, the CUBBI presets and the version, 2.");
    EXPECT_EQ(Problems("presets.json", Presets("[0,0,0,0,0,0,0,0,0,false]", 4)),
              (std::vector<std::string>{
                  "presets.json: line 1: the JAMMI presets must be a list of 5 banks, a–e.",
                  "presets.json: line 1: the CUBBI presets must be a list of 5 banks, a–e.",
              }));

    std::string short_bank = Presets();
    short_bank.replace(short_bank.find("[830"), 41, ""); // JAMMI a1's entry and its comma
    EXPECT_EQ(OnlyProblem("presets.json", short_bank), "presets.json: line 1: JAMMI bank a must be a list of 14 slots.");
}

TEST(CardCheck, PresetsEntries)
{
    std::string presets = Presets();
    presets.replace(presets.find("[830"), 40, "[830,0,1000,0,0,1000,1000,704,false]");
    EXPECT_EQ(OnlyProblem("presets.json", presets),
              "presets.json: line 1: JAMMI a1 must be 9 numbers followed by true or false.");

    presets = Presets();
    presets.replace(presets.rfind("[830"), 40, "\n[830,1500,1000,0,0,1000,1000,704,500,true]");
    EXPECT_EQ(OnlyProblem("presets.json", presets), "presets.json: line 2: CUBBI e14: start is 1500; it must be 0–1000.");

    // A whole file of bad entries is cut short.
    const std::vector<std::string> many = Problems("presets.json", Presets("[1,2,3,4,5,6,7,8,9,10]"));
    ASSERT_EQ(many.size(), 9u);
    EXPECT_EQ(many.back(), "presets.json: and 132 more problems like these.");
}

TEST(CardCheck, PresetsSize)
{
    EXPECT_EQ(OnlyProblem("presets.json", Presets() + std::string(9120 - Presets().size(), '\n')),
              "presets.json: 9120 bytes; TAPE reads at most 8191, so the end would be lost.");
}

// ---- Several at once ---------------------------------------------------------------------------

TEST(CardCheck, ReportsEveryProblemInNameOrder)
{
    TempDir dir;
    WavSpec mono;
    mono.channels = 1;
    WriteHostFile(dir / "jammi_a2.wav", MakeWav(mono));
    WriteHostFile(dir / "kick.wav", MakeWav());
    WriteHostFile(dir / "options.json", Options(Option("Midi In Channel", "17")));
    WriteHostFile(dir / "jammi_a1.wav", MakeWav());
    WriteHostFile(dir / "cubbi_b1_double.wav", MakeWav());

    std::vector<fs::path> files;
    for(const CardProblem& p : CheckCard(dir.path()))
        files.push_back(p.path);
    EXPECT_EQ(files, (std::vector<fs::path>{"cubbi_b1_double.wav", "jammi_a2.wav", "kick.wav", "options.json"}));
}
