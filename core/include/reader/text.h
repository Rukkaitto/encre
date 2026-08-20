#pragma once
#include <string_view>

#include "reader/tracking.h"

namespace reader {
class Framebuffer;
class Font;

// Which colour glyph coverage paints. White exists so inverted text (a focused
// row, a filled action block) needs no scratch buffer: fill the rect black,
// then draw over it with Ink::White.
enum class Ink { Black, White };

// Which bit-plane of a 2-bit grey level this pass emits. The screen is drawn
// three times: Bw produces the base frame the panel paints first, then Lsb and
// Msb produce the two planes the controller combines into 4 levels. Opaque
// drawing (rules, fills, icons) has coverage 0 or 3 and so is identical in all
// three; only glyph edges differ.
enum class Plane { Bw, Lsb, Msb };

// Draws UTF-8 text with kerning; (x, baselineY) is the pen origin.
// `tracking` is the letter-spacing added after every glyph -- including the
// last, which is what CSS does and therefore what the boards' flex layouts
// measure. It is a fraction (see reader/tracking.h): the pen is accumulated in
// 1/64 px and only each glyph's paint position is rounded, so an em-derived
// value like 0.12em at 21px (2.52px) does not drift across a word.
//
// Returns the advance width consumed, which is exactly Font::measure of the
// same string and tracking. That equality is load-bearing: right-aligned runs
// are placed at `edge - measure(s)`.
int drawText(Framebuffer& fb, const Font& font, int x, int baselineY, std::string_view utf8,
             Ink ink = Ink::Black, Tracking tracking = {}, Plane plane = Plane::Bw);

// --- Vertical placement, in one place ---------------------------------------
//
// Every box on every screen that holds a line of text or a mark has to answer
// the same question, and the design answers it the same way each time: the
// boards lay their chrome out with `align-items: center`, so every child of a
// flex line sits on that line's one cross-axis centre. For a glyph run that
// means CSS half-leading -- the font's extent centred in the box, with the
// baseline falling out of it; for anything with a plain box height, an icon or
// a rule, it means the box centred in the box.
//
// These two functions are those two answers. They exist as shared functions
// rather than as a formula each caller repeats because the formula was got wrong
// the obvious way: `boxTop + boxH / 2 + ascent / 2` looks like centring but centres
// the *ascent*, and ascent reserves room above the caps for accents that a
// label like CONTINUE or LIBRARY does not have. Text placed that way sits low in
// its box by half the descent, consistently, on every screen. Six more screens
// are still to be built against these primitives; none of them should have to
// know that.

// Baseline for a single line of `font` centred in the box [boxTop, boxTop+boxH).
// `boxH` may be smaller than the font's own extent -- the boards tighten some
// line boxes below it (`line-height: 1.05` on a title, `1` on the big numeral)
// -- in which case the baseline is pulled up rather than the run being flushed
// to the top, which is what CSS does and what stops a 67px numeral opening a
// crater in a column.
int baselineIn(const Font& font, int boxTop, int boxH);

// Top row for an item `itemH` tall centred across the box [boxTop, boxTop+boxH)
// -- CSS `align-items: center` on a flex line, which is what every box on every
// board that pairs an icon with a line of text declares. The item's own
// content is irrelevant to the answer: flex centres each child's box on the
// line's single cross-axis centre, so an icon, a glyph run and a rule all
// resolve against the same box.
//
// It takes the *box*, not a font and a baseline, and that is the point rather
// than a convenience. Deriving the centre from a baseline is algebraically the
// same number -- `baseline - (ascent + descent) / 2` is identical to
// `boxTop + boxH / 2` once the baseline came out of baselineIn -- but it gets
// there through two more integer divisions, and each one sheds up to half a
// pixel. That is how the header band's battery ended up 1.5px below its
// percentage while the arithmetic looked correct: the half-leading rounded one
// way, `(ascent + descent) / 2` a second, `iconH / 2` a third. The browser
// divides once, in fractions, and snaps at the end. So does this.
//
// Halves round up, which is what Chrome's pixel snapping does with a
// half-pixel layout offset: the board's 21px battery in a 32px band lands on
// row 24, not 23, and the measurement of the rendered board agrees.
int iconTopIn(int boxTop, int boxH, int itemH);
}  // namespace reader
