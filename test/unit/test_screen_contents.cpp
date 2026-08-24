// The chapter list, and the jump.
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/screen_contents.h"
#include "reader/screen_reader_menu.h"
#include "reader/screens.h"

using reader::Action;
using reader::ContentsScreen;
using reader::ScreenId;
using reader::TocEntry;

namespace {

// InputEvents, not gestures: Screen::onEvent takes what the recognizer emits, and the
// gesture mapping is gestureFor's job (which test_gesture.cpp covers).
const reader::InputEvent kDown{reader::Button::Down, reader::PressKind::Short};
const reader::InputEvent kUp{reader::Button::Up, reader::PressKind::Short};
const reader::InputEvent kGo{reader::Button::Confirm, reader::PressKind::Short};
const reader::InputEvent kBack{reader::Button::Back, reader::PressKind::Short};

// Le Fleau's shape, reduced: section headers at depth 1 over chapters at depth 2.
std::vector<TocEntry> sectioned() {
  return {{0, 1, "PART ONE"}, {0, 2, "One"},      {1, 2, "Two"},
          {2, 1, "PART TWO"}, {2, 2, "Three"},    {3, 2, "Four"}};
}

// Both Dexter editions' shape: no depth at all.
std::vector<TocEntry> flat() {
  return {{0, 1, "One"}, {1, 1, "Two"}, {2, 1, "Three"}, {3, 1, "Four"}};
}

}  // namespace

TEST_CASE("a sectioned book draws headers; a flat one draws none") {
  // Measured: one of four real books is three levels deep and two are flat, so BOTH
  // shapes are the common case and neither is an edge. Treating depth 1 as a header
  // unconditionally would render a flat book as nothing but headers -- no focusable
  // row, nothing to select.
  ContentsScreen deep(sectioned(), "Le Fleau", 0, 8);
  CHECK(deep.sectioned());
  ContentsScreen plain(flat(), "Dexter", 0, 8);
  CHECK_FALSE(plain.sectioned());
  for (const reader::ListRow& r : plain.vm().rows) CHECK_FALSE(r.isHeader);
}

TEST_CASE("the focus starts on the chapter being read, not at the top") {
  // What a reader opening this screen is looking for.
  ContentsScreen s(sectioned(), "Le Fleau", 3, 8);
  CHECK(s.chosenSpine() == 3);
  CHECK(s.vm().rows[static_cast<size_t>(s.vm().focusedRow)].label == "Four");
}

TEST_CASE("a header cannot be focused, and the focus skips it") {
  ContentsScreen s(sectioned(), "Le Fleau", 0, 8);
  // Row 0 is `PART ONE`; the focus lands on row 1.
  CHECK(s.focus() == 1);
  // Stepping down from the last row of part one crosses the `PART TWO` header without
  // stopping on it.
  s.onEvent(kDown);
  CHECK(s.focus() == 2);
  s.onEvent(kDown);
  CHECK(s.focus() == 4);  // 3 is the header
  CHECK(s.chosenSpine() == 2);
}

TEST_CASE("a spine the contents do not mention lands as near as the list allows") {
  // `set` CLAMPS where `move` wraps, so a chapter with no entry does not throw the
  // focus to the far end of the list.
  ContentsScreen s(sectioned(), "Le Fleau", 99, 8);
  CHECK(s.focus() >= 0);
  CHECK(s.chosenSpine() >= 0);
}

TEST_CASE("GO pops back to the Reader and names the chapter") {
  // It does NOT push a Reader: one is already on the stack under the menu this was
  // opened from, and a second would leave the first below it with its own position.
  ContentsScreen s(sectioned(), "Le Fleau", 0, 8);
  const Action a = s.onEvent(kGo);
  CHECK(a.kind == Action::Kind::PopTo);
  CHECK(a.target == ScreenId::Reader);
  CHECK(s.chosenSpine() == 0);
}

TEST_CASE("an empty contents offers nothing to go to and stays put") {
  // A book with no NCX. Dismissing the screen on a press that achieved nothing would
  // be worse than leaving it up.
  ContentsScreen s({}, "No contents", 0, 8);
  CHECK(s.rowCount() == 0);
  CHECK(s.chosenSpine() == -1);
  CHECK(s.onEvent(kGo).kind == Action::Kind::None);
  CHECK(s.onEvent(kBack).kind == Action::Kind::Pop);
}

TEST_CASE("A SECTIONED BOOK ALWAYS HAS A FOCUSABLE ROW, by construction") {
  // Written first as "a contents of only headers refuses GO" -- and that state cannot
  // exist. `sectioned()` is true only when some entry is deeper than 1, and every such
  // entry is a ROW; a book with no deeper entry is flat, and then nothing is a header
  // at all. So either shape has something to select, which is why neither the screen
  // nor the theme needs a nothing-focusable branch.
  //
  // Checked over both shapes rather than argued, because the argument is the kind that
  // has been wrong here before.
  for (const std::vector<TocEntry>& toc : {sectioned(), flat()}) {
    ContentsScreen s(toc, "Book", 0, 8);
    int focusable = 0;
    for (const reader::ListRow& r : s.vm().rows)
      if (!r.isHeader) ++focusable;
    CHECK(focusable > 0);
    CHECK(s.focus() >= 0);
    CHECK(s.chosenSpine() >= 0);
    CHECK(s.onEvent(kGo).kind == Action::Kind::PopTo);
  }
  // An EMPTY contents is the only nothing-to-select case, and it is covered above.
}

TEST_CASE("the visible slice never names a row that was not drawn") {
  // ScrollWindow::slice's contract, and the reason the view model holds a slice rather
  // than the whole book: a 96-entry contents would otherwise hand the theme every row.
  std::vector<TocEntry> many;
  for (int i = 0; i < 40; ++i) many.push_back({i, 1, "Chapter " + std::to_string(i)});
  ContentsScreen s(many, "Long", 0, 7);
  CHECK(static_cast<int>(s.vm().rows.size()) <= 7);
  CHECK(s.vm().scrollable);
  CHECK(s.vm().scrollTotal == 40);
  CHECK(s.vm().focusedRow >= 0);
  CHECK(s.vm().focusedRow < static_cast<int>(s.vm().rows.size()));
}

TEST_CASE("a list told no row count renders empty rather than guessing") {
  // The Library's own rule -- the shell must set it before the first paint.
  ContentsScreen s(sectioned(), "Le Fleau", 0, 0);
  CHECK(s.vm().rows.empty());
}

TEST_CASE("the row being read is the only one marked NOW") {
  ContentsScreen s(flat(), "Dexter", 2, 8);
  int marked = 0;
  for (const reader::ListRow& r : s.vm().rows)
    if (r.value.find("NOW") != std::string::npos) ++marked;
  CHECK(marked == 1);
}

// --- The menu ----------------------------------------------------------------

TEST_CASE("the reader menu focuses Contents, skipping the rows that do nothing") {
  reader::ReaderMenuScreen m("Middlemarch", "6%");
  CHECK(m.id() == ScreenId::ReaderMenu);
  CHECK(m.isOverlay());
  CHECK(m.focus() == reader::ReaderMenuScreen::kContents);
  CHECK(m.onEvent(kGo).kind == Action::Kind::Push);
  // Down from Contents skips Typography, Go to page and Bookmarks and About this book,
  // landing on Close book -- four inert rows in a row, which is the case a naive skip
  // walk gets wrong.
  m.onEvent(kDown);
  CHECK(m.focus() == reader::ReaderMenuScreen::kCloseBook);
  // ...and wraps back round to Contents rather than sticking.
  m.onEvent(kDown);
  CHECK(m.focus() == reader::ReaderMenuScreen::kContents);
}

TEST_CASE("Close book closes the book AND the panel over it") {
  reader::ReaderMenuScreen m("Middlemarch", "6%");
  m.onEvent(kDown);
  REQUIRE(m.focus() == reader::ReaderMenuScreen::kCloseBook);
  const Action a = m.onEvent(kGo);
  // popTo, not pop: a screen returns ONE action, and a Pop followed by a second Pop
  // would be this screen reaching into the stack.
  CHECK(a.kind == Action::Kind::PopTo);
  CHECK(a.target == ScreenId::Library);
}

TEST_CASE("Close book draws no chevron, and the board says so") {
  // It acts in place. Deriving `discloses` from an empty value drew a chevron promising
  // a screen that does not exist -- and `Bookmarks` is the other half of the same rule,
  // a value where its siblings have marks.
  const reader::ReaderMenuScreen m("Middlemarch", "6%");
  const auto& rows = m.vm().rows;
  REQUIRE(rows.size() == reader::ReaderMenuScreen::kRowCount);
  CHECK_FALSE(rows[reader::ReaderMenuScreen::kCloseBook].discloses);
  CHECK(rows[reader::ReaderMenuScreen::kCloseBook].value.empty());
  CHECK(rows[reader::ReaderMenuScreen::kContents].discloses);
  CHECK_FALSE(rows[reader::ReaderMenuScreen::kBookmarks].discloses);
  CHECK(rows[reader::ReaderMenuScreen::kBookmarks].value == "2");
  // The one row the board tracks.
  CHECK(rows[reader::ReaderMenuScreen::kCloseBook].trackingEm1000 > 0);
}

TEST_CASE("the menu's panel never changes height, so every focus move is partial") {
  // Unlike the actions panel, whose focused row loses its rule and makes the panel a
  // pixel shorter -- so two of its four focus moves refuse the fast path. All six rows
  // here are one height, which is what makes a constant footprint a true promise.
  reader::ReaderMenuScreen m("Middlemarch", "6%");
  const uint32_t before = m.paintFootprint();
  m.onEvent(kDown);
  CHECK(m.paintFootprint() == before);
  CHECK(before != 0);  // zero is "no promise" and would refuse the fast path
}
