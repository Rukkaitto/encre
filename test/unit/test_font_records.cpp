// Font::glyph() and Font::kerning() search the asset's own tables in place --
// there is no index any more (see the note at the top of reader/font.h; it cost
// 99,008 bytes of heap across the chrome ramp). A search that assumes an order
// the bytes do not have fails by MISSING records, which renders as notdef boxes
// rather than as a crash, so it is exactly the kind of defect a golden catches
// late and a unit test should catch first.
//
// These tests come in two halves:
//
//   1. The committed assets really are in ascending order, checked against the
//      BYTES rather than against the loader's opinion of them. That was
//      asserted from reading tools/fontc.py before this change, which is not the
//      same thing.
//   2. Every record in an asset is reachable, and resolves to exactly the fields
//      the raw record holds -- including a bitmap pointer that still points into
//      the caller's blob rather than at a copy.
//
// Plus the fallback: a table that is NOT in order is still searched correctly,
// linearly. fontc.py's codepoint list is hand-maintained and its comment invites
// appending to the end, so this is the case that stops a future append from
// quietly breaking lookups.
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/font.h"
#include "reader/fontset.h"
#include "rfnt_builder.h"

namespace {

// Every .rfnt the Makefile's `fonts` target writes: the eleven chrome ramp
// faces plus the Literata body face. Named rather than globbed, so an asset
// that stops being generated fails here instead of silently leaving the set.
const char* kAssets[] = {
    "spacegrotesk_400_10pt.rfnt", "spacegrotesk_500_10pt.rfnt",
    "spacegrotesk_400_11pt.rfnt", "spacegrotesk_500_11pt.rfnt",
    "spacegrotesk_500_12pt.rfnt", "spacegrotesk_700_12pt.rfnt",
    "spacegrotesk_400_14pt.rfnt", "spacegrotesk_500_14pt.rfnt",
    "spacegrotesk_700_14pt.rfnt", "spacegrotesk_700_20pt.rfnt",
    "spacegrotesk_700_32pt.rfnt", "literata_18.rfnt",
};

std::vector<uint8_t> slurp(const std::string& name) {
  std::ifstream f(std::string(ASSETS_DIR) + "/built/" + name, std::ios::binary);
  REQUIRE_MESSAGE(f.good(), name);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

template <typename T>
T rd(const std::vector<uint8_t>& d, size_t at) {
  T v;
  REQUIRE(at + sizeof(T) <= d.size());
  std::memcpy(&v, d.data() + at, sizeof(T));
  return v;
}

// The .rfnt layout, parsed here independently of Font::load so the two are not
// checking each other against a shared misreading. Mirrors rfnt_builder.h's
// documented header.
struct Tables {
  size_t glyphsAt = 0, kernsAt = 0, blobAt = 0;
  uint16_t glyphCount = 0, kernCount = 0;
  int bpp = 1;
};

Tables tablesOf(const std::vector<uint8_t>& d) {
  Tables t;
  const uint16_t versionWord = rd<uint16_t>(d, 4);
  const uint8_t version = versionWord & 0xFF;
  t.bpp = (versionWord >> 8) ? (versionWord >> 8) : 1;
  t.glyphCount = rd<uint16_t>(d, 6);
  t.kernCount = rd<uint16_t>(d, 14);
  t.glyphsAt = version >= 2 ? 20u : 16u;
  t.kernsAt = t.glyphsAt + t.glyphCount * 18u;
  t.blobAt = t.kernsAt + t.kernCount * 12u;
  return t;
}

}  // namespace

TEST_CASE("every committed .rfnt has a strictly ascending glyph table") {
  // The precondition for bisecting. tools/fontc.py emits its CODEPOINTS list in
  // order and appends new members at the end -- 0xFFFD was appended exactly that
  // way -- so this holds today and would stop holding the first time something
  // is appended that is not the largest codepoint in the set.
  for (const char* name : kAssets) {
    const auto d = slurp(name);
    const Tables t = tablesOf(d);
    CAPTURE(name);
    REQUIRE(t.glyphCount > 0);
    REQUIRE(t.blobAt <= d.size());
    uint32_t prev = 0;
    for (uint16_t i = 0; i < t.glyphCount; ++i) {
      const uint32_t cp = rd<uint32_t>(d, t.glyphsAt + i * 18u);
      if (i > 0) REQUIRE_MESSAGE(cp > prev, name);
      prev = cp;
    }
    // The kern table, and this loop used to assert nothing: every committed
    // asset had zero pairs, because FreeType's get_kerning reads only the legacy
    // `kern` table and neither face has one (their kerning is GPOS -- see
    // tools/gposkern.py). tools/fontc.py reads GPOS directly now, so the table
    // is non-empty in all twelve and the kern search finally bisects real
    // records. **Non-empty is asserted, not assumed**: a generator that silently
    // stopped finding pairs would put the project back where it was and the only
    // symptom would be text a couple of pixels wide of the boards.
    REQUIRE_MESSAGE(t.kernCount > 0, name);
    uint64_t prevKey = 0;
    for (uint16_t i = 0; i < t.kernCount; ++i) {
      const size_t at = t.kernsAt + i * 12u;
      const uint64_t key =
          (static_cast<uint64_t>(rd<uint32_t>(d, at)) << 32) | rd<uint32_t>(d, at + 4);
      if (i > 0) REQUIRE_MESSAGE(key > prevKey, name);
      prevKey = key;
      // A kern of zero is a record that costs 12 bytes and changes nothing --
      // Font::kerning already answers 0 for a pair it has no record for -- and
      // fontc.py drops those, so one here means the filter stopped working.
      REQUIRE_MESSAGE(rd<int32_t>(d, at + 8) != 0, name);
    }
  }
}

TEST_CASE("every record in every committed .rfnt kern table is reachable") {
  // The other half of the glyph-table test, on the table that had no coverage
  // from any real asset until now: Phase 3A gave Font::kerning's bisection
  // synthetic coverage only, so a bisection bug would have shown up as text
  // drifting a pixel or two on some screens and nothing else. These are real
  // pairs -- 1297 to 2923 per face -- so the search is exercised at depth ~11.
  for (const char* name : kAssets) {
    const auto d = slurp(name);
    const Tables t = tablesOf(d);
    reader::Font font;
    REQUIRE(font.load(d.data(), d.size()));
    CAPTURE(name);
    for (uint16_t i = 0; i < t.kernCount; ++i) {
      const size_t at = t.kernsAt + i * 12u;
      const auto left = static_cast<char32_t>(rd<uint32_t>(d, at));
      const auto right = static_cast<char32_t>(rd<uint32_t>(d, at + 4));
      REQUIRE(font.kerning(left, right) == rd<int32_t>(d, at + 8));
      // Every kerned pair is a pair of glyphs the face actually has, or the
      // adjustment is applied to a notdef box and the subsets have diverged.
      REQUIRE(font.glyph(left).has_value());
      REQUIRE(font.glyph(right).has_value());
    }
    // And a pair that is not in the table is zero, not the neighbour's value:
    // U+0020 against itself, which no face kerns.
    CHECK(font.kerning(U' ', U' ') == 0);
  }
}

TEST_CASE("every record in every committed .rfnt is reachable and decodes exactly") {
  // The whole table, record by record, against the raw bytes: a search that
  // found the neighbour of the right record (an off-by-one in the bisection's
  // half-open bounds is the classic) would return a plausible glyph with the
  // wrong advance and the wrong bitmap, and every assertion below is one of the
  // fields that would move.
  for (const char* name : kAssets) {
    const auto d = slurp(name);
    const Tables t = tablesOf(d);
    reader::Font font;
    REQUIRE(font.load(d.data(), d.size()));
    CAPTURE(name);
    for (uint16_t i = 0; i < t.glyphCount; ++i) {
      const size_t at = t.glyphsAt + i * 18u;
      const auto cp = static_cast<char32_t>(rd<uint32_t>(d, at));
      const auto g = font.glyph(cp);
      REQUIRE_MESSAGE(g.has_value(), name);
      REQUIRE(g->advance == rd<int16_t>(d, at + 4));
      REQUIRE(g->bitmapW == rd<int16_t>(d, at + 6));
      REQUIRE(g->bitmapH == rd<int16_t>(d, at + 8));
      REQUIRE(g->xOff == rd<int16_t>(d, at + 10));
      REQUIRE(g->yOff == rd<int16_t>(d, at + 12));
      // The stride is not in the record: a Glyph does not know its font's bit
      // depth, so the font computes it. ceil(w * bpp / 8).
      REQUIRE(g->rowBytes() == (g->bitmapW * t.bpp + 7) / 8);
      // ZERO COPIES. The bitmap must still point into the blob the caller
      // owns, at the record's own offset -- this is the assertion that
      // "borrowed, not copied" is a fact rather than a comment.
      REQUIRE(g->bitmap == d.data() + t.blobAt + rd<uint32_t>(d, at + 14));
    }
  }
}

TEST_CASE("an absent codepoint is absent, on every side of the table") {
  // The three ways a bisection goes wrong at the edges: below the first record,
  // above the last, and in the gaps the subset leaves in the middle. The
  // committed faces cover 0x20..0x7E and 0xA0..0xFF plus nine typographic marks
  // in 0x2013..0x203A, so the holes are real and known.
  const auto d = slurp("spacegrotesk_400_14pt.rfnt");
  reader::Font font;
  REQUIRE(font.load(d.data(), d.size()));
  const Tables t = tablesOf(d);
  const auto first = static_cast<char32_t>(rd<uint32_t>(d, t.glyphsAt));
  const auto last =
      static_cast<char32_t>(rd<uint32_t>(d, t.glyphsAt + (t.glyphCount - 1) * 18u));
  REQUIRE(first == 0x20);
  REQUIRE(font.glyph(first).has_value());
  REQUIRE(font.glyph(last).has_value());

  CHECK_FALSE(font.glyph(0x00).has_value());       // far below the first
  CHECK_FALSE(font.glyph(first - 1).has_value());  // one below the first
  CHECK_FALSE(font.glyph(last + 1).has_value());   // one above the last
  CHECK_FALSE(font.glyph(0x10FFFF).has_value());   // far above the last
  // Gaps in the middle: DEL and the C1 range, the Latin-Extended-A block, and
  // the run just below the typographic marks.
  for (char32_t cp : {0x7Fu, 0x80u, 0x9Fu, 0x100u, 0x1FFFu, 0x2012u, 0x2020u}) {
    CAPTURE(static_cast<uint32_t>(cp));
    CHECK_FALSE(font.glyph(cp).has_value());
  }
}

TEST_CASE("the whole ramp measures the same as it did with a hash index") {
  // A regression guard with real numbers rather than a tautology: if the search
  // resolves even one glyph differently on any face of the ramp, a measurement
  // moves and right-aligned text drifts against the margin it is aligned to.
  // (The goldens are the pixel-level proof; this is the one that says WHICH
  // face went wrong.)
  //
  // The widths came from the unordered_map implementation and every one of them
  // moved when the ramp gained kerning -- by 2 to 5 px, always NARROWER, which
  // is the only direction a kern can move a run in either face here. So the
  // constant alone is no longer self-evidently right, and `kern` is what makes
  // it auditable: the assertion below is that `expected` is the sum of the
  // face's own advances plus exactly that much kerning, which is a statement
  // about the asset rather than a number someone copied out of a failure.
  const struct {
    reader::Role role;
    const char* asset;
    int expected;
    int kern;
  } cases[] = {
      {reader::Role::Meta400, "spacegrotesk_400_10pt.rfnt", 261, -2},
      {reader::Role::Meta500, "spacegrotesk_500_10pt.rfnt", 270, -2},
      {reader::Role::Label400, "spacegrotesk_400_11pt.rfnt", 287, -2},
      {reader::Role::Label500, "spacegrotesk_500_11pt.rfnt", 293, -2},
      {reader::Role::Value500, "spacegrotesk_500_12pt.rfnt", 312, -2},
      {reader::Role::Value700, "spacegrotesk_700_12pt.rfnt", 316, -2},
      {reader::Role::Body400, "spacegrotesk_400_14pt.rfnt", 376, -3},
      {reader::Role::Body500, "spacegrotesk_500_14pt.rfnt", 373, -3},
      {reader::Role::Body700, "spacegrotesk_700_14pt.rfnt", 368, -3},
      {reader::Role::Title700, "spacegrotesk_700_20pt.rfnt", 537, -3},
      {reader::Role::Display700, "spacegrotesk_700_32pt.rfnt", 850, -5},
  };
  // Mixed case, digits, punctuation, a Latin-1 accent and an em dash: one
  // string that touches all four blocks the subset covers.
  const std::string s = "Wyoming 1969 — réédition";
  for (const auto& c : cases) {
    const auto d = slurp(c.asset);
    reader::Font font;
    REQUIRE(font.load(d.data(), d.size()));
    CAPTURE(c.asset);
    CHECK(font.measure(s) == c.expected);
    // The same run with the kerning taken back out, glyph by glyph. measure()
    // is advances plus kerns and nothing else on this face, so the difference
    // is the kerning -- and it must be negative, because a face that kerned a
    // run WIDER would be a sign error in the generator that every golden would
    // pass through as "text moved a bit".
    int unkerned = 0;
    size_t i = 0;
    while (i < s.size()) {
      const char32_t cp = reader::utf8Next(s, i);
      const std::optional<int> adv = font.advance(cp);
      unkerned += adv ? *adv : font.notdefAdvance();
    }
    CHECK(c.expected - unkerned == c.kern);
    CHECK(c.kern < 0);
  }
}

TEST_CASE("an out-of-order glyph table is still searched correctly") {
  // THE FALLBACK. Bisecting an unordered table silently misses records, and the
  // symptom (some characters become notdef boxes) points at the font, not at
  // the search. So load() decides once whether the table ascends and the search
  // scans linearly when it does not.
  //
  // The order below is what a careless append to fontc.py's CODEPOINTS list
  // produces: a codepoint added at the end that is not the largest.
  rfnt::Builder b;
  rfnt::GlyphRec g;
  g.bitmapW = 8;
  g.bitmapH = 1;
  for (auto [cp, adv] : {std::pair<uint32_t, int16_t>{U'A', 11},
                         {U'Z', 13},
                         {0x2014, 17},
                         {U'B', 19}}) {  // out of order, deliberately
    g.cp = cp;
    g.advance = adv;
    b.glyphs.push_back(g);
  }
  b.blob = {0xFF};
  const auto bytes = b.build();

  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));
  // Every record is still found, including the one out of place and the one it
  // displaced.
  REQUIRE(font.glyph(U'A').has_value());
  CHECK(font.glyph(U'A')->advance == 11);
  REQUIRE(font.glyph(U'B').has_value());
  CHECK(font.glyph(U'B')->advance == 19);
  REQUIRE(font.glyph(U'Z').has_value());
  CHECK(font.glyph(U'Z')->advance == 13);
  REQUIRE(font.glyph(0x2014).has_value());
  CHECK(font.glyph(0x2014)->advance == 17);
  // And absence is still absence: a linear scan must not report a near miss.
  CHECK_FALSE(font.glyph(U'C').has_value());
  CHECK_FALSE(font.glyph(0x2013).has_value());
  // measure() goes through the same search, so it sees all four advances.
  CHECK(font.measure("AB") == 11 + 19);
}

TEST_CASE("an out-of-order kern table is still searched correctly") {
  // Same fallback, second table. No committed asset has a kern pair at all, so
  // without this the kern search's unordered path has no coverage anywhere.
  rfnt::Builder b;
  rfnt::GlyphRec g;
  g.advance = 10;
  g.bitmapW = 8;
  g.bitmapH = 1;
  for (char32_t cp : {U'A', U'V', U'W'}) {
    g.cp = static_cast<uint32_t>(cp);
    b.glyphs.push_back(g);
  }
  b.blob = {0xFF};
  // Descending by the packed (left << 32) | right key.
  b.kerns = {{U'W', U'A', -5}, {U'A', U'W', -2}, {U'A', U'V', -3}};
  const auto bytes = b.build();

  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));
  CHECK(font.kerning(U'A', U'V') == -3);
  CHECK(font.kerning(U'A', U'W') == -2);
  CHECK(font.kerning(U'W', U'A') == -5);
  CHECK(font.kerning(U'V', U'A') == 0);
  CHECK(font.kerning(U'W', U'V') == 0);
}

TEST_CASE("a sorted kern table is found at both ends and in the middle") {
  // The ordered path, with enough pairs that the bisection actually recurses:
  // an off-by-one in the half-open bounds typically loses the first or the last
  // record only, so both extremes are named explicitly.
  rfnt::Builder b;
  rfnt::GlyphRec g;
  g.advance = 10;
  g.bitmapW = 8;
  g.bitmapH = 1;
  for (char32_t cp = U'A'; cp <= U'P'; ++cp) {
    g.cp = static_cast<uint32_t>(cp);
    b.glyphs.push_back(g);
  }
  b.blob = {0xFF};
  for (char32_t cp = U'A'; cp <= U'P'; ++cp)
    b.kerns.push_back({static_cast<uint32_t>(cp), U'x',
                       static_cast<int32_t>(cp - U'A' + 1)});
  const auto bytes = b.build();

  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));
  for (char32_t cp = U'A'; cp <= U'P'; ++cp) {
    CAPTURE(static_cast<uint32_t>(cp));
    CHECK(font.kerning(cp, U'x') == static_cast<int>(cp - U'A' + 1));
  }
  CHECK(font.kerning(U'@', U'x') == 0);  // one below the first key
  CHECK(font.kerning(U'Q', U'x') == 0);  // one above the last key
  CHECK(font.kerning(U'A', U'y') == 0);  // right half of the key differs
}

TEST_CASE("an empty glyph table is searched without touching anything") {
  // glyphCount 0 with a matching table: the bisection's bounds are [0, 0) and
  // the scan's loop body never runs. Under ASAN this is what catches a search
  // that reads a record before checking the count.
  rfnt::Builder b;
  b.blob = {};
  const auto bytes = b.build();
  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));
  CHECK_FALSE(font.glyph(U'A').has_value());
  CHECK(font.kerning(U'A', U'V') == 0);
  // measure() then draws a notdef box per codepoint, which is the existing
  // contract for a face with no glyph -- not a crash and not zero.
  CHECK(font.measure("A") == font.notdefAdvance());
}
