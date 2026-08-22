#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reader {

class FileSystem;

// WHERE BOOKS LIVE, as spec 4.1 puts it. One definition, because three callers
// need the same string and none of them may guess: the shell roots the device's
// Library here and creates the directory when a card has none, the simulator
// prefers it under a `--root` so a card image behaves like a card, and the sample
// content claims it as its own path so Book details' `Location` row reads the
// same on the desktop as on the device.
constexpr const char* kBooksRoot = "/books";

// One row of the Library: a file or folder on the card that the user might want
// to open.
//
// `title()` is what the screen draws and `name` is what the filesystem knows the
// thing by; they are separate because Phase 3 will fill the title from the EPUB's
// own metadata while `name` still has to address the file.
//
// THE TITLE IS STORED ONLY WHEN IT DIFFERS FROM THE DERIVED ONE. Today it never
// does -- the rule is "the name minus its final extension", which is always a
// PREFIX of the name -- so `titleOverride` is empty on every entry and `title()`
// hands back a view into `name` with nothing allocated. That is worth a little
// awkwardness because it is per BOOK: the old second `std::string` heap-allocated
// on every entry whose name exceeds the 15-char small-string buffer, which is
// essentially all of them ("Middlemarch - George Eliot.epub" is 31), and a
// library is the one structure here whose size the user controls.
//
// Phase 3 fills `titleOverride` for the books whose metadata says something else,
// and pays for exactly those. The design the old comment described is intact; it
// just stopped charging for it in advance.
struct BookEntry {
  std::string name;  // leaf name as the card spells it, never a path
  // Empty means "derive from `name`". Set only when the real title differs.
  std::string titleOverride;
  bool isDir = false;
  uint32_t size = 0;  // 0 for a directory

  // What the row shows. A VIEW, into either `name` or `titleOverride` -- so it
  // lives exactly as long as this entry does and dies if `name` is reassigned.
  // Copy it into a std::string if it needs to outlive the entry.
  std::string_view title() const;
};

// Turns a directory listing into Library rows: what counts as a book, what a row
// is called, and what order the rows come in.
//
// A static-only class rather than free functions so the two filename rules can
// be tested on their own -- they are string handling with an easy off-by-one at
// each end, and pinning them through a filesystem would only ever exercise the
// cases a test remembers to create on the card.
class BookList {
 public:
  // Replaces `out` with `path`'s rows, sorted. False when `path` is not a
  // readable directory or the filesystem is not mounted; TRUE with an empty
  // `out` for a directory holding no books, which is a valid state and not the
  // same thing at all -- one draws an empty library, the other means the card is
  // gone. `out` is cleared either way, so a failed rescan cannot leave the
  // previous card's books on screen.
  static bool scan(FileSystem& fs, std::string_view path, std::vector<BookEntry>& out);

  // How many books a directory holds, or -1 when it could not be read.
  //
  // The board's folder row says `FOLDER - 6 BOOKS` and its band says `12 BOOKS`
  // over six books and that folder, so the design counts one level down. This is
  // that one level and no further: a full recursive walk of a card is unbounded
  // work on a screen that has to paint, and V1's spec asks for neither.
  //
  // -1 rather than 0 for an unreadable directory, because "no books in it" and
  // "could not look" draw differently -- the row shows a bare `FOLDER` for the
  // second, which is honest, where a 0 would be a claim.
  static int countBooks(FileSystem& fs, std::string_view path);

  // The number a LIBRARY BAND shows for `path`: the books in it plus the books
  // one level down, which is what design/Library.dc.html's `12 BOOKS` measures
  // over six books and a six-book folder. -1 when `path` could not be read.
  //
  // It exists because HOME's `LIBRARY` row shows the same number and Home is
  // built before any Library screen is, so it cannot ask one. That makes this the
  // SECOND expression of one rule -- LibraryScreen::syncVm computes it from a
  // listing it already has, rather than reading the card again during a paint --
  // and the duplication is deliberate but not unguarded: test_booklist.cpp
  // asserts the two agree on the same tree, so a change to either rule that does
  // not change both fails a test rather than showing the user two different
  // counts for one directory.
  static int countLibrary(FileSystem& fs, std::string_view path);

  // The title rule as a VIEW -- always a prefix of `name`, so it never
  // allocates. `titleFor` is this plus a copy, and both exist so there is one
  // rule rather than two that can drift.
  static std::string_view titleView(std::string_view name, bool isDir);

  // Whether a FILE belongs on the list: `.epub` or `.txt`, case-insensitively,
  // on its final extension only. FAT is case-preserving but not case-sensitive,
  // and a card written on a Mac will have a `.EPUB` on it eventually.
  static bool isBook(std::string_view name);

  // Whether an entry of either kind is hidden and should not be a row.
  //
  // A dotfile is not a book, and this earns its own function because of what a
  // card that has been mounted on a Mac carries: AppleDouble sidecars named
  // `._Book.epub`, which end in `.epub` and would appear as a phantom duplicate
  // of every real book, plus `.Spotlight-V100`, `.fseventsd` and `.Trashes`,
  // which are DIRECTORIES and so would pass any test that only filtered files.
  static bool isHidden(std::string_view name);

  // The display title: the name minus its final extension, and nothing
  // cleverer. No underscore-to-space and no title-casing -- it is a placeholder
  // until Phase 3 reads the real title out of the EPUB, and a clever transform
  // would make a wrong title look deliberate. A directory keeps its whole name;
  // it has no extension to strip, and "covers" is not the folder "covers.jpg".
  static std::string titleFor(std::string_view name, bool isDir);
};

}  // namespace reader
