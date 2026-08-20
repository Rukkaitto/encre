#!/usr/bin/env python3
"""fontc: TTF/OTF -> .rfnt v1 bitmap font (1bpp, mono-rendered, with kerning).

Usage: python3 tools/fontc.py FONT.ttf --size 18 --out out.rfnt
"""
import argparse
import struct

import freetype

# ASCII + Latin-1 + typographic set used by the UI and books.
CODEPOINTS = (
    list(range(0x20, 0x7F))
    + list(range(0xA0, 0x100))
    + [0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026, 0x2039, 0x203A]
)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("font")
    ap.add_argument("--size", type=int, required=True, help="pixel size")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    face = freetype.Face(args.font)
    face.set_pixel_sizes(0, args.size)

    glyphs, blob = [], bytearray()
    for cp in CODEPOINTS:
        if face.get_char_index(cp) == 0:
            continue
        face.load_char(chr(cp), freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
        g = face.glyph
        bmp = g.bitmap
        row_bytes = (bmp.width + 7) // 8
        packed = bytearray(row_bytes * bmp.rows)
        for row in range(bmp.rows):
            src = bmp.buffer[row * bmp.pitch : row * bmp.pitch + row_bytes]
            packed[row * row_bytes : (row + 1) * row_bytes] = bytes(src)
        glyphs.append(
            (cp, g.advance.x // 64, bmp.width, bmp.rows, g.bitmap_left, g.bitmap_top, len(blob))
        )
        blob += packed

    kerns = []
    if face.has_kerning:
        present = [g[0] for g in glyphs]
        for left in present:
            for right in present:
                k = face.get_kerning(chr(left), chr(right)).x // 64
                if k:
                    kerns.append((left, right, k))

    ascent = face.size.ascender // 64
    descent = face.size.descender // 64  # negative
    line_gap = (face.size.height // 64) - ascent + descent

    with open(args.out, "wb") as f:
        f.write(struct.pack("<4sHHhhhH", b"RFNT", 1, len(glyphs), ascent, descent, line_gap, min(len(kerns), 0xFFFF)))
        for cp, adv, w, h, xo, yo, off in glyphs:
            f.write(struct.pack("<IhhhhhI", cp, adv, w, h, xo, yo, off))
        for left, right, adj in kerns[:0xFFFF]:
            f.write(struct.pack("<IIi", left, right, adj))
        f.write(bytes(blob))
    print(f"{args.out}: {len(glyphs)} glyphs, {len(kerns)} kern pairs, blob {len(blob)} bytes")


if __name__ == "__main__":
    main()
