#pragma once
#include <string>
#include <string_view>

#include "reader/fontset.h"
#include "reader/icons.h"

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

// One hint-bar slot. `hold` is the second line a long-press variant gets, drawn
// inside the owning button's slot rather than as a fifth hint.
struct Hint {
  const Icon* icon;
  std::string_view label;
  std::string_view hold;
};

// Each returns the height it consumed, so callers stack without recomputing.
int drawHeaderBand(Framebuffer& fb, const FontSet& fonts, std::string_view label,
                   std::string_view value);
int drawRow(Framebuffer& fb, const FontSet& fonts, int y, std::string_view label,
            std::string_view value, bool focused);
// Draws at the bottom of fb. Reports each slot's x in slotXOut[4] so tests and
// callers can assert the distribution.
int drawHintBar(Framebuffer& fb, const FontSet& fonts, const Hint hints[4], int slotXOut[4]);

}  // namespace reader
