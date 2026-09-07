// The chapter list, and the jump.
#include <string>
#include <vector>

#include "doctest.h"
#include "ramp.h"
#include "reader/framebuffer.h"
#include "reader/theme_quiet.h"
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

TEST_CASE("the reader menu focuses Contents, and now nothing is skipped on the way down") {
  reader::ReaderMenuScreen m("Middlemarch", "6%");
  CHECK(m.id() == ScreenId::ReaderMenu);
  CHECK(m.isOverlay());
  CHECK(m.focus() == reader::ReaderMenuScreen::kContents);
  CHECK(m.onEvent(kGo).kind == Action::Kind::Push);
  // Down from Contents reaches Typography, which is the row next to it and is live.
  m.onEvent(kDown);
  CHECK(m.focus() == reader::ReaderMenuScreen::kTypography);
  // Down from Typography reaches About this book with NO row skipped between them.
  // `Names` sat there and was skipped, and is cut (#73) -- so the gated walk and the
  // ungated arithmetic now agree on this screen, which is why the wrap cases below are
  // the only interesting movement left here.
  m.onEvent(kDown);
  CHECK(m.focus() == reader::ReaderMenuScreen::kAboutBook);
  // ...and wraps back round to Contents rather than sticking. About this book is the
  // LAST row, so this is also the wrap-from-the-end case that `Close book` used to
  // stand in for.
  m.onEvent(kDown);
  CHECK(m.focus() == reader::ReaderMenuScreen::kContents);
  // Up from Contents wraps the other way, to the same last row.
  m.onEvent(kUp);
  CHECK(m.focus() == reader::ReaderMenuScreen::kAboutBook);
  // AND THE WALK IS EXHAUSTIVE, which is the property the three CHECKs above are only
  // a sample of: every index is landable, so stepping kRowCount times returns the focus
  // to where it started whatever the table's length becomes.
  const int start = m.focus();
  for (int i = 0; i < reader::ReaderMenuScreen::kRowCount; ++i) m.onEvent(kDown);
  CHECK(m.focus() == start);
}

TEST_CASE("the menu's Typography row opens the panel") {
  // ONE OF TWO DOORS. Settings has the other -- separate rows on separate screens
  // pushing the same ScreenId -- and each gets its own test, because a door that
  // opens the wrong screen or nothing is the defect both of these rows have shipped
  // before: this one answered none() behind a comment saying the screen did not
  // exist, and stayed that way after it did.
  reader::ReaderMenuScreen m("Middlemarch", "6%");
  // Contents is row 0 and is live, so one Down reaches Typography.
  m.onEvent(kDown);
  REQUIRE(m.focus() == reader::ReaderMenuScreen::kTypography);
  const Action a = m.onEvent(kGo);
  CHECK(a.kind == Action::Kind::Push);
  CHECK(a.target == ScreenId::Typography);
}

TEST_CASE("every row on this sheet responds, and the flag still means only input") {
  // `ListRow::focusable` IS ABOUT INPUT, NOT APPEARANCE. An inert row is drawn exactly
  // as an unfocused live one -- a theme that dimmed on the flag would be inventing a
  // design decision nobody made. Pinned here as well as in the goldens, because a
  // golden says the pixels are the same and this says which field is allowed to differ.
  const reader::ReaderMenuScreen m("Middlemarch", "6%");
  const auto& rows = m.vm().rows;
  REQUIRE(rows.size() == reader::ReaderMenuScreen::kRowCount);
  CHECK(rows[reader::ReaderMenuScreen::kTypography].label == "Typography");
  // The board draws it with a chevron and no value, unchanged by going live.
  CHECK(rows[reader::ReaderMenuScreen::kTypography].value.empty());
  CHECK(rows[reader::ReaderMenuScreen::kTypography].discloses);
  // NO INERT ROW IS LEFT. Bookmarks and Names were the last two, and both were cut for
  // one reason: skipping the focus keeps an unbuilt row from misleading a press and
  // does NOT keep it from promising a feature the release does not have -- and both
  // rows' screens are out of V1. Asserted as the PROPERTY rather than as a count of
  // inert rows, which is what a screen with none of them can actually say; the count
  // this file used to pin has been 1 and 2 and would be 0 now, and the reader-menu
  // comments have already been wrong about a row count twice.
  for (const auto& r : rows) CHECK(r.focusable);
  // ...and the two halves agree: the screen's own landing gate says the same thing the
  // view model does, for every index, so a row cannot be drawn live and refuse a press.
  for (int i = 0; i < reader::ReaderMenuScreen::kRowCount; ++i) {
    reader::ReaderMenuScreen n("Middlemarch", "6%");
    n.setFocus(i);
    CHECK(n.focus() == i);
  }
}

TEST_CASE("the menu draws its enum and nothing else, and no cut row came back") {
  // GO TO PAGE AND CLOSE BOOK ARE GONE, for reasons that are about reading rather than
  // about room: a reflowable book has no stable page to go to -- the number a picker
  // would offer moves with the type size -- so the honest jump is the chapter name,
  // which Contents gives. And Back from the page already closes the book, so that row
  // was a second door to a room with one, and had to carry its own save edge to stay
  // correct.
  //
  // BOOKMARKS AND NAMES ARE THE OTHER TWO, and both went with a scope call rather than
  // a design one: Bookmarks moved to V1.1 (#3) and the whole Names family to V2 (#73).
  // A row that discloses a screen this release does not have is a control that cannot
  // act -- which this project has shipped twice and refuses again. Both come back with
  // their screens.
  const reader::ReaderMenuScreen m("Middlemarch", "6%");
  const auto& rows = m.vm().rows;
  // THE PROPERTY, NOT A MAGIC NUMBER: the drawn rows, the declared count and the enum's
  // last member are three spellings of one length, and this asserts they agree rather
  // than pinning what they agree ON. The count in this screen's own comments has been
  // wrong twice, and re-pinning `== 4` here is what made a shrunk table a two-line edit
  // in a file that says nothing about which row went.
  CHECK(rows.size() == static_cast<size_t>(reader::ReaderMenuScreen::kRowCount));
  CHECK(reader::ReaderMenuScreen::kAboutBook + 1 == reader::ReaderMenuScreen::kRowCount);
  for (const auto& r : rows) {
    CHECK(r.label.find("Go to page") == std::string::npos);
    CHECK(r.label.find("Close book") == std::string::npos);
    CHECK(r.label.find("Bookmarks") == std::string::npos);
    CHECK(r.label.find("Names") == std::string::npos);
  }
  // The board's order, first and last, so the enum cannot be reordered silently.
  CHECK(rows.front().label == "Contents");
  CHECK(rows[reader::ReaderMenuScreen::kAboutBook].label == "About this book");
  CHECK(rows.back().label == "About this book");
}

TEST_CASE("About this book opens Book details") {
  // It was inert, because Book details was built from the LIBRARY's focused row -- fine
  // from the Library and wrong from a Reader opened through Home's CONTINUE, where there
  // is no Library on the stack. Making it focusable without fixing that would have been
  // a button that works only sometimes, which nobody can learn.
  reader::ReaderMenuScreen m("Middlemarch", "6%");
  // TWO Downs, not one: Typography sits between Contents and here and is live. It is
  // also exactly two now that `Names` is cut -- it was two before as well, because the
  // gate skipped it, so this is the one movement the cut left numerically unchanged.
  m.onEvent(kDown);
  m.onEvent(kDown);
  REQUIRE(m.focus() == reader::ReaderMenuScreen::kAboutBook);
  const Action a = m.onEvent(kGo);
  CHECK(a.kind == Action::Kind::Push);
  CHECK(a.target == ScreenId::BookDetails);
}

TEST_CASE("the menu's only way out is CLOSE, and it dismisses the panel not the book") {
  // `Close book` used to pop the Reader from under this overlay. With it gone the menu
  // returns a plain Pop for Back and nothing else leaves a screen -- the Reader's own
  // Back closes the book, which is where the `leaving` save already fires.
  reader::ReaderMenuScreen m("Middlemarch", "6%");
  const Action a = m.onEvent(kBack);
  CHECK(a.kind == Action::Kind::Pop);
  // No row answers with a PopTo any more. Walked rather than indexed, so it covers
  // exactly the rows a reader can actually reach -- every live row, once round.
  reader::ReaderMenuScreen n("Middlemarch", "6%");
  const int first = n.focus();
  int seen = 0;
  do {
    CHECK(n.onEvent(kGo).kind == Action::Kind::Push);
    ++seen;
    n.onEvent(kDown);
  } while (n.focus() != first && seen < reader::ReaderMenuScreen::kRowCount);
  // AS MANY PRESSES AS THERE ARE LANDABLE ROWS, derived rather than pinned: the walk
  // has to visit every row a reader can reach and stop, and counting the focusable rows
  // is what says that whatever the table's length is. `== 3` was the number when this
  // sheet had a skipped row; with `Names` cut it is also kRowCount, and hardcoding
  // either would have hidden a walk that stopped one row short.
  size_t landable = 0;
  for (const auto& r : n.vm().rows)
    if (r.focusable) ++landable;
  CHECK(static_cast<size_t>(seen) == landable);
}

TEST_CASE("no menu row states a quantity, and every one of them discloses") {
  // `discloses` CANNOT BE DERIVED FROM AN EMPTY VALUE, and deriving it drew a chevron
  // promising a screen that does not exist. `Bookmarks` was the surviving instance of
  // the other half -- a value where its siblings have marks -- and with it cut this
  // sheet states no quantity at all, so every row here now carries a chevron.
  const reader::ReaderMenuScreen m("Middlemarch", "6%");
  const auto& rows = m.vm().rows;
  REQUIRE(rows.size() == reader::ReaderMenuScreen::kRowCount);
  for (const auto& r : rows) {
    CHECK(r.discloses);
    CHECK(r.value.empty());
  }
  // NEITHER TRACKED NOR VALUED NOW. `Close book` was the only row on any panel the
  // boards letter-spaced and `Bookmarks` the only one that counted anything, so this
  // asserts BOTH absences rather than leaving the reader of this file to assume either
  // path still has a producer here. The value path is pinned at the primitive instead
  // -- test_components.cpp, "a panel row states a quantity or discloses a screen" --
  // which is the difference between capability that still works and capability nobody
  // runs.
  for (const auto& r : rows) CHECK(r.trackingEm1000 == 0);
}

TEST_CASE("the menu promises a constant paint footprint on every focus move") {
  // WHAT THIS PINS IS THE PROMISE, NOT THE PIXELS, and the distinction is #68: the rows
  // are all one height, but `renderReaderMenu` sizes the panel through
  // `rowRuleFor`, which suppresses the rule for the focused row AND for the last row --
  // so focusing the LAST row is the one state where the centred panel moves a pixel
  // (measured on the X3: top 213, 213, 212). `ItemActions::paintFootprint` counts
  // borderless rows for exactly that and this does not, so the promise is currently
  // stronger than the render. Cutting `Names` (#73) did not touch it either way: that
  // row was never focusable and never last, so the focusable set and every per-state
  // delta are unchanged and only the panel's overall height moved, by one row.
  reader::ReaderMenuScreen m("Middlemarch", "6%");
  const uint32_t before = m.paintFootprint();
  m.onEvent(kDown);
  CHECK(m.paintFootprint() == before);
  CHECK(before != 0);  // zero is "no promise" and would refuse the fast path
}

// --- The factory refuses rather than substituting ------------------------------

TEST_CASE("a Contents nothing primed is REFUSED, not filled with the board's own") {
  // THIS IS THE BUG THAT SHIPPED. The factory fell back to demoContents() whenever
  // nothing had set a real one, so a book whose table of contents failed to LOAD showed
  // Middlemarch's chapters -- and the load had failed for a diagnosable reason (a second
  // 32 KB inflate window against a 45,840-byte heap floor) that the substitution hid
  // completely.
  //
  // "A factory that substitutes content is worse than one that refuses" was already
  // written down for the Reader, one screen earlier.
  reader::DemoScreenFactory f;
  f.setContentsVisibleRows(8);
  CHECK(f.create(ScreenId::Contents) == nullptr);
  CHECK(f.create(ScreenId::ReaderMenu) == nullptr);
}

TEST_CASE("the demo is built when it is ASKED for") {
  // What the simulator and the goldens do, exactly as they call setReaderDemo.
  reader::DemoScreenFactory f;
  f.setContentsVisibleRows(8);
  f.setContentsDemo();
  auto contents = f.create(ScreenId::Contents);
  REQUIRE(contents != nullptr);
  CHECK(static_cast<ContentsScreen*>(contents.get())->rowCount() > 0);
  CHECK(f.create(ScreenId::ReaderMenu) != nullptr);
}

TEST_CASE("a real book with NO table of contents still builds, and builds empty") {
  // The distinction the refusal has to preserve: "nothing was primed" is a shell bug,
  // and "this book has no NCX" is a book that reads perfectly well and cannot name its
  // chapters. The second must render honestly rather than be refused.
  reader::DemoScreenFactory f;
  f.setContentsVisibleRows(8);
  f.setContents({}, 0);
  auto contents = f.create(ScreenId::Contents);
  REQUIRE(contents != nullptr);
  CHECK(static_cast<ContentsScreen*>(contents.get())->rowCount() == 0);
}

TEST_CASE("a real book's contents are the ones built, never the demo's") {
  reader::DemoScreenFactory f;
  f.setContentsVisibleRows(8);
  f.setContents(sectioned(), 2);
  auto scr = f.create(ScreenId::Contents);
  REQUIRE(scr != nullptr);
  const auto& c = *static_cast<ContentsScreen*>(scr.get());
  CHECK(c.rowCount() == static_cast<int>(sectioned().size()));
  // The title comes from the opened book, which this fixture has none of -- what is
  // pinned here is that the ROWS are the real ones.
  // ...and the demo's first label is nowhere in it.
  for (const reader::ListRow& r : c.vm().rows) CHECK(r.label.find("Miss Brooke") == std::string::npos);
}

TEST_CASE("A LONG CHAPTER NAME ELIDES INSIDE THE ROW") {
  // The device showed names running off the panel. drawDetailRow drew the label at full
  // length from the left margin -- Book details' labels are field names and never
  // overflowed, so it only surfaced when real chapter names went through it.
  //
  // Asserted on the PIXELS, because the elision is the primitive's and a view-model
  // check would pass on a label the renderer then overflows.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  const char* const kLong =
      "PREMI\xC3\x88RE PARTIE : \xC3\x80 LIRE AVANT L'ACHAT ET AUSSI APR\xC3\x88S, "
      "UN TITRE QUI NE FINIT JAMAIS";
  for (const int w : {480, 528}) {
    const int h = w == 480 ? 800 : 792;
    // The long name on an UNFOCUSED row: the focused one is full-bleed and its own
    // fill would mask an overflow rather than reveal it.
    ContentsScreen s({{0, 1, "Short"}, {1, 1, kLong}}, "Book", 0, 8);
    reader::Framebuffer fb(w, h);
    theme.renderContents(fb, ramp.fonts, s.vm(), reader::Plane::Bw);
    // NO TEXT IS INKED IN THE RIGHT MARGIN. kMargin is the board's 24px, so the last
    // column a label may touch is w - 24 - 1.
    //
    // THE FOCUSED ROW'S FILL IS NOT AN OVERFLOW, and this test first reported it as
    // one: that row is FULL-BLEED inverted, so it legitimately inks x=0 to x=w-1. The
    // discriminator is x=0 -- a full-bleed fill inks it and an overrunning label never
    // reaches it, since every label starts at kMargin.
    for (int y = 0; y < h; ++y) {
      if (!fb.getPixel(0, y)) continue;  // a full-bleed focused row, ink to both edges
      for (int x = w - 24; x < w; ++x)
        if (!fb.getPixel(x, y)) {
          CHECK_MESSAGE(false, "label ink in the right margin at " << x << "," << y << " on " << w);
          y = h;
          break;
        }
    }
  }
}

TEST_CASE("held UP or DOWN scrolls, as the Library's does") {
  // A 96-entry contents needs it: one row a press is ~570 ms of panel a row.
  ContentsScreen s(flat(), "Dexter", 0, 8);
  const reader::ButtonMask repeats = s.autoRepeat();
  CHECK((repeats & reader::buttonBit(reader::Button::Down)) != 0);
  CHECK((repeats & reader::buttonBit(reader::Button::Up)) != 0);
  // A repeat carries a DISTANCE, and the screen must spend it rather than step once --
  // a paint blocks the loop for ~570 ms, so one event stands for all the time the panel
  // was busy.
  std::vector<TocEntry> many;
  for (int i = 0; i < 40; ++i) many.push_back({i, 1, "Chapter " + std::to_string(i)});
  ContentsScreen big(many, "Long", 0, 7);
  const int was = big.focus();
  big.onEvent(reader::InputEvent{reader::Button::Down, reader::PressKind::Repeat, 9});
  CHECK(big.focus() == was + 9);
}
