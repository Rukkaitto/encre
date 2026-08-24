#include "reader/gesture.h"

namespace reader {

GestureEvent gestureFor(const InputEvent& ev, ButtonMask holds, ButtonMask repeats,
                        bool splitMovers) {
  GestureEvent out;

  switch (ev.button) {
    // FOUR MOVERS, NOT TWO: the front-right pair AND the two side buttons.
    //
    // Left and Right are the SIDE buttons (shell/src/main.cpp maps the SDK's BTN_UP
    // and BTN_DOWN onto them -- the SDK's names describe its band order, not this
    // device's panel). They fell into the default below and produced NO gesture at
    // all, so the sides did nothing anywhere: spec 4.0 puts page turns on them, and
    // the shell's own comment said "the sides turn pages in the Reader (Phase 3) and
    // do nothing before it -- verify when page turns land". Page turns landed and
    // this did not.
    //
    // ONE CODE PATH, not a second one beside it. A side button is a mover exactly as
    // Up and Down are, so it inherits their repeat gating, their held flag and their
    // dropped-Long rule rather than getting its own spelling of each. The Reader
    // declares no auto-repeat, so a held side button resolves as Long and is dropped
    // here -- one page per press, which is what a page turn should be on a panel that
    // costs ~570 ms a repaint.
    //
    // THE CONSEQUENCE ELSEWHERE IS DELIBERATE: on a list the sides move the focus,
    // like Up and Down. They advertise nothing -- the hint bar has four slots for the
    // four FRONT buttons and the sides are not in it -- but an unadvertised button
    // that does the obvious thing is not a broken promise, and it is what a hand
    // reaches for.
    case Button::Left:
    case Button::Right:
    case Button::Up:
    case Button::Down: {
      // MORE OF THE SAME, whatever the kind. A Repeat only counts where the screen
      // asked for one; the distance and the held flag come straight off the event,
      // because the recognizer is what knows how long the button has been down.
      if (ev.kind == PressKind::Repeat && !maskHas(repeats, ev.button)) return {};
      if (ev.kind == PressKind::Long) return {};  // a hold on a mover is its repeat
      // BACKWARD is Up and the LEFT side; forward is Down and the right side. Which
      // physical side is which is stated by the shell's mapping and was never
      // verified against behaviour, because until now there was none to verify it
      // against -- if the sides turn pages the wrong way round on glass, the fix is
      // the two BTN_UP/BTN_DOWN lines in shell/src/main.cpp, not this.
      // ON A SPLIT SCREEN the front row keeps its own identity. `Button::Up`/`Down`
      // ARE the front row -- the shell's mapping is crossed, so read it rather than
      // the names -- and they arrive as AltPrev/AltNext. The sides still page.
      if (splitMovers) {
        out.what = ev.button == Button::Up      ? Gesture::AltPrev
                   : ev.button == Button::Down  ? Gesture::AltNext
                   : ev.button == Button::Left  ? Gesture::Prev
                                                : Gesture::Next;
      } else {
        out.what = (ev.button == Button::Up || ev.button == Button::Left) ? Gesture::Prev
                                                                         : Gesture::Next;
      }
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
