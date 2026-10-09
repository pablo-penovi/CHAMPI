"""Writes app/panel_layout.h: where everything on the CHOMPI top panel sits, from the board files.

The enclosure's top panel (CC_Chompi_Rev4_Enc_TOP.brd) has the outline, the cut-outs the keys
show through, and the holes for the encoders, the scrub wheel and the LED windows. The main board
(CC_Chompi_Rev4.brd) says which part is which: KEYn, SWn (ENCn), the LEDs, and the USB socket and
SD slot on the front edge. Both are EAGLE XML
in millimetres. The main board sits under the panel at a fixed offset, which is fitted here from
the encoder shafts and their holes. The lower board (ENC5 and the jacks) sits at its own offset,
fitted from ENC5 and the scrub wheel's hole.

The header is in panel millimetres with the origin at the top-left corner of the outline and y
pointing down, as on screen.

Run from the repo root:  python3 tools/extract_panel_layout.py
Check it's up to date:   python3 tools/extract_panel_layout.py --check
"""
import argparse
import math
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOP_BRD = "hardware/hardware-enclosure/CC_Chompi_Rev4_Enc_TOP.brd"
MAIN_BRD = "hardware/hardware-pcb/board files/CC_Chompi_Rev4.brd"
DIMENSION_LAYER = "20"

NUM_KEYS = 28
NUM_ENCODERS = 6
SCRUB_ENCODER = 5  # ENC5 is on the lower board and comes up through the big hole

# The PTH LEDs over the CHOMPI, play and loop keys, the encoders, and ENC5's second LED.
KEY_PTH_LEDS = {26: "LED1", 27: "LED8", 28: "LED9"}
ENCODER_LEDS = {1: "LED3", 2: "LED4", 3: "LED5", 4: "LED2", 5: "LED6", 6: "LED10"}
ENCODER5_SECOND_LED = "LED7"
LINE_IN_JACK = "J_AUDIO_IN"
PHONES_JACK = "J_HP_OUT"
USB_SOCKET = "J1"  # USB-C, on the main board's front edge
SD_SLOT = "P5"     # the micro-SD holder, also on the front edge

TOLERANCE = 0.1  # mm, for anything that should line up exactly


def fail(message):
    sys.exit(f"extract_panel_layout: {message}")


def parse_rot(rot):
    """EAGLE's rot attribute, e.g. "MR90", as (mirrored, degrees)."""
    m = re.fullmatch(r"S?(M?)R(-?[\d.]+)", rot or "R0")
    if not m:
        fail(f"can't read rotation {rot!r}")
    return bool(m.group(1)), float(m.group(2))


def place(element, x, y):
    """A point in an element's package, on the board: mirror, then rotate, then move."""
    mirrored, degrees = parse_rot(element.get("rot"))
    if mirrored:
        x = -x
    a = math.radians(degrees)
    return (float(element.get("x")) + x * math.cos(a) - y * math.sin(a),
            float(element.get("y")) + x * math.sin(a) + y * math.cos(a))


def loops(wires):
    """Joins the outline wires into closed loops of corner points. Arcs are dropped to their ends;
    the panel's only arcs are small corner roundings and the ends of the mic slots."""
    key = lambda x, y: (round(x, 3), round(y, 3))
    segments = [(key(w["x1"], w["y1"]), key(w["x2"], w["y2"])) for w in wires]
    result = []
    while segments:
        a, b = segments.pop()
        points = [a, b]
        while points[-1] != points[0]:
            for i, (p, q) in enumerate(segments):
                if points[-1] in (p, q):
                    points.append(q if points[-1] == p else p)
                    segments.pop(i)
                    break
            else:
                fail(f"outline starting at {a} doesn't close")
        result.append(points[:-1])
    return result


def bbox(points):
    xs, ys = [p[0] for p in points], [p[1] for p in points]
    return min(xs), min(ys), max(xs), max(ys)


def inside(points, x, y):
    """Even-odd point-in-polygon test."""
    result = False
    for (x1, y1), (x2, y2) in zip(points, points[1:] + points[:1]):
        if (y1 > y) != (y2 > y) and x < x1 + (y - y1) * (x2 - x1) / (y2 - y1):
            result = not result
    return result


def nearest(holes, x, y, drills):
    candidates = [h for h in holes if h["drill"] in drills]
    return min(candidates, key=lambda h: math.hypot(h["x"] - x, h["y"] - y))


class Layout:
    def __init__(self, chompi):
        top = ET.parse(chompi / TOP_BRD).getroot().find("drawing/board")
        main = ET.parse(chompi / MAIN_BRD).getroot().find("drawing/board")

        plain = top.find("plain")
        self.holes = [{k: float(h.get(k)) for k in ("x", "y", "drill")} for h in plain.iter("hole")]
        wires = [{k: float(w.get(k)) for k in ("x1", "y1", "x2", "y2")}
                 for w in plain.iter("wire") if w.get("layer") == DIMENSION_LAYER]
        self.loops = loops(wires)

        self.packages = {}
        for library in main.iter("library"):
            for package in library.iter("package"):
                self.packages[(library.get("name"), package.get("name"))] = package
        self.elements = {e.get("name"): e for e in main.iter("element")}

        self.fit_offset()

    def element(self, name):
        if name not in self.elements:
            fail(f"no {name} on the main board")
        return self.elements[name]

    def origin(self, name):
        e = self.element(name)
        return float(e.get("x")), float(e.get("y"))

    def switch_centre(self, name):
        """A key switch's centre: the big hole for the switch's centre post, not the socket's
        origin."""
        e = self.element(name)
        package = self.packages[(e.get("library"), e.get("package"))]
        post = max(package.iter("hole"), key=lambda h: float(h.get("drill")))
        return place(e, float(post.get("x")), float(post.get("y")))

    def fit_offset(self):
        """The main board's position under the panel, from the encoder shafts on it."""
        pairs = []
        for n in range(1, NUM_ENCODERS + 1):
            if n == SCRUB_ENCODER:
                continue
            x, y = self.origin(f"SW{n}")
            hole = nearest(self.holes, x, y, {7.7})
            pairs.append(((x, y), (hole["x"], hole["y"])))
        dx = sum(h[0] - b[0] for b, h in pairs) / len(pairs)
        dy = sum(h[1] - b[1] for b, h in pairs) / len(pairs)
        for (bx, by), (hx, hy) in pairs:
            if math.hypot(bx + dx - hx, by + dy - hy) > TOLERANCE:
                fail(f"encoder at {bx}, {by} doesn't line up with its hole")
        self.offset = dx, dy

    def on_panel(self, x, y):
        return x + self.offset[0], y + self.offset[1]

    def build(self):
        outline = max(self.loops, key=lambda p: (bbox(p)[2] - bbox(p)[0]) * (bbox(p)[3] - bbox(p)[1]))
        left, bottom, right, top = bbox(outline)
        to_screen = lambda x, y: (x - left, top - y)
        cutouts = [p for p in self.loops if p is not outline]

        def cutout_holding(x, y, what):
            for points in cutouts:
                if inside(points, x, y):
                    return points
            fail(f"{what} isn't under any cut-out")

        def screen_rect(points):
            x0, y0, x1, y1 = bbox(points)
            sx, sy = to_screen(x0, y1)
            return (sx, sy, x1 - x0, y1 - y0)

        def window(led):
            x, y = self.on_panel(*self.origin(led))
            hole = nearest(self.holes, x, y, {8.4, 5.1})
            if math.hypot(hole["x"] - x, hole["y"] - y) > 1:
                fail(f"{led} has no window over it")
            return (*to_screen(hole["x"], hole["y"]), hole["drill"])

        out = {"size": (right - left, top - bottom)}

        keys, key_leds = [], []
        for n in range(1, NUM_KEYS + 1):
            x, y = self.on_panel(*self.switch_centre(f"KEY{n}"))
            keys.append(to_screen(x, y))
            if n in KEY_PTH_LEDS:
                continue
            # The LED shining through KEYn's cap sits straight above the switch centre.
            matches = [name for name in self.elements if name.startswith("LED_K")
                       and abs(self.on_panel(*self.origin(name))[0] - x) < TOLERANCE
                       and 3 < self.on_panel(*self.origin(name))[1] - y < 7]
            if len(matches) != 1:
                fail(f"KEY{n} has LEDs {matches}, expected one")
            key_leds.append(to_screen(*self.on_panel(*self.origin(matches[0]))))
        out["keys"], out["key_leds"] = keys, key_leds
        out["key_led_windows"] = [window(KEY_PTH_LEDS[n]) for n in sorted(KEY_PTH_LEDS)]

        # Every key shows through a cut-out: one for the two rows, one for the CHOMPI key, one for
        # play and loop.
        white_row = cutout_holding(*self.on_panel(*self.switch_centre("KEY1")), "KEY1")
        for n in range(1, 26):
            if cutout_holding(*self.on_panel(*self.switch_centre(f"KEY{n}")), f"KEY{n}") is not white_row:
                fail(f"KEY{n} isn't in the keyboard cut-out")
        chompi = cutout_holding(*self.on_panel(*self.switch_centre("KEY26")), "KEY26")
        play_loop = cutout_holding(*self.on_panel(*self.switch_centre("KEY27")), "KEY27")
        if cutout_holding(*self.on_panel(*self.switch_centre("KEY28")), "KEY28") is not play_loop:
            fail("KEY27 and KEY28 aren't in one cut-out")
        out["keyboard_cutout"] = [to_screen(x, y) for x, y in white_row]
        out["chompi_cutout"] = screen_rect(chompi)
        out["play_loop_cutout"] = screen_rect(play_loop)

        encoders = []
        scrub = max(self.holes, key=lambda h: h["drill"])
        scrub_x, scrub_y = self.origin(f"SW{SCRUB_ENCODER}")
        if abs(self.on_panel(scrub_x, scrub_y)[0] - scrub["x"]) > 2 * TOLERANCE:
            fail(f"SW{SCRUB_ENCODER} isn't under the big hole")
        on_lower = lambda x, y: (x + scrub["x"] - scrub_x, y + scrub["y"] - scrub_y)
        for n in range(1, NUM_ENCODERS + 1):
            if n == SCRUB_ENCODER:
                hole = scrub
            else:
                hole = nearest(self.holes, *self.on_panel(*self.origin(f"SW{n}")), {7.7})
            encoders.append((*to_screen(hole["x"], hole["y"]), hole["drill"]))
        out["encoders"] = encoders
        out["encoder_led_windows"] = [window(ENCODER_LEDS[n]) for n in range(1, NUM_ENCODERS + 1)]
        out["encoder5_second_led_window"] = window(ENCODER5_SECOND_LED)

        # The jacks are on the right-hand side, out of sight from above.
        out["line_in_jack"] = to_screen(*on_lower(*self.origin(LINE_IN_JACK)))
        out["phones_jack"] = to_screen(*on_lower(*self.origin(PHONES_JACK)))

        # The USB socket and the SD slot face the player, on the front edge. Their origins are the
        # connectors' mouths, which stick out a little past the panel's outline.
        out["usb_socket"] = to_screen(*self.on_panel(*self.origin(USB_SOCKET)))
        out["sd_slot"] = to_screen(*self.on_panel(*self.origin(SD_SLOT)))

        toggle_x, toggle_y = self.on_panel(*self.origin("SW_NORMAL"))
        toggle = cutout_holding(toggle_x, toggle_y, "the toggle switch")
        out["toggle_slot"] = screen_rect(toggle)

        # What's left are the mic's sound slots.
        used = {id(p) for p in (white_row, chompi, play_loop, toggle)}
        slots = sorted((p for p in cutouts if id(p) not in used), key=lambda p: bbox(p)[0])
        if len(slots) != 5:
            fail(f"expected 5 mic slots, found {len(slots)}")
        out["mic_slots"] = [screen_rect(p) for p in slots]
        return out


def num(v):
    s = f"{v:.3f}".rstrip("0").rstrip(".")
    return (s if "." in s else s + ".0") + "f"


def tup(values):
    return "{" + ", ".join(num(v) for v in values) + "}"


def array(ctype, name, rows, comment, per_line=1):
    lines = [f"// {comment}", f"constexpr {ctype} {name}[{len(rows)}] = {{"]
    for i in range(0, len(rows), per_line):
        lines.append("    " + " ".join(tup(r) + "," for r in rows[i:i + per_line]))
    lines.append("};")
    return "\n".join(lines)


def header(layout):
    w, h = layout["size"]
    parts = [
        "// Generated by tools/extract_panel_layout.py from the CHOMPI board files. Don't edit.",
        "//",
        "// The CHOMPI top panel in millimetres: the origin is the top-left corner of the outline",
        "// and y points down. Circles are {x, y, diameter} and rectangles {x, y, width, height}.",
        "#pragma once",
        "",
        "namespace champi::layout",
        "{",
        "struct Point",
        "{",
        "    float x, y;",
        "};",
        "struct Circle",
        "{",
        "    float x, y, d;",
        "};",
        "struct Rect",
        "{",
        "    float x, y, w, h;",
        "};",
        "",
        f"constexpr float kWidth  = {num(w)};",
        f"constexpr float kHeight = {num(h)};",
        "",
        array("Point", "kKey", layout["keys"], "KEYn's switch centre is kKey[n - 1].", 3),
        "",
        array("Point", "kKeyLed", layout["key_leds"],
              "The LED under KEYn's cap, for KEY1-25.", 3),
        "",
        array("Circle", "kKeyLedWindow", layout["key_led_windows"],
              "The LED windows over KEY26-28 (CHOMPI, play and loop)."),
        "",
        array("Circle", "kEncoder", layout["encoders"],
              "ENCn's shaft hole is kEncoder[n - 1]; ENC5's is the scrub wheel's."),
        "",
        array("Circle", "kEncoderLedWindow", layout["encoder_led_windows"],
              "ENCn's LED window; ENC5 has a second one."),
        f"constexpr Circle kEncoder5SecondLedWindow = {tup(layout['encoder5_second_led_window'])};",
        "",
        "// The line-in and headphone jacks, on the right-hand side of the case.",
        f"constexpr Point kLineInJack = {tup(layout['line_in_jack'])};",
        f"constexpr Point kPhonesJack = {tup(layout['phones_jack'])};",
        "",
        "// The USB-C socket and the micro-SD slot, on the front edge (y past kHeight: they stick out).",
        f"constexpr Point kUsbSocket = {tup(layout['usb_socket'])};",
        f"constexpr Point kSdSlot    = {tup(layout['sd_slot'])};",
        "",
        array("Point", "kKeyboardCutout", layout["keyboard_cutout"],
              "The cut-out the white and black rows show through, corner by corner.", 4),
        f"constexpr Rect kChompiCutout   = {tup(layout['chompi_cutout'])};",
        f"constexpr Rect kPlayLoopCutout = {tup(layout['play_loop_cutout'])};",
        f"constexpr Rect kToggleSlot     = {tup(layout['toggle_slot'])};",
        "",
        array("Rect", "kMicSlots", layout["mic_slots"], "The mic's sound slots."),
        "",
        "} // namespace champi::layout",
        "",
    ]
    return "\n".join(parts)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--chompi", type=Path, default=ROOT / "third_party/CHOMPI")
    parser.add_argument("-o", "--output", type=Path, default=ROOT / "app/panel_layout.h")
    parser.add_argument("--check", action="store_true",
                        help="don't write; fail if the output file isn't up to date")
    args = parser.parse_args()

    text = header(Layout(args.chompi).build())
    if args.check:
        if not args.output.exists() or args.output.read_text() != text:
            fail(f"{args.output} is out of date: run tools/extract_panel_layout.py")
        return
    args.output.write_text(text)


if __name__ == "__main__":
    main()
