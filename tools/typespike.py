#!/usr/bin/env python3
"""Type-rendering comparison sheet for Encre. DECISION-SUPPORT ONLY.

Not part of the build. Nothing imports it, no target invokes it, and no
generated asset depends on it - it exists so a type decision can be argued from
rendered output rather than from taste. It renders the real Home-screen strings
across size / weight / bit-depth combinations onto one contact sheet (1:1 and
4x) and prints, per variant, the share of horizontal ink runs that are exactly
one pixel wide. That number is the whole point: at ~230 PPI a 1px stem is about
0.11 mm and reads as a faint grey hairline rather than a black stroke, so a high
1px share means the text will look washed out on the panel no matter how good it
looks on a monitor.

What it settled (2026-08, Phase 1 follow-up):
  * 16px weight 300 (the variable default, i.e. the bug) -> 81% 1px runs.
  * 16px weight 500 -> 7%. Weight, not size, was the fix; this is what
    tools/fontc.py now pins for the chrome faces.
  * 16px weight 400 with 2-bit anti-aliasing -> 2%, but the grey fringe softens
    letterspaced uppercase and would force the slower grayscale refresh path.
    Rejected for chrome; revisit for book body text in Phase 3.

Run it when a chrome type question comes up again:
    pip install -r tools/requirements.txt pillow
    python3 tools/typespike.py
Writes build/type-comparison.png (build/ is not committed) and prints the table.
Paths are absolute so it can be run from anywhere; edit VARIANTS to ask a new
question.
"""
import freetype
from PIL import Image, ImageDraw

SG = "/Users/lucasgoudin/dev/encre/assets/fonts/SpaceGrotesk.ttf"
LIT = "/Users/lucasgoudin/dev/encre/assets/fonts/Literata.ttf"

# Panel-realistic gray ramps. The X4/X3 do 4 levels via the dual-plane path.
RAMP_1BIT = [0, 255]
RAMP_2BIT = [0, 0x55, 0xAA, 255]
RAMP_4BIT = [i * 17 for i in range(16)]


def render_run(path, text, px, weight, ramp, autohint=True, opsz=None):
    """Render text to a list of (x, y, level) ink pixels plus its extent."""
    face = freetype.Face(path)
    face.set_pixel_sizes(0, px)
    if face.has_multiple_masters:
        coords = [weight] if opsz is None else [opsz, weight]
        try:
            face.set_var_design_coords(coords)
        except Exception:
            pass
    flags = freetype.FT_LOAD_RENDER
    if autohint:
        flags |= freetype.FT_LOAD_FORCE_AUTOHINT
    if len(ramp) == 2:
        flags |= freetype.FT_LOAD_TARGET_MONO

    levels = len(ramp)
    pen, out = 0, []
    ascender = face.size.ascender // 64
    for ch in text:
        face.load_char(ch, flags)
        g = face.glyph
        bmp = g.bitmap
        for row in range(bmp.rows):
            for col in range(bmp.width):
                if len(ramp) == 2:  # 1bpp source
                    byte = bmp.buffer[row * bmp.pitch + col // 8]
                    on = (byte >> (7 - col % 8)) & 1
                    if on:
                        out.append((pen + g.bitmap_left + col,
                                    ascender - g.bitmap_top + row, 0))
                else:  # 8bpp coverage -> quantise to the ramp
                    v = bmp.buffer[row * bmp.pitch + col]
                    idx = round(v / 255 * (levels - 1))
                    if idx:
                        out.append((pen + g.bitmap_left + col,
                                    ascender - g.bitmap_top + row,
                                    ramp[levels - 1 - idx]))
        pen += g.advance.x // 64
    return out, pen, face.size.height // 64


def stem_stats(ink, ramp):
    """Fraction of horizontal ink runs that are a single pixel wide."""
    rows = {}
    for x, y, _ in ink:
        rows.setdefault(y, set()).add(x)
    ones = total = 0
    for y, xs in rows.items():
        xs = sorted(xs)
        i = 0
        while i < len(xs):
            n = 1
            while i + n < len(xs) and xs[i + n] == xs[i + n - 1] + 1:
                n += 1
            if n < 30:
                total += 1
                if n == 1:
                    ones += 1
            i += n
    return (100 * ones / total) if total else 0


VARIANTS = [
    ("A  WAS THE BUG", SG, 16, 300, RAMP_1BIT, "1-bit, variable default (300)"),
    ("B  SHIPPING label face", SG, 16, 500, RAMP_1BIT, "1-bit, weight 500"),
    ("B2 SHIPPING value face", SG, 16, 700, RAMP_1BIT, "1-bit, weight 700"),
    ("C", SG, 18, 500, RAMP_1BIT, "1-bit, 18px weight 500"),
    ("D", SG, 20, 700, RAMP_1BIT, "1-bit, 20px weight 700"),
    ("E", SG, 16, 400, RAMP_2BIT, "2-bit AA (4 levels, panel native)"),
    ("F", SG, 18, 500, RAMP_2BIT, "2-bit AA, 18px weight 500"),
    ("G", SG, 18, 500, RAMP_4BIT, "4-bit AA (16 levels, reference only)"),
]

SAMPLES = ["NOW READING", "87%", "Middlemarch", "CH. 01 — MISS BROOKE", "LIBRARY  12"]

ZOOM = 4
LABEL_W = 300
SHEET_W = 1180
row_h = 44
zoom_h = 96

img = Image.new("L", (SHEET_W, len(VARIANTS) * (row_h + zoom_h + 16) + 70), 255)
d = ImageDraw.Draw(img)
d.text((20, 16), "ENCRE - CHROME TYPE COMPARISON  (Space Grotesk, 1:1 above, 4x zoom below)", fill=0)
d.text((20, 32), "Simulated on a monitor; 4-level gray approximates the panel dual-plane path.", fill=90)

y = 60
for tag, path, px, wt, ramp, desc in VARIANTS:
    ink, _, _ = render_run(path, "".join(SAMPLES), px, wt, ramp)
    pct1 = stem_stats(ink, ramp)
    d.text((20, y + 4), f"{tag}", fill=0)
    d.text((20, y + 18), f"{px}px  w{wt}  {desc}", fill=60)
    d.text((20, y + 32), f"1px runs: {pct1:.0f}%", fill=0 if pct1 > 50 else 90)

    # 1:1 row
    x = LABEL_W
    for s in SAMPLES:
        ink, adv, _ = render_run(path, s, px, wt, ramp)
        for ix, iy, lv in ink:
            px_x, px_y = x + ix, y + 10 + iy
            if 0 <= px_x < SHEET_W:
                img.putpixel((px_x, px_y), lv)
        x += adv + 26

    # 4x zoom of the first two samples
    zx = LABEL_W
    for s in SAMPLES[:3]:
        ink, adv, _ = render_run(path, s, px, wt, ramp)
        for ix, iy, lv in ink:
            for dy in range(ZOOM):
                for dx in range(ZOOM):
                    px_x, px_y = zx + ix * ZOOM + dx, y + row_h + 6 + iy * ZOOM + dy
                    if 0 <= px_x < SHEET_W and px_y < img.height:
                        img.putpixel((px_x, px_y), lv)
        zx += adv * ZOOM + 30
        if zx > SHEET_W - 200:
            break

    y += row_h + zoom_h + 16
    d.line([(0, y - 8), (SHEET_W, y - 8)], fill=200)

img.save("/Users/lucasgoudin/dev/encre/build/type-comparison.png")
print("wrote build/type-comparison.png", img.size)

print("\n1px-run share by variant (lower = more solid strokes):")
for tag, path, px, wt, ramp, desc in VARIANTS:
    ink, _, _ = render_run(path, "".join(SAMPLES), px, wt, ramp)
    print(f"  {tag:20s} {px}px w{wt:3d} {len(ramp):2d} levels -> {stem_stats(ink, ramp):5.1f}%   {desc}")
