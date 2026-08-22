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

TEST_CASE("a hold resolves from the release edge when tick never got to run") {
  // The real sequence this protects: the user holds Confirm on a list item while
  // the panel is mid-repaint. A gray refresh blocks loop() for ~1.5 s, so tick()
  // is not called for the whole press -- but the input task sampled both edges
  // at their true times, so the release alone is enough to classify it.
  //
  // Without this, the press classifies as Short and the wrong thing happens: on
  // Library, a hold opens the item-actions overlay and a press OPENS THE BOOK.
  // Silently doing the other action is much worse than doing it late.
  PressRecognizer r;
  r.setLongPressable(buttonBit(Button::Confirm));
  r.sample(Button::Confirm, true, 1000);
  r.sample(Button::Confirm, false, 1800);  // 800 ms, and not one tick() between
  Drain d(r);
  REQUIRE(d.n == 1);
  CHECK(d.ev[0].button == Button::Confirm);
  CHECK(d.ev[0].kind == PressKind::Long);
}

TEST_CASE("a short press with no tick in between is still Short") {
  // The other half of the same rule: the release-edge path must classify by
  // elapsed time, not simply assume any un-ticked press was a hold.
  PressRecognizer r;
  r.setLongPressable(buttonBit(Button::Confirm));
  r.sample(Button::Confirm, true, 1000);
  r.sample(Button::Confirm, false, 1000 + kLongPressMs - 1);
  Drain d(r);
  REQUIRE(d.n == 1);
  CHECK(d.ev[0].kind == PressKind::Short);
}

TEST_CASE("the release edge never doubles a hold that tick already fired") {
  // One physical press is still exactly one event: the release path must respect
  // the consumed latch, or a hold spanning a tick AND a long release would emit
  // Long twice.
  PressRecognizer r;
  r.setLongPressable(buttonBit(Button::Confirm));
  r.sample(Button::Confirm, true, 1000);
  r.tick(1600);
  REQUIRE(Drain(r).n == 1);
  r.sample(Button::Confirm, false, 3000);
  CHECK(Drain(r).n == 0);
}

TEST_CASE("a long release on a button with no hold bound is still Short") {
  PressRecognizer r;
  r.setLongPressable(buttonBit(Button::Confirm));
  r.sample(Button::Up, true, 1000);
  r.sample(Button::Up, false, 9000);
  Drain d(r);
  REQUIRE(d.n == 1);
  CHECK(d.ev[0].kind == PressKind::Short);
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

// --- Recovering from a dropped raw edge -------------------------------------
//
// Review finding 4. `shell/src/input_task.cpp` drops raw edges when its 32-deep
// queue fills, which a paint makes possible because the loop that drains it is
// blocked for the paint's whole duration (~825 ms on a FULL refresh, and longer
// for anything Phase 3 puts in front of it).
//
// A dropped PRESS is harmless -- nothing happened, and nothing is claimed. A
// dropped RELEASE is not: it leaves the recognizer's state latched `down`, and
// from there two separate wrong things follow. `forgetPresses()` is the seam that
// lets the shell say "I no longer know what is held", which is the truth after a
// drop, and losing a press is strictly better than inventing one the user never
// made.

TEST_CASE("a dropped release would otherwise fire a Long the user never made") {
  reader::PressRecognizer r;
  r.setLongPressable(reader::buttonBit(reader::Button::Confirm));

  r.sample(reader::Button::Confirm, true, 1000);
  // The release at 1050 is DROPPED -- the shell never calls sample() for it.
  // Without the forget, tick() past the long-press threshold invents a Long.
  r.forgetPresses();
  r.tick(1000 + reader::kLongPressMs + 50);

  reader::InputEvent e{};
  CHECK_FALSE(r.pop(e));
}

TEST_CASE("a dropped release does not make the NEXT short press a Long") {
  reader::PressRecognizer r;
  r.setLongPressable(reader::buttonBit(reader::Button::Confirm));

  r.sample(reader::Button::Confirm, true, 1000);  // release dropped
  r.forgetPresses();

  // A genuine short press much later. Without the forget, the stale downAt of
  // 1000 makes this release classify as Long -- on a list, that is "open the
  // actions overlay" instead of "open the item".
  r.sample(reader::Button::Confirm, true, 9000);
  r.sample(reader::Button::Confirm, false, 9040);

  reader::InputEvent e{};
  REQUIRE(r.pop(e));
  CHECK(e.button == reader::Button::Confirm);
  CHECK(e.kind == reader::PressKind::Short);
  CHECK_FALSE(r.pop(e));
}

TEST_CASE("forgetPresses keeps events already recognised") {
  // The drop invalidates what is HELD, not what already happened. An event that
  // was recognised before the queue overflowed is a press the user really made.
  reader::PressRecognizer r;
  r.sample(reader::Button::Down, true, 100);
  r.sample(reader::Button::Down, false, 140);
  r.forgetPresses();

  reader::InputEvent e{};
  REQUIRE(r.pop(e));
  CHECK(e.button == reader::Button::Down);
  CHECK(e.kind == reader::PressKind::Short);
}

TEST_CASE("a real release arriving after a forget is ignored, not misread") {
  // The other order: we forgot, but the button was genuinely still down and its
  // release does arrive. A release with no matching press must emit nothing --
  // the same rule that makes a wake-button release on a fresh boot silent.
  reader::PressRecognizer r;
  r.sample(reader::Button::Back, true, 100);
  r.forgetPresses();
  r.sample(reader::Button::Back, false, 200);

  reader::InputEvent e{};
  CHECK_FALSE(r.pop(e));
}

// --- Held-scroll acceleration -----------------------------------------------
//
// A held Up or Down asks for MORE OF THE SAME, which is a third kind of press
// alongside Short and Long. What makes it unusual is that the step comes from
// ELAPSED TIME rather than from a count of events: tick() only runs from the main
// loop and a paint blocks that loop for 520-825 ms, so a scheme that moved one row
// per repeat would move about two rows a second whatever interval it asked for --
// 256 books in over a minute. These tests pin the time-based behaviour, because
// the tick-based version passes any test that ticks in a tight loop.

namespace {
reader::PressRecognizer scroller() {
  reader::PressRecognizer r;
  r.setAutoRepeat(reader::buttonBit(reader::Button::Down));
  return r;
}
// Total rows a run of ticks asks for.
int drainSteps(reader::PressRecognizer& r) {
  int total = 0;
  reader::InputEvent e{};
  while (r.pop(e))
    if (e.kind == reader::PressKind::Repeat) total += e.steps;
  return total;
}
}  // namespace

TEST_CASE("a tap shorter than the delay is still exactly one Short") {
  reader::PressRecognizer r = scroller();
  r.sample(reader::Button::Down, true, 1000);
  r.tick(1000 + reader::kRepeatDelayMs - 50);
  reader::InputEvent e{};
  CHECK_FALSE(r.pop(e));  // nothing yet: the delay is what separates hold from tap
  r.sample(reader::Button::Down, false, 1000 + reader::kRepeatDelayMs - 40);
  REQUIRE(r.pop(e));
  CHECK(e.kind == reader::PressKind::Short);
  CHECK(e.steps == 1);
  CHECK_FALSE(r.pop(e));
}

TEST_CASE("a hold past the delay repeats, and the release adds nothing") {
  reader::PressRecognizer r = scroller();
  r.sample(reader::Button::Down, true, 1000);
  r.tick(1000 + reader::kRepeatDelayMs + 500);
  const int moved = drainSteps(r);
  CHECK(moved >= 1);

  // The repeats WERE the press. A trailing Short here would move the list one
  // further row after the user let go.
  r.sample(reader::Button::Down, false, 1000 + reader::kRepeatDelayMs + 520);
  reader::InputEvent e{};
  CHECK_FALSE(r.pop(e));
}

TEST_CASE("holding longer scrolls faster") {
  // The same elapsed slice, early in the hold and late in it. Late must ask for
  // more rows, or the ramp does not exist.
  const uint32_t slice = 300;
  int early = 0, late = 0;
  {
    reader::PressRecognizer r = scroller();
    r.sample(reader::Button::Down, true, 0);
    r.tick(reader::kRepeatDelayMs);
    drainSteps(r);
    r.tick(reader::kRepeatDelayMs + slice);
    early = drainSteps(r);
  }
  {
    reader::PressRecognizer r = scroller();
    r.sample(reader::Button::Down, true, 0);
    r.tick(reader::kRepeatDelayMs + reader::kRepeatRampMs);
    drainSteps(r);
    r.tick(reader::kRepeatDelayMs + reader::kRepeatRampMs + slice);
    late = drainSteps(r);
  }
  CHECK(late > early);
  // And the fast end is roughly the declared rate.
  CHECK(late >= (reader::kRepeatFastRowsPerSec * static_cast<int>(slice)) / 1000 - 1);
}

TEST_CASE("ONE tick after a long paint owes all the rows the paint cost") {
  // The whole reason the step is time-based. A single tick arriving 825 ms late --
  // one FULL refresh -- must account for that time, not for one interval.
  reader::PressRecognizer r = scroller();
  r.sample(reader::Button::Down, true, 0);
  r.tick(reader::kRepeatDelayMs);      // repeats begin
  drainSteps(r);
  r.tick(reader::kRepeatDelayMs + 825);  // the next tick the loop could manage
  const int owed = drainSteps(r);
  CHECK(owed >= 4);  // a tick-per-row scheme would have said 1
}

TEST_CASE("no single event can cash in an unbounded stall") {
  // A five-second block -- a grayscale screen, a FAT scan -- must not arrive as a
  // 150-row jump.
  reader::PressRecognizer r = scroller();
  r.sample(reader::Button::Down, true, 0);
  r.tick(reader::kRepeatDelayMs);
  drainSteps(r);
  r.tick(reader::kRepeatDelayMs + 5000);
  reader::InputEvent e{};
  while (r.pop(e)) CHECK(e.steps <= reader::kRepeatMaxSteps);
}

TEST_CASE("a button outside the mask never repeats, however long it is held") {
  reader::PressRecognizer r = scroller();  // Down repeats, Up does not
  r.sample(reader::Button::Up, true, 0);
  r.tick(10000);
  reader::InputEvent e{};
  CHECK_FALSE(r.pop(e));
  r.sample(reader::Button::Up, false, 10010);
  REQUIRE(r.pop(e));
  CHECK(e.kind == reader::PressKind::Short);
}

TEST_CASE("auto-repeat and long-press are mutually exclusive, and enforced") {
  // A button in both masks would behave according to whichever fired first --
  // which depends on how long it was held and when the loop happened to tick.
  reader::PressRecognizer r;
  r.setLongPressable(static_cast<reader::ButtonMask>(
      reader::buttonBit(reader::Button::Down) | reader::buttonBit(reader::Button::Confirm)));
  r.setAutoRepeat(reader::buttonBit(reader::Button::Down));

  CHECK(reader::maskHas(r.autoRepeat(), reader::Button::Down));
  // Down lost its long-press bit...
  CHECK_FALSE(reader::maskHas(r.longPressable(), reader::Button::Down));
  // ...and Confirm kept its.
  CHECK(reader::maskHas(r.longPressable(), reader::Button::Confirm));

  // Behaviourally: holding Down repeats and never emits Long.
  r.sample(reader::Button::Down, true, 0);
  r.tick(reader::kLongPressMs + 1000);
  reader::InputEvent e{};
  bool sawLong = false, sawRepeat = false;
  while (r.pop(e)) {
    if (e.kind == reader::PressKind::Long) sawLong = true;
    if (e.kind == reader::PressKind::Repeat) sawRepeat = true;
  }
  CHECK(sawRepeat);
  CHECK_FALSE(sawLong);
}

TEST_CASE("a fraction of a row is kept rather than rounded away every tick") {
  // At the slow end a 100ms tick is well under one row. Ten of them must still
  // produce movement -- a scheme that truncated each tick to zero would never
  // move at all, which is the bug this accumulation exists to prevent.
  reader::PressRecognizer r = scroller();
  r.sample(reader::Button::Down, true, 0);
  int total = 0;
  for (int i = 1; i <= 10; ++i) {
    r.tick(reader::kRepeatDelayMs + static_cast<uint32_t>(i) * 100u);
    total += drainSteps(r);
  }
  CHECK(total >= (reader::kRepeatSlowRowsPerSec * 1000) / 1000 - 1);
  CHECK(total > 0);
}

TEST_CASE("forgetPresses stops a repeat dead") {
  // A dropped release during held scrolling would otherwise leave the list
  // scrolling on its own.
  reader::PressRecognizer r = scroller();
  r.sample(reader::Button::Down, true, 0);
  r.tick(reader::kRepeatDelayMs + 500);
  drainSteps(r);
  r.forgetPresses();
  r.tick(reader::kRepeatDelayMs + 2000);
  reader::InputEvent e{};
  CHECK_FALSE(r.pop(e));
}
