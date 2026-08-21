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
// Which pass of the panel's rendering model this draw is for. Bw/Lsb/Msb are
// the three-pass grayscale path; BwDithered is the single-pass 1-bit path that
// keeps anti-aliasing by stippling edge coverage instead of thresholding it
// away, which is what lets chrome be smooth AND cost one waveform.
enum class Plane { Bw, Lsb, Msb, BwDithered };

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
// These two answers are two functions -- one for a glyph run, one for a plain
// box -- and each is ONE function, not a family. They exist as shared functions
// rather than as a formula each caller repeats because the formula was got wrong
// the obvious way: `boxTop + boxH / 2 + ascent / 2` looks like centring but centres
// the *ascent*, and ascent reserves room above the caps for accents that a
// label like CONTINUE or LIBRARY does not have. Text placed that way sits low in
// its box by half the descent, consistently, on every screen. Six more screens
// are still to be built against these primitives; none of them should have to
// know that.
//
// And they round the same way as each other: halves go UP, once, at the end,
// which is what Chrome's pixel snapping does. The whole-pixel baseline used to
// round twice and therefore truncate -- see baselineIn below.

// Baseline for a single line of `font` centred in a box whose top and height are
// carried in 1/64 px. This is the implementation; the whole-pixel form below is
// a unit conversion in front of it.
//
// Fractional because not every box the boards compute lands on a whole pixel. A
// wrapped paragraph's line box is `line-height: 1.55` on a 29px face -- 44.95px,
// which Chrome holds as 44.9375 in its own 1/64 unit and never rounds until it
// paints -- so the third line's box top is 89.875px below the first's. Rounding
// each line box to a whole pixel first and centring inside that re-introduces
// exactly the accumulating error `Tracking` exists to avoid, one line at a time
// instead of one glyph at a time. So the box arrives as a fraction, the
// half-leading is taken in the same unit, and the ONE rounding is the returned
// baseline.
//
// `boxH` may be smaller than the font's own extent -- the boards tighten some
// line boxes below it (`line-height: 1.05` on a title, `1` on the big numeral)
// -- in which case the baseline is pulled up rather than the run being flushed
// to the top, which is what CSS does and what stops a 67px numeral opening a
// crater in a column.
int baselineInF26(const Font& font, int boxTopF26, int boxHF26);

// The same rule for a box already on the whole-pixel grid, which is most of the
// chrome. It converts and forwards: there is no second formula here, deliberately.
//
// It used to have one -- `boxTop + (boxH - extent) / 2 + ascent` -- and that is
// two roundings, the half-leading and then the baseline, where the fractional
// form has one. The two therefore disagreed by exactly 1px on every box with odd
// positive slack (`boxH - extent`), the whole-pixel one sitting a pixel high, and
// the disagreement was pinned in a test instead of fixed for one commit. Two
// helpers that mean the same thing and differ by a pixel is a trap: the next
// screen picks whichever one its neighbour used and the defect is too small to
// fail review. SdMissing's action block is the box that had it (68px box, 33px
// extent, slack 35); its two goldens were re-blessed onto the fractional answer.
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
// row 24, not 23, and the measurement of the rendered board agrees. That is the
// same rounding baselineInF26 applies to a baseline, so the mark and the run
// beside it are snapped by one rule and not two -- `centreIn` is already exactly
// `f26ToPx` of the fractional centre, proved exhaustively in test_components.cpp,
// so it needs no fractional twin to forward to.
int iconTopIn(int boxTop, int boxH, int itemH);

// The axis-agnostic form of the same answer, and what iconTopIn is implemented
// as. It exists because the boards centre on the cross axis and on the main axis
// with the same arithmetic -- `align-items: center` on a flex row is
// `justify-content: center` on a flex column -- and a full-screen prompt centres
// its icon, its title, every line of its paragraph and its button box
// horizontally. Those five call sites wanting `iconTopIn` for an x would have
// been five chances to write `(boxW - itemW) / 2` instead and round the other
// way on a half.
int centreIn(int boxStart, int boxSize, int itemSize);
}  // namespace reader
