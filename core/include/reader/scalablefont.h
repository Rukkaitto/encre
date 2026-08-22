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

  // A default that is deliberately small (decision 4 says "a few KB"), sized so
  // one line of body text at a reading size fits and a page does not: at 29px a
  // lowercase glyph is ~14x20 at 2bpp, about 80 bytes, so 8 KB is ~100 glyphs --
  // more than the distinct characters of an English page, which is what a cache
  // for a *drawing* pass actually has to hold (see GlyphSource::advance: a
  // measuring pass populates nothing).
  static constexpr size_t kDefaultCacheBytes = 8 * 1024;

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

  // GPOS pair kerning, which is where Literata keeps its (it has no `kern`
  // table). Scaled and rounded the same way an advance is, so measure() and
  // drawText() accumulate identical pens.
  int kerning(char32_t left, char32_t right) const override;

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
