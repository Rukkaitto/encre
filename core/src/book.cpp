#include "reader/book.h"

#include <memory>

#include "reader/epub.h"
#include "reader/zip.h"

namespace reader {

bool openBook(FileSystem& fs, std::string_view path, OpenedBook& out, const char** reason) {
  out = OpenedBook{};

  // Every one of these is a local: what leaves this function is a path, two strings
  // and twelve bytes a chapter, so the archive, its central directory and the OPF's
  // parse are all released before a single block is read. That is what makes
  // opening a chapter cost the same whatever the book -- and what makes reaching
  // ANOTHER chapter cost nothing at all.
  std::unique_ptr<FileHandle> file = fs.openRead(path);
  if (file == nullptr) {
    *reason = "cannot open the book file";
    return false;
  }

  Zip zip;
  if (!zip.open(*file)) {
    *reason = zip.reason();
    return false;
  }

  Epub book;
  if (!book.open(*file, zip)) {
    *reason = book.reason();
    return false;
  }

  out.path = std::string(path);
  out.title = book.title();
  out.author = book.author();
  out.chapters.reserve(book.chapters().size());

  for (const Epub::Chapter& ch : book.chapters()) {
    ChapterSpan span;
    const Zip::Entry* entry = zip.find(ch.path);
    // NO LOCAL HEADER IS READ HERE. Epub::open has already refused the book if any
    // spine entry is missing, so `find` succeeds; what is left is two numbers the
    // central directory already holds. Resolving them to a data offset is a 30-byte
    // read per entry, and doing all 92 of a real book's cost 368 ms -- more than the
    // pagination it was supposed to make cheap. ChapterReader does it for the one
    // chapter it opens.
    if (entry != nullptr) {
      span.localHeaderOffset = entry->localHeaderOffset;
      span.compressedSize = entry->compressedSize;
      span.deflated = entry->deflated;
    }
    out.chapters.push_back(span);
  }

  if (out.chapters.empty()) {
    *reason = "the spine names no chapters";
    return false;
  }
  return true;
}

}  // namespace reader
