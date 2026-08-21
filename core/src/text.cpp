#include <optional>

#include "reader/dither.h"
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
    const std::optional<Glyph> g = font.glyph(cp);
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
          case Plane::BwDithered: {
            // Stipple the edge instead of thresholding it away. Coverage is
            // 0..3, so the nominal ink fraction is cov/3; comparing cov*16/3
            // against the dispersed Bayer rank makes cov 1 ink 5 cells of 16,
            // cov 2 ink 10, and cov 3 ink all 16 -- monotonic, with full
            // coverage staying solid (a glyph's interior must never be
            // stippled) and zero staying blank.
            const int px = pen + g->xOff + col;
            const int py = baselineY - g->yOff + row;
            emit = (cov * 16) / 3 > bayer4(px, py);
            break;
          }
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

std::string elideToWidth(const Font& font, std::string_view utf8, int maxW, Tracking tracking) {
  if (font.measure(utf8, tracking) <= maxW) return std::string(utf8);
  const int ellipsisW = font.measure(kEllipsis, tracking);
  // Not even the mark fits. See the header: nothing, rather than something that
  // overhangs the box this whole function exists to respect.
  if (ellipsisW > maxW) return std::string();

  // The pen is accumulated exactly as Font::measure accumulates it -- 1/64 px,
  // kerning included, rounded once -- and after each codepoint we ask what the
  // run WOULD measure with the ellipsis joined on. That join has to be part of
  // the measurement rather than added afterwards: `measure(prefix) +
  // measure(kEllipsis)` misses the kern across the join, and the kern is where a
  // one-pixel overhang would come from.
  constexpr char32_t kEllipsisCp = 0x2026;
  const std::optional<Glyph> eg = font.glyph(kEllipsisCp);
  const int ellipsisAdvanceF = pxToF26(eg ? eg->advance : font.notdefAdvance());

  int penF = 0;
  char32_t prev = 0;
  size_t fits = 0;  // byte length of the longest prefix that fits with the mark
  size_t i = 0;
  while (i < utf8.size()) {
    const char32_t cp = utf8Next(utf8, i);
    const std::optional<Glyph> g = font.glyph(cp);
    if (!g) {
      penF += pxToF26(font.notdefAdvance()) + tracking.f26();
      prev = 0;
    } else {
      if (prev) penF += pxToF26(font.kerning(prev, cp));
      penF += pxToF26(g->advance) + tracking.f26();
      prev = cp;
    }
    int joinedF = penF;
    if (prev) joinedF += pxToF26(font.kerning(prev, kEllipsisCp));
    joinedF += ellipsisAdvanceF + tracking.f26();
    if (f26ToPx(joinedF) > maxW) break;
    fits = i;
  }
  // `i` is already on a codepoint boundary at every iteration -- utf8Next only
  // ever leaves it on one -- so this substr can never split a sequence.
  std::string out(utf8.substr(0, fits));
  out += kEllipsis;
  return out;
}

int drawTextElided(Framebuffer& fb, const Font& font, int x, int baselineY,
                   std::string_view utf8, int maxW, Ink ink, Tracking tracking, Plane plane) {
  if (font.measure(utf8, tracking) <= maxW)
    return drawText(fb, font, x, baselineY, utf8, ink, tracking, plane);
  const std::string cut = elideToWidth(font, utf8, maxW, tracking);
  return drawText(fb, font, x, baselineY, cut, ink, tracking, plane);
}

// descent() is negative, so `ascent - descent` is the run's full extent and
// `(ascent + descent) / 2` is the signed distance from the baseline up to the
// extent's midpoint.
//
// There is ONE implementation, and it is the fractional one. The whole-pixel
// entry point is a unit conversion in front of it, not a second rule: it used to
// compute `boxTop + (boxH - extent) / 2 + ascent`, which rounds the half-leading
// AND then lands on a whole baseline -- two roundings -- and so answered 1px
// higher than this one on every box whose slack (boxH - extent) is odd and
// positive. That is the "round once" invariant, and the two spellings disagreeing
// by a pixel was a trap: a screen picked whichever helper its neighbour used and
// the defect was too small to see in review.
//
// Exactly two boxes on the implemented screens have odd positive slack, and both
// set Value700 (33px extent): a menu row's VALUE in the 80px row content box
// (slack 47) and the action block's label in its 68px block (slack 35). Both
// moved down a pixel and both now land where Chrome puts them, measured off the
// rasterised boards -- Home's "12" on rows 606..623 against the board's 606..623,
// and SdMissing's RETRY 25px above and 25px below inside its slab where it used
// to be 24 and 26. Six goldens were re-blessed onto those numbers. Every other
// box is even-slack (a row's LABEL, the header band's label, the CONTINUE block)
// or negative-slack (the boards tighten the title, the numeral and every
// line-height-1 box below its own extent), where the two spellings always agreed.
int baselineInF26(const Font& font, int boxTopF26, int boxHF26) {
  const int extentF26 = pxToF26(font.ascent() - font.descent());
  // Arithmetic shift rather than / 2, so a box shorter than the run it holds
  // (the boards do tighten line boxes below their content) halves the same way
  // on either side of zero instead of truncating toward it.
  const int halfLeading = (boxHF26 - extentF26) >> 1;
  return f26ToPx(boxTopF26 + halfLeading + pxToF26(font.ascent()));
}

int baselineIn(const Font& font, int boxTop, int boxH) {
  return baselineInF26(font, pxToF26(boxTop), pxToF26(boxH));
}

// One division, at the end, halves up -- and a floor that behaves the same
// either side of zero, so an item taller than its box (the boards do tighten
// line boxes below their content) overhangs symmetrically instead of being
// pulled back toward the origin by integer truncation.
int centreIn(int boxStart, int boxSize, int itemSize) {
  const int slack = boxSize - itemSize;
  const int half = slack >= 0 ? (slack + 1) / 2 : -((-slack) / 2);
  return boxStart + half;
}

int iconTopIn(int boxTop, int boxH, int itemH) { return centreIn(boxTop, boxH, itemH); }


std::string upperAscii(std::string_view s) {
  std::string out(s);
  for (char& c : out)
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  return out;
}

}  // namespace reader
