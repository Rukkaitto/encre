#!/usr/bin/env python3
"""ttfprep: a variable TTF -> the static instance stb_truetype actually reads.

    python3 tools/ttfprep.py assets/fonts/Literata.ttf \
        --axis wght=400 --axis opsz=12 --out assets/built/literata_body.ttf

Unlike tools/fontc.py this does NOT rasterise anything. Body text is rasterised
at runtime from a TTF in flash (see core/include/reader/scalablefont.h), so what
ships is a font file, not a bitmap set -- and the only question is which bytes of
the file the firmware can use.

**stb_truetype does not implement OpenType variations at all.** It reads `glyf`
and ignores `gvar`, `avar`, `fvar`, `HVAR` and `MVAR`, which means a variable
font in flash renders as its DEFAULT instance and the 573 KB of variation deltas
are bytes the device can never turn into a pixel. On Literata that is 60% of the
file. This tool resolves the axes for real -- so the shipped face is the one that
was asked for rather than whatever the default happened to be -- and then drops
what is left over.

Dropped, and why each:
  gvar avar fvar HVAR MVAR  variation data, consumed by the instancing above
  GSUB GDEF STAT            OpenType layout stb does not read
  vmtx vhea VVAR            vertical metrics; nothing here sets vertical text
  post                      glyph NAMES (18 KB), which nothing looks up
  GPOS                      104 KB of kerning stb cannot reach -- see below

**GPOS is dropped, and it was nearly kept.** It is where Literata keeps its
kerning (the face has no legacy `kern` table at all) and stb_truetype does read
pair positioning out of GPOS -- but only LookupType 2, PairPos, formats 1 and 2.
Literata wraps its `kern` feature in **LookupType 9, Extension Positioning**,
which stb does not implement, so stbtt_GetGlyphKernAdvance returns 0 for every
pair in the face. Measured against the bytes rather than inferred: 0 for AV, To,
Wa, AW, r., y,, Ta, LT, ov and fi, out of the ORIGINAL file as well as this one,
so keeping the table would have been 104 KB of flash for exactly nothing.

Space Grotesk is the same, which is why the chrome ramp has zero kern pairs in
all eleven committed .rfnt assets: FreeType's get_kerning reads only the legacy
`kern` table, and neither face has one. So this is a gap the whole project
already has, not one body text introduces.

`--keep-gpos` retains it, for a face whose kern feature is a plain PairPos
lookup or for the day something here can read an Extension one. The real fix is
to SYNTHESISE a legacy `kern` table here from the GPOS pairs, which stb reads
happily and which would cost a fraction of 104 KB; that is typesetting work and
belongs with the rest of it.

Needs fonttools: pip install -r tools/requirements.txt
"""
import argparse
import os
import sys

from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

# Tables stb_truetype cannot use. GPOS is in the list and the module docstring
# says why -- it is the one entry here that is not obviously dead.
DROP = ["gvar", "avar", "fvar", "HVAR", "MVAR", "VVAR", "GSUB", "GDEF", "GPOS",
        "STAT", "vmtx", "vhea", "post", "DSIG"]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("ttf")
    ap.add_argument("--axis", action="append", default=[], metavar="TAG=VALUE",
                    help="pin one variation axis, e.g. --axis wght=400")
    ap.add_argument("--keep-gpos", action="store_true",
                    help="retain GPOS; only useful for a face whose kern feature "
                         "is a plain PairPos lookup (Literata's is not)")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    axes = {}
    for spec in args.axis:
        if "=" not in spec:
            sys.exit(f"error: --axis wants TAG=VALUE, got {spec!r}")
        tag, value = spec.split("=", 1)
        axes[tag.strip()] = float(value)

    font = TTFont(args.ttf)
    before = os.path.getsize(args.ttf)

    if axes:
        if "fvar" not in font:
            sys.exit(f"error: {args.ttf} is not a variable font, so --axis has "
                     f"nothing to pin. Drop the flags or pick another face.")
        available = {a.axisTag for a in font["fvar"].axes}
        unknown = sorted(set(axes) - available)
        if unknown:
            sys.exit(f"error: {args.ttf} has no axis {', '.join(unknown)} "
                     f"(it has {', '.join(sorted(available))})")
        # Every axis is pinned explicitly, never left to the face's default --
        # the same rule `make fonts` states for fontc.py, and for the same
        # reason: relying on a variable default once gave the chrome Space
        # Grotesk Light and 1px hairline stems.
        missing = sorted(available - set(axes))
        if missing:
            sys.exit(f"error: {args.ttf} axis {', '.join(missing)} left unpinned. "
                     f"A face's variable default is not a design decision; state it.")
        instancer.instantiateVariableFont(font, axes, inplace=True,
                                         updateFontNames=False)

    dropped = []
    for tag in [t for t in DROP if not (t == "GPOS" and args.keep_gpos)]:
        if tag in font:
            dropped.append(tag)
            del font[tag]

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    font.save(args.out)
    after = os.path.getsize(args.out)
    axis_note = ", ".join(f"{t}={v:g}" for t, v in sorted(axes.items())) or "static"
    print(f"{args.out}: {after} bytes ({axis_note}); "
          f"was {before}, saved {before - after} "
          f"({100 * (before - after) // before}%); dropped {', '.join(dropped)}")


if __name__ == "__main__":
    main()
