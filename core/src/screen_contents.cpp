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
  // HELD UP OR DOWN SCROLLS, accelerating, exactly as the Library's does -- a 96-entry
  // contents is long enough to need it, and a list that only steps one row a press is
  // ~570 ms a row on this panel. `moveFocus(±steps, held)` above already takes the
  // distance and the held flag, so this is the declaration that makes them arrive.
  //
  // Up and Down only, matching the Library. The SIDE buttons are movers now too, and a
  // held one on a list currently resolves as Long and is dropped -- worth deciding
  // deliberately for both screens rather than changing one of them here.
  declareRepeat(static_cast<ButtonMask>(buttonBit(Button::Up) | buttonBit(Button::Down)));

  // THE FIRST FOCUSABLE ROW, NOT ROW 0: row 0 is a section header in a book whose
  // contents open on a part rather than on front matter -- 5 of the corpus's 103
  // sectioned books.
  //
  // IT TAKES THE GATED WALK AND NOT A GATED SET, and this was `setFocus(0)` for two
  // phases behind a comment claiming that landed "on a row that can act whatever the
  // book's shape". It cannot: `Focus::set` documents itself as REFUSING a landing the
  // gate declines and restoring the index it had, and a freshly built `Focus` already
  // holds 0 -- so the refusal left the selection sitting on the header, which
  // `renderContents` draws with no inversion at all. A list with nothing visibly
  // selected, and a GO that jumps somewhere the reader was never shown.
  //
  // `Focus::move` is the overload that steps OVER a refused position without consuming
  // any of its distance, so one step from a refused row 0 lands on the first row that
  // can act. Guarded on the current position rather than run unconditionally, because
  // row 0 is focusable in every other book and a bare step would skip it.
  //
  // A book whose contents are ENTIRELY headers cannot exist -- a header is a header
  // only because something deeper follows it, and that something is a row -- so there
  // is no nothing-focusable case here beyond an empty contents, which `move` refuses on
  // its own count.
  if (!focusable(focus())) moveFocus(+1);
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
  if (!sectioned_) return false;
  const size_t i = static_cast<size_t>(index);
  if (entries_[i].depth > 1) return false;
  // A HEADER IS AN ENTRY THAT GROUPS OTHERS, and in a list walked in document order
  // that is exactly "the next entry sits deeper than this one" -- an NCX's children
  // immediately follow their parent, so the lookahead is the whole test.
  //
  // KEYING ON DEPTH ALONE WAS ISSUE #75. Every depth-1 entry in a sectioned book was a
  // header, so a top-level entry with nothing nested inside it -- an INTRODUCTION, a
  // FOREWORD, a NOTES -- was drawn as a tracked-caps label the focus skips, and there
  // was no way to reach the chapter at all. The header above this one recorded the cost
  // as "one unreachable target per section" and consoled itself that "its first child
  // usually names the same spine entry"; a CHILDLESS entry has no first child, so for
  // that shape the consolation is false and the row is simply gone.
  //
  // The shape is not an edge case. Measured over ~/.cache/encre-corpus: 98 of its 103
  // sectioned books carry at least one childless depth-1 entry, 1,635 rows in all, and
  // 9 of the 9 sectioned books on the user's own shelf do. It is what Standard Ebooks
  // emits for every book with parts (`Titlepage`, `Imprint`, `Colophon`, `Uncopyright`
  // beside a real `Part I`), and the worst case in the corpus loses 362 rows of 384.
  return i + 1 < entries_.size() && entries_[i + 1].depth > entries_[i].depth;
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
    // `NOW` on the chapter being read and NOTHING on the others. This slot has held two
    // wrong things: a page number (which needs the whole book paginated) and then the
    // spine position, which is free and true and read WORSE on a real book -- chapter
    // names carry their own numbering, so a row said `Chapitre 1.        CH. 09`, two
    // numbering systems side by side with neither explaining the other. The board says
    // so now.
    if (!row.isHeader && e.spine == spine_) row.value = "NOW";
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
