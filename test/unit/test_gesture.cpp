#include <utility>

#include "doctest.h"
#include "reader/gesture.h"

using reader::Button;
using reader::buttonBit;
using reader::Gesture;
using reader::gestureFor;
using reader::InputEvent;
using reader::PressKind;

namespace {
constexpr reader::ButtonMask kConfirmHold = buttonBit(Button::Confirm);
constexpr reader::ButtonMask kMovers =
    static_cast<reader::ButtonMask>(buttonBit(Button::Up) | buttonBit(Button::Down));
}  // namespace

TEST_CASE("a press becomes the obvious intent") {
  CHECK(gestureFor({Button::Back, PressKind::Short}, 0, 0).what == Gesture::Back);
  CHECK(gestureFor({Button::Confirm, PressKind::Short}, 0, 0).what == Gesture::Activate);
  CHECK(gestureFor({Button::Up, PressKind::Short}, 0, 0).what == Gesture::Prev);
  CHECK(gestureFor({Button::Down, PressKind::Short}, 0, 0).what == Gesture::Next);
}

TEST_CASE("a press carries one step and is not held") {
  const reader::GestureEvent g = gestureFor({Button::Down, PressKind::Short}, 0, kMovers);
  CHECK(g.steps == 1);
  CHECK_FALSE(g.held);
}

TEST_CASE("a HOLD the hint bar advertises becomes Secondary") {
  const reader::GestureEvent g =
      gestureFor({Button::Confirm, PressKind::Long}, kConfirmHold, 0);
  CHECK(g.what == Gesture::Secondary);
}

TEST_CASE("a HOLD the hint bar does NOT advertise is DROPPED, not demoted") {
  // The important one. Demoting it to Activate gives a user who held too long an
  // action they never asked for -- on a list, opening a book instead of its
  // actions panel. Six screens used to carry a guard for this and any one of them
  // could have forgotten it.
  const reader::GestureEvent g = gestureFor({Button::Confirm, PressKind::Long}, 0, 0);
  CHECK(g.what == Gesture::None);
}

TEST_CASE("a held Up or Down carries its distance and says it was held") {
  InputEvent ev{Button::Down, PressKind::Repeat};
  ev.steps = 17;
  const reader::GestureEvent g = gestureFor(ev, 0, kMovers);
  CHECK(g.what == Gesture::Next);
  CHECK(g.steps == 17);
  CHECK(g.held);
}

TEST_CASE("a repeat on a button that does not repeat is dropped") {
  InputEvent ev{Button::Down, PressKind::Repeat};
  ev.steps = 9;
  CHECK(gestureFor(ev, 0, 0).what == Gesture::None);
}

TEST_CASE("Confirm never repeats, however it arrives") {
  InputEvent ev{Button::Confirm, PressKind::Repeat};
  ev.steps = 4;
  CHECK(gestureFor(ev, kConfirmHold, kMovers).what == Gesture::None);
}

TEST_CASE("a hold on a MOVER is not a Secondary, whatever the bar says") {
  // Up and Down mean "more of the same" when held, and their held form is a
  // Repeat. A Long on one is the recognizer and the masks disagreeing, and turning
  // it into a Secondary would fire a context action from a scroll.
  CHECK(gestureFor({Button::Down, PressKind::Long}, kMovers, kMovers).what == Gesture::None);
  CHECK(gestureFor({Button::Up, PressKind::Long}, kMovers, kMovers).what == Gesture::None);
}

TEST_CASE("a held Back is dropped unless the bar rings it") {
  // Back is the button a user leans on when a screen seems unresponsive, so a
  // held Back must not do something extra by accident.
  CHECK(gestureFor({Button::Back, PressKind::Long}, 0, 0).what == Gesture::None);
  CHECK(gestureFor({Button::Back, PressKind::Long}, buttonBit(Button::Back), 0).what ==
        Gesture::Secondary);
}

// --- The side buttons --------------------------------------------------------
//
// Left and Right are the two SIDE buttons (shell/src/main.cpp maps the SDK's BTN_UP
// and BTN_DOWN onto them). They produced no gesture at all until page turns needed
// them, so the sides did nothing anywhere on the device -- and nothing here covered
// that, which is why extending the mapping broke no test.

TEST_CASE("the side buttons are movers, like the front-right pair") {
  CHECK(gestureFor({Button::Left, PressKind::Short}, 0, 0).what == Gesture::Prev);
  CHECK(gestureFor({Button::Right, PressKind::Short}, 0, 0).what == Gesture::Next);
}

TEST_CASE("a side button means exactly what the front button it matches means") {
  // ONE code path, asserted as such: a side press and the front press it mirrors must
  // agree on every field, or the sides have quietly become a second mover with their
  // own repeat gating and their own held flag.
  constexpr reader::ButtonMask kAll = static_cast<reader::ButtonMask>(
      buttonBit(Button::Up) | buttonBit(Button::Down) | buttonBit(Button::Left) |
      buttonBit(Button::Right));
  for (const PressKind k : {PressKind::Short, PressKind::Long, PressKind::Repeat}) {
    for (const auto pair : {std::pair{Button::Up, Button::Left},
                            std::pair{Button::Down, Button::Right}}) {
      const reader::GestureEvent front = gestureFor({pair.first, k, 3}, 0, kAll);
      const reader::GestureEvent side = gestureFor({pair.second, k, 3}, 0, kAll);
      CHECK(front.what == side.what);
      CHECK(front.steps == side.steps);
      CHECK(front.held == side.held);
    }
  }
}

TEST_CASE("a held side button is dropped where the screen asked for no repeat") {
  // Which is the Reader: it declares no auto-repeat, so a held side button resolves
  // as Long and turns exactly one page. On a panel that costs ~570 ms a repaint, a
  // side button that ran away would be a defect rather than a convenience.
  CHECK(gestureFor({Button::Right, PressKind::Long}, 0, 0).what == Gesture::None);
  CHECK(gestureFor({Button::Left, PressKind::Long}, 0, 0).what == Gesture::None);
  CHECK(gestureFor({Button::Right, PressKind::Repeat, 4}, 0, 0).what == Gesture::None);
}

TEST_CASE("a held side button repeats where the screen DID ask for one") {
  constexpr reader::ButtonMask kSides =
      static_cast<reader::ButtonMask>(buttonBit(Button::Left) | buttonBit(Button::Right));
  const reader::GestureEvent g = gestureFor({Button::Right, PressKind::Repeat, 7}, 0, kSides);
  CHECK(g.what == Gesture::Next);
  CHECK(g.steps == 7);
  CHECK(g.held);
}
