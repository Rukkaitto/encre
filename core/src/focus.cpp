#include "reader/focus.h"

namespace reader {

Focus::Focus(int count, None none) : none_(none == WithNone) {
  setCount(count);
  // A list that has items starts on the first one; a WithNone focus starts on its
  // none slot, which is where Home's board puts it.
  if (count_ > 0 && !none_) index_ = 0;
}

int Focus::lowest() const {
  // An empty list has no row 0, so -1 is the only position it can hold -- whether
  // or not it was built with a none slot.
  if (count_ <= 0) return -1;
  return none_ ? -1 : 0;
}

void Focus::clampIndex() {
  if (count_ <= 0) {
    index_ = -1;
    return;
  }
  if (index_ < lowest()) index_ = lowest();
  if (index_ > count_ - 1) index_ = count_ - 1;
}

bool Focus::set(int index, const Gate* gate) {
  const int was = index_;
  index_ = index;
  clampIndex();
  // Clamp FIRST, then let the gate refuse the landing: a record naming row 400
  // clamps to the last row, and if that row cannot be landed on the restore is
  // refused with the index unchanged. An empty list is never asked: -1 is its
  // only state and there is nothing to refuse into.
  if (gate != nullptr && count_ > 0 && !gate->focusable(index_)) {
    index_ = was;
    return false;
  }
  return index_ != was;
}

int Focus::stepOnce(int from, int dir) const {
  if (count_ <= 0) return from;
  if (!wrap_) {
    int next = from + dir;
    if (next < lowest()) next = lowest();
    if (next > count_ - 1) next = count_ - 1;
    return next;
  }
  const int span = count_ - lowest();
  if (span <= 1) return from;  // one position: nowhere to step to
  int offset = (from - lowest() + dir) % span;
  if (offset < 0) offset += span;
  return lowest() + offset;
}

bool Focus::move(int delta, const Gate* gate) {
  if (count_ <= 0) return false;
  if (gate == nullptr) {
    // The ungated path keeps its O(1) arithmetic, bit-for-bit: every existing
    // caller lands exactly where it always did, multi-lap wraps included.
    if (!wrap_) return set(index_ + delta);

    // The ring runs from lowest() to count-1 inclusive, so a WithNone focus
    // wraps through its none slot rather than past it.
    const int span = count_ - lowest();
    if (span <= 1) return false;  // one position: nowhere to wrap to
    // Positive modulo: delta may be several laps in either direction, because a
    // held button delivers a distance rather than a press.
    int offset = (index_ - lowest() + delta) % span;
    if (offset < 0) offset += span;
    const int next = lowest() + offset;
    if (next == index_) return false;
    index_ = next;
    return true;
  }

  // The gated walk: one position at a time, so each LANDING can be judged. A
  // refused position is stepped over without consuming any distance; the guard
  // bounds the skip at one full lap, so a list with nothing focusable moves
  // nothing instead of spinning -- the full-circle check SettingsScreen used to
  // hand-roll, now in the one place movement rules live. The test suite pins
  // this walk to the arithmetic above with an everything-focusable gate over
  // every configuration.
  const int start = index_;
  const int dir = delta < 0 ? -1 : 1;
  int steps = delta < 0 ? -delta : delta;
  int i = index_;
  while (steps-- > 0) {
    int j = stepOnce(i, dir);
    int guard = count_ - lowest();
    while (j != i && !gate->focusable(j) && guard-- > 0) j = stepOnce(j, dir);
    if (j == i || !gate->focusable(j)) break;  // a clamping end, or nothing to land on
    i = j;
  }
  if (i == start) return false;
  index_ = i;
  return true;
}

void Focus::setCount(int n) {
  // A negative count is a count of zero: callers arrive at counts by subtraction,
  // and normalising here keeps every screen free of its own guard.
  count_ = n < 0 ? 0 : n;
  // Refilling a list that had emptied selects the first row -- but only where -1
  // is not a position in its own right. On a WithNone focus, -1 is where the user
  // was, and dragging them onto row 0 because the list grew would be a move they
  // did not make.
  if (count_ > 0 && index_ < 0 && !none_) index_ = 0;
  clampIndex();
}

}  // namespace reader
