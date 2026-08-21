#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reader {

class FileSystem;

// One row of the Library: a file or folder on the card that the user might want
// to open.
//
// `title` is what the screen draws and `name` is what the filesystem knows the
// thing by; they are separate because Phase 3 will fill `title` from the EPUB's
// own metadata while `name` still has to address the file. Until then the title
// is derived from the filename, which is the placeholder the plan's Scope
// section says it is.
struct BookEntry {
  std::string name;   // leaf name as the card spells it, never a path
  std::string title;  // what the row shows: the name minus its final extension
  bool isDir = false;
  uint32_t size = 0;  // 0 for a directory
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
