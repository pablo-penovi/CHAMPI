// A placeholder panel until chunk 6: plain shapes for the keys and encoders, the LEDs in their
// colours, and the audio load. The keys, encoders, toggle and line-in jack already work by mouse:
// click a key, scroll over an encoder to turn it and click it to push.
#include <algorithm>
#include <chrono>
#include <cstdio>

#include "DistrhoUI.hpp"
#include "app.h"
#include "champi_plugin.h"
#include "led_frame.h"
#include "runtime.h"

START_NAMESPACE_DISTRHO

namespace
{
constexpr float kWidth  = DISTRHO_UI_DEFAULT_WIDTH;
constexpr float kHeight = DISTRHO_UI_DEFAULT_HEIGHT;

struct Box
{
    float x, y, w, h;
    bool  Contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// Where everything sits, in a kWidth x kHeight panel.
struct Layout
{
    Box   keys[champi::kNumKeys];
    float enc_x[champi::kNumEncoders], enc_y, enc_r[champi::kNumEncoders];
    Box   toggle, line_in;

    Layout()
    {
        const float key_w = 50, gap = 6, left = 20;
        for(int i = 0; i < 15; i++) // white row, KEY1-15
            keys[i] = {left + i * (key_w + gap), 250, key_w, 50};
        for(int i = 0; i < 10; i++) // black row, KEY16-25, over the gaps of the white row
            keys[15 + i] = {left + (i + 0.5f) * (key_w + gap) + 2 * (key_w + gap), 185, key_w, 50};
        keys[champi::kChompiKey - 1] = {870, 120, 70, 50};
        keys[champi::kPlayKey - 1]   = {870, 185, 70, 50};
        keys[champi::kLoopKey - 1]   = {870, 250, 70, 50};

        enc_y = 85;
        for(int i = 0; i < champi::kNumEncoders; i++)
        {
            enc_x[i] = 70 + i * 120;
            enc_r[i] = i == 4 ? 42 : 28; // ENC5 is the big scrub wheel
        }
        toggle  = {780, 40, 60, 30};
        line_in = {780, 100, 60, 30};
    }

    int EncoderAt(float x, float y) const
    {
        for(int i = 0; i < champi::kNumEncoders; i++)
        {
            const float dx = x - enc_x[i], dy = y - enc_y;
            if(dx * dx + dy * dy <= enc_r[i] * enc_r[i])
                return i + 1;
        }
        return 0;
    }

    int KeyAt(float x, float y) const
    {
        for(int i = 0; i < champi::kNumKeys; i++)
            if(keys[i].Contains(x, y))
                return i + 1;
        return 0;
    }
};

// The LEDs get a quarter (keys) or an eleventh (PTH) of what the firmware asks for; scale back up
// so they're visible. Chunk 6 does this properly, with gamma.
Color LedColor(const daisycola::Rgb& c, int scale)
{
    auto up = [scale](uint8_t v) { return std::min(255, v * scale); };
    return Color(up(c.r), up(c.g), up(c.b));
}
} // namespace

class ChampiUI : public UI
{
  public:
    ChampiUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        loadSharedResources();
        setGeometryConstraints(DISTRHO_UI_DEFAULT_WIDTH / 2, DISTRHO_UI_DEFAULT_HEIGHT / 2, true);
    }

  protected:
    void parameterChanged(uint32_t, float) override {}

    void uiIdle() override
    {
        champi::ReadLedFrame(leds_);

        // Load figures twice a second, so the peak covers half a second.
        const auto now = std::chrono::steady_clock::now();
        if(now - last_load_ >= std::chrono::milliseconds(500))
        {
            last_load_ = now;
            load_      = Plugin().Load();
        }
        repaint();
    }

    void onNanoDisplay() override
    {
        champi::PanelState& panel = champi::Runtime::Get().Panel();

        // Tiling window managers ignore the aspect ratio, so fit the panel and centre it.
        beginPath();
        rect(0, 0, getWidth(), getHeight());
        fillColor(Color(16, 16, 18));
        fill();
        const View v = Fit();
        translate(v.x, v.y);
        scale(v.scale, v.scale);

        fontFace(NANOVG_DEJAVU_SANS_TTF);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);

        for(int k = 1; k <= champi::kNumKeys; k++)
        {
            const Box& b       = layout_.keys[k - 1];
            const bool white   = k <= 15;
            const bool pressed = panel.KeyPressed(k);
            Color      body    = white ? Color(225, 220, 205) : Color(60, 60, 66);
            if(k == champi::kChompiKey)
                body = Color(230, 120, 190);
            else if(k == champi::kPlayKey)
                body = Color(90, 210, 220);
            else if(k == champi::kLoopKey)
                body = Color(235, 210, 80);
            if(pressed)
                body = Color(body.red * 0.6f, body.green * 0.6f, body.blue * 0.6f);

            beginPath();
            roundedRect(b.x, b.y, b.w, b.h, 5);
            fillColor(body);
            fill();

            // KEY26-28 have PTH LEDs, the rest SMT ones.
            beginPath();
            roundedRect(b.x + 6, b.y + 6, b.w - 12, 10, 3);
            fillColor(LedColor(leds_.key[k - 1], k >= champi::kChompiKey ? 11 : 4));
            fill();

            fontSize(12);
            fillColor(white || k >= champi::kChompiKey ? Color(30, 30, 30) : Color(200, 200, 200));
            char label[8];
            std::snprintf(label, sizeof label, "%d", k);
            text(b.x + b.w / 2, b.y + b.h - 14, label, nullptr);
        }

        for(int e = 1; e <= champi::kNumEncoders; e++)
        {
            const float x = layout_.enc_x[e - 1], y = layout_.enc_y, r = layout_.enc_r[e - 1];
            beginPath();
            circle(x, y, r + 5);
            fillColor(LedColor(leds_.encoder[e - 1], 11));
            fill();
            beginPath();
            circle(x, y, r);
            fillColor(e == 5 ? Color(120, 70, 170) : Color(200, 170, 80));
            fill();
            beginPath();
            circle(x, y, r * 0.6f);
            fillColor(panel.EncoderPushed(e) ? Color(150, 150, 150) : Color(240, 240, 240));
            fill();
            if(e == 5)
            {
                beginPath();
                circle(x + r + 12, y - r, 6);
                fillColor(LedColor(leds_.encoder5_second, 11));
                fill();
            }
            fontSize(12);
            fillColor(Color(200, 200, 200));
            char label[8];
            std::snprintf(label, sizeof label, "ENC%d", e);
            text(x, y + r + 16, label, nullptr);
        }

        Switch(layout_.toggle, panel.Toggle() ? "toggle on" : "toggle off", panel.Toggle());
        Switch(layout_.line_in, panel.LineIn() ? "line in" : "mic", panel.LineIn());

        // Status, along the bottom.
        char status[200];
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fontSize(13);
        fillColor(Color(170, 170, 170));
        std::snprintf(status, sizeof status,
                      "%s    %.0f Hz / %u%s    load %.0f%% (peak %.0f%%)    xruns %llu    late %llu    "
                      "dropouts %llu",
                      champi::Runtime::Get().Booted() ? "running" : "booting", getSampleRate(),
                      Plugin().getBufferSize(), Plugin().Resampling() ? " resampled" : "",
                      load_.average * 100, load_.peak * 100, (unsigned long long)champi::g_xruns.load(),
                      (unsigned long long)load_.late_blocks, (unsigned long long)load_.dropouts);
        text(20, 328, status, nullptr);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if(ev.button != 1)
            return false;
        champi::PanelState& panel = champi::Runtime::Get().Panel();
        float               x, y;
        Fit().ToPanel(ev.pos, x, y);

        if(!ev.press)
        {
            if(held_key_)
                panel.SetKey(held_key_, false);
            if(held_encoder_)
                panel.SetEncoderPushed(held_encoder_, false);
            held_key_ = held_encoder_ = 0;
            return true;
        }
        if(const int k = layout_.KeyAt(x, y))
        {
            panel.SetKey(held_key_ = k, true);
            return true;
        }
        if(const int e = layout_.EncoderAt(x, y))
        {
            panel.SetEncoderPushed(held_encoder_ = e, true);
            return true;
        }
        if(layout_.toggle.Contains(x, y))
            panel.SetToggle(!panel.Toggle());
        else if(layout_.line_in.Contains(x, y))
            panel.SetLineIn(!panel.LineIn());
        else
            return false;
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        float x, y;
        Fit().ToPanel(ev.pos, x, y);
        const int e = layout_.EncoderAt(x, y);
        if(!e || ev.delta.getY() == 0)
            return false;
        champi::Runtime::Get().Panel().TurnEncoder(e, ev.delta.getY() > 0 ? 1 : -1);
        return true;
    }

  private:
    // Where the panel sits in the window.
    struct View
    {
        float x, y, scale;
        void  ToPanel(const Point<double>& pos, float& px, float& py) const
        {
            px = (float(pos.getX()) - x) / scale;
            py = (float(pos.getY()) - y) / scale;
        }
    };

    View Fit() const
    {
        const float s = std::min(getWidth() / kWidth, getHeight() / kHeight);
        return {(getWidth() - kWidth * s) / 2, (getHeight() - kHeight * s) / 2, s};
    }

    ChampiPlugin& Plugin() const { return *static_cast<ChampiPlugin*>(getPluginInstancePointer()); }

    void Switch(const Box& b, const char* label, bool on)
    {
        beginPath();
        roundedRect(b.x, b.y, b.w, b.h, 4);
        fillColor(on ? Color(90, 90, 100) : Color(40, 40, 46));
        fill();
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fontSize(11);
        fillColor(Color(220, 220, 220));
        text(b.x + b.w / 2, b.y + b.h / 2, label, nullptr);
    }

    const Layout                          layout_;
    champi::LedFrame                      leds_{};
    champi::AudioLoad                     load_{};
    std::chrono::steady_clock::time_point last_load_{};
    int                                   held_key_     = 0;
    int                                   held_encoder_ = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChampiUI)
};

UI* createUI()
{
    return new ChampiUI();
}

END_NAMESPACE_DISTRHO
