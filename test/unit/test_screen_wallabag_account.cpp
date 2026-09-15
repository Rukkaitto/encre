#include "doctest.h"
#include "reader/screen_wallabag_account.h"

using namespace reader;

namespace {
GestureEvent ev(Gesture g) {
  GestureEvent e;
  e.what = g;
  e.steps = 1;
  return e;
}
WallabagAccountScreen::Facts facts(bool configured = true) {
  return {"LUCASG", 3, "NO NEW", 50, 1, configured};
}
}  // namespace

TEST_CASE("the focus starts on the first row that can act, and skips the four that cannot") {
  // Settings' rule: a row that cannot be reached cannot mislead, where a row
  // that focuses and then ignores Confirm is the silent no-op this project has
  // been bitten by twice.
  WallabagAccountScreen s(facts());
  CHECK(s.focus() == 3);  // Keep offline
  CHECK(s.vm().rows[0].label == "Account");
  CHECK(s.vm().rows[0].value == "LUCASG");
  CHECK(s.vm().rows[1].value == "3 ARTICLES");
  CHECK(s.vm().rows[2].value == "NO NEW");
  CHECK(s.vm().rows[4].value == "1 TO PUSH");
  CHECK(s.vm().rows[5].isHeader);
  CHECK(s.vm().bandValue == "SIGNED IN");

  // Down from Keep offline steps past the value row and the header onto Remove.
  REQUIRE(s.onGesture(ev(Gesture::Next)).kind == Action::Kind::Redraw);
  CHECK(s.focus() == 6);
  // ...and wraps back to Keep offline rather than landing on a value row.
  REQUIRE(s.onGesture(ev(Gesture::Next)).kind == Action::Kind::Redraw);
  CHECK(s.focus() == 3);
}

TEST_CASE("Keep offline cycles in place and latches") {
  WallabagAccountScreen s(facts());
  REQUIRE(s.focus() == 3);
  CHECK(s.vm().rows[3].value == "NEWEST 50");

  Action a = s.onGesture(ev(Gesture::Activate));
  CHECK(a.kind == Action::Kind::Article);
  CHECK(s.chosen() == WallabagAccountScreen::Chosen::KeepOffline);
  // The value moves HERE, so the screen redraws correct immediately; the shell
  // then commits and prunes, which is card work.
  CHECK(s.keepOffline() == 100);
  CHECK(s.vm().rows[3].value == "NEWEST 100");

  s.onGesture(ev(Gesture::Activate));
  CHECK(s.keepOffline() == 20);
  s.onGesture(ev(Gesture::Activate));
  CHECK(s.keepOffline() == 50);
}

TEST_CASE("an off-table value snaps onto the table rather than sitting outside it forever") {
  // A hand-edited settings file can say anything. nextKeepOfflineStep snaps
  // first, so 37 cycles to 50 instead of never joining the vocabulary.
  WallabagAccountScreen s({"LUCASG", 0, "NEVER", 37, 0, true});
  s.onGesture(ev(Gesture::Activate));
  CHECK(s.keepOffline() == 50);
}

TEST_CASE("the Remove row pushes the confirmation rather than latching") {
  // Nothing has happened yet, so nothing is latched: the confirmation is what
  // asks, and its REMOVE slab is where the latch lives.
  WallabagAccountScreen s(facts());
  REQUIRE(s.onGesture(ev(Gesture::Next)).kind == Action::Kind::Redraw);
  REQUIRE(s.focus() == 6);
  const Action a = s.onGesture(ev(Gesture::Activate));
  CHECK(a.kind == Action::Kind::Push);
  CHECK(a.target == ScreenId::ArticlesRemoveConfirm);
  CHECK(s.chosen() == WallabagAccountScreen::Chosen::None);
}

TEST_CASE("the Confirm hint follows the focused row, which is Settings' own precedent") {
  // One row cycles a value in place and one opens a screen. A Confirm labelled
  // CHANGE that opens a screen is a promise the press does not keep.
  WallabagAccountScreen s(facts());
  CHECK(s.vm().hints[1] == "CHANGE");
  REQUIRE(s.onGesture(ev(Gesture::Next)).kind == Action::Kind::Redraw);
  CHECK(s.vm().hints[1] == "OPEN");
}

TEST_CASE("an unconfigured device says so, and cannot reach the destructive row") {
  // There is nothing downloaded to remove, so the row that opens a destructive
  // confirmation is unreachable. Derived from `configured` rather than
  // tabulated, which is Settings' `Cover fit` precedent -- the answer changes
  // with the card and there is nothing to invalidate.
  WallabagAccountScreen s(facts(/*configured=*/false));
  CHECK(s.vm().bandValue == "NOT SET UP");
  CHECK(s.focus() == 3);
  // Keep offline is the ONLY focusable row, so a move lands back on it.
  s.onGesture(ev(Gesture::Next));
  CHECK(s.focus() == 3);
  s.onGesture(ev(Gesture::Prev));
  CHECK(s.focus() == 3);
  CHECK_FALSE(s.vm().rows[6].focusable);
}

TEST_CASE("a row states a quantity or discloses a screen, never both") {
  // Settings' test asserts this of every drawn row and this screen inherits it:
  // the Remove row carries the chevron and no value, and every value row carries
  // a value and no chevron.
  WallabagAccountScreen s(facts());
  for (const ListRow& r : s.vm().rows) {
    if (r.isHeader) continue;
    const bool statesAndDiscloses = r.discloses && !r.value.empty();
    CHECK_FALSE(statesAndDiscloses);
  }
  CHECK(s.vm().rows[6].discloses);
  CHECK(s.vm().rows[6].value.empty());
}
