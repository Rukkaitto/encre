#include "reader/screen_reader_menu.h"

#include "reader/theme.h"

namespace reader {

namespace {

// The board's three rows. `value` is the row's right slot where the board puts one and
// empty where it draws a chevron -- the same "a row states a quantity or discloses a
// screen, never both" rule the menu rows on Home follow.
//
// WHAT RESPONDS is the third flag, and it is a statement about what exists rather than
// about the design. EVERY ROW HERE IS `true` NOW, and this table had the last `false`
// in the screen: Names had a board and no screen, and the screen it was waiting for
// turned out to be V2's rather than V1's, so it was cut (#73) instead of going live the
// way Typography's did. The flag stays because the rule is the screen's, not this
// table's -- the next unbuilt V1 row is drawn and skipped by setting one word.
//
// NEITHER A TRACKING COLUMN NOR A VALUE ONE. `Close book` was the only row on any panel
// in this firmware that the boards letter-spaced, and `Bookmarks` was the only one that
// stated a count; with both gone, `ListRow::trackingEm1000`, `ListRow::value` on THIS
// view-model and `drawPanelRow`'s two optional arguments have no producer here. The
// plumbing stays -- they are generic component parameters, Bookmarks (#3) is the board
// that asks for the value again, and `ListRow::value` is still driven by Settings,
// Contents and Typography through their own row primitives.
//
// THE DIFFERENCE BETWEEN THEM IS WHETHER ANYTHING EXERCISES THE DRAWING. Tracking is
// untested capability, which this file has said since `Close book` went. The value path
// is not: test_components.cpp drives `drawPanelRow`'s value slot directly, so what a
// board asks for again is behaviour that still works rather than a parameter nobody has
// run since its last caller left. That test is the deliberate answer to "does the value
// path keep a test or go" -- the pixels are cheaper to keep honest than to re-derive.
struct Item {
  const char* label;
  const char* value;
  bool live;
  bool discloses;
};
constexpr Item kItems[ReaderMenuScreen::kRowCount] = {
    {"Contents", "", true, true},
    {"Typography", "", true, true},
    // ABOUT THIS BOOK OPENS BOOK DETAILS, and it was inert because that screen used to be
    // built from the LIBRARY's focused row -- fine from the Library and wrong from a
    // Reader opened through Home's CONTINUE, where there is no Library on the stack. It
    // takes facts now, so both callers can answer it.
    //
    // THE LAST ROW, so the panel's own border closes the list and this one draws no
    // rule. `Close book` held that position and that job; the row that inherits the
    // position inherits the missing rule with it.
    {"About this book", "", true, true},
};

}  // namespace

ReaderMenuScreen::ReaderMenuScreen(std::string bookTitle, std::string progress)
    : FocusScreen(kRowCount, kRowCount) {
  vm_.bookTitle = std::move(bookTitle);
  vm_.progress = std::move(progress);
  vm_.rows.reserve(kRowCount);
  for (const Item& it : kItems)
    vm_.rows.push_back(ListRow{it.label, it.value, /*isHeader=*/false, it.discloses,
                               /*trackingEm1000=*/0, it.live});
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
        case kTypography:
          return Action::push(ScreenId::Typography);
        case kAboutBook:
          return Action::push(ScreenId::BookDetails);
        default:
          // EVERY ROW IS NAMED ABOVE NOW, so a press cannot reach this at all -- with
          // `Names` cut there is no inert row for the focus to be sitting on. What is
          // left is a focus OUT OF RANGE, and the one way a number arrives here from
          // outside is a restore: FocusScreen refuses an index it cannot land on, so
          // this answers none() rather than asserting, and it keeps working as the
          // landing pad if an unbuilt V1 row is ever drawn and skipped here again.
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
