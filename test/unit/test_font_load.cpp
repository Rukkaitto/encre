// Font::load is the trust boundary for .rfnt blobs: both callers (sim/shell)
// treat a true return as "safe to draw with". These tests feed it corrupt
// buffers and require rejection, because anything accepted here is dereferenced
// by drawText without further checks.
#include <vector>

#include "doctest.h"
#include "reader/font.h"
#include "rfnt_builder.h"

namespace {

// A single glyph `w` px wide and one row tall, whose row is the given byte.
// Used for the depth tests, where the packed coverage bytes are hand-written so
// the loader is checked against the format rather than against itself.
rfnt::Builder oneRowFont(int bpp, int16_t w, uint8_t row) {
  rfnt::Builder b;
  b.bpp = bpp;
  rfnt::GlyphRec g;
  g.cp = U'A';
  g.advance = 5;
  g.bitmapW = w;
  g.bitmapH = 1;
  g.yOff = 1;
  b.glyphs.push_back(g);
  b.blob = {row};
  return b;
}

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
  REQUIRE(font.glyph(U'A').has_value());
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
  // 1 and 2 are the formats that exist; 3 is not one of them, and neither is 0.
  for (uint16_t version : {uint16_t(0), uint16_t(3), uint16_t(255)}) {
    auto b = minimalFont();
    b.version = version;
    const auto bytes = b.build();
    reader::Font font;
    CAPTURE(version);
    CHECK_FALSE(font.load(bytes.data(), bytes.size()));
  }
}

TEST_CASE("Font::load reads format 2's declared size and weight, and format 1 has none") {
  // Format 2 appends ppem and weight after the kern count. They are what the
  // type ramp binds a role to a face by (reader/fontset.h), so a loader that
  // read them from the wrong offset would silently make every role unbindable.
  auto b = minimalFont();
  b.version = 2;
  b.ppem = 29;
  b.weight = 400;
  const auto bytes = b.build();
  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));
  CHECK(font.ppem() == 29);
  CHECK(font.weight() == 400);
  // The glyph table moved 4 bytes along with the header and is still read.
  CHECK(font.glyph(U'A').has_value());

  // A format-1 asset declares neither and says so with 0 rather than with a
  // plausible-looking guess.
  const auto v1 = minimalFont().build();
  reader::Font old;
  REQUIRE(old.load(v1.data(), v1.size()));
  CHECK(old.ppem() == 0);
  CHECK(old.weight() == 0);
  CHECK(old.glyph(U'A').has_value());
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
  REQUIRE(font.glyph(U'A').has_value());

  CHECK_FALSE(font.load(badBytes.data(), badBytes.size()));
  CHECK_FALSE(font.glyph(U'A').has_value());  // the previous load's table is gone
  CHECK(font.ascent() == 0);
  CHECK(font.descent() == 0);
  CHECK(font.lineHeight() == 0);
  CHECK(font.ppem() == 0);
  CHECK(font.weight() == 0);
  // Not measure("A") == 0: an empty font has no glyph for 'A', and measure now
  // reports the width of the notdef box drawText paints for it (see
  // Font::notdefAdvance) rather than skipping it. On a zeroed face that box is
  // degenerate, which is the only reason the number is small -- it is not
  // evidence of a clean slate, so the assertions above are.
  CHECK(font.measure("A") == font.notdefAdvance());
}

TEST_CASE("Font::load is idempotent when called twice with the same font") {
  const auto bytes = minimalFont().build();
  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));
  const int first = font.measure("AA");
  REQUIRE(font.load(bytes.data(), bytes.size()));
  CHECK(font.measure("AA") == first);
}

TEST_CASE("Font::load accepts a v2 2bpp font and reports its depth") {
  // One glyph, 4px wide, coverage 0..3 across its single row.
  const auto bytes = oneRowFont(/*bpp=*/2, /*w=*/4, 0b00011011).build();
  reader::Font f;
  REQUIRE(f.load(bytes.data(), bytes.size()));
  CHECK(f.bpp() == 2);
  const auto g = f.glyph(U'A');
  REQUIRE(g.has_value());
  CHECK(g->rowBytes() == 1);  // 4 px * 2 bpp = 1 byte
  CHECK(f.coverage(*g, 0, 0) == 0);
  CHECK(f.coverage(*g, 1, 0) == 1);
  CHECK(f.coverage(*g, 2, 0) == 2);
  CHECK(f.coverage(*g, 3, 0) == 3);
}

TEST_CASE("a v1 font still loads and reports 1bpp with binary coverage") {
  const auto bytes = oneRowFont(/*bpp=*/1, /*w=*/3, 0b10100000).build();
  reader::Font f;
  REQUIRE(f.load(bytes.data(), bytes.size()));
  CHECK(f.bpp() == 1);
  const auto g = f.glyph(U'A');
  REQUIRE(g.has_value());
  CHECK(g->rowBytes() == 1);
  CHECK(f.coverage(*g, 0, 0) == 3);  // a set 1bpp bit is full coverage
  CHECK(f.coverage(*g, 1, 0) == 0);
  CHECK(f.coverage(*g, 2, 0) == 3);
}

TEST_CASE("a 2bpp row is one byte wider every four pixels") {
  // 5 px at 2 bpp needs 2 bytes per row; the fifth pixel is the top of byte 1.
  auto b = oneRowFont(/*bpp=*/2, /*w=*/5, 0b11000000);
  b.blob = {0b00000000, 0b10000000};
  const auto bytes = b.build();
  reader::Font f;
  REQUIRE(f.load(bytes.data(), bytes.size()));
  const auto g = f.glyph(U'A');
  REQUIRE(g.has_value());
  CHECK(g->rowBytes() == 2);
  CHECK(f.coverage(*g, 3, 0) == 0);
  CHECK(f.coverage(*g, 4, 0) == 2);
}

TEST_CASE("a 2bpp bitmap extent is validated at the wider stride") {
  // 8 px at 2 bpp is 2 bytes per row, so a 2-row glyph needs 4 blob bytes.
  auto b = oneRowFont(/*bpp=*/2, /*w=*/8, 0);
  b.glyphs[0].bitmapH = 2;
  b.blob = {0, 0, 0};  // one byte short at 2 bpp, but enough if read as 1 bpp
  const auto bytes = b.build();
  reader::Font f;
  CHECK_FALSE(f.load(bytes.data(), bytes.size()));
}

TEST_CASE("an unknown bit depth is rejected") {
  {
    auto b = oneRowFont(/*bpp=*/1, /*w=*/2, 0xFF);
    auto bytes = b.build();
    bytes[4] = 0x03;  // version low byte 3: not a version we know
    reader::Font f;
    CHECK_FALSE(f.load(bytes.data(), bytes.size()));
  }
  {
    auto b = oneRowFont(/*bpp=*/1, /*w=*/2, 0xFF);
    auto bytes = b.build();
    bytes[5] = 0x04;  // version high byte 4: a depth we cannot decode
    reader::Font f;
    CHECK_FALSE(f.load(bytes.data(), bytes.size()));
  }
}

TEST_CASE("a rejected 2bpp font leaves the depth back at 1") {
  const auto good = oneRowFont(/*bpp=*/2, /*w=*/4, 0b00011011).build();
  auto bad = oneRowFont(/*bpp=*/2, /*w=*/4, 0b00011011);
  bad.glyphs[0].bitmapOffset = 0x1000;
  const auto badBytes = bad.build();

  reader::Font f;
  REQUIRE(f.load(good.data(), good.size()));
  REQUIRE(f.bpp() == 2);
  CHECK_FALSE(f.load(badBytes.data(), badBytes.size()));
  CHECK(f.bpp() == 1);
}
