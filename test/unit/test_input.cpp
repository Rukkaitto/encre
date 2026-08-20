#include "doctest.h"
#include "reader/input.h"

using namespace reader;

namespace {
// Drain the queue into a vector-ish fixed array for readable assertions.
struct Drain {
  InputEvent ev[8];
  int n = 0;
  explicit Drain(PressRecognizer& r) {
    InputEvent e{};
    while (n < 8 && r.pop(e)) ev[n++] = e;
  }
};
}  // namespace

TEST_CASE("a quick press fires one Short on release") {
  PressRecognizer r;
  r.sample(Button::Confirm, true, 1000);
  r.tick(1050);
  CHECK(Drain(r).n == 0);  // nothing while still down
  r.sample(Button::Confirm, false, 1100);
  Drain d(r);
  REQUIRE(d.n == 1);
  CHECK(d.ev[0].button == Button::Confirm);
  CHECK(d.ev[0].kind == PressKind::Short);
}

TEST_CASE("a hold on a long-pressable button fires Long while still down, and the release fires nothing") {
  PressRecognizer r;
  r.setLongPressable(buttonBit(Button::Confirm));
  r.sample(Button::Confirm, true, 1000);
  r.tick(1000 + kLongPressMs - 1);
  CHECK(Drain(r).n == 0);
  r.tick(1000 + kLongPressMs);
  Drain fired(r);
  REQUIRE(fired.n == 1);
  CHECK(fired.ev[0].kind == PressKind::Long);
  // Still held: no repeats.
  r.tick(1000 + kLongPressMs + 5000);
  CHECK(Drain(r).n == 0);
  // The release must not also produce a Short.
  r.sample(Button::Confirm, false, 9000);
  CHECK(Drain(r).n == 0);
}

TEST_CASE("a hold on a button with no long action still fires Short on release") {
  PressRecognizer r;
  r.setLongPressable(buttonBit(Button::Confirm));  // Up is NOT in the mask
  r.sample(Button::Up, true, 1000);
  r.tick(1000 + kLongPressMs * 10);
  CHECK(Drain(r).n == 0);
  r.sample(Button::Up, false, 1000 + kLongPressMs * 10);
  Drain d(r);
  REQUIRE(d.n == 1);
  CHECK(d.ev[0].button == Button::Up);
  CHECK(d.ev[0].kind == PressKind::Short);
}

TEST_CASE("the mask changing mid-hold does not resurrect a consumed press") {
  // The real sequence: hold Confirm, Long fires, the screen pushes a screen whose
  // Confirm has no hold, and only THEN does the finger come up.
  PressRecognizer r;
  r.setLongPressable(buttonBit(Button::Confirm));
  r.sample(Button::Confirm, true, 1000);
  r.tick(1000 + kLongPressMs);
  REQUIRE(Drain(r).n == 1);
  r.setLongPressable(0);
  r.sample(Button::Confirm, false, 2000);
  CHECK(Drain(r).n == 0);
}

TEST_CASE("two buttons held at once classify independently") {
  PressRecognizer r;
  r.setLongPressable(buttonBit(Button::Confirm));
  r.sample(Button::Confirm, true, 1000);
  r.sample(Button::Up, true, 1010);
  r.tick(1000 + kLongPressMs);
  Drain first(r);
  REQUIRE(first.n == 1);
  CHECK(first.ev[0].button == Button::Confirm);
  CHECK(first.ev[0].kind == PressKind::Long);
  r.sample(Button::Up, false, 1600);
  Drain second(r);
  REQUIRE(second.n == 1);
  CHECK(second.ev[0].button == Button::Up);
  CHECK(second.ev[0].kind == PressKind::Short);
}

TEST_CASE("a hold measured across a millisecond-clock wrap still fires") {
  // millis() is uint32 and rolls over about every 49 days. Unsigned subtraction
  // gets this right; a signed comparison would fire a spurious Long on the very
  // first tick after a press that straddles the rollover.
  PressRecognizer r;
  r.setLongPressable(buttonBit(Button::Confirm));
  const uint32_t before = 0xFFFFFF00u;
  r.sample(Button::Confirm, true, before);
  r.tick(before + 100);  // wrapped past zero, 100 ms elapsed
  CHECK(Drain(r).n == 0);
  r.tick(before + kLongPressMs);
  Drain d(r);
  REQUIRE(d.n == 1);
  CHECK(d.ev[0].kind == PressKind::Long);
}

TEST_CASE("a repeated down with no release does not restart the hold clock") {
  // The shell queues edges, but a dropped release or a debounce artefact can
  // deliver two downs. The second must not push the threshold out of reach.
  PressRecognizer r;
  r.setLongPressable(buttonBit(Button::Confirm));
  r.sample(Button::Confirm, true, 1000);
  r.sample(Button::Confirm, true, 1400);
  r.tick(1000 + kLongPressMs);
  Drain d(r);
  REQUIRE(d.n == 1);
  CHECK(d.ev[0].kind == PressKind::Long);
}

TEST_CASE("a release with no matching down is ignored") {
  PressRecognizer r;
  r.sample(Button::Back, false, 1000);
  CHECK(Drain(r).n == 0);
}

TEST_CASE("a full queue drops the newest and counts it") {
  // Dropping the newest preserves causality: the events that survive are still
  // in the order they happened. Dropping the oldest would silently reorder.
  PressRecognizer r;
  for (int i = 0; i < 20; ++i) {
    r.sample(Button::Down, true, 1000u + i * 10);
    r.sample(Button::Down, false, 1005u + i * 10);
  }
  int n = 0;
  InputEvent e{};
  while (r.pop(e)) ++n;
  CHECK(n == 16);
  CHECK(r.dropped() == 4);
}
