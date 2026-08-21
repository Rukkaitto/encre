#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

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
//
// --- Why there is no index --------------------------------------------------
//
// This class used to keep two std::unordered_maps, one codepoint -> Glyph and
// one packed kern pair -> adjustment, built during load(). On the device that
// cost **99,008 bytes of heap across the eleven faces of the chrome ramp**
// (measured: free heap 229,980 before the ramp loads, 130,972 after) to index
// records that were already sitting in memory-mapped flash. A 200-glyph face
// paid ~8.6 KB for 200 nodes of 20 bytes plus a bucket array, and it paid it
// eleven times. Phase 3's pagination wants that heap more than a hash lookup is
// worth.
//
// So the tables are searched where they lie. tools/fontc.py emits both in
// ascending key order (its CODEPOINTS list ascends, and the kern pairs are
// generated as a nested walk over that list, so they ascend by (left, right)
// which is the packed key), verified against the bytes of all twelve committed
// assets and pinned by test_font_records.cpp. A search is therefore a binary
// search: ~8 comparisons on 200 records.
//
// **A table that does NOT ascend is still searched correctly**, by a linear
// scan, decided once at load() and remembered. That is not defensive
// decoration: fontc.py's codepoint list is hand-maintained and its comment
// invites appending to the end, so the next codepoint added out of order would
// otherwise turn some glyph lookups into silent misses -- text rendering as
// notdef boxes for reasons no test names. load() does not *reject* an unsorted
// table, because it is the trust boundary for what is safe to draw with and an
// unsorted table is safe to draw with; it is merely slower.
class Font {
 public:
  bool load(const uint8_t* data, size_t size);

  // The record for `cp`, or nullopt if this face has no glyph for it.
  //
  // BY VALUE, and that is what removing the maps costs: there is no longer a
  // stored Glyph to hand out a pointer to. The 18-byte on-disk record and a
  // Glyph are different shapes -- a Glyph carries a resolved bitmap pointer and
  // the row stride, neither of which is in the record -- so the blob cannot be
  // reinterpreted as an array of Glyph. What matters is that the BITMAP is
  // still borrowed, not copied: `bitmap` points straight into the caller's blob
  // exactly as it did before, and that is the only part with a size worth
  // caring about.
  std::optional<Glyph> glyph(char32_t cp) const;
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
  // The record within the glyph table for `cp`, or nullptr. Separate from
  // glyph() so measure() and the record-level tests can ask the same question
  // the same way.
  const uint8_t* findGlyphRecord(char32_t cp) const;

  // Borrowed: all three point into the blob load() was handed, and are null
  // until a load succeeds. Every record they cover was bounds-checked by
  // load(), which is what lets glyph() decode one without re-validating it.
  const uint8_t* glyphRecords_ = nullptr;  // glyphCount_ records of 18 bytes
  const uint8_t* kernRecords_ = nullptr;   // kernCount_ records of 12 bytes
  const uint8_t* bitmaps_ = nullptr;       // the packed-row blob the records index
  uint16_t glyphCount_ = 0;
  uint16_t kernCount_ = 0;
  // Whether each table's keys ascend, so a search knows whether it may bisect.
  // True for an empty table, which is vacuously sorted and never searched.
  bool glyphsAscend_ = true;
  bool kernsAscend_ = true;
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
