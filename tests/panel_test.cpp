// The on-screen panel without the drawing: the layout taken from the board files, what the mouse
// hits, how the LEDs look, and the mouse played into a real PanelState on daisycola's manual clock.
// Its own program, since a PanelState attaches once per process.
#include <cmath>
#include <memory>
#include <gtest/gtest.h>

#include "daisycola/host.h"
#include "panel.h"
#include "panel_layout.h"
#include "panel_state.h"

namespace layout = champi::layout;
using champi::Hit;
using champi::HitTest;
using champi::MouseControl;
using Kind = Hit::Kind;

namespace
{
constexpr float kKeyPitch = 20.111f; // the main board's key spacing

Hit KeyHit(int k)
{
    return {Kind::kKey, k};
}

Hit EncoderHit(int e)
{
    return {Kind::kEncoder, e};
}
} // namespace

TEST(PanelLayout, WhiteAndBlackRowsAreEvenlySpaced)
{
    for(int k = 2; k <= 15; k++)
    {
        EXPECT_FLOAT_EQ(layout::kKey[k - 1].y, layout::kKey[0].y) << "KEY" << k;
        EXPECT_NEAR(layout::kKey[k - 1].x - layout::kKey[k - 2].x, kKeyPitch, 0.002f) << "KEY" << k;
    }
    // The black row sits a key above, half a key along, in groups of 2, 3, 2 and 3.
    const int black_over_gap[10] = {1, 2, 4, 5, 6, 8, 9, 11, 12, 13}; // between white key n and n+1
    for(int i = 0; i < 10; i++)
    {
        const layout::Point& black = layout::kKey[15 + i];
        const layout::Point& white = layout::kKey[black_over_gap[i] - 1];
        EXPECT_NEAR(black.x, white.x + kKeyPitch / 2, 0.01f) << "KEY" << 16 + i;
        EXPECT_NEAR(white.y - black.y, 20, 0.01f) << "KEY" << 16 + i;
    }
}

TEST(PanelLayout, ControlsAreWhereThePanelHasThem)
{
    // The CHOMPI key is left of every encoder, play and loop right of the scrub wheel.
    for(int e = 1; e <= champi::kNumEncoders; e++)
        EXPECT_LT(layout::kKey[champi::kChompiKey - 1].x, layout::kEncoder[e - 1].x);
    EXPECT_GT(layout::kKey[champi::kPlayKey - 1].x, layout::kEncoder[4].x);
    EXPECT_GT(layout::kKey[champi::kLoopKey - 1].x, layout::kKey[champi::kPlayKey - 1].x);

    // Left to right: ENC4 (speed), ENC1, ENC2, ENC3, the scrub wheel, ENC6 (volume).
    const int order[] = {4, 1, 2, 3, 5, 6};
    for(int i = 1; i < 6; i++)
        EXPECT_LT(layout::kEncoder[order[i - 1] - 1].x, layout::kEncoder[order[i] - 1].x);
    EXPECT_GT(layout::kEncoder[4].d, 30); // the scrub wheel's hole

    // Each LED window is straight above its encoder, but ENC5's two flank the scrub wheel.
    for(int e : {1, 2, 3, 4, 6})
    {
        EXPECT_NEAR(layout::kEncoderLedWindow[e - 1].x, layout::kEncoder[e - 1].x, 0.05f) << "ENC" << e;
        EXPECT_LT(layout::kEncoderLedWindow[e - 1].y, layout::kEncoder[e - 1].y) << "ENC" << e;
    }
    EXPECT_LT(layout::kEncoderLedWindow[4].x, layout::kEncoder[4].x);
    EXPECT_GT(layout::kEncoder5SecondLedWindow.x, layout::kEncoder[4].x);

    // The key LEDs shine through the top of the caps.
    for(int k = 1; k < champi::kChompiKey; k++)
    {
        EXPECT_FLOAT_EQ(layout::kKeyLed[k - 1].x, layout::kKey[k - 1].x) << "KEY" << k;
        EXPECT_NEAR(layout::kKey[k - 1].y - layout::kKeyLed[k - 1].y, 5.05f, 0.01f) << "KEY" << k;
    }
}

TEST(PanelHitTest, EveryControlIsUnderItsOwnCentre)
{
    for(int k = 1; k <= champi::kNumKeys; k++)
        EXPECT_EQ(HitTest(layout::kKey[k - 1].x, layout::kKey[k - 1].y), KeyHit(k)) << "KEY" << k;
    for(int e = 1; e <= champi::kNumEncoders; e++)
        EXPECT_EQ(HitTest(layout::kEncoder[e - 1].x, layout::kEncoder[e - 1].y), EncoderHit(e)) << "ENC" << e;
    const layout::Rect& t = layout::kToggleSlot;
    EXPECT_EQ(HitTest(t.x + t.w / 2, t.y + t.h / 2).kind, Kind::kToggle);
    EXPECT_EQ(HitTest(layout::kLineInJack.x, layout::kLineInJack.y).kind, Kind::kLineIn);
}

TEST(PanelHitTest, EdgesAndGaps)
{
    const layout::Point& k1 = layout::kKey[0];
    EXPECT_EQ(HitTest(k1.x + champi::kKeyCap / 2 - 0.1f, k1.y), KeyHit(1));
    EXPECT_EQ(HitTest(k1.x + kKeyPitch / 2, k1.y).kind, Kind::kNone); // between KEY1 and KEY2
    const layout::Circle& e1 = layout::kEncoder[0];
    EXPECT_EQ(HitTest(e1.x, e1.y - champi::kKnobRing / 2 + 0.1f), EncoderHit(1));
    EXPECT_EQ(HitTest(e1.x, e1.y - champi::kKnobRing / 2 - 0.5f).kind, Kind::kNone);
    EXPECT_EQ(HitTest(layout::kEncoder[4].x + 15, layout::kEncoder[4].y), EncoderHit(5));
    EXPECT_EQ(HitTest(0.5f, 0.5f).kind, Kind::kNone);
    EXPECT_EQ(HitTest(layout::kPhonesJack.x, layout::kPhonesJack.y).kind, Kind::kNone);
}

TEST(PanelLeds, UndoTheFirmwareScalingAndGammaEncode)
{
    using champi::LedLight;
    EXPECT_EQ(LedLight({0, 0, 0}, champi::kSmtDivisor).Brightness(), 0);

    // The brightest each chain gets: 255 / 4 and 255 / 11.
    EXPECT_NEAR(LedLight({63, 0, 0}, champi::kSmtDivisor).r, 1, 0.01f);
    EXPECT_NEAR(LedLight({0, 23, 0}, champi::kPthDivisor).g, 1, 0.01f);

    // A quarter of full light, gamma-encoded.
    EXPECT_NEAR(LedLight({0, 0, 16}, champi::kSmtDivisor).b, std::pow(64 / 255.0f, 1 / 2.2f), 1e-4f);

    // Channels stay separate, and nothing goes past 1.
    const champi::Light l = LedLight({255, 8, 0}, champi::kPthDivisor);
    EXPECT_EQ(l.r, 1);
    EXPECT_GT(l.g, 0);
    EXPECT_EQ(l.b, 0);
}

class PanelMouse : public ::testing::Test
{
  protected:
    using Clock = MouseControl::Clock;

    static void SetUpTestSuite()
    {
        daisycola::UseManualClock(true); // queued detents stay queued
        panel_.Attach();
    }
    static void TearDownTestSuite() { daisycola::UseManualClock(false); }

    void SetUp() override { mouse_ = std::make_unique<MouseControl>(panel_); }

    void TearDown() override
    {
        // Leave the panel at rest for the next test.
        for(int e = 1; e <= champi::kNumEncoders; e++)
        {
            panel_.SetEncoderPushed(e, false);
            panel_.TurnEncoder(e, -panel_.PendingDetents(e));
        }
        panel_.SetToggle(true);
        panel_.SetLineIn(false);
    }

    Clock::time_point At(int ms) const { return t0_ + std::chrono::milliseconds(ms); }

    static champi::PanelState     panel_;
    std::unique_ptr<MouseControl> mouse_;
    const Clock::time_point       t0_ = Clock::now();
};

champi::PanelState PanelMouse::panel_;

TEST_F(PanelMouse, KeysPlayWhileHeld)
{
    for(int k = 1; k <= champi::kNumKeys; k++)
    {
        const layout::Point& c = layout::kKey[k - 1];
        ASSERT_TRUE(mouse_->Press(c.x, c.y, At(0)));
        EXPECT_TRUE(panel_.KeyPressed(k)) << "KEY" << k;
        mouse_->Move(c.x + 30, c.y - 30, At(10)); // sliding off doesn't let go
        EXPECT_TRUE(panel_.KeyPressed(k)) << "KEY" << k;
        mouse_->Release(At(20));
        EXPECT_FALSE(panel_.KeyPressed(k)) << "KEY" << k;
    }
}

TEST_F(PanelMouse, DraggingUpTurnsClockwise)
{
    const layout::Circle& e = layout::kEncoder[3];
    mouse_->Press(e.x, e.y, At(0));
    mouse_->Move(e.x, e.y - 0.5f, At(10)); // not a drag yet
    EXPECT_EQ(panel_.PendingDetents(4), 0);
    mouse_->Move(e.x, e.y - 5 * MouseControl::kMmPerDetent, At(20));
    EXPECT_EQ(panel_.PendingDetents(4), 5);
    mouse_->Move(e.x, e.y - 2 * MouseControl::kMmPerDetent, At(30));
    EXPECT_EQ(panel_.PendingDetents(4), 2);
    mouse_->Move(e.x + 40, e.y + 3 * MouseControl::kMmPerDetent, At(1000)); // anywhere on screen
    EXPECT_EQ(panel_.PendingDetents(4), -3);
    mouse_->Release(At(1010));

    EXPECT_EQ(mouse_->Turned(4), -3);
    EXPECT_FALSE(panel_.EncoderPushed(4)); // a drag never pushes
    mouse_->Tick(At(2000));
    EXPECT_FALSE(panel_.EncoderPushed(4));
}

TEST_F(PanelMouse, AClickPushesBriefly)
{
    for(int n = 1; n <= champi::kNumEncoders; n++)
    {
        const layout::Circle& e = layout::kEncoder[n - 1];
        mouse_->Press(e.x, e.y, At(0));
        EXPECT_FALSE(panel_.EncoderPushed(n)) << "ENC" << n; // could still become a drag
        mouse_->Release(At(50));
        EXPECT_TRUE(panel_.EncoderPushed(n)) << "ENC" << n;
        mouse_->Tick(At(50) + MouseControl::kClickPush - std::chrono::milliseconds(1));
        EXPECT_TRUE(panel_.EncoderPushed(n)) << "ENC" << n;
        mouse_->Tick(At(50) + MouseControl::kClickPush);
        EXPECT_FALSE(panel_.EncoderPushed(n)) << "ENC" << n;
    }
}

TEST_F(PanelMouse, HoldingStillKeepsItPushed)
{
    const layout::Circle& e = layout::kEncoder[5];
    mouse_->Press(e.x, e.y, At(0));
    mouse_->Tick(At(0) + MouseControl::kHoldToPush - std::chrono::milliseconds(1));
    EXPECT_FALSE(panel_.EncoderPushed(6));
    mouse_->Tick(At(0) + MouseControl::kHoldToPush);
    EXPECT_TRUE(panel_.EncoderPushed(6));
    mouse_->Tick(At(5000));
    EXPECT_TRUE(panel_.EncoderPushed(6));

    // Dragging now turns it while it stays pushed.
    mouse_->Move(e.x, e.y - 3 * MouseControl::kMmPerDetent, At(5100));
    EXPECT_EQ(panel_.PendingDetents(6), 3);
    EXPECT_TRUE(panel_.EncoderPushed(6));

    mouse_->Release(At(6000));
    EXPECT_FALSE(panel_.EncoderPushed(6));
}

TEST_F(PanelMouse, APressEndsTheLastClick)
{
    const layout::Circle& a = layout::kEncoder[0];
    const layout::Circle& b = layout::kEncoder[1];
    mouse_->Press(a.x, a.y, At(0));
    mouse_->Release(At(10));
    EXPECT_TRUE(panel_.EncoderPushed(1));
    mouse_->Press(b.x, b.y, At(20));
    EXPECT_FALSE(panel_.EncoderPushed(1));
    mouse_->Release(At(30));
    EXPECT_TRUE(panel_.EncoderPushed(2));
}

TEST_F(PanelMouse, TheWheelTurnsWhatItsOver)
{
    const layout::Circle& e = layout::kEncoder[4];
    EXPECT_TRUE(mouse_->Scroll(e.x + 10, e.y, 1));
    EXPECT_TRUE(mouse_->Scroll(e.x, e.y, 1));
    EXPECT_EQ(panel_.PendingDetents(5), 2);
    EXPECT_TRUE(mouse_->Scroll(e.x, e.y, -3));
    EXPECT_EQ(panel_.PendingDetents(5), -1);

    // Smooth scrolling: fractions add up to a detent.
    mouse_->Scroll(e.x, e.y, 0.4f);
    mouse_->Scroll(e.x, e.y, 0.4f);
    EXPECT_EQ(panel_.PendingDetents(5), -1);
    mouse_->Scroll(e.x, e.y, 0.4f);
    EXPECT_EQ(panel_.PendingDetents(5), 0);
    EXPECT_EQ(mouse_->Turned(5), 0);

    EXPECT_FALSE(mouse_->Scroll(layout::kKey[0].x, layout::kKey[0].y, 1));
}

TEST_F(PanelMouse, TheToggleAndTheJackFlipOnAClick)
{
    const layout::Rect& t = layout::kToggleSlot;
    ASSERT_TRUE(panel_.Toggle());
    mouse_->Press(t.x + t.w / 2, t.y, At(0));
    mouse_->Release(At(10));
    EXPECT_FALSE(panel_.Toggle());
    mouse_->Press(t.x + t.w / 2, t.y + t.h, At(20));
    EXPECT_TRUE(panel_.Toggle());
    mouse_->Release(At(30));

    ASSERT_FALSE(panel_.LineIn());
    mouse_->Press(layout::kLineInJack.x, layout::kLineInJack.y, At(40));
    EXPECT_TRUE(panel_.LineIn());
    mouse_->Release(At(50));
    mouse_->Press(layout::kLineInJack.x, layout::kLineInJack.y, At(60));
    EXPECT_FALSE(panel_.LineIn());
}

TEST_F(PanelMouse, ClicksOnNothingAreIgnored)
{
    EXPECT_FALSE(mouse_->Press(0.5f, 0.5f, At(0)));
    mouse_->Move(0.5f, -40, At(10));
    mouse_->Release(At(20));
    mouse_->Tick(At(5000));
    for(int e = 1; e <= champi::kNumEncoders; e++)
    {
        EXPECT_EQ(panel_.PendingDetents(e), 0);
        EXPECT_FALSE(panel_.EncoderPushed(e));
    }
}
