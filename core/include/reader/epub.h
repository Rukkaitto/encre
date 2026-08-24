#pragma once
#include <string>
#include <vector>

#include "reader/zip.h"

namespace reader {

// An EPUB's structure: what the book is called, and what order to read it in.
//
// Three files deep, and each step can fail for its own reason:
//
//   META-INF/container.xml  ->  names the OPF's path
//   the OPF                 ->  metadata, a manifest of parts, a spine order
//   the spine               ->  the chapters, in reading order
//
// This layer does NOT parse a chapter. It hands back the archive entry for one,
// and the document model above turns bytes into a tree. Keeping that split means
// a book with one broken chapter still opens.
class Epub {
 public:
  struct Chapter {
    std::string id;    // the manifest id the spine referenced
    std::string path;  // the entry's full path in the archive, already resolved
  };

  // A spine longer than this is refused. A long novel is a few hundred files;
  // this is the number that sizes a vector, so it is bounded like everything else
  // a file gets to claim.
  static constexpr size_t kMaxChapters = 1024;

  // False on anything not understood, with `reason()` saying which. `file` and
  // `zip` are BORROWED and must outlive this: an Epub holds paths, not bytes, so
  // a reader can keep a book open across pages without a second copy of anything.
  bool open(FileHandle& file, Zip& zip);

  const std::string& title() const { return title_; }
  const std::string& author() const { return author_; }
  // The `unique-identifier`'s resolved value. Non-empty on success, because a
  // book whose identifier does not resolve is refused -- see open().
  const std::string& identifier() const { return identifier_; }

  const std::vector<Chapter>& chapters() const { return chapters_; }

  // The table of contents' resolved archive path, or EMPTY when the book has none.
  //
  // Empty is not a failure. Measured over four real books, every one carries an EPUB 2
  // `toc.ncx` and NOT ONE has an EPUB 3 nav document -- so this is the NCX's path in
  // practice. A book with neither still opens and reads; it just cannot offer a
  // chapter list, which is a missing feature and not a broken book.
  const std::string& tocPath() const { return tocPath_; }

  const char* reason() const { return reason_; }

 private:
  bool fail(const char* why);

  std::string title_;
  std::string author_;
  std::string identifier_;
  std::vector<Chapter> chapters_;
  std::string tocPath_;
  const char* reason_ = "";
};

// Resolve `href` relative to the directory holding `base`, the way an OPF's
// manifest hrefs are relative to the OPF itself.
//
// Exposed and tested on its own because it is where a path bug hides: the OPF at
// `OEBPS/content.opf` with an href of `ch1.xhtml` means `OEBPS/ch1.xhtml`, and
// getting that wrong produces "the chapter is not in the archive" for a book that
// is perfectly fine. `..` segments are RESOLVED, and one that would climb above
// the archive root is refused -- a zip entry name is not a filesystem path and
// `../../etc` is not a thing an EPUB may address.
bool resolveHref(std::string_view base, std::string_view href, std::string& out);

}  // namespace reader
