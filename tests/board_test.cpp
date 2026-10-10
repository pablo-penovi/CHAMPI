// The CHOMPI board model, read back through TAPE's own code: chompi::Hardware's Init,
// ProcessAllControls, shift registers, encoders and battery checks, and TAPE's LED driver. All
// single-threaded on daisycola's manual clock.
#include <array>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "daisycola/host.h"
#include "hardware.h" // defines TAPE's LED globals: include in this file only
#include "led_frame.h"
#include "mp2722.h"
#include "panel_state.h"

using champi::LedFrame;
using champi::Mp2722;
using champi::PanelState;
using SwId = chompi::Hardware::SwId;

namespace
{
// A global, so it starts zeroed as on the device, where TAPE's Hardware is a global too.
chompi::Hardware hw;
PanelState       panel;
Mp2722           charger;

// KEYn's SwId, by the firmware's own names.
#define KEY(n) SwId::KEY_##n
const SwId kKeySwIds[champi::kNumKeys] = {
    KEY(1),  KEY(2),  KEY(3),  KEY(4),  KEY(5),  KEY(6),  KEY(7),  KEY(8),  KEY(9),  KEY(10),
    KEY(11), KEY(12), KEY(13), KEY(14), KEY(15), KEY(16), KEY(17), KEY(18), KEY(19), KEY(20),
    KEY(21), KEY(22), KEY(23), KEY(24), KEY(25), KEY(26), KEY(27), KEY(28),
};
#undef KEY

// ENCn's push on the button chain; ENC5's is on a pin and read through its ChompiEncoder.
const SwId kPushSwIds[champi::kNumEncoders] = {
    SwId::ENC_1_SW, SwId::ENC_2_SW, SwId::ENC_3_SW, SwId::ENC_4_SW, SwId::SR_LAST, SwId::ENC_6_SW,
};

// TAPE's led_map from NormalPage.h, copied as it is: the LED index for each SwId. Keys 1-25 are
// on the SMT chain; KEY26-28 and the encoder pushes name PTH LEDs.
const uint8_t kLedMap[40] = {
    2, 3, 4, 1, 0, 0, 0x00, 0, 23, 22, 21, 20, 1, 2, 3, 24, 19, 18, 17, 16,
    15, 4, 5, 6, 14, 13, 12, 11, 10, 7, 8, 9, 9, 7, 8, 0x00, 0x00, 0x00, 0x00, 0x00,
};

class Board : public ::testing::Test
{
  protected:
    static void SetUpTestSuite()
    {
        daisycola::UseManualClock(true);
        panel.Attach();
        charger.Attach();
        hw.Init();
        Poll(50); // the shift registers' debounce starts out "pressed"
    }
    static void TearDownTestSuite() { daisycola::UseManualClock(false); }

    // Runs TAPE's control scan at 2 kHz, as its audio callback does.
    static void Poll(int ms)
    {
        for(int i = 0; i < 2 * ms; i++)
        {
            hw.ProcessAllControls();
            daisycola::AdvanceClock(500);
        }
    }

    // The SwIds the firmware sees held down, the toggle aside.
    static std::vector<int> Held()
    {
        std::vector<int> held;
        for(int i = 0; i < int(SwId::SR_LAST); i++)
            if(i != int(SwId::SW_TOG) && hw.button_sr.State(i))
                held.push_back(i);
        return held;
    }

    // One status read the way TAPE does it, waiting for its DMA callback.
    static std::array<uint8_t, 6> ReadStatus()
    {
        hw.MpReadAll();
        daisycola::AdvanceClock(2000);
        EXPECT_TRUE(hw.read_ready);
        std::array<uint8_t, 6> status;
        std::copy(hw.mp_buff_, hw.mp_buff_ + 6, status.begin());
        return status;
    }

    // TAPE debounces the charger over eight reads.
    static void ReadStatusEightTimes()
    {
        for(int i = 0; i < 8; i++)
            ReadStatus();
    }
};

} // namespace

// ---- Panel --------------------------------------------------------------------------------------

TEST_F(Board, NothingIsHeldAtRest)
{
    EXPECT_EQ(Held(), std::vector<int>{});
    EXPECT_FALSE(hw.enc[4].Pressed());
    EXPECT_TRUE(hw.GetToggleState());
    EXPECT_FALSE(hw.jack_detect.Read());
}

TEST_F(Board, EachKeyReachesTheFirmwareAsItsSwId)
{
    for(int key = 1; key <= champi::kNumKeys; key++)
    {
        panel.SetKey(key, true);
        EXPECT_TRUE(panel.KeyPressed(key));
        Poll(20);
        EXPECT_EQ(Held(), std::vector<int>{int(kKeySwIds[key - 1])}) << "KEY" << key;

        panel.SetKey(key, false);
        EXPECT_FALSE(panel.KeyPressed(key));
        Poll(20);
        EXPECT_EQ(Held(), std::vector<int>{}) << "KEY" << key;
    }
}

TEST_F(Board, KeysGiveTheFirmwaresEdges)
{
    // An edge shows for one scan only, so check after each.
    auto scans_until = [](auto edge) {
        for(int i = 0; i < 80; i++)
        {
            hw.ProcessAllControls();
            if(edge())
                return i;
            daisycola::AdvanceClock(500);
        }
        return -1;
    };

    panel.SetKey(champi::kPlayKey, true);
    EXPECT_GE(scans_until([] { return hw.button_sr.RisingEdge(int(SwId::KEY_27)); }), 0);
    panel.SetKey(champi::kPlayKey, false);
    EXPECT_GE(scans_until([] { return hw.button_sr.FallingEdge(int(SwId::KEY_27)); }), 0);
}

TEST_F(Board, ATapBetweenTwoBurstsStillGivesBothEdges)
{
    // Under JACK at 1024 frames, TAPE scans 42 times back to back every 21.3 ms, and its debounce
    // counts one scan a millisecond. A tap that comes and goes between two bursts still lands.
    panel.SetKey(8, true);
    panel.SetKey(8, false);
    EXPECT_FALSE(panel.KeyPressed(8));
    int rises = 0, falls = 0;
    for(int period = 0; period < 40; period++)
    {
        for(int block = 0; block < 42; block++)
        {
            hw.ProcessAllControls();
            rises += hw.button_sr.RisingEdge(int(SwId::KEY_8));
            falls += hw.button_sr.FallingEdge(int(SwId::KEY_8));
            daisycola::AdvanceClock(20);
        }
        daisycola::AdvanceClock(21333 - 42 * 20);
    }
    EXPECT_EQ(rises, 1);
    EXPECT_EQ(falls, 1);
    EXPECT_EQ(Held(), std::vector<int>{});
}

TEST_F(Board, EncoderTurnsReachTheRightEncoder)
{
    for(int n = 1; n <= champi::kNumEncoders; n++)
    {
        for(int detents : {3, -2})
        {
            int before[6];
            std::copy(hw.enc_trackers, hw.enc_trackers + 6, before);
            panel.TurnEncoder(n, detents);
            EXPECT_EQ(panel.PendingDetents(n), detents);
            Poll(100);
            EXPECT_EQ(panel.PendingDetents(n), 0);
            for(int i = 0; i < 6; i++)
                EXPECT_EQ(hw.enc_trackers[i] - before[i], i == n - 1 ? detents : 0)
                    << "turned ENC" << n << " by " << detents << ", read ENC" << i + 1;
        }
    }
}

TEST_F(Board, EncoderPushes)
{
    for(int n = 1; n <= champi::kNumEncoders; n++)
    {
        panel.SetEncoderPushed(n, true);
        EXPECT_TRUE(panel.EncoderPushed(n));
        Poll(20);
        if(n == 5)
        {
            EXPECT_TRUE(hw.enc[4].Pressed());
            EXPECT_EQ(Held(), std::vector<int>{});
        }
        else
            EXPECT_EQ(Held(), std::vector<int>{int(kPushSwIds[n - 1])}) << "ENC" << n;

        panel.SetEncoderPushed(n, false);
        EXPECT_FALSE(panel.EncoderPushed(n));
        Poll(20);
        EXPECT_FALSE(hw.enc[4].Pressed());
        EXPECT_EQ(Held(), std::vector<int>{}) << "ENC" << n;
    }
}

TEST_F(Board, ToggleSwitch)
{
    // TAPE smooths the toggle over 100 to 200 scans.
    panel.SetToggle(false);
    EXPECT_FALSE(panel.Toggle());
    Poll(150);
    EXPECT_FALSE(hw.GetToggleState());

    panel.SetToggle(true);
    EXPECT_TRUE(panel.Toggle());
    Poll(150);
    EXPECT_TRUE(hw.GetToggleState());
}

TEST_F(Board, LineInJack)
{
    panel.SetLineIn(true);
    EXPECT_TRUE(panel.LineIn());
    EXPECT_TRUE(hw.jack_detect.Read());
    panel.SetLineIn(false);
    EXPECT_FALSE(panel.LineIn());
    EXPECT_FALSE(hw.jack_detect.Read());
}

TEST_F(Board, BadNumbersThrow)
{
    EXPECT_THROW(panel.SetKey(0, true), std::out_of_range);
    EXPECT_THROW(panel.SetKey(29, true), std::out_of_range);
    EXPECT_THROW(panel.TurnEncoder(7, 1), std::out_of_range);
    EXPECT_THROW(panel.SetEncoderPushed(0, true), std::out_of_range);
}

// ---- LEDs ---------------------------------------------------------------------------------------

// TAPE's LED driver sends a distinct colour to every LED; each lands on the key or encoder the
// firmware's led_map says it lights.
TEST_F(Board, LedsLandOnTheirKeysAndEncoders)
{
    LedFrame frame;
    EXPECT_FALSE(champi::ReadLedFrame(frame)) << "nothing sent yet";
    EXPECT_EQ(frame.smt_sequence, 0u);

    chompi::LedSetup();
    for(int i = 0; i < 25; i++)
        chompi::SetSmtLed(i, uint8_t(4 * i), 8, uint8_t(4 * (24 - i))); // stored / 4: (i, 2, 24 - i)
    for(int i = 0; i < 10; i++)
        chompi::SetPthLed(i, uint8_t(11 * i), 11, uint8_t(11 * (9 - i))); // stored / 11: (i, 1, 9 - i)
    chompi::fill_led_data();
    daisycola::AdvanceClock(10000);

    ASSERT_TRUE(champi::ReadLedFrame(frame));
    EXPECT_GT(frame.smt_sequence, 0u);
    EXPECT_GT(frame.pth_sequence, 0u);

    auto expect_smt = [](const daisycola::Rgb& led, int i, const char* what) {
        EXPECT_EQ(led.r, i) << what;
        EXPECT_EQ(led.g, 2) << what;
        EXPECT_EQ(led.b, 24 - i) << what;
    };
    auto expect_pth = [](const daisycola::Rgb& led, int i, const char* what) {
        EXPECT_EQ(led.r, i) << what;
        EXPECT_EQ(led.g, 1) << what;
        EXPECT_EQ(led.b, 9 - i) << what;
    };

    for(int key = 1; key <= 25; key++)
        expect_smt(frame.key[key - 1], kLedMap[int(kKeySwIds[key - 1])], ("KEY" + std::to_string(key)).c_str());
    for(int key = 26; key <= 28; key++)
        expect_pth(frame.key[key - 1], kLedMap[int(kKeySwIds[key - 1])], ("KEY" + std::to_string(key)).c_str());
    for(int n : {1, 2, 3, 4, 6})
        expect_pth(frame.encoder[n - 1], kLedMap[int(kPushSwIds[n - 1])], ("ENC" + std::to_string(n)).c_str());
    // ENC5 has two LEDs, which TestPage.h lights as PTH 5 and 6.
    expect_pth(frame.encoder[4], 5, "ENC5");
    expect_pth(frame.encoder5_second, 6, "ENC5 second");
}

// ---- MP2722 -------------------------------------------------------------------------------------

TEST_F(Board, ChargerDefaultsKeepTheFirmwareRunning)
{
    charger.SetUsbPower(true);
    charger.SetBatteryMillivolts(4100);
    charger.SetChargeDone(true);

    // TAPE runs this on every main-loop pass; it must come straight back.
    for(int i = 0; i < 8; i++)
    {
        hw.LowBatteryLockoutCheck();
        daisycola::AdvanceClock(2000);
    }
    EXPECT_EQ(hw.vin_gd_bounce, 0xff);
    EXPECT_EQ(hw.batt_low_bounce, 0x00);
    EXPECT_EQ(hw.legacy_cable_bounce, 0x00);
    EXPECT_EQ(hw.iindpm_stat_bounce, 0x00);
    EXPECT_EQ(hw.batt_level, chompi::Hardware::FULL);
}

TEST_F(Board, ChargerAnswersTapesStatusRead)
{
    charger.SetUsbPower(true);
    charger.SetBatteryMillivolts(3900);
    charger.SetChargeDone(false);
    std::array<uint8_t, 6> s = ReadStatus();
    EXPECT_EQ(s[1] >> 6 & 1, 1) << "VIN_GD";
    EXPECT_EQ(s[1] >> 5 & 1, 1) << "VIN_RDY";
    EXPECT_EQ(s[1] >> 4 & 1, 0) << "LEGACY_CABLE";
    EXPECT_EQ(s[3], 0) << "no faults";
    EXPECT_EQ(s[0] & 1, 0) << "IINDPM_STAT";
    EXPECT_NE(s[2] >> 5, 0b101) << "CHG_STAT";
    EXPECT_EQ(s[5] >> 4 & 1, 0) << "BATT_LOW_STAT";

    charger.SetChargeDone(true);
    charger.SetUsbPower(false);
    s = ReadStatus();
    EXPECT_EQ(s[1] >> 6 & 1, 0) << "VIN_GD";
    EXPECT_EQ(s[1] >> 5 & 1, 0) << "VIN_RDY";
    EXPECT_EQ(s[2] >> 5, 0b101) << "CHG_STAT";
}

// TAPE tells medium from high by moving the BATT_LOW threshold to 3.3 V and reading again.
TEST_F(Board, ChargerFollowsTapesBattLowThreshold)
{
    charger.SetUsbPower(true);
    charger.SetChargeDone(false);
    const uint8_t k3v3 = 0b01011101, k3v0 = 0b01010001; // TAPE's two settings
    struct Case
    {
        uint32_t mv;
        bool     low_at_3v3, low_at_3v0;
    };
    for(Case c : {Case{3900, false, false}, Case{3150, true, false}, Case{2900, true, true}})
    {
        charger.SetBatteryMillivolts(c.mv);
        hw.MpWrite(0x0c, k3v3);
        EXPECT_EQ(charger.BattLowMillivolts(), 3300u);
        EXPECT_EQ(ReadStatus()[5] >> 4 & 1, c.low_at_3v3) << c.mv << " mV";
        hw.MpWrite(0x0c, k3v0);
        EXPECT_EQ(charger.BattLowMillivolts(), 3000u);
        EXPECT_EQ(ReadStatus()[5] >> 4 & 1, c.low_at_3v0) << c.mv << " mV";
    }
}

// TAPE's own three-step check (BMCMediumBattCheck), 30 ms apart.
// Step 2 starts a DMA read and at once writes the 3.0 V threshold; the write waits for the read.
TEST_F(Board, MediumAndHighBattery)
{
    charger.SetUsbPower(true);
    charger.SetChargeDone(false);
    for(auto [mv, level] : {std::pair{3150u, chompi::Hardware::MEDIUM}, std::pair{3900u, chompi::Hardware::HIGH}})
    {
        charger.SetBatteryMillivolts(mv);
        hw.batt_level       = chompi::Hardware::HIGH;
        hw.batt_check_state = 0;
        daisycola::AdvanceClock(31'000'000);
        for(int step = 0; step < 3; step++)
        {
            hw.BMCMediumBattCheck();
            daisycola::AdvanceClock(31'000);
        }
        EXPECT_EQ(hw.batt_check_state, 0);
        EXPECT_EQ(hw.batt_level, level) << mv << " mV";
    }
}

TEST_F(Board, FlatBatteryOnlyLocksOutWithoutUsb)
{
    charger.SetChargeDone(false);
    charger.SetBatteryMillivolts(2900);

    // On USB: TAPE's lockout check returns, low battery or not.
    charger.SetUsbPower(true);
    for(int i = 0; i < 8; i++)
    {
        hw.LowBatteryLockoutCheck();
        daisycola::AdvanceClock(2000);
    }
    EXPECT_EQ(hw.batt_low_bounce, 0xff);
    EXPECT_EQ(hw.vin_gd_bounce, 0xff);

    // Unplugged: the debounced bits now meet TAPE's lockout condition (it would blink the LEDs
    // for 15 s and then ask for shipping mode). Read without running the check, which would spin.
    charger.SetUsbPower(false);
    ReadStatusEightTimes();
    EXPECT_TRUE(hw.batt_low_bounce == 0xff && hw.vin_gd_bounce == 0x00);

    charger.SetUsbPower(true);
    ReadStatusEightTimes();
    EXPECT_EQ(hw.vin_gd_bounce, 0xff);
}

TEST_F(Board, ChargerStoresOtherRegisters)
{
    hw.MpWrite(0x08, 0b10111111); // TAPE's shipping-mode write
    EXPECT_EQ(charger.Register(0x08), 0b10111111);
    hw.MpWrite(0x12, 0x00); // status: read-only
    charger.SetUsbPower(true);
    EXPECT_EQ(charger.Register(0x12), 1 << 6 | 1 << 5);
}
