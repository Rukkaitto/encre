#include "reader/input.h"

namespace reader {

void PressRecognizer::emit(Button b, PressKind kind) {
  if (count_ >= kQueueLen) {
    ++dropped_;
    return;
  }
  queue_[(head_ + count_) % kQueueLen] = InputEvent{b, kind};
  ++count_;
}

void PressRecognizer::sample(Button b, bool down, uint32_t ms) {
  const int i = static_cast<int>(b);
  if (i < 0 || i >= kButtonCount) return;
  State& s = state_[i];
  if (down) {
    // A second down with no release in between keeps the ORIGINAL timestamp: the
    // press has been held since then, and restarting the clock would push the
    // hold threshold further away every time a stray edge arrived.
    if (!s.down) {
      s.down = true;
      s.consumed = false;
      s.downAt = ms;
    }
    return;
  }
  if (!s.down) return;  // release with no matching down
  s.down = false;
  // A hold that already fired consumed the press; only an unconsumed one
  // becomes a Short. This is what makes one physical press exactly one event.
  if (!s.consumed) emit(b, PressKind::Short);
  s.consumed = false;
}

void PressRecognizer::tick(uint32_t ms) {
  for (int i = 0; i < kButtonCount; ++i) {
    State& s = state_[i];
    if (!s.down || s.consumed) continue;
    const Button b = static_cast<Button>(i);
    if (!maskHas(longPressable_, b)) continue;
    // Unsigned subtraction, so a clock that has wrapped past zero still yields
    // the true elapsed time.
    if (static_cast<uint32_t>(ms - s.downAt) >= kLongPressMs) {
      s.consumed = true;
      emit(b, PressKind::Long);
    }
  }
}

bool PressRecognizer::pop(InputEvent& out) {
  if (count_ == 0) return false;
  out = queue_[head_];
  head_ = (head_ + 1) % kQueueLen;
  --count_;
  return true;
}

}  // namespace reader
