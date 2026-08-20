#include "reader/font.h"

#include <cstring>

namespace reader {

namespace {
template <typename T>
T rd(const uint8_t* p) {  // little-endian read
  T v;
  std::memcpy(&v, p, sizeof(T));
  return v;
}
}  // namespace

// No bitmap font at the sizes this reader uses has a glyph anywhere near this
// large; a bigger dimension means the record is corrupt, not merely unusual.
static constexpr int kMaxGlyphDim = 512;

bool Font::load(const uint8_t* d, size_t size) {
  // load() is the trust boundary: callers draw with the font as soon as this
  // returns true, so every offset it hands out must already be in range.
  // Reset first so a second call cannot inherit a previous font's tables and a
  // rejected font leaves the object empty rather than half-populated.
  glyphs_.clear();
  kerns_.clear();
  ascent_ = descent_ = lineGap_ = 0;

  if (size < 16 || std::memcmp(d, "RFNT", 4) != 0 || rd<uint16_t>(d + 4) != 1) return false;
  const uint16_t glyphCount = rd<uint16_t>(d + 6);
  const uint16_t kernCount = rd<uint16_t>(d + 14);

  const size_t glyphsAt = 16;
  const size_t kernsAt = glyphsAt + glyphCount * 18u;
  const size_t blobAt = kernsAt + kernCount * 12u;
  if (blobAt > size) return false;

  for (uint16_t i = 0; i < glyphCount; ++i) {
    const uint8_t* g = d + glyphsAt + i * 18u;
    Glyph gl;
    const uint32_t cp = rd<uint32_t>(g);
    gl.advance = rd<int16_t>(g + 4);
    gl.bitmapW = rd<int16_t>(g + 6);
    gl.bitmapH = rd<int16_t>(g + 8);
    gl.xOff = rd<int16_t>(g + 10);
    gl.yOff = rd<int16_t>(g + 12);
    const uint32_t bitmapOffset = rd<uint32_t>(g + 14);
    if (gl.bitmapW < 0 || gl.bitmapH < 0 || gl.bitmapW > kMaxGlyphDim ||
        gl.bitmapH > kMaxGlyphDim) {
      glyphs_.clear();  // metrics are still zero: they are only set on success
      kerns_.clear();
      return false;
    }
    // The whole bitmap, not just its first byte, must lie inside the blob.
    const size_t extent = blobAt + bitmapOffset +
                          static_cast<size_t>(gl.rowBytes()) * static_cast<size_t>(gl.bitmapH);
    if (extent < blobAt || extent > size) {  // extent < blobAt catches wraparound
      glyphs_.clear();
      kerns_.clear();
      return false;
    }
    gl.bitmap = d + blobAt + bitmapOffset;
    glyphs_.emplace(static_cast<char32_t>(cp), gl);
  }
  for (uint16_t i = 0; i < kernCount; ++i) {
    const uint8_t* k = d + kernsAt + i * 12u;
    const uint64_t key = (static_cast<uint64_t>(rd<uint32_t>(k)) << 32) | rd<uint32_t>(k + 4);
    kerns_.emplace(key, rd<int32_t>(k + 8));
  }
  ascent_ = rd<int16_t>(d + 8);
  descent_ = rd<int16_t>(d + 10);
  lineGap_ = rd<int16_t>(d + 12);
  return true;
}

const Glyph* Font::glyph(char32_t cp) const {
  auto it = glyphs_.find(cp);
  return it == glyphs_.end() ? nullptr : &it->second;
}

int Font::kerning(char32_t l, char32_t r) const {
  auto it = kerns_.find((static_cast<uint64_t>(l) << 32) | r);
  return it == kerns_.end() ? 0 : it->second;
}

int Font::measure(std::string_view utf8, int tracking) const {
  int w = 0;
  char32_t prev = 0;
  for (size_t i = 0; i < utf8.size();) {
    const char32_t cp = utf8Next(utf8, i);
    const Glyph* g = glyph(cp);
    if (!g) continue;
    if (prev) w += kerning(prev, cp);
    w += g->advance + tracking;
    prev = cp;
  }
  return w;
}

char32_t utf8Next(std::string_view s, size_t& i) {
  const auto b0 = static_cast<uint8_t>(s[i]);
  if (b0 < 0x80) { ++i; return b0; }

  // Sequence length, the lead byte's payload bits, and the smallest code point
  // that legally uses this length (anything below it is an overlong encoding).
  size_t extra;
  char32_t cp, lowest;
  if ((b0 & 0xE0) == 0xC0) {
    extra = 1; cp = b0 & 0x1Fu; lowest = 0x80;
  } else if ((b0 & 0xF0) == 0xE0) {
    extra = 2; cp = b0 & 0x0Fu; lowest = 0x800;
  } else if ((b0 & 0xF8) == 0xF0) {
    extra = 3; cp = b0 & 0x07u; lowest = 0x10000;
  } else {
    ++i;  // bare continuation byte, or a 5+ byte lead that UTF-8 has no room for
    return 0xFFFD;
  }

  for (size_t k = 1; k <= extra; ++k) {
    if (i + k >= s.size()) {  // truncated at end of input: nothing left to skip to
      i = s.size();
      return 0xFFFD;
    }
    const auto b = static_cast<uint8_t>(s[i + k]);
    if ((b & 0xC0) != 0x80) {
      // Not a continuation byte. Advance exactly one byte past the lead so the
      // character that follows the broken sequence is decoded, not swallowed.
      ++i;
      return 0xFFFD;
    }
    cp = (cp << 6) | (b & 0x3Fu);
  }
  // Overlong, surrogate half, or beyond the Unicode range: not a code point.
  if (cp < lowest || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
    ++i;
    return 0xFFFD;
  }
  i += extra + 1;
  return cp;
}

}  // namespace reader
