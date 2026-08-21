#include "reader/scrollwindow.h"

namespace reader {

ScrollWindow::ScrollWindow(int count, int visibleRows) : count_(count), visible_(visibleRows) {
  clamp();
}

void ScrollWindow::clamp() {
  // A window with no height, and a negative count, are both states a caller can
  // legitimately reach -- a screen that asks before it has measured itself, a
  // geometry whose content area is shorter than one row, a count arrived at by
  // subtraction. Normalising them here is what keeps every other line below
  // free of guards.
  if (visible_ < 0) visible_ = 0;
  if (count_ < 0) count_ = 0;

  if (count_ == 0) {
    // Nothing to select and nothing to show. -1 rather than 0 so a screen can
    // tell an empty list from a list whose first row is focused.
    focus_ = -1;
    first_ = 0;
    return;
  }

  // A list that has items always has a selection, even one arrived at from the
  // empty state (focus_ == -1) by a rescan that found books.
  if (focus_ < 0) focus_ = 0;
  if (focus_ > count_ - 1) focus_ = count_ - 1;

  if (visible_ == 0) {
    // Inert: there is no visible window, so there is nowhere to scroll to. The
    // focus survives -- it is simply not on glass -- and moveFocus refuses to
    // move it, because "the focus is always inside the window" cannot be
    // satisfied by any window of nothing.
    first_ = 0;
    return;
  }

  // Scroll by the overflow, not by a page: exactly enough to bring the focus
  // back inside the window, which for a one-row move is one row.
  if (focus_ < first_) first_ = focus_;
  if (focus_ > first_ + visible_ - 1) first_ = focus_ - visible_ + 1;

  // ...and never past the end. A list shorter than the window pins first_ at 0,
  // so it does not scroll at all.
  const int maxFirst = count_ > visible_ ? count_ - visible_ : 0;
  if (first_ > maxFirst) first_ = maxFirst;
  if (first_ < 0) first_ = 0;
}

int ScrollWindow::visibleCount() const {
  const int rest = count_ - first_;
  if (rest <= 0) return 0;
  return rest < visible_ ? rest : visible_;
}

bool ScrollWindow::moveFocus(int delta) {
  if (count_ == 0 || visible_ == 0) return false;
  return setFocus(focus_ + delta);
}

bool ScrollWindow::setFocus(int index) {
  const int wasFocus = focus_, wasFirst = first_;
  focus_ = index;
  clamp();
  // Both halves, not just the focus. They move together today -- the window only
  // scrolls because the focus left it -- but reporting "changed" from the pair is
  // what keeps that an implementation detail rather than something a caller
  // relies on.
  return focus_ != wasFocus || first_ != wasFirst;
}

void ScrollWindow::setCount(int n) {
  count_ = n;
  clamp();
}

void ScrollWindow::setVisibleRows(int n) {
  visible_ = n;
  clamp();
}

}  // namespace reader
