#include "reader/chapter.h"

#include <new>

namespace reader {

bool ChapterReader::begin(FileSystem& fs, const ChapterLocation& where) {
  fs_ = &fs;
  where_ = where;
  position_ = 0;
  error_ = "";
  file_ = fs.openRead(where_.bookPath);
  if (file_ == nullptr) {
    error_ = "cannot open the book file";
    return false;
  }
  if (where_.compressedSize == 0) {
    error_ = "this chapter has no bytes";
    return false;
  }
  // ONE 30-byte read, here rather than when the book was opened -- see
  // ChapterSpan. Cached, so a rewind does not go back to the card for it.
  if (!Zip::locateData(*file_, where_.localHeaderOffset, where_.compressedSize, dataOffset_)) {
    error_ = "this chapter's local header will not parse";
    return false;
  }
  return startStream();
}

bool ChapterReader::beginBuffer(std::string_view xhtml) {
  fs_ = nullptr;
  file_.reset();
  fromBuffer_ = true;
  buffer_.assign(xhtml);
  where_ = ChapterLocation{};
  position_ = 0;
  error_ = "";
  return startStream();
}

bool ChapterReader::rewind() {
  if (fromBuffer_) {
    position_ = 0;
    error_ = "";
    return startStream();
  }
  if (fs_ == nullptr) {
    error_ = "nothing to rewind";
    return false;
  }
  position_ = 0;
  error_ = "";
  // The HANDLE is kept: seeking it back is what a rewind is, and reopening would
  // cost a directory walk on the card for no gain. `startStream` seeks through
  // EntrySource, which addresses the entry absolutely.
  if (file_ == nullptr) {
    file_ = fs_->openRead(where_.bookPath);
    if (file_ == nullptr) {
      error_ = "cannot reopen the book file";
      return false;
    }
  }
  return startStream();
}

void ChapterReader::release() {
  // ORDER IS INNERMOST FIRST, because blocks_ reads through inflated_ which reads
  // through inflater_/bufSrc_/file_. Destroying an owner before its user would leave a
  // live object reading freed memory for as long as the reset expression took, which
  // is not observable today and is the kind of ordering that stops being safe
  // silently.
  blocks_.reset();
  inflated_.reset();
  // THE LINE THE FEATURE IS FOR. The four resets around it free a block reader, a
  // ~40-byte wrapper, a buffer view and a file handle; this frees the ~37 KB. See the
  // header for why it is not a unique_ptr like its neighbours.
  inflater_.release();
  bufSrc_.reset();
  file_.reset();
  // `where_`, `fromBuffer_`, `buffer_` and `dataOffset_` are deliberately NOT cleared
  // -- see the header. They are what begin() and rewind() need to put this back, and
  // dataOffset_ is what keeps a reacquire from going back to the card for a header it
  // has already read.
}

bool ChapterReader::startStream() {
  ByteSource* bytes = nullptr;

  if (fromBuffer_) {
    if (bufSrc_ == nullptr) {
      bufSrc_.reset(new (std::nothrow) BufferSource(buffer_));
      if (bufSrc_ == nullptr) {
        error_ = "not enough memory to read this chapter";
        return false;
      }
    } else {
      bufSrc_->reset(buffer_);
    }
    bytes = bufSrc_.get();
    if (blocks_ == nullptr) {
      blocks_.reset(new (std::nothrow) BlockReader(*bytes));
      if (blocks_ != nullptr) blocks_->setItalicClasses(italicClasses_);
      if (blocks_ == nullptr || !blocks_->ok()) {
        error_ = "not enough memory to read this chapter";
        return false;
      }
    } else {
      blocks_->restart(*bytes);
    blocks_->setItalicClasses(italicClasses_);
    }
    return true;
  }

  entry_.reset(*file_, dataOffset_, where_.compressedSize);

  if (where_.deflated) {
    // begin() reuses the window if one is already allocated, which is what keeps a
    // rewind from churning 32 KB on a heap that has ~142 KB free.
    if (!inflater_.begin(entry_)) {
      error_ = inflater_.error();
      return false;
    }
    if (inflated_ == nullptr) {
      inflated_.reset(new (std::nothrow) InflateSource(inflater_));
      if (inflated_ == nullptr) {
        error_ = "not enough memory to read this chapter";
        return false;
      }
    } else {
      // Forget the chunk in hand: it belongs to the stream just abandoned.
      inflated_->reset();
    }
    bytes = inflated_.get();
  } else {
    // A stored entry: the bytes ARE the text, so the inflater is skipped entirely.
    // Rare in an EPUB's content but legal, and cheaper than pretending.
    bytes = &entry_;
  }

  if (blocks_ == nullptr) {
    blocks_.reset(new (std::nothrow) BlockReader(*bytes));
    if (blocks_ != nullptr) blocks_->setItalicClasses(italicClasses_);
    if (blocks_ == nullptr || !blocks_->ok()) {
      error_ = "not enough memory to read this chapter";
      return false;
    }
  } else {
    blocks_->restart(*bytes);
    blocks_->setItalicClasses(italicClasses_);
  }
  return true;
}

uint32_t ChapterReader::sizeBytes() const {
  return fromBuffer_ ? static_cast<uint32_t>(buffer_.size()) : where_.uncompressedSize;
}

bool ChapterReader::next(Block& out) {
  if (blocks_ == nullptr || !ok()) return false;
  if (!blocks_->next(out)) {
    // A refusal from any layer surfaces here with the layer's own words: the
    // tokenizer's for malformed markup, the inflater's for a corrupt stream.
    if (!blocks_->ok()) error_ = blocks_->error();
    else if (where_.deflated && !inflater_.done() && inflater_.error()[0] != '\0')
      error_ = inflater_.error();
    return false;
  }
  ++position_;
  return true;
}

}  // namespace reader
