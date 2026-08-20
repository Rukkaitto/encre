#include "reader/text.h"

#include "reader/font.h"
#include "reader/framebuffer.h"

namespace reader {

int drawText(Framebuffer& fb, const Font& font, int x, int baselineY, std::string_view utf8,
             Ink ink, Tracking tracking, Plane plane) {
  const bool white = (ink == Ink::White);
  // The pen is 26.6 fixed point; `pen` below is only ever the *paint* position,
  // rounded off it. With integer tracking penF stays a multiple of 64 and every
  // glyph lands exactly where the old integer pen put it, so this is a strict
  // generalisation rather than a re-rounding of the untracked runs.
  int penF = pxToF26(x);
  char32_t prev = 0;
  for (size_t i = 0; i < utf8.size();) {
    const char32_t cp = utf8Next(utf8, i);
    const Glyph* g = font.glyph(cp);
    if (!g) {
      // No glyph for this codepoint: draw a hollow box so malformed or
      // out-of-subset text is visibly wrong instead of silently invisible.
      // Font::notdefAdvance() is the width this consumes, so Font::measure can
      // account for it without a second copy of the geometry.
      const int pen = f26ToPx(penF);
      const int h = font.ascent() * 2 / 3;
      const int w = h / 2 + 1;
      const int top = baselineY - h;
      for (int col = 0; col < w; ++col) {
        fb.setPixel(pen + col, top, white);
        fb.setPixel(pen + col, baselineY - 1, white);
      }
      for (int row = 0; row < h; ++row) {
        fb.setPixel(pen, top + row, white);
        fb.setPixel(pen + w - 1, top + row, white);
      }
      penF += pxToF26(font.notdefAdvance()) + tracking.f26();
      prev = 0;
      continue;
    }
    if (prev) penF += pxToF26(font.kerning(prev, cp));
    const int pen = f26ToPx(penF);
    for (int row = 0; row < g->bitmapH; ++row)
      for (int col = 0; col < g->bitmapW; ++col) {
        const uint8_t cov = font.coverage(*g, col, row);
        bool emit = false;
        switch (plane) {
          case Plane::Bw:
            emit = cov >= 2;
            break;
          case Plane::Lsb:
            emit = (cov & 1) != 0;
            break;
          case Plane::Msb:
            emit = (cov & 2) != 0;
            break;
        }
        if (emit) fb.setPixel(pen + g->xOff + col, baselineY - g->yOff + row, white);
      }
    penF += pxToF26(g->advance) + tracking.f26();
    prev = cp;
  }
  // f26ToPx is exact-linear in x (x is a whole pixel, so it factors out of the
  // rounding), which is why this equals Font::measure of the same run.
  return f26ToPx(penF) - x;
}

// descent() is negative, so `ascent - descent` is the run's full extent and
// `(ascent + descent) / 2` is the signed distance from the baseline up to the
// extent's midpoint. The two spellings below are the same identity; each is
// written the way its caller thinks about the problem.
int baselineIn(const Font& font, int boxTop, int boxH) {
  const int extent = font.ascent() - font.descent();
  return boxTop + (boxH - extent) / 2 + font.ascent();
}

// One division, at the end, halves up -- and a floor that behaves the same
// either side of zero, so an item taller than its box (the boards do tighten
// line boxes below their content) overhangs symmetrically instead of being
// pulled back toward the origin by integer truncation.
int iconTopIn(int boxTop, int boxH, int itemH) {
  const int slack = boxH - itemH;
  const int half = slack >= 0 ? (slack + 1) / 2 : -((-slack) / 2);
  return boxTop + half;
}

}  // namespace reader
