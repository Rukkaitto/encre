#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "reader/filesystem.h"
#include "reader/names.h"

namespace reader {

// THE CARD'S SIDE OF THE NAMES FEATURE: what one book's scan accumulates.
//
// `/.reader/names/<hash>/index` holds one line per admitted run, sorted by run name,
// and a header. The per-chapter extracts live beside it in write-once numbered parts
// and are not this file's business.
//
// --- WHY IT IS NOT JSON --------------------------------------------------------
//
// `core/include/reader/json.h` is ONE FLAT OBJECT with no nesting and no arrays,
// bounded at 64 pairs, and this is a list of ~722 things each carrying a list of its
// own. Line-oriented instead, and SORTED BY RUN NAME, which is what makes the
// per-chapter merge a linear two-way merge rather than a sort.
//
// THAT SORT ORDER IS FOR THE MERGE, NOT FOR THE SCREEN. The file is ordered by RUN
// name; the list a reader sees is ordered by GROUP display name, which only exists
// after grouping, and grouping happens when the screen opens. A screen that assumed
// this order would be right until the first alias merged.
//
// --- WHY THE MERGE STREAMS -----------------------------------------------------
//
// The index is ~19.6 KB on a real novel. Holding the old copy, the new copy and the
// chapter's table together is ~48 KB against a measured 45,840-byte floor. The file
// is sorted and `FileHandle` is seekable and read-only (`filesystem.h`), so the old
// copy never goes resident: read a line, merge, append to the output. Peak is about
// 28 KB, and no window has to be created -- which matters, because the
// chapter-crossing window the first design put this in does not exist.
struct NameIndexHeader {
  // A FORMAT CHANGE DISCARDS RATHER THAN MISREADS. Same hard gate `kPositionVersion`
  // is: a record from a future version is refused whole, not parsed hopefully.
  static constexpr int kVersion = 1;

  std::string bookPath;
  // The EPUB's size, which is the cheapest identity a FileSystem with no timestamps
  // and no hashes can offer -- `DirEntry` already carries it, and the reading
  // position already uses it for exactly this. A mismatch discards the store, because
  // an index for a different book is worse than none.
  uint32_t bookBytes = 0;
  // THE THRESHOLDS IN FORCE WHEN THE STORE WAS BUILT. Changing either invalidates the
  // index rather than mixing two populations into one set of counts: a store built at
  // two mid-sentence mentions and extended at three would hold names admitted under a
  // rule that no longer applies, with nothing saying which.
  int admitMidSentence = NameScanner::kAdmitMidSentence;
  int extractCap = 8;
  // WHICH SPINE ENTRIES HAVE BEEN SCANNED, one bit each. THE FIELD WHOSE ABSENCE
  // WOULD BE THE WORST BUG IN THE FEATURE: without it, re-reading a chapter scans it
  // twice and double-counts, inflating every mention and admitting runs that had been
  // correctly rejected. Re-reading is normal, so this is not an edge case.
  std::vector<uint8_t> scanned;

  bool isScanned(int spine) const;
  void markScanned(int spine);
  int scannedCount() const;
  // The highest chapter such that every chapter below it has been scanned. What the
  // screen would state as coverage if it stated any -- and the reason it does not is
  // that backfill makes a gap temporary. Kept because a caller that wants "is this
  // book's index still filling" wants exactly this, not the popcount.
  int contiguousPrefix() const;
};

// One run as the card holds it.
struct NameIndexEntry {
  std::string text;
  int midSentence = 0;
  int chapterOpening = 0;
  int total = 0;
  // WHERE THIS RUN'S EXTRACTS ARE, oldest first: the spine entry and how many
  // extracts that chapter's parts hold for it. At most `extractCap` entries, because
  // a run cannot have extracts in more chapters than it has extracts. Measured on a
  // real novel: 1.92 chapters per name on average and 7 at worst, so reading one
  // name's mentions is about two file opens.
  struct At {
    int spine = 0;
    int count = 0;
  };
  std::vector<At> extracts;

  int extractCount() const;
};

// --- Serialisation -------------------------------------------------------------
//
// Exposed rather than private because the merge is the only writer and the tests are
// the only other reader, and a format whose round trip is not directly testable is a
// format that drifts.

// The header's own text, terminated by a blank line. Returns false only for a header
// that cannot be represented.
std::string serialiseHeader(const NameIndexHeader& h);

// Parse a header. Returns false for a version mismatch, a missing required field, or
// text that is not a header at all -- a store that cannot be read is discarded and
// rebuilt, which backfill then does without the reader noticing.
bool parseHeader(std::string_view text, NameIndexHeader& out, size_t* bodyOffset);

// One entry's line, without the newline.
std::string serialiseEntry(const NameIndexEntry& e);
bool parseEntry(std::string_view line, NameIndexEntry& out);

// --- The store -----------------------------------------------------------------

class NameStore {
 public:
  NameStore(FileSystem& fs, std::string bookPath, uint32_t bookBytes);

  // The directory this book's store lives in, `/.reader/names/<hash>`. FNV-1a over
  // the path, eight lowercase hex, which is the reading position's naming and for its
  // reason: a book path holds `/` by construction and real cards carry accented
  // 90-character titles.
  const std::string& dir() const { return dir_; }
  std::string indexPath() const;

  // Read the header alone, without the body. What a caller asks before deciding
  // whether a chapter needs scanning at all.
  bool loadHeader(NameIndexHeader& out) const;

  // MERGE ONE CHAPTER'S ADMITTED RUNS INTO THE INDEX, streaming the old copy off the
  // card. `runs` must be sorted by text, which `NameScanner` guarantees.
  //
  // A CHAPTER ALREADY MARKED SCANNED IS A NO-OP AND RETURNS TRUE. That is the
  // scanned-spine bitmap doing its whole job: re-reading is normal, and without the
  // refusal every re-read would double-count.
  //
  // THE HEADER AND ITS BIT ARE WRITTEN LAST, in the same `writeAll` as the body, so
  // an interruption leaves a store that never knew about the chapter rather than one
  // promising extracts it does not have.
  bool mergeChapter(int spine, const std::vector<const NameScanner::Run*>& runs,
                    const std::vector<int>* extractCounts = nullptr);

  // Every entry, for the screen's grouping pass. THE ONE PLACE THE WHOLE INDEX GOES
  // RESIDENT, which is why the screen releases the Reader's chapter before it opens.
  bool loadAll(NameIndexHeader& header, std::vector<NameIndexEntry>& out) const;

  // Remove the whole store. Called when its book is deleted, as article state is.
  bool remove();

 private:
  FileSystem& fs_;
  std::string bookPath_;
  uint32_t bookBytes_ = 0;
  std::string dir_;
};

// FNV-1a over a book path, eight lowercase hex. Exposed for the tests and for the
// shell's own cleanup, which has to name a directory it is about to delete.
std::string nameStoreDirFor(std::string_view bookPath);

}  // namespace reader
