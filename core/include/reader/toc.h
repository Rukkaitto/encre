#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "reader/filesystem.h"

namespace reader {

// A BOOK'S TABLE OF CONTENTS: what its chapters are called, and which spine entry
// each one names.
//
// The seventh layer of the reader, and the last one that reads the archive rather
// than the text. It exists because the spine gives an ORDER and no names: without
// this the Reader's footer can only say `CH. 03`, Book details' "Current story" row
// is blank, and there is no chapter list to jump from.
//
// --- IT IS THE NCX, NOT THE EPUB 3 NAV DOCUMENT ------------------------------
//
// Measured before it was written, over four real books: every one carries an EPUB 2
// `toc.ncx` and NOT ONE has an EPUB 3 nav document. Building the modern form first
// would have parsed something no book on this card contains.
//
// | book                    | format    | nested | bytes  | entries |
// |-------------------------|-----------|--------|--------|---------|
// | Le Fleau                | `toc.ncx` | no     | 20,821 | 96      |
// | Darkly Dreaming Dexter  | `toc.ncx` | no     |  7,012 | 28      |
// | ...another edition      | `toc.ncx` | no     |  5,931 | 31      |
//
// The nav document is a later job and a small one -- same shape, different tags --
// and `Epub::tocPath()` already answers "which part is the contents" by media type,
// so it is the only thing that would need widening.
//
// --- IT KEEPS THE DEPTH, BECAUSE REAL FILES NEST ------------------------------
//
// `Contents.dc.html` draws two levels -- `BOOK I - MISS BROOKE` with chapters beneath
// it -- and the board is RIGHT about that. This comment previously said the opposite,
// on a measurement that was wrong: a regex looking for a `navPoint` inside a
// `navPoint` required only tags between the two, and real files put text there, so it
// reported every book as flat. Parsing the NCX properly says:
//
// | book                    | entries | by depth              |
// |-------------------------|---------|-----------------------|
// | Le Fleau                | 96      | `{1: 10, 2: 84, 3: 2}`|
// | Darkly Dreaming Dexter  | 28      | `{1: 28}`             |
// | ...another edition      | 31      | `{1: 31}`             |
//
// So one book is three levels deep -- ten section headers over eighty-four chapters --
// and two are flat. Flattening would have shown `PREFACE EN DEUX PARTIES` as a peer of
// the two parts inside it, which is worse than either honest alternative.
//
// The list stays LINEAR and each entry carries its `depth`, rather than becoming a
// tree. A tree would need allocation per node and a traversal to draw, where the
// screen wants "the Nth visible row" -- and a depth is all the board's indentation and
// grouping need. Every entry is a real target either way: a section header in an NCX
// carries its own `content src`, so selecting one is meaningful.
//
// --- THE LIMITATION WORTH KNOWING ---------------------------------------------
//
// An NCX target is a file PLUS an optional fragment (`ch3.xhtml#part2`), and the
// reader positions by spine entry only. So several entries pointing into one file
// all resolve to the same spine index and all land at that file's start. They are
// KEPT rather than deduplicated: they are the book's own structure and their labels
// are real, so hiding them would hide content -- but selecting one of them is
// approximate, and that is why Le Fleau has 96 entries for 92 spine entries.
struct TocEntry {
  int spine = 0;      // the spine index this names, always in range
  int depth = 1;      // 1 for a top-level entry, 2 for one nested inside it, and so on
  std::string label;  // as authored, not shouted -- casing is the theme's decision
};

// A list this long is refused, for `Epub::kMaxChapters`' reason: it sizes a vector
// and a file is free to claim anything.
inline constexpr size_t kMaxTocEntries = 1024;

// One label's cap. A chapter title is a line on a screen; the longest observed is
// under 40 bytes, and `Xml` hands a longer run over in several pieces so an
// unbounded accumulation is a file's choice rather than a document's property.
inline constexpr size_t kMaxTocLabelBytes = 128;

// Reads `bookPath`'s table of contents.
//
// TRUE WITH AN EMPTY LIST IS A NORMAL ANSWER: a book with no NCX still opens and
// reads, it just has no chapter list. False means the archive itself would not parse,
// with `reason` saying which layer refused -- and the caller's response to that is
// the same as anywhere else in this stack, which is to say so and carry on.
//
// IT RE-OPENS THE ARCHIVE, deliberately. `OpenedBook` holds twelve bytes a spine
// entry and no hrefs, and matching an NCX target to a spine index needs the real
// paths on both sides -- so this pays one central-directory parse and one OPF inflate
// (~32 KB transient) rather than growing every book's resident footprint for a screen
// the reader opens occasionally. It is called when Contents opens, not when a book
// does.
// `italicClassesOut`, when given, also collects the class names the book's own
// stylesheets set in italics -- see reader/css.h. It rides this call rather than
// having its own because both are "what the book says about itself", both are wanted
// at the same moment, and a second archive open costs ~100 ms and a second
// central-directory parse to learn something this one already has the handles for.
bool loadToc(FileSystem& fs, std::string_view bookPath, std::vector<TocEntry>& out,
             const char** reason, std::vector<std::string>* italicClassesOut = nullptr);

// The entry naming `spine`, or -1. What the Reader needs to put a chapter's NAME in
// its footer, and what Contents needs to mark the row the reader is on.
//
// The LAST match, not the first: where several entries share a spine index (a file
// with fragments), the later ones are further into it, so the last is the closest
// thing to "where you are" that a spine-granular position can name.
int tocIndexForSpine(const std::vector<TocEntry>& toc, int spine);

}  // namespace reader
