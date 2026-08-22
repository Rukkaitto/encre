#pragma once
#include "reader/focus.h"

namespace reader {

// A focus plus a first-visible index: everything a list longer than the screen
// needs in order to move, and nothing about how it is drawn.
//
// Home's menu is three rows and can hold its focus in the view-model. `/books`
// can hold hundreds, so Library needs to know which slice of the list is on
// glass as well as which row is selected. That is this class, and it is
// deliberately shared -- Settings and Bookmarks are lists too, and a second copy
// of these rules would be a second chance to page where the board scrolls.
//
// Two rules are behavioural and neither is enforceable by a type, so both are
// pinned in test/unit/test_scrollwindow.cpp:
//
//   - The focus is ALWAYS inside the window. Moving off the bottom scrolls the
//     window by exactly the overflow -- one row for a one-row move -- rather
//     than by a page. Paging on overflow moves every row under the user's eye
//     when they asked for the next item.
//   - The window never shows past the last item, and a list shorter than the
//     window does not scroll at all.
//
// `visibleRows` is a COMPUTED number, not a constant: the panel height minus the
// header band and the hint bar, divided by the row height. The two geometries
// differ by 8px and the band's height depends on its type role, so the caller
// derives it and may re-derive it -- hence setVisibleRows rather than a value
// fixed at construction. Three defects in this project came from pinning a
// number the board computes.
class ScrollWindow {
 public:
  ScrollWindow() = default;
  // `none` is passed to the Focus this window owns: a WithNone window's -1 is a
  // position below the first row (Home's CONTINUE block), with every none-slot
  // rule -- setCount keeping -1 where -1 is a place -- already Focus's.
  ScrollWindow(int count, int visibleRows, Focus::None none = Focus::Noneless);

  int count() const { return focus_.count(); }
  int visibleRows() const { return visible_; }

  // The selected row, or -1 when there is nothing to select. An empty /books is
  // a valid state rather than an error, and -1 is what lets a screen tell it
  // apart from a list whose first row happens to be focused.
  int focus() const { return focus_.index(); }

  // The first row on glass. Always 0 for a list that fits.
  int firstVisible() const { return first_; }

  // THE WINDOW AS A VIEW-MODEL CONSUMES IT: which rows are on glass, and where
  // the focus sits AMONG THEM. `focused` indexes the slice, not the list, and is
  // -1 when the focus is not on glass -- an empty list, a heightless window --
  // so a view-model built from a slice can never name a row that was not drawn.
  // Library and Settings each derived these three numbers by hand, and the -1
  // subtlety was documented on one copy and re-derived inside the other's loop.
  struct Slice {
    int first = 0;     // index into the whole list of the first row on glass
    int count = 0;     // rows on glass -- visibleCount()
    int focused = -1;  // the focus as an index into the slice, or -1
  };
  Slice slice() const;

  // How many rows a renderer may actually draw: visibleRows() in the middle of a
  // long list, fewer at the end of a short one, 0 for an empty list or a window
  // with no height. Callers loop over this rather than over visibleRows(), so a
  // short list cannot read past the end of its own vector.
  int visibleCount() const;

  // Moves the focus by `delta` -- WRAPPING off each end onto the other unless
  // setWrapping(false) has been used, which is Focus's rule and not a second copy
  // of it -- and scrolls the window by however much it takes to keep the new
  // focus visible. Wrapping to the far end scrolls the window with it, which is
  // the half of the behaviour that belongs to this class.
  //
  // Returns whether anything changed, so a screen can answer Action::none() at
  // the end of a list instead of paying a refresh that repaints an identical
  // screen. On this panel that is at least 520 ms, and spending it to change
  // nothing is what makes the end of a list feel like a stuck button.
  //
  // `held` and `gate` are passed straight through to Focus::move, which is where
  // clamp-versus-wrap and the landing rules are decided. A window has no opinion
  // about either; it only has to follow the focus it ends up with.
  bool moveFocus(int delta, bool held = false, const Focus::Gate* gate = nullptr);
  // Same guard as Focus::move's: a gate passed second would silently become
  // `held == true`.
  bool moveFocus(int delta, const Focus::Gate*) = delete;

  // Selects a row outright, clamping into range. For the wake restore, where the
  // session record names a row and no press implies it; a record written before
  // some books were deleted must still restore to something. Same return
  // contract as moveFocus, and the same gate pass-through: a refused landing
  // leaves focus and window both where they were.
  bool setFocus(int index, const Focus::Gate* gate = nullptr);

  // The list changed length -- a rescan after a delete, say. The focus is pulled
  // back into range and the window follows it, because a focus left past the end
  // would index one past the vector on the next render. Refilling a list that
  // had emptied selects the first row rather than staying at -1.
  void setCount(int n);

  void setVisibleRows(int n);

  // Whether the list rolls off each end onto the other. ON by default, like every
  // list in the firmware; a pass-through to Focus so a list screen that wants to
  // opt out says so once. See Focus::setWrapping, which records what enabling it
  // reversed and where it is sharpest (a held button on a long list).
  void setWrapping(bool on) { focus_.setWrapping(on); }
  bool wraps() const { return focus_.wraps(); }

 private:
  // Re-establishes the WINDOW's invariants after any change. The focus's own
  // invariants belong to Focus, which is the whole reason this is no longer one
  // function doing both -- the clamp it used to hold was the fifth copy of a rule
  // four screens also had.
  void clampWindow();

  Focus focus_;
  int visible_ = 0;
  int first_ = 0;
};

}  // namespace reader
