#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "reader/filesystem.h"

namespace reader {

// A zip reader for EPUBs, over a seekable FileHandle -- which is exactly why 3A
// made the handle seekable: a zip's central directory is at the END of the file,
// so a forward-only stream cannot read one at all.
//
// NOT A GENERAL ZIP LIBRARY, and the list of refusals is the design rather than a
// backlog. Stored and deflated entries only; no encryption, no zip64, no spanning,
// no data descriptors. Everything else is a `false` with a reason, because the
// alternative on this device is worse than not opening the book: under
// `-fno-exceptions` a size believed from a corrupt field is an `abort()` with no
// diagnostic, and a book that fails to open is a screen the user can act on.
//
// ONLY THE CENTRAL DIRECTORY IS TRUSTED. A local header restates an entry's sizes
// and the two can disagree -- a fact of the format, not a corruption -- and the
// central directory is the authority. So the local header is read only for the
// offset of the data that follows it.
class Zip {
 public:
  struct Entry {
    std::string name;
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
    uint32_t localHeaderOffset = 0;
    bool deflated = false;  // false = stored; nothing else is accepted
  };

  // How many entries an archive may claim before it is refused. An EPUB is a
  // handful of files per chapter; a claim of thousands is either a mistake or an
  // attack, and the count is what sizes a vector.
  static constexpr size_t kMaxEntries = 512;

  // The largest entry this will read into memory. A chapter is tens of KB; the
  // biggest thing an EPUB legitimately holds is an image, and images are 3C's.
  // The number is bounded by the heap, not by the format: 3A measured a
  // ~155 KB floor with a 56 KB rasteriser spike on top of it.
  static constexpr uint32_t kMaxEntryBytes = 512u * 1024u;

  // The EOCD is at the end, but a legal comment can push it up to 64 KB earlier,
  // so the backwards scan is bounded by that rather than by the file.
  static constexpr uint32_t kMaxEocdSearch = 64u * 1024u + 22u;

  // False on anything not understood, and `reason()` says which. The handle is
  // BORROWED and must outlive this object: a Zip holds no file, only the offsets
  // it read, so a reader can keep a book open across pages without holding the
  // directory twice.
  bool open(FileHandle& file);

  // Every entry, in central-directory order.
  const std::vector<Entry>& entries() const { return entries_; }

  // The entry with this exact name, or null. Names are compared byte for byte:
  // an EPUB's OPF names its parts exactly, and case-folding would be a guess
  // about a filesystem this code never sees.
  const Entry* find(std::string_view name) const;

  // Reads and, if needed, inflates one entry. `out` is REPLACED and sized from
  // the entry's own uncompressedSize -- which is a claim, and is why the cap
  // above exists and is checked before the allocation.
  bool read(FileHandle& file, const Entry& entry, std::string& out) const;

  // Why the last open() or read() failed. A sentence, for a log line.
  const char* reason() const { return reason_; }

 private:
  bool fail(const char* why);

  std::vector<Entry> entries_;
  const char* reason_ = "";
};

}  // namespace reader
