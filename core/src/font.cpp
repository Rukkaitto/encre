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

// The two on-disk record sizes, spelled once. They are the layout
// tools/fontc.py writes and test/unit/rfnt_builder.h mirrors:
//   glyph 18B <IhhhhhI>  codepoint, advance, w, h, xOff, yOff, bitmapOffset
//   kern  12B <IIi>      left, right, adjust
constexpr size_t kGlyphRecordBytes = 18;
constexpr size_t kKernRecordBytes = 12;

// The key each table is ordered by. The kern key packs the pair the same way
// Font::kerning does, so "the table ascends by key" and "the table ascends by
// (left, right)" are the same statement.
uint32_t glyphKey(const uint8_t* rec) { return rd<uint32_t>(rec); }
uint64_t kernKey(const uint8_t* rec) {
  return (static_cast<uint64_t>(rd<uint32_t>(rec)) << 32) | rd<uint32_t>(rec + 4);
}

// Bisect `count` fixed-size records for `key`, or scan them when the table is
// not in order. One template so the glyph and kern tables cannot drift into two
// slightly different searches -- the kern table is the one with no coverage in
// any shipped asset (every committed .rfnt has zero kern pairs), so it is
// exactly the one that would rot.
template <typename Key, size_t RecordBytes, Key (*KeyOf)(const uint8_t*)>
const uint8_t* findRecord(const uint8_t* records, size_t count, bool ascends, Key key) {
  if (records == nullptr) return nullptr;
  if (!ascends) {
    for (size_t i = 0; i < count; ++i) {
      const uint8_t* rec = records + i * RecordBytes;
      if (KeyOf(rec) == key) return rec;
    }
    return nullptr;
  }
  size_t lo = 0, hi = count;  // half-open; the answer is in [lo, hi)
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    const uint8_t* rec = records + mid * RecordBytes;
    const Key k = KeyOf(rec);
    if (k == key) return rec;
    if (k < key)
      lo = mid + 1;
    else
      hi = mid;
  }
  return nullptr;
}
}  // namespace

// No bitmap font at the sizes this reader uses has a glyph anywhere near this
// large; a bigger dimension means the record is corrupt, not merely unusual.
static constexpr int kMaxGlyphDim = 512;

bool Font::load(const uint8_t* d, size_t size) {
  // load() is the trust boundary: callers draw with the font as soon as this
  // returns true, so every offset it hands out must already be in range.
  // Reset first so a second call cannot inherit a previous font's tables and a
  // rejected font leaves the object empty rather than half-populated. The table
  // pointers are the whole of the state now, and they are published at the very
  // END of this function -- so every early return below leaves the object empty
  // without having to remember to undo anything but the depth.
  glyphRecords_ = kernRecords_ = bitmaps_ = nullptr;
  glyphCount_ = kernCount_ = 0;
  glyphsAscend_ = kernsAscend_ = true;
  ascent_ = descent_ = lineGap_ = 0;
  ppem_ = weight_ = 0;
  bpp_ = 1;

  if (size < 16 || std::memcmp(d, "RFNT", 4) != 0) return false;
  // The bit depth rides in the high byte of the version word: version 1 alone
  // is v1/1bpp, 1 | (2 << 8) is v2/2bpp. A v1 file therefore stays byte-valid.
  const uint16_t versionWord = rd<uint16_t>(d + 4);
  const uint8_t version = versionWord & 0xFF;
  const uint8_t depth = (versionWord >> 8) ? static_cast<uint8_t>(versionWord >> 8) : 1;
  // Format 2 appends two declarations to the header -- the nominal pixel size
  // FreeType resolved the request to, and the weight the variation axis was
  // pinned to (see Font::ppem/weight). Format 1 is still accepted and reports
  // both as 0: the container grew, the glyph and kern tables did not, so an old
  // asset loads and renders identically.
  const size_t headerAt = version >= 2 ? 20u : 16u;
  if (version < 1 || version > 2 || (depth != 1 && depth != 2)) return false;
  if (size < headerAt) return false;
  bpp_ = depth;
  const uint16_t glyphCount = rd<uint16_t>(d + 6);
  const uint16_t kernCount = rd<uint16_t>(d + 14);

  const size_t glyphsAt = headerAt;
  const size_t kernsAt = glyphsAt + glyphCount * kGlyphRecordBytes;
  const size_t blobAt = kernsAt + kernCount * kKernRecordBytes;
  if (blobAt > size) return false;

  // The validation walk. Nothing is stored per glyph any more, but every record
  // must still be checked HERE rather than at lookup time: glyph() decodes a
  // record and hands back a bitmap pointer with no further checks, so this loop
  // is what makes that pointer safe. It also decides, in the same pass, whether
  // the table ascends -- the question the search asks (see font.h).
  bool glyphsAscend = true;
  for (uint16_t i = 0; i < glyphCount; ++i) {
    const uint8_t* g = d + glyphsAt + i * kGlyphRecordBytes;
    const uint32_t cp = glyphKey(g);
    if (i > 0 && cp <= glyphKey(g - kGlyphRecordBytes)) glyphsAscend = false;
    const int16_t bitmapW = rd<int16_t>(g + 6);
    const int16_t bitmapH = rd<int16_t>(g + 8);
    const uint32_t bitmapOffset = rd<uint32_t>(g + 14);
    if (bitmapW < 0 || bitmapH < 0 || bitmapW > kMaxGlyphDim || bitmapH > kMaxGlyphDim) {
      bpp_ = 1;  // metrics and tables are still zero: they are only set on success
      return false;
    }
    const size_t rowBytes = static_cast<size_t>((bitmapW * bpp_ + 7) / 8);
    // The whole bitmap, not just its first byte, must lie inside the blob.
    const size_t extent =
        blobAt + bitmapOffset + rowBytes * static_cast<size_t>(bitmapH);
    if (extent < blobAt || extent > size) {  // extent < blobAt catches wraparound
      bpp_ = 1;
      return false;
    }
  }
  bool kernsAscend = true;
  for (uint16_t i = 1; i < kernCount; ++i) {
    const uint8_t* k = d + kernsAt + i * kKernRecordBytes;
    if (kernKey(k) <= kernKey(k - kKernRecordBytes)) kernsAscend = false;
  }
  glyphRecords_ = d + glyphsAt;
  kernRecords_ = d + kernsAt;
  bitmaps_ = d + blobAt;
  glyphCount_ = glyphCount;
  kernCount_ = kernCount;
  glyphsAscend_ = glyphsAscend;
  kernsAscend_ = kernsAscend;
  ascent_ = rd<int16_t>(d + 8);
  descent_ = rd<int16_t>(d + 10);
  lineGap_ = rd<int16_t>(d + 12);
  if (version >= 2) {
    ppem_ = rd<uint16_t>(d + 16);
    weight_ = rd<uint16_t>(d + 18);
  }
  return true;
}

const uint8_t* Font::findGlyphRecord(char32_t cp) const {
  return findRecord<uint32_t, kGlyphRecordBytes, glyphKey>(glyphRecords_, glyphCount_,
                                                          glyphsAscend_,
                                                          static_cast<uint32_t>(cp));
}

std::optional<Glyph> Font::glyph(char32_t cp) const {
  const uint8_t* g = findGlyphRecord(cp);
  if (g == nullptr) return std::nullopt;
  Glyph gl;
  gl.advance = rd<int16_t>(g + 4);
  gl.bitmapW = rd<int16_t>(g + 6);
  gl.bitmapH = rd<int16_t>(g + 8);
  gl.xOff = rd<int16_t>(g + 10);
  gl.yOff = rd<int16_t>(g + 12);
  gl.stride = static_cast<int16_t>((gl.bitmapW * bpp_ + 7) / 8);
  // Borrowed, not copied, and load() has already proved every row of it lies
  // inside the blob -- which is what lets this line skip the check.
  gl.bitmap = bitmaps_ + rd<uint32_t>(g + 14);
  return gl;
}

uint8_t Font::coverage(const Glyph& g, int col, int row) const {
  const uint8_t* r = g.bitmap + static_cast<size_t>(row) * g.stride;
  if (bpp_ == 1) return ((r[col / 8] >> (7 - col % 8)) & 1) ? 3 : 0;
  // 2bpp, MSB-first: two bits per pixel, four pixels per byte.
  const int shift = 6 - 2 * (col % 4);
  return static_cast<uint8_t>((r[col / 4] >> shift) & 0x3);
}

int Font::kerning(char32_t l, char32_t r) const {
  const uint64_t key = (static_cast<uint64_t>(l) << 32) | r;
  const uint8_t* k = findRecord<uint64_t, kKernRecordBytes, kernKey>(kernRecords_, kernCount_,
                                                                    kernsAscend_, key);
  return k == nullptr ? 0 : rd<int32_t>(k + 8);
}

// The hollow box drawText paints in place of a missing glyph: h = ascent * 2/3,
// w = h / 2 + 1, then two pixels of side bearing. Spelled once, read by both.
int Font::notdefAdvance() const { return (ascent_ * 2 / 3) / 2 + 1 + 2; }

int Font::measure(std::string_view utf8, Tracking tracking) const {
  // Accumulated in 1/64 px and rounded once, exactly as drawText does it, so
  // measure() == drawText()'s returned advance for every string and every
  // tracking value -- including fractional ones, where a per-glyph rounding
  // here and there would part company after the second character. Right
  // alignment is `edge - measure(s)`, so a disagreement of even one pixel is a
  // run that does not end on the margin it was aligned to.
  int penF = 0;
  char32_t prev = 0;
  for (size_t i = 0; i < utf8.size();) {
    const char32_t cp = utf8Next(utf8, i);
    const std::optional<Glyph> g = glyph(cp);
    if (!g) {
      // Not `continue`: drawText draws a box here and advances past it, so
      // skipping it made a run with one unmapped codepoint measure short by the
      // box's width and kern across the hole as if it were not there.
      penF += pxToF26(notdefAdvance()) + tracking.f26();
      prev = 0;
      continue;
    }
    if (prev) penF += pxToF26(kerning(prev, cp));
    penF += pxToF26(g->advance) + tracking.f26();
    prev = cp;
  }
  return f26ToPx(penF);
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
