#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "ramp.h"
#include "reader/dither.h"
#include "reader/font.h"
#include "reader/fontset.h"
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

// --- The dithered 1-bit plane ----------------------------------------------

TEST_CASE("the dithered plane keeps solid coverage solid and blank coverage blank") {
  // The whole point of stippling edges is that it must NOT stipple interiors:
  // a glyph's solid body dithered to 75% would read as a grey, hollow letter.
  // And zero coverage must stay paper, or every glyph gains a halo.
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 4; ++x) {
      const int b = reader::bayer4(x, y);
      CHECK((3 * 16) / 3 > b);        // full coverage always inks
      CHECK_FALSE((0 * 16) / 3 > b);  // no coverage never inks
    }
}

TEST_CASE("the dithered plane's ink density is monotonic in coverage") {
  // 5, 10 and 16 cells of every 16, so a lighter edge is never denser than a
  // darker one -- the property that makes the stipple read as tone.
  int inked[4] = {0, 0, 0, 0};
  for (int cov = 0; cov < 4; ++cov)
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 4; ++x)
        if ((cov * 16) / 3 > reader::bayer4(x, y)) ++inked[cov];
  CHECK(inked[0] == 0);
  CHECK(inked[1] == 5);
  CHECK(inked[2] == 10);
  CHECK(inked[3] == 16);
  CHECK(inked[0] < inked[1]);
  CHECK(inked[1] < inked[2]);
  CHECK(inked[2] < inked[3]);
}

TEST_CASE("the edge dither is dispersed, not clustered") {
  // ditherRect's matrix is clustered on purpose, to match the board's cover
  // tint. Edges need the opposite: clustering ink on a stem edge reads as the
  // stroke thickening rather than smoothing -- which is what the panel actually
  // looked like when a clustered pattern was in this path. Pin that these two
  // are different so nobody "unifies" them.
  //
  // The distinguishing property is adjacency, not row/column spread: Bayer 4x4
  // is recursive, so its four lowest ranks land on a 2x2 sub-lattice (rows and
  // columns 0 and 2) rather than one per row. What makes it dispersed is that
  // none of them TOUCH -- the clustered matrix puts its four lowest in a
  // contiguous 2x2 blob at the tile's centre.
  int lowX[4] = {}, lowY[4] = {}, n = 0;
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 4; ++x)
      if (reader::bayer4(x, y) < 4) {
        REQUIRE(n < 4);
        lowX[n] = x;
        lowY[n] = y;
        ++n;
      }
  REQUIRE(n == 4);
  for (int i = 0; i < 4; ++i)
    for (int j = i + 1; j < 4; ++j) {
      const int dx = lowX[i] - lowX[j], dy = lowY[i] - lowY[j];
      const bool orthogonallyAdjacent = (dx == 0 && (dy == 1 || dy == -1)) ||
                                        (dy == 0 && (dx == 1 || dx == -1));
      CHECK_FALSE(orthogonallyAdjacent);
    }
}

TEST_CASE("a dithered glyph reproduces the glyph's ink mass better than thresholding") {
  // This is the actual quality claim, and it is not "dithering inks more".
  // Measured, it inks slightly LESS than the Bw threshold: thresholding at
  // cov >= 2 pays FULL ink for every half-covered pixel and nothing at all for
  // quarter-covered ones, which both over-inks edges and drops the faintest
  // ones. The stipple pays 10/16 and 5/16 instead. Whether that is more or
  // fewer pixels depends on the glyph; what must hold is that it lands closer
  // to the true coverage mass, because that is what makes an edge read as a
  // smooth contour instead of a staircase.
  ramp::Ramp r;
  const reader::Font& f = r.fonts[reader::Role::Display700];

  auto render = [&](reader::Plane p) {
    reader::Framebuffer fb(480, 200);
    fb.clear(true);
    reader::drawText(fb, f, 20, 120, "6%", reader::Ink::Black, {}, p);
    return fb;
  };
  auto inkCount = [](const reader::Framebuffer& fb) {
    int n = 0;
    for (int y = 0; y < fb.height(); ++y)
      for (int x = 0; x < fb.width(); ++x)
        if (!fb.getPixel(x, y)) ++n;
    return n;
  };

  // True mass: rebuild each pixel's 0..3 coverage from the two grey planes the
  // panel combines, and sum it as a fraction of full ink.
  const reader::Framebuffer lsb = render(reader::Plane::Lsb);
  const reader::Framebuffer msb = render(reader::Plane::Msb);
  double mass = 0.0;
  for (int y = 0; y < lsb.height(); ++y)
    for (int x = 0; x < lsb.width(); ++x) {
      const int cov = (!msb.getPixel(x, y) ? 2 : 0) + (!lsb.getPixel(x, y) ? 1 : 0);
      mass += cov / 3.0;
    }
  REQUIRE(mass > 0.0);

  const double bw = inkCount(render(reader::Plane::Bw));
  const double dithered = inkCount(render(reader::Plane::BwDithered));
  const double bwError = bw > mass ? bw - mass : mass - bw;
  const double ditherError = dithered > mass ? dithered - mass : mass - dithered;
  CHECK_MESSAGE(ditherError < bwError, "mass " << mass << ", bw " << bw << " (err "
                                               << bwError << "), dithered " << dithered
                                               << " (err " << ditherError << ")");
}
