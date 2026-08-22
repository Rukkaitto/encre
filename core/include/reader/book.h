#pragma once
#include <string>

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
// WHAT IT DOES NOT DO: keep the book open. It reads ONE chapter and closes the
// file, because a Document is self-contained and the alternative is a card handle
// held for as long as someone is reading -- across a sleep, across a card removal.
// Turning to the next chapter reopens. That costs a zip central-directory parse
// per chapter, which is measured in the roadmap and is not what a page turn pays.
struct OpenedChapter {
  Document doc;
  std::string bookTitle;  // from the OPF, as authored
  std::string author;
  int chapterCount = 0;
};

// `path` is the EPUB, absolute on `fs`. `chapter` indexes Epub::chapters().
//
// False with `*reason` set for every failure -- a missing file, a zip that is not
// one, an OPF that does not parse, a chapter index past the spine, XHTML the
// document builder refuses. NEVER an abort: this is bytes off a user's card, and
// the caller has a screen it can put the reason on.
bool openChapter(FileSystem& fs, std::string_view path, int chapter, OpenedChapter& out,
                 const char** reason);

}  // namespace reader
