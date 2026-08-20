#pragma once
#include <string_view>

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
// `tracking` adds that many pixels after every glyph, for the letterspaced
// labels the design uses. Returns the advance width consumed.
int drawText(Framebuffer& fb, const Font& font, int x, int baselineY, std::string_view utf8,
             Ink ink = Ink::Black, int tracking = 0, Plane plane = Plane::Bw);

// --- Vertical placement, in one place ---------------------------------------
//
// Every box on every screen that holds a line of text has to answer the same
// question, and the design answers it the same way each time: the boards lay
// their chrome out with `align-items: center`, so a run is centred by CSS
// half-leading -- the font's ascent..descent extent is centred in the box and
// the baseline falls out of that.
//
// These two functions are that answer. They exist as shared functions rather
// than as a formula each caller repeats because the formula was got wrong the
// obvious way: `boxTop + boxH / 2 + ascent / 2` looks like centring but centres
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

// Top row for an icon `iconH` tall that pairs with a line of `font` sitting on
// `baseline`, such that the two share an optical centre. This is the inverse of
// baselineIn, so an icon placed with it and text placed with baselineIn out of
// the same box agree exactly.
//
// Hanging an icon off the baseline by a fixed offset -- what every call site used
// to do -- can only ever be right for one icon height. The design's bars mix
// heights (a 25px square mark beside a 38x21 battery), so a fixed offset put the
// battery visibly off its percentage and the hint marks off their labels.
int iconTopFor(const Font& font, int baseline, int iconH);
}  // namespace reader
