// Font::load is the trust boundary for .rfnt blobs: both callers (sim/shell)
// treat a true return as "safe to draw with". These tests feed it corrupt
// buffers and require rejection, because anything accepted here is dereferenced
// by drawText without further checks.
#include <vector>

#include "doctest.h"
#include "reader/font.h"
#include "rfnt_builder.h"

namespace {

// One 8x2 glyph plus a matching 2-byte blob: the minimal well-formed font.
rfnt::Builder minimalFont() {
  rfnt::Builder b;
  rfnt::GlyphRec g;
  g.cp = U'A';
  g.advance = 9;
  g.bitmapW = 8;
  g.bitmapH = 2;
  g.bitmapOffset = 0;
  b.glyphs.push_back(g);
  b.blob = {0xFF, 0x81};
  return b;
}

}  // namespace

TEST_CASE("Font::load accepts a well-formed synthetic font") {
  const auto bytes = minimalFont().build();
  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));
  REQUIRE(font.glyph(U'A') != nullptr);
  CHECK(font.glyph(U'A')->bitmap[0] == 0xFF);
}

TEST_CASE("Font::load rejects a bad magic") {
  auto b = minimalFont();
  b.magic = "RFNU";
  const auto bytes = b.build();
  reader::Font font;
  CHECK_FALSE(font.load(bytes.data(), bytes.size()));
}

TEST_CASE("Font::load rejects an unknown version") {
  auto b = minimalFont();
  b.version = 2;
  const auto bytes = b.build();
  reader::Font font;
  CHECK_FALSE(font.load(bytes.data(), bytes.size()));
}

TEST_CASE("Font::load rejects a short header") {
  const auto bytes = minimalFont().build();
  reader::Font font;
  CHECK_FALSE(font.load(bytes.data(), 15));
  CHECK_FALSE(font.load(bytes.data(), 0));
}

TEST_CASE("Font::load rejects a truncated glyph table") {
  auto b = minimalFont();
  b.glyphCountOverride = 4;  // header promises 4 glyphs, only 1 record follows
  const auto bytes = b.build();
  reader::Font font;
  CHECK_FALSE(font.load(bytes.data(), bytes.size()));
}

TEST_CASE("Font::load rejects a truncated kern table") {
  auto b = minimalFont();
  b.kerns.push_back({U'A', U'V', -1});
  b.kernCountOverride = 8;  // header promises 8 kern pairs, one record follows
  const auto bytes = b.build();
  reader::Font font;
  CHECK_FALSE(font.load(bytes.data(), bytes.size()));
}

TEST_CASE("Font::load rejects a bitmapOffset past the end of the blob") {
  auto b = minimalFont();
  b.glyphs[0].bitmapOffset = 0x1000;  // blob is 2 bytes
  const auto bytes = b.build();
  reader::Font font;
  CHECK_FALSE(font.load(bytes.data(), bytes.size()));
}

TEST_CASE("Font::load rejects a bitmap extent past the end of the blob") {
  auto b = minimalFont();
  b.glyphs[0].bitmapH = 64;  // 64 rows x 1 byte, blob holds 2 bytes
  const auto bytes = b.build();
  reader::Font font;
  CHECK_FALSE(font.load(bytes.data(), bytes.size()));
}

TEST_CASE("Font::load rejects negative bitmap dimensions") {
  {
    auto b = minimalFont();
    b.glyphs[0].bitmapW = -8;
    const auto bytes = b.build();
    reader::Font font;
    CHECK_FALSE(font.load(bytes.data(), bytes.size()));
  }
  {
    auto b = minimalFont();
    b.glyphs[0].bitmapH = -2;
    const auto bytes = b.build();
    reader::Font font;
    CHECK_FALSE(font.load(bytes.data(), bytes.size()));
  }
}

TEST_CASE("Font::load rejects absurd bitmap dimensions") {
  {
    auto b = minimalFont();
    b.glyphs[0].bitmapW = 513;
    b.glyphs[0].bitmapH = 0;  // extent stays in range; only the width is absurd
    const auto bytes = b.build();
    reader::Font font;
    CHECK_FALSE(font.load(bytes.data(), bytes.size()));
  }
  {
    auto b = minimalFont();
    b.glyphs[0].bitmapW = 0;
    b.glyphs[0].bitmapH = 513;
    const auto bytes = b.build();
    reader::Font font;
    CHECK_FALSE(font.load(bytes.data(), bytes.size()));
  }
}

TEST_CASE("Font::load leaves no state behind after a failure") {
  const auto good = minimalFont().build();
  auto bad = minimalFont();
  bad.glyphs[0].bitmapOffset = 0x1000;
  const auto badBytes = bad.build();

  reader::Font font;
  REQUIRE(font.load(good.data(), good.size()));
  REQUIRE(font.glyph(U'A') != nullptr);

  CHECK_FALSE(font.load(badBytes.data(), badBytes.size()));
  CHECK(font.glyph(U'A') == nullptr);  // glyphs from the previous load are gone
  CHECK(font.ascent() == 0);
  CHECK(font.descent() == 0);
  CHECK(font.lineHeight() == 0);
  CHECK(font.measure("A") == 0);
}

TEST_CASE("Font::load is idempotent when called twice with the same font") {
  const auto bytes = minimalFont().build();
  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));
  const int first = font.measure("AA");
  REQUIRE(font.load(bytes.data(), bytes.size()));
  CHECK(font.measure("AA") == first);
}
