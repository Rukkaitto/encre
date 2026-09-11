#pragma once
#include "reader/app.h"
#include "reader/gridfocus.h"

namespace reader {

// A SCREEN WHOSE SELECTION MOVES IN TWO AXES, and FocusScreen's rule applied to
// a grid: A SCREEN THAT REPORTS A FOCUS ACCEPTS ONE BACK.
//
// The pair is FINAL here for the reason it is final there. Before FocusScreen
// the rule was five copies of a six-line mirror, and THREE SCREENS SHIPPED WITH
// HALF OF IT -- each behind a header comment arguing why its own case was the
// exception. Every one of those premises was true and every conclusion was
// wrong.
//
// THE TEMPTING ARGUMENT HERE IS PRECISELY THE DISCREDITED ONE. A wake never
// reaches the password keyboard: the factory refuses it unprimed, so
// App::restore stops early and the reader lands on WifiSettings. It is still
// not this header's business. Whether a screen is reachable by a restore is a
// fact about the factory and the shell's ladder, and encoding that fact in
// core/ is exactly how the previous three went one-way.
//
// A SIBLING OF FocusScreen RATHER THAN A GENERALISATION OF IT. Teaching
// FocusScreen to hold either a ScrollWindow or a GridFocus would edit the base
// every focused screen in the firmware derives from, to serve one new screen,
// and would leak a ragged grid's semantics into a type that has none. The cost
// is that the two bases share their shape by convention rather than by code --
// which is worth watching, but is a smaller edge than the alternative.
//
// WHAT A DERIVED SCREEN SUPPLIES:
//   - its grid, at construction -- the row widths.
//   - syncVm(): mirror focus() (or row()/col(), which is what a keyboard
//     actually draws) into the view-model the theme reads. Called exactly when
//     something moved.
//
// NO focusable() GATE, unlike FocusScreen, because GridFocus has none: every
// cell of every keyboard layer is a live key. See gridfocus.h for what adding
// one would mean.
class GridFocusScreen : public Screen {
 public:
  // WHERE THE SELECTION IS, as the flat cell index -- what the session record
  // stores, and what a restore hands back. -1 on an empty grid.
  int focus() const final { return grid_.index(); }

  // PUT A STORED FOCUS BACK. Clamps into range rather than wrapping, which is
  // Focus's restore-path rule inherited through GridFocus. The bool means
  // "something changed", per Screen::setFocus.
  bool setFocus(int index) final {
    if (!grid_.set(index)) return false;
    syncVm();
    return true;
  }

 protected:
  explicit GridFocusScreen(const GridFocus& grid) : grid_(grid) {}

  // The two axes. Redraw only when something actually moved: at a clamping edge
  // a press must not cost a ~520 ms panel refresh that draws an identical
  // frame. `held` is GestureEvent's flag, passed straight through to GridFocus,
  // which is where clamp-versus-wrap is decided -- no screen has an opinion.
  //
  // WHICH GESTURE DRIVES WHICH IS THE SCREEN'S, not this base's, and that is
  // deliberate: the keyboard declares split movers, so Up/Down arrive as
  // AltPrev/AltNext and the side buttons as Prev/Next. A base that hard-wired
  // that mapping would be making an input decision on behalf of a screen that
  // has not been written yet.
  Action moveCol(int delta, bool held = false) {
    if (!grid_.moveCol(delta, held)) return Action::none();
    syncVm();
    return Action::redraw();
  }
  Action moveRow(int delta, bool held = false) {
    if (!grid_.moveRow(delta, held)) return Action::none();
    syncVm();
    return Action::redraw();
  }

  // The grid, for what stays the screen's own business: reading row()/col() to
  // index a layout, and setCell for a deliberate jump.
  GridFocus& grid() { return grid_; }
  const GridFocus& grid() const { return grid_; }

  // Mirror the focus into the view-model the theme draws. The mirror is the
  // whole of what a screen still does about focus.
  virtual void syncVm() = 0;

 private:
  GridFocus grid_;
};

}  // namespace reader
