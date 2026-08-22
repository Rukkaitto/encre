// ScalableFont: the runtime-rasterised body face.
//
// A second implementation of stb_truetype is compiled INTO THIS FILE, with
// stb's own default macros, and that is deliberate rather than lazy. The
// firmware's copy (core/src/scalablefont.cpp) replaces every one of stb's
// math.h macros because the ESP32-C3 has no FPU, so the metric checks below
// compare our answers against a *differently configured* stb: if a replacement
// macro ever changed a result, this file is what notices. STBTT_STATIC keeps
// both copies internal to their own translation unit, so there is no ODR
// question.
#include <string>
#include <vector>

#include "doctest.h"
#include "golden.h"
#include "reader/components.h"
#include "reader/font.h"
#include "reader/framebuffer.h"
#include "reader/layout.h"
#include "reader/scalablefont.h"
#include "reader/text.h"

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

using reader::Glyph;
using reader::ScalableFont;

namespace {

// The prepared static instance `make fonts` produces and the firmware embeds.
std::vector<uint8_t> bodyTtf() {
  return golden::slurp(std::string(ASSETS_DIR) + "/built/literata_body.ttf");
}

// The variable font it was prepared FROM, for the one test that has to compare
// the two.
std::vector<uint8_t> variableTtf() {
  return golden::slurp(std::string(ASSETS_DIR) + "/fonts/Literata.ttf");
}

// A face at `sizePx` with a stated budget, ready to draw with.
struct Body {
  std::vector<uint8_t> bytes = bodyTtf();
  ScalableFont face;
  explicit Body(int sizePx = 29, size_t budget = ScalableFont::kDefaultCacheBytes)
      : face(budget) {
    REQUIRE(face.init(bytes.data(), bytes.size(), sizePx));
    REQUIRE(face.ready());
  }
};

// The specimen the tests draw. Composed rather than quoted: it carries the
// things a rendering baseline has to cover and a literary excerpt would not
// reliably contain -- ascenders against descenders, the kern pairs a serif face
// actually adjusts (AV, To, Wa, Ty), an em dash, curly quotes, an accented
// letter, small digits, and a word long enough to force a wrap.
constexpr const char* kSpecimen =
    "The quick brown fox jumps over a lazy dog \xE2\x80\x94 "
    "\xE2\x80\x9C AVAST, To Wander Typographically! \xE2\x80\x9D "
    "caf\xC3\xA9, na\xC3\xAFve, 0123456789.";

}  // namespace

TEST_CASE("THE DEFAULT CACHE HOLDS THE WHOLE WORKING SET AT THE READING SIZE") {
  // The assertion that keeps kDefaultCacheBytes honest. The budget was 8 KB, chosen
  // against "the distinct characters of an English page" -- which is 36 glyphs and
  // fits twice over, and is not the number that matters. The arena is a ring, so
  // what has to fit is the UNION across pages: printable ASCII plus the accents and
  // punctuation every subset carries. At ppem 32 that is 12,292 bytes, which 8 KB
  // never held at any reading size.
  //
  // Measured here rather than trusted, so a face swap or a gamma change that grows
  // the bitmaps fails this instead of quietly thrashing on the pages that use a
  // capital the previous page did not.
  Body b(reader::kBodyPpem, 1u << 20);  // a big cache, to measure with
  size_t bytes = 0;
  int glyphs = 0;
  const char32_t kExtended[] = {
      0xE0, 0xE1, 0xE2, 0xE4, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEE, 0xEF, 0xF1, 0xF4, 0xF6,
      0xF9, 0xFB, 0xFC, 0xC0, 0xC7, 0xC9, 0xD6, 0xDC, 0x2018, 0x2019, 0x201C, 0x201D,
      0x2013, 0x2014, 0x2026, 0xA0, 0xAB, 0xBB, 0xFFFD};
  const auto add = [&](char32_t cp) {
    const std::optional<reader::Glyph> g = b.face.glyph(cp);
    if (!g) return;
    bytes += static_cast<size_t>(g->stride) * g->bitmapH;
    ++glyphs;
  };
  for (char32_t cp = 0x20; cp < 0x7F; ++cp) add(cp);
  for (const char32_t cp : kExtended) add(cp);

  CAPTURE(glyphs);
  CAPTURE(bytes);
  CHECK(glyphs > 100);  // the set is really being walked
  CHECK(bytes <= ScalableFont::kDefaultCacheBytes);
  // And with margin, so one more accented codepoint in a subset does not put it
  // over: the point is a budget that cannot thrash, not one that just fits.
  CHECK(bytes * 100 <= ScalableFont::kDefaultCacheBytes * 85);
}

TEST_CASE("ScalableFont metrics match stb_truetype's own for a known face and size") {
  std::vector<uint8_t> bytes = bodyTtf();
  stbtt_fontinfo info;
  REQUIRE(stbtt_InitFont(&info, bytes.data(), stbtt_GetFontOffsetForIndex(bytes.data(), 0)));

  // Every size the EPUB fixtures were measured asking for, plus chrome's body
  // size, because "unbounded set of sizes" is the reason this class exists.
  for (int sizePx : {16, 29, 32, 41, 51, 64}) {
    ScalableFont face;
    REQUIRE(face.init(bytes.data(), bytes.size(), sizePx));

    // The scale must be EM -> pixels, not ascent-to-descent -> pixels. A
    // stylesheet's `font-size: 32px` is an em, and ScaleForPixelHeight would
    // resolve the same request to a visibly larger face inside a line box the
    // layout had already reserved.
    const float scale = stbtt_ScaleForMappingEmToPixels(&info, static_cast<float>(sizePx));
    CHECK(face.ppem() == sizePx);

    int a = 0, d = 0, g = 0;
    stbtt_GetFontVMetrics(&info, &a, &d, &g);
    CHECK(face.ascent() == static_cast<int>(std::ceil(a * scale)));
    CHECK(face.descent() == static_cast<int>(std::floor(d * scale)));
    CHECK(face.descent() < 0);
    CHECK(face.lineHeight() > face.ascent());

    for (char32_t cp : {U'a', U'A', U'g', U'W', U' ', U'.', U'é', U'—'}) {
      int aw = 0, lsb = 0;
      stbtt_GetCodepointHMetrics(&info, static_cast<int>(cp), &aw, &lsb);
      const int expect = static_cast<int>(aw * scale + 0.5f);
      const std::optional<int> got = face.advance(cp);
      REQUIRE(got.has_value());
      CHECK(*got == expect);
      // ...and the glyph's own advance is the same number, so drawText's pen
      // and measure()'s cannot diverge for a run that is drawn.
      const std::optional<Glyph> gl = face.glyph(cp);
      REQUIRE(gl.has_value());
      CHECK(gl->advance == *got);
    }

    // The bitmap box, which is what positions the glyph against the baseline.
    for (char32_t cp : {U'a', U'g', U'W'}) {
      int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
      stbtt_GetCodepointBitmapBox(&info, static_cast<int>(cp), scale, scale, &x0, &y0, &x1,
                                  &y1);
      const std::optional<Glyph> gl = face.glyph(cp);
      REQUIRE(gl.has_value());
      CHECK(gl->bitmapW == x1 - x0);
      CHECK(gl->bitmapH == y1 - y0);
      CHECK(gl->xOff == x0);
      // stb's box is y-down from the baseline; Glyph::yOff is measured UP.
      CHECK(gl->yOff == -y0);
    }
  }
}

TEST_CASE("ScalableFont reports the weight its OS/2 table declares") {
  Body b;
  // The prepared face is the wght=400 instance `make fonts` pins, and the axis
  // pin is only in the file's variation data -- OS/2's usWeightClass is what a
  // static reader sees, so this is a check that ttfprep.py's instancing wrote it
  // through rather than leaving the variable default's.
  CHECK(b.face.weight() == 400);
}

TEST_CASE("the same string measures identically twice: the cache does not perturb metrics") {
  Body b(32);
  const int first = b.face.measure(kSpecimen);
  // Draw the whole specimen in between, so the cache is populated, wrapped and
  // evicted from before the second measurement.
  reader::Framebuffer fb(480, 200);
  reader::drawText(fb, b.face, 4, 60, kSpecimen);
  const int second = b.face.measure(kSpecimen);
  CHECK(first == second);
  CHECK(first > 0);

  // And a measure taken with tracking is stable too -- that is the path a
  // right-aligned run is placed by.
  const reader::Tracking t = reader::Tracking::em(b.face.ppem(), 120);
  const int tracked = b.face.measure(kSpecimen, t);
  reader::drawText(fb, b.face, 4, 120, kSpecimen, reader::Ink::Black, t);
  CHECK(b.face.measure(kSpecimen, t) == tracked);
  CHECK(tracked > first);
}

TEST_CASE("measure() and the elide never populate the bitmap cache") {
  Body b(32);
  b.face.resetCacheStats();

  // A whole paragraph's worth of measurement: a wrap, an elide and a bare
  // measure, which between them are every measuring caller in the renderer.
  const int w = b.face.measure(kSpecimen);
  const reader::Prose prose = reader::wrapProse(b.face, kSpecimen, 300, 1550);
  const std::string cut = reader::elideToWidth(b.face, kSpecimen, 200);
  CHECK(w > 0);
  CHECK(prose.lineCount() > 1);
  CHECK(cut.size() < std::string(kSpecimen).size());

  // THE assertion. Nothing above rasterised, so nothing above is in the cache:
  // if a future change routes a measurement through glyph(), this is what says
  // so -- and it says so whether or not the rendering happens to still be
  // right, which a pixel test could not.
  const ScalableFont::CacheStats s = b.face.cacheStats();
  CHECK(s.rasterisations == 0);
  CHECK(s.misses == 0);
  CHECK(s.hits == 0);
  CHECK(s.entries == 0);
  CHECK(s.usedBytes == 0);

  // ...and a DRAW of the same text does populate it, so the assertion above is
  // measuring the distinction rather than a face that never caches anything.
  reader::Framebuffer fb(480, 200);
  reader::drawText(fb, b.face, 4, 60, kSpecimen);
  const ScalableFont::CacheStats after = b.face.cacheStats();
  CHECK(after.rasterisations > 0);
  CHECK(after.entries > 0);
}

TEST_CASE("a cache too small to hold a string still draws it correctly, only slower") {
  const std::string text = kSpecimen;
  auto render = [&](size_t budget, ScalableFont::CacheStats* out) {
    std::vector<uint8_t> bytes = bodyTtf();
    ScalableFont face(budget);
    REQUIRE(face.init(bytes.data(), bytes.size(), 32));
    reader::Framebuffer fb(480, 120);
    face.resetCacheStats();
    reader::drawText(fb, face, 4, 60, text);
    *out = face.cacheStats();
    return fb;
  };

  ScalableFont::CacheStats big{}, small{}, tiny{}, none{};
  const reader::Framebuffer roomy = render(64 * 1024, &big);
  const reader::Framebuffer cramped = render(512, &small);
  const reader::Framebuffer minute = render(64, &tiny);
  const reader::Framebuffer zero = render(0, &none);

  // Byte-identical. The cache is a memo, and a memo that changed the answer
  // would be a defect the goldens could only catch by luck of which glyph was
  // evicted.
  const size_t n = roomy.sizeBytes();
  REQUIRE(cramped.sizeBytes() == n);
  CHECK(std::memcmp(roomy.data(), cramped.data(), n) == 0);
  CHECK(std::memcmp(roomy.data(), minute.data(), n) == 0);
  CHECK(std::memcmp(roomy.data(), zero.data(), n) == 0);

  // And it really was cramped: the budget was honoured rather than grown into.
  CHECK(big.capacityBytes == 64 * 1024);
  CHECK(small.capacityBytes == 512);
  CHECK(small.usedBytes <= small.capacityBytes);
  CHECK(small.evictions > 0);
  CHECK(small.rasterisations > big.rasterisations);
  CHECK(big.evictions == 0);

  // 64 bytes cannot hold a 32px glyph at all, and 0 bytes cannot hold anything,
  // so both fall through to the bypass path -- which is what makes "degrades to
  // slower" true at the bottom end and not just in the middle.
  CHECK(tiny.bypasses > 0);
  CHECK(none.capacityBytes == 0);
  CHECK(none.bypasses > 0);
  // Not zero entries: the SPACE is still cached, because it occupies no arena
  // bytes at all. That is the right behaviour rather than an accident of the
  // bookkeeping -- a zero-byte entry costs nothing to keep and a space is the
  // commonest character in a paragraph -- so what a zero budget asserts is that
  // no arena bytes were spoken for, not that the cache is inert.
  CHECK(none.usedBytes == 0);
  CHECK(none.entries == 1);
}

TEST_CASE("the same glyph at two sizes gives different bitmaps") {
  std::vector<uint8_t> bytes = bodyTtf();
  ScalableFont small, large;
  REQUIRE(small.init(bytes.data(), bytes.size(), 16));
  REQUIRE(large.init(bytes.data(), bytes.size(), 64));

  const std::optional<Glyph> a16 = small.glyph(U'a');
  const std::optional<Glyph> a64 = large.glyph(U'a');
  REQUIRE(a16.has_value());
  REQUIRE(a64.has_value());
  CHECK(a64->bitmapW > a16->bitmapW);
  CHECK(a64->bitmapH > a16->bitmapH);
  CHECK(a64->advance > a16->advance);

  // ...and re-initing ONE face at a new size flushes what the old size cached,
  // which is the case a shared face gets when the reader changes its type size.
  ScalableFont one;
  REQUIRE(one.init(bytes.data(), bytes.size(), 16));
  const std::optional<Glyph> before = one.glyph(U'a');
  REQUIRE(before.has_value());
  const int wBefore = before->bitmapW;
  REQUIRE(one.init(bytes.data(), bytes.size(), 64));
  CHECK(one.cacheStats().entries == 0);
  const std::optional<Glyph> after = one.glyph(U'a');
  REQUIRE(after.has_value());
  CHECK(after->bitmapW > wBefore);
}

TEST_CASE("a codepoint the face lacks takes Font's missing-glyph path") {
  Body b(32);
  // U+4E00, the CJK ideograph "one". Literata does not have it, and a face that
  // reported stb's glyph index 0 as a hit would draw the FONT's .notdef box at
  // the FONT's advance -- a different box and a different width from the one
  // drawText paints and Font::notdefAdvance measures.
  constexpr char32_t kAbsent = U'一';
  CHECK_FALSE(b.face.advance(kAbsent).has_value());
  CHECK_FALSE(b.face.glyph(kAbsent).has_value());

  // The invariant that matters is that measure and drawText agree on the width
  // of a run containing one. It is the same invariant test_text.cpp pins for
  // Font, asserted here on the other implementation of the same interface.
  const std::string mixed = "a\xE4\xB8\x80z";
  reader::Framebuffer fb(300, 80);
  const int drawn = reader::drawText(fb, b.face, 10, 50, mixed);
  CHECK(drawn == b.face.measure(mixed));

  // And the box is visible rather than a hole: the run is wider than its two
  // real glyphs, by the notdef advance.
  CHECK(b.face.measure(mixed) > b.face.measure("az"));
  CHECK(b.face.notdefAdvance() > 0);
}

TEST_CASE("ScalableFont coverage is the renderer's 0..3 scale") {
  Body b(32);
  CHECK(b.face.bpp() == 2);
  bool sawPartial = false, sawFull = false, sawNone = false;
  for (char32_t cp : {U'a', U'e', U'W', U'o', U'g'}) {
    const std::optional<Glyph> g = b.face.glyph(cp);
    REQUIRE(g.has_value());
    REQUIRE(g->bitmapW > 0);
    REQUIRE(g->stride == (g->bitmapW * 2 + 7) / 8);
    for (int row = 0; row < g->bitmapH; ++row)
      for (int col = 0; col < g->bitmapW; ++col) {
        const uint8_t cov = b.face.coverage(*g, col, row);
        REQUIRE(cov <= 3);
        if (cov == 0) sawNone = true;
        if (cov == 1 || cov == 2) sawPartial = true;
        if (cov == 3) sawFull = true;
      }
  }
  // All three states have to occur, or the "0..3" claim is vacuous: a face that
  // only ever emitted 0 and 3 would satisfy the bound and would have thrown its
  // anti-aliasing away, which is what Plane::BwDithered exists to keep.
  CHECK(sawNone);
  CHECK(sawPartial);
  CHECK(sawFull);
}

TEST_CASE("a space is cached with metrics and no bitmap") {
  Body b(32);
  b.face.resetCacheStats();
  const std::optional<Glyph> sp = b.face.glyph(U' ');
  REQUIRE(sp.has_value());
  CHECK(sp->bitmapW == 0);
  CHECK(sp->bitmapH == 0);
  CHECK(sp->advance > 0);
  // Cached, because a space is the commonest character in a paragraph -- and
  // cached at zero bytes, so it cannot be evicted by arena pressure.
  CHECK(b.face.cacheStats().entries == 1);
  CHECK(b.face.cacheStats().usedBytes == 0);
  CHECK(b.face.glyph(U' ').has_value());
  CHECK(b.face.cacheStats().hits == 1);
}

TEST_CASE("ScalableFont refuses what it cannot draw with, rather than trying") {
  std::vector<uint8_t> bytes = bodyTtf();
  ScalableFont face;
  CHECK_FALSE(face.init(nullptr, 0, 29));
  CHECK_FALSE(face.ready());
  CHECK_FALSE(face.init(bytes.data(), bytes.size(), 0));
  CHECK_FALSE(face.init(bytes.data(), bytes.size(), -12));
  const std::vector<uint8_t> junk(4096, 0x5A);
  CHECK_FALSE(face.init(junk.data(), junk.size(), 29));
  CHECK_FALSE(face.ready());
  // A face that is not ready answers nothing rather than answering wrongly.
  CHECK_FALSE(face.glyph(U'a').has_value());
  CHECK_FALSE(face.advance(U'a').has_value());
  CHECK(face.kerning(U'A', U'V') == 0);
  // ...and it recovers: a failed init leaves the object usable, not poisoned.
  REQUIRE(face.init(bytes.data(), bytes.size(), 29));
  CHECK(face.glyph(U'a').has_value());
}

// --- Kerning: stb must actually consume the table ttfprep synthesises --------
//
// This test used to assert ZERO, and the zero was real. The finding, and why the
// inversion needs pinning rather than trusting:
//
//   - Literata has no legacy `kern` table of its own. Its kerning is in GPOS.
//   - stb_truetype DOES read pair positioning out of GPOS -- but only
//     LookupType 2, and only where ValueFormat1 is exactly 4.
//   - Literata's `kern` feature is **LookupType 9, Extension Positioning**, and
//     before instancing its ValueFormat1 is 68 (XAdvance | XAdvDevice). stb
//     fails it twice over, so stbtt_GetGlyphKernAdvance returned 0 for every
//     pair in the face and this project had no kerning anywhere.
//
// tools/ttfprep.py now resolves the extension lookups, expands both PairPos
// formats over fontc.py's subset, and writes the result back as a **legacy
// `kern` table, format 0** -- the one form stb reads. 6064 pairs, 36402 bytes.
//
// THE ASSERTION THAT MATTERS is that stb consumes it, asked of stb directly
// rather than through our wrapper: `stbtt_GetKerningTableLength` and
// `stbtt_GetCodepointKernAdvance` on the shipped bytes, in this file's own
// differently-configured copy of stb. A future face whose kern feature moved,
// or a future stb whose legacy reader changed, would put the zero back
// silently -- the glyphs would still draw and every golden but one would still
// pass -- and this is what says so.
//
// Two conditions are load-bearing and neither is obvious:
//   - The table must be ASCENDING by (leftGID << 16) | rightGID, because stb
//     bisects on that key. Unsorted misses pairs rather than failing.
//   - The face must have NO GPOS, because stb is `if (gpos) ... else if (kern)`
//     -- a face that keeps GPOS never reaches the legacy table at all. That is
//     why ttfprep refuses --keep-gpos alongside the synthesis.
TEST_CASE("the shipped face kerns, and stb is what reads it") {
  std::vector<uint8_t> bytes = bodyTtf();
  stbtt_fontinfo info;
  REQUIRE(stbtt_InitFont(&info, bytes.data(), stbtt_GetFontOffsetForIndex(bytes.data(), 0)));

  // stb found a horizontal format-0 subtable and will bisect it. Zero here is
  // the whole regression: no table, or one stb rejected.
  const int tableLength = stbtt_GetKerningTableLength(&info);
  CHECK(tableLength > 0);
  CHECK(tableLength == 6064);

  // Design units, straight out of stb. Negative because a kern TUCKS -- a sign
  // flip would widen every one of these pairs and still be "non-zero".
  for (auto pair : {std::pair<char32_t, char32_t>{U'A', U'V'},
                    {U'T', U'o'},
                    {U'W', U'a'},
                    {U'A', U'W'},
                    {U'r', U'.'},
                    {U'y', U','},
                    {U'T', U'a'},
                    {U'L', U'T'},
                    {U'o', U'v'}}) {
    CAPTURE(static_cast<uint32_t>(pair.first));
    CAPTURE(static_cast<uint32_t>(pair.second));
    const int units = stbtt_GetCodepointKernAdvance(&info, static_cast<int>(pair.first),
                                                    static_cast<int>(pair.second));
    CHECK(units < 0);
  }
  // `fi` is a deliberate exception and worth naming so it is not read as a
  // miss: Literata kerns that pair at 0 and handles it with a GSUB ligature
  // instead, which ttfprep drops because stb does not do substitution either.
  CHECK(stbtt_GetCodepointKernAdvance(&info, 'f', 'i') == 0);

  // And ScalableFont reports exactly stb's number, scaled by its one rounding
  // rule. At 64px every pair above clears a whole pixel; at a small size some
  // legitimately round to zero, which is the .rfnt path's problem too and is
  // stated in tools/fontc.py.
  Body b(64);
  const float scale = stbtt_ScaleForMappingEmToPixels(&info, 64.0f);
  for (auto pair : {std::pair<char32_t, char32_t>{U'A', U'V'},
                    {U'T', U'o'},
                    {U'W', U'a'},
                    {U'L', U'T'}}) {
    const int units = stbtt_GetCodepointKernAdvance(&info, static_cast<int>(pair.first),
                                                    static_cast<int>(pair.second));
    const float exact = units * scale;
    const int expect = exact >= 0.0f ? static_cast<int>(exact + 0.5f)
                                     : -static_cast<int>(-exact + 0.5f);
    CAPTURE(static_cast<uint32_t>(pair.first));
    CHECK(b.face.kerning(pair.first, pair.second) == expect);
    CHECK(b.face.kerning(pair.first, pair.second) < 0);
  }

  // What must hold whether or not the face kerns: measure() applies exactly the
  // adjustment kerning() reports, so the wrap and the draw pick up the same pen.
  // Now that the adjustment is non-zero this is a real equation rather than
  // 0 == 0 -- "AV" is strictly narrower than "A" plus "V".
  const int sum = b.face.measure("A") + b.face.measure("V");
  const int pair = b.face.measure("AV");
  CHECK(pair == sum + b.face.kerning(U'A', U'V'));
  CHECK(pair < sum);
}

TEST_CASE("the prepared TTF renders identically to the variable font it came from") {
  // tools/ttfprep.py resolves Literata's axes and then strips 718,480 bytes of
  // variation and layout tables. That is only sound because stb_truetype does
  // not read them -- an argument, and an argument is exactly what should be
  // checked against the bytes. So: the same face, at the same size, out of both
  // files, compared glyph for glyph.
  std::vector<uint8_t> prepared = bodyTtf();
  std::vector<uint8_t> original = variableTtf();
  ScalableFont a(64 * 1024), b(64 * 1024);
  REQUIRE(a.init(prepared.data(), prepared.size(), 32));
  REQUIRE(b.init(original.data(), original.size(), 32));

  CHECK(a.ascent() == b.ascent());
  CHECK(a.descent() == b.descent());
  CHECK(a.lineHeight() == b.lineHeight());

  int compared = 0;
  for (char32_t cp = 0x20; cp < 0x180; ++cp) {
    const std::optional<Glyph> ga = a.glyph(cp);
    const std::optional<Glyph> gb = b.glyph(cp);
    REQUIRE(ga.has_value() == gb.has_value());
    if (!ga) continue;
    ++compared;
    REQUIRE(ga->advance == gb->advance);
    REQUIRE(ga->bitmapW == gb->bitmapW);
    REQUIRE(ga->bitmapH == gb->bitmapH);
    REQUIRE(ga->xOff == gb->xOff);
    REQUIRE(ga->yOff == gb->yOff);
    for (int row = 0; row < ga->bitmapH; ++row)
      for (int col = 0; col < ga->bitmapW; ++col)
        REQUIRE(a.coverage(*ga, col, row) == b.coverage(*gb, col, row));
  }
  CHECK(compared > 200);
  // Kerning is the ONE thing that deliberately does not agree, and the
  // disagreement is the tool's whole point. The original file's kerning is in
  // GPOS behind an Extension lookup that stb cannot follow, so it reports zero;
  // the prepared file carries the same pairs as a legacy `kern` table, which
  // stb reads. Dropping GPOS therefore took nothing away and the synthesis put
  // something real back. See the kerning test above.
  CHECK(b.kerning(U'A', U'V') == 0);
  CHECK(b.kerning(U'T', U'o') == 0);
  CHECK(a.kerning(U'A', U'V') < 0);
  CHECK(a.kerning(U'T', U'o') < 0);
}

TEST_CASE("chrome's ramp and the body face are one text path") {
  // Both are a GlyphSource, and the point of that is that a caller written
  // against the interface draws either. Not a rendering assertion -- a
  // compile-and-behave one: the same call, twice, on the two implementations.
  Body b(32);
  auto bytes = golden::slurp(std::string(ASSETS_DIR) + "/built/literata_18.rfnt");
  reader::Font ramp;
  REQUIRE(ramp.load(bytes.data(), bytes.size()));

  for (const reader::GlyphSource* f :
       {static_cast<const reader::GlyphSource*>(&ramp),
        static_cast<const reader::GlyphSource*>(&b.face)}) {
    reader::Framebuffer fb(400, 100);
    const int drawn = reader::drawText(fb, *f, 10, 60, "Wander", reader::Ink::Black, {},
                                       reader::Plane::BwDithered);
    CHECK(drawn == f->measure("Wander"));
    CHECK(drawn > 0);
    CHECK(reader::baselineIn(*f, 0, 100) > 0);
  }
}

// --- The golden -------------------------------------------------------------
//
// Body text through the single-pass 1-bit DITHERED path -- Plane::BwDithered,
// which stipples edge coverage through the dispersed Bayer 4x4 instead of
// thresholding it away. Chrome ships Fidelity::Mono because at 21px a stipple
// reads as noise on the stroke, but this is not chrome: it is a paragraph at a
// reading size, where the same stipple reads as a soft edge, and it is the case
// CLAUDE.md already records the 67px numeral losing on Mono.
//
// It goes through wrapProse and drawProse rather than a bare drawText, so what
// is pinned is the whole body-text path a page render will take: the wrap
// measured on the face's own advances, the fractional line box, the half-leading
// baseline per line, and the dither keyed on absolute panel coordinates.
//
// 3C is expected to break this. That is what it is for.
TEST_CASE("body text renders to golden through the dithered path") {
  Body b(32, 64 * 1024);
  const std::string text = kSpecimen;

  reader::Framebuffer fb(480, 300);
  const int column = 480 - 2 * reader::kMargin;
  reader::Prose prose = reader::wrapProse(b.face, text, column, 1550);
  REQUIRE(prose.lineCount() >= 3);
  reader::drawProse(fb, b.face, prose, reader::kMargin, column, reader::pxToF26(24),
                    reader::Ink::Black, reader::Plane::BwDithered, reader::ProseAlign::Left);

  golden::checkGolden(fb, "body_text_dithered");
}
