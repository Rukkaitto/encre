#pragma once
#include <cstdint>
#include <string_view>
#include <unordered_map>

#include "reader/tracking.h"

namespace reader {

struct Glyph {
  int16_t advance, bitmapW, bitmapH, xOff, yOff;  // yOff: baseline to bitmap top, +up
  const uint8_t* bitmap;                          // MSB-first coverage, see Font::coverage
  // Bytes per bitmap row, ceil(bitmapW * bpp / 8). Stored rather than computed
  // because a Glyph does not know its font's bit depth.
  int16_t stride;
  int rowBytes() const { return stride; }
};

// Zero-copy view over an .rfnt blob; the blob must outlive the Font.
class Font {
 public:
  bool load(const uint8_t* data, size_t size);
  const Glyph* glyph(char32_t cp) const;
  int kerning(char32_t left, char32_t right) const;
  int ascent() const { return ascent_; }
  int descent() const { return descent_; }
  int lineHeight() const { return ascent_ - descent_ + lineGap_; }

  // --- What this face IS, not merely how it measures ------------------------
  //
  // An .rfnt used to carry ascent, descent and line gap and nothing else, so a
  // Font could not answer "what size are you?" or "what weight are you?". Both
  // questions have callers. The design states letter-spacing in em, which is
  // meaningless without a pixel size, and it was resolved instead against a
  // duplicate table of the ramp's sizes kept in components.h -- a second
  // source of truth that no build step checked against the assets. And the
  // roles of the type ramp name a weight (see reader/fontset.h): nothing could
  // verify that the blob bound to a 400 role was not in fact the 500 asset,
  // which is precisely the mistake that shipped -- Home's author line rendered
  // ~19% heavier than the board for exactly that reason.
  //
  // Both are declared by the generator (tools/fontc.py knows the --pt it asked
  // FreeType for and the --weight it pinned the variation axis to) and ride in
  // the v2 header. A v1 asset reports 0 for both, meaning "undeclared".
  int ppem() const { return ppem_; }
  int weight() const { return weight_; }

  // `tracking` is added after every glyph, matching drawText, and accumulated
  // in the same 1/64 px unit -- the two must agree exactly or right-aligned
  // text drifts against the run it is aligned on.
  int measure(std::string_view utf8, Tracking tracking = {}) const;
  // Advance of the hollow box drawText paints for a codepoint this face has no
  // glyph for. Lives here so measure() and drawText() cannot disagree about the
  // width of a run containing one.
  int notdefAdvance() const;
  // 1 or 2. A 1bpp font reports coverage 0 or 3, so callers never branch on it.
  int bpp() const { return bpp_; }
  // Coverage of one glyph pixel, 0 (none) to 3 (full). Both depths are packed
  // MSB-first: 1bpp is one bit per pixel, 2bpp two bits, four pixels per byte,
  // pixel `col` occupying bits 6 - 2 * (col % 4).
  uint8_t coverage(const Glyph& g, int col, int row) const;

 private:
  std::unordered_map<char32_t, Glyph> glyphs_;
  std::unordered_map<uint64_t, int32_t> kerns_;
  int ascent_ = 0, descent_ = 0, lineGap_ = 0;
  int ppem_ = 0, weight_ = 0;  // 0 = undeclared (a v1 asset)
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
