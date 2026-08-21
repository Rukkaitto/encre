#include "reader/booklist.h"

#include <algorithm>

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

std::string BookList::titleFor(std::string_view name, bool isDir) {
  if (isDir) return std::string(name);
  const size_t dot = extDot(name);
  if (dot == std::string_view::npos) return std::string(name);
  return std::string(name.substr(0, dot));
}

int BookList::countBooks(FileSystem& fs, std::string_view path) {
  std::vector<DirEntry> raw;
  if (!fs.list(path, raw)) return -1;
  int n = 0;
  // Files only, and the same isBook/isHidden rules the listing itself uses -- a
  // count that disagreed with the rows it summarises would be worse than none.
  // Subdirectories are not counted and not walked: see the header.
  for (const DirEntry& e : raw)
    if (!e.isDir && isBook(e.name)) ++n;
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

  out.reserve(raw.size());
  for (const DirEntry& e : raw) {
    if (isHidden(e.name)) continue;
    // A folder is a place to descend into, so its name says nothing about
    // whether it belongs on the list -- including when it ends in something
    // that looks like a rejected extension.
    if (!e.isDir && !isBook(e.name)) continue;
    out.push_back(BookEntry{e.name, titleFor(e.name, e.isDir), e.isDir, e.isDir ? 0u : e.size});
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
    if (lessNoCase(a.title, b.title)) return true;
    if (lessNoCase(b.title, a.title)) return false;
    return a.name < b.name;
  });
  return true;
}

}  // namespace reader
