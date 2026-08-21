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
  REQUIRE_FALSE(font.glyph(0x4E2D).has_value());   // CJK, definitely not in the subset

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

// --- elideToWidth: the runs whose text arrives from the card -------------------
//
// Every case below is a real filename someone will put on a card. The property
// that has to hold for all of them is the one the boards now declare: the run
// occupies at most the width it was given, and it says so with an ellipsis when
// it had to give something up.

namespace {

// Decodes `s` and reports whether any codepoint came back U+FFFD -- which is what
// a cut inside a multi-byte sequence produces, and what fontc.py deliberately has
// a glyph for, so it would render as a box on the end of a name rather than as
// nothing.
bool hasReplacementChar(std::string_view s) {
  for (size_t i = 0; i < s.size();)
    if (reader::utf8Next(s, i) == 0xFFFD) return true;
  return false;
}

bool endsWithEllipsis(std::string_view s) {
  return s.size() >= reader::kEllipsis.size() &&
         s.substr(s.size() - reader::kEllipsis.size()) == reader::kEllipsis;
}

}  // namespace

TEST_CASE("elideToWidth leaves a run that fits untouched, with no ellipsis") {
  ramp::Ramp r;
  const reader::Font& f = r.fonts[reader::Role::Body500];
  const std::string title = "Middlemarch";
  const int w = f.measure(title);
  // Exactly the width it needs is still a fit: `<=`, not `<`. Every golden in the
  // suite depends on this -- their sample titles all fit, and an off-by-one here
  // would put an ellipsis on all fifteen of them.
  CHECK(reader::elideToWidth(f, title, w) == title);
  CHECK(reader::elideToWidth(f, title, w + 1) == title);
  CHECK(reader::elideToWidth(f, title, 10000) == title);
}

TEST_CASE("elideToWidth cuts a run one pixel too wide") {
  ramp::Ramp r;
  const reader::Font& f = r.fonts[reader::Role::Body500];
  const std::string title = "Middlemarch";
  const int w = f.measure(title);
  const std::string cut = reader::elideToWidth(f, title, w - 1);
  CHECK(cut != title);
  CHECK(endsWithEllipsis(cut));
  CHECK(f.measure(cut) <= w - 1);
  // And what survives is a prefix of the original -- nothing reordered, nothing
  // dropped from the middle.
  CHECK(title.compare(0, cut.size() - reader::kEllipsis.size(),
                      cut.substr(0, cut.size() - reader::kEllipsis.size())) == 0);
}

TEST_CASE("elideToWidth cuts inside a single unbroken word") {
  ramp::Ramp r;
  const reader::Font& f = r.fonts[reader::Role::Body500];
  // The shape a real filename has: no spaces at all, so there is no break
  // opportunity anywhere and the only way to make it fit is to cut it.
  const std::string name = "Middlemarch_George_Eliot_1871_unabridged";
  const std::string cut = reader::elideToWidth(f, name, 200);
  CHECK(endsWithEllipsis(cut));
  CHECK(f.measure(cut) <= 200);
  CHECK(cut.size() < name.size());
}

TEST_CASE("elideToWidth returns the ellipsis alone when only the ellipsis fits") {
  ramp::Ramp r;
  const reader::Font& f = r.fonts[reader::Role::Label500];
  const int ellipsisW = f.measure(reader::kEllipsis);
  CHECK(reader::elideToWidth(f, "Middlemarch", ellipsisW) == std::string(reader::kEllipsis));
}

TEST_CASE("elideToWidth returns nothing when not even the ellipsis fits") {
  ramp::Ramp r;
  const reader::Font& f = r.fonts[reader::Role::Label500];
  const int ellipsisW = f.measure(reader::kEllipsis);
  REQUIRE(ellipsisW > 1);
  // Pinned, not undefined: a column too narrow for one glyph gets nothing. The
  // alternative -- an ellipsis hanging out of its box -- is the defect this
  // function exists to remove, so it cannot be the fallback.
  CHECK(reader::elideToWidth(f, "Middlemarch", ellipsisW - 1).empty());
  CHECK(reader::elideToWidth(f, "Middlemarch", 1).empty());
  CHECK(reader::elideToWidth(f, "Middlemarch", 0).empty());
  CHECK(reader::elideToWidth(f, "Middlemarch", -5).empty());
}

TEST_CASE("elideToWidth passes an empty run through at any width") {
  ramp::Ramp r;
  const reader::Font& f = r.fonts[reader::Role::Body500];
  // Nothing measures 0, so it fits -- and an empty run must never acquire an
  // ellipsis, or every Library row with no author would grow one. It is empty at a
  // negative width too, which is the only answer that is not an overhang.
  CHECK(reader::elideToWidth(f, "", 100).empty());
  CHECK(reader::elideToWidth(f, "", 0).empty());
  CHECK(reader::elideToWidth(f, "", -1).empty());
}

TEST_CASE("elideToWidth never cuts inside a multi-byte character") {
  ramp::Ramp r;
  const reader::Font& f = r.fonts[reader::Role::Body500];
  // Two-byte Latin-1 (in the subset -- the Library board's own CHARLOTTE BRONTE
  // has one) and a three-byte typographic quote, so a naive byte-wise cut lands
  // mid-sequence at many widths rather than at one lucky one.
  const std::string name =
      "\xC3\x89\x6D\x69\x6C\x65 \xE2\x80\x9C\xC3\x84\xC3\x96\xC3\x9C\xE2\x80\x9D "
      "\xC3\xA9\xC3\xA8\xC3\xAA\xC3\xAB";
  const int full = f.measure(name);
  REQUIRE_FALSE(hasReplacementChar(name));
  // Sweep every width the run could be given, including past its own end. A cut
  // that split a sequence would show up as a U+FFFD at some width, and this is the
  // only way to find WHICH width rather than trusting one sample.
  for (int maxW = -2; maxW <= full + 3; ++maxW) {
    const std::string cut = reader::elideToWidth(f, name, maxW);
    CHECK_MESSAGE(!hasReplacementChar(cut), "broken sequence at maxW " << maxW);
    if (!cut.empty()) CHECK_MESSAGE(f.measure(cut) <= maxW, "overhang at maxW " << maxW);
    if (maxW >= full) CHECK(cut == name);
  }
}

TEST_CASE("elideToWidth measures with the tracking the run will be drawn with") {
  ramp::Ramp r;
  const reader::Font& f = r.fonts[reader::Role::Label500];
  // 0.22em on a 23px face is 5.06px a character -- the band label's own spacing,
  // and the case that makes a pre-rounded tracking drift. A budget wide enough for
  // the untracked run must still cut the tracked one.
  const reader::Tracking t = reader::Tracking::em(f.ppem(), 220);
  const std::string name = "MIDDLEMARCH";
  const int plain = f.measure(name);
  REQUIRE(f.measure(name, t) > plain);
  CHECK(reader::elideToWidth(f, name, plain, t) != name);
  CHECK(f.measure(reader::elideToWidth(f, name, plain, t), t) <= plain);
  // And the same budget with no tracking is a fit, so the difference really is
  // the tracking and not the width.
  CHECK(reader::elideToWidth(f, name, plain) == name);
}

TEST_CASE("drawTextElided draws the elided run and no ink past its budget") {
  ramp::Ramp r;
  const reader::Font& f = r.fonts[reader::Role::Body500];
  const std::string name = "Middlemarch_George_Eliot_1871_unabridged";
  const int maxW = 200;
  const int x = 24;

  reader::Framebuffer fb(480, 80);
  fb.clear(true);
  const int advance = reader::drawTextElided(fb, f, x, 50, name, maxW);
  // The advance is the elided run's, so a caller can still place something after
  // it -- and it is inside the budget, which is the whole promise.
  CHECK(advance == f.measure(reader::elideToWidth(f, name, maxW)));
  CHECK(advance <= maxW);

  // The stronger check: no INK past the budget. An advance can be right while a
  // glyph's bitmap overhangs it, and it is the ink the user sees.
  int rightmost = -1;
  for (int y = 0; y < fb.height(); ++y)
    for (int px = 0; px < fb.width(); ++px)
      if (!fb.getPixel(px, y) && px > rightmost) rightmost = px;
  CHECK(rightmost >= x);
  CHECK(rightmost < x + maxW);

  // And it is byte-for-byte the same frame as drawing elideToWidth's own output.
  reader::Framebuffer same(480, 80);
  same.clear(true);
  reader::drawText(same, f, x, 50, reader::elideToWidth(f, name, maxW));
  bool identical = true;
  for (int y = 0; y < fb.height() && identical; ++y)
    for (int px = 0; px < fb.width(); ++px)
      if (fb.getPixel(px, y) != same.getPixel(px, y)) { identical = false; break; }
  CHECK(identical);
}
