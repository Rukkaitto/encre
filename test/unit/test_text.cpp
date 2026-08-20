#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/font.h"
#include "reader/framebuffer.h"
#include "reader/text.h"
#include "rfnt_builder.h"

static std::vector<uint8_t> slurpFont(const char* name) {
  std::ifstream f(std::string(ASSETS_DIR) + "/built/" + name, std::ios::binary);
  REQUIRE(f.good());
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

TEST_CASE("drawText can draw white ink on a black field") {
  auto bytes = slurpFont("spacegrotesk_500_14pt.rfnt");
  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));

  reader::Framebuffer black(64, 32);
  black.fillRect(0, 0, 64, 32, false);          // all black
  const int advance = reader::drawText(black, font, 4, 22, "A", reader::Ink::White);
  CHECK(advance > 0);

  // Some pixel inside the glyph must now be white, and the field still black.
  bool anyWhite = false;
  for (int y = 0; y < 32 && !anyWhite; ++y)
    for (int x = 0; x < 64 && !anyWhite; ++x)
      if (black.getPixel(x, y)) anyWhite = true;
  CHECK(anyWhite);
  CHECK_FALSE(black.getPixel(63, 31));          // untouched corner stays black
}

TEST_CASE("black ink is still the default") {
  auto bytes = slurpFont("spacegrotesk_500_14pt.rfnt");
  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));
  reader::Framebuffer white(64, 32);
  reader::drawText(white, font, 4, 22, "A");
  bool anyBlack = false;
  for (int y = 0; y < 32 && !anyBlack; ++y)
    for (int x = 0; x < 64 && !anyBlack; ++x)
      if (!white.getPixel(x, y)) anyBlack = true;
  CHECK(anyBlack);
}

TEST_CASE("measure accounts for tracking and agrees with drawText") {
  auto bytes = slurpFont("spacegrotesk_500_14pt.rfnt");
  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));

  const reader::Tracking two = reader::Tracking::px(2);
  const int plain = font.measure("LIBRARY");
  const int tracked = font.measure("LIBRARY", two);
  CHECK(tracked == plain + 2 * 7);  // 7 glyphs, 2px each -- and the last one too

  // The advance drawText reports must equal what measure predicts, or
  // right-aligned chrome drifts.
  reader::Framebuffer fb(400, 40);
  CHECK(reader::drawText(fb, font, 0, 30, "LIBRARY", reader::Ink::Black, two) == tracked);
}

TEST_CASE("a missing glyph draws a visible box rather than nothing") {
  auto bytes = slurpFont("spacegrotesk_500_14pt.rfnt");
  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));
  REQUIRE(font.glyph(0x4E2D) == nullptr);   // CJK, definitely not in the subset

  reader::Framebuffer fb(64, 32);
  const int advance = reader::drawText(fb, font, 4, 24, "\xE4\xB8\xAD");  // U+4E2D
  CHECK(advance > 0);                        // it occupies space

  int inked = 0;
  for (int y = 0; y < 32; ++y)
    for (int x = 0; x < 64; ++x)
      if (!fb.getPixel(x, y)) ++inked;
  CHECK(inked > 0);                          // and it is visible

  // A hollow box: its interior is untouched, so it reads as a placeholder
  // rather than a solid blob.
  CHECK(inked < 4 * font.ascent());
}

TEST_CASE("each plane emits its own bit of a glyph's coverage") {
  // Synthetic 2bpp glyph: one row, coverage 0,1,2,3.
  rfnt::Builder b;
  b.bpp = 2;
  rfnt::GlyphRec g;
  g.cp = U'A';
  g.advance = 5;
  g.bitmapW = 4;
  g.bitmapH = 1;
  g.yOff = 1;
  b.glyphs.push_back(g);
  b.blob = {0b00011011};
  const auto bytes = b.build();

  reader::Font f;
  REQUIRE(f.load(bytes.data(), bytes.size()));

  auto inkAt = [&](reader::Plane plane, int px) {
    reader::Framebuffer fb(16, 4);
    reader::drawText(fb, f, 0, 1, "A", reader::Ink::Black, {}, plane);
    return !fb.getPixel(px, 0);  // true when this pixel got ink
  };

  // Bw: ink where coverage >= 2, i.e. pixels 2 and 3.
  CHECK_FALSE(inkAt(reader::Plane::Bw, 0));
  CHECK_FALSE(inkAt(reader::Plane::Bw, 1));
  CHECK(inkAt(reader::Plane::Bw, 2));
  CHECK(inkAt(reader::Plane::Bw, 3));

  // Lsb: bit 0 set, i.e. coverage 1 and 3.
  CHECK_FALSE(inkAt(reader::Plane::Lsb, 0));
  CHECK(inkAt(reader::Plane::Lsb, 1));
  CHECK_FALSE(inkAt(reader::Plane::Lsb, 2));
  CHECK(inkAt(reader::Plane::Lsb, 3));

  // Msb: bit 1 set, i.e. coverage 2 and 3.
  CHECK_FALSE(inkAt(reader::Plane::Msb, 0));
  CHECK_FALSE(inkAt(reader::Plane::Msb, 1));
  CHECK(inkAt(reader::Plane::Msb, 2));
  CHECK(inkAt(reader::Plane::Msb, 3));
}

TEST_CASE("a 1bpp font is identical in every plane") {
  auto bytes = slurpFont("literata_18.rfnt");  // still 1bpp
  reader::Font f;
  REQUIRE(f.load(bytes.data(), bytes.size()));
  auto render = [&](reader::Plane plane) {
    reader::Framebuffer fb(200, 40);
    reader::drawText(fb, f, 4, 30, "Aa", reader::Ink::Black, {}, plane);
    int n = 0;
    for (int y = 0; y < 40; ++y)
      for (int x = 0; x < 200; ++x)
        if (!fb.getPixel(x, y)) ++n;
    return n;
  };
  const int bw = render(reader::Plane::Bw);
  CHECK(bw > 0);
  CHECK(render(reader::Plane::Lsb) == bw);
  CHECK(render(reader::Plane::Msb) == bw);
}
