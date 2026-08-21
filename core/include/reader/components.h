#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "reader/fontset.h"
#include "reader/icons.h"
#include "reader/text.h"

namespace reader {
class Framebuffer;

// Fixed in pixels, not proportional: the X3 and X4 are within ~2% of the same
// PPI, so a margin should be the same physical size on both. What must adapt is
// the canvas width, which every primitive reads from the framebuffer.
inline constexpr int kMargin = 24;
// A menu row is 80 tall because the board says so in so many words
// (`height: 80px`), and the CONTINUE block 72 for the same reason.
// A menu row is the board's content box plus its own top border. The boards say
// `height: 80px` with `border-top: 1px`, and box-sizing is content-box there, so
// the rendered box is 81 -- the same pinned-number-ignoring-the-border mistake
// the two bars had. Every menu screen (Library, Settings, Wi-Fi settings...)
// stacks these, so a 1px error compounds per row.
inline constexpr int kRowContentH = 80;
inline constexpr int kRowRuleH = 1;
inline constexpr int kRowH = kRowContentH + kRowRuleH;

// --- The two bars that state a padding, not a height ------------------------
//
// The header band and the hint bar are the only chrome the boards size
// *implicitly*: neither declares a height. Each is padding around its tallest
// flex item plus a rule, and the browser adds them up. Both used to be pinned
// here as a single integer, and both integers were wrong -- the band by 6px,
// which put every screen's content 6px low, and the hint bar's text by 3px,
// because 20 + 27 + 16 + 1 happens to come to the pinned 64 while the padding
// is asymmetric and the primitive centred its content symmetrically.
//
// A pinned sum cannot be right for more than one type size, and the ramp has
// seven roles. So the padding is what is stated here -- which is what the boards
// state -- and the height is derived from it plus the content, the way Chrome
// derives it. That makes a bar's height depend on the type role it draws, which
// is the correct dependency: a screen whose band label is set larger gets a
// taller band, with no constant to remember to change.
//
// Both values are the boards' own, and they agree across every screen that has
// one: `padding: 18px 24px 14px 24px; border-bottom: 2px solid` on all 27
// screen header bands, `padding: 20px 24px 16px 24px; border-top: 1px solid` on
// 29 of the 30 hint bars (the exception is DirectionTerminal, an exploration
// board, not a V1 screen).
//
// The 8 boards that state something else for a top rule -- ReaderMenu,
// DeleteConfirm, GoToPage and the other overlays, all `padding: 21px 20px` --
// are not header bands: they are the caption of a modal panel, inset from the
// panel's own edge rather than the screen margin. When those screens are built
// they want their own primitive, not a widened kBandPadTop.
inline constexpr int kBandPadTop = 18;
inline constexpr int kBandPadBottom = 14;
inline constexpr int kBandRuleH = 2;
inline constexpr int kHintPadTop = 20;
inline constexpr int kHintPadBottom = 16;
inline constexpr int kHintRuleH = 1;
// There is no second line in a hint slot any more, and so no column gap: the
// long-press variant is a hollow ring beside the label (design 662557d). The
// board sets one `gap: 7px` on that flex row, which is kHintIconGap below, and
// it applies to the ring exactly as it applies to the leading mark. What used to
// be kHintHoldGap (`gap: 3px` on a flex *column*) describes a box the boards no
// longer draw, so it is gone rather than kept as a number nothing reads.

// Letter-spacing. The boards state it per run, in em, and the runs do not agree:
// 0.22em on the band's label, 0.18em on a menu row's, 0.20em on an action
// block's, 0.12em on a hint label, 0.16em and 0.10em on the two meta lines. One
// shared `kLabelTracking = 2` stood in for all six, which is right for exactly
// one of them and left every tracked label on every screen at the wrong width.
//
// What is stated here is the design's own number -- em in thousandths, nothing
// resolved, nothing rounded. Resolving it needs a pixel size, and the size now
// comes from the face itself (Font::ppem, declared by the asset), not from a
// private copy of the ramp's sizes kept alongside these constants. That copy was
// a second source of truth for every role, and it only covered the two roles
// today's screens tracked; a screen setting tracked text in Value or Title had
// nowhere to look. `trackingEm(font, kHintEm)` works for any role, including
// ones that do not exist yet.
//
// Nor is the product rounded to a whole pixel any more: 0.12em at 21px is
// 2.52px, and see reader/tracking.h for why that fraction has to survive.
inline constexpr int kBandLabelEm = 220;   // 0.22em, header band label
inline constexpr int kRowLabelEm = 180;    // 0.18em, menu row label
inline constexpr int kBlockLabelEm = 200;  // 0.20em, action block label
inline constexpr int kHintEm = 120;        // 0.12em, hint bar label
inline constexpr int kMetaEm = 160;        // 0.16em, the page-count meta line
inline constexpr int kTightMetaEm = 100;   // 0.10em, the chapter meta line
inline constexpr int kActionEm = 180;      // 0.18em, a prompt button's label
inline constexpr int kPromptTitleEm = 60;  // 0.06em, a full-screen prompt's title

// The design's em value, resolved against the face that will draw it.
inline Tracking trackingEm(const Font& font, int em1000) {
  return Tracking::em(font.ppem(), em1000);
}

// The design's `gap: 7px` between the header band's value and its battery
// glyph, and the same breathing room between a row's value and a trailing mark
// and between a hint's mark and its label -- the boards use one gap for all
// three, so this is one number three times rather than three numbers. The hint
// slot's gap is a flex `gap`, so it separates every child of that row: the
// leading mark from the label, and the label from the trailing hold ring.
inline constexpr int kBandGap = 7;
inline constexpr int kRowGap = 7;
inline constexpr int kHintIconGap = 7;

// A hint slot for a button with no action is not zero-wide: eight boards -- every
// one with a button that does nothing on that screen (SdMissing, HomeEmpty,
// BookDetails, WifiConnect, Transfer, SetupHotspot, ArticlesSetup,
// InstapaperConnect) -- author it as `<div style="width: 36px;"></div>`, and
// `justify-content: space-between` divides the leftover around it. Measuring an
// empty slot as 0 is not "drawing nothing", it is drawing the other slots in the
// wrong places: on SdMissing it moves the RETRY hint 36px left of the board's and
// widens each of the three gaps by 12px.
//
// It is a *width* only. The board's placeholder is 0 tall, and it makes no
// difference to the bar's height either way -- the height is the tallest of the
// four slots, and a bar has to have something in it to be worth drawing -- so an
// empty slot keeps the one line box every other slot has. That is what holds the
// invariant that the bar is exactly one line tall whatever its slots carry.
inline constexpr int kHintEmptySlotW = 36;

// One hint-bar slot: a leading mark, a label, and whether that button also has a
// long-press action.
//
// `hasHold` is a flag, not text. It used to be the second line of the slot
// (`"HOLD - ACTIONS"`), and the boards now say a long-press variant with a
// hollow ring after the label instead -- so there is nothing left to print, and
// a `std::string_view hold` would be a field whose contents no caller could see.
// Two real reasons for the change, both in design 662557d: a bar whose height
// varies with a hold moves every list stacked above it from screen to screen,
// and "- HOLD" does not fit a four-slot bar at 10pt on the 480-wide X4.
struct Hint {
  const Icon* icon;
  std::string_view label;
  bool hasHold;
};

// --- Derived heights -------------------------------------------------------
//
// The two bars' heights, computed the way the boards compute them: the padding
// above, the tallest thing inside, the padding below, and the rule. Both are
// exposed rather than left private to the draw calls because a caller sometimes
// has to know a bar's height *before* it draws it -- the hint bar draws itself
// at the bottom of the framebuffer, so a screen stacking menu rows above it
// needs the height to find their top edge. Asking the primitive is how that
// caller stays correct when the bar's content changes; a `kHintBarH` constant
// alongside it would just be the pinned number again under another name.

// Padding, the tallest of the band's own items, padding, rule. The items are a
// label, a value and the battery glyph, so the band tracks whichever of those
// roles is tallest -- which is the Value face on today's ramp (32 against the
// Label's 29 and the battery's 21), and would be the label on a screen that set
// its band label larger.
int headerBandHeight(const FontSet& fonts);

// The same, for the hint bar. A slot is exactly one line of Meta, at least as
// tall as the marks on that line; the bar takes the tallest slot. So the bar's
// height does *not* depend on whether any slot has a hold -- the ring rides on
// the same line as the label -- which is the point of the change: a bar that
// grew for a hold moved every list stacked above it. It still depends on the type
// role and on the marks, because that is what the board's box model depends on;
// the height is derived, never pinned.
int hintBarHeight(const FontSet& fonts, const Hint hints[4]);

// Each returns the height it consumed, so callers stack without recomputing.
// The band's value is drawn as one right-aligned group with the battery glyph.
// `plane` selects which bit-plane of a 2-bit grey level the text emits (see
// reader/text.h); it defaults to Plane::Bw so existing call sites are
// unaffected. It is passed on to drawIcon too, now that icons carry 2-bit
// anti-aliased coverage generated from the design's SVG and so differ per plane
// on every curve and diagonal. fillRect and ditherRect still need no plane:
// they are opaque by construction (coverage 0 or 3), identical in every plane.
int drawHeaderBand(Framebuffer& fb, const FontSet& fonts, std::string_view label,
                   std::string_view value, Plane plane = Plane::Bw);
// `value` may be empty and `trailing` may be null; a row may carry either, both
// or neither. A trailing mark is right-aligned on the margin and takes the row's
// ink, so it reverses out of a focused row along with the text.
int drawRow(Framebuffer& fb, const FontSet& fonts, int y, std::string_view label,
            std::string_view value, bool focused, const Icon* trailing = nullptr,
            Plane plane = Plane::Bw);
// Draws at the bottom of fb. Reports each slot's x in slotXOut[4] so tests and
// callers can assert the distribution.
int drawHintBar(Framebuffer& fb, const FontSet& fonts, const Hint hints[4], int slotXOut[4],
                Plane plane = Plane::Bw);

// --- A prompt button -------------------------------------------------------
//
// The boards' primary action on a full-screen prompt: a filled slab with one
// centred label. Five V1 boards draw it (SdMissing, DeleteConfirm, BookError,
// WifiError, ArticleEnd) and all five state the same box -- `height: 68px`, no
// border, label `--t-value` at weight 700 and `letter-spacing: 0.18em`, the
// block an `align-items: center; justify-content: center` flex row. The width is
// NOT shared: SdMissing pins its own 260, the overlays take their column's.
//
// This is not Home's CONTINUE block, which is 72 tall, left-aligns its label
// against `padding: 0 20px` and carries a trailing arrow. Same idea, different
// box, and drawing one with the other's numbers is the pinned-height mistake
// again.
//
// Only the filled form is here, because only the filled form has a screen. The
// four boards above pair it with a `border: 2px solid; box-sizing: border-box`
// outlined sibling at the same 68px for their secondary action; that variant
// belongs in this function when DeleteConfirm or BookError lands, not before.
inline constexpr int kActionH = 68;

int drawActionButton(Framebuffer& fb, const FontSet& fonts, int x, int y, int w,
                     std::string_view label, Plane plane = Plane::Bw);

// --- Wrapped prose ---------------------------------------------------------
//
// The first paragraph in the firmware: SdMissing's explanation is body copy that
// wraps inside a `max-width: 400px` column, centred, at `line-height: 1.55`.
// Everything above this line is a single line of text in a box whose size the
// board states; a paragraph's height is instead a *result* -- of the face, the
// copy and the column -- and the screen cannot place anything below it without
// wrapping it first.
//
// So the wrap is a value, computed once and then both measured and drawn. Two
// calls that each re-wrapped would be two chances to disagree about the line
// count, and a disagreement would not look like a bug in the wrap: it would look
// like a paragraph that had drifted off centre.
//
// Line breaking is greedy on ASCII spaces, which is what the boards' copy needs
// and no more: no hyphenation, no break inside a word (a word wider than the
// column gets its own line and overhangs, visibly, rather than being silently
// cut), and no bidi or CJK line breaking. Phase 3's EPUB text is a different
// problem with a different budget.
//
// Note that this wraps against the FIRMWARE's own metrics, which are not
// Chrome's: the .rfnt faces are autohinted, so their advances are whole pixels
// and a long run measures ~3% wider here than the same string in the browser.
// The line count following the face is the honest behaviour -- the alternative is
// a paragraph laid out for a font the device does not have -- and it is why
// SdMissing's board now says `max-width: 420px` where it used to say 400. Its
// copy's second line is 401px in this face against 389.97 in Chrome's, so the
// board's own three lines came out as four here, with "retry." alone on the last.
// 420 is the same three lines in both engines, and Chrome renders the board
// pixel-for-pixel identically either way -- the wider box moves no break, no line
// and not the paragraph's height, because nothing in the copy can rise into the
// extra 20px. The number was wrong, not the design.
inline constexpr int kProseMaxW = 420;     // SdMissing's `max-width: 420px`
inline constexpr int kProseLeadEm = 1550;  // its `line-height: 1.55`

struct Prose {
  // Views into the text passed to wrapProse -- which must outlive the Prose.
  std::vector<std::string_view> lines;
  // One line box, in 1/64 px: the board's line-height resolved against the face.
  // Fractional on purpose (1.55 x 29px is 44.95), so the lines below the first
  // do not accumulate a rounding error each.
  int leadF26 = 0;
  // Carried with the lines rather than passed again at draw time: the wrap and
  // the centring have to measure identically, and a second Tracking argument at
  // the draw call is a way for them not to.
  Tracking tracking{};

  int lineCount() const { return static_cast<int>(lines.size()); }
  // The paragraph's own height, the way the board computes it: line boxes, times
  // the line-height. In 1/64 px, because that is what it is.
  int heightF26() const { return lineCount() * leadF26; }
};

Prose wrapProse(const Font& font, std::string_view text, int maxW, int leadEm1000,
                Tracking tracking = {});

// Draws every line centred in the column [boxX, boxX + boxW), the first line box
// starting at `topF26`. Returns the height consumed, in 1/64 px, so a caller
// stacking below it does not have to recompute what it just drew.
int drawProse(Framebuffer& fb, const Font& font, const Prose& prose, int boxX, int boxW,
              int topF26, Ink ink = Ink::Black, Plane plane = Plane::Bw);

}  // namespace reader
