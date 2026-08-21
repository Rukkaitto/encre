#pragma once
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
  ScrollWindow(int count, int visibleRows);

  int count() const { return count_; }
  int visibleRows() const { return visible_; }

  // The selected row, or -1 when there is nothing to select. An empty /books is
  // a valid state rather than an error, and -1 is what lets a screen tell it
  // apart from a list whose first row happens to be focused.
  int focus() const { return focus_; }

  // The first row on glass. Always 0 for a list that fits.
  int firstVisible() const { return first_; }

  // How many rows a renderer may actually draw: visibleRows() in the middle of a
  // long list, fewer at the end of a short one, 0 for an empty list or a window
  // with no height. Callers loop over this rather than over visibleRows(), so a
  // short list cannot read past the end of its own vector.
  int visibleCount() const;

  // Moves the focus by `delta`, clamping at both ends -- no wrapping, the rule
  // HomeScreen::moveFocus already states -- and scrolls the window by however
  // much it takes to keep the new focus visible.
  //
  // Returns whether anything changed, so a screen can answer Action::none() at
  // the end of a list instead of paying a refresh that repaints an identical
  // screen. On this panel that is at least 520 ms, and spending it to change
  // nothing is what makes the end of a list feel like a stuck button.
  bool moveFocus(int delta);

  // Selects a row outright, clamping into range. For the wake restore, where the
  // session record names a row and no press implies it; a record written before
  // some books were deleted must still restore to something. Same return
  // contract as moveFocus.
  bool setFocus(int index);

  // The list changed length -- a rescan after a delete, say. The focus is pulled
  // back into range and the window follows it, because a focus left past the end
  // would index one past the vector on the next render. Refilling a list that
  // had emptied selects the first row rather than staying at -1.
  void setCount(int n);

  void setVisibleRows(int n);

 private:
  // Re-establishes both invariants after any change to any of the four fields.
  // One function rather than one per mutator: the invariants are joint, and a
  // setter that maintained only its own half is how a window ends up scrolled
  // past the end of a list it just shrank.
  void clamp();

  int count_ = 0;
  int visible_ = 0;
  int focus_ = -1;
  int first_ = 0;
};

}  // namespace reader
