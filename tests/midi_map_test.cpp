// The MIDI controller mapping: the targets, mapping files, which controllers' mappings apply,
// learning, the menu's table, and mapped messages played into a real PanelState on daisycola's
// manual clock. Its own program, since a PanelState attaches once per process.
#include <algorithm>
#include <memory>
#include <gtest/gtest.h>

#include "daisycola/host.h"
#include "midi_map.h"
#include "midi_map_menu.h"
#include "panel_state.h"
#include "test_util.h"

using champi::Graph;
using champi::KnobMode;
using champi::MidiBinding;
using champi::MidiLearner;
using champi::MidiMapper;
using champi::MidiMappingMenu;
using champi::MidiMappings;
using champi::MidiProfile;
using champi::MidiTarget;
using champi::PeerPort;
using champi::PortType;
using Type = MidiBinding::Type;

namespace
{
MidiBinding Note(int number, int channel = 0)
{
    MidiBinding b;
    b.type    = Type::kNote;
    b.number  = uint8_t(number);
    b.channel = uint8_t(channel);
    return b;
}

MidiBinding Cc(int number, int channel = 0, KnobMode mode = KnobMode::kAbsolute)
{
    MidiBinding b;
    b.type    = Type::kCc;
    b.number  = uint8_t(number);
    b.channel = uint8_t(channel);
    b.mode    = mode;
    return b;
}

int Target(const char* name)
{
    return *champi::MidiTargetFromName(name);
}

// A graph with two controllers and a synth; `connected` are on CHAMPI's MIDI in.
Graph Controllers(std::vector<std::string> connected)
{
    Graph g;
    g.champi_present = true;
    for(const char* name : {"Midi-Bridge:KeyStep 32 (capture)", "Midi-Bridge:Launchpad (capture)", "Synth:midi_in"})
    {
        PeerPort p;
        p.name   = name;
        p.type   = PortType::kMidi;
        p.output = std::string(name).find("capture") != std::string::npos;
        g.ports.push_back(p);
    }
    for(const std::string& c : connected)
        g.connections.insert({champi::kEventsIn, c});
    return g;
}

const std::string kKeyStep   = "Midi-Bridge:KeyStep 32 (capture)";
const std::string kLaunchpad = "Midi-Bridge:Launchpad (capture)";
} // namespace

// ---- Targets and bindings -----------------------------------------------------------------------

TEST(MidiTargets, KnobsComeFirstThenButtonsThenKeys)
{
    using K = MidiTarget::Kind;
    EXPECT_EQ(champi::kNumMidiTargets, 41);
    // Knobs are numbered left to right: ENC4, ENC1, ENC2, ENC3, ENC5, ENC6.
    EXPECT_EQ(champi::MidiTargetAt(0), (MidiTarget{K::kKnobTurn, 4}));
    EXPECT_EQ(champi::MidiTargetAt(1), (MidiTarget{K::kKnobPush, 4}));
    EXPECT_EQ(champi::MidiTargetAt(2), (MidiTarget{K::kKnobTurn, 1}));
    EXPECT_EQ(champi::MidiTargetAt(7), (MidiTarget{K::kKnobPush, 3}));
    EXPECT_EQ(champi::MidiTargetAt(8), (MidiTarget{K::kKnobTurn, 5}));
    EXPECT_EQ(champi::MidiTargetAt(11), (MidiTarget{K::kKnobPush, 6}));
    EXPECT_EQ(champi::MidiTargetAt(12), (MidiTarget{K::kKey, champi::kChompiKey}));
    EXPECT_EQ(champi::MidiTargetAt(14), (MidiTarget{K::kKey, champi::kLoopKey}));
    EXPECT_EQ(champi::MidiTargetAt(15), (MidiTarget{K::kToggle, 0}));
    EXPECT_EQ(champi::MidiTargetAt(16), (MidiTarget{K::kKey, 1}));
    EXPECT_EQ(champi::MidiTargetAt(40), (MidiTarget{K::kKey, 25}));

    for(int i = 0; i < champi::kNumMidiTargets; i++)
        EXPECT_EQ(champi::MidiTargetFromName(champi::MidiTargetName(i)), i) << champi::MidiTargetName(i);
    EXPECT_FALSE(champi::MidiTargetFromName("key_26"));

    EXPECT_EQ(champi::MidiTargetLabel(0), "Knob 1 rotation");
    EXPECT_EQ(champi::MidiTargetName(0), "knob_1");
    EXPECT_EQ(champi::MidiTargetHint(0), "speed");
    EXPECT_EQ(champi::MidiTargetLabel(7), "Knob 4 press");
    EXPECT_EQ(champi::MidiTargetName(7), "knob_4_push");
    EXPECT_EQ(champi::MidiTargetHint(7), "effect");
    EXPECT_EQ(champi::MidiTargetHint(9), "scrub wheel");
    EXPECT_EQ(champi::MidiTargetLabel(12), "CHAMPI button");
    EXPECT_EQ(champi::MidiTargetLabel(13), "Play button");
    EXPECT_EQ(champi::MidiTargetLabel(15), "Toggle switch");
    EXPECT_EQ(champi::MidiTargetLabel(31), "Key 16");
    EXPECT_EQ(champi::MidiTargetHint(31), "black");
}

TEST(MidiBindings, RelativeEncodingsReadBothWays)
{
    EXPECT_EQ(champi::RelativeSteps(KnobMode::kAbsolute, 65), 0);
    EXPECT_EQ(champi::RelativeSteps(KnobMode::kRelativeOffset, 65), 1);
    EXPECT_EQ(champi::RelativeSteps(KnobMode::kRelativeOffset, 61), -3);
    EXPECT_EQ(champi::RelativeSteps(KnobMode::kRelativeOffset, 64), 0);
    EXPECT_EQ(champi::RelativeSteps(KnobMode::kRelativeTwos, 2), 2);
    EXPECT_EQ(champi::RelativeSteps(KnobMode::kRelativeTwos, 127), -1);
    EXPECT_EQ(champi::RelativeSteps(KnobMode::kRelativeTwos, 120), -8);
    EXPECT_EQ(champi::RelativeSteps(KnobMode::kRelativeSigned, 3), 3);
    EXPECT_EQ(champi::RelativeSteps(KnobMode::kRelativeSigned, 65), -1);
}

TEST(MidiBindings, Labels)
{
    EXPECT_EQ(champi::MidiBindingLabel(Note(36, 9), false), "Note 36 (C2)  ch 10");
    EXPECT_EQ(champi::MidiBindingLabel(Note(61), false), "Note 61 (C#4)  ch 1");
    EXPECT_EQ(champi::MidiBindingLabel(Cc(74, 0, KnobMode::kRelativeOffset), true),
              "CC 74  ch 1  relative, 64 is still");
    EXPECT_EQ(champi::MidiBindingLabel(Cc(74), false), "CC 74  ch 1");
    EXPECT_EQ(champi::MidiBindingLabel({}, false), "");
}

// ---- Mapping files ------------------------------------------------------------------------------

TEST(MidiProfile, ParsesAndWritesAMapping)
{
    const MidiProfile p = MidiProfile::Parse("controller = \"Midi-Bridge:KeyStep 32 (capture)\"\n"
                                             "knob_1 = \"cc 21 ch 1 absolute\"\n"
                                             "knob_5 = \"CC 74 ch 2 relative-64\"  # the scrub wheel\n"
                                             "knob_1_push = \"note 36 ch 10\"\n"
                                             "play = 'cc 51 ch 16'\n");
    EXPECT_EQ(p.controller, kKeyStep);
    EXPECT_EQ(p.bindings[Target("knob_1")], Cc(21));
    EXPECT_EQ(p.bindings[Target("knob_5")], Cc(74, 1, KnobMode::kRelativeOffset));
    EXPECT_EQ(p.bindings[Target("knob_1_push")], Note(36, 9));
    EXPECT_EQ(p.bindings[Target("play")], Cc(51, 15));
    EXPECT_FALSE(p.bindings[Target("loop")].Bound());
    EXPECT_EQ(p.Count(), 4);

    const MidiProfile again = MidiProfile::Parse(p.ToToml());
    EXPECT_EQ(again.controller, p.controller);
    EXPECT_EQ(again.bindings, p.bindings);
    EXPECT_NE(p.ToToml().find("knob_5 = \"cc 74 ch 2 relative-64\"\n"), std::string::npos);
}

TEST(MidiProfile, MistakesSayWhere)
{
    auto error = [](const std::string& toml) {
        try
        {
            MidiProfile::Parse(toml, "m.toml");
        }
        catch(const std::runtime_error& e)
        {
            return std::string(e.what());
        }
        return std::string("no error");
    };
    auto starts = [](const std::string& s, const std::string& prefix) { return s.rfind(prefix, 0) == 0; };
    const std::string c = "controller = \"x\"\n";
    EXPECT_EQ(error("knob_1 = \"cc 1 ch 1 absolute\"\n"),
              "m.toml:1: no controller = \"...\" line says which controller it's for");
    EXPECT_EQ(error(c + "knob_9 = \"cc 1 ch 1\"\n"), "m.toml:2: no control is called \"knob_9\"");
    EXPECT_EQ(error(c + "knob_1 = \"note 1 ch 1 absolute\"\n"), "m.toml:2: a knob's rotation takes a CC");
    // A rotation needs its mode, and only a rotation takes one.
    EXPECT_TRUE(starts(error(c + "knob_1 = \"cc 1 ch 1\"\n"), "m.toml:2: \"cc 1 ch 1\" isn't a mapping"));
    EXPECT_TRUE(starts(error(c + "play = \"cc 1 ch 1 absolute\"\n"), "m.toml:2: \"cc 1 ch 1 absolute\" isn't"));
    EXPECT_TRUE(starts(error(c + "play = \"cc 1 ch 17\"\n"), "m.toml:2: \"cc 1 ch 17\" isn't"));
    EXPECT_TRUE(starts(error(c + "play = \"cc 128 ch 1\"\n"), "m.toml:2: \"cc 128 ch 1\" isn't"));
    EXPECT_TRUE(starts(error(c + "play = \"pad 1 ch 1\"\n"), "m.toml:2: \"pad 1 ch 1\" isn't"));
    EXPECT_EQ(error(c + "play = \"cc 1 ch 1\"\nloop = \"cc 1 ch 1\"\n"),
              "m.toml:3: \"cc 1 ch 1\" is already mapped on line 2");
    EXPECT_EQ(error(c + "play = \"cc 1 ch 1\"\nplay = \"cc 2 ch 1\"\n"), "m.toml:3: play is set twice");
    EXPECT_EQ(error(c + "play = 3\n"), "m.toml:2: play takes one value in quotes");
    // The same number as a note and as a CC are different messages.
    EXPECT_EQ(error(c + "play = \"cc 1 ch 1\"\nloop = \"note 1 ch 1\"\n"), "no error");
}

TEST(MidiProfile, BindingAMessageMovesIt)
{
    MidiProfile p;
    p.Bind(Target("play"), Note(36));
    p.Bind(Target("loop"), Note(36));
    EXPECT_FALSE(p.bindings[Target("play")].Bound());
    EXPECT_EQ(p.bindings[Target("loop")], Note(36));
    p.Bind(Target("loop"), {});
    EXPECT_EQ(p.Count(), 0);
}

TEST(MidiMappings, SavesAFilePerControllerAndLoadsThemBack)
{
    TempDir      dir;
    MidiMappings m;
    m.Get(kKeyStep).Bind(Target("play"), Note(36));
    m.Save(kKeyStep, dir.path());
    const std::string similar = "Midi-Bridge:KeyStep 32 capture";
    m.Get(similar).Bind(Target("loop"), Cc(20));
    m.Save(similar, dir.path());
    // Both make the same file name: the second gets a number.
    EXPECT_EQ(m.Find(kKeyStep)->file, dir / "Midi-Bridge_KeyStep_32_capture.toml");
    EXPECT_EQ(m.Find(similar)->file, dir / "Midi-Bridge_KeyStep_32_capture-2.toml");

    // Saving again keeps the file.
    m.Get(kKeyStep).Bind(Target("loop"), Note(37));
    m.Save(kKeyStep, dir.path());
    EXPECT_EQ(m.Find(kKeyStep)->file, dir / "Midi-Bridge_KeyStep_32_capture.toml");

    const MidiMappings loaded = MidiMappings::Load(dir.path());
    EXPECT_EQ(loaded, m);
    EXPECT_EQ(loaded.Find(kKeyStep)->bindings[Target("loop")], Note(37));
    EXPECT_TRUE(MidiMappings::Load(dir / "none").Profiles().empty());

    WriteHostFile(dir / "copy.toml", ReadHostFile(dir / "Midi-Bridge_KeyStep_32_capture-2.toml"));
    EXPECT_THROW(MidiMappings::Load(dir.path()), std::runtime_error);
}

TEST(MidiMappings, AControllerIsFoundByItsNameOrOneThatResolvesToIt)
{
    MidiMappings m;
    // Saved when PipeWire called the bridge "Midi-Bridge-62".
    m.Get("Midi-Bridge-62:KeyStep 32 (capture)").Bind(Target("play"), Note(36));
    const Graph g = Controllers({kKeyStep});
    ASSERT_NE(m.ForPort(g, kKeyStep), nullptr);
    EXPECT_EQ(m.KeyForPort(g, kKeyStep), "Midi-Bridge-62:KeyStep 32 (capture)");
    EXPECT_EQ(m.ForPort(g, kLaunchpad), nullptr);
    EXPECT_EQ(m.KeyForPort(g, kLaunchpad), kLaunchpad);
}

TEST(MidiMappings, OnlyConnectedControllersApplyAndTheFirstWins)
{
    MidiMappings m;
    m.Get(kKeyStep).Bind(Target("play"), Note(36));
    m.Get(kKeyStep).Bind(Target("loop"), Note(37));
    m.Get(kLaunchpad).Bind(Target("play"), Note(40));    // play is the KeyStep's
    m.Get(kLaunchpad).Bind(Target("chompi"), Note(37)); // so is note 37
    m.Get(kLaunchpad).Bind(Target("toggle"), Cc(9));

    EXPECT_EQ(champi::MidiControllers(Controllers({kLaunchpad, kKeyStep})),
              (std::vector<std::string>{kKeyStep, kLaunchpad}));

    const auto none = champi::ActiveMapping(m, Controllers({}));
    EXPECT_TRUE(std::none_of(none.begin(), none.end(), [](const MidiBinding& b) { return b.Bound(); }));

    const auto launchpad = champi::ActiveMapping(m, Controllers({kLaunchpad}));
    EXPECT_EQ(launchpad[Target("play")], Note(40));
    EXPECT_EQ(launchpad[Target("chompi")], Note(37));

    const auto both = champi::ActiveMapping(m, Controllers({kKeyStep, kLaunchpad}));
    EXPECT_EQ(both[Target("play")], Note(36));
    EXPECT_EQ(both[Target("loop")], Note(37));
    EXPECT_FALSE(both[Target("chompi")].Bound());
    EXPECT_EQ(both[Target("toggle")], Cc(9));
}

// ---- Learning -----------------------------------------------------------------------------------

TEST(MidiLearner, APressIsItsFirstNoteOnOrCc)
{
    MidiLearner    press;
    const uint8_t  off[3] = {0x80, 36, 0}, silent[3] = {0x99, 36, 0}, bend[3] = {0xe0, 0, 64};
    const uint8_t  on[3] = {0x99, 36, 100}, cc[3] = {0xb2, 51, 127};
    EXPECT_FALSE(press.Feed(off));
    EXPECT_FALSE(press.Feed(silent)); // a note-on at velocity 0 is a note-off
    EXPECT_FALSE(press.Feed(bend));
    EXPECT_EQ(press.Feed(on), Note(36, 9));
    EXPECT_EQ(MidiLearner().Feed(cc), Cc(51, 2));
}

TEST(MidiLearner, ARotationHearsSeveralCcsOfOneControl)
{
    MidiLearner turn(true);
    const uint8_t note[3] = {0x90, 36, 100}, other[3] = {0xb0, 1, 10};
    EXPECT_FALSE(turn.Feed(note));
    EXPECT_FALSE(turn.Feed(other));
    EXPECT_EQ(turn.Heard(), 1);
    // Another knob starts again.
    std::optional<MidiBinding> learnt;
    for(int v = 40; v < 40 + MidiLearner::kTurnMessages; v++)
    {
        EXPECT_FALSE(learnt);
        const uint8_t m[3] = {0xb0, 74, uint8_t(v)};
        learnt             = turn.Feed(m);
        if(!learnt)
        {
            EXPECT_EQ(turn.Hearing(), Cc(74));
        }
    }
    EXPECT_EQ(learnt, Cc(74, 0, KnobMode::kAbsolute));
}

TEST(MidiLearner, GuessesHowAKnobReads)
{
    EXPECT_EQ(champi::GuessKnobMode({40, 41, 42, 43, 44, 45}), KnobMode::kAbsolute);
    EXPECT_EQ(champi::GuessKnobMode({5, 4, 3, 2, 3, 4}), KnobMode::kAbsolute); // turned back
    EXPECT_EQ(champi::GuessKnobMode({65, 65, 65, 66, 65, 65}), KnobMode::kRelativeOffset);
    EXPECT_EQ(champi::GuessKnobMode({63, 63, 65, 65, 65, 63}), KnobMode::kRelativeOffset);
    EXPECT_EQ(champi::GuessKnobMode({1, 1, 1, 127, 127, 127}), KnobMode::kRelativeTwos);
    EXPECT_EQ(champi::GuessKnobMode({1, 1, 2, 1, 1, 1}), KnobMode::kRelativeTwos);
    EXPECT_EQ(champi::GuessKnobMode({1, 1, 65, 65, 66, 1}), KnobMode::kRelativeSigned);
}

// ---- The menu's table ---------------------------------------------------------------------------

TEST(MidiMappingMenu, LearnsUnmapsAndChangesHowAKnobReads)
{
    MidiMappingMenu menu;
    menu.SetGraph(Controllers({}));
    EXPECT_EQ(menu.Controller(), "");
    menu.Activate();
    EXPECT_FALSE(menu.Learning()); // nothing to learn from

    menu.SetGraph(Controllers({kKeyStep}));
    EXPECT_EQ(menu.Controller(), kKeyStep);
    menu.Move(1); // knob 1 press
    menu.Activate();
    EXPECT_TRUE(menu.Learning());
    const uint8_t on[3] = {0x99, 36, 100};
    EXPECT_TRUE(menu.Feed(on));
    EXPECT_FALSE(menu.Learning());
    EXPECT_EQ(menu.Binding(1), Note(36, 9));
    EXPECT_EQ(menu.TakeChanged(), std::vector<std::string>{kKeyStep});
    EXPECT_TRUE(menu.TakeChanged().empty());

    // A knob's rotation, then how it reads.
    menu.Move(-1);
    menu.Activate();
    for(int i = 0; i < MidiLearner::kTurnMessages; i++)
    {
        const uint8_t m[3] = {0xb0, 74, 65};
        EXPECT_EQ(menu.Feed(m), i == MidiLearner::kTurnMessages - 1);
    }
    EXPECT_EQ(menu.Binding(0), Cc(74, 0, KnobMode::kRelativeOffset));
    menu.ChangeMode(1);
    EXPECT_EQ(menu.Binding(0).mode, KnobMode::kRelativeTwos);
    menu.ChangeMode(-1);
    menu.ChangeMode(-1);
    menu.ChangeMode(-1);
    EXPECT_EQ(menu.Binding(0).mode, KnobMode::kRelativeSigned);

    // Esc stops learning, and leaves what was there.
    menu.Activate();
    EXPECT_TRUE(menu.Back());
    EXPECT_FALSE(menu.Back());
    EXPECT_TRUE(menu.Binding(0).Bound());
    menu.Clear();
    EXPECT_FALSE(menu.Binding(0).Bound());
    EXPECT_EQ(menu.Mappings().Find(kKeyStep)->Count(), 1);
    EXPECT_EQ(menu.TakeChanged(), std::vector<std::string>{kKeyStep});
}

TEST(MidiMappingMenu, TabPicksTheControllerAndEachKeepsItsOwn)
{
    MidiMappingMenu menu;
    MidiMappings    saved;
    saved.Get("Midi-Bridge-62:Launchpad (capture)").Bind(Target("play"), Note(40));
    menu.SetMappings(saved);
    menu.SetGraph(Controllers({kKeyStep, kLaunchpad}));
    EXPECT_EQ(menu.Controllers().size(), 2u);
    EXPECT_FALSE(menu.Binding(Target("play")).Bound());
    menu.SwitchController();
    EXPECT_EQ(menu.Controller(), kLaunchpad);
    EXPECT_EQ(menu.Binding(Target("play")), Note(40));

    // Learnt into the mapping that resolves to it, not a new one.
    menu.Move(Target("loop"));
    menu.Activate();
    const uint8_t on[3] = {0x90, 41, 100};
    menu.Feed(on);
    EXPECT_EQ(menu.TakeChanged(), std::vector<std::string>{"Midi-Bridge-62:Launchpad (capture)"});
    EXPECT_EQ(menu.Mappings().Profiles().size(), 1u);

    // The graph changing keeps it on the same controller; unplugging it stops learning.
    menu.SetGraph(Controllers({kKeyStep, kLaunchpad}));
    EXPECT_EQ(menu.Controller(), kLaunchpad);
    menu.Activate();
    menu.SetGraph(Controllers({kKeyStep}));
    EXPECT_FALSE(menu.Learning());
    EXPECT_EQ(menu.Controller(), kKeyStep);
}

TEST(MidiMappingMenu, TheTableScrollsWithTheSelection)
{
    MidiMappingMenu menu;
    menu.SetGraph(Controllers({kKeyStep}));
    const int lines = MidiMappingMenu::VisibleLines();
    ASSERT_LT(lines, champi::kNumMidiTargets);
    menu.Move(lines);
    EXPECT_EQ(menu.FirstVisible(), 1);
    menu.Page(1);
    menu.Page(1);
    menu.Page(1);
    menu.Page(1);
    EXPECT_EQ(menu.Selected(), champi::kNumMidiTargets - 1);
    EXPECT_EQ(menu.FirstVisible(), champi::kNumMidiTargets - lines);
    menu.ScrollBy(-100);
    EXPECT_EQ(menu.FirstVisible(), 0);

    // A click learns the line it's on.
    const champi::layout::Rect r = menu.LineRect(2);
    menu.Click(r.x + 5, r.y + r.h / 2);
    EXPECT_EQ(menu.Selected(), 2);
    EXPECT_TRUE(menu.Learning());
    menu.Reset();
    EXPECT_FALSE(menu.Learning());
    EXPECT_EQ(menu.Selected(), 0);
}

// ---- Playing ------------------------------------------------------------------------------------

class MidiMapperPanel : public ::testing::Test
{
  protected:
    static void SetUpTestSuite()
    {
        daisycola::UseManualClock(true); // queued detents stay queued
        panel_.Attach();
    }
    static void TearDownTestSuite() { daisycola::UseManualClock(false); }

    void SetUp() override { map_ = std::make_unique<MidiMapper>(); }

    void TearDown() override
    {
        for(int k = 1; k <= champi::kNumKeys; k++)
            EXPECT_FALSE(panel_.KeyPressed(k)) << "KEY" << k << " left held";
        for(int e = 1; e <= champi::kNumEncoders; e++)
        {
            EXPECT_FALSE(panel_.EncoderPushed(e)) << "ENC" << e << " left pushed";
            panel_.TurnEncoder(e, -panel_.PendingDetents(e));
        }
        panel_.SetToggle(true);
    }

    MidiMapper::Result Send(uint8_t status, uint8_t data1, uint8_t data2)
    {
        const uint8_t m[3] = {status, data1, data2};
        return map_->Process(m, 3, panel_, out_);
    }

    void Map(const char* target, const MidiBinding& b)
    {
        mapping_[Target(target)] = b;
        map_->SetMapping(mapping_);
    }

    static champi::PanelState   panel_;
    std::unique_ptr<MidiMapper> map_;
    champi::MidiMapping         mapping_{};
    uint8_t                     out_[3] = {};
};

champi::PanelState MidiMapperPanel::panel_;

TEST_F(MidiMapperPanel, UnmappedMessagesGoToTheFirmware)
{
    using R = MidiMapper::Result;
    EXPECT_EQ(Send(0x90, 36, 100), R::kPass);
    EXPECT_EQ(Send(0x80, 36, 0), R::kPass);
    EXPECT_EQ(Send(0xb0, 21, 64), R::kPass);
    const uint8_t clock[1] = {0xf8}, program[2] = {0xc0, 3};
    EXPECT_EQ(map_->Process(clock, 1, panel_, out_), R::kPass);
    EXPECT_EQ(map_->Process(program, 2, panel_, out_), R::kPass);
    // A mapped note on another channel is another message.
    Map("play", Note(36, 9));
    EXPECT_EQ(Send(0x90, 36, 100), R::kPass);
    EXPECT_EQ(Send(0x80, 36, 0), R::kPass);
}

TEST_F(MidiMapperPanel, NotesAndCcsHoldKeysAndPushes)
{
    using R = MidiMapper::Result;
    Map("play", Note(36, 9));
    Map("key_16", Cc(30));
    Map("knob_3_push", Note(37, 9)); // ENC2

    EXPECT_EQ(Send(0x99, 36, 100), R::kConsumed);
    EXPECT_TRUE(panel_.KeyPressed(champi::kPlayKey));
    EXPECT_EQ(Send(0x99, 36, 0), R::kConsumed); // velocity 0 lets go
    EXPECT_FALSE(panel_.KeyPressed(champi::kPlayKey));

    EXPECT_EQ(Send(0xb0, 30, 64), R::kConsumed);
    EXPECT_TRUE(panel_.KeyPressed(16));
    Send(0xb0, 30, 100); // still held
    EXPECT_TRUE(panel_.KeyPressed(16));
    Send(0xb0, 30, 63);
    EXPECT_FALSE(panel_.KeyPressed(16));

    Send(0x99, 37, 90);
    EXPECT_TRUE(panel_.EncoderPushed(2));
    Send(0x89, 37, 0);
    EXPECT_FALSE(panel_.EncoderPushed(2));
}

TEST_F(MidiMapperPanel, TheToggleFlipsOnEachPress)
{
    Map("toggle", Cc(9));
    ASSERT_TRUE(panel_.Toggle());
    Send(0xb0, 9, 127);
    EXPECT_FALSE(panel_.Toggle());
    Send(0xb0, 9, 120); // no new press
    Send(0xb0, 9, 0);
    EXPECT_FALSE(panel_.Toggle());
    Send(0xb0, 9, 127);
    EXPECT_TRUE(panel_.Toggle());
}

TEST_F(MidiMapperPanel, AnAbsoluteKnobBecomesTapesOwnCc)
{
    Map("knob_1", Cc(74, 3)); // the leftmost, ENC4: speed
    map_->SetFirmwareChannel(2);
    ASSERT_EQ(Send(0xb3, 74, 99), MidiMapper::Result::kFirmware);
    EXPECT_EQ(out_[0], 0xb2);
    EXPECT_EQ(out_[1], 20); // ENC4 is TAPE's first knob too
    EXPECT_EQ(out_[2], 99);
    EXPECT_EQ(MidiMapper::FirmwareCc(1), 21);
    EXPECT_EQ(MidiMapper::FirmwareCc(5), 24);
    EXPECT_EQ(MidiMapper::FirmwareCc(6), 25);
}

TEST_F(MidiMapperPanel, AnEndlessEncoderTurnsItsKnob)
{
    Map("knob_2", Cc(75, 0, KnobMode::kRelativeTwos)); // ENC1
    EXPECT_EQ(Send(0xb0, 75, 1), MidiMapper::Result::kConsumed);
    EXPECT_EQ(panel_.PendingDetents(1), 1);
    EXPECT_EQ(map_->Turned(1), 1);
    Send(0xb0, 75, 127);
    EXPECT_EQ(panel_.PendingDetents(1), 0);
    // A big step is capped, as the mouse's are.
    Send(0xb0, 75, 50);
    EXPECT_EQ(panel_.PendingDetents(1), champi::kMaxPendingDetents);
}

TEST_F(MidiMapperPanel, LearningKeepsMessagesForTheMenu)
{
    using R = MidiMapper::Result;
    Map("play", Note(36));
    Send(0x90, 36, 100);
    ASSERT_TRUE(panel_.KeyPressed(champi::kPlayKey));

    map_->SetLearning(true);
    EXPECT_EQ(Send(0xb0, 74, 10), R::kConsumed);
    EXPECT_EQ(Send(0x90, 50, 10), R::kConsumed);
    // A release still lets go.
    EXPECT_EQ(Send(0x80, 36, 0), R::kConsumed);
    EXPECT_FALSE(panel_.KeyPressed(champi::kPlayKey));
    const uint8_t clock[1] = {0xf8};
    EXPECT_EQ(map_->Process(clock, 1, panel_, out_), R::kPass);

    uint8_t m[3];
    ASSERT_TRUE(map_->PopHeard(m));
    EXPECT_EQ(m[0], 0xb0);
    EXPECT_EQ(m[1], 74);
    EXPECT_EQ(m[2], 10);
    ASSERT_TRUE(map_->PopHeard(m));
    EXPECT_EQ(m[1], 50);
    ASSERT_TRUE(map_->PopHeard(m));
    EXPECT_EQ(m[0], 0x80);
    EXPECT_FALSE(map_->PopHeard(m));

    // Learning again starts with nothing left over.
    Send(0x90, 51, 10);
    map_->SetLearning(false);
    EXPECT_EQ(Send(0x90, 52, 10), R::kPass);
    map_->SetLearning(true);
    EXPECT_FALSE(map_->PopHeard(m));
    map_->SetLearning(false);
}
