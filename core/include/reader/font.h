#pragma once
#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace reader {

struct Glyph {
  int16_t advance, bitmapW, bitmapH, xOff, yOff;  // yOff: baseline to bitmap top, +up
  const uint8_t* bitmap;                          // 1bpp MSB-first, 1 = ink
  int rowBytes() const { return (bitmapW + 7) / 8; }
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
  int measure(std::string_view utf8) const;

 private:
  std::unordered_map<char32_t, Glyph> glyphs_;
  std::unordered_map<uint64_t, int32_t> kerns_;
  int ascent_ = 0, descent_ = 0, lineGap_ = 0;
};

// Decodes one UTF-8 code point starting at s[i]; advances i. Invalid -> U+FFFD.
char32_t utf8Next(std::string_view s, size_t& i);

}  // namespace reader
