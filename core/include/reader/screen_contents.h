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
// --- ITS ROWS ARE THE NCX's, AND ITS SECTIONS ARE ITS DEPTHS ------------------
//
// `toc.h` hands over a LINEAR list where each entry carries a `depth`. A depth-1 entry
// becomes a SECTION HEADER and everything deeper becomes a row, which is what draws
// the board's `BOOK I - MISS BROOKE` grouping. That mapping is the whole of the
// hierarchy: no tree, no traversal, and a screen that wants "the Nth visible row" gets
// it by indexing.
//
// A FLAT NCX HAS NO DEPTH-2 ENTRIES AT ALL, and two of the four books measured are
// flat. So the mapping cannot be "depth 1 is a header" unconditionally -- that would
// render a flat book as a list of headers and no rows, with nothing focusable and
// nothing to select. `sectioned()` decides once, from the list: only a book that
// HAS deeper entries gets headers.
//
// --- A SECTION HEADER IS ALSO A TARGET, AND IS STILL NOT FOCUSABLE -------------
//
// An NCX section header carries its own `content src`, so it names a real spine entry
// and jumping to it would work. It is drawn as a header anyway and skipped by the
// focus, because the board draws it as one: a tracked caps label with its own rule and
// no value, which is not a row a selection can sit on. What that costs is one
// unreachable target per section -- and its first child usually names the same spine
// entry, which is the row directly beneath it.
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
  // a flat NCX, where every entry is a row.
  bool sectioned() const { return sectioned_; }

  int rowCount() const { return static_cast<int>(entries_.size()); }

 protected:
  void syncVm() override;
  bool focusable(int index) const override;

 private:
  bool isHeaderAt(int index) const;

  std::vector<TocEntry> entries_;
  bool sectioned_ = false;
  ContentsViewModel vm_{};
  int spine_ = 0;
};

}  // namespace reader
