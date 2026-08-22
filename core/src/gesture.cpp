#include "reader/gesture.h"

namespace reader {

GestureEvent gestureFor(const InputEvent& ev, ButtonMask holds, ButtonMask repeats) {
  GestureEvent out;

  switch (ev.button) {
    case Button::Up:
    case Button::Down: {
      // MORE OF THE SAME, whatever the kind. A Repeat only counts where the screen
      // asked for one; the distance and the held flag come straight off the event,
      // because the recognizer is what knows how long the button has been down.
      if (ev.kind == PressKind::Repeat && !maskHas(repeats, ev.button)) return {};
      if (ev.kind == PressKind::Long) return {};  // a hold on a mover is its repeat
      out.what = (ev.button == Button::Up) ? Gesture::Prev : Gesture::Next;
      out.steps = ev.kind == PressKind::Repeat ? ev.steps : 1;
      out.held = ev.kind == PressKind::Repeat;
      return out;
    }
    case Button::Confirm:
      if (ev.kind == PressKind::Long) {
        // Only where the bar draws a ring. An unadvertised hold is DROPPED rather
        // than demoted to Activate: the alternative gives a user who held too long
        // an action they did not ask for, and on a list that is opening a book.
        if (!maskHas(holds, Button::Confirm)) return {};
        out.what = Gesture::Secondary;
        return out;
      }
      if (ev.kind == PressKind::Repeat) return {};  // Confirm does not repeat
      out.what = Gesture::Activate;
      return out;
    case Button::Back:
      if (ev.kind != PressKind::Short) {
        // A held Back is dropped for the same reason, and it matters more here:
        // Back is the button a user leans on when a screen is not responding.
        if (ev.kind == PressKind::Long && maskHas(holds, Button::Back)) {
          out.what = Gesture::Secondary;
          return out;
        }
        return {};
      }
      out.what = Gesture::Back;
      return out;
    default:
      return {};
  }
}

}  // namespace reader
