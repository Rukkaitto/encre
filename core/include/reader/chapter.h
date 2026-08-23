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

  // Back to block 0, reusing every buffer -- no 32 KB reallocation. This is what a
  // backward page turn costs.
  bool rewind();

  // The next block. False means the chapter ended (`ok()`) or was refused.
  bool next(Block& out);

  // The index of the block `next()` will return, which is what a page Cursor names.
  int position() const { return position_; }

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
  int position_ = 0;
  const char* error_ = "";
};

}  // namespace reader
