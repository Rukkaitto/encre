#pragma once
#include <cstdint>

namespace reader {

// --- Letter-spacing, carried as a fraction ----------------------------------
//
// The boards state letter-spacing in em, per run, and the products are not
// whole pixels: `letter-spacing: 0.12em` on a 21px hint label is 2.52px, 0.16em
// is 3.36px, 0.22em on a 23px band label is 5.06px. A browser adds the exact
// fraction after every character and lays the next glyph at a fractional pen
// position; only the final layout box is snapped.
//
// Rounding em to a whole pixel first -- which is what a plain `int tracking`
// forces -- turns that fraction into a per-glyph error that *accumulates*. At
// 0.12em the error is 0.48px a character, so SELECT (six of them) came out 3px
// wider than the board, and every slot after it in the hint bar was pushed
// along by the difference. No amount of care at the call site fixes that; the
// unit is wrong.
//
// So tracking is held in 1/64 px -- FreeType's 26.6 convention, and far finer
// than any error the design cares about -- and the pen is accumulated in that
// unit by both drawText and Font::measure. Each glyph is *drawn* at a whole
// pixel (the framebuffer has no subpixel positions and the glyph bitmaps are
// pre-rendered) but the pen it advances from does not lose the fraction, so the
// drift cannot build up.
//
// It is a class rather than a bare int on purpose: `drawText(..., 3)` and
// `drawText(..., Tracking::px(3))` differ by a factor of 64, and a silent
// reinterpretation of one as the other is exactly the kind of mistake that is
// invisible in review and obvious only on glass.
class Tracking {
 public:
  constexpr Tracking() = default;

  // Whole pixels, for a run whose spacing is stated in px rather than em.
  static constexpr Tracking px(int wholePixels) { return Tracking(wholePixels * 64); }

  // `em1000` is the design's em value in thousandths (0.12em -> 120), resolved
  // against a face's pixel size. Rounded to the nearest 1/64 px, which is a
  // 0.008px worst case -- three orders of magnitude below the whole-pixel
  // rounding it replaces.
  static constexpr Tracking em(int sizePx, int em1000) {
    const int num = sizePx * em1000 * 64;
    return Tracking(num >= 0 ? (num + 500) / 1000 : -((-num + 500) / 1000));
  }

  constexpr int f26() const { return f26_; }
  constexpr bool operator==(const Tracking& o) const { return f26_ == o.f26_; }

 private:
  constexpr explicit Tracking(int f26) : f26_(f26) {}
  int f26_ = 0;
};

// 26.6 fixed point -> whole pixels, rounding halves up. Arithmetic shift, so
// the rounding direction is the same for a negative pen as for a positive one
// (floor(x + 0.5) either side of zero) rather than flipping about the origin
// the way integer division truncation does.
constexpr int f26ToPx(int f26) { return (f26 + 32) >> 6; }
constexpr int pxToF26(int px) { return px * 64; }

}  // namespace reader
