#pragma once
// Builds synthetic .rfnt buffers in memory so Font::load can be exercised on
// inputs no real font file would contain (corrupt offsets, kern pairs,
// truncated tables). Mirrors the layout tools/fontc.py writes:
//   header 16B  <4sHHhhhH>  magic, version, glyphCount, ascent, descent,
//                           lineGap, kernCount
//   glyph  18B  <IhhhhhI>   codepoint, advance, bitmapW, bitmapH, xOff, yOff,
//                           bitmapOffset (relative to the blob)
//   kern   12B  <IIi>       left, right, adjust
//   blob             packed MSB-first rows, ceil(bitmapW * bpp / 8) per row
//
// `bpp` selects the depth: 1 writes the version word untouched (so a v1 buffer
// is byte-identical to what it was before v2 existed), 2 ORs the depth into the
// version word's high byte. The blob itself is supplied verbatim by the caller,
// so a test can hand-write the exact coverage bytes it wants to see read back.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace rfnt {

struct GlyphRec {
  uint32_t cp = 0;
  int16_t advance = 0, bitmapW = 0, bitmapH = 0, xOff = 0, yOff = 0;
  uint32_t bitmapOffset = 0;
};

struct KernRec {
  uint32_t left = 0, right = 0;
  int32_t adjust = 0;
};

class Builder {
 public:
  std::string magic = "RFNT";
  uint16_t version = 1;
  int bpp = 1;  // 1 or 2; rides in the high byte of the version word
  int16_t ascent = 12, descent = -4, lineGap = 2;
  std::vector<GlyphRec> glyphs;
  std::vector<KernRec> kerns;
  std::vector<uint8_t> blob;
  // Overrides for the declared counts, so a header can lie about its tables.
  int glyphCountOverride = -1, kernCountOverride = -1;

  std::vector<uint8_t> build() const {
    std::vector<uint8_t> out;
    for (size_t i = 0; i < 4; ++i) out.push_back(i < magic.size() ? static_cast<uint8_t>(magic[i]) : 0);
    put<uint16_t>(out, versionWord());
    put<uint16_t>(out, static_cast<uint16_t>(glyphCountOverride >= 0 ? glyphCountOverride : glyphs.size()));
    put<int16_t>(out, ascent);
    put<int16_t>(out, descent);
    put<int16_t>(out, lineGap);
    put<uint16_t>(out, static_cast<uint16_t>(kernCountOverride >= 0 ? kernCountOverride : kerns.size()));
    for (const GlyphRec& g : glyphs) {
      put<uint32_t>(out, g.cp);
      put<int16_t>(out, g.advance);
      put<int16_t>(out, g.bitmapW);
      put<int16_t>(out, g.bitmapH);
      put<int16_t>(out, g.xOff);
      put<int16_t>(out, g.yOff);
      put<uint32_t>(out, g.bitmapOffset);
    }
    for (const KernRec& k : kerns) {
      put<uint32_t>(out, k.left);
      put<uint32_t>(out, k.right);
      put<int32_t>(out, k.adjust);
    }
    out.insert(out.end(), blob.begin(), blob.end());
    return out;
  }

  // Bytes one row of `w` pixels occupies at this depth, for sizing a blob.
  int rowBytes(int w) const { return (w * bpp + 7) / 8; }

 private:
  uint16_t versionWord() const {
    // bpp 1 is implicit, so a 1bpp header keeps its bare version word.
    return bpp <= 1 ? version : static_cast<uint16_t>(version | (bpp << 8));
  }

  template <typename T>
  static void put(std::vector<uint8_t>& out, T v) {
    uint8_t tmp[sizeof(T)];
    std::memcpy(tmp, &v, sizeof(T));
    out.insert(out.end(), tmp, tmp + sizeof(T));
  }
};

}  // namespace rfnt
