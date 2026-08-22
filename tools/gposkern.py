#!/usr/bin/env python3
"""GPOS pair kerning, read for real, for a glyph subset.

Both generators need the same thing and neither of the libraries they are built
on will give it to them:

  - tools/ttfprep.py ships a TTF that stb_truetype rasterises at runtime.
    stb reads pair positioning out of GPOS, but ONLY LookupType 2 whose
    ValueFormat1 is exactly 4 (XAdvance alone). Literata's `kern` feature is
    **LookupType 9, Extension Positioning**, and before instancing its
    ValueFormat1 is 68 (XAdvance | XAdvDevice) -- so stb misses it twice over.
    ttfprep synthesises a legacy `kern` table from what this module returns,
    which stb does read.
  - tools/fontc.py pre-renders a bitmap ramp with FreeType, whose
    FT_Get_Kerning reads ONLY the legacy `kern` table. Space Grotesk has none
    (its kerning is GPOS LookupType 2, ValueFormat1 68), so FreeType reported
    nothing and every committed .rfnt carried zero kern pairs.

So the pairs are read here, once, straight out of the GPOS structures, and each
tool converts them into whatever its own container wants.

--- What "read for real" means -----------------------------------------------

The rules below are OpenType's, and they are what a shaper (so HarfBuzz, so
Chrome, so the design boards) actually applies. Getting any of them wrong yields
a table that looks like kerning and is not:

  - **LookupType 9 is resolved**, to the LookupType 2 subtable underneath.
    Skipping it is the entire reason this project had no kerning.
  - **Both PairPos formats are handled.** Format 1 lists explicit pairs; format
    2 is class-based, and in a modern face **most of the real kerning lives in
    the class-based subtable** -- Literata's format-1 subtable covers 21520
    explicit pairs, but its format-2 grid is 155x123 classes over 1194 covered
    glyphs. Reading only format 1 would look like "kerning works" while missing
    the bulk of it.
  - **Within one lookup, the FIRST applicable subtable wins** and the rest are
    not consulted. Applicable means: format 1, left in Coverage AND right in
    that left's PairSet; format 2, left in Coverage -- a format-2 subtable
    applies to *every* pair whose first glyph it covers, because class 0 is
    "everything else", so a zero it yields is a deliberate zero and stops the
    search. This is also exactly what stb does, which is why a face stb can
    already read and a synthesised table agree.
  - **Across lookups, adjustments SUM.** Each lookup in a feature is its own
    pass. Both faces here have exactly one kern lookup, so this is the branch
    that never fires today -- it is written down because "first match wins"
    would be silently wrong for a face that has two.

--- The subset is the whole economy -----------------------------------------

A class-based subtable expanded to explicit pairs is enormous: Literata's
155x123 grid over 1194 covered glyphs is millions of ordered pairs. Only pairs
whose BOTH glyphs can be reached matter, so every caller passes the glyph names
it can actually draw and nothing else is expanded. That turns "a table too big
to ship" into a few thousand pairs.

Needs fonttools: pip install -r tools/requirements.txt
"""

# ValueRecord flags, from the OpenType GPOS spec. XAdvance is the only one a
# horizontal kern needs; XAdvDevice is the variable-font delta reference, and a
# caller that has not resolved it is asking for the wrong numbers (see below).
VF_X_ADVANCE = 0x0004
VF_X_ADV_DEVICE = 0x0040


class UnresolvedVariations(Exception):
    """A ValueRecord still refers to an ItemVariationStore.

    Raised rather than papered over. A variable font's kern values are a default
    plus per-axis deltas, and reading the default alone gives the numbers for
    whatever instance the face happens to default to -- Space Grotesk defaults
    to 300 Light, which is not a weight this project ships. Pin the axes first
    (fontTools' instancer.instantiateVariableFont resolves these and drops the
    device tables, taking ValueFormat1 from 68 to 4).
    """


def _x_advance(value, value_format):
    """The XAdvance of one ValueRecord, in font design units."""
    if value is None:
        return 0
    if value_format & VF_X_ADV_DEVICE:
        raise UnresolvedVariations(
            "GPOS ValueFormat1 %d still carries XAdvDevice: the kern values are "
            "variable-font deltas, not numbers. Instance the axes first." % value_format
        )
    if not value_format & VF_X_ADVANCE:
        return 0
    return getattr(value, "XAdvance", 0) or 0


def _matcher(subtable):
    """A `f(left, right) -> (applies, xAdvance)` for one PairPos subtable."""
    if subtable.Format == 1:
        # Explicit pairs. PairSet[i] belongs to Coverage.glyphs[i], which is the
        # coverage ORDER -- zipping them is the index relationship, not a guess.
        table = {}
        for name, pairset in zip(subtable.Coverage.glyphs, subtable.PairSet):
            row = {}
            for record in pairset.PairValueRecord:
                row[record.SecondGlyph] = _x_advance(record.Value1, subtable.ValueFormat1)
            table[name] = row

        def match(left, right):
            row = table.get(left)
            if row is None or right not in row:
                return False, 0
            return True, row[right]

        return match

    if subtable.Format == 2:
        covered = set(subtable.Coverage.glyphs)
        classes1 = subtable.ClassDef1.classDefs if subtable.ClassDef1 else {}
        classes2 = subtable.ClassDef2.classDefs if subtable.ClassDef2 else {}
        grid = []
        for class1 in subtable.Class1Record:
            grid.append([_x_advance(c.Value1, subtable.ValueFormat1)
                         for c in class1.Class2Record])

        def match(left, right):
            if left not in covered:
                return False, 0
            c1 = classes1.get(left, 0)
            c2 = classes2.get(right, 0)
            # A class index past the declared count is a malformed subtable;
            # treat it as not applying rather than indexing off the grid.
            if c1 >= len(grid) or c2 >= len(grid[c1]):
                return False, 0
            # APPLIES even at zero: class 0 is "every other glyph", so a
            # covered left glyph is always positioned by this subtable and the
            # next one in the lookup must not be consulted.
            return True, grid[c1][c2]

        return match

    # Format 3 does not exist for PairPos; anything else is a face we do not
    # understand, and guessing is worse than reporting nothing.
    return lambda left, right: (False, 0)


def _lookups(gpos, feature_tag):
    """The `feature_tag` feature's PairPos subtables, grouped per lookup.

    Extension lookups are resolved here, which is the fix this whole module
    exists for. A lookup contributes nothing if it holds no pair positioning --
    the `kern` feature legitimately also carries cursive or mark lookups in some
    faces.
    """
    indices = []
    for record in gpos.FeatureList.FeatureRecord:
        if record.FeatureTag != feature_tag:
            continue
        for i in record.Feature.LookupListIndex:
            if i not in indices:
                indices.append(i)

    out = []
    for i in sorted(indices):
        lookup = gpos.LookupList.Lookup[i]
        subtables = []
        for subtable in lookup.SubTable:
            if lookup.LookupType == 9:  # Extension Positioning
                if subtable.ExtensionLookupType == 2:
                    subtables.append(subtable.ExtSubTable)
            elif lookup.LookupType == 2:  # PairPos
                subtables.append(subtable)
        if subtables:
            out.append(subtables)
    return out


def pair_adjustments(font, glyph_names, feature_tag="kern"):
    """{(left, right): xAdvance} in font design units, for `glyph_names` only.

    `font` is a fontTools TTFont whose variation axes are already pinned (see
    UnresolvedVariations). `glyph_names` is the subset both sides of a pair must
    lie in. Zero adjustments are omitted -- they are the majority of a class
    grid and they are what the absence of a record already means.

    Ordered pairs: (A, V) and (V, A) are different keys and a face routinely
    kerns only one of them.
    """
    if "GPOS" not in font:
        return {}
    gpos = font["GPOS"].table
    if gpos is None or gpos.LookupList is None or gpos.FeatureList is None:
        return {}

    lookups = [[_matcher(s) for s in subtables]
               for subtables in _lookups(gpos, feature_tag)]
    if not lookups:
        return {}

    # Sorted so the walk -- and so the emitted table, and so the reported
    # numbers -- are reproducible rather than dependent on set iteration order.
    names = sorted(glyph_names)
    pairs = {}
    for left in names:
        for right in names:
            total = 0
            for subtables in lookups:
                for match in subtables:
                    applies, value = match(left, right)
                    if applies:
                        total += value
                        break  # first applicable subtable in this lookup wins
            if total:
                pairs[(left, right)] = total
    return pairs


def subset_glyph_names(font, codepoints):
    """The glyph names `codepoints` reach through the font's Unicode cmap.

    Codepoints the face has no glyph for are skipped, exactly as fontc.py skips
    them when building its glyph table, so the two subsets are the same subset.
    """
    cmap = font.getBestCmap()
    return {cmap[cp] for cp in codepoints if cp in cmap}
