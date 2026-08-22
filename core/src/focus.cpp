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

bool Focus::set(int index) {
  const int was = index_;
  index_ = index;
  clampIndex();
  return index_ != was;
}

bool Focus::move(int delta, bool held) {
  if (count_ <= 0) return false;
  // A HELD move clamps even where a pressed one wraps -- see the header.
  if (!wrap_ || held) return set(index_ + delta);

  // The ring runs from lowest() to count-1 inclusive, so a WithNone focus wraps
  // through its none slot rather than past it.
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
