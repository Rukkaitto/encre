#include "reader/screen_reader_menu.h"

#include "reader/theme.h"

namespace reader {

namespace {

// The board's six rows. `value` is the row's right slot where the board puts one and
// empty where it draws a chevron -- the same "a row states a quantity or discloses a
// screen, never both" rule the menu rows on Home follow.
//
// WHAT RESPONDS is the second flag, and it is a statement about what exists rather
// than about the design: Typography, Go to page and Bookmarks have boards and no
// screens, and About this book has a screen that is built from the LIBRARY's selection
// -- which a reader who arrived through Home's CONTINUE does not have. Each becomes
// focusable in the commit that gives it something to do.
struct Item {
  const char* label;
  const char* value;
  bool live;
  bool discloses;
  int trackingEm1000;
};
constexpr Item kItems[ReaderMenuScreen::kRowCount] = {
    {"Contents", "", true, true, 0},
    {"Typography", "", false, true, 0},
    {"Go to page\xE2\x80\xA6", "", false, true, 0},
    // The board shows `2`, a bookmark count. Zero would be a claim about a feature that
    // cannot make one, so the row carries the board's own value and does not act.
    {"Bookmarks", "2", false, false, 0},
    // The character index another branch boards as Names.dc.html. Drawn and inert like
    // its four unbuilt siblings -- it becomes focusable in the commit that gives it a
    // screen, and needs no change here when it does.
    {"Names", "", false, true, 0},
    // ABOUT THIS BOOK OPENS BOOK DETAILS, and it was inert because that screen used to be
    // built from the LIBRARY's focused row -- fine from the Library and wrong from a
    // Reader opened through Home's CONTINUE, where there is no Library on the stack. It
    // takes facts now, so both callers can answer it.
    {"About this book", "", true, true, 0},
    // NO MARK AND ITS OWN TRACKING, both of which the board states. It closes the book
    // rather than opening a screen, so a chevron would promise somewhere to go -- and
    // `letter-spacing: 0.06em` is the one row on this panel the board tracks.
    {"Close book", "", true, false, 60},
};

}  // namespace

ReaderMenuScreen::ReaderMenuScreen(std::string bookTitle, std::string progress)
    : FocusScreen(kRowCount, kRowCount) {
  vm_.bookTitle = std::move(bookTitle);
  vm_.progress = std::move(progress);
  vm_.rows.reserve(kRowCount);
  for (const Item& it : kItems)
    vm_.rows.push_back(
        ListRow{it.label, it.value, /*isHeader=*/false, it.discloses, it.trackingEm1000, it.live});
  // The board's own labels. CLOSE rather than BACK, because Back here dismisses a
  // panel rather than leaves a screen -- the actions overlay says the same for the
  // same reason. No holds, so no slot shows a ring.
  vm_.hints = {"CLOSE", "SELECT", "UP", "DOWN"};
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);
  // The base starts on row 0, which is Contents and is live -- but that is a fact
  // about kItems rather than a guarantee, so the focus is set through the gated path
  // and lands on the first row that can act whatever the table says.
  setFocus(0);
  syncVm();
}

void ReaderMenuScreen::syncVm() { vm_.focusedRow = focus(); }

bool ReaderMenuScreen::focusable(int index) const {
  if (index < 0 || index >= kRowCount) return false;
  return kItems[static_cast<size_t>(index)].live;
}

Action ReaderMenuScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    case Gesture::Next:
      return moveFocus(+1);
    case Gesture::Prev:
      return moveFocus(-1);
    case Gesture::Back:
      return Action::pop();
    case Gesture::Activate:
      switch (vm_.focusedRow) {
        case kContents:
          return Action::push(ScreenId::Contents);
        case kAboutBook:
          return Action::push(ScreenId::BookDetails);
        case kCloseBook:
          // CLOSE THE BOOK: this panel AND the Reader under it, in one action, because
          // a screen returns one Action and a Pop followed by a second Pop would be
          // this screen reaching into the stack. `popTo` is the primitive the delete
          // confirmation already needed for the same shape.
          //
          // THE TARGET IS THE LIBRARY, and its absence is handled by popTo's own
          // documented rule rather than by a branch here: "stops at the root if
          // `target` is not on the stack". A reader who opened the book from the
          // Library lands back on it; one who came through Home's CONTINUE has no
          // Library on the stack and lands on Home. Both are where they came from.
          return Action::popTo(ScreenId::Library);
        default:
          // An inert row cannot be focused, so this is unreachable by a press. It
          // answers none() rather than asserting, because a restored focus is the one
          // way a number could arrive here from outside -- and FocusScreen refuses an
          // unlandable restore for exactly that reason.
          return Action::none();
      }
    default:
      return Action::none();
  }
}

void ReaderMenuScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                             Plane plane) const {
  theme.renderReaderMenu(fb, fonts, vm_, plane);
}

}  // namespace reader
