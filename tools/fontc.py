#!/usr/bin/env python3
"""fontc: TTF/OTF -> .rfnt v1 bitmap font (1bpp, mono-rendered, with kerning).

Usage: python3 tools/fontc.py FONT.ttf --size 18 --out out.rfnt
       python3 tools/fontc.py FONT.ttf --size 16 --weight 500 --autohint --out out.rfnt

The bundled faces are variable fonts, and their variable *defaults* are not the
weights the UI asks for (Space Grotesk defaults to 300 Light). Left unset, every
chrome glyph renders as a 1px hairline, which is ~0.11 mm and near-invisible on
the ~230 PPI panel. --weight/--opsz pin the variation axes explicitly so a
generated asset is a deliberate choice rather than whatever the font shipped
with; --autohint forces FreeType's own autohinter, which snaps stems to the
pixel grid instead of trusting the face's (mono-hostile) TrueType hints.

The output format is unchanged by any of this: .rfnt v1, byte-for-byte the same
layout, so the C++ loader is untouched.
"""
import argparse
import struct
import sys

import freetype

# ASCII + Latin-1 + typographic set used by the UI and books, plus U+FFFD
# REPLACEMENT CHARACTER so malformed UTF-8 (which the decoder turns into
# U+FFFD) renders as a visible box instead of vanishing. Appended last so
# adding it cannot shift any existing glyph's record or bitmap offset.
CODEPOINTS = (
    list(range(0x20, 0x7F))
    + list(range(0xA0, 0x100))
    + [0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026, 0x2039, 0x203A]
    + [0xFFFD]
)


def apply_variations(face, requested: dict) -> str:
    """Pin the face's variation axes to `requested` {tag: value}.

    Axis order is the face's own (Literata is opsz,wght; Space Grotesk is wght
    only), so it is queried rather than assumed. Axes the caller did not ask for
    keep their current design coordinate. Returns a human-readable summary of
    the resolved coordinates, or "" for a static face.
    """
    if not face.has_multiple_masters:
        if requested:
            name = face.family_name
            name = name.decode() if isinstance(name, bytes) else name
            tags = ", ".join(sorted(requested))
            sys.exit(f"error: {name} is not a variable font; cannot set {tags}")
        return ""

    info = [(a.tag, a.minimum, a.maximum) for a in face.get_variation_info().axes]
    axes = [tag for tag, _, _ in info]

    unknown = [t for t in requested if t not in axes]
    if unknown:
        sys.exit(
            f"error: axis {', '.join(sorted(unknown))} not present on this face; "
            f"it has {', '.join(axes)}"
        )
    # FreeType clamps out-of-range coordinates silently, which would hand back a
    # face that is not the weight the caller asked for. Refuse instead.
    for tag, lo, hi in info:
        if tag in requested and not lo <= requested[tag] <= hi:
            sys.exit(f"error: {tag}={requested[tag]:g} is outside this face's range {lo:g}..{hi:g}")

    coords = [
        float(requested.get(tag, current))
        for tag, current in zip(axes, face.get_var_design_coords())
    ]
    face.set_var_design_coords(coords)
    return " ".join(f"{tag}={value:g}" for tag, value in zip(axes, coords))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("font")
    ap.add_argument("--size", type=int, required=True, help="pixel size")
    ap.add_argument("--out", required=True)
    ap.add_argument("--weight", type=float, help="'wght' variation axis, e.g. 500")
    ap.add_argument("--opsz", type=float, help="'opsz' variation axis, e.g. 12")
    ap.add_argument(
        "--autohint",
        action="store_true",
        help="force FreeType's autohinter (snaps stems to the pixel grid)",
    )
    args = ap.parse_args()

    requested = {}
    if args.weight is not None:
        requested["wght"] = args.weight
    if args.opsz is not None:
        requested["opsz"] = args.opsz

    face = freetype.Face(args.font)
    # Axes first: FT_Set_Var_Design_Coordinates can reset the active size.
    axis_summary = apply_variations(face, requested)
    face.set_pixel_sizes(0, args.size)

    load_flags = freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO
    if args.autohint:
        load_flags |= freetype.FT_LOAD_FORCE_AUTOHINT

    glyphs, blob = [], bytearray()
    for cp in CODEPOINTS:
        if face.get_char_index(cp) == 0:
            continue
        face.load_char(chr(cp), load_flags)
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
    # Axes and hinting go in the summary so a generated asset is traceable to
    # the exact rendering settings that produced it.
    traits = [f"{args.size}px", axis_summary or "static", "autohint" if args.autohint else "hinted"]
    print(
        f"{args.out}: {len(glyphs)} glyphs, {len(kerns)} kern pairs, "
        f"blob {len(blob)} bytes, {', '.join(t for t in traits if t)}"
    )


if __name__ == "__main__":
    main()
