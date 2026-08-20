#pragma once
#include <cstdint>
#include <string_view>
#include <unordered_map>

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
  // `tracking` adds that many pixels after every glyph, matching drawText.
  int measure(std::string_view utf8, int tracking = 0) const;
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
