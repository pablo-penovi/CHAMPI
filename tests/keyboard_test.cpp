// The computer keyboard on the panel: the default keymap, keymap.toml, and every key played into a
// real PanelState on daisycola's manual clock. Its own program, since a PanelState attaches once per
// process.
#include <linux/input-event-codes.h>

#include <memory>
#include <stdexcept>
#include <gtest/gtest.h>

#include "daisycola/host.h"
#include "keyboard.h"
#include "panel_layout.h"
#include "panel_state.h"

using champi::Action;
using champi::KeyboardControl;
using champi::Keymap;
using champi::Scancode;
using Kind = Action::Kind;

namespace layout = champi::layout;

namespace
{
Action Key(int k)
{
    return {Kind::kKey, k};
}

Action Select(int e)
{
    return {Kind::kSelectEncoder, e};
}

std::string ApplyError(const std::string& toml)
{
    Keymap m = Keymap::Defaults();
    try
    {
        m.Apply(toml, "keymap.toml");
    }
    catch(const std::runtime_error& e)
    {
        return e.what();
    }
    return "";
}
} // namespace

TEST(KeyNames, NameAndScancodeRoundTrip)
{
    EXPECT_EQ(champi::ScancodeFromName("Z"), KEY_Z);
    EXPECT_EQ(champi::ScancodeFromName("z"), KEY_Z);
    EXPECT_EQ(champi::ScancodeFromName("leftbracket"), KEY_LEFTBRACE);
    EXPECT_EQ(champi::ScancodeFromName("F12"), KEY_F12);
    EXPECT_EQ(champi::ScancodeFromName("KP5"), KEY_KP5);
    EXPECT_FALSE(champi::ScancodeFromName("Hyper"));
    EXPECT_FALSE(champi::ScancodeFromName(""));
    for(Scancode code : {KEY_A, KEY_GRAVE, KEY_SPACE, KEY_ENTER, KEY_LEFT, KEY_KPENTER})
        EXPECT_EQ(champi::ScancodeFromName(champi::ScancodeName(code)), code) << code;
    EXPECT_EQ(champi::ScancodeName(KEY_PROG1), std::to_string(KEY_PROG1));
}

TEST(KeyNames, ActionsRoundTrip)
{
    EXPECT_EQ(champi::ActionName(Key(1)), "key_1");
    EXPECT_EQ(champi::ActionName(Key(champi::kChompiKey)), "chompi");
    EXPECT_EQ(champi::ActionName(Key(champi::kPlayKey)), "play");
    EXPECT_EQ(champi::ActionName(Key(champi::kLoopKey)), "loop");
    EXPECT_EQ(champi::ActionName(Select(4)), "encoder_4");
    EXPECT_EQ(champi::ActionFromName("key_25"), Key(25));
    EXPECT_EQ(champi::ActionFromName("line_in"), (Action{Kind::kLineIn, 0}));
    EXPECT_FALSE(champi::ActionFromName("key_26")); // that's "chompi"
    EXPECT_FALSE(champi::ActionFromName("encoder_7"));
}

TEST(DefaultKeymap, TrackerStyleTwoOctaves)
{
    const Keymap m = Keymap::Defaults();
    const char*  white = "ZXCVBNMQWERTYUI";
    for(int k = 1; k <= 15; k++)
        EXPECT_EQ(m.Lookup(*champi::ScancodeFromName(std::string(1, white[k - 1]))), Key(k)) << "KEY" << k;

    // Each black key sits between the two white keys whose computer keys flank its own: S between
    // Z and X, 2 between Q and W, and so on.
    const char* black          = "SDGHJ23567";
    const int   over_gap[10]   = {1, 2, 4, 5, 6, 8, 9, 11, 12, 13};
    for(int i = 0; i < 10; i++)
    {
        const int key = 16 + i;
        EXPECT_EQ(m.Lookup(*champi::ScancodeFromName(std::string(1, black[i]))), Key(key)) << "KEY" << key;
        // The board agrees: KEYn sits over the gap after white key over_gap[i].
        EXPECT_GT(layout::kKey[key - 1].x, layout::kKey[over_gap[i] - 1].x);
        EXPECT_LT(layout::kKey[key - 1].x, layout::kKey[over_gap[i]].x);
    }

    EXPECT_EQ(m.Lookup(KEY_TAB), Key(champi::kChompiKey));
    EXPECT_EQ(m.Lookup(KEY_SPACE), Key(champi::kPlayKey));
    EXPECT_EQ(m.Lookup(KEY_ENTER), Key(champi::kLoopKey));
    EXPECT_EQ(m.Lookup(KEY_GRAVE), (Action{Kind::kToggle, 0}));
    EXPECT_EQ(m.Lookup(KEY_F12), (Action{Kind::kLineIn, 0}));
    EXPECT_EQ(m.Lookup(KEY_BACKSLASH), (Action{Kind::kPush, 0}));
    EXPECT_EQ(m.KeysFor({Kind::kTurnLeft, 0}), (std::vector<Scancode>{KEY_LEFTBRACE, KEY_LEFT}));
    EXPECT_EQ(m.KeysFor({Kind::kTurnRight, 0}), (std::vector<Scancode>{KEY_RIGHTBRACE, KEY_RIGHT}));
    EXPECT_EQ(m.Lookup(KEY_A), Action{});
}

TEST(DefaultKeymap, FunctionKeysPickTheEncodersLeftToRight)
{
    const Keymap   m      = Keymap::Defaults();
    const Scancode f[6]   = {KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6};
    float          last_x = -1;
    for(int i = 0; i < 6; i++)
    {
        const Action a = m.Lookup(f[i]);
        ASSERT_EQ(a.kind, Kind::kSelectEncoder) << "F" << i + 1;
        EXPECT_GT(layout::kEncoder[a.index - 1].x, last_x) << "F" << i + 1;
        last_x = layout::kEncoder[a.index - 1].x;
    }
    EXPECT_EQ(m.Lookup(KEY_F1), Select(4)); // the speed knob
    EXPECT_EQ(m.Lookup(KEY_F5), Select(5)); // the scrub wheel
}

TEST(DefaultKeymap, EveryActionHasAKey)
{
    const Keymap m = Keymap::Defaults();
    for(int k = 1; k <= champi::kNumKeys; k++)
        EXPECT_FALSE(m.KeysFor(Key(k)).empty()) << "KEY" << k;
    for(int e = 1; e <= champi::kNumEncoders; e++)
        EXPECT_FALSE(m.KeysFor(Select(e)).empty()) << "ENC" << e;
    for(Kind kind : {Kind::kTurnLeft, Kind::kTurnRight, Kind::kPush, Kind::kToggle, Kind::kLineIn})
        EXPECT_FALSE(m.KeysFor({kind, 0}).empty()) << champi::ActionName({kind, 0});
}

TEST(KeymapToml, OverridesOnlyWhatItNames)
{
    Keymap m = Keymap::Defaults();
    m.Apply(R"(
        # Play on the keypad, and no line-in key.
        play = "KP0"        # was Space
        turn_left = ["Minus", 13, 'Comma',
                     "Down"]
        line_in = []
    )");
    EXPECT_EQ(m.KeysFor(Key(champi::kPlayKey)), std::vector<Scancode>{KEY_KP0});
    EXPECT_EQ(m.Lookup(KEY_SPACE), Action{});
    EXPECT_EQ(m.KeysFor({Kind::kTurnLeft, 0}), (std::vector<Scancode>{KEY_MINUS, KEY_EQUAL, KEY_COMMA, KEY_DOWN}));
    EXPECT_EQ(m.Lookup(KEY_LEFT), Action{});
    EXPECT_TRUE(m.KeysFor({Kind::kLineIn, 0}).empty());
    EXPECT_EQ(m.Lookup(KEY_Z), Key(1)); // untouched
}

TEST(KeymapToml, AKeyLeavesWhatItDidBefore)
{
    Keymap m = Keymap::Defaults();
    m.Apply("chompi = \"Z\"\nkey_1 = \"A\"\n");
    EXPECT_EQ(m.Lookup(KEY_Z), Key(champi::kChompiKey));
    EXPECT_EQ(m.Lookup(KEY_A), Key(1));
    EXPECT_EQ(m.Lookup(KEY_TAB), Action{});
}

TEST(KeymapToml, MistakesNameTheLineAndChangeNothing)
{
    EXPECT_EQ(ApplyError("play = \"Space\"\nplya = \"Tab\""), "keymap.toml:2: no action is called \"plya\"");
    EXPECT_EQ(ApplyError("play = \"Spacebar\""), "keymap.toml:1: no key is called \"Spacebar\"");
    EXPECT_EQ(ApplyError("play = \"Z\"\n\nkey_2 = [\"A\", \"Z\"]"), "keymap.toml:3: Z is already set on line 1");
    EXPECT_EQ(ApplyError("play = \"Z\"\nplay = \"A\""), "keymap.toml:2: play is set twice");
    EXPECT_EQ(ApplyError("[keys]\nplay = \"Z\""), "keymap.toml:1: tables aren't used in a keymap; write `action = \"Key\"` lines");
    EXPECT_EQ(ApplyError("play = Space"), "keymap.toml:1: expected a key name in quotes, a scancode, or a [list] of them");
    EXPECT_EQ(ApplyError("play \"Z\""), "keymap.toml:1: expected '='");
    EXPECT_EQ(ApplyError("play = \"Z\" \"X\""), "keymap.toml:1: expected the end of the line");
    EXPECT_EQ(ApplyError("play = [\"Z\""), "keymap.toml:1: expected ']'");
    EXPECT_EQ(ApplyError("play = 99999"), "keymap.toml:1: scancodes go up to " + std::to_string(KEY_MAX));

    Keymap m = Keymap::Defaults();
    EXPECT_THROW(m.Apply("play = \"Z\"\nloop = \"Nope\""), std::runtime_error);
    EXPECT_EQ(m.Lookup(KEY_Z), Key(1)); // the first line didn't stick
    EXPECT_EQ(m.Lookup(KEY_SPACE), Key(champi::kPlayKey));
}

TEST(KeymapToml, WrittenOutItReadsBackTheSame)
{
    Keymap m = Keymap::Defaults();
    m.Apply("play = [\"Space\", \"KP0\"]\nline_in = []\nloop = 191\n");
    const std::string toml = m.ToToml();
    EXPECT_NE(toml.find("play = [\"Space\", \"KP0\"]\n"), std::string::npos) << toml;
    EXPECT_NE(toml.find("line_in = []\n"), std::string::npos) << toml;
    EXPECT_NE(toml.find("loop = 191\n"), std::string::npos) << toml;

    // Applying it to an empty keymap gives the same keymap back.
    Keymap back;
    back.Apply(toml);
    EXPECT_EQ(back.ToToml(), toml);
    for(Scancode code = 1; code < 256; code++)
        EXPECT_EQ(back.Lookup(code), m.Lookup(code)) << code;
}

class PanelKeyboard : public ::testing::Test
{
  protected:
    using Clock = KeyboardControl::Clock;

    static void SetUpTestSuite()
    {
        daisycola::UseManualClock(true); // queued detents stay queued
        panel_.Attach();
    }
    static void TearDownTestSuite() { daisycola::UseManualClock(false); }

    void SetUp() override { keys_ = std::make_unique<KeyboardControl>(panel_, Keymap::Defaults()); }

    void TearDown() override
    {
        keys_->ReleaseAll();
        for(int k = 1; k <= champi::kNumKeys; k++)
            EXPECT_FALSE(panel_.KeyPressed(k)) << "KEY" << k << " left held";
        for(int e = 1; e <= champi::kNumEncoders; e++)
        {
            EXPECT_FALSE(panel_.EncoderPushed(e)) << "ENC" << e << " left pushed";
            panel_.TurnEncoder(e, -panel_.PendingDetents(e));
        }
        panel_.SetToggle(true);
        panel_.SetLineIn(false);
    }

    Clock::time_point At(int ms) const { return t0_ + std::chrono::milliseconds(ms); }

    static champi::PanelState        panel_;
    std::unique_ptr<KeyboardControl> keys_;
    const Clock::time_point          t0_ = Clock::now();
};

champi::PanelState PanelKeyboard::panel_;

TEST_F(PanelKeyboard, EveryPanelKeyPlaysWhileHeld)
{
    const Keymap m = Keymap::Defaults();
    for(int k = 1; k <= champi::kNumKeys; k++)
    {
        const Scancode code = m.KeysFor(Key(k)).at(0);
        EXPECT_TRUE(keys_->Press(code, At(0)));
        EXPECT_TRUE(panel_.KeyPressed(k)) << "KEY" << k;
        EXPECT_TRUE(keys_->Press(code, At(500))); // a repeat changes nothing
        EXPECT_TRUE(keys_->Release(code));
        EXPECT_FALSE(panel_.KeyPressed(k)) << "KEY" << k;
    }
}

TEST_F(PanelKeyboard, ChordsAndTwoKeysOnOnePanelKey)
{
    keys_->Press(KEY_Z, At(0));
    keys_->Press(KEY_C, At(0));
    keys_->Press(KEY_B, At(0));
    EXPECT_TRUE(panel_.KeyPressed(1) && panel_.KeyPressed(3) && panel_.KeyPressed(5));
    keys_->Release(KEY_C);
    EXPECT_FALSE(panel_.KeyPressed(3));
    EXPECT_TRUE(panel_.KeyPressed(1) && panel_.KeyPressed(5));
    keys_->Release(KEY_Z);
    keys_->Release(KEY_B);

    Keymap m = Keymap::Defaults();
    m.Apply("key_1 = [\"Z\", \"A\"]");
    KeyboardControl two(panel_, m);
    two.Press(KEY_Z, At(0));
    two.Press(KEY_A, At(10));
    two.Release(KEY_Z);
    EXPECT_TRUE(panel_.KeyPressed(1)); // A still holds it
    two.Release(KEY_A);
    EXPECT_FALSE(panel_.KeyPressed(1));
}

TEST_F(PanelKeyboard, UnmappedKeysAreIgnored)
{
    EXPECT_FALSE(keys_->Press(KEY_A, At(0)));
    EXPECT_FALSE(keys_->Release(KEY_A));
    EXPECT_FALSE(keys_->Release(KEY_Z)); // not held
    EXPECT_FALSE(keys_->Used());
}

TEST_F(PanelKeyboard, ArrowsTurnTheSelectedEncoder)
{
    EXPECT_EQ(keys_->Selected(), 4);
    keys_->Press(KEY_RIGHT, At(0));
    keys_->Release(KEY_RIGHT);
    keys_->Press(KEY_RIGHTBRACE, At(10));
    keys_->Release(KEY_RIGHTBRACE);
    EXPECT_EQ(panel_.PendingDetents(4), 2);

    keys_->Press(KEY_F6, At(20));
    keys_->Release(KEY_F6);
    EXPECT_EQ(keys_->Selected(), 6);
    keys_->Press(KEY_LEFT, At(30));
    keys_->Release(KEY_LEFT);
    EXPECT_EQ(panel_.PendingDetents(6), -1);
    EXPECT_EQ(panel_.PendingDetents(4), 2);
    EXPECT_EQ(keys_->Turned(4), 2);
    EXPECT_EQ(keys_->Turned(6), -1);
    EXPECT_TRUE(keys_->Used());
}

TEST_F(PanelKeyboard, HoldingATurnKeyRepeats)
{
    keys_->Press(KEY_F5, At(0));
    keys_->Press(KEY_RIGHT, At(0));
    EXPECT_EQ(panel_.PendingDetents(5), 1);
    const auto delay = KeyboardControl::kRepeatDelay, every = KeyboardControl::kRepeatEvery;
    keys_->Tick(At(0) + delay - std::chrono::milliseconds(1));
    EXPECT_EQ(panel_.PendingDetents(5), 1);
    keys_->Tick(At(0) + delay);
    EXPECT_EQ(panel_.PendingDetents(5), 2);
    keys_->Tick(At(0) + delay + every);
    keys_->Tick(At(0) + delay + 2 * every);
    EXPECT_EQ(panel_.PendingDetents(5), 4);

    // A late tick turns once, then the repeat carries on from there.
    keys_->Tick(At(0) + delay + 10 * every);
    EXPECT_EQ(panel_.PendingDetents(5), 5);

    keys_->Release(KEY_RIGHT);
    keys_->Tick(At(10000));
    EXPECT_EQ(panel_.PendingDetents(5), 5);
}

TEST_F(PanelKeyboard, PushHoldsTheEncoderItPushed)
{
    keys_->Press(KEY_F2, At(0)); // ENC1
    keys_->Press(KEY_BACKSLASH, At(10));
    EXPECT_TRUE(panel_.EncoderPushed(1));
    // Turning while pushed, as the shift menus want.
    keys_->Press(KEY_RIGHT, At(20));
    EXPECT_EQ(panel_.PendingDetents(1), 1);
    keys_->Release(KEY_RIGHT);
    EXPECT_TRUE(panel_.EncoderPushed(1));

    // Selecting another encoder doesn't move the push.
    keys_->Press(KEY_F3, At(30));
    EXPECT_TRUE(panel_.EncoderPushed(1));
    EXPECT_FALSE(panel_.EncoderPushed(2));
    keys_->Release(KEY_BACKSLASH);
    EXPECT_FALSE(panel_.EncoderPushed(1));
}

TEST_F(PanelKeyboard, ToggleAndLineInFlipOnAPress)
{
    ASSERT_TRUE(panel_.Toggle());
    keys_->Press(KEY_GRAVE, At(0));
    EXPECT_FALSE(panel_.Toggle());
    keys_->Press(KEY_GRAVE, At(100)); // still held: a repeat
    EXPECT_FALSE(panel_.Toggle());
    keys_->Release(KEY_GRAVE);
    keys_->Press(KEY_GRAVE, At(200));
    EXPECT_TRUE(panel_.Toggle());
    keys_->Release(KEY_GRAVE);

    ASSERT_FALSE(panel_.LineIn());
    keys_->Press(KEY_F12, At(300));
    EXPECT_TRUE(panel_.LineIn());
    keys_->Release(KEY_F12);
    keys_->Press(KEY_F12, At(400));
    EXPECT_FALSE(panel_.LineIn());
}

TEST_F(PanelKeyboard, LosingFocusLetsGoOfEverything)
{
    keys_->Press(KEY_TAB, At(0));
    keys_->Press(KEY_Q, At(0));
    keys_->Press(KEY_F1, At(0));
    keys_->Press(KEY_BACKSLASH, At(0));
    keys_->Press(KEY_LEFT, At(0));
    EXPECT_TRUE(panel_.KeyPressed(champi::kChompiKey));
    EXPECT_TRUE(panel_.EncoderPushed(4));
    keys_->ReleaseAll();
    EXPECT_FALSE(panel_.KeyPressed(champi::kChompiKey));
    EXPECT_FALSE(panel_.KeyPressed(8));
    EXPECT_FALSE(panel_.EncoderPushed(4));
    keys_->Tick(At(10000)); // the turn key doesn't repeat either
    EXPECT_EQ(panel_.PendingDetents(4), -1);
}
