#pragma once
#include <cstdint>

namespace reader {

// The seven physical buttons, in the SDK's own index order. The four front
// buttons that get hint slots are Back, Confirm, Up, Down (spec 4.0); Left and
// Right turn pages in the Reader and never get hints; Power sleeps.
enum class Button : uint8_t { Back, Confirm, Left, Right, Up, Down, Power, Count_ };
inline constexpr int kButtonCount = static_cast<int>(Button::Count_);

// `Repeat` is a held button ASKING FOR MORE OF THE SAME, not a distinct action:
// it is what holding Up or Down on a long list emits, over and over, while
// `Short` and `Long` are one per press. It carries a step count -- see
// InputEvent::steps and kRepeat* below for why it must.
enum class PressKind : uint8_t { Short, Long, Repeat };

struct InputEvent {
  Button button;
  PressKind kind;
  // How many units of the action this event is worth. Always 1 for Short and
  // Long; for Repeat it is how far the list should move, which is derived from
  // ELAPSED TIME rather than from a count of events.
  //
  // That is forced by the panel, not a preference. tick() only runs from the main
  // loop and a paint blocks that loop for 520-825 ms, so a conventional "one
  // repeat per interval" scheme fires about twice a second whatever interval it
  // asks for -- 256 books would take over two minutes. Carrying the step lets one
  // event represent all the time that passed while the panel was busy, so held
  // scrolling runs at a real rate instead of the repaint rate.
  int steps = 1;
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

// --- Held-scroll acceleration ------------------------------------------------
//
// Rows per second, ramped, because a long library needs both ends: a slow start
// so a held button can still land on the row you meant, and a fast end so 256
// books do not take a minute. Time-based, so a paint that blocks the loop for
// 825 ms costs nothing -- the next tick simply owes more rows.
//
// The delay is what keeps a deliberate hold distinct from a slow tap; below it a
// press is still exactly one Short.
constexpr uint32_t kRepeatDelayMs = 400;
constexpr int kRepeatSlowRowsPerSec = 6;
constexpr int kRepeatFastRowsPerSec = 30;
constexpr uint32_t kRepeatRampMs = 1800;
// A ceiling on ONE event's step, so a pathological stall -- a grayscale screen, a
// card probe's FAT scan -- cannot cash in five seconds of held button as a
// 150-row jump. The time over the cap is dropped rather than banked.
constexpr int kRepeatMaxSteps = 40;

// A button's name, for logs and diagnostics. Here rather than in each consumer
// because there were two copies the moment a second one wanted it, and a log
// that names the wrong button is worse than one that prints an index.
const char* buttonName(Button b);

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

  // Buttons that emit `Repeat` while held, accelerating. For list movement.
  //
  // MUTUALLY EXCLUSIVE WITH setLongPressable, and enforced rather than
  // documented: a button in both masks would have its press consumed by whichever
  // of the two fired first, so the behaviour would depend on how long the user
  // held it and on when the loop happened to tick. autoRepeat wins and the
  // long-press bit is dropped, because a hint bar's hold ring is a promise about
  // a DIFFERENT action while auto-repeat is more of the same one -- a screen that
  // asked for both has a bug in its hint bar, not in its scrolling.
  void setAutoRepeat(ButtonMask mask);
  ButtonMask autoRepeat() const { return autoRepeat_; }

  // Forget which buttons are held, WITHOUT emitting anything for them.
  //
  // For the one case where the caller knows it has lost raw edges: the shell's
  // input task drops transitions when its queue fills, which a long paint makes
  // possible. A dropped PRESS costs nothing. A dropped RELEASE leaves a button
  // latched down here, and then either tick() fires a Long for a button the user
  // is no longer touching, or the next press inherits the stale timestamp and
  // classifies as Long -- which on a list means opening the actions overlay
  // instead of the item.
  //
  // After a drop the honest state is "unknown", and this is how to say it. Any
  // event already recognised is kept: the drop invalidates what is HELD, not what
  // already happened. If a button really was down, its eventual release arrives
  // with no matching press and sample() ignores it, so the cost is one lost press
  // -- strictly better than one invented.
  ButtonMask longPressable() const { return longPressable_; }

  // One raw transition. `ms` is a millisecond clock and is allowed to wrap.
  void forgetPresses();

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

  void emit(Button b, PressKind kind, int steps = 1);

  struct State {
    bool down = false;
    bool consumed = false;  // Long already fired for this press
    // When the last Repeat was accounted for, and whether any fired. A press that
    // repeated emits nothing on release, exactly as a Long does: the repeats WERE
    // the press, and a trailing Short would move the list one further row after
    // the user let go.
    uint32_t lastRepeatAt = 0;
    bool repeated = false;
    uint32_t downAt = 0;
  };

  State state_[kButtonCount];
  InputEvent queue_[kQueueLen];
  int head_ = 0, count_ = 0;
  ButtonMask longPressable_ = 0;
  ButtonMask autoRepeat_ = 0;
  uint32_t dropped_ = 0;
};

}  // namespace reader
