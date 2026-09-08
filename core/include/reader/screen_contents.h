#pragma once
#include <string>
#include <vector>

#include "reader/focus_screen.h"
#include "reader/toc.h"
#include "reader/viewmodel.h"

namespace reader {

// The book's chapters, and a jump to one (design/Contents.dc.html).
//
// A FULL SCREEN, not an overlay: it is a list you read and scroll, not a question
// about the page behind it. Book details makes the same call for the same reason.
//
// --- ITS ROWS ARE THE NCX's, AND A HEADER IS AN ENTRY THAT GROUPS OTHERS -------
//
// `toc.h` hands over a LINEAR list where each entry carries a `depth`. A top-level
// entry that has entries NESTED INSIDE IT becomes a SECTION HEADER and everything else
// becomes a row, which is what draws the board's `BOOK I - MISS BROOKE` grouping. That
// mapping is the whole of the hierarchy: no tree, no traversal, and a screen that wants
// "the Nth visible row" gets it by indexing. An NCX's children immediately follow their
// parent in document order, so "does it group others" is one lookahead --
// `entries[i + 1].depth > entries[i].depth`.
//
// TWO SHAPES FORCE THAT MAPPING AND EACH ONE BROKE THE SIMPLER ONE:
//
//  - A FLAT NCX HAS NO DEPTH-2 ENTRIES AT ALL, and two of the first four books measured
//    are flat. So the mapping cannot be "depth 1 is a header" unconditionally -- that
//    renders a flat book as a list of headers and no rows, with nothing focusable and
//    nothing to select. `sectioned()` decides once, from the list: only a book that HAS
//    deeper entries gets headers at all.
//  - A CHILDLESS TOP-LEVEL ENTRY IS A CHAPTER, NOT A SECTION, and that is ISSUE #75:
//    reported off glass as "the chapter labelled INTRODUCTION shows up in the contents
//    but cannot be selected". Front and back matter sit at depth 1 beside the parts
//    that really do group chapters, so keying on depth alone drew `Introduction`,
//    `Foreword` and `Notes` as tracked-caps labels the focus skips, with no way to
//    reach those chapters at all. Measured over ~/.cache/encre-corpus: 98 of its 103
//    sectioned books carry at least one, 1,635 rows in total, 9 of the user's own 9
//    sectioned books, and the worst case loses 362 rows of 384. It is what Standard
//    Ebooks emits for every book with parts.
//
// --- A SECTION HEADER IS ALSO A TARGET, AND IS STILL NOT FOCUSABLE -------------
//
// An NCX section header carries its own `content src`, so it names a real spine entry
// and jumping to it would work. It is drawn as a header anyway and skipped by the
// focus, because the board draws it as one: a tracked caps label with its own rule and
// no value, which is not a row a selection can sit on. What that costs is one
// unreachable target per SECTION -- and a section, by the lookahead above, always has a
// first child naming the row directly beneath it. That consolation is what was false
// while a childless entry counted as a section: a childless entry HAS no first child,
// so the target was not approximated by a neighbouring row, it was gone.
//
// --- THE INVARIANT, STRENGTHENED BY THAT FIX -----------------------------------
//
// A SECTIONED BOOK ALWAYS HAS A FOCUSABLE ROW, and it is now structural rather than an
// argument about depth-2 entries: a header exists ONLY because something deeper follows
// it, and that something is a row. So every header is immediately followed by a row,
// which is stronger than the old form ("`sectioned()` requires a depth-2 entry and
// every such entry is a row") and rules out the same state. The only nothing-to-select
// case remains an EMPTY contents -- a book with no NCX.
//
// --- `NOW` MARKS AT MOST ONE ROW, AND IT IS DECIDED ONCE ------------------------
//
// Reported off an X3 on `Discourse on the Method`: TWO rows read `NOW`. The rule was
// `!row.isHeader && e.spine == spine_` per row, so every entry naming the open spine
// entry got the marker -- and a group of entries on one spine entry is the MAJORITY
// case, since an NCX target is a file plus an optional fragment while the reader
// positions by spine entry only (109 of the corpus's 206 books with a usable NCX, and
// 373 rows at once in the worst of them). `NOW` is a claim about where the reader is,
// so more than one of them is a false claim -- the shape this project refuses for an
// unread gauge (-1, never 0%) and for a badge promising a wake charging cannot deliver.
//
// The ROWS are kept: their labels are real content and `toc.h` says so. Only the
// marker is single, and it goes on `rowForSpine()` -- `tocIndexForSpine`'s first match,
// stepped past a header, since the board gives a header no value slot.
//
// IT IS COMPUTED ONCE, IN THE CONSTRUCTOR, OVER `entries_`, and that is structural
// rather than an optimisation. `syncVm` walks the VISIBLE SLICE, so a rule evaluated
// inside that loop would answer "the first match ON SCREEN": the marker would hop
// between members of the group as the list scrolled, and would land on a row that is
// not the reader's as soon as the real one scrolled out of the window. That is worse
// than the defect above and no single-screenful test can see it. An absolute index
// compared against `s.first + i` cannot have the question. The inputs are immutable
// after construction -- `entries_` and `spine_` have no setters -- so there is nothing
// to invalidate.
class ContentsScreen : public FocusScreen {
 public:
  // `toc` is the book's whole table of contents and `spine` is the entry being read,
  // which is what marks a row `NOW`. `visibleRows` comes from the theme, as the
  // Library's does -- a row count depends on a panel height and `core/` cannot ask.
  ContentsScreen(std::vector<TocEntry> toc, std::string bookTitle, int spine, int visibleRows);

  ScreenId id() const override { return ScreenId::Contents; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const ContentsViewModel& vm() const { return vm_; }

  // The spine entry the focused row names, or -1 when nothing is selectable. What the
  // shell reads after a Select to know where to send the Reader.
  int chosenSpine() const;

  // Whether this book's contents have a hierarchy worth drawing as sections. False for
  // a flat NCX, where every entry is a row. TRUE IS NOT "EVERY TOP-LEVEL ENTRY IS A
  // HEADER": a top-level entry is a header only where it groups others -- see the
  // header comment and `isHeaderAt`.
  bool sectioned() const { return sectioned_; }

  int rowCount() const { return static_cast<int>(entries_.size()); }

  // THE ONE ROW MARKED `NOW`, as an index into the whole list, or -1 for none. See the
  // paragraph above and `rowForSpine`.
  int nowRow() const { return nowRow_; }

 protected:
  void syncVm() override;
  bool focusable(int index) const override;

 private:
  bool isHeaderAt(int index) const;
  int rowForSpine() const;

  std::vector<TocEntry> entries_;
  bool sectioned_ = false;
  ContentsViewModel vm_{};
  int spine_ = 0;
  int nowRow_ = -1;
};

}  // namespace reader
