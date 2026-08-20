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
// Sized for the pt-at-150dpi type ramp, not the earlier px one: the chrome faces
// roughly doubled (Label 13px -> 23px, Meta 12px -> 21px), so every box that
// holds a line of text had to grow with them or the text would fill it edge to
// edge. The CONTINUE block is 72 tall and a menu row 80 because the boards say
// so in so many words (`height: 72px`, `height: 80px`).
//
// The other two are inferred, and measuring the boards says both inferences are
// a little off. The header band declares no height at all: it is `padding: 18px
// 24px 14px` around its tallest flex item plus a 2px rule, and Chrome renders
// that 66 tall, not 72 -- so content below it currently starts 6px low on every
// screen. `make compare` shows it directly: the band's rule lands at y=64 on
// the board and y=70 in the firmware, and the whole stats column follows it
// down. The hint bar's 20 + 27 + 16 + 1 does come to 64, but that padding is
// asymmetric where the primitives centre their content symmetrically, which
// leaves the bar's text 2px high.
//
// Both are left alone here on purpose: correcting them moves every screen, and
// that is a layout change to review on its own rather than a rider on the
// centring and tracking fixes these primitives just had. They are the largest
// remaining deviation from the boards on Home.
inline constexpr int kBandH = 72;
inline constexpr int kRowH = 80;
inline constexpr int kHintBarH = 64;

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
