#include "reader/scrollwindow.h"

namespace reader {

ScrollWindow::ScrollWindow(int count, int visibleRows, Focus::None none)
    : focus_(count, none), visible_(visibleRows) {
  clampWindow();
}

void ScrollWindow::clampWindow() {
  // WHERE THE FOCUS IS is Focus's job; this is only the window around it. A
  // window with no height is a state a caller can legitimately reach -- a screen
  // that asks before it has measured itself, a geometry whose content area is
  // shorter than one row -- so it is normalised here and every line below is free
  // of guards. (A negative COUNT is normalised by Focus, for the same reason.)
  if (visible_ < 0) visible_ = 0;

  const int focus = focus_.index();
  if (focus_.count() == 0 || visible_ == 0) {
    // Nothing to show, or nowhere to show it. The focus survives -- it is simply
    // not on glass -- and moveFocus refuses to move it, because "the focus is
    // always inside the window" cannot be satisfied by a window of nothing.
    first_ = 0;
    return;
  }

  // Scroll by the overflow, not by a page: exactly enough to bring the focus
  // back inside the window, which for a one-row move is one row.
  if (focus < first_) first_ = focus;
  if (focus > first_ + visible_ - 1) first_ = focus - visible_ + 1;

  // ...and never past the end. A list shorter than the window pins first_ at 0,
  // so it does not scroll at all.
  const int maxFirst = focus_.count() > visible_ ? focus_.count() - visible_ : 0;
  if (first_ > maxFirst) first_ = maxFirst;
  if (first_ < 0) first_ = 0;
}

int ScrollWindow::visibleCount() const {
  const int rest = focus_.count() - first_;
  if (rest <= 0) return 0;
  return rest < visible_ ? rest : visible_;
}

// Both halves, not just the focus. They move together today -- the window only
// scrolls because the focus left it -- but reporting "changed" from the pair is
// what keeps that an implementation detail rather than something a caller relies
// on.
bool ScrollWindow::moveFocus(int delta, const Focus::Gate* gate) {
  if (focus_.count() == 0 || visible_ == 0) return false;
  const int wasFirst = first_;
  const bool moved = focus_.move(delta, gate);
  clampWindow();
  return moved || first_ != wasFirst;
}

bool ScrollWindow::setFocus(int index, const Focus::Gate* gate) {
  const int wasFirst = first_;
  const bool moved = focus_.set(index, gate);
  clampWindow();
  return moved || first_ != wasFirst;
}

void ScrollWindow::setCount(int n) {
  focus_.setCount(n);
  clampWindow();
}

void ScrollWindow::setVisibleRows(int n) {
  visible_ = n;
  clampWindow();
}

}  // namespace reader
