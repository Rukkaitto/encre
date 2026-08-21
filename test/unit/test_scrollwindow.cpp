#include "doctest.h"
#include "reader/scrollwindow.h"

using namespace reader;

TEST_CASE("focus starts at the first item and the window at the top") {
  ScrollWindow w(20, 7);
  CHECK(w.count() == 20);
  CHECK(w.visibleRows() == 7);
  CHECK(w.focus() == 0);
  CHECK(w.firstVisible() == 0);
  CHECK(w.visibleCount() == 7);
}

TEST_CASE("focus clamps at both ends and reports no change there") {
  // A press that changes nothing must not report a change: on this panel a
  // repaint is 520 ms minimum, and spending it to redraw an identical screen is
  // what makes the end of a list feel like a stuck button.
  ScrollWindow w(3, 3);
  CHECK_FALSE(w.moveFocus(-1));
  CHECK(w.focus() == 0);
  CHECK(w.moveFocus(+1));
  CHECK(w.moveFocus(+1));
  CHECK(w.focus() == 2);
  CHECK_FALSE(w.moveFocus(+1));
  CHECK(w.focus() == 2);
  // ...and no wrapping, at either end. One rule for every list, as
  // HomeScreen::moveFocus already says: a list that jumps silently from the last
  // item to the first is indistinguishable from a stuck button.
  CHECK(w.focus() != 0);
}

TEST_CASE("moving past the bottom scrolls by exactly one row, not by a page") {
  // This is the rule the board states and the one a naive implementation gets
  // wrong: paging on overflow moves every row under the user's eye when they
  // asked for the next item.
  ScrollWindow w(10, 4);
  for (int i = 0; i < 3; ++i) {
    CHECK(w.moveFocus(+1));
    CHECK(w.firstVisible() == 0);  // still inside the first window
  }
  CHECK(w.focus() == 3);
  CHECK(w.moveFocus(+1));
  CHECK(w.focus() == 4);
  CHECK(w.firstVisible() == 1);  // exactly one, not 4
  CHECK(w.moveFocus(+1));
  CHECK(w.focus() == 5);
  CHECK(w.firstVisible() == 2);
}

TEST_CASE("moving above the top scrolls back by exactly one row") {
  ScrollWindow w(10, 4);
  for (int i = 0; i < 9; ++i) w.moveFocus(+1);
  REQUIRE(w.focus() == 9);
  REQUIRE(w.firstVisible() == 6);
  CHECK(w.moveFocus(-1));
  CHECK(w.focus() == 8);
  CHECK(w.firstVisible() == 6);  // still visible, so the window holds still
  for (int i = 0; i < 2; ++i) w.moveFocus(-1);
  REQUIRE(w.focus() == 6);
  CHECK(w.firstVisible() == 6);
  CHECK(w.moveFocus(-1));
  CHECK(w.focus() == 5);
  CHECK(w.firstVisible() == 5);  // one, not a page
}

TEST_CASE("the focus is always inside the window, whatever the movement") {
  // The invariant behind every rule above, asserted over a long walk with
  // deltas bigger than one -- a screen may one day bind a page jump, and
  // "scroll by one" must not be the only case that keeps focus visible.
  const int kDeltas[] = {+1, +1, +5, -1, -3, +7, +1, -9, +4, +4, +4, -2, +1};
  ScrollWindow w(23, 6);
  for (int d : kDeltas) {
    w.moveFocus(d);
    CAPTURE(d);
    CHECK(w.focus() >= w.firstVisible());
    CHECK(w.focus() < w.firstVisible() + w.visibleRows());
    CHECK(w.firstVisible() >= 0);
    CHECK(w.firstVisible() + w.visibleRows() <= w.count());
  }
}

TEST_CASE("the window never shows past the last item") {
  ScrollWindow w(10, 4);
  w.moveFocus(+100);
  CHECK(w.focus() == 9);
  CHECK(w.firstVisible() == 6);  // 10 - 4, not 9
  CHECK(w.visibleCount() == 4);
}

TEST_CASE("a list shorter than the window never scrolls at all") {
  ScrollWindow w(3, 8);
  w.moveFocus(+2);
  CHECK(w.focus() == 2);
  CHECK(w.firstVisible() == 0);
  CHECK(w.visibleCount() == 3);  // three rows, not eight
  w.moveFocus(+50);
  CHECK(w.firstVisible() == 0);
  // Exactly the window's length is still not scrollable.
  ScrollWindow exact(8, 8);
  exact.moveFocus(+7);
  CHECK(exact.focus() == 7);
  CHECK(exact.firstVisible() == 0);
}

TEST_CASE("setCount smaller than the current focus pulls focus back into range") {
  // The delete flow: the list is rescanned under a focus that pointed at the
  // item that is now gone. Leaving focus past the end would index a vector one
  // past its size on the next render.
  ScrollWindow w(10, 4);
  w.moveFocus(+9);
  REQUIRE(w.focus() == 9);
  REQUIRE(w.firstVisible() == 6);
  w.setCount(5);
  CHECK(w.count() == 5);
  CHECK(w.focus() == 4);
  CHECK(w.firstVisible() == 1);  // 5 - 4: the window follows the focus back
  CHECK(w.visibleCount() == 4);
  // Shrinking to one leaves a valid selection, not -1.
  w.setCount(1);
  CHECK(w.focus() == 0);
  CHECK(w.firstVisible() == 0);
  CHECK(w.visibleCount() == 1);
}

TEST_CASE("a count of zero means no selection and a window of nothing") {
  // An empty /books is a valid state, not an error, so this must be a describable
  // window rather than a crash. -1 is "nothing is selected", which is what lets a
  // screen tell an empty list from a list whose first row is focused.
  ScrollWindow w(0, 6);
  CHECK(w.focus() == -1);
  CHECK(w.firstVisible() == 0);
  CHECK(w.visibleCount() == 0);
  // ...and moving in an empty list changes nothing and says so.
  CHECK_FALSE(w.moveFocus(+1));
  CHECK_FALSE(w.moveFocus(-1));
  CHECK(w.focus() == -1);

  // Emptying a non-empty list is the same state.
  ScrollWindow had(10, 4);
  had.moveFocus(+9);
  had.setCount(0);
  CHECK(had.focus() == -1);
  CHECK(had.firstVisible() == 0);
  CHECK(had.visibleCount() == 0);
  // ...and refilling it selects the first row rather than staying at -1.
  had.setCount(3);
  CHECK(had.focus() == 0);
}

TEST_CASE("a negative count is the empty list, not a negative one") {
  ScrollWindow w(-5, 4);
  CHECK(w.count() == 0);
  CHECK(w.focus() == -1);
  CHECK(w.visibleCount() == 0);
}

TEST_CASE("setVisibleRows re-clamps the window it just resized") {
  // How many rows fit is computed from the panel height, the header band and the
  // hint bar, and the two geometries differ -- so this is not a constant set
  // once at construction.
  ScrollWindow w(10, 4);
  w.moveFocus(+9);
  REQUIRE(w.firstVisible() == 6);
  w.setVisibleRows(6);
  CHECK(w.visibleRows() == 6);
  CHECK(w.focus() == 9);
  CHECK(w.firstVisible() == 4);  // 10 - 6, so the last row is still visible
  w.setVisibleRows(3);
  CHECK(w.firstVisible() == 7);  // the focus is what the window follows
  CHECK(w.focus() == 9);
  // Growing past the count stops scrolling entirely.
  w.setVisibleRows(20);
  CHECK(w.firstVisible() == 0);
  CHECK(w.visibleCount() == 10);
}

TEST_CASE("a visibleRows of zero or negative is inert, not a division by zero") {
  // A geometry whose content area is shorter than one row -- or a screen that
  // asks before it has measured itself -- must produce a window of nothing, and
  // must not move a focus into a window that cannot show it.
  for (int rows : {0, -1, -7}) {
    CAPTURE(rows);
    ScrollWindow w(10, rows);
    CHECK(w.visibleRows() == 0);
    CHECK(w.firstVisible() == 0);
    CHECK(w.visibleCount() == 0);
    CHECK_FALSE(w.moveFocus(+1));
    CHECK_FALSE(w.moveFocus(-1));
    CHECK(w.focus() == 0);  // a selection still exists; it just is not on screen
  }
  // ...and it recovers the moment it is given a real height.
  ScrollWindow w(10, 0);
  w.setVisibleRows(4);
  CHECK(w.visibleCount() == 4);
  CHECK(w.moveFocus(+5));
  CHECK(w.focus() == 5);
  CHECK(w.firstVisible() == 2);
}

TEST_CASE("moveFocus of zero is not a change") {
  ScrollWindow w(10, 4);
  CHECK_FALSE(w.moveFocus(0));
  CHECK(w.focus() == 0);
}

TEST_CASE("setFocus lands on a row and reports whether it moved") {
  // The wake restore: the session record names a row and there is no press that
  // implies it. Out-of-range clamps rather than being refused, because a record
  // written before books were deleted must still restore to something.
  ScrollWindow w(10, 4);
  CHECK(w.setFocus(7));
  CHECK(w.focus() == 7);
  CHECK(w.firstVisible() == 4);
  CHECK_FALSE(w.setFocus(7));
  CHECK(w.setFocus(99));
  CHECK(w.focus() == 9);
  CHECK(w.setFocus(-4));
  CHECK(w.focus() == 0);
  CHECK(w.firstVisible() == 0);
  ScrollWindow empty(0, 4);
  CHECK_FALSE(empty.setFocus(3));
  CHECK(empty.focus() == -1);
}

TEST_CASE("visibleCount is what a renderer may draw, and never overruns the list") {
  for (int count = 0; count <= 12; ++count) {
    for (int rows = 0; rows <= 6; ++rows) {
      ScrollWindow w(count, rows);
      w.moveFocus(+count);  // walk to the end, wherever that leaves the window
      CAPTURE(count);
      CAPTURE(rows);
      CHECK(w.visibleCount() >= 0);
      CHECK(w.visibleCount() <= rows);
      CHECK(w.firstVisible() + w.visibleCount() <= count);
    }
  }
}
