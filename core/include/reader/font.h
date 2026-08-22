#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "reader/glyphsource.h"
#include "reader/tracking.h"

namespace reader {

// Zero-copy view over an .rfnt blob; the blob must outlive the Font. This is
// CHROME's face -- the pre-rendered type ramp, where the boards are pixel-exact
// and the seventeen goldens live. Body text uses ScalableFont instead, and both
// satisfy GlyphSource so there is one text path (see glyphsource.h).
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
class Font : public GlyphSource {
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
  //
  // Nothing here rasterises, so glyph() and advance() cost the same on this
  // face -- the distinction between them is ScalableFont's (see glyphsource.h),
  // and it is on the interface rather than on that class alone precisely so a
  // caller cannot reach for the rasterising one by accident.
  std::optional<Glyph> glyph(char32_t cp) const override;
  std::optional<int> advance(char32_t cp) const override;
  int kerning(char32_t left, char32_t right) const override;

  // --- What this face IS, not merely how it measures ------------------------
  //
  // ppem() and weight() are GlyphSource's; what is worth recording is where an
  // .rfnt's come from and why it has them at all.
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

 private:
  // The record within the glyph table for `cp`, or nullptr. Separate from
  // glyph() so advance() and the record-level tests can ask the same question
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
};

}  // namespace reader
