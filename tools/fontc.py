#!/usr/bin/env python3
"""fontc: TTF/OTF -> .rfnt bitmap font (1 or 2 bpp, with kerning).

Usage: python3 tools/fontc.py FONT.ttf --size 18 --out out.rfnt
       python3 tools/fontc.py FONT.ttf --pt 11 --weight 500 --autohint --out out.rfnt
       python3 tools/fontc.py FONT.ttf --pt 12 --weight 700 --bpp 2 --out out.rfnt

Exactly one of --pt or --size sets the size, and they are not interchangeable:

  --pt  points at 150 DPI, via set_char_size(pt << 6, pt << 6, 150, 150). This
        is what CrossPoint does, character for character, and it is the unit the
        chrome ramp is authored in (see design/Main.dc.html). ppem works out to
        pt * 150 / 72, so 10pt is 20.83 -> FreeType's own rounding, not ours.
  --size  a pixel size, via set_pixel_sizes. Kept for the Literata body face,
        whose asset predates the pt ramp and whose glyph tables must stay
        byte-identical (the format-2 header fields are additive).

The two paths are deliberately NOT one path with a conversion in front: rounding
a pt size to an integer ppem and calling set_pixel_sizes gives FreeType a
different scale and a different hinting outcome than set_char_size does at the
same nominal size, so a "converted" pt would not reproduce CrossPoint's proven
rendering. Mirror the reference implementation instead of approximating it.

The bundled faces are variable fonts, and their variable *defaults* are not the
weights the UI asks for (Space Grotesk defaults to 300 Light). Left unset, every
chrome glyph renders as a 1px hairline, which is ~0.11 mm and near-invisible on
the ~230 PPI panel. --weight/--opsz pin the variation axes explicitly so a
generated asset is a deliberate choice rather than whatever the font shipped
with; --autohint forces FreeType's own autohinter, which snaps stems to the
pixel grid instead of trusting the face's (mono-hostile) TrueType hints.

--bpp 2 stores anti-aliased coverage instead of a hard threshold. At 12-13px a
stem is 1-1.5px, so thresholding either drops it or leaves a jagged single-pixel
stroke, which is what made the chrome illegible on the X3 panel. Two bits give
four levels, which the panel can actually paint. The bit depth rides in the high
byte of the version word (2 = 1bpp, 2 | (2 << 8) = 2bpp), independent of the
format version in the low byte; the loader accepts both depths and both formats.

The container is format 2, whose header carries the resolved ppem and the pinned
weight after the kern count (see the write below for why). Format 1 assets omit
those two fields and still load.

--coverage-gamma is the transfer curve applied to coverage before it is
quantised, and it is why the chrome no longer renders thinner than the design.
FreeType hands back *linear* coverage: the fraction of the pixel the outline
covers, and nothing else. No shipping text rasteriser puts that on screen
unmodified -- Skia (so Chrome, so the design boards) runs the glyph mask through
a gamma/contrast LUT first, because compositing linear coverage in a non-linear
colour space thins black-on-white type. Quantising FreeType's raw coverage
skipped that stage, and the result measured 88% of the browser's ink mass on the
same string at the same nominal size and weight. The 2-bit quantisation itself
was not the culprit; it is mass-neutral to within 0.2%. The missing curve was.

  gamma 1.0 is the identity and reproduces the pre-correction bytes exactly.
  gamma 2.0 (the default) brings the chrome faces to ~95% of Chrome's ink mass,
    which is as close as four levels reach; the remainder is quantisation floor.
  Past ~2.4 counters begin to silt up at 21px, which is a worse defect than
    being slightly light, so this is not a knob to keep turning.

This is a coverage correction, not a weight change: --weight still says exactly
what the design says, and the outline is the one the design asked for.
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

DEFAULT_COVERAGE_GAMMA = 2.0


def coverage_lut(gamma: float) -> list:
    """8-bit linear coverage -> 2-bit level, 256 entries.

    At gamma == 1.0 this is exactly the arithmetic it replaces,
    `(v * 3 + 127) // 255`: that expression is floor(3v/255 + 127/255) and this
    one is floor(3v/255 + 0.5), and 255k - 3v is an integer so no v can fall
    between the two. The identity holding *bit for bit* is what makes the
    correction auditable -- the only difference in a generated asset is the
    curve, not a second rounding change smuggled in beside it.
    """
    if gamma <= 0:
        sys.exit(f"error: --coverage-gamma must be positive, got {gamma:g}")
    return [min(3, int((v / 255.0) ** (1.0 / gamma) * 3 + 0.5)) for v in range(256)]


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
    # Not an argparse mutually-exclusive group: that reports "one of the
    # arguments --pt --size is required", which does not say which unit a caller
    # should be reaching for. Check by hand and explain the choice.
    ap.add_argument("--pt", type=int, help="size in points at 150 DPI (CrossPoint's unit)")
    ap.add_argument("--size", type=int, help="size in pixels (Literata body face only)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--weight", type=float, help="'wght' variation axis, e.g. 500")
    ap.add_argument("--opsz", type=float, help="'opsz' variation axis, e.g. 12")
    ap.add_argument(
        "--autohint",
        action="store_true",
        help="force FreeType's autohinter (snaps stems to the pixel grid)",
    )
    ap.add_argument(
        "--bpp",
        type=int,
        default=1,
        choices=(1, 2),
        help="bits of coverage per pixel: 1 thresholds, 2 keeps anti-aliasing",
    )
    ap.add_argument(
        "--coverage-gamma",
        type=float,
        default=None,
        help=f"transfer curve on coverage before quantising (default "
             f"{DEFAULT_COVERAGE_GAMMA:g}, 1.0 = none); --bpp 2 only",
    )
    args = ap.parse_args()

    if args.coverage_gamma is not None and args.bpp == 1:
        sys.exit(
            "error: --coverage-gamma applies to 2-bit coverage and --bpp 1 has none.\n"
            "  A 1bpp face is thresholded by FreeType before this tool sees it, so the\n"
            "  flag would be silently doing nothing. Drop it, or pass --bpp 2."
        )
    gamma = DEFAULT_COVERAGE_GAMMA if args.coverage_gamma is None else args.coverage_gamma
    lut = coverage_lut(gamma) if args.bpp == 2 else None

    if args.pt is not None and args.size is not None:
        sys.exit(
            "error: --pt and --size are two different size units; give exactly one.\n"
            "  --pt N    points at 150 DPI (set_char_size) - the chrome ramp\n"
            "  --size N  pixels (set_pixel_sizes) - the Literata body face"
        )
    if args.pt is None and args.size is None:
        sys.exit(
            "error: no size given; --pt or --size is required.\n"
            "  --pt N    points at 150 DPI (set_char_size) - the chrome ramp\n"
            "  --size N  pixels (set_pixel_sizes) - the Literata body face"
        )
    if args.pt is not None and args.pt <= 0:
        sys.exit(f"error: --pt must be positive, got {args.pt}")
    if args.size is not None and args.size <= 0:
        sys.exit(f"error: --size must be positive, got {args.size}")

    requested = {}
    if args.weight is not None:
        requested["wght"] = args.weight
    if args.opsz is not None:
        requested["opsz"] = args.opsz

    face = freetype.Face(args.font)
    # Axes first: FT_Set_Var_Design_Coordinates can reset the active size.
    axis_summary = apply_variations(face, requested)
    if args.pt is not None:
        # 26.6 fixed point, both axes, at 150x150 DPI -- byte for byte what
        # CrossPoint's set_char_size(size << 6, size << 6, 150, 150) does.
        face.set_char_size(args.pt << 6, args.pt << 6, 150, 150)
    else:
        face.set_pixel_sizes(0, args.size)
    # Whatever FreeType actually resolved the request to, reported rather than
    # recomputed: at --pt 10 the nominal 20.83 ppem rounds inside FreeType.
    ppem = face.size.y_ppem

    # At 2bpp the mono target must be off, or FreeType hands back a 1-bit bitmap
    # and there is no anti-aliasing left to quantise.
    load_flags = freetype.FT_LOAD_RENDER
    if args.bpp == 1:
        load_flags |= freetype.FT_LOAD_TARGET_MONO
    if args.autohint:
        load_flags |= freetype.FT_LOAD_FORCE_AUTOHINT

    glyphs, blob = [], bytearray()
    # Reported, not eyeballed: the level histogram and the ink mass are how a
    # change to the coverage curve is judged. `linear_mass` is FreeType's own
    # coverage summed as a pixel area, so the ratio below states exactly how much
    # ink the stored 4-level approximation carries against the outline's true
    # area -- above 1.0 once the gamma curve is on, which is the point.
    hist = [0, 0, 0, 0]
    linear_mass = 0.0
    for cp in CODEPOINTS:
        if face.get_char_index(cp) == 0:
            continue
        face.load_char(chr(cp), load_flags)
        g = face.glyph
        bmp = g.bitmap
        if args.bpp == 2:
            # 8-bit coverage -> 2-bit level, four pixels per byte, MSB-first:
            # pixel `col` occupies bits 6 - 2 * (col % 4). Font::coverage in
            # core/src/font.cpp unpacks exactly this layout.
            row_bytes = (bmp.width * 2 + 7) // 8
            packed = bytearray(row_bytes * bmp.rows)
            for row in range(bmp.rows):
                for col in range(bmp.width):
                    v = bmp.buffer[row * bmp.pitch + col]
                    linear_mass += v / 255.0
                    level = lut[v]
                    hist[level] += 1
                    if level:
                        packed[row * row_bytes + col // 4] |= level << (6 - 2 * (col % 4))
        else:
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

    # Format 2 records what the face IS, not only how it measures: the nominal
    # pixel size FreeType resolved the request to, and the weight the variation
    # axis was pinned to. Both are things only this tool knows, and both had
    # callers that were guessing.
    #
    # ppem: the design states letter-spacing in em, which needs a pixel size to
    # resolve. Without it in the asset, core/ kept its own table of the ramp's
    # sizes -- a second source of truth, covering only the roles that happened
    # to be tracked, checked by nothing.
    #
    # weight: the type ramp's roles name a weight (see core/include/reader/
    # fontset.h), and until the asset declared its own nothing could tell a 400
    # face from a 500 one. That is how Home's author line came to be drawn in
    # the 500 body face against a board that asks for 400.
    #
    # Only the header grew; the glyph and kern tables are byte-identical, so a
    # v1 asset still loads and renders the same (Font::load accepts both, and
    # reports 0/0 for an asset that declares neither).
    weight = int(round(args.weight)) if args.weight is not None else 0
    if not 0 <= weight <= 0xFFFF or not 0 <= ppem <= 0xFFFF:
        sys.exit(f"error: ppem {ppem} / weight {weight} do not fit the v2 header")
    with open(args.out, "wb") as f:
        version = 2 if args.bpp == 1 else 2 | (args.bpp << 8)
        f.write(struct.pack("<4sHHhhhHHH", b"RFNT", version, len(glyphs), ascent, descent,
                            line_gap, min(len(kerns), 0xFFFF), ppem, weight))
        for cp, adv, w, h, xo, yo, off in glyphs:
            f.write(struct.pack("<IhhhhhI", cp, adv, w, h, xo, yo, off))
        for left, right, adj in kerns[:0xFFFF]:
            f.write(struct.pack("<IIi", left, right, adj))
        f.write(bytes(blob))
    # Axes and hinting go in the summary so a generated asset is traceable to
    # the exact rendering settings that produced it.
    size_trait = (
        f"{args.pt}pt@150dpi -> {ppem}px ppem" if args.pt is not None else f"{args.size}px"
    )
    traits = [
        size_trait,
        f"bpp={args.bpp}",
        axis_summary or "static",
        "autohint" if args.autohint else "hinted",
    ]
    if args.bpp == 2:
        traits.append(f"gamma={gamma:g}")
    print(
        f"{args.out}: {len(glyphs)} glyphs, {len(kerns)} kern pairs, "
        f"blob {len(blob)} bytes, {', '.join(t for t in traits if t)}"
    )
    if args.bpp == 2:
        quantised_mass = sum(i * n for i, n in enumerate(hist)) / 3.0
        print(
            f"  levels 0/1/2/3 = {hist[0]}/{hist[1]}/{hist[2]}/{hist[3]}, "
            f"ink mass {quantised_mass:.1f}px vs {linear_mass:.1f}px linear "
            f"({100 * quantised_mass / linear_mass:.1f}%)"
        )


if __name__ == "__main__":
    main()
