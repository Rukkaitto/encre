#include "reader/text.h"

#include "reader/font.h"
#include "reader/framebuffer.h"

namespace reader {

int drawText(Framebuffer& fb, const Font& font, int x, int baselineY, std::string_view utf8,
             Ink ink, int tracking) {
  const bool white = (ink == Ink::White);
  int pen = x;
  char32_t prev = 0;
  for (size_t i = 0; i < utf8.size();) {
    const char32_t cp = utf8Next(utf8, i);
    const Glyph* g = font.glyph(cp);
    if (!g) {
      // No glyph for this codepoint: draw a hollow box so malformed or
      // out-of-subset text is visibly wrong instead of silently invisible.
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
      pen += w + 2 + tracking;
      prev = 0;
      continue;
    }
    if (prev) pen += font.kerning(prev, cp);
    for (int row = 0; row < g->bitmapH; ++row) {
      const uint8_t* src = g->bitmap + row * g->rowBytes();
      for (int col = 0; col < g->bitmapW; ++col)
        if ((src[col / 8] >> (7 - col % 8)) & 1)
          fb.setPixel(pen + g->xOff + col, baselineY - g->yOff + row, white);
    }
    pen += g->advance + tracking;
    prev = cp;
  }
  return pen - x;
}

}  // namespace reader
