// THE FOCUS PRIMITIVE, which five screens were each implementing by hand.
//
// HomeScreen, the Settings list, ItemActionsScreen, DeleteConfirmScreen and ScrollWindow
// all carried the same eight lines -- add a delta, clamp to a range, return
// whether it moved -- with the range spelled slightly differently in each. That
// duplication is what let three of them ship a focus() with no setFocus() and
// nobody notice, and it is the reason "clamp, do not wrap" had to be written into
// four separate comments to stay one rule.
//
// This is that rule as an object, so a screen states its RANGE and gets the
// behaviour, and a change to the behaviour -- wrapping, say -- happens once.
#include "doctest.h"
#include "reader/focus.h"

using reader::Focus;

TEST_CASE("a focus starts on the first item and moves within the list") {
  Focus f(3);
  CHECK(f.index() == 0);
  CHECK(f.count() == 3);
  CHECK(f.move(+1));
  CHECK(f.index() == 1);
  CHECK(f.move(+1));
  CHECK(f.index() == 2);
}

TEST_CASE("a non-wrapping focus clamps at both ends and says nothing moved") {
  // Opt-out behaviour now, and still what a screen wants when running off the end
  // would be a mistake rather than a convenience. A press that changes nothing
  // must not cost a panel refresh that repaints an identical screen -- at least
  // 520 ms on this glass.
  Focus f(3);
  f.setWrapping(false);
  REQUIRE(f.move(+2));
  CHECK_FALSE(f.move(+1));
  CHECK(f.index() == 2);
  REQUIRE(f.move(-2));
  CHECK_FALSE(f.move(-1));
  CHECK(f.index() == 0);
}

TEST_CASE("a none slot is a real position below the first item") {
  // Home's CONTINUE block is -1: not "no selection", a place the user can be, and
  // one the wake record has to be able to hold.
  Focus f(2, Focus::WithNone);
  CHECK(f.index() == -1);
  CHECK(f.lowest() == -1);
  CHECK(f.move(+1));
  CHECK(f.index() == 0);
  CHECK(f.move(-1));
  CHECK(f.index() == -1);
  // There is no floor below it any more -- moving off it wraps to the last item,
  // which the ring test below states properly. What this case pins is that -1 is
  // a position the focus can rest on at all.
  CHECK(f.move(-1));
  CHECK(f.index() == 1);
}

TEST_CASE("an empty list sits at -1 whether or not it has a none slot") {
  // Both empties are the same state: there is no row 0 to be on. An empty Library
  // reports -1 and so does a Settings screen with no rows.
  for (const Focus::None none : {Focus::WithNone, Focus::Noneless}) {
    Focus f(0, none);
    CHECK(f.index() == -1);
    CHECK(f.lowest() == -1);
    CHECK_FALSE(f.move(+1));
    CHECK_FALSE(f.move(-1));
    CHECK_FALSE(f.set(0));
    CHECK(f.index() == -1);
  }
}

TEST_CASE("set CLAMPS even when moving would wrap, because a restore is not a move") {
  // The distinction the wake record needs. A record naming row 400 of a list that
  // now has three rows means "as far down as you can go" -- wrapping it round to
  // row 1 would put the user somewhere with no relation to where they were.
  Focus f(3);
  CHECK(f.set(400));
  CHECK(f.index() == 2);
  CHECK(f.set(-400));
  CHECK(f.index() == 0);
}

TEST_CASE("wrapping is on by default") {
  Focus f(3);
  CHECK(f.wraps());
  REQUIRE(f.set(2));
  CHECK(f.move(+1));
  CHECK(f.index() == 0);
}

TEST_CASE("a wrapping focus rolls off each end onto the other") {
  Focus f(3);
  REQUIRE(f.set(2));
  CHECK(f.move(+1));
  CHECK(f.index() == 0);
  CHECK(f.move(-1));
  CHECK(f.index() == 2);
}

TEST_CASE("a wrapping focus includes the none slot in the ring") {
  // Otherwise Home would wrap past CONTINUE, which is a place the user can be.
  Focus f(2, Focus::WithNone);
  REQUIRE(f.index() == -1);
  CHECK(f.move(-1));
  CHECK(f.index() == 1);
  CHECK(f.move(+1));
  CHECK(f.index() == -1);
}

TEST_CASE("a wrapping move of more than one lap lands where a single lap would") {
  // Held Up and Down deliver a distance, not a press -- InputEvent::steps can be
  // tens of rows -- so a wrapping list has to take a delta bigger than itself.
  Focus f(3);
  CHECK(f.move(+7));  // 0 -> 1, three laps and one
  CHECK(f.index() == 1);
  CHECK(f.move(-7));
  CHECK(f.index() == 0);
}

TEST_CASE("a list of one cannot move, wrapping or not") {
  Focus f(1);
  CHECK_FALSE(f.move(+1));
  f.setWrapping(false);
  CHECK_FALSE(f.move(+1));
  CHECK(f.index() == 0);
}

TEST_CASE("a shorter list pulls the focus back into range") {
  // A rescan after a delete. A focus left past the end indexes one past the
  // vector on the next render.
  Focus f(9);
  REQUIRE(f.set(8));
  f.setCount(3);
  CHECK(f.index() == 2);
  f.setCount(0);
  CHECK(f.index() == -1);
  // ...and refilling selects the first row rather than staying at -1.
  f.setCount(4);
  CHECK(f.index() == 0);
}

TEST_CASE("an emptied list with a none slot stays on the none slot when refilled") {
  // Home's menu is built at boot and does not change, but the rule has to be
  // stated: -1 is where a WithNone focus already was, so refilling must not drag
  // the user onto row 0 the way it does for a list that has no none slot.
  Focus f(0, Focus::WithNone);
  REQUIRE(f.index() == -1);
  f.setCount(3);
  CHECK(f.index() == -1);
}

TEST_CASE("a negative count is a count of zero, not an error") {
  // Callers arrive at counts by subtraction. Normalising here is what keeps every
  // screen free of its own guard.
  Focus f(-4);
  CHECK(f.count() == 0);
  CHECK(f.index() == -1);
}

// --- Held moves clamp where pressed moves wrap -------------------------------
//
// The interaction neither wrapping nor held-scroll owned alone, and the reason
// Focus::move takes a second argument.
//
// A wrap is right for a PRESS: the screen always changes, so it can never read as
// a dead button. It is wrong for a HOLD: a held button that wraps has no end and
// cycles for as long as it is down, which is a carousel rather than scrolling. On
// a 256-book library that is the difference between reaching the last book and
// never being able to stop on it.

TEST_CASE("a PRESSED move wraps, as it should") {
  reader::Focus f(4, reader::Focus::Noneless);
  REQUIRE(f.set(3));
  CHECK(f.move(+1));
  CHECK(f.index() == 0);  // round the ring
}

TEST_CASE("a HELD move clamps at the end instead of wrapping") {
  reader::Focus f(4, reader::Focus::Noneless);
  REQUIRE(f.set(3));
  CHECK_FALSE(f.move(+1, /*held=*/true));  // nothing moved: already at the end
  CHECK(f.index() == 3);
}

TEST_CASE("a HELD move clamps at the start too") {
  reader::Focus f(4, reader::Focus::Noneless);
  f.set(0);
  REQUIRE(f.index() == 0);  // where it LANDED, not whether it moved
  CHECK_FALSE(f.move(-1, true));
  CHECK(f.index() == 0);
}

TEST_CASE("a long held run lands on the last item rather than lapping") {
  // The defect, stated as the test that would have caught it: held scrolling
  // delivers a DISTANCE, so one event can be several laps. Wrapping turned a
  // 30-rows-per-second hold into an endless cycle; clamping lands it on the end
  // and leaves it there.
  reader::Focus f(10, reader::Focus::Noneless);
  f.set(0);
  REQUIRE(f.index() == 0);
  CHECK(f.move(+100, true));
  CHECK(f.index() == 9);
  // ...and staying there costs no further redraw, which is what stops the panel
  // repainting an identical screen 30 times a second.
  CHECK_FALSE(f.move(+100, true));
}

TEST_CASE("held clamping does not disturb a focus that already does not wrap") {
  reader::Focus f(4, reader::Focus::Noneless);
  f.setWrapping(false);
  REQUIRE(f.set(3));
  CHECK_FALSE(f.move(+1));
  CHECK_FALSE(f.move(+1, true));
  CHECK(f.index() == 3);
}

TEST_CASE("a held move through a none slot still clamps at the none end") {
  // Home's CONTINUE block is -1, a position rather than the absence of one. A held
  // Up must be able to reach it and then stop, not lap round to the last row.
  reader::Focus f(2, reader::Focus::WithNone);
  REQUIRE(f.set(1));
  CHECK(f.move(-50, true));
  CHECK(f.index() == -1);
  CHECK_FALSE(f.move(-50, true));
}
