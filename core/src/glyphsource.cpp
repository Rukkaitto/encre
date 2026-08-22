#include "reader/glyphsource.h"

namespace reader {

uint8_t GlyphSource::coverage(const Glyph& g, int col, int row) const {
  const uint8_t* r = g.bitmap + static_cast<size_t>(row) * g.stride;
  if (bpp_ == 1) return ((r[col / 8] >> (7 - col % 8)) & 1) ? 3 : 0;
  // 2bpp, MSB-first: two bits per pixel, four pixels per byte.
  const int shift = 6 - 2 * (col % 4);
  return static_cast<uint8_t>((r[col / 4] >> shift) & 0x3);
}

// The hollow box drawText paints in place of a missing glyph: h = ascent * 2/3,
// w = h / 2 + 1, then two pixels of side bearing. Spelled once, read by both.
int GlyphSource::notdefAdvance() const { return (ascent_ * 2 / 3) / 2 + 1 + 2; }

int GlyphSource::measure(std::string_view utf8, Tracking tracking) const {
  // Accumulated in 1/64 px and rounded once, exactly as drawText does it, so
  // measure() == drawText()'s returned advance for every string and every
  // tracking value -- including fractional ones, where a per-glyph rounding
  // here and there would part company after the second character. Right
  // alignment is `edge - measure(s)`, so a disagreement of even one pixel is a
  // run that does not end on the margin it was aligned to.
  //
  // It asks advance(), never glyph(), and that is design decision 3 made
  // structural rather than promised: there is no code path from a measurement
  // to a rasteriser, because this one function is every face's measure() and it
  // does not know how to reach one. It also recovers what Task 1's reclaim
  // cost -- the old body decoded a whole Glyph, five int16 reads and a stride
  // computation, and then used one field of it.
  int penF = 0;
  char32_t prev = 0;
  for (size_t i = 0; i < utf8.size();) {
    const char32_t cp = utf8Next(utf8, i);
    const std::optional<int> adv = advance(cp);
    if (!adv) {
      // Not `continue`: drawText draws a box here and advances past it, so
      // skipping it made a run with one unmapped codepoint measure short by the
      // box's width and kern across the hole as if it were not there.
      penF += pxToF26(notdefAdvance()) + tracking.f26();
      prev = 0;
      continue;
    }
    if (prev) penF += pxToF26(kerning(prev, cp));
    penF += pxToF26(*adv) + tracking.f26();
    prev = cp;
  }
  return f26ToPx(penF);
}

char32_t utf8Next(std::string_view s, size_t& i) {
  const auto b0 = static_cast<uint8_t>(s[i]);
  if (b0 < 0x80) { ++i; return b0; }

  // Sequence length, the lead byte's payload bits, and the smallest code point
  // that legally uses this length (anything below it is an overlong encoding).
  size_t extra;
  char32_t cp, lowest;
  if ((b0 & 0xE0) == 0xC0) {
    extra = 1; cp = b0 & 0x1Fu; lowest = 0x80;
  } else if ((b0 & 0xF0) == 0xE0) {
    extra = 2; cp = b0 & 0x0Fu; lowest = 0x800;
  } else if ((b0 & 0xF8) == 0xF0) {
    extra = 3; cp = b0 & 0x07u; lowest = 0x10000;
  } else {
    ++i;  // bare continuation byte, or a 5+ byte lead that UTF-8 has no room for
    return 0xFFFD;
  }

  for (size_t k = 1; k <= extra; ++k) {
    if (i + k >= s.size()) {  // truncated at end of input: nothing left to skip to
      i = s.size();
      return 0xFFFD;
    }
    const auto b = static_cast<uint8_t>(s[i + k]);
    if ((b & 0xC0) != 0x80) {
      // Not a continuation byte. Advance exactly one byte past the lead so the
      // character that follows the broken sequence is decoded, not swallowed.
      ++i;
      return 0xFFFD;
    }
    cp = (cp << 6) | (b & 0x3Fu);
  }
  // Overlong, surrogate half, or beyond the Unicode range: not a code point.
  if (cp < lowest || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
    ++i;
    return 0xFFFD;
  }
  i += extra + 1;
  return cp;
}

}  // namespace reader
