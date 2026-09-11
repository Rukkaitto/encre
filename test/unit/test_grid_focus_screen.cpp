// THE BASE CLASS THAT MAKES THE GRID'S FOCUS PAIR STRUCTURAL. Its contract is
// tested here on a minimal screen, so the real keyboard's suite tests its
// content rather than the mechanism -- and so a change to the mechanism fails
// here with the mechanism's name on it.
//
// This mirrors test_focus_screen.cpp deliberately: the two bases share their
// shape by convention rather than by code, so the convention is what has to be
// pinned.
#include <initializer_list>

#include "doctest.h"
#include "reader/grid_focus_screen.h"

using namespace reader;

namespace {

struct MiniGrid : GridFocusScreen {
  int synced = 0;
  int mirroredIndex = -99;
  int mirroredRow = -99;
  int mirroredCol = -99;

  MiniGrid() : GridFocusScreen(GridFocus({10, 10, 10, 10, 4})) { syncVm(); }

  // The protected mechanics, exposed so the test can drive them directly.
  using GridFocusScreen::grid;
  using GridFocusScreen::moveCol;
  using GridFocusScreen::moveRow;

  ScreenId id() const override { return ScreenId::Home; }
  Action onGesture(const GestureEvent& g) override {
    // The split-mover mapping a keyboard declares: the front buttons change
    // row, the side buttons move along one.
    if (g.what == Gesture::AltNext) return moveRow(+g.steps, g.held);
    if (g.what == Gesture::AltPrev) return moveRow(-g.steps, g.held);
    if (g.what == Gesture::Next) return moveCol(+g.steps, g.held);
    if (g.what == Gesture::Prev) return moveCol(-g.steps, g.held);
    return Action::none();
  }
  void render(Framebuffer&, const FontSet&, Theme&, Plane) const override {}

  void syncVm() override {
    ++synced;
    mirroredIndex = focus();
    mirroredRow = grid().row();
    mirroredCol = grid().col();
  }
};

}  // namespace

TEST_CASE("the grid screen mirrors into the vm exactly when something changed") {
  MiniGrid s;
  const int base = s.synced;
  CHECK(s.setFocus(23));
  CHECK(s.mirroredIndex == 23);
  CHECK(s.mirroredRow == 2);
  CHECK(s.mirroredCol == 3);
  CHECK(s.synced == base + 1);

  // Unchanged is false and owes no mirror call -- the shell reads the bool to
  // decide whether a repaint is owed, and a repaint here is ~520 ms.
  CHECK_FALSE(s.setFocus(23));
  CHECK(s.synced == base + 1);
}

TEST_CASE("a move that changes nothing costs no redraw and no mirror") {
  MiniGrid s;
  REQUIRE(s.focus() == 0);  // a fresh grid starts here; setFocus(0) would be a no-op
  const int base = s.synced;
  // Held at the top-left corner: both axes clamp, so neither moves.
  CHECK(s.moveRow(-1, /*held=*/true).kind == Action::Kind::None);
  CHECK(s.moveCol(-1, /*held=*/true).kind == Action::Kind::None);
  CHECK(s.synced == base);
  // The same presses unheld wrap, so both are a redraw.
  CHECK(s.moveRow(-1).kind == Action::Kind::Redraw);
  CHECK(s.moveCol(-1).kind == Action::Kind::Redraw);
  CHECK(s.synced == base + 2);
}

TEST_CASE("the two axes reach the vm as row and column, not just an index") {
  // A keyboard draws a cell, not a list position, so the mirror has to carry
  // both -- and they have to agree with the index after every kind of move.
  MiniGrid s;
  REQUIRE(s.focus() == 0);  // a fresh grid starts here; setFocus(0) would be a no-op
  s.moveRow(+4);
  CHECK(s.mirroredRow == 4);
  CHECK(s.mirroredCol == 0);
  CHECK(s.mirroredIndex == 40);
  s.moveCol(+3);
  CHECK(s.mirroredRow == 4);
  CHECK(s.mirroredCol == 3);
  CHECK(s.mirroredIndex == 43);
}

TEST_CASE("A STORED FOCUS ROUND-TRIPS THROUGH THE SCREEN, WHICH IS THE WHOLE RULE") {
  // The pair is final on the base, so this property cannot be half-implemented
  // -- but the property is what three screens shipped without, so it is
  // asserted rather than assumed.
  MiniGrid s;
  for (int i = 0; i < s.grid().count(); ++i) {
    s.setFocus(i);
    REQUIRE(s.focus() == i);
    s.setFocus(0);
    REQUIRE(s.focus() == 0);
    s.setFocus(i);
    CHECK(s.focus() == i);
  }
}

TEST_CASE("a restore past the end clamps rather than wrapping") {
  // Focus's restore-path rule, inherited two layers down: a record naming a
  // cell that no longer exists means "as far as you can go".
  MiniGrid s;
  CHECK(s.setFocus(999));
  CHECK(s.focus() == 43);
  CHECK(s.mirroredRow == 4);
  CHECK(s.mirroredCol == 3);
}

TEST_CASE("the split-mover mapping drives the right axis") {
  // Not the base's decision -- the screen's -- but the wiring is what a
  // keyboard will copy, so a case pins it.
  MiniGrid s;
  REQUIRE(s.focus() == 0);  // a fresh grid starts here; setFocus(0) would be a no-op
  s.onGesture({Gesture::Next, 1, false});
  CHECK(s.grid().row() == 0);
  CHECK(s.grid().col() == 1);
  s.onGesture({Gesture::AltNext, 1, false});
  CHECK(s.grid().row() == 1);
  CHECK(s.grid().col() == 1);
  s.onGesture({Gesture::AltPrev, 1, false});
  CHECK(s.grid().row() == 0);
  s.onGesture({Gesture::Prev, 1, false});
  CHECK(s.grid().col() == 0);
}
