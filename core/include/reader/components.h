#pragma once
#include <string>
#include <string_view>

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
// six roles. So the padding is what is stated here -- which is what the boards
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
// `gap: 3px` between a hint's label and its hold line -- the boards set the
// two-line slot up as a flex column with that gap, so the second line is not
// simply the next line box down.
inline constexpr int kHintHoldGap = 3;

// Letter-spacing. The boards state it per run, in em, and the runs do not agree:
// 0.22em on the band's label, 0.18em on a menu row's, 0.20em on an action
// block's, 0.12em on a hint label, 0.16em and 0.10em on the two meta lines. One
// shared `kLabelTracking = 2` stood in for all six, which is right for exactly
// one of them and left every tracked label on every screen at the wrong width.
//
// A Font cannot report its own ppem (an .rfnt carries ascent, descent and line
// gap, not a size), so em cannot be resolved from the face. It is resolved here
// instead, against the type ramp's pixel sizes, which are fixed by the ramp
// itself -- ppem = pt * 150 / 72. Spelling the conversion out as a constexpr
// rather than as six magic integers is what lets a reviewer check a value
// against the board without doing the arithmetic themselves.
inline constexpr int kMetaPx = 21;   // 10pt at 150 DPI
inline constexpr int kLabelPx = 23;  // 11pt at 150 DPI
// em given in thousandths; rounded to whole pixels, which is drawText's unit.
constexpr int trackingPx(int sizePx, int em1000) { return (sizePx * em1000 + 500) / 1000; }

inline constexpr int kBandLabelTracking = trackingPx(kLabelPx, 220);   // 0.22em -> 5
inline constexpr int kRowLabelTracking = trackingPx(kLabelPx, 180);    // 0.18em -> 4
inline constexpr int kBlockLabelTracking = trackingPx(kLabelPx, 200);  // 0.20em -> 5
inline constexpr int kHintTracking = trackingPx(kMetaPx, 120);         // 0.12em -> 3
inline constexpr int kMetaTracking = trackingPx(kMetaPx, 160);         // 0.16em -> 3
inline constexpr int kTightMetaTracking = trackingPx(kMetaPx, 100);    // 0.10em -> 2
// The design's `gap: 7px` between the header band's value and its battery
// glyph, and the same breathing room between a row's value and a trailing mark
// and between a hint's mark and its label -- the boards use one gap for all
// three, so this is one number three times rather than three numbers.
inline constexpr int kBandGap = 7;
inline constexpr int kRowGap = 7;
inline constexpr int kHintIconGap = 7;

// One hint-bar slot. `hold` is the second line a long-press variant gets, drawn
// inside the owning button's slot rather than as a fifth hint.
struct Hint {
  const Icon* icon;
  std::string_view label;
  std::string_view hold;
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

// The same, for the hint bar. A slot is one line of Meta, or two with the
// board's `gap: 3px` when it carries a hold line, and at least as tall as its
// own mark; the bar takes the tallest slot. A bar with a hold line is therefore
// taller than one without, exactly as the boards render it.
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

}  // namespace reader
