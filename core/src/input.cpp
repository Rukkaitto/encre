#include "reader/input.h"

namespace reader {

const char* buttonName(Button b) {
  switch (b) {
    case Button::Back: return "BACK";
    case Button::Confirm: return "CONFIRM";
    case Button::Left: return "LEFT";
    case Button::Right: return "RIGHT";
    case Button::Up: return "UP";
    case Button::Down: return "DOWN";
    case Button::Power: return "POWER";
    case Button::Count_: break;
  }
  return "?";
}

void PressRecognizer::emit(Button b, PressKind kind) {
  if (count_ >= kQueueLen) {
    ++dropped_;
    return;
  }
  queue_[(head_ + count_) % kQueueLen] = InputEvent{b, kind};
  ++count_;
}

void PressRecognizer::forgetPresses() {
  // Only the down-state, and deliberately not the event queue -- see the header.
  for (State& s : state_) s = State{};
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
  // A hold that already fired consumed the press, so the release emits nothing.
  // That is what makes one physical press exactly one event.
  if (!s.consumed) {
    // tick() usually fires a hold while the button is still down, but it only
    // runs from the main loop, and a gray refresh blocks that loop for ~1.5 s.
    // A press made entirely inside a repaint therefore reaches here never having
    // been ticked -- so the release classifies it, from the edge timestamps the
    // input task captured at their true times.
    //
    // Getting this wrong is not a latency bug, it is a WRONG ACTION: on a list,
    // a hold opens the item-actions overlay and a press opens the item. Firing
    // the hold late is tolerable; silently doing the other thing is not.
    const bool heldLongEnough = static_cast<uint32_t>(ms - s.downAt) >= kLongPressMs;
    emit(b, heldLongEnough && maskHas(longPressable_, b) ? PressKind::Long : PressKind::Short);
  }
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
