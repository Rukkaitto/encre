#include "reader/book.h"

#include <memory>

#include "reader/epub.h"
#include "reader/heapguard.h"
#include "reader/zip.h"

namespace reader {
namespace {

// The four facts the CENTRAL DIRECTORY already holds about an entry. Written twice
// -- once for a chapter and once for the cover -- before it was a function, which is
// this project's second-copy rule arriving one copy late again.
//
// NO LOCAL HEADER IS READ. Resolving these to a data offset is a 30-byte read per
// entry, and doing all 92 of a real book's cost 368 ms -- more than the pagination it
// was supposed to make cheap. ChapterReader does it for the one entry it opens.
ChapterSpan spanFrom(const Zip::Entry& e) {
  ChapterSpan span;
  span.localHeaderOffset = e.localHeaderOffset;
  span.compressedSize = e.compressedSize;
  span.uncompressedSize = e.uncompressedSize;
  span.deflated = e.deflated;
  return span;
}

}  // namespace

bool openBook(FileSystem& fs, std::string_view path, OpenedBook& out, const char** reason) {
  out = OpenedBook{};

  // Every one of these is a local: what leaves this function is a path, two strings
  // and twelve bytes a chapter, so the archive, its central directory and the OPF's
  // parse are all released before a single block is read. That is what makes
  // opening a chapter cost the same whatever the book -- and what makes reaching
  // ANOTHER chapter cost nothing at all.
  std::unique_ptr<FileHandle> file = fs.openRead(path);
  if (file == nullptr) {
    *reason = kOpenCannotOpen;
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
  // The only allocation this function makes that is sized by the book: 16 bytes an
  // entry, so 5,136 for the longest spine in a 225-book corpus, taken while the Zip
  // and the Epub above are BOTH still held. Small, and guarded for the reason the
  // big ones are -- a `reserve` that cannot allocate is the same silent `abort()`
  // whatever its size, and the loop under it pushes exactly this many.
  if (!ensureRoom(out.chapters, book.chapters().size())) {
    *reason = "not enough memory to hold the book's chapter list";
    return false;
  }

  for (const Epub::Chapter& ch : book.chapters()) {
    // Epub::open has already refused the book if any spine entry is missing, so
    // `find` succeeds here; a null is the zero span, which locate() refuses.
    const Zip::Entry* entry = zip.find(ch.path);
    out.chapters.push_back(entry != nullptr ? spanFrom(*entry) : ChapterSpan{});
  }

  if (out.chapters.empty()) {
    *reason = "the spine names no chapters";
    return false;
  }

  // THE COVER, FROM THE SAME CENTRAL DIRECTORY AND THE SAME FOUR FACTS. Epub has
  // already resolved the href against the OPF's directory -- the identical
  // resolveHref a spine href goes through -- so what is left here is a lookup.
  //
  // BELOW THE REFUSAL, deliberately: a book about to be refused should not pay for a
  // cover nobody will see. Unreachable in practice, since Epub::open already refuses
  // an empty spine, but the ordering should read as decided rather than as wherever
  // the code happened to be typed.
  //
  // A COVER THE ARCHIVE DOES NOT HOLD IS NOT A REFUSAL, which is where this differs
  // from a chapter: Epub::open validates every SPINE entry against the archive and
  // refuses the whole book if one is missing, because a spine is a reading order and
  // a book missing a chapter is a book the reader cannot tell is broken. A cover is
  // metadata. `out.cover` stays unreadable and the book opens.
  if (!book.coverPath().empty()) {
    const Zip::Entry* art = zip.find(book.coverPath());
    if (art != nullptr) out.cover = spanFrom(*art);
  }
  return true;
}

}  // namespace reader
