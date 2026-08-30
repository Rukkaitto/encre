#pragma once
#include <string>
#include <vector>

#include "reader/chapter.h"
#include "reader/document.h"
#include "reader/filesystem.h"

namespace reader {

// A FILE ON THE CARD INTO A CHAPTER YOU CAN LAY OUT. The one function that joins
// 3B's four layers -- zip, xml, epub, document -- to the filesystem.
//
// IT LIVES IN core/ RATHER THAN IN THE SHELL, and that is the whole point of it
// existing at all. The shell has no test harness, and this project has now traced
// five bugs to code that lived there because it touched hardware: the wrong reset
// reason, the erased breadcrumbs, the 2500ms USB wait, the probe that reset the
// device it measured, the battery-resume path. "It needs a filesystem" is not a
// reason to be untestable -- FileSystem is an interface, and test/unit/fake_fs.h
// serves real EPUB bytes through it.
//
// It also keeps the ORDER right in one place. A Zip must stay alive while its
// entries are read, the FileHandle must outlive the Zip, and the Document must
// outlive every page laid out from it -- three lifetimes with one correct nesting,
// which is exactly the sort of thing each caller would get subtly differently.
//
// WHAT IT HANDS BACK IS THE BOOK'S GEOMETRY, NOT A CHAPTER. It used to return the
// whole chapter's blocks, which is exactly what could not be afforded: Le Fléau's
// longest chapter is 228,849 bytes of them.
//
// AND NOT ONE CHAPTER'S LOCATION EITHER, which was the next mistake. It took a
// chapter index and returned that chapter's offsets, so the reader called it again
// for every chapter it wanted -- and every call re-parses the 121-entry central
// directory and re-inflates the 8,472-byte OPF. The device measured it: the open's
// pagination phase took minimum free heap from 85,860 to 41,188 and cost 433 ms of
// a 511 ms open, because reaching this book's first chapter with text means trying
// three spine entries and therefore three of those parses.
//
// The whole spine's offsets are 12 bytes an entry -- 1,104 for a 92-chapter book --
// so they are read ONCE and kept. A chapter change is then picking a row: no
// archive, no directory, no OPF.
struct ChapterSpan {
  // The LOCAL HEADER's offset, not the data's. Resolving one to the other is a
  // 30-byte read, and doing it for all 92 entries when the book opens cost 368 ms --
  // more than the pagination it was meant to make cheap. ChapterReader resolves the
  // one chapter it is asked for.
  uint32_t localHeaderOffset = 0;
  uint32_t compressedSize = 0;    // 0 means the spine named an entry the archive lacks
  // What it inflates to. Not needed to READ the chapter -- it is needed to decide
  // whether counting its pages is cheap enough to do before the first paint, and the
  // central directory has already said, so it costs nothing to carry.
  uint32_t uncompressedSize = 0;
  bool deflated = true;         // false for a stored entry: the bytes are the text

  // A spine entry with no bytes. Epub::open validates EVERY spine entry against the
  // manifest and the archive and refuses the whole book if one is missing
  // (epub.cpp:186 and :189, two distinct messages), so in practice this is only a
  // zero-length entry. A local header that will not parse is found later, by
  // ChapterReader, and refused there.
  bool readable() const { return compressedSize > 0; }
};

struct OpenedBook {
  std::string path;    // the EPUB on the card, as given
  std::string title;   // from the OPF, as authored
  std::string author;
  // One per spine entry, in spine order. `size()` is the chapter count.
  std::vector<ChapterSpan> chapters;

  // WHERE THE COVER IS, or an unreadable span if the book declares none.
  //
  // A ChapterSpan rather than a new type, because it is the same four facts -- a
  // local header offset, two sizes and whether it is deflated -- and a second type
  // spelling one shape is what this project's own rule warns about. It is not a
  // chapter and it is not in `chapters`: the spine names what to READ, and a cover
  // is not in it.
  //
  // NOTED DURING THE OPF WALK, not looked up later: Epub already resolves every
  // manifest href, so finding it afterwards would re-parse the OPF for ~100 ms and
  // ~32 KB of transient. Exactly the argument Epub already makes for the NCX.
  //
  // TWO ROUTES, BOTH NEEDED. `<meta name="cover" content="id">` is the EPUB 2
  // convention and what a 225-book corpus overwhelmingly uses; `properties="cover-image"`
  // is EPUB 3's. Neither is required by any spec, so a book may have neither -- and
  // that is NOT an error. The book opens, and the screen that wanted a picture falls
  // back to what it drew before.
  ChapterSpan cover;

  int chapterCount() const { return static_cast<int>(chapters.size()); }

  // Where chapter `i` is, for a ChapterReader. An unreadable or out-of-range index
  // yields a location with no size, which ChapterReader refuses.
  ChapterLocation locate(int i) const {
    if (i < 0 || i >= chapterCount()) return ChapterLocation{};
    return locationOf(chapters[static_cast<size_t>(i)]);
  }

  // Where the cover is, for a decoder. A book with no cover yields a location with no
  // size and no path -- which every consumer already treats as "there is nothing
  // here", so nothing needs a special case for the commonest reason a cover does not
  // appear.
  ChapterLocation locateCover() const { return locationOf(cover); }

 private:
  // "REFUSES THE SAME WAY" WAS A CLAIM A COMMENT MADE, and it is one function now.
  // locate() and locateCover() were the same five-field fill with the same
  // readable() gate written twice, which is this project's own second-copy rule
  // arriving one copy late again -- and the failure mode is precise: a span that
  // reported a path with no size, or a size with no path, would be refused by some
  // consumers and not others.
  ChapterLocation locationOf(const ChapterSpan& c) const {
    ChapterLocation out;
    if (!c.readable()) return out;
    out.bookPath = path;
    out.localHeaderOffset = c.localHeaderOffset;
    out.uncompressedSize = c.uncompressedSize;
    out.compressedSize = c.compressedSize;
    out.deflated = c.deflated;
    return out;
  }
};

// `path` is the EPUB, absolute on `fs`.
//
// False with `*reason` set for a missing file, a zip that is not one, an OPF that
// does not parse, a spine with nothing in it, or -- via Epub::open -- any spine
// entry the manifest or the archive does not hold. NEVER an abort: this is bytes off
// a user's card, and the caller has a screen it can put the reason on.
bool openBook(FileSystem& fs, std::string_view path, OpenedBook& out, const char** reason);

}  // namespace reader
