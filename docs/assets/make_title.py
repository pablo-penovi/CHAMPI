"""Writes docs/assets/champi-title.svg, the README title: ANSI Shadow ASCII art, one colour per letter.

Run from the repo root: python3 docs/assets/make_title.py docs/assets/champi-title.svg
"""
import sys
from xml.sax.saxutils import escape

LETTERS = {
    "C": [" ██████╗", "██╔════╝", "██║     ", "██║     ", "╚██████╗", " ╚═════╝"],
    "H": ["██╗  ██╗", "██║  ██║", "███████║", "██╔══██║", "██║  ██║", "╚═╝  ╚═╝"],
    "A": [" █████╗ ", "██╔══██╗", "███████║", "██╔══██║", "██║  ██║", "╚═╝  ╚═╝"],
    "M": ["███╗   ███╗", "████╗ ████║", "██╔████╔██║", "██║╚██╔╝██║", "██║ ╚═╝ ██║", "╚═╝     ╚═╝"],
    "P": ["██████╗ ", "██╔══██╗", "██████╔╝", "██╔═══╝ ", "██║     ", "╚═╝     "],
    "I": ["██╗", "██║", "██║", "██║", "██║", "╚═╝"],
}

# Face colour and shadow colour per letter, from the mushroom: spark orange, tongue pink, caramel.
# Mid tones, so they read on GitHub's light and dark backgrounds alike.
COLOURS = [
    ("#F5A623", "#B86E0C"),  # C
    ("#F0727F", "#B23A4A"),  # H
    ("#C98B52", "#7A5030"),  # A
    ("#F5A623", "#B86E0C"),  # M
    ("#F0727F", "#B23A4A"),  # P
    ("#C98B52", "#7A5030"),  # I
]
BOUNCE = [0, -10, 4, -6, 6, -12]  # each letter's vertical offset, in px

FONT_SIZE = 22
CHAR_W = FONT_SIZE * 0.6  # monospace advance
LINE_H = FONT_SIZE * 1.0
GAP = 1.4  # columns between letters, a little slack for fonts with wider box glyphs
PAD_X, TOP = 46, 48


def tspans(line, face, shadow):
    """Splits a row into runs of block characters (face) and box lines (shadow)."""
    out, run, kind = [], "", None
    for ch in line:
        k = "face" if ch == "█" else "shadow"
        if ch == " ":
            k = kind  # spaces join the current run
        if k != kind and run:
            out.append((kind, run))
            run = ""
        kind = k or "face"
        run += ch
    if run:
        out.append((kind, run))
    return "".join(
        f'<tspan fill="{face if k == "face" else shadow}">{escape(t)}</tspan>' for k, t in out
    )


def main(path):
    parts, x = [], PAD_X
    for i, letter in enumerate("CHAMPI"):
        rows = LETTERS[letter]
        face, shadow = COLOURS[i]
        for r, row in enumerate(rows):
            y = TOP + BOUNCE[i] + (r + 1) * LINE_H
            parts.append(f'<text x="{x:.1f}" y="{y:.1f}">{tspans(row, face, shadow)}</text>')
        x += (len(rows[0]) + GAP) * CHAR_W
    width = x - GAP * CHAR_W + PAD_X
    height = TOP + 6 * LINE_H + 40

    # Sparks, like the ones around the mushroom: three strokes in each top corner.
    def spark(x1, y1, x2, y2):
        return (f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="#F5A623" '
                f'stroke-width="5" stroke-linecap="round"/>')

    w = round(width)
    sparks = [
        spark(14, 30, 30, 42), spark(8, 58, 28, 60), spark(30, 8, 38, 26),
        spark(w - 14, 30, w - 30, 42), spark(w - 8, 58, w - 28, 60), spark(w - 30, 8, w - 38, 26),
    ]

    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{round(height)}" viewBox="0 0 {w} {round(height)}" role="img" aria-label="CHAMPI">
<title>CHAMPI</title>
<g font-family="'DejaVu Sans Mono', Menlo, Consolas, 'Liberation Mono', 'Courier New', monospace" font-size="{FONT_SIZE}" font-weight="bold" xml:space="preserve">
{chr(10).join(parts)}
</g>
{chr(10).join(sparks)}
</svg>
"""
    open(path, "w", encoding="utf-8").write(svg)


if __name__ == "__main__":
    main(sys.argv[1])
