#include "reader/book.h"

#include <memory>

#include "reader/epub.h"
#include "reader/zip.h"

namespace reader {

bool openBook(FileSystem& fs, std::string_view path, int chapter, OpenedBook& out,
              const char** reason) {
  out = OpenedBook{};

  // The nesting the header describes, spelled once. Every one of these is a local:
  // what leaves this function is a path and three numbers, so the archive, its
  // central directory and the OPF's parse are all released before a single block is
  // read. That is what makes opening a chapter cost the same whatever the book.
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

  out.title = book.title();
  out.author = book.author();
  out.chapterCount = static_cast<int>(book.chapters().size());
  if (chapter < 0 || chapter >= out.chapterCount) {
    *reason = "no such chapter";
    return false;
  }

  const Zip::Entry* entry = zip.find(book.chapters()[static_cast<size_t>(chapter)].path);
  if (entry == nullptr) {
    // The spine named a manifest item whose file is not in the archive. Epub::open
    // does not catch this: it resolves hrefs against the OPF's directory without
    // checking that the result exists, because a book with one broken chapter
    // should still open.
    *reason = "the chapter named by the spine is not in the archive";
    return false;
  }

  uint32_t dataOffset = 0;
  if (!zip.locate(*file, *entry, dataOffset)) {
    *reason = zip.reason();
    return false;
  }

  out.chapter.bookPath = std::string(path);
  out.chapter.dataOffset = dataOffset;
  out.chapter.compressedSize = entry->compressedSize;
  out.chapter.deflated = entry->deflated;
  return true;
}

}  // namespace reader
