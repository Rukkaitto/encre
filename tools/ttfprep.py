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
  GPOS                      104 KB stb cannot reach -- but the KERNING in it is
                            kept, re-expressed as a legacy `kern` table below

--- The kern table this tool synthesises -------------------------------------

GPOS is where Literata keeps its kerning; the face has no legacy `kern` table at
all. stb_truetype does read pair positioning out of GPOS, but only LookupType 2
whose ValueFormat1 is exactly 4, and Literata fails that on both counts: its
`kern` feature is **LookupType 9, Extension Positioning**, and before instancing
its ValueFormat1 is 68 (XAdvance | XAdvDevice). So stbtt_GetGlyphKernAdvance
returned 0 for every pair in the face, measured against the bytes rather than
inferred -- 0 for AV, To, Wa, AW, r., y,, Ta, LT, ov and fi -- and this project
shipped nine months of chrome and a body face with **no kerning anywhere**. At
21px chrome sizes it was invisible. At a 32px reading size it is not: "AVAST"
had an untucked A-V and "To Wander" a hole after the T.

Keeping GPOS would not have fixed it (stb still cannot read an Extension
lookup), and it would have cost 104 KB. So the pairs are read properly --
tools/gposkern.py resolves the extension, both PairPos formats, and OpenType's
first-applicable-subtable rule -- and written back out as a **legacy `kern`
table, format 0**, which is the one format stb reads happily:

    kern header      version 0, nTables 1
    subtable header  version 0, length, coverage 1 (horizontal, format 0)
    format 0 header  nPairs, searchRange, entrySelector, rangeShift
    pairs            left GID, right GID, xAdvance -- 6 bytes each,
                     **ascending by (left << 16) | right**

That ordering is load-bearing: stb bisects on exactly that key
(stbtt__GetGlyphKernInfoAdvance), so an unsorted table does not fail loudly, it
silently misses pairs. It is asserted below rather than assumed.

The table is restricted to the glyphs tools/fontc.py's CODEPOINTS can reach,
which is what makes it affordable: Literata's class-based subtable is a 155x123
grid over 1194 covered glyphs, millions of ordered pairs, and the subset takes
that to ~6000. The cost of the restriction is that a book character outside
ASCII + Latin-1 + the typographic marks gets no kerning -- widening the set is
one list in fontc.py and 6 bytes a pair.

`--keep-gpos` is REFUSED alongside the synthesis, and the reason is one line of
stb: `if (info->gpos) ... else if (info->kern)`. A face with a GPOS table never
reaches the legacy path at all, so keeping GPOS would make the synthesised table
dead bytes AND leave the face unkerned. Pass `--no-kern` if that is really what
is wanted.

Needs fonttools: pip install -r tools/requirements.txt
"""
import argparse
import os
import struct
import sys

from fontTools.ttLib import TTFont
from fontTools.ttLib.tables.DefaultTable import DefaultTable
from fontTools.varLib import instancer

import gposkern
# The subset is fontc.py's, not a second copy of it: the two generators must
# agree about which glyphs the firmware can reach, or the body face kerns pairs
# the chrome ramp does not and nothing says why.
from fontc import CODEPOINTS

# Tables stb_truetype cannot use. GPOS is in the list and the module docstring
# says why -- it is the one entry here that is not obviously dead, and its
# kerning is extracted before it goes.
DROP = ["gvar", "avar", "fvar", "HVAR", "MVAR", "VVAR", "GSUB", "GDEF", "GPOS",
        "STAT", "vmtx", "vhea", "post", "DSIG"]

# stb reads `length` as a uint16 and so does everything else; 14 bytes of header
# plus 6 a pair is the ceiling this implies.
MAX_KERN_PAIRS = (0xFFFF - 14) // 6


def search_range(n_pairs: int):
    """The three legacy binary-search hint fields, per the `kern` spec.

    stb does not read them -- it bisects on nPairs alone -- but FreeType and
    every other consumer do, and a table that lies about its own search geometry
    is a table that works in exactly one reader.
    """
    entry_selector = 0
    while (1 << (entry_selector + 1)) <= n_pairs:
        entry_selector += 1
    if n_pairs == 0:
        return 0, 0, 0
    range_ = 6 * (1 << entry_selector)
    return range_, entry_selector, 6 * n_pairs - range_


def build_kern_table(font, pairs) -> bytes:
    """Pack {(leftName, rightName): xAdvance} into a format-0 `kern` table."""
    gids = font.getReverseGlyphMap()
    records = sorted(((gids[left] << 16) | gids[right], value)
                     for (left, right), value in pairs.items())
    for _, value in records:
        if not -0x8000 <= value <= 0x7FFF:
            sys.exit(f"error: kern adjustment {value} does not fit an int16")
    # The property stb's bisection rests on, checked rather than trusted. sorted()
    # gives it; a duplicate key would not, and a duplicate key is what a face with
    # two glyph names mapping to one GID would produce.
    keys = [k for k, _ in records]
    if keys != sorted(set(keys)):
        sys.exit("error: duplicate kern pair keys; the table would not bisect")

    n = len(records)
    if n > MAX_KERN_PAIRS:
        sys.exit(f"error: {n} kern pairs overflows the subtable's uint16 length "
                 f"(max {MAX_KERN_PAIRS}). Narrow fontc.py's CODEPOINTS.")
    rng, sel, shift = search_range(n)
    out = bytearray(struct.pack(">HH", 0, 1))              # version, nTables
    out += struct.pack(">HHH", 0, 14 + 6 * n, 1)           # version, length, coverage
    out += struct.pack(">HHHH", n, rng, sel, shift)
    for key, value in records:
        out += struct.pack(">HHh", key >> 16, key & 0xFFFF, value)
    return bytes(out)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("ttf")
    ap.add_argument("--axis", action="append", default=[], metavar="TAG=VALUE",
                    help="pin one variation axis, e.g. --axis wght=400")
    ap.add_argument("--keep-gpos", action="store_true",
                    help="retain GPOS. Refused unless --no-kern is also given: "
                         "stb reads GPOS *instead of* `kern`, so a face that "
                         "keeps GPOS ignores the synthesised table and, "
                         "Literata's kern feature being an Extension lookup, "
                         "kerns nothing at all")
    ap.add_argument("--no-kern", action="store_true",
                    help="do not synthesise a legacy `kern` table from GPOS; "
                         "the shipped face then has no kerning")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    if args.keep_gpos and not args.no_kern:
        sys.exit("error: --keep-gpos would make the synthesised `kern` table dead "
                 "bytes.\n"
                 "  stb_truetype's stbtt_GetGlyphKernAdvance is `if (gpos) ... else "
                 "if (kern)`,\n"
                 "  so a face with GPOS never reaches the legacy table -- and it "
                 "cannot read\n"
                 "  Literata's LookupType 9 kern feature out of GPOS either. Add "
                 "--no-kern if\n"
                 "  you want GPOS anyway.")

    axes = {}
    for spec in args.axis:
        if "=" not in spec:
            sys.exit(f"error: --axis wants TAG=VALUE, got {spec!r}")
        tag, value = spec.split("=", 1)
        axes[tag.strip()] = float(value)

    # recalcTimestamp=False, and it is not a detail: fontTools defaults to
    # stamping `head.modified` with the current time on save, so two runs of
    # `make fonts` over an unchanged source produced files differing in seven
    # bytes -- the timestamp and the two checksums derived from it. A committed
    # generated asset that churns for no reason makes "regenerate and diff" --
    # the only cheap check that an asset matches its generator -- useless. With
    # this, the .ttf is as reproducible as the .rfnt files already are.
    font = TTFont(args.ttf, recalcTimestamp=False)
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

    # AFTER instancing and BEFORE the drop: the kern values are variable deltas
    # until the axes are resolved (gposkern raises rather than reading the
    # default instance's numbers), and GPOS is about to be deleted.
    kern_note = "no kern synthesised"
    if not args.no_kern:
        if "kern" in font:
            kern_note = "kept the face's own `kern` table"
        else:
            names = gposkern.subset_glyph_names(font, CODEPOINTS)
            pairs = gposkern.pair_adjustments(font, names)
            if pairs:
                table = DefaultTable("kern")
                table.data = build_kern_table(font, pairs)
                font["kern"] = table
                kern_note = (f"synthesised `kern`: {len(pairs)} pairs over "
                             f"{len(names)} glyphs, {len(table.data)} bytes")
            else:
                kern_note = f"no GPOS kern pairs within the {len(names)}-glyph subset"

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
    print(f"  {kern_note}")


if __name__ == "__main__":
    main()
