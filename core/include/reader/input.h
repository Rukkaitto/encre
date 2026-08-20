#pragma once
#include <cstdint>

namespace reader {

// The seven physical buttons, in the SDK's own index order. The four front
// buttons that get hint slots are Back, Confirm, Up, Down (spec 4.0); Left and
// Right turn pages in the Reader and never get hints; Power sleeps.
enum class Button : uint8_t { Back, Confirm, Left, Right, Up, Down, Power, Count_ };
inline constexpr int kButtonCount = static_cast<int>(Button::Count_);

enum class PressKind : uint8_t { Short, Long };

struct InputEvent {
  Button button;
  PressKind kind;
};

// A set of buttons. Bit (1 << index).
using ButtonMask = uint8_t;
constexpr ButtonMask buttonBit(Button b) {
  return static_cast<ButtonMask>(1u << static_cast<int>(b));
}
constexpr bool maskHas(ButtonMask m, Button b) { return (m & buttonBit(b)) != 0; }

// How long a press must be held to count as a hold. 500 ms, matching the SDK's
// touch long-press rather than its 650 ms synthesized button holds: its own
// comment says 650 reads as sluggish where there is no button travel to absorb
// it, and a hold the user has to discover from a small ring mark wants to be at
// the short end of comfortable.
inline constexpr uint32_t kLongPressMs = 500;

// Turns raw level transitions into classified presses.
//
// Two rules govern everything here:
//   * One physical press produces EXACTLY ONE event. A hold that fires Long
//     while the button is still down consumes the press, so the release that
//     follows emits nothing.
//   * A button outside the long-press mask fires Short ON RELEASE however long
//     it was held -- never nothing. A user who presses slowly must not be
//     punished for it.
class PressRecognizer {
 public:
  // Which buttons have a long-press action right now. The App sets this from the
  // focused screen's hint slots, so it changes as screens are pushed and popped
  // -- including in the middle of a hold, which is why a fired press is latched
  // as consumed rather than re-derived at release time.
  void setLongPressable(ButtonMask mask) { longPressable_ = mask; }
  ButtonMask longPressable() const { return longPressable_; }

  // One raw transition. `ms` is a millisecond clock and is allowed to wrap.
  void sample(Button b, bool down, uint32_t ms);

  // Current time with no transition, so a hold can fire while still held. Call
  // every poll, or a hold never resolves until the button comes back up.
  void tick(uint32_t ms);

  bool pop(InputEvent& out);

  // Events discarded because the queue was full. The shell logs this; a non-zero
  // value means the main loop is not draining fast enough.
  uint32_t dropped() const { return dropped_; }

 private:
  static constexpr int kQueueLen = 16;

  void emit(Button b, PressKind kind);

  struct State {
    bool down = false;
    bool consumed = false;  // Long already fired for this press
    uint32_t downAt = 0;
  };

  State state_[kButtonCount];
  InputEvent queue_[kQueueLen];
  int head_ = 0, count_ = 0;
  ButtonMask longPressable_ = 0;
  uint32_t dropped_ = 0;
};

}  // namespace reader
