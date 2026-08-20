#!/usr/bin/env python3
"""iconc: design-board SVG -> 2-bit anti-aliased icon bitmaps in a C++ header.

Usage: python3 tools/iconc.py --out core/src/icons_data.h
       python3 tools/iconc.py --out core/src/icons_data.h --sheet build/icons.png

The firmware's UI marks used to be hand-authored binary literals. They were
drawn from a *description* of the design's icons rather than from the icons
themselves, and drifted twice: once in shape (the book read as the letters
"OC") and once in size (the chrome type ramp doubled to pt-at-150dpi and the
boards' icons grew with it, while the bitmaps stayed at 13x13). This tool ends
both failure modes the way tools/fontc.py ended them for type: the bytes are
generated from the design's own source, so they cannot disagree with it.

Pipeline, per icon:
  1. The board's `<path>`/`<rect>`/`<circle>` markup, copied verbatim, is
     wrapped in an <svg> carrying that icon's original viewBox and the target
     pixel size. Keeping the viewBox is what makes the size a free parameter --
     the geometry scales, the coordinates are never re-authored.
  2. Headless Chrome rasterises it on an opaque white ground. Chrome is the only
     SVG rasteriser on this machine (no cairosvg/rsvg/inkscape) and it is the
     same renderer tools/compare-design.py measures the design with, so an icon
     is compared against the engine that drew the reference.
  3. Ink coverage (255 - luma) is quantised to 2 bits, 0..3, with fontc.py's
     rounding, and packed MSB-first four pixels per byte.

2 bits, not 1: these are stroke icons at 21-46px with curves, diagonals and
~2px stems. Thresholding them reproduces exactly the staircase that made the
1-bit chrome type illegible on the panel. Icon::coverage/drawIcon in
core/src/icons.cpp unpack this layout, and reader::Plane splits it into the
panel's two grey planes just as glyphs are split.

Do not edit the emitted header; edit the definitions below and rerun `make
icons`.
"""
import argparse
import os
import pathlib
import subprocess
import sys
import tempfile

try:
    from PIL import Image
except ImportError:  # pragma: no cover - tool-only dependency
    sys.exit("error: Pillow is required (pip install -r tools/requirements.txt)")

CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"

# The curated icon set, in one place on purpose: the SVG body, the viewBox it
# was authored in, and the pixel size the firmware wants it at. `source` is the
# board and line the markup was lifted from, so a reviewer can diff it against
# the design without searching; `size` is the width/height that board
# renders it at, recorded because it is *not* always the target below and a
# future pass may want to reconcile the two (see NOTE).
#
# NOTE on sizes: the targets are the ones this change was specified with --
# back/dot/up/down/chevron 23, book 25, folder 46x39, battery 38x21. Four of
# them differ slightly from their board's own attributes (back 25, dot 19,
# chevron 25, book 26). The geometry is the design's either way; only the
# rasterisation scale differs, and it differs by at most 2px.
ICONS = {
    "back": {
        "symbol": "kBack",
        "note": "arrow curving left: the Back button",
        "viewbox": "0 0 16 16",
        "body": (
            '<path d="M6 4L2 8l4 4" stroke="#000000" stroke-width="1.6"></path>'
            '<path d="M2 8h8a4 4 0 0 0 4-4" stroke="#000000" stroke-width="1.6"></path>'
        ),
        "size": (25, 25),
        "source": "design/Library.dc.html",
    },
    "dot": {
        "symbol": "kDot",
        "note": "filled circle: the Confirm button",
        "viewbox": "0 0 10 10",
        "body": '<circle cx="5" cy="5" r="4" fill="#000000"></circle>',
        "size": (19, 19),
        "source": "design/Main.dc.html",
    },
    "up": {
        "symbol": "kUp",
        "note": "stem with a chevron head, pointing up",
        "viewbox": "0 0 16 16",
        "body": '<path d="M8 11V5M5 8l3-3 3 3" stroke="#000000" stroke-width="1.6"></path>',
        "size": (23, 23),
        "source": "design/Main.dc.html",
    },
    "down": {
        "symbol": "kDown",
        "note": "stem with a chevron head, pointing down",
        "viewbox": "0 0 16 16",
        "body": '<path d="M8 5v6M5 8l3 3 3-3" stroke="#000000" stroke-width="1.6"></path>',
        "size": (23, 23),
        "source": "design/Main.dc.html",
    },
    "chevron": {
        "symbol": "kChevron",
        "note": "right-pointing disclosure",
        "viewbox": "0 0 16 16",
        "body": '<path d="M6 3l5 5-5 5" stroke="#000000" stroke-width="2"></path>',
        "size": (25, 25),
        "source": "design/Main.dc.html",
    },
    "book": {
        "symbol": "kBook",
        "note": "open book: the Read action",
        "viewbox": "0 0 16 16",
        "body": (
            '<path d="M8 3.2C6.6 2 4.3 1.7 2 2v11c2.3-.3 4.6 0 6 1.2 1.4-1.2 3.7-1.5 6-1.2V2c-2.3-.3-4.6 0-6 1.2z"'
            ' stroke="#000000" stroke-width="1.4"></path>'
            '<path d="M8 3.2v11" stroke="#000000" stroke-width="1.4"></path>'
        ),
        "size": (26, 26),
        "source": "design/Main.dc.html",
    },
    "folder": {
        "symbol": "kFolder",
        "note": "folder: a Library directory row",
        "viewbox": "0 0 26 22",
        "body": (
            '<path d="M1 4a2 2 0 0 1 2-2h6l3 3h11a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V4z"'
            ' stroke="#000000" stroke-width="1.8"></path>'
        ),
        "size": (46, 39),
        "source": "design/Library.dc.html",
    },
    "battery": {
        "symbol": "kBattery",
        "note": "the header band's charge cell",
        "viewbox": "0 0 22 12",
        "body": (
            '<rect x="0.5" y="0.5" width="18" height="11" stroke="#000000"></rect>'
            '<rect x="2" y="2" width="14" height="8" fill="#000000"></rect>'
            '<rect x="19.5" y="3.5" width="2" height="5" fill="#000000"></rect>'
        ),
        "size": (38, 21),
        "source": "design/Main.dc.html",
    },
}

# Chrome will not open a window smaller than a platform minimum, and a
# screenshot of a clamped window is the wrong size. Render into a comfortably
# larger frame with the SVG pinned at the origin and crop, so the icon's own
# pixel grid is untouched by the frame.
PAD = 60


def rasterise(name, spec, tmpdir):
    """Rasterise one icon to an (w, h) list-of-rows of 8-bit ink coverage."""
    w, h = spec["size"]
    page = (
        '<!doctype html><meta charset="utf-8">'
        "<style>html,body{margin:0;padding:0;background:#fff}"
        "svg{position:absolute;left:0;top:0;display:block}</style>"
        f'<svg width="{w}" height="{h}" viewBox="{spec["viewbox"]}" fill="none">'
        f'{spec["body"]}</svg>'
    )
    html = tmpdir / f"{name}.html"
    html.write_text(page)
    png = tmpdir / f"{name}.png"
    subprocess.run(
        [CHROME, "--headless", "--disable-gpu", "--force-device-scale-factor=1",
         "--hide-scrollbars", "--default-background-color=FFFFFFFF",
         # Same reason as tools/compare-design.py: without a virtual-time budget
         # the screenshot can fire before the page has painted.
         "--virtual-time-budget=4000",
         f"--window-size={w + PAD},{h + PAD}", f"--screenshot={png}",
         html.as_uri()],
        check=True, capture_output=True)
    if not png.exists():
        sys.exit(f"error: Chrome produced no screenshot for {name}")
    im = Image.open(png).convert("L").crop((0, 0, w, h))
    px = im.load()
    # Ink, not light: a black stroke on white paper is coverage 255.
    return [[255 - px[x, y] for x in range(w)] for y in range(h)]


def quantise(cov):
    """8-bit ink coverage -> 2-bit level, with fontc.py's rounding."""
    return [[(c * 3 + 127) // 255 for c in row] for row in cov]


def pack(levels, w):
    """2-bit levels -> bytes, MSB-first, four pixels per byte."""
    row_bytes = (w * 2 + 7) // 8
    out = bytearray(row_bytes * len(levels))
    for y, row in enumerate(levels):
        for x, level in enumerate(row):
            if level:
                out[y * row_bytes + x // 4] |= level << (6 - 2 * (x % 4))
    return bytes(out), row_bytes


def contact_sheet(rendered, path, zoom=6):
    """1:1 and zoomed strips of every icon, for eyeballing the set as a set."""
    cell_w = max(w for w, _ in (spec["size"] for spec in ICONS.values())) + 8
    tiles = []
    for name, levels in rendered.items():
        h, w = len(levels), len(levels[0])
        im = Image.new("L", (w, h), 255)
        px = im.load()
        for y in range(h):
            for x in range(w):
                px[x, y] = (255, 170, 85, 0)[levels[y][x]]
        tiles.append((name, im))
    # Two rows: every icon at 1:1, then every icon at `zoom`x nearest-neighbour,
    # which is the only way to actually see where the intermediate greys land.
    row_h = max(im.height for _, im in tiles)
    zoom_w = sum(im.width * zoom + 8 for _, im in tiles)
    sheet = Image.new("L", (max(cell_w * len(tiles), zoom_w), row_h + 8 + row_h * zoom + 16), 255)
    x = 4
    for _, im in tiles:
        sheet.paste(im, (x, 4))
        x += cell_w
    x = 4
    for _, im in tiles:
        sheet.paste(im.resize((im.width * zoom, im.height * zoom), Image.NEAREST),
                    (x, row_h + 12))
        x += im.width * zoom + 8
    pathlib.Path(path).parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path)
    return sheet.size


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, help="C++ header to write")
    ap.add_argument("--sheet", help="also write a 1:1 + 6x contact sheet PNG here")
    args = ap.parse_args()

    if not pathlib.Path(CHROME).exists():
        sys.exit(f"error: Chrome not found at {CHROME}")

    rendered, blobs = {}, {}
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        for name, spec in ICONS.items():
            levels = quantise(rasterise(name, spec, tmp))
            rendered[name] = levels
            blobs[name] = pack(levels, spec["size"][0])

    lines = [
        "// Generated from the design boards' inline SVG by tools/iconc.py - do not edit.",
        "// Regenerate with: make icons",
        "//",
        "// 2-bit ink coverage per pixel (0..3), packed MSB-first four pixels per",
        "// byte, exactly as .rfnt v2 glyph bitmaps are. Icon::coverage in",
        "// core/src/icons.cpp unpacks it; reader::Plane splits it into the panel's",
        "// two grey planes.",
        "#pragma once",
        "#include <cstdint>",
        "",
        "namespace reader::icons::data {",
    ]
    for name, spec in ICONS.items():
        w, h = spec["size"]
        blob, row_bytes = blobs[name]
        sym = spec["symbol"]
        hist = [0, 0, 0, 0]
        for row in rendered[name]:
            for level in row:
                hist[level] += 1
        lines += [
            "",
            f"// {name}: {spec['note']}",
            f"// {spec['source']}, viewBox \"{spec['viewbox']}\", drawn {w}x{h}"
            ,
            f"// coverage levels 0/1/2/3: {hist[0]}/{hist[1]}/{hist[2]}/{hist[3]} px",
            f"inline constexpr int {sym}W = {w};",
            f"inline constexpr int {sym}H = {h};",
            f"inline constexpr uint8_t {sym}Bits[] = {{",
        ]
        for y in range(h):
            row = blob[y * row_bytes:(y + 1) * row_bytes]
            lines.append("    " + " ".join(f"0x{b:02X}," for b in row))
        lines.append("};")
    lines += ["", "}  // namespace reader::icons::data", ""]

    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines))

    total = 0
    for name, spec in ICONS.items():
        w, h = spec["size"]
        blob, _ = blobs[name]
        total += len(blob)
        hist = [0, 0, 0, 0]
        for row in rendered[name]:
            for level in row:
                hist[level] += 1
        print(f"{name:8s} {w:2d}x{h:<2d} 2bpp {len(blob):4d} bytes  "
              f"levels 0/1/2/3 = {hist[0]}/{hist[1]}/{hist[2]}/{hist[3]}  "
              f"viewBox {spec['viewbox']!r} <- {spec['source']}")
    rel = os.path.relpath(out).replace(os.sep, "/")
    print(f"{rel}: {len(ICONS)} icons, {total} bytes of bitmap")
    if args.sheet:
        size = contact_sheet(rendered, args.sheet, zoom=6)
        print(f"{args.sheet}: contact sheet {size[0]}x{size[1]} (1:1 row, then 6x)")


if __name__ == "__main__":
    main()
