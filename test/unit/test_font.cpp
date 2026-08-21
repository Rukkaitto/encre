#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/font.h"
#include "rfnt_builder.h"

static std::vector<uint8_t> slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  REQUIRE(f.good());
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

TEST_CASE("font loads and measures text") {
  auto bytes = slurp(std::string(ASSETS_DIR) + "/built/spacegrotesk_500_14pt.rfnt");
  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));

  CHECK(font.ascent() > 8);
  CHECK(font.descent() < 0);
  CHECK(font.glyph(U'A').has_value());
  CHECK(font.glyph(U'é').has_value());   // é (Latin-1)
  CHECK(font.glyph(U'—').has_value());   // em dash
  CHECK_FALSE(font.glyph(0x1F600).has_value());  // no emoji

  const int wa = font.measure("A");
  const int wab = font.measure("AB");
  CHECK(wa > 0);
  CHECK(wab > wa);
  CHECK(font.measure("") == 0);
}

// Both shipped .rfnt files carry zero kern pairs (fontc finds no GPOS kerning
// FreeType exposes through get_kerning), so kerning can only be exercised on a
// synthetic font. Without this the kern-record parse and the (left << 32) |
// right key packing were dead code in the test suite.
TEST_CASE("kerning is parsed and applied by measure") {
  rfnt::Builder b;
  rfnt::GlyphRec a;
  a.cp = U'A';
  a.advance = 10;
  a.bitmapW = 8;
  a.bitmapH = 1;
  a.bitmapOffset = 0;
  rfnt::GlyphRec v = a;
  v.cp = U'V';
  v.advance = 12;
  v.bitmapOffset = 1;
  b.glyphs = {a, v};
  b.blob = {0xAA, 0x55};
  b.kerns = {{U'A', U'V', -3}};
  const auto bytes = b.build();

  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));

  CHECK(font.kerning(U'A', U'V') == -3);
  CHECK(font.kerning(U'V', U'A') == 0);  // the pair is directional
  CHECK(font.kerning(U'A', U'A') == 0);
  // Exactly advance + advance + kern: no rounding, no dropped adjustment.
  CHECK(font.measure("AV") == 10 + 12 - 3);
  CHECK(font.measure("VA") == 12 + 10);
  CHECK(font.measure("A") == 10);
  // Kerning applies at every junction, not just the first.
  CHECK(font.measure("AVAV") == 4 * 11 - 2 * 3);
}

TEST_CASE("kern keys do not collide across the 32-bit split") {
  // The key is (left << 32) | right. A narrower key, or one built by adding
  // rather than shifting, would confuse these three pairs.
  rfnt::Builder b;
  rfnt::GlyphRec g;
  g.advance = 4;
  g.bitmapW = 8;
  g.bitmapH = 1;
  for (char32_t cp : {U'A', U'B', U'—', U'\U0001F600'}) {
    g.cp = static_cast<uint32_t>(cp);
    b.glyphs.push_back(g);
  }
  b.blob = {0xF0};
  b.kerns = {{U'A', U'B', -1},
             {U'B', U'A', 7},
             {0x2014, 0x1F600, -5},
             {0x1F600, 0x2014, 2}};
  const auto bytes = b.build();

  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));
  CHECK(font.kerning(U'A', U'B') == -1);
  CHECK(font.kerning(U'B', U'A') == 7);
  CHECK(font.kerning(0x2014, 0x1F600) == -5);
  CHECK(font.kerning(0x1F600, 0x2014) == 2);
  CHECK(font.kerning(U'A', 0x1F600) == 0);
  CHECK(font.measure("A—") == 8);  // no pair: plain advance sum
}
