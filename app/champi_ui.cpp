// The CHOMPI panel, drawn in vectors from the board files' layout (panel_layout.h) and played with
// the mouse (MouseControl) and the computer keyboard (KeyboardControl). The look follows the reference render: black body, cream and gold line
// art, white keys and knobs, the purple scrub wheel and the coloured CHOMPI, play and loop keys.
//
// The connections key (F8) opens the connections menu over the panel (ConnectionsMenu); nothing on
// the panel shows it. While it's open the panel takes no input.
//
// Drawing is in panel millimetres; the panel is scaled to fit the window, with the status line
// underneath. A skin folder can replace the logo and the glyphs on the CHOMPI, play and loop keys
// with PNGs (see Skin below); none are shipped.
#include <linux/input-event-codes.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <utility>

#include "DistrhoUI.hpp"
#include "app.h"
#include "champi_plugin.h"
#include "connections_menu.h"
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
const Color kMenuBox(24, 24, 27);
const Color kMenuSelected(54, 48, 36);
const Color kMenuMissing(96, 96, 100);

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
        menu_.SetLevels(champi::Options().audio_levels);
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
        if(champi::RoutingService* routing = champi::Options().routing)
            if(auto snapshot = routing->SnapshotIfNewer(routing_version_))
            {
                routing_version_ = snapshot->version;
                routing_ready_   = snapshot->graph.champi_present;
                menu_.SetSnapshot(*snapshot);
            }

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

        if(menu_open_)
        {
            // The panel stays in sight, dimmed.
            resetTransform();
            beginPath();
            rect(0, 0, getWidth(), getHeight());
            fillColor(Color(0, 0, 0, 0.6f));
            fill();
            translate(v.x, v.y);
            scale(v.scale, v.scale);
            DrawMenu();
        }
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if(ev.button != 1)
            return false;
        if(menu_open_)
        {
            if(ev.press && MenuReady())
            {
                float x, y;
                Fit().ToPanel(ev.pos, x, y);
                Send(menu_.Click(x, y));
                ApplyLevels();
            }
            return true;
        }
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
        if(menu_open_)
            return true;
        float x, y;
        Fit().ToPanel(ev.pos, x, y);
        mouse_.Move(x, y, std::chrono::steady_clock::now());
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        if(menu_open_)
        {
            const float steps = float(ev.delta.getY());
            menu_.ScrollBy(steps > 0 ? -1 : steps < 0 ? 1 : 0);
            return true;
        }
        float x, y;
        Fit().ToPanel(ev.pos, x, y);
        return mouse_.Scroll(x, y, float(ev.delta.getY()), std::chrono::steady_clock::now());
    }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        // On X11 a keycode is the evdev scancode plus 8: the physical key, whatever the layout.
        const champi::Scancode code = champi::Scancode(ev.keycode) - 8;
        if(ev.press && keyboard_.keymap().Lookup(code).kind == champi::Action::Kind::kConnections)
        {
            menu_open_ ? CloseMenu() : OpenMenu();
            return true;
        }
        if(menu_open_)
        {
            if(ev.press)
            {
                MenuKey(code);
                ApplyLevels();
            }
            return true;
        }
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

    // The panel lets go of everything it holds, as on losing focus: no release will reach it.
    void OpenMenu()
    {
        keyboard_.ReleaseAll();
        mouse_.Cancel();
        menu_.Reset();
        menu_open_ = true;
    }

    void CloseMenu() { menu_open_ = false; }

    // Whether the menu lists CHAMPI's ports, rather than a message.
    bool MenuReady() const { return champi::Options().routing && routing_ready_; }

    void MenuKey(champi::Scancode code)
    {
        if(!MenuReady() && code != KEY_ESC && code != KEY_BACKSPACE)
            return;
        switch(code)
        {
            case KEY_UP: menu_.Move(0, -1); break;
            case KEY_DOWN: menu_.Move(0, 1); break;
            case KEY_LEFT: menu_.Move(-1, 0); break;
            case KEY_RIGHT: menu_.Move(1, 0); break;
            case KEY_TAB: menu_.SwitchColumn(); break;
            case KEY_PAGEUP: menu_.Page(-1); break;
            case KEY_PAGEDOWN: menu_.Page(1); break;
            case KEY_ENTER:
            case KEY_KPENTER:
            case KEY_SPACE: Send(menu_.Activate()); break;
            case KEY_ESC:
            case KEY_BACKSPACE:
                if(!menu_.Back())
                    CloseMenu();
                break;
            default: break;
        }
    }

    // Sets the gains of ports whose volume the menu changed, and saves them.
    void ApplyLevels()
    {
        champi::AudioLevels& levels = champi::Options().audio_levels;
        if(menu_.Levels() == levels)
            return;
        levels = menu_.Levels();
        Plugin().SetLevels(levels);
        const auto& path = champi::Options().audio_levels_path;
        if(path.empty())
            return;
        try
        {
            levels.Save(path);
        }
        catch(const std::exception& e)
        {
            std::fprintf(stderr, "champi: can't save the volumes: %s\n", e.what());
        }
    }

    void Send(std::vector<champi::RouteChange> changes)
    {
        if(!changes.empty() && champi::Options().routing)
            champi::Options().routing->Request(std::move(changes));
    }

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

    // The panel graphics, after the reference: cream line art grouping the controls, with arrows
    // for the knobs' travel and small icons, and gold round the CHOMPI key and the scrub wheel.
    void DrawLineArt()
    {
        DrawChompiArt();
        DrawKnobArt();
        DrawScrubArt();
        DrawVolumeArt();
    }

    // A path through `points`, each corner rounded by its own radius.
    struct Corner
    {
        float x, y, r;
    };
    void RoundedPath(std::initializer_list<Corner> points)
    {
        const Corner* p = points.begin();
        beginPath();
        moveTo(p[0].x, p[0].y);
        for(size_t i = 1; i + 1 < points.size(); i++)
            arcTo(p[i].x, p[i].y, p[i + 1].x, p[i + 1].y, p[i].r);
        lineTo(p[points.size() - 1].x, p[points.size() - 1].y);
        stroke();
    }

    // A filled arrowhead with its tip at x, y, pointing along `angle`.
    void Arrowhead(float x, float y, float angle, const Color& c)
    {
        constexpr float kLength = 1.4f, kHalfWidth = 0.8f;
        const float     dx = std::cos(angle), dy = std::sin(angle);
        const float     bx = x - dx * kLength, by = y - dy * kLength;
        beginPath();
        moveTo(x, y);
        lineTo(bx - dy * kHalfWidth, by + dx * kHalfWidth);
        lineTo(bx + dy * kHalfWidth, by - dx * kHalfWidth);
        closePath();
        fillColor(c);
        fill();
    }

    // A cream arc round x, y from a0 clockwise to a1, with arrowheads on the ends asked for.
    void ArcArrow(float x, float y, float r, float a0, float a1, bool head0, bool head1)
    {
        const float back = 1.2f / r; // the arc stops under the arrowhead
        const float s = head0 ? a0 + back : a0, e = head1 ? a1 - back : a1;
        beginPath();
        arc(x, y, r, s, e, CW);
        stroke();
        // Each head carries on from where the arc stops, along its tangent.
        if(head0)
            Arrowhead(x + r * std::cos(s) + 1.4f * std::sin(s), y + r * std::sin(s) - 1.4f * std::cos(s),
                      s - kPi / 2, kCream);
        if(head1)
            Arrowhead(x + r * std::cos(e) - 1.4f * std::sin(e), y + r * std::sin(e) + 1.4f * std::cos(e),
                      e + kPi / 2, kCream);
    }

    // A rounded box filled in cream, for an icon.
    void IconBox(float x, float y, float w, float h)
    {
        beginPath();
        roundedRect(x - w / 2, y - h / 2, w, h, 0.8f);
        fillColor(kCream);
        fill();
    }

    // Three overlapping circles, one filled and marked with a plus or a star, as on the reference's
    // shift icons. `ink` draws the circles and `paper` is what they're on.
    void DrawCircleTrio(float x, float y, float r, int filled, bool star, const Color& ink, const Color& paper)
    {
        const float step = r * 1.12f;
        auto        draw = [&](int i) {
            const float cx = x + (i - 1) * step;
            beginPath();
            circle(cx, y, r);
            fillColor(i == filled ? ink : paper);
            fill();
            strokeColor(ink);
            stroke();
            if(i != filled)
                return;
            const float g = r * 0.45f;
            strokeColor(paper);
            beginPath();
            if(star)
            {
                for(int k = 0; k < 4; k++)
                {
                    const float a = k * kPi / 4;
                    moveTo(cx - g * std::cos(a), y - g * std::sin(a));
                    lineTo(cx + g * std::cos(a), y + g * std::sin(a));
                }
            }
            else
            {
                moveTo(cx - g, y);
                lineTo(cx + g, y);
                moveTo(cx, y - g);
                lineTo(cx, y + g);
            }
            stroke();
        };
        // The filled circle sits on top of the others.
        if(filled == 0)
            for(int i = 2; i >= 0; i--)
                draw(i);
        else
            for(int i = 0; i <= 2; i++)
                draw(i);
    }

    // The CHOMPI key and the toggle switch on one gold plate, framed in cream, with the toggle
    // wired to the shift icons above and below it.
    void DrawChompiArt()
    {
        const layout::Rect& c = layout::kChompiCutout;
        const layout::Rect& t = layout::kToggleSlot;

        const float px = c.x - 2, py = c.y - 1.9f, pw = c.w + 4, ph = c.h + 3.8f;
        beginPath();
        roundedRect(px, py, pw, ph, 1);
        roundedRect(t.x - 2.25f, t.y - 1.85f, px - t.x + 3, t.h + 3.7f, 1);
        fillColor(kGold);
        fill();

        strokeWidth(0.5f);
        strokeColor(kCream);
        const float right = px + pw + 2, top = py - 1.9f, bottom = py + ph + 1.9f;
        RoundedPath({{px + 0.3f, top, 0}, {right, top, 1.6f}, {right, bottom, 1.6f}, {px + 0.3f, bottom, 0}});

        // The icon boxes, the upper one outlined and the lower one filled.
        const float bx = t.x - 2.95f, bw = 12.3f, bh = 7.3f;
        const float upper = top - 0.45f + bh / 2, lower = bottom + 0.25f - bh / 2;
        beginPath();
        roundedRect(bx, upper - bh / 2, bw, bh, 0.8f);
        stroke();
        IconBox(bx + bw / 2, lower, bw, bh);
        strokeWidth(0.4f);
        DrawCircleTrio(bx + bw / 2, upper, 2.4f, 0, false, kCream, kBody);
        DrawCircleTrio(bx + bw / 2, lower, 2.4f, 2, true, kBody, kCream);

        // The wire: from the upper box down to the toggle, a meander, and on to the lower box.
        strokeWidth(0.5f);
        strokeColor(kCream);
        const float spine = t.x - 8.35f, row = 1.6f, r = row / 2, y0 = t.y + 0.35f;
        RoundedPath({{bx, upper, 0},
                     {spine, upper, 1.2f},
                     {spine, y0, r},
                     {spine - 4.8f, y0, r},
                     {spine - 4.8f, y0 + row, r},
                     {t.x - 3.55f, y0 + row, r},
                     {t.x - 3.55f, y0 + 2 * row, r},
                     {spine - 2.7f, y0 + 2 * row, r},
                     {spine - 2.7f, y0 + 3 * row, r},
                     {t.x - 4.8f, y0 + 3 * row, r},
                     {t.x - 4.8f, y0 + 4 * row, r},
                     {spine, y0 + 4 * row, 1.2f},
                     {spine, lower, 1.2f},
                     {bx, lower, 0}});
    }

    // ENC4 (speed) and ENC3 (effect) have an arc over the top with an icon below; ENC1 and ENC2
    // (start and end) share a box with the sample between its flags.
    void DrawKnobArt()
    {
        constexpr float kArc = 11; // the arcs' radius round the small knobs
        const float     y    = layout::kEncoder[0].y;
        strokeWidth(0.5f);
        strokeColor(kCream);

        for(int e : {4, 3})
        {
            const float x = layout::kEncoder[e - 1].x;
            ArcArrow(x, y, kArc, kPi * 2 / 3, kPi * 7 / 3, true, true);
            IconBox(x, y + 14.6f, 9.2f, 7.4f);
            if(e == 4)
                DrawGauge(x, y + 14.6f);
            else
                DrawWand(x, y + 14.6f);
            strokeWidth(0.5f);
            strokeColor(kCream);
        }

        // The box: its top runs between the knobs' arcs, which carry on inwards to arrows.
        const float x1 = layout::kEncoder[0].x, x2 = layout::kEncoder[1].x, bottom = 52.2f;
        RoundedPath({{x1 - kArc, y, 0}, {x1 - kArc, bottom, 3}, {x2 + kArc, bottom, 3}, {x2 + kArc, y, 0}});
        beginPath();
        moveTo(x1, y - kArc);
        lineTo(x2, y - kArc);
        stroke();
        ArcArrow(x1, y, kArc, kPi, kPi * 2.05f, false, true);
        ArcArrow(x2, y, kArc, kPi * 0.95f, kPi * 2, true, false);
        DrawSampleSketch(x1 - 4, x2 + 4, 49.5f);
    }

    // A sample between a start flag and an end flag: silence, a fade in, the sound, a fade out.
    void DrawSampleSketch(float left, float right, float base)
    {
        strokeWidth(0.4f);
        beginPath();
        moveTo(left, base);
        lineTo(right, base);
        stroke();

        // The flags, on poles with a knob on top; the start one points right, the end one left.
        for(int side : {1, -1})
        {
            const float x = side > 0 ? left : right, top = base - 5.2f;
            beginPath();
            moveTo(x, base);
            lineTo(x, top);
            stroke();
            Circle(x, top, 0.9f, kCream);
            beginPath();
            moveTo(x, top + 0.8f);
            lineTo(x + side * 2.6f, top + 0.8f);
            lineTo(x + side * 1.8f, top + 1.85f);
            lineTo(x + side * 2.6f, top + 2.9f);
            lineTo(x, top + 2.9f);
            closePath();
            fillColor(kCream);
            fill();
        }

        const float mid = (left + right) / 2, level = base - 4.5f;
        const float in0 = mid - 13.1f, in1 = mid - 9.3f, out0 = mid + 9.3f, out1 = mid + 13.1f;
        const float s0 = mid - 7.3f, s1 = mid + 7.3f;
        beginPath();
        moveTo(in0, base);
        bezierTo((in0 + in1) / 2, base, (in0 + in1) / 2, level, in1, level);
        lineTo(s0, level);
        // The sound: narrow peaks of different heights, dipping a little under the level.
        constexpr int kSteps  = 90;
        constexpr float kCycles = 3.5f;
        const float     peaks[] = {3.0f, 4.6f, 3.6f, 2.2f};
        for(int i = 1; i <= kSteps; i++)
        {
            const float t = float(i) / kSteps, w = std::sin(t * kCycles * 2 * kPi);
            const float h = w > 0 ? peaks[std::min(3, int(t * kCycles))] : 1.0f;
            lineTo(s0 + (s1 - s0) * t, level - w * h);
        }
        lineTo(out0, level);
        bezierTo((out0 + out1) / 2, level, (out0 + out1) / 2, base, out1, base);
        stroke();

        // Dotted lines down from where the sound starts and stops.
        for(float x : {in1, out0})
            for(float dy = 0.8f; dy < base - level - 0.4f; dy += 0.9f)
                Circle(x, level + dy, 0.3f, kCream);
    }

    // A speedometer, in the body colour on an icon box.
    void DrawGauge(float x, float y)
    {
        strokeColor(kBody);
        strokeWidth(0.45f);
        const float cy = y + 1.1f, r = 2.9f;
        beginPath();
        arc(x, cy, r, kPi, 2 * kPi, CW);
        stroke();
        beginPath();
        for(int i = 0; i <= 4; i++)
        {
            const float a = kPi + kPi * (i + 0.5f) / 5.5f;
            moveTo(x + r * 0.62f * std::cos(a), cy + r * 0.62f * std::sin(a));
            lineTo(x + r * 0.82f * std::cos(a), cy + r * 0.82f * std::sin(a));
        }
        moveTo(x, cy);
        lineTo(x + 1.4f, cy - 1.6f);
        moveTo(x - r - 0.6f, cy + 1.0f);
        lineTo(x + r + 0.6f, cy + 1.0f);
        moveTo(x - r + 0.4f, cy + 2.0f);
        lineTo(x + r - 0.4f, cy + 2.0f);
        stroke();
        Circle(x, cy, 1.1f, kBody);
    }

    // A magic wand with a star and sparkles.
    void DrawWand(float x, float y)
    {
        strokeColor(kBody);
        strokeWidth(0.55f);
        lineCap(ROUND);
        beginPath();
        moveTo(x - 3.2f, y + 2.8f);
        lineTo(x + 0.2f, y - 0.6f);
        stroke();
        const float sx = x + 1.1f, sy = y - 1.5f;
        beginPath();
        for(int i = 0; i < 10; i++)
        {
            const float a = -kPi / 2 + i * kPi / 5, r = i % 2 ? 0.75f : 1.8f;
            if(i == 0)
                moveTo(sx + r * std::cos(a), sy + r * std::sin(a));
            else
                lineTo(sx + r * std::cos(a), sy + r * std::sin(a));
        }
        closePath();
        strokeWidth(0.4f);
        lineJoin(ROUND);
        stroke();
        for(const auto& p : {std::make_pair(-1.6f, -2.6f), std::make_pair(3.4f, -0.4f), std::make_pair(3.0f, 1.8f),
                             std::make_pair(-0.4f, -3.1f)})
            Circle(x + p.first, y + p.second, 0.5f, kBody);
        lineCap(BUTT);
        lineJoin(MITER);
    }

    // The scrub wheel and the play and loop keys: a gold outline round both, an arc saying the wheel
    // turns both ways, the play and loop LEDs as two reels with a sound between them, and a cream
    // frame with a cassette's lid.
    void DrawScrubArt()
    {
        const layout::Circle& scrub = layout::kEncoder[4];
        const layout::Rect&   pl    = layout::kPlayLoopCutout;
        const float           line  = 1.0f;

        // Both shapes filled in gold, then in black a line's width smaller.
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

        strokeWidth(0.5f);
        strokeColor(kCream);
        ArcArrow(scrub.x, scrub.y, scrub.d / 2 + 5, kPi * 0.72f, kPi * 1.28f, true, true);

        // «|» between ENC5's two LEDs: back, stop, forward.
        const float cx = scrub.x, cy = layout::kEncoder5SecondLedWindow.y;
        beginPath();
        moveTo(cx, cy - 1.9f);
        lineTo(cx, cy + 1.9f);
        for(int side : {-1, 1})
            for(float tip : {2.0f, 3.3f})
            {
                moveTo(cx + side * (tip - 0.9f), cy - 1.1f);
                lineTo(cx + side * tip, cy);
                lineTo(cx + side * (tip - 0.9f), cy + 1.1f);
            }
        strokeWidth(0.55f);
        stroke();
        strokeWidth(0.5f);

        // The frame, its top bumped up into the lid between the reels.
        const float top = pl.y - 2.2f - line - 1.4f, bottom = pl.y + pl.h + 2.2f + line + 1.4f;
        const float left = scrub.x + scrub.d / 2 + 2.6f + line + 0.4f, right = pl.x + pl.w + 4.5f;
        const float lid = (layout::kKeyLedWindow[1].x + layout::kKeyLedWindow[2].x) / 2;
        RoundedPath({{left, top, 0},
                     {lid - 7.2f, top, 0.5f},
                     {lid - 6.4f, top - 2.4f, 0.5f},
                     {lid + 6.4f, top - 2.4f, 0.5f},
                     {lid + 7.2f, top, 0.5f},
                     {right, top, 1.6f},
                     {right, bottom, 1.6f},
                     {left, bottom, 0}});
        beginPath();
        roundedRect(lid - 4, top - 1.5f, 8, 0.6f, 0.3f);
        fillColor(kCream);
        fill();
        Circle(lid - 5.1f, top - 1.2f, 0.7f, kCream);
        Circle(lid + 5.1f, top - 1.2f, 0.7f, kCream);

        // The reels round the play and loop LEDs.
        for(int i : {1, 2})
        {
            const layout::Circle& w = layout::kKeyLedWindow[i];
            beginPath();
            circle(w.x, w.y, 5.1f);
            strokeWidth(0.35f);
            stroke();
            strokeWidth(1.1f);
            for(int k = 0; k < 8; k++)
            {
                const float a = k * kPi / 4 + kPi / 8;
                beginPath();
                arc(w.x, w.y, 4.0f, a - 0.28f, a + 0.28f, CW);
                stroke();
            }
        }

        // The sound between them, swelling in the middle.
        const float s0 = layout::kKeyLedWindow[1].x + 5.1f, s1 = layout::kKeyLedWindow[2].x - 5.1f;
        const float sy = layout::kKeyLedWindow[1].y;
        strokeWidth(0.4f);
        beginPath();
        moveTo(s0, sy);
        constexpr int kSteps = 60;
        for(int i = 1; i <= kSteps; i++)
        {
            const float t = float(i) / kSteps, env = std::sin(t * kPi);
            lineTo(s0 + (s1 - s0) * t, sy - 1.9f * env * env * std::sin(t * 5 * 2 * kPi));
        }
        stroke();
    }

    // ENC6 (volume) in a ring, between the mic, wired to the mic grille, and the power.
    void DrawVolumeArt()
    {
        const layout::Circle& v    = layout::kEncoder[5];
        const layout::Rect&   grille = layout::kMicSlots[2];
        const float           ring = 11.5f, disc = 2.9f;
        strokeWidth(0.5f);
        strokeColor(kCream);
        beginPath();
        circle(v.x, v.y, ring);
        stroke();

        const float mic = v.x - ring - disc + 0.2f, power = v.x + ring + disc - 0.2f;
        const float gx = grille.x + grille.w / 2, gy = grille.y + grille.h / 2 + 7;
        beginPath();
        moveTo(gx, gy);
        lineTo(gx, gy + 3.8f);
        bezierTo(gx, gy + 5.6f, mic, gy + 4.6f, mic, gy + 7);
        lineTo(mic, v.y - disc);
        stroke();

        Circle(mic, v.y, 2 * disc, kCream);
        Circle(power, v.y, 2 * disc, kCream);

        // A microphone.
        strokeColor(kBody);
        strokeWidth(0.4f);
        beginPath();
        roundedRect(mic - 0.6f, v.y - 1.9f, 1.2f, 2.4f, 0.6f);
        fillColor(kBody);
        fill();
        beginPath();
        arc(mic, v.y - 0.3f, 1.2f, 0, kPi, CW);
        moveTo(mic, v.y + 0.9f);
        lineTo(mic, v.y + 1.7f);
        moveTo(mic - 0.8f, v.y + 1.7f);
        lineTo(mic + 0.8f, v.y + 1.7f);
        stroke();

        // A lightning bolt.
        beginPath();
        moveTo(power + 0.5f, v.y - 2.0f);
        lineTo(power - 1.0f, v.y + 0.3f);
        lineTo(power + 0.1f, v.y + 0.3f);
        lineTo(power - 0.5f, v.y + 2.0f);
        lineTo(power + 1.0f, v.y - 0.4f);
        lineTo(power - 0.1f, v.y - 0.4f);
        closePath();
        fillColor(kBody);
        fill();
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
        for(int k = 1; k <= 14; k++)
        {
            char label[4];
            std::snprintf(label, sizeof label, "%d", k);
            text(layout::kKey[k - 1].x, 99.6f, label, nullptr);
        }
        // KEY15 has the shift icon instead of a number, as on the reference.
        strokeWidth(0.35f);
        DrawCircleTrio(layout::kKey[14].x, 99.6f, 1.6f, 2, false, kCream, kBody);
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
        // The lever: up is off, TAPE's record mode; down is on, playback.
        const float lh = s.h * 0.48f, ly = on ? s.y + s.h - lh - 0.4f : s.y + 0.4f;
        beginPath();
        roundedRect(s.x + 0.6f, ly, s.w - 1.2f, lh, 0.6f);
        fillPaint(linearGradient(0, ly, 0, ly + lh, Color(236, 236, 232), Color(140, 140, 136)));
        fill();
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
    // the real ones. Clicking either plugs or unplugs a cable.
    void DrawJacks()
    {
        const champi::PanelState& panel = champi::Runtime::Get().Panel();
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
        socket(layout::kLineInJack, panel.LineIn() ? "LINE" : "MIC", panel.LineIn());
        socket(layout::kPhonesJack, panel.Phones() ? "PHONES" : "MASTER", panel.Phones());
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

    // ---- The connections menu ------------------------------------------------------------------

    void DrawMenu()
    {
        using Menu         = champi::ConnectionsMenu;
        const layout::Rect& b = Menu::kBox;
        beginPath();
        roundedRect(b.x, b.y, b.w, b.h, 2);
        fillColor(kMenuBox);
        fill();
        strokeColor(kDarkGold);
        strokeWidth(0.4f);
        stroke();

        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(4.2f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(kCream);
        text(b.x + Menu::kPad, Menu::kTitleY, "Connections", nullptr);

        const std::string close = MenuKeyName() + " or Esc closes";
        const char* hint = !MenuReady() ? close.c_str()
                           : menu_.InPeers() ? "Enter or Space ticks    Esc goes back"
                                             : nullptr;
        const bool        level = !menu_.InPeers() && Menu::HasLevel(menu_.OpenRow());
        const std::string first = std::string(level ? "Left/Right sets the volume    Tab switches column    " : "")
                                  + "Enter opens a port    " + close;
        fontSize(2.8f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(kStatusText);
        text(b.x + b.w - Menu::kPad, Menu::kTitleY, hint ? hint : first.c_str(), nullptr);

        if(!MenuReady())
        {
            fontSize(3.4f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(kCream);
            text(b.x + b.w / 2, b.y + b.h / 2,
                 !champi::Options().routing ? "Routing needs JACK or PipeWire: CHAMPI is running on native audio."
                                            : "Waiting for CHAMPI's ports...",
                 nullptr);
            return;
        }
        if(menu_.InPeers())
            DrawMenuPeers();
        else
            DrawMenuPorts();
    }

    std::string MenuKeyName() const
    {
        const auto keys = keyboard_.keymap().KeysFor({champi::Action::Kind::kConnections, 0});
        return keys.empty() ? "Esc" : champi::ScancodeName(keys[0]);
    }

    void DrawMenuPorts()
    {
        using Menu = champi::ConnectionsMenu;
        for(int c = 0; c < 2; c++)
        {
            const layout::Rect first = Menu::RowRect(c, 0);
            fontSize(3.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(kGold);
            text(first.x + 1, Menu::kSubtitleY, c == 0 ? "INPUTS" : "OUTPUTS", nullptr);

            const auto& rows = menu_.Column(c);
            for(int r = 0; r < int(rows.size()); r++)
            {
                const Menu::Row&   row      = rows[r];
                const layout::Rect rr       = Menu::RowRect(c, r);
                const bool         selected = menu_.SelectedColumn() == c && menu_.SelectedRow() == r;
                beginPath();
                roundedRect(rr.x, rr.y, rr.w, rr.h, 1.2f);
                fillColor(selected ? kMenuSelected : Color(32, 32, 35));
                fill();
                if(selected)
                {
                    strokeColor(kGold);
                    strokeWidth(0.35f);
                    stroke();
                }

                std::string summary;
                for(size_t i = 0; i < row.connected.size(); i++)
                    summary += (i ? ",  " : "") + row.connected[i];
                if(summary.empty())
                    summary = "not connected";
                if(row.missing)
                    summary += "    (" + std::to_string(row.missing) + " saved, not present)";

                const bool         level = selected && Menu::HasLevel(row);
                const layout::Rect bar   = Menu::LevelRect(c, r);
                scissor(rr.x, rr.y, (level ? bar.x - 3 : rr.x + rr.w - 1.5f) - rr.x, rr.h);
                fontSize(3.6f);
                textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
                fillColor(kCream);
                text(rr.x + 3, rr.y + 4.4f, row.label.c_str(), nullptr);
                fontSize(2.7f);
                fillColor(row.connected.empty() ? kMenuMissing : kStatusText);
                text(rr.x + 3, rr.y + 9.4f, summary.c_str(), nullptr);
                resetScissor();
                if(level)
                    DrawLevel(bar, menu_.RowLevel(row));
            }
        }
    }

    // A volume bar with its figure above it.
    void DrawLevel(const layout::Rect& bar, int percent)
    {
        const std::string figure = "Volume " + std::to_string(percent) + "%";
        fontSize(2.7f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(kStatusText);
        text(bar.x + bar.w, bar.y - 3.8f, figure.c_str(), nullptr);
        beginPath();
        roundedRect(bar.x, bar.y, bar.w, bar.h, bar.h / 2);
        fillColor(Color(18, 18, 20));
        fill();
        strokeColor(kStatusText);
        strokeWidth(0.25f);
        stroke();
        if(percent > 0)
        {
            beginPath();
            roundedRect(bar.x, bar.y, bar.w * percent / champi::AudioLevels::kMax, bar.h, bar.h / 2);
            fillColor(kGold);
            fill();
        }
    }

    void DrawMenuPeers()
    {
        using Menu             = champi::ConnectionsMenu;
        using Item             = Menu::Item;
        const Menu::Row&   row = menu_.OpenRow();
        const layout::Rect& k  = Menu::kBack;

        beginPath();
        roundedRect(k.x, k.y, k.w, k.h, 1);
        strokeColor(kStatusText);
        strokeWidth(0.3f);
        stroke();
        fontSize(2.9f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(kStatusText);
        text(k.x + k.w / 2, k.y + k.h / 2, "< Back", nullptr);

        const bool        input = champi::kChampiPorts[row.ports[0]].input;
        const std::string title = row.label + (input ? ": connect from" : ": connect to");
        fontSize(3.6f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(kCream);
        text(k.x + k.w + 3, Menu::kSubtitleY, title.c_str(), nullptr);

        const auto&         items = menu_.Items();
        const layout::Rect& list  = Menu::kList;
        bool any_port = false;
        for(const Item& item : items)
            any_port |= item.kind == Item::Kind::kPeer;

        scissor(list.x, list.y, list.w, list.h);
        const int last = std::min(int(items.size()), menu_.FirstVisible() + Menu::VisibleLines() + 1);
        for(int i = menu_.FirstVisible(); i < last; i++)
        {
            const Item&        item = items[i];
            const layout::Rect r    = menu_.ItemRect(i);
            const float        cy   = r.y + r.h / 2;
            if(item.kind == Item::Kind::kHeader)
            {
                fontSize(2.9f);
                textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
                fillColor(kGold);
                text(r.x + 1, cy + 0.6f, item.label.c_str(), nullptr);
                continue;
            }
            if(i == menu_.SelectedItem())
            {
                beginPath();
                roundedRect(r.x, r.y + 0.3f, r.w - 3, r.h - 0.6f, 1);
                fillColor(kMenuSelected);
                fill();
            }
            DrawCheckbox(r.x + 4, cy, item.tick, item.missing);
            fontSize(3.2f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(item.missing ? kMenuMissing : kCream);
            text(r.x + 8, cy, item.label.c_str(), nullptr);
        }
        resetScissor();

        if(!any_port)
        {
            fontSize(3.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(kMenuMissing);
            text(list.x + 8, menu_.ItemRect(int(items.size())).y + Menu::kLineHeight / 2,
                 "Nothing on the graph fits this port.", nullptr);
        }

        // A scroll bar when the list is longer than the box.
        const int lines = Menu::VisibleLines();
        if(int(items.size()) > lines)
        {
            const float h = list.h * lines / items.size();
            const float y = list.y + (list.h - h) * menu_.FirstVisible() / float(items.size() - lines);
            beginPath();
            roundedRect(list.x + list.w - 1.2f, y, 1.2f, h, 0.6f);
            fillColor(kStatusText);
            fill();
        }
    }

    void DrawCheckbox(float x, float y, champi::ConnectionsMenu::Tick tick, bool missing)
    {
        using Tick           = champi::ConnectionsMenu::Tick;
        constexpr float kBox = 3.4f;
        const Color     ink  = missing ? kMenuMissing : kGold;
        beginPath();
        roundedRect(x - kBox / 2, y - kBox / 2, kBox, kBox, 0.5f);
        if(tick == Tick::kOn)
        {
            fillColor(ink);
            fill();
            beginPath();
            moveTo(x - 1.0f, y + 0.1f);
            lineTo(x - 0.25f, y + 0.85f);
            lineTo(x + 1.1f, y - 0.8f);
            strokeColor(kMenuBox);
            strokeWidth(0.5f);
            stroke();
            return;
        }
        strokeColor(ink);
        strokeWidth(0.35f);
        stroke();
        if(tick == Tick::kSome)
        {
            beginPath();
            moveTo(x - 0.9f, y);
            lineTo(x + 0.9f, y);
            strokeWidth(0.5f);
            stroke();
        }
    }

    champi::MouseControl                  mouse_;
    champi::KeyboardControl               keyboard_;
    champi::LedFrame                      leds_{};
    champi::AudioLoad                     load_{};
    std::chrono::steady_clock::time_point last_load_{};
    Skin                                  skin_;
    bool                                  skin_loaded_ = false;
    bool                                  test_mode_hold_;
    champi::ConnectionsMenu               menu_;
    bool                                  menu_open_       = false;
    uint64_t                              routing_version_ = 0;
    bool                                  routing_ready_   = false; // CHAMPI's ports are listed

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChampiUI)
};

UI* createUI()
{
    return new ChampiUI();
}

END_NAMESPACE_DISTRHO
