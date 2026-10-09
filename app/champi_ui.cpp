// The CHOMPI panel, drawn in vectors from the board files' layout (panel_layout.h) and played with
// the mouse (MouseControl) and the computer keyboard (KeyboardControl). The look follows the reference render: black body, cream and gold line
// art, white keys and knobs, the purple scrub wheel and the coloured CHOMPI, play and loop keys.
//
// Drawing is in panel millimetres; the panel is scaled to fit the window, with the status line
// underneath. A skin folder can replace the logo and the glyphs on the CHOMPI, play and loop keys
// with PNGs (see Skin below); none are shipped.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

#include "DistrhoUI.hpp"
#include "app.h"
#include "champi_plugin.h"
#include "keyboard.h"
#include "led_frame.h"
#include "panel.h"
#include "runtime.h"

START_NAMESPACE_DISTRHO

namespace
{
namespace layout = champi::layout;
using champi::Light;

constexpr float kStatusHeight = 6.0f; // mm under the panel
constexpr float kTotalHeight  = layout::kHeight + kStatusHeight;
constexpr float kPi           = 3.14159265f;

// The logo's box: the top-left corner, left of the CHOMPI key's LED window.
constexpr layout::Rect kLogo{5, 6, 32, 14.6f};

// Colours, from the reference render.
const Color kBackground(29, 29, 32);
const Color kBody(12, 12, 13);
const Color kWell(22, 22, 24);
const Color kCream(241, 234, 211);
const Color kGold(199, 163, 92);
const Color kDarkGold(120, 92, 44);
const Color kLedOff(8, 8, 8);
const Color kStatusText(130, 130, 136);

struct KeyColours
{
    Color face, skirt, slot;
};
const KeyColours kWhiteKey{Color(242, 242, 239), Color(178, 178, 174), Color(34, 34, 34)};
const KeyColours kBlackKey{Color(46, 46, 48), Color(22, 22, 23), Color(14, 14, 14)};
const KeyColours kChompiKey{Color(239, 128, 138), Color(186, 84, 98), Color(0, 0, 0)};
const KeyColours kPlayKey{Color(98, 206, 228), Color(52, 150, 176), Color(0, 0, 0)};
const KeyColours kLoopKey{Color(243, 212, 108), Color(190, 158, 58), Color(0, 0, 0)};

const KeyColours& ColoursOf(int key)
{
    switch(key)
    {
        case champi::kChompiKey: return kChompiKey;
        case champi::kPlayKey: return kPlayKey;
        case champi::kLoopKey: return kLoopKey;
        default: return key <= 15 ? kWhiteKey : kBlackKey;
    }
}

Color Darker(const Color& c, float by)
{
    return Color(c.red * by, c.green * by, c.blue * by, c.alpha);
}

Color ToColor(const Light& l, float alpha = 1)
{
    return Color(l.r, l.g, l.b, alpha);
}

// The base colour with the LED's light added.
Color Lit(const Color& base, const Light& l)
{
    return Color(std::min(1.0f, base.red + l.r), std::min(1.0f, base.green + l.g),
                 std::min(1.0f, base.blue + l.b));
}

// User art that replaces CHAMPI's own glyphs: PNGs in the skin folder, each optional.
struct Skin
{
    NanoImage logo, chompi, play, loop;
};
} // namespace

class ChampiUI : public UI
{
  public:
    ChampiUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT),
          mouse_(champi::Runtime::Get().Panel(), champi::Runtime::Get().Charger(), champi::Runtime::Get().Card()),
          keyboard_(champi::Runtime::Get().Panel(), champi::Runtime::Get().Charger(), champi::Runtime::Get().Card(),
                    champi::Options().keymap),
          test_mode_hold_(champi::Options().test_mode)
    {
        loadSharedResources();
        setGeometryConstraints(DISTRHO_UI_DEFAULT_WIDTH / 2, DISTRHO_UI_DEFAULT_HEIGHT / 2, true);
        // X11 repeats a held key as a release and a press, which would retrigger it. KeyboardControl
        // repeats the turn keys itself.
        getWindow().setIgnoringKeyRepeat(true);
    }

  protected:
    void parameterChanged(uint32_t, float) override {}

    void uiIdle() override
    {
        champi::ReadLedFrame(leds_);
        // --test-mode holds ENC6 from power-on (see ChampiPlugin) until the firmware has booted.
        if(test_mode_hold_ && champi::Runtime::Get().Booted())
        {
            test_mode_hold_ = false;
            champi::Runtime::Get().Panel().SetEncoderPushed(6, false);
        }
        mouse_.Tick(std::chrono::steady_clock::now());
        keyboard_.Tick(std::chrono::steady_clock::now());

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
        if(!skin_loaded_)
            LoadSkin();

        beginPath();
        rect(0, 0, getWidth(), getHeight());
        fillColor(kBackground);
        fill();

        // Tiling window managers ignore the aspect ratio, so fit the panel and centre it.
        const View v = Fit();
        translate(v.x, v.y);
        scale(v.scale, v.scale);

        DrawBody();
        DrawLineArt();
        DrawLogo();
        DrawWells();
        for(int k = 1; k <= champi::kNumKeys; k++)
            DrawKey(k);
        DrawKeyNumbers();
        for(int e = 1; e <= champi::kNumEncoders; e++)
            DrawEncoder(e);
        DrawLedWindows();
        DrawToggle();
        DrawMic();
        DrawJacks();
        DrawFrontEdge();
        DrawStatus();
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if(ev.button != 1)
            return false;
        const auto now = std::chrono::steady_clock::now();
        if(!ev.press)
        {
            mouse_.Release(now);
            return true;
        }
        float x, y;
        Fit().ToPanel(ev.pos, x, y);
        return mouse_.Press(x, y, now);
    }

    bool onMotion(const MotionEvent& ev) override
    {
        float x, y;
        Fit().ToPanel(ev.pos, x, y);
        mouse_.Move(x, y, std::chrono::steady_clock::now());
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        float x, y;
        Fit().ToPanel(ev.pos, x, y);
        return mouse_.Scroll(x, y, float(ev.delta.getY()), std::chrono::steady_clock::now());
    }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        // On X11 a keycode is the evdev scancode plus 8: the physical key, whatever the layout.
        const champi::Scancode code = champi::Scancode(ev.keycode) - 8;
        if(ev.press)
            return keyboard_.Press(code, std::chrono::steady_clock::now());
        return keyboard_.Release(code);
    }

    // Keys held when the window loses focus never get their release.
    void uiFocus(bool focus, CrossingMode) override
    {
        if(!focus)
            keyboard_.ReleaseAll();
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
        const float s = std::min(getWidth() / layout::kWidth, getHeight() / kTotalHeight);
        return {(getWidth() - layout::kWidth * s) / 2, (getHeight() - kTotalHeight * s) / 2, s};
    }

    ChampiPlugin& Plugin() const { return *static_cast<ChampiPlugin*>(getPluginInstancePointer()); }

    void LoadSkin()
    {
        skin_loaded_          = true;
        const std::string dir = champi::Options().skin_dir.string();
        auto load = [&](NanoImage& image, const char* name) {
            const std::string path = dir + "/" + name;
            if(FILE* f = std::fopen(path.c_str(), "rb"))
            {
                std::fclose(f);
                image = createImageFromFile(path.c_str(), IMAGE_GENERATE_MIPMAPS);
                if(!image.isValid())
                    std::fprintf(stderr, "champi: can't load skin image %s\n", path.c_str());
            }
        };
        if(dir.empty())
            return;
        load(skin_.logo, "logo.png");
        load(skin_.chompi, "chompi.png");
        load(skin_.play, "play.png");
        load(skin_.loop, "loop.png");
    }

    // Draws `image` as large as fits in the box, centred, keeping its aspect ratio.
    void DrawImage(const NanoImage& image, float x, float y, float w, float h, float alpha = 1)
    {
        const auto  size = image.getSize();
        const float s    = std::min(w / size.getWidth(), h / size.getHeight());
        const float iw = size.getWidth() * s, ih = size.getHeight() * s;
        const float ix = x + (w - iw) / 2, iy = y + (h - ih) / 2;
        beginPath();
        rect(ix, iy, iw, ih);
        fillPaint(imagePattern(ix, iy, iw, ih, 0, image, alpha));
        fill();
    }

    void Circle(float x, float y, float d, const Color& c)
    {
        beginPath();
        circle(x, y, d / 2);
        fillColor(c);
        fill();
    }

    // A soft halo of light round an LED.
    void Glow(float x, float y, float inner, float outer, const Light& l)
    {
        const float a = l.Brightness();
        if(a <= 0.01f)
            return;
        beginPath();
        circle(x, y, outer);
        fillPaint(radialGradient(x, y, inner, outer, ToColor(l, 0.55f * a), ToColor(l, 0)));
        fill();
    }

    void DrawBody()
    {
        beginPath();
        roundedRect(0, 0, layout::kWidth, layout::kHeight, 2);
        fillPaint(linearGradient(0, 0, 0, layout::kHeight, Color(20, 20, 22), kBody));
        fill();
    }

    // Cream and gold outlines grouping the controls, as on the reference.
    void DrawLineArt()
    {
        const layout::Circle& scrub = layout::kEncoder[4];
        const layout::Rect&   pl    = layout::kPlayLoopCutout;
        const float           line  = 0.5f;

        // The scrub wheel and the play and loop keys in one outline: both shapes filled in gold,
        // then in black a line's width smaller.
        auto capsule = [&](float grow, const Color& c) {
            const float r = scrub.d / 2 + 2.6f + grow;
            beginPath();
            circle(scrub.x, scrub.y, r);
            fillColor(c);
            fill();
            beginPath();
            roundedRect(scrub.x, pl.y - 2.2f - grow, pl.x + pl.w + 2.2f - scrub.x + grow, pl.h + 4.4f + 2 * grow,
                        2.5f + grow);
            fillColor(c);
            fill();
        };
        capsule(line, kGold);
        capsule(0, kBody);

        strokeWidth(line);
        strokeColor(kCream);
        auto box = [&](float x0, float x1) {
            beginPath();
            roundedRect(x0, layout::kEncoder[0].y - 12, x1 - x0, 24, 3);
            stroke();
        };
        box(layout::kEncoder[0].x - 12.5f, layout::kEncoder[1].x + 12.5f); // ENC1 and ENC2
        box(layout::kEncoder[2].x - 11.5f, layout::kEncoder[2].x + 11.5f); // ENC3

        // A gold frame round the CHOMPI key.
        const layout::Rect& c = layout::kChompiCutout;
        beginPath();
        roundedRect(c.x - 1.6f, c.y - 1.6f, c.w + 3.2f, c.h + 3.2f, 2.2f);
        strokeWidth(0.9f);
        strokeColor(kGold);
        stroke();
    }

    void DrawLogo()
    {
        if(skin_.logo.isValid())
        {
            DrawImage(skin_.logo, kLogo.x, kLogo.y, kLogo.w, kLogo.h);
            return;
        }
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(7.5f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(kCream);
        text(kLogo.x, kLogo.y + kLogo.h / 2, "CHAMPI", nullptr);
    }

    // The recesses the keys sit in.
    void DrawWells()
    {
        beginPath();
        moveTo(layout::kKeyboardCutout[0].x, layout::kKeyboardCutout[0].y);
        for(const auto& p : layout::kKeyboardCutout)
            lineTo(p.x, p.y);
        closePath();
        for(const layout::Rect& r : {layout::kChompiCutout, layout::kPlayLoopCutout})
            roundedRect(r.x, r.y, r.w, r.h, 0.5f);
        fillColor(kWell);
        fill();
    }

    void DrawKey(int k)
    {
        const KeyColours&   c       = ColoursOf(k);
        const bool          pressed = champi::Runtime::Get().Panel().KeyPressed(k);
        const layout::Rect  cap     = champi::KeyCap(k);
        const float         dy      = pressed ? 0.7f : 0; // the face sinks into the skirt
        const float         fx = cap.x + 1.1f, fy = cap.y + 0.8f + dy, fw = cap.w - 2.2f, fh = cap.h - 3.2f;

        beginPath();
        roundedRect(cap.x, cap.y, cap.w, cap.h, 1.8f);
        fillColor(c.skirt);
        fill();

        const Color face = pressed ? Darker(c.face, 0.86f) : c.face;
        beginPath();
        roundedRect(fx, fy, fw, fh, 1.4f);
        fillPaint(linearGradient(fx, fy, fx, fy + fh, face, Darker(face, 0.93f)));
        fill();

        if(k < champi::kChompiKey)
        {
            // The LED shines through a slot near the top of the cap.
            const layout::Point& p = layout::kKeyLed[k - 1];
            const Light          l = champi::LedLight(leds_.key[k - 1], champi::kSmtDivisor);
            beginPath();
            roundedRect(p.x - 2.3f, p.y - 1 + dy, 4.6f, 2, 1);
            fillColor(Lit(c.slot, l));
            fill();
            Glow(p.x, p.y + dy, 1.2f, 6.5f, l);
            return;
        }

        // The CHOMPI, play and loop keys carry a glyph instead.
        const float gx = cap.x + cap.w / 2, gy = fy + fh / 2;
        const NanoImage& art = k == champi::kChompiKey ? skin_.chompi : k == champi::kPlayKey ? skin_.play : skin_.loop;
        if(art.isValid())
            DrawImage(art, gx - 6.5f, gy - 6.5f, 13, 13);
        else if(k == champi::kChompiKey)
            DrawMushroom(gx, gy);
        else if(k == champi::kPlayKey)
            DrawPlayPause(gx, gy);
        else
            DrawLoopArrow(gx, gy);
    }

    void DrawMushroom(float x, float y)
    {
        const Color ink(60, 30, 36);
        strokeColor(ink);
        strokeWidth(0.7f);
        lineJoin(ROUND);
        beginPath(); // stem
        roundedRect(x - 2.2f, y - 0.2f, 4.4f, 5, 1.6f);
        fillColor(Color(250, 236, 220));
        fill();
        stroke();
        beginPath(); // cap
        moveTo(x - 6, y + 0.4f);
        bezierTo(x - 6, y - 6.5f, x + 6, y - 6.5f, x + 6, y + 0.4f);
        closePath();
        fillColor(Color(250, 236, 220));
        fill();
        stroke();
        for(const auto& s : {std::make_pair(-2.8f, -2.0f), std::make_pair(1.0f, -3.4f), std::make_pair(3.4f, -1.2f)})
            Circle(x + s.first, y + s.second, 1.3f, ink);
        // A smile.
        beginPath();
        arc(x, y + 1.6f, 1, 0.15f * kPi, 0.85f * kPi, CW);
        stroke();
    }

    void DrawPlayPause(float x, float y)
    {
        const Color ink(20, 40, 48);
        beginPath();
        moveTo(x - 5, y - 3.5f);
        lineTo(x + 0.5f, y);
        lineTo(x - 5, y + 3.5f);
        closePath();
        fillColor(ink);
        fill();
        beginPath();
        rect(x + 1.8f, y - 3.5f, 1.3f, 7);
        rect(x + 4.2f, y - 3.5f, 1.3f, 7);
        fill();
    }

    void DrawLoopArrow(float x, float y)
    {
        const Color ink(56, 46, 14);
        beginPath();
        arc(x, y, 3.8f, 0.35f * kPi, 1.9f * kPi, CW);
        strokeColor(ink);
        strokeWidth(1.2f);
        lineCap(ROUND);
        stroke();
        // The arrowhead at the arc's end.
        const float a = 0.35f * kPi, hx = x + 3.8f * std::cos(a), hy = y + 3.8f * std::sin(a);
        beginPath();
        moveTo(hx + 2.4f, hy - 0.6f);
        lineTo(hx - 1.2f, hy + 2.0f);
        lineTo(hx - 0.6f, hy - 2.4f);
        closePath();
        fillColor(ink);
        fill();
        lineCap(BUTT);
    }

    void DrawKeyNumbers()
    {
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(3.4f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(kCream);
        for(int k = 1; k <= 15; k++)
        {
            char label[4];
            std::snprintf(label, sizeof label, "%d", k);
            text(layout::kKey[k - 1].x, 99.6f, label, nullptr);
        }
    }

    void DrawEncoder(int e)
    {
        champi::PanelState&   panel  = champi::Runtime::Get().Panel();
        const layout::Circle  knob   = champi::Knob(e);
        const bool            pushed = panel.EncoderPushed(e);
        const int             turned = mouse_.Turned(e) + keyboard_.Turned(e);
        const float           angle  = -kPi / 2 + 2 * kPi * turned / champi::kDetentsPerTurn;
        const float           x = knob.x, y = knob.y;

        // The encoder the keyboard turns, once the keyboard has been used.
        if(keyboard_.Used() && keyboard_.Selected() == e)
        {
            beginPath();
            circle(x, y, (e == 5 ? layout::kEncoder[4].d : knob.d) / 2 + 1.2f);
            strokeColor(kCream);
            strokeWidth(0.5f);
            stroke();
        }

        if(e == 5)
        {
            // The scrub wheel: a purple disc with a finger dimple, in a gold rim.
            const float r = knob.d / 2 * (pushed ? 0.97f : 1);
            Circle(x, y, layout::kEncoder[4].d, kDarkGold);
            Circle(x, y, layout::kEncoder[4].d - 1, Color(10, 10, 10));
            beginPath();
            circle(x, y, r);
            fillPaint(radialGradient(x - r * 0.3f, y - r * 0.35f, r * 0.1f, r * 1.2f, Color(186, 160, 238),
                                     Color(128, 96, 204)));
            fill();
            const float dr = r * 0.55f, dx = x + dr * std::cos(angle + kPi / 4), dyy = y + dr * std::sin(angle + kPi / 4);
            beginPath();
            circle(dx, dyy, 3.6f);
            fillPaint(radialGradient(dx + 0.8f, dyy + 0.8f, 0.5f, 3.6f, Color(160, 132, 226), Color(118, 88, 192)));
            fill();
            return;
        }

        beginPath(); // the gold ring
        circle(x, y, knob.d / 2);
        fillPaint(radialGradient(x - 2, y - 2.5f, 1, knob.d / 2, Color(232, 200, 128), kDarkGold));
        fill();
        Circle(x, y, champi::kKnob + 1.4f, Color(18, 18, 18));

        const float r = champi::kKnob / 2 * (pushed ? 0.93f : 1);
        const Color top = pushed ? Color(214, 214, 210) : Color(255, 255, 255);
        beginPath();
        circle(x, y, r);
        fillPaint(radialGradient(x - r * 0.3f, y - r * 0.4f, r * 0.2f, r * 1.1f, top, Color(196, 196, 192)));
        fill();
        beginPath(); // the pointer
        moveTo(x + r * 0.2f * std::cos(angle), y + r * 0.2f * std::sin(angle));
        lineTo(x + r * 0.8f * std::cos(angle), y + r * 0.8f * std::sin(angle));
        strokeColor(Color(50, 50, 52));
        strokeWidth(0.8f);
        lineCap(ROUND);
        stroke();
        lineCap(BUTT);
    }

    // The round windows the PTH LEDs shine through: over the CHOMPI, play and loop keys, the
    // encoders, and ENC5's second LED.
    void DrawLedWindows()
    {
        auto window = [&](const layout::Circle& w, const daisycola::Rgb& led) {
            const Light l = champi::LedLight(led, champi::kPthDivisor);
            Circle(w.x, w.y, w.d + 0.9f, kDarkGold);
            beginPath();
            circle(w.x, w.y, w.d / 2);
            fillPaint(radialGradient(w.x, w.y, 0, w.d / 2, Lit(Color(30, 30, 30), l), Lit(kLedOff, l)));
            fill();
            Glow(w.x, w.y, w.d / 2, w.d * 1.25f, l);
        };
        window(layout::kKeyLedWindow[0], leds_.key[champi::kChompiKey - 1]);
        window(layout::kKeyLedWindow[1], leds_.key[champi::kPlayKey - 1]);
        window(layout::kKeyLedWindow[2], leds_.key[champi::kLoopKey - 1]);
        for(int e = 0; e < champi::kNumEncoders; e++)
            window(layout::kEncoderLedWindow[e], leds_.encoder[e]);
        window(layout::kEncoder5SecondLedWindow, leds_.encoder5_second);
    }

    void DrawToggle()
    {
        const layout::Rect& s  = layout::kToggleSlot;
        const bool          on = champi::Runtime::Get().Panel().Toggle();
        beginPath();
        roundedRect(s.x, s.y, s.w, s.h, 0.8f);
        fillColor(Color(4, 4, 4));
        fill();
        // The lever: up is on.
        const float lh = s.h * 0.48f, ly = on ? s.y + 0.4f : s.y + s.h - lh - 0.4f;
        beginPath();
        roundedRect(s.x + 0.6f, ly, s.w - 1.2f, lh, 0.6f);
        fillPaint(linearGradient(0, ly, 0, ly + lh, Color(236, 236, 232), Color(140, 140, 136)));
        fill();
        // Little brackets, as on the reference.
        strokeColor(kCream);
        strokeWidth(0.4f);
        for(float y : {s.y - 3.2f, s.y + s.h + 3.2f})
        {
            beginPath();
            moveTo(s.x - 6.5f, y);
            lineTo(s.x + s.w + 6.5f, y);
            stroke();
        }
    }

    void DrawMic()
    {
        const layout::Rect& middle = layout::kMicSlots[2];
        const float         x = middle.x + middle.w / 2, y = middle.y + middle.h / 2;
        beginPath();
        circle(x, y, 7);
        fillPaint(radialGradient(x - 2, y - 2, 1, 7, Color(226, 192, 118), kDarkGold));
        fill();
        for(const layout::Rect& s : layout::kMicSlots)
        {
            beginPath();
            roundedRect(s.x, s.y, s.w, s.h, s.w / 2);
            fillColor(Color(6, 6, 6));
            fill();
        }
    }

    // The line-in and headphone jacks are on the side; they're drawn at the right edge, level with
    // the real ones. Clicking line in plugs or unplugs a cable.
    void DrawJacks()
    {
        const bool plugged = champi::Runtime::Get().Panel().LineIn();
        auto       socket  = [&](const layout::Point& p, const char* label, bool plug) {
            Circle(p.x, p.y, champi::kJack, Color(150, 150, 150));
            Circle(p.x, p.y, champi::kJack - 1.2f, Color(4, 4, 4));
            if(plug)
            {
                beginPath();
                moveTo(p.x, p.y);
                lineTo(layout::kWidth + 4, p.y);
                strokeColor(Color(34, 34, 36));
                strokeWidth(2.2f);
                stroke();
                Circle(p.x, p.y, champi::kJack - 1.6f, Color(60, 60, 62));
                Circle(p.x, p.y, 1.6f, Color(190, 190, 190));
            }
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            fontSize(2.4f);
            textAlign(ALIGN_CENTER | ALIGN_TOP);
            fillColor(kCream);
            text(p.x, p.y + champi::kJack / 2 + 1, label, nullptr);
        };
        socket(layout::kLineInJack, plugged ? "LINE" : "MIC", plugged);
        socket(layout::kPhonesJack, "PHONES", false);
    }

    // The USB socket and the SD slot face the player. They're drawn just inside the front edge,
    // over the real ones. A click on the socket plugs or unplugs USB power, the wheel over it sets
    // the battery; a click on the slot pulls the card out or puts it back.
    void DrawFrontEdge()
    {
        champi::Runtime& runtime = champi::Runtime::Get();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(2.4f);
        fillColor(kCream);

        const layout::Rect usb = champi::UsbSocket();
        beginPath();
        roundedRect(usb.x, usb.y, usb.w, usb.h, usb.h / 2);
        fillColor(Color(150, 150, 150));
        fill();
        beginPath();
        roundedRect(usb.x + 0.6f, usb.y + 0.6f, usb.w - 1.2f, usb.h - 1.2f, (usb.h - 1.2f) / 2);
        fillColor(runtime.Charger().UsbPower() ? Color(60, 60, 62) : Color(4, 4, 4));
        fill();
        if(runtime.Charger().UsbPower())
        {
            // The plug's tongue, and its cable running off the front.
            beginPath();
            rect(usb.x + 2.2f, usb.y + usb.h / 2 - 0.35f, usb.w - 4.4f, 0.7f);
            fillColor(Color(190, 190, 190));
            fill();
            beginPath();
            rect(usb.x + usb.w / 2 - 1.1f, usb.y + usb.h, 2.2f, layout::kHeight - usb.y - usb.h);
            fillColor(Color(34, 34, 36));
            fill();
        }
        char label[32];
        std::snprintf(label, sizeof label, "USB  %.1f V", runtime.Charger().BatteryMillivolts() / 1000.0);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(kCream);
        text(usb.x + usb.w + 1.5f, usb.y + usb.h / 2, label, nullptr);

        const layout::Rect sd = champi::SdSlot();
        beginPath();
        roundedRect(sd.x, sd.y, sd.w, sd.h, 0.4f);
        fillColor(Color(4, 4, 4));
        fill();
        if(runtime.Card().Inserted())
        {
            beginPath(); // the card's back edge, flush in the slot
            rect(sd.x + 0.8f, sd.y + 0.4f, sd.w - 1.6f, sd.h - 0.8f);
            fillColor(Color(70, 70, 76));
            fill();
        }
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(kCream);
        text(sd.x - 1.5f, sd.y + sd.h / 2, runtime.Card().Inserted() ? "SD" : "NO SD", nullptr);
    }

    void DrawStatus()
    {
        char status[200];
        std::snprintf(status, sizeof status,
                      "%s    %.0f Hz / %u%s    load %.0f%% (peak %.0f%%)    xruns %llu    late %llu    "
                      "dropouts %llu",
                      !champi::Runtime::Get().Card().Inserted() ? "no SD card (restart to read it again)"
                      : champi::Runtime::Get().Booted()         ? "running"
                                                                : "booting",
                      getSampleRate(),
                      Plugin().getBufferSize(), Plugin().Resampling() ? " resampled" : "",
                      load_.average * 100, load_.peak * 100, (unsigned long long)champi::g_xruns.load(),
                      (unsigned long long)load_.late_blocks, (unsigned long long)load_.dropouts);
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(2.6f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(kStatusText);
        text(2, layout::kHeight + kStatusHeight / 2, status, nullptr);
    }

    champi::MouseControl                  mouse_;
    champi::KeyboardControl               keyboard_;
    champi::LedFrame                      leds_{};
    champi::AudioLoad                     load_{};
    std::chrono::steady_clock::time_point last_load_{};
    Skin                                  skin_;
    bool                                  skin_loaded_ = false;
    bool                                  test_mode_hold_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChampiUI)
};

UI* createUI()
{
    return new ChampiUI();
}

END_NAMESPACE_DISTRHO
