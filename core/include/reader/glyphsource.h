#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "reader/tracking.h"

namespace reader {

// One glyph's metrics plus a BORROWED pointer to its packed coverage rows.
//
// It is returned BY VALUE from GlyphSource::glyph() and that is deliberate: a
// pre-rendered face hands out a pointer into its blob, and a scalable face
// hands out a pointer into an evicting cache. A `const Glyph*` into the second
// would dangle the moment the next glyph was rasterised, and it would dangle
// silently -- the drawing loop would read whatever glyph replaced it. A value
// with a borrowed bitmap is the shape both can honour, and the contract is
// written down here rather than left to each face: the bitmap is valid until
// the next call into the same GlyphSource.
struct Glyph {
  int16_t advance, bitmapW, bitmapH, xOff, yOff;  // yOff: baseline to bitmap top, +up
  const uint8_t* bitmap;                          // MSB-first coverage, see coverage()
  // Bytes per bitmap row, ceil(bitmapW * bpp / 8). Stored rather than computed
  // because a Glyph does not know its font's bit depth.
  int16_t stride;
  int rowBytes() const { return stride; }
};

// --- What drawText draws from ------------------------------------------------
//
// Chrome's type is a pre-rendered .rfnt ramp (reader::Font) and body text is a
// TTF rasterised at runtime (reader::ScalableFont), because book CSS asks for
// an unbounded set of sizes -- measured across fixtures: 16, 32, 41, 51 and 64
// px, differing per book -- and a pre-rendered asset per size x weight x style
// is not a finite set of assets. Both go through ONE text path: drawText,
// measure, the wrap and the elide all take this, not a concrete face.
//
// Two paths would drift, and the drift would be one-directional. Every fidelity
// fix this project has made landed in text.cpp or components.cpp -- the
// fractional pen, the single rounding, half-leading, the notdef box being
// measured as well as drawn, the ellipsis kerning across its join -- and a
// second private copy for body text would have inherited none of them, nor any
// of the ones 3B and 3C will make.
//
// --- Why this is an abstract base with state, not a pure interface -----------
//
// Only THREE operations are virtual: glyph(), advance() and kerning(). Those
// are the three that genuinely differ between a table lookup and a rasteriser,
// and they are per-GLYPH.
//
// Everything else is shared and non-virtual, which is a performance decision
// and a correctness one at the same time:
//
//   - coverage() is called per PIXEL. A virtual call there is the one place
//     dispatch could plausibly have cost something real, and it does not need
//     to be virtual at all: a scalable face packs its cache in the same
//     MSB-first 2bpp format fontc.py emits, so ONE implementation serves both.
//     That is also what makes "coverage is 0..3" structural rather than a
//     promise each face keeps separately -- Plane and the Bayer dither read
//     the same scale whatever drew the glyph.
//   - ascent/descent/lineGap/ppem/weight/bpp are five ints that mean the same
//     thing for any face, so they live here and their accessors are direct
//     loads. baselineIn and notdefAdvance read them on every line.
//   - measure() and notdefAdvance() are one implementation each, here. That is
//     the strongest form of "measure() and drawText() cannot disagree about a
//     run's width": there is nothing for a second face to reimplement.
class GlyphSource {
 public:
  virtual ~GlyphSource() = default;

  // The glyph for `cp`, or nullopt if this face has none.
  //
  // MAY RASTERISE, and on a scalable face it does -- so this is the expensive
  // one and it is called only from a drawing pass. `bitmap` is borrowed and
  // valid only until the next call into this same GlyphSource (see Glyph).
  virtual std::optional<Glyph> glyph(char32_t cp) const = 0;

  // The advance for `cp` in whole pixels, or nullopt if this face has no glyph
  // for it -- the same question glyph() answers, without the bitmap.
  //
  // MUST NEVER RASTERISE. This is design decision 3 of the Phase 3A plan and
  // it is the reason the two are separate calls rather than one: measure() is
  // called far more often than drawText -- every wrap decision, every ellipsis
  // fit, every centred line -- and a face that rasterised while measuring
  // would make laying a paragraph out cost what drawing it costs. A TTF's
  // advances are in `hmtx` and reading one does not touch an outline, so the
  // separation costs nothing to honour. test_scalablefont.cpp asserts on the
  // cache's own statistics that a measuring walk populates nothing.
  virtual std::optional<int> advance(char32_t cp) const = 0;

  virtual int kerning(char32_t left, char32_t right) const = 0;

  int ascent() const { return ascent_; }
  int descent() const { return descent_; }
  int lineHeight() const { return ascent_ - descent_ + lineGap_; }

  // The nominal pixel size, which the design's em-stated letter-spacing and
  // line-height resolve against (see components.h's trackingEm), and the weight
  // -- which is what FontSet::load checks a chrome asset's binding with. 0
  // means undeclared.
  int ppem() const { return ppem_; }
  int weight() const { return weight_; }

  // 1 or 2. A 1bpp face reports coverage 0 or 3, so callers never branch on it.
  int bpp() const { return bpp_; }

  // Coverage of one glyph pixel, 0 (none) to 3 (full). Both depths are packed
  // MSB-first: 1bpp is one bit per pixel, 2bpp two bits, four pixels per byte,
  // pixel `col` occupying bits 6 - 2 * (col % 4).
  uint8_t coverage(const Glyph& g, int col, int row) const;

  // Advance of the hollow box drawText paints for a codepoint this face has no
  // glyph for: h = ascent * 2/3, w = h / 2 + 1, then two pixels of side
  // bearing. Spelled once so measure() and drawText() cannot disagree about the
  // width of a run containing one.
  int notdefAdvance() const;

  // `tracking` is added after every glyph, matching drawText, and accumulated
  // in the same 1/64 px unit -- the two must agree exactly or right-aligned
  // text drifts against the run it is aligned on.
  int measure(std::string_view utf8, Tracking tracking = {}) const;

 protected:
  // Borrowed by every accessor above; a subclass fills them when it loads.
  int ascent_ = 0, descent_ = 0, lineGap_ = 0;
  int ppem_ = 0, weight_ = 0;  // 0 = undeclared
  int bpp_ = 1;
};

// Decodes one UTF-8 code point starting at s[i] (i must be < s.size()) and
// advances i past it. Strict: overlong encodings, surrogates (U+D800-U+DFFF),
// code points above U+10FFFF, bare continuation bytes and 5+ byte lead bytes
// all yield U+FFFD.
//
// On a malformed sequence i advances exactly one byte past the offending lead
// byte, so a valid character immediately after a broken one is still decoded.
// The one exception is a sequence truncated by the end of the input, where i
// jumps to s.size() because there is nothing left to resynchronise on.
char32_t utf8Next(std::string_view s, size_t& i);

}  // namespace reader
