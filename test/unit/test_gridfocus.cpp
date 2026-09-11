// <initializer_list> for the braced constructor and the range-for over braced
// lists below. libc++ satisfies this transitively and libstdc++ does not, so a
// file without it compiles on macOS and fails on the first Linux build.
#include <initializer_list>

#include "doctest.h"
#include "reader/gridfocus.h"

using namespace reader;

// The keyboard this exists for: four rows of ten characters and a function row
// of four (SHIFT, #+=, SPACE, JOIN). The ragged last row is the interesting
// part and is why the widths are per row rather than one number.
static GridFocus keyboard() { return GridFocus({10, 10, 10, 10, 4}); }

TEST_CASE("the grid is its row widths, and the count is their sum") {
  GridFocus g = keyboard();
  CHECK(g.rows() == 5);
  CHECK(g.count() == 44);
  CHECK(g.rowWidth(0) == 10);
  CHECK(g.rowWidth(4) == 4);
  CHECK(g.index() == 0);
  CHECK(g.row() == 0);
  CHECK(g.col() == 0);
}

TEST_CASE("row and column are derived from the flat index, both ways") {
  GridFocus g = keyboard();
  // Every cell round-trips: this is what the session record stores and hands
  // back, so a flat index that does not name the cell it came from would
  // restore the keyboard onto a different key.
  // The return value is "did anything move" and is deliberately not asserted
  // here: set(0) when the focus is already on cell 0 is correctly false, which
  // is Focus's contract and the thing that lets a screen skip a ~520 ms
  // repaint. What this case is about is the mapping, so it checks the state.
  for (int i = 0; i < g.count(); ++i) {
    g.set(i);
    REQUIRE(g.index() == i);
    const int r = g.row(), c = g.col();
    g.set(0);
    REQUIRE(g.index() == 0);
    g.setCell(r, c);
    CHECK(g.index() == i);
  }
}

TEST_CASE("the side buttons move along a row and never leave it") {
  // The board's own promise: "UP AND DOWN MOVE BETWEEN ROWS; THE SIDE PAGE
  // BUTTONS MOVE ALONG A ROW." A serpentine walk would break it.
  GridFocus g = keyboard();
  REQUIRE(g.setCell(1, 0));
  for (int c = 1; c < 10; ++c) {
    CHECK(g.moveCol(+1));
    CHECK(g.row() == 1);
    CHECK(g.col() == c);
  }
  // Off the end of row 1 comes back to the START OF ROW 1, not the start of row 2.
  CHECK(g.moveCol(+1));
  CHECK(g.row() == 1);
  CHECK(g.col() == 0);
  // And backwards off the start returns to the end of the same row.
  CHECK(g.moveCol(-1));
  CHECK(g.row() == 1);
  CHECK(g.col() == 9);
}

TEST_CASE("a column move wraps inside the ragged row too") {
  GridFocus g = keyboard();
  REQUIRE(g.setCell(4, 3));
  CHECK(g.moveCol(+1));
  CHECK(g.row() == 4);
  CHECK(g.col() == 0);
  CHECK(g.moveCol(-1));
  CHECK(g.row() == 4);
  CHECK(g.col() == 3);
}

TEST_CASE("rows wrap off each end onto the other") {
  GridFocus g = keyboard();
  REQUIRE(g.setCell(0, 2));
  CHECK(g.moveRow(-1));
  CHECK(g.row() == 4);
  CHECK(g.moveRow(+1));
  CHECK(g.row() == 0);
  CHECK(g.col() == 2);
}

TEST_CASE("landing in a narrower row clamps the column") {
  GridFocus g = keyboard();
  REQUIRE(g.setCell(3, 7));
  CHECK(g.moveRow(+1));
  CHECK(g.row() == 4);
  CHECK(g.col() == 3);  // the function row is 4 wide
}

TEST_CASE("THE ORIGINAL COLUMN IS REMEMBERED ACROSS A NARROWER ROW") {
  // Without this, walking down through the 4-wide function row and back up
  // leaves the focus in column 3 -- the user's column is destroyed by passing
  // through a row that could not hold it, which on a keyboard means the letter
  // under your thumb moves while you are only going down and up.
  GridFocus g = keyboard();
  REQUIRE(g.setCell(3, 7));
  REQUIRE(g.moveRow(+1));
  REQUIRE(g.col() == 3);
  CHECK(g.moveRow(-1));
  CHECK(g.row() == 3);
  CHECK(g.col() == 7);
}

TEST_CASE("a column move re-establishes the remembered column") {
  // Moving along a row is the user saying which column they want, so it
  // replaces the memory rather than being overridden by it.
  GridFocus g = keyboard();
  REQUIRE(g.setCell(3, 7));
  REQUIRE(g.moveRow(+1));   // clamped to col 3 in the 4-wide row
  REQUIRE(g.moveCol(-1));   // the user picks col 2 deliberately
  REQUIRE(g.col() == 2);
  CHECK(g.moveRow(-1));
  CHECK(g.row() == 3);
  CHECK(g.col() == 2);      // 2, not the forgotten 7
}

TEST_CASE("set() and setCell() also re-establish the remembered column") {
  GridFocus g = keyboard();
  REQUIRE(g.setCell(3, 7));
  REQUIRE(g.moveRow(+1));
  REQUIRE(g.setCell(4, 1));
  CHECK(g.moveRow(-1));
  CHECK(g.col() == 1);
}

TEST_CASE("A HELD MOVE CLAMPS ON BOTH AXES WHERE A PRESS WRAPS") {
  // Focus's own rule, which a grid has to honour twice: a held button that
  // wraps has no end and cycles for as long as it is down, which is a carousel
  // rather than scrolling.
  GridFocus g = keyboard();
  REQUIRE(g.setCell(1, 9));
  CHECK_FALSE(g.moveCol(+1, /*held=*/true));
  CHECK(g.col() == 9);
  CHECK(g.row() == 1);

  REQUIRE(g.setCell(4, 0));
  CHECK_FALSE(g.moveRow(+1, /*held=*/true));
  CHECK(g.row() == 4);

  REQUIRE(g.setCell(0, 0));
  CHECK_FALSE(g.moveRow(-1, /*held=*/true));
  CHECK(g.row() == 0);
  CHECK_FALSE(g.moveCol(-1, /*held=*/true));
  CHECK(g.col() == 0);
}

TEST_CASE("a move that changes nothing reports false") {
  // So a screen can answer Action::none() instead of paying a ~520 ms repaint
  // that draws an identical frame.
  GridFocus g = keyboard();
  CHECK_FALSE(g.moveCol(0));
  CHECK_FALSE(g.moveRow(0));
  GridFocus one({1});
  CHECK_FALSE(one.moveCol(+1));
  CHECK_FALSE(one.moveRow(+1));
}

TEST_CASE("a delta larger than the axis lands where one lap would") {
  // A held Up or Down delivers a DISTANCE rather than a press
  // (InputEvent::steps), so several laps have to resolve.
  GridFocus g = keyboard();
  REQUIRE(g.setCell(2, 0));
  CHECK(g.moveCol(+23));  // 23 mod 10 == 3
  CHECK(g.row() == 2);
  CHECK(g.col() == 3);

  REQUIRE(g.setCell(0, 0));
  CHECK(g.moveRow(+12));  // 12 mod 5 == 2
  CHECK(g.row() == 2);

  REQUIRE(g.setCell(0, 0));
  CHECK(g.moveRow(-7));   // -7 mod 5 == 3
  CHECK(g.row() == 3);
}

TEST_CASE("set() CLAMPS rather than wrapping, because it is the restore path") {
  // Focus's distinction, inherited: a record naming a cell that no longer
  // exists means "as far as you can go", where wrapping it would land the user
  // somewhere unrelated to where they were.
  GridFocus g = keyboard();
  CHECK(g.set(999));
  CHECK(g.index() == 43);
  CHECK(g.set(-5));
  CHECK(g.index() == 0);
  CHECK(g.setCell(99, 99));
  CHECK(g.row() == 4);
  CHECK(g.col() == 3);
  CHECK(g.setCell(-1, -1));
  CHECK(g.row() == 0);
  CHECK(g.col() == 0);
}

TEST_CASE("wrapping can be turned off, and then both axes clamp") {
  GridFocus g = keyboard();
  g.setWrapping(false);
  CHECK_FALSE(g.wraps());
  REQUIRE(g.setCell(1, 9));
  CHECK_FALSE(g.moveCol(+1));
  CHECK(g.col() == 9);
  REQUIRE(g.setCell(4, 0));
  CHECK_FALSE(g.moveRow(+1));
  CHECK(g.row() == 4);
  REQUIRE(g.setCell(0, 0));
  CHECK_FALSE(g.moveRow(-1));
  CHECK_FALSE(g.moveCol(-1));
}

TEST_CASE("A ONE-ROW GRID IS A PLAIN FOCUS AND A ONE-COLUMN GRID IS ITS TRANSPOSE") {
  // The degenerate cases are worth pinning because they are what a second
  // caller is most likely to be: a single row of keys, or a vertical list.
  GridFocus row({6});
  CHECK(row.count() == 6);
  CHECK_FALSE(row.moveRow(+1));  // nowhere to go
  CHECK(row.moveCol(+1));
  CHECK(row.col() == 1);
  CHECK(row.moveCol(+5));        // wraps a lap
  CHECK(row.col() == 0);

  GridFocus column({1, 1, 1});
  CHECK(column.count() == 3);
  CHECK_FALSE(column.moveCol(+1));  // each row is one cell wide
  CHECK(column.moveRow(+1));
  CHECK(column.row() == 1);
  CHECK(column.moveRow(+2));
  CHECK(column.row() == 0);
}

TEST_CASE("an empty grid is a state, not a crash") {
  // A screen constructed before its layout is known, which is how every
  // heightless ScrollWindow in this firmware also behaves.
  GridFocus g;
  CHECK(g.rows() == 0);
  CHECK(g.count() == 0);
  CHECK(g.index() == -1);
  CHECK(g.row() == -1);
  CHECK(g.col() == -1);
  CHECK_FALSE(g.moveCol(+1));
  CHECK_FALSE(g.moveRow(+1));
  CHECK_FALSE(g.set(0));
}

TEST_CASE("a zero-width row is refused rather than becoming a hole") {
  // A row you cannot land in would make moveRow's wrap skip it silently and
  // row()/col() ambiguous at its boundary. Refusing at construction keeps every
  // row a real destination.
  GridFocus g({10, 0, 10});
  CHECK(g.rows() == 0);
  CHECK(g.count() == 0);
}

TEST_CASE("the flat index and the two axes never disagree") {
  // A property over every reachable state rather than a hand-picked table:
  // whatever sequence of moves got us here, index() must name the cell
  // (row(), col()) names.
  GridFocus g = keyboard();
  int seed = 1;
  for (int step = 0; step < 4000; ++step) {
    seed = seed * 1103515245 + 12345;
    const int pick = (seed >> 16) & 3;
    const int delta = ((seed >> 8) & 7) - 3;
    const bool held = ((seed >> 20) & 1) != 0;
    if (pick == 0) g.moveCol(delta, held);
    else if (pick == 1) g.moveRow(delta, held);
    else if (pick == 2) g.set((seed >> 4) % 50 - 3);
    else g.setCell((seed >> 4) % 7 - 1, (seed >> 12) % 13 - 1);

    REQUIRE(g.index() >= 0);
    REQUIRE(g.index() < g.count());
    REQUIRE(g.row() >= 0);
    REQUIRE(g.row() < g.rows());
    REQUIRE(g.col() >= 0);
    REQUIRE(g.col() < g.rowWidth(g.row()));
    // The index is the sum of the widths above it plus the column.
    int flat = 0;
    for (int r = 0; r < g.row(); ++r) flat += g.rowWidth(r);
    REQUIRE(flat + g.col() == g.index());
  }
}
