#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/font.h"

static std::vector<uint8_t> slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  REQUIRE(f.good());
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

TEST_CASE("font loads and measures text") {
  auto bytes = slurp(std::string(ASSETS_DIR) + "/built/spacegrotesk_16.rfnt");
  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));

  CHECK(font.ascent() > 8);
  CHECK(font.descent() < 0);
  CHECK(font.glyph(U'A') != nullptr);
  CHECK(font.glyph(U'é') != nullptr);   // é (Latin-1)
  CHECK(font.glyph(U'—') != nullptr);   // em dash
  CHECK(font.glyph(0x1F600) == nullptr);     // no emoji

  const int wa = font.measure("A");
  const int wab = font.measure("AB");
  CHECK(wa > 0);
  CHECK(wab > wa);
  CHECK(font.measure("") == 0);
}

TEST_CASE("kerning changes measure when pairs exist") {
  auto bytes = slurp(std::string(ASSETS_DIR) + "/built/literata_18.rfnt");
  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));
  // "AV" should not be wider than advance sum (kerning tightens or is zero).
  const auto* a = font.glyph(U'A');
  const auto* v = font.glyph(U'V');
  REQUIRE(a); REQUIRE(v);
  CHECK(font.measure("AV") <= a->advance + v->advance);
}
