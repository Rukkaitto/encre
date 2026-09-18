#pragma once
#include <memory>
#include <string>

#include "reader/document.h"
#include "reader/filesystem.h"
#include "reader/inflate_stream.h"
#include "reader/xml.h"  // BufferSource
#include "reader/zip.h"

namespace reader {

// WHERE A CHAPTER'S BYTES ARE, which is all a re-read needs.
//
// Three numbers and a path, deliberately: reaching an earlier page means decoding
// the chapter from its start again -- a DEFLATE stream cannot be seeked without its
// 32 KB window, and checkpointing one costs 32 KB a checkpoint -- and doing that
// from a location rather than from an open archive means a backward page turn pays
// an inflate instead of an inflate plus a central-directory scan.
struct ChapterLocation {
  std::string bookPath;          // the EPUB, absolute on the card
  uint32_t localHeaderOffset = 0;  // the entry's local header; the data follows it
  uint32_t compressedSize = 0;
  uint32_t uncompressedSize = 0;   // what it inflates to; see ChapterSpan
  bool deflated = true;          // false for a stored entry: the bytes are the text
};

// AN OPEN CHAPTER, POSITIONED. Hands over blocks as they are decoded and forgets
// them, so its memory does not depend on the chapter's length.
//
// It owns the whole chain -- the file handle, the compressed-byte source, the
// inflater and its 32 KB window, the tokenizer, the block builder -- because each
// link holds a pointer to the one below it and the nesting has exactly one correct
// order. Measured on a real book, the whole thing peaks at ~37 KB for any chapter,
// against 546 KB for the largest chapter of the same book before this existed.
//
// IT KEEPS THE CARD HANDLE OPEN WHILE READING, and that is a change from the
// one-shot form, which read a chapter and closed. The trade: a forward page turn
// continues the stream and costs nothing, where reopening would cost a full decode
// every time. What it risks is a handle held across a card removal -- which fails
// the next read and refuses, rather than corrupting anything -- and across a sleep,
// which cannot happen: deep sleep is a chip reset, so the reader is gone.
class ChapterReader {
 public:
  ChapterReader() = default;
  ChapterReader(const ChapterReader&) = delete;
  ChapterReader& operator=(const ChapterReader&) = delete;

  // Opens the file and positions at the chapter's first block. False with a reason
  // for a missing file, a stream that will not start, or a heap that cannot hold
  // the window.
  bool begin(FileSystem& fs, const ChapterLocation& where);

  // A chapter ALREADY IN MEMORY, streamed through exactly the same layers minus the
  // inflate. For the demo content the simulator and the goldens render, which have
  // no card -- and for a test that wants a chapter without an archive around it.
  // The bytes are copied, so the caller need not keep them.
  bool beginBuffer(std::string_view xhtml);

  // The book's italic class names, handed on to every BlockReader this makes. Not
  // owned: the set belongs to the book and outlives every chapter read from it. See
  // reader/css.h for why a real book needs it at all.
  void setItalicClasses(const std::vector<std::string>* classes) { italicClasses_ = classes; }

  // Back to block 0, reusing every buffer -- no 32 KB reallocation. This is what a
  // backward page turn costs.
  bool rewind();

  // --- LET GO OF THE STREAM, KEEP WHAT IT WAS OPENED FROM --------------------
  //
  // FOR THE PEEK, and it is what makes the peek possible at all: a live chapter peaks
  // at 69,884 bytes with a 36,956-byte single allocation (the inflate window and its
  // tables) against a measured 45,840-byte heap floor, so TWO live chapters leave
  // single-digit kilobytes on a part where a failed allocation is abort() with no
  // diagnostic. The Reader beneath a peek therefore lets go while the panel is up and
  // takes its stream back when the panel closes.
  //
  // THE OBVIOUS VERSION OF THAT IS WRONG, AND IT IS WORTH KNOWING WHY. Four of the
  // five things released here are unique_ptrs -- file_, bufSrc_, inflated_, blocks_ --
  // so it reads as a design where letting go is resetting pointers and nothing new had
  // to be invented. `inflater_` is not one of them: it is a VALUE member, and the
  // 36,956 bytes this whole feature is about live behind ITS pointer, freed by its
  // destructor and by nothing else. Resetting only the unique_ptrs frees the
  // BlockReader, a ~40-byte InflateSource wrapper and a file handle, and keeps every
  // byte the peek needs. `Inflater::release()` exists for that and is the load-bearing
  // line below.
  //
  // `where_` IS KEPT, which is the whole difference between this and destroying the
  // object: begin() has to be callable again with the same location, and a release
  // that lost it would produce a reader that opens and yields nothing --
  // indistinguishable from a chapter that ended. Idempotent, because the shell
  // reacquires on two different paths and a double release must not be a crash.
  void release();

  // Whether a stream is established. False after release() and before the first
  // begin(). It exists as an OBSERVATION POINT rather than as a guard: nothing in the
  // shell branches on it, and the peek's release test is what needs it.
  bool held() const { return blocks_ != nullptr; }

  // Whether the inflate window is allocated -- the 36,956-byte block release() exists
  // to give back. DISTINCT FROM held(), and it has to be: held() reads blocks_, so a
  // release that dropped the block reader and kept the window would satisfy it and
  // free nothing, and from outside this class nothing else can tell those apart.
  // bytesRead() cannot: it gates on `inflateActive_`, which release() clears either
  // way. An observation point, like held(); nothing branches on it.
  bool inflateWindowHeld() const { return inflater_.ready(); }

  // The next block. False means the chapter ended (`ok()`) or was refused.
  bool next(Block& out);

  // How many bytes this chapter inflates to -- the buffer's own size for an
  // in-memory chapter. What a caller needs to decide whether counting its pages is
  // cheap enough to do eagerly.
  uint32_t sizeBytes() const;

  // The index of the block `next()` will return, which is what a page Cursor names.
  int position() const { return position_; }

  // HOW FAR INTO THE CHAPTER THE STREAM HAS DECODED, in inflated bytes. Zero means
  // "not knowable here", which is the stored-entry and in-memory cases -- the blocks
  // then come straight off a buffer with no inflater to ask.
  //
  // It exists because BOOK PROGRESS SHOULD NOT WAIT FOR A PAGE COUNT. The percentage
  // is a fraction of the book's bytes, and within the open chapter it used to
  // interpolate on page/pageTotal -- so with the count deferred (which is the normal
  // state of a long chapter for its first seconds, and longer while the reader keeps
  // pressing) it did not advance at all. This is the same quantity the percentage is
  // already made of, available with no count and no walk.
  // IT IS THE SOURCE'S `consumed()`, NOT THE INFLATER'S `produced()`, and that
  // distinction is the whole of #148: `produced()` is where the DECODER has reached,
  // which runs a whole 16 KB chunk in front of the page on the glass -- so a chapter
  // under a chunk long reported every byte of itself read before its first page was
  // laid out, and the percentage stood still for the length of the chapter. See
  // InflateSource::consumed.
  //
  // WHAT REMAINS IS BOUNDED AND SMALL: the tokenizer's own 512-byte buffer, plus the
  // tail of the block that filled the page (`kMaxBlockBytes`, 8 KB, and a paragraph
  // in practice). A page boundary sits INSIDE a block, so no cheaper answer exists
  // without a byte offset per laid-out line.
  uint32_t bytesRead() const { return inflateActive_ ? inflated_->consumed() : 0; }

  // HOW MANY TIMES A BLOCK HAS BEEN CUT AT `kMaxBlockBytes` on the current walk --
  // `BlockReader::blocksSplit()`, passed through, and an observation point in
  // `held()`'s sense rather than something to branch on.
  //
  // PER WALK, NOT PER CHAPTER, and the difference is load-bearing: a rewind calls
  // `BlockReader::restart()`, which zeroes the counter, so this answers "cuts since
  // the stream was last established" and a reader that has paged backward has reset
  // it. That is what makes it honest for a single forward walk -- which is what the
  // corpus probe and the page index both do -- and why nothing sums it across a
  // reading session.
  size_t blocksSplit() const { return blocks_ != nullptr ? blocks_->blocksSplit() : 0; }

  bool ok() const { return error_[0] == '\0'; }
  const char* error() const { return error_; }

 private:
  bool startStream();

  // The data offset, resolved by one 30-byte read of the local header. Cached,
  // because a rewind must not go back to the card for it.
  uint32_t dataOffset_ = 0;

  FileSystem* fs_ = nullptr;
  // Set when the chapter came from beginBuffer: there is no file, no entry and no
  // inflate, and a rewind just points the source back at the start.
  std::string buffer_;
  bool fromBuffer_ = false;
  std::unique_ptr<BufferSource> bufSrc_;
  ChapterLocation where_{};
  std::unique_ptr<FileHandle> file_;
  EntrySource entry_;
  Inflater inflater_;
  // Points at whichever source the blocks come from: the inflater's output for a
  // deflated entry, the entry's own bytes for a stored one.
  std::unique_ptr<InflateSource> inflated_;
  std::unique_ptr<BlockReader> blocks_;
  const std::vector<std::string>* italicClasses_ = nullptr;
  int position_ = 0;
  // Whether `inflated_` is the source the blocks are ACTUALLY coming from. Not
  // `inflated_ != nullptr`: the wrapper is kept across a re-stream so a rewind does
  // not reallocate it, so a stored entry or an in-memory chapter following a deflated
  // one would otherwise be answered from the PREVIOUS chapter's decoder. Set at the
  // one place that chooses a source.
  bool inflateActive_ = false;
  const char* error_ = "";
};

}  // namespace reader
