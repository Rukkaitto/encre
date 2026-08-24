#include "reader/screen_contents.h"

#include <cstdio>

#include "reader/theme.h"

namespace reader {

namespace {

// Does this table of contents have a hierarchy, or is it one flat list?
//
// Measured: two of four real books are flat and one is three levels deep. So this is
// a property of the BOOK, decided once, and not a rule applied per entry -- treating
// depth 1 as a header unconditionally would render a flat book as nothing but headers,
// with no focusable row and nothing to select.
bool hasDeeperEntries(const std::vector<TocEntry>& toc) {
  for (const TocEntry& e : toc)
    if (e.depth > 1) return true;
  return false;
}

}  // namespace

ContentsScreen::ContentsScreen(std::vector<TocEntry> toc, std::string bookTitle, int spine,
                               int visibleRows)
    : FocusScreen(static_cast<int>(toc.size()), visibleRows),
      entries_(std::move(toc)),
      sectioned_(hasDeeperEntries(entries_)),
      spine_(spine) {
  vm_.title = "CONTENTS";
  vm_.bookTitle = std::move(bookTitle);
  // The board's own labels. BACK because this is a screen to leave, and GO rather than
  // SELECT because what Confirm does here is travel -- the board says so.
  vm_.hints = {"BACK", "GO", "UP", "DOWN"};
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);

  // THE FIRST FOCUSABLE ROW, NOT ROW 0: row 0 is a section header in a sectioned book.
  // Set through the gated path so it lands on a row that can act whatever the book's
  // shape -- including a book whose contents are entirely headers, where nothing can
  // and the screen renders readable rather than aborting.
  setFocus(0);
  // AND THEN ONTO THE CHAPTER BEING READ, which is what a reader opening this screen
  // is looking for. Set, not moved: `set` CLAMPS where `move` wraps, and a spine the
  // contents do not mention should land as near as the list allows rather than at the
  // far end of it.
  const int here = tocIndexForSpine(entries_, spine_);
  if (here >= 0) setFocus(here);
  syncVm();
}

bool ContentsScreen::isHeaderAt(int index) const {
  if (index < 0 || index >= rowCount()) return false;
  return sectioned_ && entries_[static_cast<size_t>(index)].depth <= 1;
}

bool ContentsScreen::focusable(int index) const { return index >= 0 && index < rowCount() &&
                                                         !isHeaderAt(index); }

int ContentsScreen::chosenSpine() const {
  const int f = focus();
  if (f < 0 || f >= rowCount()) return -1;
  return entries_[static_cast<size_t>(f)].spine;
}

void ContentsScreen::syncVm() {
  // THE VISIBLE SLICE, never the whole book -- `ScrollWindow::slice` answers
  // first/count/focused-in-slice-or-minus-one, so the view model cannot name a row
  // that was not drawn. A 96-entry contents would otherwise hand the theme every row.
  const ScrollWindow::Slice s = window().slice();
  vm_.rows.clear();
  vm_.rows.reserve(static_cast<size_t>(s.count));
  for (int i = 0; i < s.count; ++i) {
    const int at = s.first + i;
    if (at < 0 || at >= rowCount()) continue;
    const TocEntry& e = entries_[static_cast<size_t>(at)];
    ListRow row;
    row.label = e.label;
    row.isHeader = isHeaderAt(at);
    row.focusable = !row.isHeader;
    // `CH. 03`, and `CH. 03 - NOW` on the chapter being read. The spine POSITION, not
    // a page number: a page number for a place in the book needs every chapter
    // paginated, which is ~49 s of decode on this device. design/Contents.dc.html
    // states the swap and why.
    if (!row.isHeader) {
      char buf[24];
      if (e.spine == spine_)
        std::snprintf(buf, sizeof(buf), "CH. %02d \xC2\xB7" " NOW", e.spine + 1);
      else
        std::snprintf(buf, sizeof(buf), "CH. %02d", e.spine + 1);
      row.value = buf;
    }
    vm_.rows.push_back(std::move(row));
  }
  vm_.focusedRow = s.focused;
  vm_.scrollable = rowCount() > s.count;
  vm_.scrollFirst = s.first;
  vm_.scrollCount = s.count;
  vm_.scrollTotal = rowCount();
}

Action ContentsScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    case Gesture::Next:
      return moveFocus(+g.steps, g.held);
    case Gesture::Prev:
      return moveFocus(-g.steps, g.held);
    case Gesture::Back:
      return Action::pop();
    case Gesture::Activate:
      // GO POPS BACK TO THE READER and the shell moves it, rather than this screen
      // pushing a Reader of its own: the Reader is already on the stack under the menu
      // this was opened from, and pushing a second one would leave the first below it
      // with its own position. `chosenSpine()` is what the shell reads.
      //
      // Nothing focusable means nothing to go to -- a contents of only headers, or an
      // empty one -- and answering none() leaves the screen up rather than dismissing
      // it on a press that achieved nothing.
      if (chosenSpine() < 0) return Action::none();
      return Action::popTo(ScreenId::Reader);
    default:
      return Action::none();
  }
}

void ContentsScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                            Plane plane) const {
  theme.renderContents(fb, fonts, vm_, plane);
}

}  // namespace reader
