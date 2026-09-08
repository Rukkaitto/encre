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
// RE-MEASURED OVER THE WHOLE CORPUS for issue #75, because a design turned on how these
// depths are distributed and four books is not a distribution. Of ~/.cache/encre-corpus'
// 225 real EPUBs: 19 carry no usable NCX, 103 are FLAT and 103 are sectioned, and of the
// sectioned ones 98 mix ENTRIES THAT GROUP OTHERS with top-level entries that group
// nothing -- 1,635 such entries in all, and 9 of the 9 sectioned books on the user's own
// shelf. `screen_contents.h` is where that matters: a depth is not by itself a level in
// a hierarchy, and reading it as one made those 1,635 rows unreachable.
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
// its header band, and what Contents needs to mark the row the reader is on -- ONE
// answer for one question, because two screens naming the reader's chapter differently
// is two spellings of one fact.
//
// --- THE FIRST MATCH, AND IT WAS THE LAST FOR TWO PHASES -----------------------
//
// A GROUP IS THE MAJORITY CASE, not an edge one. An NCX target is a file plus an
// optional FRAGMENT (`ch3.xhtml#part2`) and the reader positions by spine entry only,
// so several entries legitimately name one spine index. Measured over
// ~/.cache/encre-corpus: of the 206 books with a usable NCX, 109 (52.9%) have at least
// one spine entry named twice or more -- 605 such groups -- and the worst is
// standardebooks/f822606a92670aa1.epub, whose spine entry 2 is named by 378 navPoints.
//
// The rule was "the last match, because the later ones are further into the file, so
// the last is the closest thing to where you are". The premise is true; the conclusion
// needs the reader to be at the END of the file, which is not where they are. The
// fragment is STRIPPED before the match (see the resolver above), so every entry in a
// group resolves to that file's START and nothing on this path knows any offset within
// it -- the first entry is the only member that can be PROVED not to be ahead of the
// reader, since the reader is somewhere inside the file. Naming a landmark they have
// not reached is the error that misleads: `reading_position.h` grades the same trade
// the same way, degrading backwards ("the top of the right paragraph beats the front of
// the book, which beats nothing").
//
// It is also right at the one moment either rule can be checked. The Reader recomputes
// its label when a chapter OPENS and not as pages turn, and a chapter is entered at
// its first page by a jump and by a forward crossing -- where the first entry is
// exactly right and the last is exactly wrong.
//
// WHAT IT COSTS: a reader deep inside a 378-fragment file is named by that file's first
// fragment, which is stale rather than false. Closing that needs a fragment-to-block
// map, and `document.h` drops ids -- so it is not a tuning question.
//
// A CALLER WITH A DRAWING RULE OF ITS OWN LAYERS IT ON TOP: Contents will not put the
// marker on a section HEADER, because the board gives a header no value slot. That gate
// cannot live here -- a depth is a nesting level and not a role -- and it is pinned to
// this function by an equivalence in test_screen_contents.cpp.
int tocIndexForSpine(const std::vector<TocEntry>& toc, int spine);

}  // namespace reader
