#include "reader/booklist.h"

#include <algorithm>

#include "reader/dir_counts.h"
#include "reader/filesystem.h"

namespace reader {

namespace {

// ASCII-only, deliberately. The card's filenames are bytes of unknown encoding
// -- FAT long names are UTF-16 but SdFat hands them over as 8-bit, and a card
// written by another tool may hold anything -- so case-folding beyond ASCII
// would need a table and would still be guessing. Non-ASCII bytes therefore sort
// by their byte value, which is stable and wrong in the same way on every card.
char lowerAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// True when `a` sorts before `b` ignoring ASCII case. Not a full comparator on
// its own: two names that differ only in case compare equal here, and std::sort
// is not stable, so the caller has to break the tie itself or the order comes
// out of the sort's internals and two cards with the same books look different.
bool lessNoCase(std::string_view a, std::string_view b) {
  const size_t n = a.size() < b.size() ? a.size() : b.size();
  for (size_t i = 0; i < n; ++i) {
    const char ca = lowerAscii(a[i]), cb = lowerAscii(b[i]);
    if (ca != cb) return static_cast<unsigned char>(ca) < static_cast<unsigned char>(cb);
  }
  return a.size() < b.size();
}

// The final '.' in `name`, or npos when it has no extension. Position 0 is not
// an extension separator -- that is a dotfile, which isHidden has already
// rejected -- so a leading dot never yields an empty title.
size_t extDot(std::string_view name) {
  const size_t dot = name.rfind('.');
  return (dot == std::string_view::npos || dot == 0) ? std::string_view::npos : dot;
}

}  // namespace

bool BookList::isHidden(std::string_view name) {
  return name.empty() || name.front() == '.';
}

bool BookList::isBook(std::string_view name) {
  if (isHidden(name)) return false;
  const size_t dot = extDot(name);
  if (dot == std::string_view::npos) return false;
  const std::string_view ext = name.substr(dot + 1);
  // Two extensions, spelled out rather than looped over a table: a table of two
  // is a table someone extends without asking what the reader can actually open.
  if (ext.size() == 4) {
    return lowerAscii(ext[0]) == 'e' && lowerAscii(ext[1]) == 'p' &&
           lowerAscii(ext[2]) == 'u' && lowerAscii(ext[3]) == 'b';
  }
  if (ext.size() == 3) {
    return lowerAscii(ext[0]) == 't' && lowerAscii(ext[1]) == 'x' && lowerAscii(ext[2]) == 't';
  }
  return false;
}

std::string_view BookList::titleView(std::string_view name, bool isDir) {
  // Always a PREFIX of `name`, which is what lets BookEntry::title() be a view.
  // A folder keeps its whole name; a file loses its final extension.
  if (isDir) return name;
  const size_t dot = extDot(name);
  return dot == std::string_view::npos ? name : name.substr(0, dot);
}

std::string BookList::titleFor(std::string_view name, bool isDir) {
  return std::string(titleView(name, isDir));
}

std::string_view BookEntry::title() const {
  return titleOverride.empty() ? BookList::titleView(name, isDir)
                               : std::string_view(titleOverride);
}

// Set by scan(), read by lastScanDropped(). A single-threaded screen calls one
// then the other; nothing here is reentrant and nothing else writes it.
size_t gLastScanDropped = 0;

int BookList::countBooks(FileSystem& fs, std::string_view path) {
  // ASKED FOR ONCE PER CARD STATE, NOT ONCE PER CALLER. Two callers want this
  // same number for the same folder -- Home's LIBRARY row through countLibrary
  // at boot, and the Library's rescan() through its `FOLDER - 6 BOOKS` row on
  // every push -- and a folder's listing is ~2.90 ms an ENTRY that no amount of
  // work on our side makes cheaper (reader/dir_cache.h has the SdFat reasoning).
  // So the saving has to be FEWER walks, and the memo is where they go.
  //
  // WHAT INVALIDATES IT: every mutating method of the FileSystem this was asked
  // for, before it touches anything. Nothing can bypass that, because there is no
  // way to change what is on the card except through this object -- which is why
  // the memo hangs off it rather than off either caller. dir_counts.h has the
  // full argument, including why it beats the removals() key Home's own cached
  // integer uses and what V2's Wi-Fi transfer does to that one.
  //
  // A null memo is the ordinary case for HostFileSystem and for any wrapper that
  // has not thought about it, and it means exactly today's behaviour.
  DirCountCache* memo = fs.dirCounts();
  int remembered = 0;
  if (memo != nullptr && memo->lookup(path, remembered)) return remembered;

  std::vector<DirEntry> raw;
  // A FAILURE IS NOT REMEMBERED. -1 is "could not look", not a count, and holding
  // it would make one bad read permanent for the rest of the session -- where the
  // card coming back is exactly the case the SD-missing screen's RETRY exists for.
  if (!fs.list(path, raw)) return -1;
  int n = 0;
  // Files only, and the same isBook/isHidden rules the listing itself uses -- a
  // count that disagreed with the rows it summarises would be worse than none.
  // Subdirectories are not counted and not walked: see the header.
  for (const DirEntry& e : raw)
    if (!e.isDir && isBook(e.name)) ++n;
  if (memo != nullptr) memo->remember(path, n);
  return n;
}

size_t BookList::lastScanDropped() { return gLastScanDropped; }

int BookList::countLibrary(FileSystem& fs, std::string_view path) {
  // Works off the RAW listing, not scan(). It used to call scan(), which builds a
  // BookEntry per row and then SORTS them -- to produce one integer, on the boot
  // path, before the first paint. A count does not care what order it counts in.
  //
  // The rules still have to be scan()'s rules, or the band would disagree with
  // the rows it summarises, so isHidden/isBook are applied here exactly as the
  // listing applies them and test_booklist.cpp pins the two against each other.
  std::vector<DirEntry> raw;
  if (!fs.list(path, raw)) return -1;
  int n = 0;
  for (const DirEntry& e : raw) {
    if (isHidden(e.name)) continue;
    if (!e.isDir) {
      if (isBook(e.name)) ++n;
      continue;
    }
    // A folder whose own listing failed contributes nothing rather than being
    // counted as a book: `> 0` is the same guard syncVm uses, so an unreadable
    // subdirectory (-1) cannot subtract from the total.
    //
    // The join tolerates an empty or root `path` because the contract says a
    // redundant separator addresses the same thing -- "//Classics" is
    // "/Classics" -- and reaching for path.back() on an empty view would not be
    // a wrong path, it would be undefined behaviour.
    std::string child(path);
    if (child.empty() || child.back() != '/') child += '/';
    child += e.name;
    const int inside = countBooks(fs, child);
    if (inside > 0) n += inside;
  }
  return n;
}

bool BookList::scan(FileSystem& fs, std::string_view path, std::vector<BookEntry>& out) {
  // Cleared before the read, not after a successful one: a rescan that fails --
  // the card was pulled between the delete and the refresh -- must not leave the
  // previous listing on screen, because those rows would open files that are no
  // longer addressable.
  out.clear();

  std::vector<DirEntry> raw;
  if (!fs.list(path, raw)) return false;

  // Reserve from the count of rows we will actually KEEP, not from the listing.
  // A macOS-copied card carries a `._name` AppleDouble beside every book, so
  // reserving raw.size() over-allocated by 2x on a real 203-book card -- 22 KB of
  // slots for 11 KB of rows. Counting first is one pass over a vector already in
  // RAM, against an allocation charged per book.
  size_t keep = 0;
  for (const DirEntry& e : raw)
    if (!isHidden(e.name) && (e.isDir || isBook(e.name))) ++keep;
  gLastScanDropped = keep > kMaxLibraryRows ? keep - kMaxLibraryRows : 0;
  out.reserve(keep < kMaxLibraryRows ? keep : kMaxLibraryRows);

  for (DirEntry& e : raw) {
    if (isHidden(e.name)) continue;
    // A folder is a place to descend into, so its name says nothing about
    // whether it belongs on the list -- including when it ends in something
    // that looks like a rejected extension.
    if (!e.isDir && !isBook(e.name)) continue;
    // `e.name` is MOVED, not copied. `raw` is a local that dies at the closing
    // brace, so copying meant every name existed twice at the peak -- and the
    // peak is the number that matters, because it is what -fno-exceptions turns
    // into an abort() with no diagnostic. `raw` is left holding emptied strings,
    // which is fine: nothing reads it again.
    //
    // No title is built either. It is derived from the name by BookEntry::title()
    // now, so this loop allocates once per book instead of three times.
    if (out.size() >= kMaxLibraryRows) break;
    out.push_back(BookEntry{std::move(e.name), std::string(), e.isDir,
                            e.isDir ? 0u : e.size});
  }

  // FAT gives no ordering guarantee at all, so the order is ours to impose: a
  // listing that inherited the filesystem's order would look different on two
  // cards holding the same books.
  //
  // Folders first (they are navigation, not content), then by the TITLE, which
  // is what the row shows -- sorting by the filename would order by an extension
  // the screen does not display. Ties fall back to the name's bytes so the
  // comparator is a strict total order: two names differing only in case compare
  // equal case-blind, and std::sort is not stable, so without the fallback their
  // order would come out of the sort's internals.
  std::sort(out.begin(), out.end(), [](const BookEntry& a, const BookEntry& b) {
    if (a.isDir != b.isDir) return a.isDir;
    if (lessNoCase(a.title(), b.title())) return true;
    if (lessNoCase(b.title(), a.title())) return false;
    return a.name < b.name;
  });
  return true;
}

}  // namespace reader
