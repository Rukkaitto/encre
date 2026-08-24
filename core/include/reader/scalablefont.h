#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "reader/glyphsource.h"

namespace reader {

// --- Body text, rasterised at runtime ----------------------------------------
//
// Chrome's type is a finite ramp of eleven roles, so it is pre-rendered
// (reader::Font). Book CSS is not finite: measured across the EPUB fixtures a
// publisher's stylesheet asks for 16, 32, 41, 51 and 64 px, and the set differs
// per book. A pre-rendered asset per size x weight x style is therefore not a
// set of assets, and snapping a heading to the nearest built size draws it at
// the wrong size inside a line box the layout reserved for the right one.
//
// So body text comes from a TTF rasterised on demand by stb_truetype, and
// **chrome keeps its ramp**, where the boards are pixel-exact and the goldens
// live. Both satisfy GlyphSource, so both go through one drawText.
//
// The TTF itself stays in memory-mapped flash as a `const` array
// (shell/src/font_body_serif.h), so the FONT DATA costs zero RAM. Only the
// glyph cache costs RAM, and that is the number the constructor takes.
//
// --- Coverage is the same 0..3 scale, and the same CURVE ----------------------
//
// coverage() is GlyphSource's, non-virtual, shared with Font: this class packs
// its cache in the identical MSB-first 2bpp layout tools/fontc.py emits, so
// Plane, the Bayer dither and every threshold in text.cpp work on body text
// unchanged and by construction rather than by promise.
//
// The quantisation applies the SAME transfer curve fontc.py applies, and that
// is not cosmetic. Both FreeType and stb_truetype hand back *linear* coverage --
// the fraction of the pixel the outline covers -- and no shipping text
// rasteriser puts that on screen unmodified; Skia (so Chrome, so the design
// boards) runs the mask through a gamma/contrast LUT first. fontc.py measured
// the difference at 88% of Chrome's ink mass on the same string, and corrects it
// with gamma 2.0. A scalable face quantising raw stb coverage would render body
// text ~12% lighter than the chrome beside it and than the design, on the same
// screen, for a reason no test names. See kCoverageGamma.
//
// --- The cache is bounded, and eviction is a status not a crash ---------------
//
// The device build is `-fno-exceptions`, so a container that cannot allocate
// calls abort() with no diagnostic and no serial line. The cache therefore
// takes a byte budget AT CONSTRUCTION, allocates exactly that once, and evicts
// within it -- it never grows to fit. A full cache degrades to *slower*, never
// to *dead*.
//
// It is a constructor argument rather than a constant so 3C can tune it against
// measured page-render times instead of a guess made now.
class ScalableFont : public GlyphSource {
 public:
  // The transfer curve on coverage before it is quantised to 0..3, and the same
  // number tools/fontc.py defaults to. Stated as a constant rather than a
  // parameter because the two pipelines agreeing is the point: a per-face knob
  // here is a way for body text and chrome to end up different weights.
  static constexpr float kCoverageGamma = 2.0f;

  // --- The A/B: an alternative CURVE SHAPE, not another gamma ------------------
  //
  // The reference firmware quantises differently: it rasterises to 4 bits and
  // thresholds that 0..15 value linearly, at (4, 8, 12) by default and at
  // (3, 6, 10) under `--darken-aa`. The roadmap recorded this as "3/6/10 against
  // our 4/8/12" and read it as us being LIGHTER. Measured on real rendered text at
  // ppem 32, that is backwards: our curve carries **100%** of the ink, their
  // darken-aa **97.7%** and their default **93.9%**. Adopting theirs would make
  // body text lighter.
  //
  // THE REAL DIFFERENCE IS SHAPE, AND NO GAMMA EXPRESSES IT. Our 0->1 step is at
  // coverage **8/255** where their darken-aa needs **43** -- so every glyph edge
  // carries a wide halo of level-1 pixels, which on this glass may read as haze
  // rather than as smoothness. Their ramp is crisp at the bottom AND dark at the
  // top (43/94/162); gamma 1.0 gives the crisp bottom with a light top
  // (43/128/213), and gamma 2.5 the dark top with our hazy bottom (3/46/162).
  // One exponent cannot be both.
  //
  // So this is a threshold ramp rather than a gamma, selected by
  // `-DENCRE_AA_THRESHOLDS_4BIT` and OFF by default -- the desktop build never
  // sets it, so the goldens stay blessed against the shipping curve. It exists to
  // be compared ON THE PANEL, which is the only place the question can be
  // settled: 15.7% of a page's glyph pixels are anti-aliased edge, and the whole
  // disagreement lives in those.
  //
  // IT CHANGES THE BODY FACE ONLY. Chrome is a pre-rendered Space Grotesk ramp
  // built by fontc.py, and regenerating twelve assets per variant to test a
  // question about body text is not worth it -- so a body-vs-chrome weight
  // difference in a variant build is an artifact of the experiment, not of the
  // candidate.
  static constexpr uint8_t kAaThresholds4Bit[3] = {3, 6, 10};

  // Sized to hold the WHOLE working set of the reading face, measured rather than
  // scaled -- because both of the obvious estimates are wrong, in opposite
  // directions.
  //
  // What a DRAWING pass actually needs per page is small: over 5,000 pages of real
  // EPUBs at ppem 32, the worst single page used 36 distinct glyphs and 3,272
  // bytes of bitmap, and an 8 KB cache evicted nothing at all. A measuring pass
  // needs none of it (see GlyphSource::advance). So "a page of English prose" is
  // not the number to size against -- it fits twice over.
  //
  // What the cache has to survive is the UNION across pages, because the arena is
  // a ring: a page that introduces a capital or an accent the last one did not
  // advances the write pointer, and when it wraps it overwrites whatever is oldest,
  // which includes glyphs as hot as 'e'. Measured on assets/built/literata_body.ttf,
  // printable ASCII plus the 32 accented and punctuation codepoints fontc.py puts
  // in every subset:
  //
  //     ppem 29: 10,378 B      ppem 36: 15,359 B      ppem 48: 25,854 B
  //     ppem 32: 12,292 B      ppem 41: 19,284 B
  //
  // Bytes go as ppem squared, so 8 KB does not hold the set at ANY reading size --
  // not even the 29px it was chosen at. 16 KB holds ppem 32 with 25% spare, which
  // is what the reader ships at, and it costs 8 KB of a 320 KB heap the firmware
  // currently uses 5.8% of. The alternative is re-rasterising at ~3,794us a glyph
  // on the pages that thrash, which is the one cost the whole advance()/glyph()
  // split exists to avoid.
  //
  // A BODY SIZE SETTING MUST REVISIT THIS. design/Settings.dc.html has a `Size`
  // row; at 41px the set is 19,284 B and this budget thrashes. The table above is
  // the data for that decision, and the budget is a constructor argument precisely
  // so the caller can size it from the chosen ppem rather than from this default.
  static constexpr size_t kDefaultCacheBytes = 16 * 1024;

  explicit ScalableFont(size_t cacheBudgetBytes = kDefaultCacheBytes);
  ~ScalableFont() override;
  ScalableFont(const ScalableFont&) = delete;
  ScalableFont& operator=(const ScalableFont&) = delete;

  // `ttf` must outlive this object and is never copied -- it is the flash array.
  // `sizePx` is the em size in pixels, which is what a stylesheet's
  // `font-size: 16px` means; ppem() reports it back.
  //
  // Re-initing at a new size FLUSHES the cache (a cached bitmap belongs to one
  // size) but does not reallocate the arena, so changing the reading size cannot
  // fragment the heap.
  //
  // False on a font stb_truetype will not parse, on a non-positive size, or if
  // the cache could not be allocated at all -- every allocation this class makes
  // is `new (std::nothrow)`, so an out-of-memory device gets a false here rather
  // than an abort.
  bool init(const uint8_t* ttf, size_t len, int sizePx);
  bool ready() const;

  // MAY RASTERISE (that is the whole point) and populates the cache. The
  // returned `bitmap` is borrowed from the cache and is valid only until the
  // next call into this object -- see Glyph.
  std::optional<Glyph> glyph(char32_t cp) const override;

  // NEVER RASTERISES and never touches the cache: stb_truetype reads an advance
  // out of `hmtx` without looking at an outline. This is design decision 3, and
  // test_scalablefont.cpp asserts it on cacheStats() rather than trusting it.
  std::optional<int> advance(char32_t cp) const override;

  // Pair kerning out of the shipped face's legacy `kern` table. Literata has no
  // such table of its own -- its kerning is in GPOS, behind LookupType 9
  // Extension lookups that stb_truetype does not implement, so asking stb for
  // GPOS kerning here returned 0 for every pair in the face. tools/ttfprep.py
  // resolves those lookups and writes the pairs back as a format-0 `kern`
  // table, which is the form stb does read; it also drops GPOS, which is what
  // lets stb reach the legacy table at all (stbtt_GetGlyphKernAdvance is
  // `if (gpos) ... else if (kern)`).
  //
  // Scaled and rounded the same way an advance is, so measure() and drawText()
  // accumulate identical pens. Whole pixels, so a pair whose adjustment is
  // under half a pixel at the current size reports 0 -- that is the format's
  // resolution, not a missing pair.
  int kerning(char32_t left, char32_t right) const override;

 private:
  // The glyph index for a codepoint, from a 256-entry Latin-1 cache where it can
  // be. See the cache's own comment in scalablefont.cpp: a wrap measures every
  // glyph several times and each measure did three cmap binary searches.
  int gidFor(char32_t cp) const;

 public:

  // --- What the cache is doing ----------------------------------------------
  //
  // Exposed because two of this task's design decisions are unenforceable
  // without it: "a measuring walk populates nothing" is a statement about
  // `rasterisations`, and "a too-small cache still draws correctly, only
  // slower" is a statement about `evictions` being non-zero while the pixels
  // are unchanged. A test that could only look at the rendering would pass on a
  // cache that had quietly grown to fit.
  struct CacheStats {
    size_t capacityBytes = 0;  // the bitmap arena: the constructor's budget
    size_t usedBytes = 0;      // bytes of it currently spoken for by live entries
    size_t overheadBytes = 0;  // the entry table, which the budget also pays for
    int entries = 0;           // live entries
    int capacityEntries = 0;
    unsigned long hits = 0;
    unsigned long misses = 0;
    unsigned long rasterisations = 0;  // == misses, unless a raster failed
    unsigned long evictions = 0;
    unsigned long wraps = 0;  // times the arena's write pointer went round
    // Glyphs bigger than the WHOLE arena, which are rasterised into a scratch
    // buffer and not cached. That path exists because "a cache too small to hold
    // a string still draws it, only slower" has to hold for a cache too small to
    // hold even ONE glyph -- including a budget of zero. Refusing instead would
    // make drawText paint a notdef box, which is not slower, it is wrong.
    unsigned long bypasses = 0;
  };
  CacheStats cacheStats() const;
  void resetCacheStats() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace reader
