#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "reader/filesystem.h"
#include "reader/names.h"

namespace reader {

// ONE CHAPTER'S EXTRACTS: the short piece of sentence shown beside each mention.
//
// --- WHY THE TEXT IS STORED AT ALL ---------------------------------------------
//
// The first design stored none: a name opened the peek directly and the peek
// rendered the sentence from the book. A Mentions list has to show the reader which
// sighting to open, and the alternatives to storing the text are both worse --
// decoding on demand means up to eight chapter inflates to draw one screen, and
// showing only a chapter label means choosing blind.
//
// --- WHY IT IS 64 BYTES AND A WINDOW -------------------------------------------
//
// Measured over a real novel at eight extracts a name: full sentences are 437 KB of
// card and 52.8 KB in one `writeAll` for the worst chapter, against a 45,840-byte
// floor. 64 bytes is 205.6 KB and 17.8 KB.
//
// THOSE ARE THE PROBE'S SIMULATION. This code, run end to end over the same book --
// scan, merge, capture, read back -- produces **3,794 extracts and 241.2 KB in 84
// parts, the largest 8,151 bytes**, with 722 index entries in an 18,302-byte index
// and 1.88 chapters per name. It captures MORE than the simulation predicted, for a
// reason worth knowing: the simulation recorded only mid-sentence occurrences, and
// the capture keeps suppressed ones too, because a name's first appearance in a
// chapter can legitimately open a sentence. The extract list is "the first eight
// SIGHTINGS", not "the first eight that counted towards the ranking".
//
// AND IT IS CENTRED ON THE NAME, NOT TAKEN FROM THE SENTENCE START. The mean
// sentence in a real novel is 125 bytes, so 64 bytes off the front frequently stops
// before the name appears -- a row that does not contain the word it is about.
//
// --- WHY THE PARTS ARE NUMBERED ------------------------------------------------
//
// There is no streaming write: `writeAll` takes a whole buffer and truncates. The
// capture walk holds the 37,056-byte inflate scratch throughout, and the worst
// chapter's extracts are 17.8 KB, which together is over the floor. So the buffer is
// bounded and flushed to `<spine>-<n>` as it fills -- repeated `writeAll` to
// numbered paths is the only streaming write this filesystem has.
//
// It costs nothing on read: entries come out in BLOCK order rather than name order,
// so a lookup scans a chapter's parts either way.
constexpr size_t kExtractBytes = 64;

// The buffer a part is flushed at. Small enough to sit beside the inflate scratch
// with room, which is the number this whole shape exists for.
constexpr size_t kExtractPartBytes = 8 * 1024;

// A 64-byte window of `sentence` centred on the run at `offset`, snapped to UTF-8
// boundaries and trimmed to whole words at both ends. Returned rather than written,
// because the wrap is a value the tests can read.
std::string extractWindow(std::string_view sentence, size_t offset, size_t runBytes,
                          size_t budget = kExtractBytes);

// Writes one chapter's extracts as numbered parts. WRITE-ONCE: a part is never
// rewritten, which is what lets the buffer stay bounded.
class ExtractPartWriter {
 public:
  ExtractPartWriter(FileSystem& fs, std::string dir, int spine,
                    size_t partBytes = kExtractPartBytes);

  void add(std::string_view run, int block, std::string_view extract);
  // Flush the tail. A chapter with no extracts writes no part at all, so an absent
  // part means "nothing here" rather than "not written yet" -- the index's own bit
  // is what distinguishes those, and it is set after this returns.
  bool finish();

  int parts() const { return parts_; }

 private:
  bool flush();

  FileSystem& fs_;
  std::string dir_;
  int spine_;
  size_t partBytes_;
  std::string buf_;
  int parts_ = 0;
  bool failed_ = false;
};

// One stored mention, as a part holds it.
struct StoredExtract {
  std::string run;
  int block = 0;
  std::string text;
};

// Read back every extract a chapter's parts hold for one run, oldest first.
bool readExtracts(FileSystem& fs, const std::string& dir, int spine, std::string_view run,
                  std::vector<StoredExtract>& out);

// THE CAPTURE PASS. A `NameScanner::RunSink` that keeps the first `quota` extracts
// for each of `wanted`, which must be sorted. Feed it the chapter a second time.
//
// It sees suppressed occurrences too and keeps them: a name's first appearance in a
// chapter can legitimately open a sentence, and the extract list is "the first eight
// sightings", not "the first eight that counted".
class ExtractCapture : public NameScanner::RunSink {
 public:
  ExtractCapture(std::vector<std::string> wanted, std::vector<int> quota,
                 ExtractPartWriter& out);

  void onRun(std::string_view run, int blockInChapter, std::string_view sentence, size_t offset,
             bool midSentence) override;

  // How many were kept per wanted run, in the same order -- which is what the index
  // merge records as this chapter's contribution.
  const std::vector<int>& kept() const { return kept_; }

 private:
  std::vector<std::string> wanted_;
  std::vector<int> quota_;
  std::vector<int> kept_;
  ExtractPartWriter& out_;
};

}  // namespace reader
