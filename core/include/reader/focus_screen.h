#pragma once
#include "reader/app.h"
#include "reader/scrollwindow.h"

namespace reader {

// A SCREEN WITH A MOVABLE SELECTION, and the one implementation of the rule the
// catalogue kept re-learning: A SCREEN THAT REPORTS A FOCUS ACCEPTS ONE BACK.
//
// Before this class the rule was five copies of a six-line mirror -- report the
// focus, accept one, redraw only if something moved, mirror the index into the
// view-model -- and three screens shipped with half of it, each behind a header
// comment arguing why its own case was the exception (see Screen::focus in
// app.h). Here the pair is FINAL, so half-overriding it is not writable, and
// test_focus_restore.cpp's catalogue walk checks a property the type system now
// enforces rather than being the only thing that does.
//
// WHAT A DERIVED SCREEN SUPPLIES:
//   - its range, at construction. A screen that fits on one panel passes
//     visibleRows == count (a window that never scrolls behaves exactly as a
//     bare Focus); a scrolling list passes what the theme measured, or 0 for
//     "not told yet" -- movement is then refused, the same rule as "a screen
//     must not draw a row it was not given".
//   - syncVm(): mirror focus() -- and, for a windowed list, the visible slice --
//     into the view-model the theme draws. Called exactly when setFocus or
//     moveFocus changed something; a screen's own mutations (a rescan, a
//     geometry change) call it themselves.
//   - focusable(int), only where some positions refuse a landing (Settings'
//     section headers and placeholder rows). Skipping, wrapping through refused
//     ends, and refusing an unlandable restore all come from Focus::Gate -- the
//     hand-rolled walk this replaces silently stopped wrapping once.
class FocusScreen : public Screen, private Focus::Gate {
 public:
  // WHERE THE SELECTION IS, in the whole list -- what the session record
  // stores. -1 is a position (the none slot) or an empty list; see Focus.
  int focus() const final { return window_.focus(); }

  // PUT A STORED FOCUS BACK. Clamps into range (a record naming row 400 of a
  // three-row list means "as far down as you can go") and refuses a landing the
  // gate does. The bool means "something changed", per Screen::setFocus.
  bool setFocus(int index) final {
    if (!window_.setFocus(index, this)) return false;
    syncVm();
    return true;
  }

 protected:
  FocusScreen(int count, int visibleRows, Focus::None none = Focus::Noneless)
      : window_(count, visibleRows, none) {}

  // Redraw only when the focus actually moved: at the end of a clamping list a
  // press must not cost a ~520 ms panel refresh that changes nothing.
  Action moveFocus(int delta) {
    if (!window_.moveFocus(delta, this)) return Action::none();
    syncVm();
    return Action::redraw();
  }

  // The window, for what stays the screen's own business: setCount on a rescan,
  // setVisibleRows from the theme's box model, the visible slice for the vm.
  ScrollWindow& window() { return window_; }
  const ScrollWindow& window() const { return window_; }

  // Whether `index` may be LANDED on. Default: every position -- a screen with
  // unfocusable rows overrides this and gets skip, wrap and restore-refusal
  // from the one shared mechanism.
  bool focusable(int index) const override {
    (void)index;
    return true;
  }

  // Mirror the focus into the view-model the theme draws. The mirror is the
  // whole of what a screen still does about focus.
  virtual void syncVm() = 0;

 private:
  ScrollWindow window_;
};

}  // namespace reader
