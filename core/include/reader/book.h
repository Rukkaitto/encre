#pragma once
#include <string>

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
// WHAT IT HANDS BACK IS A LOCATION, NOT A DOCUMENT. It used to return the whole
// chapter's blocks, which is exactly what could not be afforded: Le Fléau's longest
// chapter is 228,849 bytes of them. So it opens the archive, reads the metadata,
// finds where the chapter's compressed bytes begin, and closes -- and a
// ChapterReader streams from that location afterwards.
//
// The archive is therefore parsed once per chapter OPENED, not per page turned: a
// ChapterLocation is a path and three numbers, so re-reading the chapter for a
// backward page turn needs no central directory at all.
struct OpenedBook {
  std::string title;   // from the OPF, as authored
  std::string author;
  int chapterCount = 0;
  ChapterLocation chapter;  // where the requested chapter's bytes are
};

// `path` is the EPUB, absolute on `fs`. `chapter` indexes Epub::chapters().
//
// False with `*reason` set for every failure -- a missing file, a zip that is not
// one, an OPF that does not parse, a chapter index past the spine, an entry the
// spine names but the archive does not contain. NEVER an abort: this is bytes off a
// user's card, and the caller has a screen it can put the reason on.
bool openBook(FileSystem& fs, std::string_view path, int chapter, OpenedBook& out,
              const char** reason);

}  // namespace reader
