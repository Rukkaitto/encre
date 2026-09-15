#include <vector>

#include "doctest.h"
#include "reader/screen_articles.h"

using namespace reader;

namespace {

// Five fixture rows, three unread, in the board's own order.
std::vector<ArticleItem> fixture() {
  return {
      {1, "The Death and Life of the Great American Essay", "LONGREADS", 22, false, false},
      {2, "Why We Forget Most of the Books We Read", "THE ATLANTIC", 9, false, false},
      {3, "In Praise of Slow Reading", "AEON", 14, false, false},
      {4, "The Tyranny of the To-Be-Read Pile", "LIT HUB", 7, true, false},
      {5, "E Ink: The Quiet Display Technology That Refused to Die", "IEEE SPECTRUM", 16, true,
       false},
  };
}

GestureEvent ev(Gesture g, int steps = 1, bool held = false) {
  GestureEvent e;
  e.what = g;
  e.steps = steps;
  e.held = held;
  return e;
}

}  // namespace

TEST_CASE("the sync row is -1, above the list, and the focus starts on the first article") {
  // -1 IS THE SYNC ROW. The board fixes it between the band and the list, so it
  // does not scroll and cannot be a member of the ScrollWindow that does.
  // Focus::WithNone already has a position outside the list for exactly this,
  // and Home spends it the same way on its CONTINUE block -- the only difference
  // being that Home's sits BELOW the first item and this one sits above it.
  //
  // THE FOCUS STARTS ON ROW 0 THOUGH, and the board is what says so: it draws
  // row 0 inverted and the sync row plain. A reader who opens this screen has
  // come to read.
  ArticlesScreen s(fixture(), "WALLABAG \xC2\xB7 NO NEW");
  s.setVisibleRows(5);
  CHECK(s.focus() == 0);
  CHECK(s.vm().focusedRow == 0);

  // Up from the first article reaches the sync row rather than wrapping to the
  // bottom, which is what -1 being a real position buys.
  REQUIRE(s.onGesture(ev(Gesture::Prev)).kind == Action::Kind::Redraw);
  CHECK(s.focus() == -1);
  const Action a = s.onGesture(ev(Gesture::Activate));
  CHECK(a.kind == Action::Kind::Article);
  CHECK(s.chosen() == ArticlesScreen::Chosen::Sync);
}

TEST_CASE("Confirm on an article row asks the shell to open it") {
  ArticlesScreen s(fixture(), "WALLABAG \xC2\xB7 NO NEW");
  s.setVisibleRows(5);
  REQUIRE(s.focus() == 0);
  const Action a = s.onGesture(ev(Gesture::Activate));
  CHECK(a.kind == Action::Kind::Open);
  CHECK(s.focusedId() == 1);
  CHECK(s.focusedTitle() == "The Death and Life of the Great American Essay");
}

TEST_CASE("a HOLD on an article row opens the actions overlay, and never on the sync row") {
  ArticlesScreen s(fixture(), "WALLABAG \xC2\xB7 NO NEW");
  s.setVisibleRows(5);
  // On the sync row there is nothing to act on, so the hold does nothing rather
  // than opening an overlay captioned with an article the reader did not choose.
  REQUIRE(s.onGesture(ev(Gesture::Prev)).kind == Action::Kind::Redraw);
  REQUIRE(s.focus() == -1);
  CHECK(s.onGesture(ev(Gesture::Secondary)).kind == Action::Kind::None);
  REQUIRE(s.onGesture(ev(Gesture::Next)).kind == Action::Kind::Redraw);
  const Action a = s.onGesture(ev(Gesture::Secondary));
  CHECK(a.kind == Action::Kind::Push);
  CHECK(a.target == ScreenId::ArticleActions);
}

TEST_CASE("the band counts the UNREAD rows, not the rows") {
  ArticlesScreen s(fixture(), "WALLABAG \xC2\xB7 NO NEW");
  s.setVisibleRows(5);
  CHECK(s.vm().bandValue == "3 UNREAD");
  CHECK_FALSE(s.vm().notSetUp);
  // And the read rows carry it into the row itself, which is what draws the
  // hollow bullet -- a flag, not the theme reading the end of `meta`.
  CHECK_FALSE(s.vm().rows[0].read);
  CHECK(s.vm().rows[3].read);
  CHECK(s.vm().rows[3].meta == "LIT HUB \xC2\xB7 7 MIN \xC2\xB7 READ");
  CHECK(s.vm().rows[0].meta == "LONGREADS \xC2\xB7 22 MIN");
}

TEST_CASE("the hint bar promises a hold on Confirm and nothing else") {
  ArticlesScreen s(fixture(), "WALLABAG \xC2\xB7 NO NEW");
  s.setVisibleRows(5);
  CHECK(s.vm().hints[0] == "BACK");
  CHECK(s.vm().hints[1] == "READ");
  CHECK(s.vm().hints[2] == "UP");
  CHECK(s.vm().hints[3] == "DOWN");
  CHECK_FALSE(s.vm().holds[0]);
  CHECK(s.vm().holds[1]);
  CHECK_FALSE(s.vm().holds[2]);
  CHECK_FALSE(s.vm().holds[3]);
  // A hold and an auto-repeat are mutually exclusive per button, so the repeat
  // is on the two movers and not on Confirm.
  CHECK(maskHas(s.autoRepeat(), Button::Up));
  CHECK(maskHas(s.autoRepeat(), Button::Down));
  CHECK_FALSE(maskHas(s.autoRepeat(), Button::Confirm));
  CHECK(maskHas(s.longPressable(), Button::Confirm));
}

TEST_CASE("the not-set-up variant is one flag, and every gesture but Back is refused") {
  // HomeEmpty's rule: a variant is what this struct SAYS, never which function
  // draws it. There is nothing to sync and nothing to read, so a Confirm that
  // latched would be a press with no work behind it.
  ArticlesScreen s;
  CHECK(s.vm().notSetUp);
  CHECK(s.vm().bandValue == "NOT SET UP");
  CHECK(s.vm().rows.empty());
  CHECK(s.vm().syncStamp.empty());
  CHECK(s.vm().hints[0] == "BACK");
  CHECK(s.vm().hints[1].empty());
  CHECK(s.vm().hints[2].empty());
  CHECK(s.vm().hints[3].empty());

  CHECK(s.onGesture(ev(Gesture::Activate)).kind == Action::Kind::None);
  CHECK(s.onGesture(ev(Gesture::Secondary)).kind == Action::Kind::None);
  CHECK(s.onGesture(ev(Gesture::Next)).kind == Action::Kind::None);
  CHECK(s.onGesture(ev(Gesture::Prev)).kind == Action::Kind::None);
  CHECK(s.onGesture(ev(Gesture::Back)).kind == Action::Kind::Pop);
  // The prose is on the model, not in the theme: the board owns the words.
  CHECK_FALSE(s.vm().setupTitle.empty());
  CHECK_FALSE(s.vm().setupProse.empty());
  CHECK_FALSE(s.vm().setupNote.empty());
}

TEST_CASE("configured with no articles is neither variant: the sync row and an empty list") {
  // The state a reader is in the moment they fill the file in and before they
  // press anything. It must NOT be the not-set-up screen -- that would tell them
  // to go and edit a file they have just edited.
  ArticlesScreen s({}, "WALLABAG \xC2\xB7 NEVER");
  s.setVisibleRows(5);
  CHECK_FALSE(s.vm().notSetUp);
  CHECK(s.vm().bandValue == "0 UNREAD");
  CHECK(s.vm().rows.empty());
  CHECK(s.vm().syncStamp == "WALLABAG \xC2\xB7 NEVER");
  CHECK(s.focus() == -1);
  CHECK(s.onGesture(ev(Gesture::Activate)).kind == Action::Kind::Article);
  CHECK(s.chosen() == ArticlesScreen::Chosen::Sync);
  // Down from the sync row has nowhere to go and must not wrap onto itself in a
  // way that reads as a dead button: with no rows the focus simply stays.
  CHECK(s.focus() == -1);
}

TEST_CASE("the slice moves as the Library's does") {
  ArticlesScreen s(fixture(), "WALLABAG \xC2\xB7 NO NEW");
  s.setVisibleRows(2);
  CHECK(s.vm().totalRows == 5);
  CHECK(s.vm().rows.size() == 2);
  CHECK(s.vm().firstRow == 0);

  for (int i = 0; i < 2; ++i) s.onGesture(ev(Gesture::Next));
  REQUIRE(s.focus() == 2);
  // Scrolled by the overflow rather than by a page, which is ScrollWindow's rule:
  // the focus lands on the window's BOTTOM edge arriving from above.
  CHECK(s.vm().firstRow == 1);
  CHECK(s.vm().focusedRow == 1);
  CHECK(s.vm().rows[0].title == "Why We Forget Most of the Books We Read");
}

TEST_CASE("the sync-done variant is the stamp and a status line, and nothing else") {
  ArticlesScreen s(fixture(), "WALLABAG \xC2\xB7 3 NEW");
  s.setVisibleRows(5);
  CHECK(s.vm().statusLine.empty());
  s.setStatusLine("SYNC COMPLETE \xC2\xB7 3 NEW ARTICLES \xC2\xB7 1 ARCHIVE PUSHED");
  CHECK_FALSE(s.vm().statusLine.empty());
  CHECK(s.vm().syncStamp == "WALLABAG \xC2\xB7 3 NEW");
  // Same rows, same focus, same everything else: a variant is what the model
  // says and never a second screen.
  CHECK(s.vm().rows.size() == 5);
  CHECK(s.id() == ScreenId::Articles);
}
