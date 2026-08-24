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

  // --- Why the body face does NOT use that gamma any more ----------------------
  //
  // IT USES A THRESHOLD RAMP, and the two pipelines are deliberately no longer the
  // same. That contradicts the paragraph above, which is left standing because its
  // reasoning about matching Chrome was right and its conclusion was superseded on
  // the panel -- judged on glass, the reference firmware's shape reads better.
  //
  // WHAT THE RAMP IS: 8-bit coverage down to 4 bits, then three linear thresholds
  // on that 0..15 value, at (3, 6, 10) -- the reference firmware's `--darken-aa`.
  //
  // AND IT IS NOT DARKER, which is worth stating because the roadmap recorded it as
  // "3/6/10 against our 4/8/12" and read lower numbers as more ink. The two are not
  // on the same scale. Measured on real text at ppem 32 this ramp carries **97.7%**
  // of the gamma curve's ink, and through the whole firmware path on a rendered page
  // **94.6%** -- 8,076 pixels lighter against 1,961 darker. What it changes is the
  // EDGE: the gamma curve's 0->1 step is at coverage **8/255** and this one's is at
  // **43**, so a glyph edge stops carrying a wide halo of barely-inked level-1
  // pixels. That halo is what read as haze at reading size.
  //
  // NO GAMMA EXPRESSES IT, which is why this is a different form and not a tuned
  // constant: this ramp is crisp at the bottom AND dark at the top (43/94/162).
  // Gamma 1.0 gives the crisp bottom with a light top (43/128/213) and gamma 2.5 the
  // dark top with the hazy bottom (3/46/162). One exponent cannot be both.
  //
  // --- CHROME KEEPS THE GAMMA CURVE, and that asymmetry is the measured part ------
  //
  // The rule this breaks is stated above -- "a per-face knob here is a way for body
  // text and chrome to end up different weights" -- so breaking it needs a number
  // rather than a preference. Applying this ramp to `fontc.py`'s eleven chrome roles
  // costs, by role and ppem:
  //
  //     Meta400   ppem 21   -10.2%      (44.9% of its pixels are AA edge)
  //     Label500  ppem 23   -10.4%      (39.6%)
  //     Value700  ppem 25    -4.8%      (35.4%)
  //     Title700  ppem 42    -3.9%      (21.5%)
  //     Display700 ppem 67   -1.5%      (14.0%)
  //     body serif ppem 32   -2.3%      (15.7%)
  //
  // SMALL TYPE IS MOSTLY EDGE -- 44.9% of Meta400's glyph pixels against 15.7% of
  // the body face's -- so the identical ramp hits it four times harder. Meta400 and
  // Label500 are the hint bar and the row metadata, already at the floor this panel
  // can hold (the boards' ramp says sizes below ~10pt are not legible on this
  // glass). Taking 10% of their ink to win 2.3% on the body is the wrong trade, and
  // nobody asked for it.
  //
  // So body and chrome now differ by ~2.3% of ink at their respective sizes, in
  // different typefaces, and the screen where they sit together is the one that was
  // judged. If that ever reads wrong, the fix is this ramp applied to fontc.py with
  // Meta400 and Label500 measured on the panel FIRST -- not a third curve.
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
