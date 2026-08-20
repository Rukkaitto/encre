#include "reader/text.h"

#include "reader/font.h"
#include "reader/framebuffer.h"

namespace reader {

int drawText(Framebuffer& fb, const Font& font, int x, int baselineY, std::string_view utf8) {
  int pen = x;
  char32_t prev = 0;
  for (size_t i = 0; i < utf8.size();) {
    const char32_t cp = utf8Next(utf8, i);
    const Glyph* g = font.glyph(cp);
    if (!g) continue;
    if (prev) pen += font.kerning(prev, cp);
    for (int row = 0; row < g->bitmapH; ++row) {
      const uint8_t* src = g->bitmap + row * g->rowBytes();
      for (int col = 0; col < g->bitmapW; ++col)
        if ((src[col / 8] >> (7 - col % 8)) & 1)
          fb.setPixel(pen + g->xOff + col, baselineY - g->yOff + row, false);
    }
    pen += g->advance;
    prev = cp;
  }
  return pen - x;
}

}  // namespace reader
