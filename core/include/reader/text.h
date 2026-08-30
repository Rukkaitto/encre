#pragma once
#include <string>
#include <string_view>

#include <vector>

#include "reader/emphasis.h"
#include "reader/tracking.h"

namespace reader {
class Framebuffer;
// Chrome's pre-rendered ramp and the reader's scalable face both satisfy this,
// and everything below takes it rather than a concrete Font -- one text path,
// so kerning, tracking, the notdef box and the Plane handling cannot drift
// between chrome and body text. See reader/glyphsource.h for why, and for the
// interface's shape.
class GlyphSource;

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
int drawText(Framebuffer& fb, const GlyphSource& font, int x, int baselineY, std::string_view utf8,
             Ink ink = Ink::Black, Tracking tracking = {}, Plane plane = Plane::Bw);

// --- Justified text -----------------------------------------------------------
//
// drawText with a stretch added after every ASCII space. That is the whole of
// justification at the drawing layer; deciding HOW MUCH is layout's job (see
// reader/layout.h), because the slack is a property of the line's column and
// only the thing that wrapped the line knows that.
//
// `extraPerGapF26` is in 1/64 px for the same reason Tracking is: a 444px column
// distributing 11px of slack across 7 gaps wants 1.571px per gap, and seven
// roundings of that land the line's right edge up to 3px short of the margin --
// visibly ragged on exactly the lines justification exists to straighten. The pen
// accumulates the fraction and rounds once per glyph, as it already does for
// letter-spacing.
//
// A GAP IS U+0020 AND NOTHING ELSE -- not a tab, not a non-breaking space (which
// is a space the author asked NOT to be stretched, and stretching it would pull
// apart the very pair it was inserted to hold together). layout.cpp counts gaps
// by the same rule; the count and the stretch must agree or the line overshoots
// its column by the difference.
//
// Returns the advance consumed, INCLUDING the stretch -- so the return is the
// justified line's real width and a caller can check it against the column.
// --- A face that changes part-way through the text ---------------------------
//
// What the reader wraps body text with once a block can carry emphasis. Every
// chrome caller keeps the single-face overload above and nothing on any existing
// board moves.
//
// IT EXISTS BECAUSE THE TWO FACES ARE NOT THE SAME WIDTH. Measured at ppem 32 on
// the prepped assets, Literata's italic runs **6% to 9% narrower** than the roman
// over the same string -- "poor dress" is 159px roman and 149px italic, and
// "ABCDEFGHIJKLMNOPQRSTUVWXYZ" is 601 against 549. So measuring emphasis with the
// roman and drawing it with the italic mis-measures a 20-byte phrase by ~20px on a
// 444px column: most of a word, enough to break the line in the wrong place AND to
// hand justification the wrong slack for it.
//
// KERNING IS LOST ACROSS A FACE BOUNDARY, and that is the right trade rather than
// an oversight. Measuring in pieces cannot see the pair that straddles a
// roman-to-italic join -- but there is no such pair to see, because the two faces
// have separate `kern` tables and nothing defines a pair between them. What matters
// is that the DRAW splits at exactly the same boundaries, so the measuring pass and
// the drawing pass agree; a joined measure and a split draw would not.
struct StyledFace {
  const GlyphSource* roman = nullptr;
  // Null means "measure emphasis as roman" -- the state the firmware was in before
  // the italic asset existed, and still the state of any caller that has no second
  // face to offer. It is a degradation rather than a failure.
  const GlyphSource* italic = nullptr;
  // Byte ranges into the text being wrapped, sorted and non-overlapping. Null or
  // empty is the overwhelmingly common case and takes the single-face path whole.
  const std::vector<Span>* emphasis = nullptr;

  bool anyEmphasis() const {
    return italic != nullptr && emphasis != nullptr && !emphasis->empty();
  }
  // Out of line: this header only forward-declares GlyphSource.
  const GlyphSource& at(size_t off) const;
  // `text[from, to)`, in as few pieces as there are style changes in it -- which is
  // ONE piece for almost every line ever measured.
  int measure(std::string_view text, size_t from, size_t to, Tracking tracking) const;
};

// Draws one line whose face may change part-way through, and returns its advance.
//
// THE PEN CARRIES ACROSS THE PIECES IN 26.6, which is the whole reason this is not
// a loop of `drawTextJustified` calls in the caller. `drawRun` takes an integer x
// and returns an integer advance, so chaining pieces at whole pixels would round at
// every face boundary -- and the fractional pen is one of the fidelity fixes
// text.cpp exists to keep in ONE place.
//
// KERNING RESETS AT A BOUNDARY, and it must: `kerning()` is a call into one
// GlyphSource about a pair of ITS glyphs, and there is no pair defined across two
// faces. StyledFace::measure splits at the same boundaries and does the same, which
// is what keeps the measuring pass and this one in agreement.
int drawTextStyled(Framebuffer& fb, const StyledFace& face, int x, int baselineY,
                   std::string_view utf8, const std::vector<Span>& emphasis,
                   int extraPerGapF26, Ink ink, Tracking tracking, Plane plane);

int drawTextJustified(Framebuffer& fb, const GlyphSource& font, int x, int baselineY,
                      std::string_view utf8, int extraPerGapF26, Ink ink = Ink::Black,
                      Tracking tracking = {}, Plane plane = Plane::Bw);

// How full a line must be, as a percentage of its column, before it is justified
// at all. Below this it is set ragged.
//
// THE TEST IS THE LINE, NOT THE GAP, and getting that round the wrong way is
// instructive. This started as a cap on how far one gap could stretch -- three
// times the space's own width -- on the reasoning that justification's failure
// case is a corridor of white between two words. The failure case is real: our
// own fixtures contain "pneumonoultramicroscopicsilicovolcanoconiosis", which is
// wider than the 444px column, so the greedy wrap puts it alone on a line and
// leaves the line before it holding two words and 330px of slack.
//
// But a per-gap cap cannot tell that line from ordinary prose, because the number
// of gaps is what converts slack into stretch. Measured on
// design/Reader.dc.html's own two paragraphs, the cap refused "necklace, and the
// two of" -- 367px of text in a 444px column, a perfectly ordinary line -- because
// its 77px of slack fell across only four gaps, 19.25px each against an 18px cap.
// It refused it BY ONE PIXEL, and set it ragged directly beneath a line it had
// justified at 15.25px. A ragged line sitting between two justified ones is
// exactly what the cap existed to avoid, arrived at from the other direction.
//
// A line that is 83% full is prose. A line that is 23% full is the corridor. So
// the question is how much of the line is TEXT, which is the thing actually
// visible, and it needs no reference to the gap count at all.
//
// 60% is where "more text than space" stops being true. Measured over the same
// 6,800 pages of tools/mkepub.py output: the per-gap cap set 18% of all lines
// ragged and the fill test sets 3.4%, taking justified lines from 71% to 86%. The
// lines that remain ragged are almost all paragraph-final, which is where ragged
// belongs.
//
// The price is admitted rather than hidden: a line at the threshold has 40% of its
// column as slack, and across four gaps that is a gap five or six times the space's
// own width -- a visible river. That is the trade a wrap with no hyphenation
// dictionary has to make, and it is made in this direction because an occasional
// wide gap reads as loose typesetting while a ragged line mid-paragraph reads as
// the feature being broken.
//
// IT LIVES HERE, WITH THE FUNCTION THAT APPLIES IT. Both were reader/layout's
// until the Typography preview needed the same rule: a constant in one layer
// governing a function in another is two places to read one decision.
//
// RE-ASKED IN 2026-08 WITH MEASUREMENTS, AND THE ANSWER IS STILL THIS RULE ALONE.
// The complaint was a real one and it is worth knowing what it was, because it will
// be noticed again: this test is BLIND TO THE GAP COUNT, and the gap count is what
// turns slack into stretch. Over 743,197 justifiable lines from 39 real books at
// this column, the MEDIAN gap on a ONE-gap line is 17.2 space widths -- typical, not
// a tail -- against 7.2 at two gaps, 4.0 at three and under 2.7 at four or more. The
// worst seen on a device was 33.
//
// A CEILING ON THE GAP WAS BUILT AND THEN DROPPED, on this evidence. Twelve space
// widths -- the smallest ceiling that leaves three-gap lines to this test at both
// geometries -- takes ragged lines from 2.50% to 5.23%, which is roughly HALF the
// pages gaining a line that stops short mid-paragraph against a quarter of them
// today. That is the wrong trade, and the reason is the one already written above:
// a wide gap reads as loose typesetting and a short line mid-paragraph reads as the
// feature being broken. Trading a defect for a WORSE-CLASS defect is not a fix, even
// when the number being traded away is large.
//
// TWO THINGS THAT MAKE IT LESS ALARMING THAN IT SOUNDS. This test already BOUNDS the
// gap: a one-gap line at exactly the floor is 30.5 spaces on the X4, so the worst
// case is bounded rather than open. And no board is anywhere near it -- forcing a
// ceiling down to SIX space widths moves not one golden, so every board's widest gap
// is under a fifth of what a real book produces.
//
// THERE IS NO THIRD MECHANISM, which is the part worth not re-deriving. For a
// one-gap line whose next word is ~190px, shrinking the spaces buys ~6px and
// letter-spacing the line at a defensible 5% of em buys ~29px. It is a wide gap or a
// short line until there is HYPHENATION, which is the actual fix and is a dictionary
// this device does not carry.
inline constexpr int kMinJustifyFillPercent = 60;

// How much each ASCII space on this line stretches, or 0 for ragged.
//
// The gap COUNT here and the codepoint drawTextJustified stretches must be the
// same rule, which is why both name U+0020 and nothing else -- and that comment is
// the reason the pair belongs in ONE layer. It was reader/layout's private helper
// while pagination was its only caller; it takes no PageMetrics, no Block and no
// cursor, so it was never pagination's, and drawProse's ProseAlign::Justify is the
// second caller that proved it.
//
// `availW` is the measure the line was WRAPPED against, which is not always the box
// it is drawn in: a blockquote is inset and a first line may be indented, and a
// stretch computed against the wider box overflows by exactly the difference.
//
// `tracking` HAS NO DEFAULT, deliberately: it must be the tracking the line was
// measured with, and a defaulted `{}` is a way for a caller to space a line it had
// measured unspaced -- which is the drift Prose::tracking and LaidLine::tracking
// both exist to prevent.
int stretchFor(const GlyphSource& font, std::string_view line, int availW, Tracking tracking);

// --- A run that has to fit -----------------------------------------------------
//
// drawText draws from a left edge with no right edge, which is right for every
// run whose text the design chose: a board's own copy fits the box the board drew
// it in, by construction. It is wrong for every run whose text arrives from the
// CARD. A filename is as long as someone made it, and drawn from a left edge it
// simply keeps going -- past its column, over the value beside it, off the panel.
// The boards now say what happens instead (`overflow: hidden; text-overflow:
// ellipsis; white-space: nowrap` on the title runs of Main, Library and
// LibraryActions), and this is that declaration.
//
// The ellipsis is the REAL character, U+2026, not three periods. It is in every
// face's subset (tools/fontc.py adds it alongside the quotes and dashes) and it
// is narrower than the periods it replaces in every role -- 16px against 18 in
// Label500, 32 against 36 in Title700 -- so the periods would be both wrong
// typography and less room for the name.
inline constexpr std::string_view kEllipsis = "\xE2\x80\xA6";

// `utf8` if it already fits `maxW`, otherwise the longest prefix that fits with
// the ellipsis joined on, plus the ellipsis.
//
// UTF-8 aware, and that is not a nicety: a prefix cut mid-sequence decodes to
// U+FFFD, and fontc.py puts U+FFFD IN the subset precisely so malformed text is
// visible -- so the name would come out with a replacement box on the end of it,
// then the ellipsis, and it would read as a font bug rather than as a truncation.
// It also measures wrong, because the box is not the width of the bytes it stands
// for, which is how a mid-sequence cut turns into an overhang after all. The cut
// is always on a codepoint boundary.
//
// Three decisions worth knowing, because each is a real filename:
//   - A run that FITS is returned untouched, with no ellipsis. Truncation must
//     engage only on overflow, which is what makes every existing golden hold.
//   - When only the ellipsis fits, the ellipsis is the whole answer.
//   - When not even the ellipsis fits -- `maxW` below its advance, including a
//     zero or negative `maxW` -- the answer is EMPTY. Drawing a mark wider than
//     its box is the defect this function exists to remove, so it cannot be this
//     function's own fallback; a column that cannot hold one glyph gets nothing
//     rather than something that overhangs.
//
// `tracking` must be the same value the run will be drawn with, for the reason
// Prose carries its own: the fit is decided by a measurement, and a measurement
// taken with different spacing is a different answer.
std::string elideToWidth(const GlyphSource& font, std::string_view utf8, int maxW,
                         Tracking tracking = {});

// drawText of the above, and the advance it returns is the elided run's -- so a
// caller can still place something after it. Short-circuits the measure when the
// text fits, which is the common case and the one that must not allocate.
int drawTextElided(Framebuffer& fb, const GlyphSource& font, int x, int baselineY,
                   std::string_view utf8, int maxW, Ink ink = Ink::Black,
                   Tracking tracking = {}, Plane plane = Plane::Bw);

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
int baselineInF26(const GlyphSource& font, int boxTopF26, int boxHF26);

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
int baselineIn(const GlyphSource& font, int boxTop, int boxH);

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

// ASCII-only uppercase. The boards set `text-transform: uppercase` on some runs
// and author others as caps outright, and both cases now have callers outside
// the theme -- the delete prompt composes a sentence with a book's title shouted
// inside it, and a panel's caption is a caps label whose text arrives from a
// filename. So this is one function rather than a private copy per file.
//
// ASCII AND THE LATIN-1 SUPPLEMENT, which is what the fonts actually carry:
// fontc.py's subset is 0x20..0x7E plus ALL of 0xA0..0xFF, so `E9` has an `C9` to
// become. It was ASCII-only, and this header said a Unicode mapping was "a table
// core/ should not carry" and that "the titles that need one arrive with real EPUB
// metadata in Phase 3" -- which they now have: the device showed `LE FLéAU`.
//
// It needs no table. U+00E0..U+00FE is `C3 A0`..`C3 BE` in UTF-8 and the uppercase
// U+00C0..U+00DE is `C3 80`..`C3 9E`, so the second byte drops by 0x20 exactly as an
// ASCII letter's only byte does. Three characters are excluded because their
// uppercase is not one byte away; text.cpp names each and why.
//
// ANYTHING BEYOND LATIN-1 STILL PASSES THROUGH UNTOUCHED -- Greek, Cyrillic, and the
// Latin Extended ranges. That is not laziness but the same rule as before: a
// character the font subset has no uppercase glyph for would render as a notdef box,
// which is worse than a lowercase letter. Widening this means widening the subset
// first.
std::string upperLatin1(std::string_view s);
}  // namespace reader
