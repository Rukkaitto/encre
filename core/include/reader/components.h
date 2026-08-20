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
inline constexpr int kBandH = 52;
inline constexpr int kRowH = 56;
inline constexpr int kHintBarH = 46;
inline constexpr int kLabelTracking = 2;
// The design's `gap: 7px` between the header band's value and its battery
// glyph, and the same breathing room between a row's value and a trailing mark.
inline constexpr int kBandGap = 7;
inline constexpr int kRowGap = 7;

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
// unaffected. drawIcon, fillRect and ditherRect need no plane: they are opaque
// by construction (coverage 0 or 3), identical in every plane already.
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
