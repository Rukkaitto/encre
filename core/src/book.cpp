#include "reader/book.h"

#include <memory>
#include <new>

#include "reader/epub.h"
#include "reader/zip.h"

namespace reader {

bool openChapter(FileSystem& fs, std::string_view path, int chapter, OpenedChapter& out,
                 const char** reason) {
  out = OpenedChapter{};

  // The nesting the header describes, spelled once. `file` outlives `zip`, which
  // outlives the read; the Document it produces outlives all three and is what
  // leaves this function.
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

  out.bookTitle = book.title();
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

  std::string xhtml;
  if (!zip.read(*file, *entry, xhtml)) {
    *reason = zip.reason();
    return false;
  }

  // A PRE-FLIGHT ON THE HEAP, because buildDocument cannot fail politely.
  //
  // Zip's own allocations are nothrow-checked now, but a Document is std::strings
  // and a std::vector growing as the parse runs, and every one of those aborts
  // under -fno-exceptions. The peak is the XHTML *and* the blocks live at once --
  // buildDocument reads from the one while filling the other -- and the blocks are
  // bounded above by the XHTML's own size, since they hold a subset of its bytes.
  //
  // So: can the heap serve a second copy? If not, refuse here, where there is a
  // reason to hand back. This is a bound and not a guarantee -- std::string's
  // doubling can briefly want 1.5x what it ends up holding -- and the honest
  // remaining risk is that a chapter close to the limit still aborts. It converts
  // the common case, which is a chapter that is simply too big for this device.
  {
    char* probe = new (std::nothrow) char[xhtml.size() + 1];
    const bool room = probe != nullptr;
    delete[] probe;
    if (!room) {
      *reason = "not enough memory to lay out this chapter";
      return false;
    }
  }

  // The XHTML is a local and dies here. Only the blocks survive -- which for a
  // real chapter is ~2.5 KB of text out of a ~10 KB inflate, and the difference
  // matters on a 300 KB heap.
  return buildDocument(xhtml, out.doc, reason);
}

}  // namespace reader
