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

void PressRecognizer::emit(Button b, PressKind kind, uint32_t at, int steps) {
  if (count_ >= kQueueLen) {
    ++dropped_;
    return;
  }
  queue_[(head_ + count_) % kQueueLen] = InputEvent{b, kind, steps, at};
  ++count_;
}

void PressRecognizer::setAutoRepeat(ButtonMask mask) {
  autoRepeat_ = mask;
  // See the header: a button in both masks has undefined behaviour from the
  // user's point of view, so the overlap is resolved here rather than left to
  // whichever fires first.
  longPressable_ = static_cast<ButtonMask>(longPressable_ & ~mask);
}

// Rows per second for a button held `heldMs`. Linear from slow to fast across the
// ramp, then flat. Integer throughout: there is no FPU on this part and a rate
// does not need a fraction.
static int repeatRateFor(uint32_t heldMs) {
  if (heldMs < kRepeatDelayMs) return 0;
  const uint32_t t = heldMs - kRepeatDelayMs;
  if (t >= kRepeatRampMs) return kRepeatFastRowsPerSec;
  const int span = kRepeatFastRowsPerSec - kRepeatSlowRowsPerSec;
  return kRepeatSlowRowsPerSec + static_cast<int>((span * t) / kRepeatRampMs);
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
      s.repeated = false;
      s.firedShort = false;
      s.lastRepeatAt = ms;
      s.downAt = ms;
      // LATCHED HERE, not read again while held -- see the header. The press that
      // fires on this edge can itself push a screen that repeats this button, and
      // re-reading the mask would then scroll the new screen under a finger that
      // has not yet come up from the press that opened it.
      s.repeatable = maskHas(autoRepeat_, b);
      // THE PRESS FIRES NOW unless it could still turn out to be a hold. Only a
      // long-press binding makes it ambiguous; without one the release adds
      // nothing but its own duration, which is 80-200 ms in front of a waveform.
      //
      // An auto-repeat button fires too, and its ramp still starts from downAt +
      // kRepeatDelayMs -- typematic. `firedShort` rather than `consumed` is what
      // keeps tick() looking at it.
      if (!maskHas(longPressable_, b)) {
        s.firedShort = true;
        emit(b, PressKind::Short, ms);
      }
    }
    return;
  }
  if (!s.down) return;  // release with no matching down
  s.down = false;
  // The down edge already spent this press. Emitting here as well is how one
  // physical press becomes two events -- on a list, a focus move followed by a
  // second focus move the user never asked for.
  if (s.firedShort) {
    s.firedShort = false;
    s.repeated = false;
    s.consumed = false;
    return;
  }
  // A press that repeated is already spent: the repeats WERE the press, and a
  // trailing Short here would move the list one more row after the user let go.
  if (s.repeated) {
    s.repeated = false;
    s.consumed = false;
    return;
  }
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
    emit(b, heldLongEnough && maskHas(longPressable_, b) ? PressKind::Long : PressKind::Short, ms);
  }
  s.consumed = false;
}

void PressRecognizer::tick(uint32_t ms) {
  for (int i = 0; i < kButtonCount; ++i) {
    State& s = state_[i];
    if (!s.down || s.consumed) continue;
    const Button b = static_cast<Button>(i);

    if (s.repeatable) {
      const uint32_t held = static_cast<uint32_t>(ms - s.downAt);
      const int rate = repeatRateFor(held);
      if (rate <= 0) continue;
      // First repeat of this press: the clock starts at the end of the delay, not
      // at the tick that noticed. Otherwise a tick arriving late -- which is the
      // normal case, since a paint blocks the loop -- would silently swallow
      // however long it was late by.
      if (!s.repeated) s.lastRepeatAt = s.downAt + kRepeatDelayMs;
      const uint32_t elapsed = static_cast<uint32_t>(ms - s.lastRepeatAt);
      int steps = static_cast<int>((elapsed * static_cast<uint32_t>(rate)) / 1000u);
      if (steps <= 0) continue;  // not yet a whole row; the remainder is kept
      if (steps > kRepeatMaxSteps) {
        steps = kRepeatMaxSteps;
        // Over the cap the surplus is DROPPED rather than banked: banking it
        // would make the next tick jump again for a hold that had already ended.
        s.lastRepeatAt = ms;
      } else {
        // Advance by exactly the time the emitted steps account for, so the
        // fraction of a row left over survives to the next tick instead of being
        // rounded away every time.
        s.lastRepeatAt += (static_cast<uint32_t>(steps) * 1000u) / static_cast<uint32_t>(rate);
      }
      s.repeated = true;
      emit(b, PressKind::Repeat, ms, steps);
      continue;
    }

    // A press already spent on the down edge cannot become a hold, however the
    // mask has moved since. Home's Confirm opens the Library, where Confirm IS
    // long-pressable, and the finger is still down when that mask arrives.
    if (s.firedShort) continue;
    if (!maskHas(longPressable_, b)) continue;
    // Unsigned subtraction, so a clock that has wrapped past zero still yields
    // the true elapsed time.
    if (static_cast<uint32_t>(ms - s.downAt) >= kLongPressMs) {
      s.consumed = true;
      // The THRESHOLD, not this tick's clock. A tick that ran late -- which is the
      // normal case, since a paint blocks the loop for 520-825 ms -- would
      // otherwise report the press as having happened late rather than reporting
      // itself as late, and the latency it caused would vanish from the log.
      emit(b, PressKind::Long, s.downAt + kLongPressMs);
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
