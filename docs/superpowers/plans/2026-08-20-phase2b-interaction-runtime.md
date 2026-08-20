# Phase 2B — Interaction Runtime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the buttons work — short/long press recognition, a screen stack you
can navigate on device, an e-ink refresh policy, and clean sleep/wake.

**Architecture:** The runtime is *logic*, so it lives in `core/` and is unit-tested
on the desktop: a press recognizer, a screen stack, a refresh-cadence policy and an
idle timer, none of which touch Arduino. The shell contributes only the three things
that genuinely need hardware — raw button samples with timestamps (from a FreeRTOS
task, because an e-ink refresh blocks the main loop for up to two seconds), the panel
refresh calls, and deep sleep. The simulator drives the same `App` from scripted key
strings, so navigation is regression-tested as PNGs rather than by hand.

**Tech Stack:** C++20, doctest, CMake (desktop) / PlatformIO + Arduino-ESP32
(device), freeink-sdk `InputManager` / `FreeInkDisplay` / `PowerManager`.

---

## Design decisions this plan locks in

Read these before writing code. Each one is a call that was made deliberately, and
several are load-bearing for later phases.

### 1. One field drives both the hold ring and the long-press binding

`design 662557d` gave a hint slot a hollow ring meaning "this button also has a
long-press action". That is simultaneously **an affordance** (the theme draws a ring)
and **a behaviour** (the recognizer may fire `Long` for that button). If those come
from two declarations they will drift, and a screen will either promise a hold it
does not have or hide one it does.

So the view-model gains `std::array<bool,4> holds` next to its existing
`std::array<std::string,4> hints`. The theme reads it to set `Hint::hasHold`; the
screen reads it to build its long-press mask. Slot order is the fixed hardware order
**Back, Confirm, Up, Down** (spec §4.0).

### 2. A button with no long action still fires on release

If a button is outside the long-press mask, holding it emits `Short` **on release**,
however long it was held. Never nothing. A user who presses slowly must not be
punished for it.

Conversely, a button *inside* the mask fires `Long` **while still down**, at the
threshold — the SDK's own touch long-press does this, and for the same reason: a hold
that only resolves on release feels broken. The release that follows a fired `Long`
emits nothing. **One physical press produces exactly one event.**

### 3. Long-press threshold is 500 ms

The SDK uses 650 ms for its synthesized button holds and 500 ms for touch
(`TOUCH_LONG_PRESS_MS`, whose comment says 650 read as sluggish where there is no
button travel to absorb it). A hold the user has to *discover* from a small ring mark
wants to be at the short end. 500 ms, as a named constant.

### 4. Screens declare a fidelity; the policy picks the refresh mode

Phase 2A-2 established that **1-bit thresholded chrome is illegible on this panel**.
So a chrome screen cannot take a cheap 1-bit `FAST_REFRESH` redraw — it must go
through the three-plane grayscale sequence every time. That makes FAST-vs-FULL
meaningless for chrome and essential for the Reader, whose body text at 14pt+ *is*
legible in 1 bit and whose page turns must be quick.

Each screen therefore declares `Fidelity::Gray` or `Fidelity::Mono`, and
`RefreshPolicy` decides `Fast` or `Full` from the cadence. Product screens are
`Gray`. The Input Monitor (below) is `Mono`, which is what proves the fast path works
on hardware during this phase rather than during Phase 3.

### 5. The screens this phase navigates between are scaffolding, on purpose

The roadmap's own exit criterion says "navigate between **stub** screens". Library,
Settings and Sleep are Phase 2C's, and inventing visuals for them here would violate
the design-first rule. So this phase adds exactly one provisional themed surface —
`renderStub`, a titled list of lines — used for a Library placeholder, a Settings
placeholder, and the Input Monitor. It carries a visible `PHASE 2C` note so nobody
can mistake it for design, it is built only from primitives already matched to
boards, and **it gets no goldens** (pinning provisional pixels would just churn).

The one genuinely new *product* state this phase renders is Home with a menu row
focused. That is not a new visual: `drawRow(..., focused, ...)` already draws the
inverted focused row the Library board specifies, and Home's own board specifies the
row. Only the focus moving there is new, and it does get goldens.

### 6. Home's hint labels do not change with focus

Tempting to make Confirm read "OPEN" when Library is focused. That would be a design
change, and design changes go in the board first. Home's board says
`READ / SELECT / UP / DOWN`; this phase renders exactly that. If the labels should
follow focus, that is a board edit and a separate change.

### 7. Sleep leaves the last screen on the panel

The Sleep screen is boarded (`design/Sleep.dc.html`) and belongs to Phase 2C. E-ink
holds its last image with no power, so sleeping without painting anything is
*correct-looking* rather than broken — the device simply keeps showing what you were
looking at. This phase logs the sleep and does not paint. 2C paints the Sleep screen
in the same place.

### 8. Deep sleep is a chip reset, and only the power button wakes it

`esp_deep_sleep_start()` resets the ESP32-C3 on wake, so **all RAM state is lost**
and the firmware boots into Home. Persisting the current screen and position needs
the settings store, which is Phase 2C.

The X3/X4 profile's `input` is `{0,1,2,3,4,5,3,false}`: the six front buttons are
*ADC-ladder band indices* multiplexed onto GPIO 1 and 2, while **power is a real GPIO
(3, active-LOW)**. A resistor-ladder button produces no GPIO edge, so the front
buttons physically cannot wake the chip from deep sleep. Wake is the power button.
This is a hardware fact, not a preference.

### 9. Focus clamps at the ends; it does not wrap

Home's menu is three items and wrapping would be pleasant there, but Library will
hold hundreds of books and a list that silently jumps from the last book to the first
is a bug the user cannot distinguish from a stuck button. One rule for every list:
clamp.

---

## File structure

**New in `core/` (portable, no Arduino):**

| File | Responsibility |
|---|---|
| `core/include/reader/input.h` + `core/src/input.cpp` | `Button`, `PressKind`, `InputEvent`, `ButtonMask`, `PressRecognizer`. Raw level transitions in, classified presses out. |
| `core/include/reader/refresh.h` + `core/src/refresh.cpp` | `Fidelity`, `RefreshMode`, `RefreshPolicy`. The FULL-every-N cadence. |
| `core/include/reader/power.h` + `core/src/power.cpp` | `PowerAction`, `IdleTimer`. Idle-to-sleep, one shot. |
| `core/include/reader/app.h` + `core/src/app.cpp` | `ScreenId`, `Action`, `Screen`, `ScreenFactory`, `App`, `hintHoldMask()`. The stack and the dispatch. |
| `core/include/reader/screen_home.h` + `core/src/screen_home.cpp` | `HomeScreen`: owns a `HomeViewModel`, moves focus, pushes targets. |
| `core/include/reader/screen_stub.h` + `core/src/screen_stub.cpp` | `StubScreen`: the provisional titled-list surface, focusable rows with optional push targets. |
| `core/include/reader/screen_input_monitor.h` + `core/src/screen_input_monitor.cpp` | `InputMonitorScreen`: `Mono` fidelity, logs classified events, one hold slot. |

**Modified:**

| File | Change |
|---|---|
| `core/include/reader/viewmodel.h` | `holds` on `HomeViewModel`; new `StubViewModel`. |
| `core/include/reader/theme.h` | `renderStub` virtual. |
| `core/src/theme_quiet.cpp` / `theme_quiet.h` | `holds`-driven hint bar; `renderStub` implementation. |
| `sim/main.cpp` | `app` subcommand with `--keys`. |
| `shell/src/main.cpp` | setup/loop split, app wiring, two paint paths, sleep. |
| `shell/src/input_task.h` + `.cpp` (new) | The polling task and its queue. |
| `platformio.ini` | `PowerManager` in `lib_deps`. |
| `CMakeLists.txt` | New `add_test` for the scripted-input sim run. |

**Tests:** `test/unit/test_input.cpp`, `test_refresh.cpp`, `test_power.cpp`,
`test_app.cpp`, `test_screen_home.cpp`, `test_app_golden.cpp`, plus a new shared
`test/unit/home_vm.h`.

**The existing test helpers, by their real names.** Use these; do not build parallel
ones:

- `ramp::Ramp` (`test/unit/ramp.h`) — declare `Ramp r;` and use `r.fonts`. Every
  existing test does `using ramp::Ramp;`.
- `golden::checkGoldenGray(lsb, msb, "name")` (`test/unit/golden.h`) — the 4-level
  assertion. **The name carries no `.png`**; `goldenPath` appends it. There is a
  1-bit sibling `golden::checkGolden(fb, "name")`.
- Tracking constants live in `core/include/reader/components.h`: `kBandLabelEm`,
  `kRowLabelEm`, `kBlockLabelEm`, `kHintEm`, `kMetaEm`, `kTightMetaEm`.
- `sampleHome()` currently exists as a `static` inside
  `test/unit/test_theme_home_golden.cpp`. **Task 9 Step 0 extracts it** into
  `test/unit/home_vm.h` so the focused-Home golden cannot drift from the plain Home
  golden's content.

**Goldens:** `test/golden/home_focus_library.png` (480×800),
`home_focus_library_x3.png` (528×792).

> **CMake uses `file(GLOB ...)`.** Re-run `cmake -S . -B build` after adding any
> source file or it is silently ignored. `make test` does this for you.

---

## Task 1: Button events and the press recognizer

**Files:**
- Create: `core/include/reader/input.h`
- Create: `core/src/input.cpp`
- Test: `test/unit/test_input.cpp`

- [ ] **Step 1: Write the header**

`core/include/reader/input.h`:

```cpp
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
```

- [ ] **Step 2: Write the failing tests**

`test/unit/test_input.cpp`:

```cpp
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
```

- [ ] **Step 3: Run the tests and confirm they fail**

Run: `make test`
Expected: compile error — `reader/input.h` has no `input.cpp` yet, so the link
fails on `PressRecognizer::sample`.

- [ ] **Step 4: Write the implementation**

`core/src/input.cpp`:

```cpp
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
```

- [ ] **Step 5: Run the tests and confirm they pass**

Run: `make test`
Expected: all cases pass, including the nine new ones.

- [ ] **Step 6: Commit**

```bash
git add core/include/reader/input.h core/src/input.cpp test/unit/test_input.cpp
git commit -m "feat(input): classify raw button transitions into short and long presses"
```

---

## Task 2: The refresh policy

**Files:**
- Create: `core/include/reader/refresh.h`, `core/src/refresh.cpp`
- Test: `test/unit/test_refresh.cpp`

- [ ] **Step 1: Write the header**

`core/include/reader/refresh.h`:

```cpp
#pragma once
#include <cstdint>

namespace reader {

// How a screen must be painted. This is not a style choice: Phase 2A-2 measured
// 1-bit thresholded chrome as illegible on this panel, so a chrome screen has to
// go through the three-plane grayscale sequence every single time and cannot
// take a cheap 1-bit refresh. Body text at 14pt and up IS legible in 1 bit,
// which is what makes the Reader's fast page turns possible.
enum class Fidelity : uint8_t {
  Gray,  // three planes, ~1.5 s. Every product chrome screen.
  Mono,  // one thresholded plane, ~0.4 s. The Reader (Phase 3) and diagnostics.
};

enum class RefreshMode : uint8_t { Fast, Full };

// FULL every N refreshes, and always on a screen transition (spec 3.3).
//
// Why a cadence at all: a differential (FAST) e-ink update leaves a little of
// the previous frame behind each time, and the residue accumulates into visible
// ghosting. A periodic FULL clears it. N is a setting because the right value is
// a taste trade -- more FULLs is cleaner and slower.
class RefreshPolicy {
 public:
  // `cadence` is the number of refreshes between FULLs; the default matches the
  // spec's 15. A cadence of 1 or less means every refresh is FULL.
  explicit RefreshPolicy(int cadence = 15) : cadence_(cadence) {}

  void setCadence(int cadence) { cadence_ = cadence; }
  int cadence() const { return cadence_; }

  // Ask for the next refresh's mode and account for it. A transition is always
  // FULL and resets the count: the screen is changing completely, so there is
  // nothing for a differential update to be differential against.
  RefreshMode next(bool transition);

  // Refreshes since the last FULL. Exposed so the shell can log it.
  int sinceFull() const { return sinceFull_; }

 private:
  int cadence_;
  int sinceFull_ = 0;
};

}  // namespace reader
```

- [ ] **Step 2: Write the failing tests**

`test/unit/test_refresh.cpp`:

```cpp
#include "doctest.h"
#include "reader/refresh.h"

using namespace reader;

TEST_CASE("a transition is always full and resets the cadence") {
  RefreshPolicy p(15);
  CHECK(p.next(true) == RefreshMode::Full);
  CHECK(p.sinceFull() == 0);
  for (int i = 0; i < 5; ++i) CHECK(p.next(false) == RefreshMode::Fast);
  CHECK(p.sinceFull() == 5);
  CHECK(p.next(true) == RefreshMode::Full);
  CHECK(p.sinceFull() == 0);
}

TEST_CASE("cadence N gives N-1 fast refreshes then a full one") {
  RefreshPolicy p(4);
  CHECK(p.next(false) == RefreshMode::Fast);
  CHECK(p.next(false) == RefreshMode::Fast);
  CHECK(p.next(false) == RefreshMode::Fast);
  CHECK(p.next(false) == RefreshMode::Full);
  CHECK(p.sinceFull() == 0);
  // and the pattern repeats, rather than sticking on Full
  CHECK(p.next(false) == RefreshMode::Fast);
}

TEST_CASE("a cadence of one or less makes every refresh full") {
  for (const int c : {1, 0, -3}) {
    RefreshPolicy p(c);
    CHECK(p.next(false) == RefreshMode::Full);
    CHECK(p.next(false) == RefreshMode::Full);
  }
}

TEST_CASE("the default cadence is the spec's fifteen") {
  RefreshPolicy p;
  CHECK(p.cadence() == 15);
  for (int i = 0; i < 14; ++i) CHECK(p.next(false) == RefreshMode::Fast);
  CHECK(p.next(false) == RefreshMode::Full);
}
```

- [ ] **Step 3: Run and confirm failure**

Run: `make test`
Expected: link error on `RefreshPolicy::next`.

- [ ] **Step 4: Implement**

`core/src/refresh.cpp`:

```cpp
#include "reader/refresh.h"

namespace reader {

RefreshMode RefreshPolicy::next(bool transition) {
  if (transition || cadence_ <= 1) {
    sinceFull_ = 0;
    return RefreshMode::Full;
  }
  if (++sinceFull_ >= cadence_) {
    sinceFull_ = 0;
    return RefreshMode::Full;
  }
  return RefreshMode::Fast;
}

}  // namespace reader
```

- [ ] **Step 5: Run and confirm passing**

Run: `make test`
Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add core/include/reader/refresh.h core/src/refresh.cpp test/unit/test_refresh.cpp
git commit -m "feat(refresh): full-every-N cadence with full on every transition"
```

---

## Task 3: The idle-to-sleep timer

**Files:**
- Create: `core/include/reader/power.h`, `core/src/power.cpp`
- Test: `test/unit/test_power.cpp`

- [ ] **Step 1: Write the header**

`core/include/reader/power.h`:

```cpp
#pragma once
#include <cstdint>

namespace reader {

enum class PowerAction : uint8_t { None, Sleep };

// Idle-to-sleep. Fed a millisecond clock and told when the user did something.
//
// Sleep fires ONCE per idle period, not on every tick after the timeout: on the
// device the action is a deep sleep that never returns, but the simulator and
// the tests call tick() in a loop, and a timer that keeps saying Sleep hides
// whether the shell is honouring the first one.
class IdleTimer {
 public:
  // `timeoutMs` of 0 disables sleeping entirely (a setting the user can pick,
  // and the right behaviour while a transfer is running).
  explicit IdleTimer(uint32_t timeoutMs) : timeoutMs_(timeoutMs) {}

  void setTimeout(uint32_t timeoutMs) { timeoutMs_ = timeoutMs; }
  uint32_t timeout() const { return timeoutMs_; }

  // The user pressed something. Also re-arms a timer that already fired.
  void noteActivity(uint32_t ms);

  PowerAction tick(uint32_t ms);

 private:
  uint32_t timeoutMs_;
  uint32_t lastActivity_ = 0;
  bool armed_ = true;
};

}  // namespace reader
```

- [ ] **Step 2: Write the failing tests**

`test/unit/test_power.cpp`:

```cpp
#include "doctest.h"
#include "reader/power.h"

using namespace reader;

TEST_CASE("no activity for the timeout asks for sleep") {
  IdleTimer t(60000);
  t.noteActivity(1000);
  CHECK(t.tick(60000) == PowerAction::None);
  CHECK(t.tick(61000) == PowerAction::Sleep);
}

TEST_CASE("activity resets the countdown") {
  IdleTimer t(60000);
  t.noteActivity(1000);
  CHECK(t.tick(50000) == PowerAction::None);
  t.noteActivity(50000);
  CHECK(t.tick(100000) == PowerAction::None);  // only 50 s since activity
  CHECK(t.tick(110000) == PowerAction::Sleep);
}

TEST_CASE("sleep is asked for once, not on every tick after the timeout") {
  IdleTimer t(1000);
  t.noteActivity(0);
  CHECK(t.tick(1000) == PowerAction::Sleep);
  CHECK(t.tick(1001) == PowerAction::None);
  CHECK(t.tick(99999) == PowerAction::None);
  // ...until the user does something, which re-arms it
  t.noteActivity(100000);
  CHECK(t.tick(101000) == PowerAction::Sleep);
}

TEST_CASE("a zero timeout never sleeps") {
  IdleTimer t(0);
  t.noteActivity(0);
  CHECK(t.tick(0xFFFFFFFFu) == PowerAction::None);
}

TEST_CASE("an idle period measured across a millisecond-clock wrap still sleeps") {
  IdleTimer t(1000);
  const uint32_t before = 0xFFFFFF00u;
  t.noteActivity(before);
  CHECK(t.tick(before + 500) == PowerAction::None);
  CHECK(t.tick(before + 1000) == PowerAction::Sleep);
}
```

- [ ] **Step 3: Run and confirm failure**

Run: `make test`
Expected: link error on `IdleTimer::tick`.

- [ ] **Step 4: Implement**

`core/src/power.cpp`:

```cpp
#include "reader/power.h"

namespace reader {

void IdleTimer::noteActivity(uint32_t ms) {
  lastActivity_ = ms;
  armed_ = true;
}

PowerAction IdleTimer::tick(uint32_t ms) {
  if (timeoutMs_ == 0 || !armed_) return PowerAction::None;
  // Unsigned subtraction, so a wrapped clock still measures the true interval.
  if (static_cast<uint32_t>(ms - lastActivity_) < timeoutMs_) return PowerAction::None;
  armed_ = false;
  return PowerAction::Sleep;
}

}  // namespace reader
```

- [ ] **Step 5: Run and confirm passing**

Run: `make test`

- [ ] **Step 6: Commit**

```bash
git add core/include/reader/power.h core/src/power.cpp test/unit/test_power.cpp
git commit -m "feat(power): one-shot idle-to-sleep timer"
```

---

## Task 4: `holds` on the view-models, and the theme reading it

**Files:**
- Create: `test/unit/home_vm.h`
- Modify: `core/include/reader/viewmodel.h`
- Modify: `core/src/theme_quiet.cpp` (the hardcoded `false` hint array, currently
  line 184 — **match on the `const Hint hints[4] = {` text, not the line number**)
- Modify: `test/unit/test_theme_home_golden.cpp` (use the shared view-model)
- Test: `test/unit/test_components.cpp` (add one case)

This is the change that makes the hold ring and the long-press binding come from
one place. Do it before the screens so they have something to read.

- [ ] **Step 0: Extract `sampleHome()` into a shared header**

`sampleHome()` is a `static` inside `test/unit/test_theme_home_golden.cpp:15`. Move it
verbatim into a new `test/unit/home_vm.h`:

```cpp
#pragma once
// The Home view-model the goldens are blessed against. Shared so the plain-Home
// golden, the focused-Home golden and the components tests cannot drift apart:
// two copies of this content would mean two goldens that disagree about what
// Home contains while both passing.
#include "reader/viewmodel.h"

inline reader::HomeViewModel sampleHome() {
  // [the existing body, unchanged]
}
```

Change `test_theme_home_golden.cpp` to `#include "home_vm.h"` and delete its local
copy. Run `make test` — **the Home goldens must still pass**, which proves the move
changed nothing.

Commit this on its own:

```bash
git add test/unit/home_vm.h test/unit/test_theme_home_golden.cpp
git commit -m "test: share the Home view-model the goldens are blessed against"
```

- [ ] **Step 1: Add `holds` to `HomeViewModel`**

In `core/include/reader/viewmodel.h`, inside `HomeViewModel`, immediately after the
`hints` member:

```cpp
  std::array<std::string, 4> hints{};        // Back, Confirm, Up, Down slots
  // Which of those four buttons also has a long-press action. The theme draws a
  // hollow ring on the slot (design 662557d) and the screen builds its
  // long-press mask from the same array, so the affordance and the behaviour
  // cannot drift apart -- a ring always means a hold is bound, and a bound hold
  // always shows a ring.
  std::array<bool, 4> holds{};
```

- [ ] **Step 2: Write the failing test**

Add to `test/unit/test_components.cpp`:

```cpp
// test/unit/test_components.cpp -- needs #include <cstring> and #include "home_vm.h"
TEST_CASE("the theme draws a hold ring exactly on the slots the view model marks") {
  // The ring is an affordance for a binding. If the theme sourced it from
  // anywhere but the view model's holds array, a screen could promise a hold it
  // does not have -- or bind one with nothing on screen to suggest it.
  Ramp r;
  reader::QuietTheme theme;

  reader::HomeViewModel vm = sampleHome();
  vm.holds = {false, true, false, false};  // Confirm only
  reader::Framebuffer with(480, 800);
  theme.renderHome(with, r.fonts, vm, reader::Plane::Bw);

  vm.holds = {false, false, false, false};
  reader::Framebuffer without(480, 800);
  theme.renderHome(without, r.fonts, vm, reader::Plane::Bw);

  REQUIRE(with.sizeBytes() == without.sizeBytes());
  // The bar is the same height either way -- the ring rides on the label's line
  // -- but the Confirm slot is wider, so the frames must differ.
  CHECK(std::memcmp(with.data(), without.data(),
                    static_cast<size_t>(with.sizeBytes())) != 0);
}
```

`test_components.cpp` already has `using ramp::Ramp;` at line 13; add
`#include "home_vm.h"`, which Step 0 above just created.

- [ ] **Step 3: Run and confirm failure**

Run: `make test`
Expected: FAIL — the two frames are identical, because `theme_quiet.cpp` still
hardcodes `false` in every slot.

- [ ] **Step 4: Read the array in the theme**

In `core/src/theme_quiet.cpp`, replace the hint array **and the comment above it**
(`// No slot on Home has a long-press action, so no slot carries the hold ring.` —
which stops being true) with:

```cpp
  // The ring comes from the view model, not from this function: a slot shows a
  // hold mark if and only if the screen bound a long-press to that button.
  const Hint hints[4] = {{&icons::kBook, vm.hints[0], vm.holds[0]},
                         {&icons::kDot, vm.hints[1], vm.holds[1]},
                         {&icons::kUp, vm.hints[2], vm.holds[2]},
                         {&icons::kDown, vm.hints[3], vm.holds[3]}};
```

- [ ] **Step 5: Run and confirm passing, and that the goldens did not move**

Run: `make test`
Expected: all pass. **The Home goldens must be byte-identical** — Home binds no
holds, `holds` default-constructs to all-false, so nothing about the current render
changes. If a golden fails here, the default is wrong; do not re-bless.

- [ ] **Step 6: Commit**

```bash
git add core/include/reader/viewmodel.h core/src/theme_quiet.cpp test/unit/test_components.cpp
git commit -m "feat(theme): source the hint hold ring from the view model"
```

---

## Task 5: The screen stack

**Files:**
- Create: `core/include/reader/app.h`, `core/src/app.cpp`
- Test: `test/unit/test_app.cpp`

- [ ] **Step 1: Write the header**

`core/include/reader/app.h`:

```cpp
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "reader/input.h"
#include "reader/refresh.h"
#include "reader/text.h"  // Plane

namespace reader {

class Framebuffer;
class FontSet;
class Theme;

enum class ScreenId : uint8_t { Home, Library, Settings, InputMonitor };

// What a screen asks the app to do after handling an event.
struct Action {
  enum class Kind : uint8_t { None, Redraw, Push, Pop, Sleep };
  Kind kind = Kind::None;
  ScreenId target = ScreenId::Home;  // meaningful for Push only

  static Action none() { return {}; }
  static Action redraw() { return {Kind::Redraw, ScreenId::Home}; }
  static Action push(ScreenId t) { return {Kind::Push, t}; }
  static Action pop() { return {Kind::Pop, ScreenId::Home}; }
  static Action sleep() { return {Kind::Sleep, ScreenId::Home}; }
};

// The four hint slots are the four front buttons in hardware order (spec 4.0).
// Getting this order wrong would bind a ring drawn over one button to a hold on
// another, which is why it is one shared helper and not four call sites.
inline constexpr std::array<Button, 4> kHintSlotButtons = {Button::Back, Button::Confirm,
                                                           Button::Up, Button::Down};

// The long-press mask a screen's hint slots imply.
constexpr ButtonMask hintHoldMask(const std::array<bool, 4>& holds) {
  ButtonMask m = 0;
  for (int i = 0; i < 4; ++i)
    if (holds[i]) m |= buttonBit(kHintSlotButtons[i]);
  return m;
}

// A screen produces a view-model and lets the theme render it (spec 3.3) --
// screens never draw pixels themselves. `render` exists on the screen only to
// pick which typed theme method its own view-model belongs to.
class Screen {
 public:
  virtual ~Screen() = default;
  virtual ScreenId id() const = 0;
  virtual Fidelity fidelity() const { return Fidelity::Gray; }
  virtual ButtonMask longPressable() const = 0;
  virtual Action onEvent(const InputEvent& ev) = 0;
  virtual void render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                      Plane plane) const = 0;
};

// Builds a screen on demand. An interface rather than a std::function so the
// firmware pulls in no <functional> and does no allocation per push, and so
// Phase 2C can give the factory the SD card and settings it will need.
class ScreenFactory {
 public:
  virtual ~ScreenFactory() = default;
  // Returning nullptr means "no such screen": the app refuses the push and
  // leaves the stack alone rather than pushing a hole into it.
  virtual std::unique_ptr<Screen> create(ScreenId id) = 0;
};

class App {
 public:
  App(std::unique_ptr<Screen> root, ScreenFactory& factory);

  Screen& top();
  const Screen& top() const;
  int depth() const { return static_cast<int>(stack_.size()); }

  void dispatch(const InputEvent& ev);

  // Something on screen changed and needs painting.
  bool dirty() const { return dirty_; }
  // ...and the change was a screen change, so the refresh must be FULL.
  bool transition() const { return transition_; }
  void clearDirty();

  bool sleepRequested() const { return sleep_; }
  void clearSleepRequest() { sleep_ = false; }

  ButtonMask longPressable() const { return top().longPressable(); }

 private:
  // V1's deepest path is Home > Library > item actions > delete confirm.
  static constexpr size_t kMaxDepth = 8;

  std::vector<std::unique_ptr<Screen>> stack_;
  ScreenFactory& factory_;
  bool dirty_ = true;  // the first frame always needs painting
  bool transition_ = true;
  bool sleep_ = false;
};

}  // namespace reader
```

- [ ] **Step 2: Write the failing tests**

`test/unit/test_app.cpp`:

```cpp
#include <map>

#include "doctest.h"
#include "reader/app.h"

using namespace reader;

namespace {

// A screen that records what it was sent and returns a scripted action. Enough
// to test the stack without dragging fonts or a theme in.
class FakeScreen : public Screen {
 public:
  FakeScreen(ScreenId id, Action next, ButtonMask holds = 0)
      : id_(id), next_(next), holds_(holds) {}
  ScreenId id() const override { return id_; }
  ButtonMask longPressable() const override { return holds_; }
  Action onEvent(const InputEvent&) override {
    ++events;
    return next_;
  }
  void render(Framebuffer&, const FontSet&, Theme&, Plane) const override {}
  void setNext(Action a) { next_ = a; }
  int events = 0;

 private:
  ScreenId id_;
  Action next_;
  ButtonMask holds_;
};

class FakeFactory : public ScreenFactory {
 public:
  std::map<ScreenId, Action> actions;
  std::map<ScreenId, ButtonMask> holds;
  bool refuse = false;
  std::unique_ptr<Screen> create(ScreenId id) override {
    if (refuse) return nullptr;
    return std::make_unique<FakeScreen>(id, actions.count(id) ? actions[id] : Action::none(),
                                        holds.count(id) ? holds[id] : 0);
  }
};

const InputEvent kConfirm{Button::Confirm, PressKind::Short};

}  // namespace

TEST_CASE("hint slots map to the hardware button order") {
  // Back, Confirm, Up, Down -- NOT the enum's own order, which puts Left and
  // Right between Confirm and Up.
  CHECK(hintHoldMask({true, false, false, false}) == buttonBit(Button::Back));
  CHECK(hintHoldMask({false, true, false, false}) == buttonBit(Button::Confirm));
  CHECK(hintHoldMask({false, false, true, false}) == buttonBit(Button::Up));
  CHECK(hintHoldMask({false, false, false, true}) == buttonBit(Button::Down));
  CHECK(hintHoldMask({false, false, false, false}) == 0);
  CHECK(hintHoldMask({true, true, true, true}) ==
        (buttonBit(Button::Back) | buttonBit(Button::Confirm) | buttonBit(Button::Up) |
         buttonBit(Button::Down)));
}

TEST_CASE("the first frame is dirty and counts as a transition") {
  FakeFactory f;
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::none()), f);
  CHECK(app.dirty());
  CHECK(app.transition());
  app.clearDirty();
  CHECK_FALSE(app.dirty());
  CHECK_FALSE(app.transition());
}

TEST_CASE("push and pop move the top of the stack") {
  FakeFactory f;
  // Script the factory BEFORE the push: a screen's action is baked in when the
  // factory builds it, so setting f.actions afterwards would change nothing and
  // the test would pass without exercising the pop.
  f.actions[ScreenId::Library] = Action::pop();
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::push(ScreenId::Library)), f);
  app.clearDirty();

  app.dispatch(kConfirm);
  CHECK(app.depth() == 2);
  CHECK(app.top().id() == ScreenId::Library);
  CHECK(app.dirty());
  CHECK(app.transition());
  app.clearDirty();

  app.dispatch(kConfirm);  // Library pops
  CHECK(app.depth() == 1);
  CHECK(app.top().id() == ScreenId::Home);
  CHECK(app.dirty());
  CHECK(app.transition());
}

TEST_CASE("popping the root is refused and never empties the stack") {
  FakeFactory f;
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::pop()), f);
  app.clearDirty();
  app.dispatch(kConfirm);
  CHECK(app.depth() == 1);
  CHECK(app.top().id() == ScreenId::Home);
  // Nothing changed on screen, so nothing needs repainting.
  CHECK_FALSE(app.dirty());
}

TEST_CASE("a popped screen returns to the one underneath, which kept its state") {
  FakeFactory f;
  auto root = std::make_unique<FakeScreen>(ScreenId::Home, Action::push(ScreenId::Settings));
  FakeScreen* home = root.get();
  App app(std::move(root), f);
  f.actions[ScreenId::Settings] = Action::pop();

  app.dispatch(kConfirm);  // Home pushes Settings
  REQUIRE(app.depth() == 2);
  app.dispatch(kConfirm);  // Settings pops
  CHECK(app.depth() == 1);
  CHECK(app.top().id() == ScreenId::Home);
  // The same object, not a rebuilt one: its event count survived the round trip.
  CHECK(home->events == 1);
  CHECK(&app.top() == home);
}

TEST_CASE("a redraw is dirty but is not a transition") {
  FakeFactory f;
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::redraw()), f);
  app.clearDirty();
  app.dispatch(kConfirm);
  CHECK(app.dirty());
  CHECK_FALSE(app.transition());
}

TEST_CASE("an action of None leaves the screen clean") {
  FakeFactory f;
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::none()), f);
  app.clearDirty();
  app.dispatch(kConfirm);
  CHECK_FALSE(app.dirty());
}

TEST_CASE("a factory that cannot build the screen leaves the stack intact") {
  FakeFactory f;
  f.refuse = true;
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::push(ScreenId::Library)), f);
  app.clearDirty();
  app.dispatch(kConfirm);
  CHECK(app.depth() == 1);
  CHECK(app.top().id() == ScreenId::Home);
  CHECK_FALSE(app.dirty());
}

TEST_CASE("the long-press mask follows the top of the stack") {
  FakeFactory f;
  f.holds[ScreenId::Library] = buttonBit(Button::Confirm);
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::push(ScreenId::Library), 0), f);
  CHECK(app.longPressable() == 0);
  app.dispatch(kConfirm);
  CHECK(app.longPressable() == buttonBit(Button::Confirm));
}

TEST_CASE("a sleep action is latched until the shell clears it") {
  FakeFactory f;
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::sleep()), f);
  CHECK_FALSE(app.sleepRequested());
  app.dispatch(kConfirm);
  CHECK(app.sleepRequested());
  app.clearSleepRequest();
  CHECK_FALSE(app.sleepRequested());
}
```

- [ ] **Step 3: Run and confirm failure**

Run: `make test`
Expected: link error on `App::App` / `App::dispatch`.

- [ ] **Step 4: Implement**

`core/src/app.cpp`:

```cpp
#include "reader/app.h"

namespace reader {

App::App(std::unique_ptr<Screen> root, ScreenFactory& factory) : factory_(factory) {
  // Reserve up front. The firmware is built -fno-exceptions, so a vector that
  // cannot grow calls abort() and takes the whole device down with no
  // diagnostic -- this project has already lost a boot to exactly that. V1's
  // deepest path is Home > Library > actions overlay > delete confirm, so four
  // is the real ceiling and eight is slack; reserving means a push allocates
  // only the screen itself.
  stack_.reserve(kMaxDepth);
  stack_.push_back(std::move(root));
}

Screen& App::top() { return *stack_.back(); }
const Screen& App::top() const { return *stack_.back(); }

void App::clearDirty() {
  dirty_ = false;
  transition_ = false;
}

void App::dispatch(const InputEvent& ev) {
  const Action a = top().onEvent(ev);
  switch (a.kind) {
    case Action::Kind::None:
      break;
    case Action::Kind::Redraw:
      dirty_ = true;
      break;
    case Action::Kind::Push: {
      auto next = factory_.create(a.target);
      // A factory that cannot build the screen is a bug in the caller, not a
      // reason to push a null onto the stack and crash on the next render.
      if (!next) break;
      stack_.push_back(std::move(next));
      dirty_ = true;
      transition_ = true;
      break;
    }
    case Action::Kind::Pop:
      // The root is the app: popping it would leave nothing to render and
      // nothing to receive the next event.
      if (stack_.size() <= 1) break;
      stack_.pop_back();
      dirty_ = true;
      transition_ = true;
      break;
    case Action::Kind::Sleep:
      sleep_ = true;
      break;
  }
}

}  // namespace reader
```

- [ ] **Step 5: Run and confirm passing**

Run: `make test`

- [ ] **Step 6: Commit**

```bash
git add core/include/reader/app.h core/src/app.cpp test/unit/test_app.cpp
git commit -m "feat(app): screen stack with push, pop and per-screen long-press mask"
```

---

## Task 6: Home as a screen

**Files:**
- Create: `core/include/reader/screen_home.h`, `core/src/screen_home.cpp`
- Test: `test/unit/test_screen_home.cpp`

- [ ] **Step 1: Write the header**

`core/include/reader/screen_home.h`:

```cpp
#pragma once
#include <vector>

#include "reader/app.h"
#include "reader/viewmodel.h"

namespace reader {

// Home. Focus runs Continue (-1) then down through the menu rows; Confirm opens
// the focused row's screen.
//
// The hint LABELS are fixed, not focus-dependent: Home's board says
// READ / SELECT / UP / DOWN, and making Confirm read "OPEN" over the Library row
// would be a design change, which belongs in the board first.
class HomeScreen : public Screen {
 public:
  // `targets` runs parallel to `vm.menu`: the screen each row opens. A row with
  // no screen yet (or one Phase 3 owns) gets no entry, and Confirm on it does
  // nothing. Kept out of the view-model because a view-model carries what the
  // theme draws, and the theme has no business knowing about ScreenId.
  HomeScreen(HomeViewModel vm, std::vector<ScreenId> targets);

  ScreenId id() const override { return ScreenId::Home; }
  ButtonMask longPressable() const override { return hintHoldMask(vm_.holds); }
  Action onEvent(const InputEvent& ev) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  int focus() const { return vm_.focusedMenuIndex; }
  const HomeViewModel& vm() const { return vm_; }

 private:
  // Returns Redraw only when the focus actually moved: at the end of the list a
  // press must not cost a 1.5 s panel refresh that changes nothing.
  Action moveFocus(int delta);

  HomeViewModel vm_;
  std::vector<ScreenId> targets_;
};

}  // namespace reader
```

- [ ] **Step 2: Write the failing tests**

`test/unit/test_screen_home.cpp`:

```cpp
#include "doctest.h"
#include "reader/screen_home.h"

using namespace reader;

namespace {

HomeViewModel vmWithTwoRows() {
  HomeViewModel vm;
  vm.title = "Middlemarch";
  vm.menu = {{"LIBRARY", "12"}, {"SETTINGS", ""}};
  vm.focusedMenuIndex = -1;
  vm.hints = {"READ", "SELECT", "UP", "DOWN"};
  return vm;
}

HomeScreen makeHome() {
  return HomeScreen(vmWithTwoRows(), {ScreenId::Library, ScreenId::Settings});
}

const InputEvent kDown{Button::Down, PressKind::Short};
const InputEvent kUp{Button::Up, PressKind::Short};
const InputEvent kConfirm{Button::Confirm, PressKind::Short};
const InputEvent kBack{Button::Back, PressKind::Short};

}  // namespace

TEST_CASE("down walks from Continue into the menu and stops at the last row") {
  HomeScreen h = makeHome();
  CHECK(h.focus() == -1);
  CHECK(h.onEvent(kDown).kind == Action::Kind::Redraw);
  CHECK(h.focus() == 0);
  CHECK(h.onEvent(kDown).kind == Action::Kind::Redraw);
  CHECK(h.focus() == 1);
  // Clamped, not wrapped -- and no redraw, because nothing moved.
  CHECK(h.onEvent(kDown).kind == Action::Kind::None);
  CHECK(h.focus() == 1);
}

TEST_CASE("up walks back to Continue and stops there") {
  HomeScreen h = makeHome();
  h.onEvent(kDown);
  h.onEvent(kDown);
  REQUIRE(h.focus() == 1);
  CHECK(h.onEvent(kUp).kind == Action::Kind::Redraw);
  CHECK(h.onEvent(kUp).kind == Action::Kind::Redraw);
  CHECK(h.focus() == -1);
  CHECK(h.onEvent(kUp).kind == Action::Kind::None);
  CHECK(h.focus() == -1);
}

TEST_CASE("confirm on a menu row pushes that row's screen") {
  HomeScreen h = makeHome();
  h.onEvent(kDown);
  Action a = h.onEvent(kConfirm);
  CHECK(a.kind == Action::Kind::Push);
  CHECK(a.target == ScreenId::Library);
  h.onEvent(kDown);
  a = h.onEvent(kConfirm);
  CHECK(a.kind == Action::Kind::Push);
  CHECK(a.target == ScreenId::Settings);
}

TEST_CASE("confirm on Continue does nothing yet -- the Reader is Phase 3") {
  HomeScreen h = makeHome();
  REQUIRE(h.focus() == -1);
  CHECK(h.onEvent(kConfirm).kind == Action::Kind::None);
}

TEST_CASE("back on Home does nothing -- its board binds Back to Read, which is Phase 3") {
  HomeScreen h = makeHome();
  CHECK(h.onEvent(kBack).kind == Action::Kind::None);
}

TEST_CASE("a row with no target screen is inert rather than pushing the wrong one") {
  // A menu longer than the target list must not read off the end.
  HomeScreen h(vmWithTwoRows(), {ScreenId::Library});
  h.onEvent(kDown);
  h.onEvent(kDown);
  REQUIRE(h.focus() == 1);
  CHECK(h.onEvent(kConfirm).kind == Action::Kind::None);
}

TEST_CASE("Home binds no long press, so its mask is empty and its bar shows no ring") {
  HomeScreen h = makeHome();
  CHECK(h.longPressable() == 0);
}

TEST_CASE("a long press on a button Home does not bind is ignored, not mistaken for a short one") {
  // The recognizer should never deliver this, but a screen that silently treated
  // Long as Short would hide a mask bug rather than surfacing it.
  HomeScreen h = makeHome();
  const InputEvent longDown{Button::Down, PressKind::Long};
  CHECK(h.onEvent(longDown).kind == Action::Kind::None);
  CHECK(h.focus() == -1);
}

TEST_CASE("an empty menu leaves focus on Continue") {
  HomeViewModel vm = vmWithTwoRows();
  vm.menu.clear();
  HomeScreen h(vm, {});
  CHECK(h.onEvent(kDown).kind == Action::Kind::None);
  CHECK(h.focus() == -1);
}
```

- [ ] **Step 3: Run and confirm failure**

Run: `make test`
Expected: link error on `HomeScreen::onEvent`.

- [ ] **Step 4: Implement**

`core/src/screen_home.cpp`:

```cpp
#include "reader/screen_home.h"

#include "reader/theme.h"

namespace reader {

HomeScreen::HomeScreen(HomeViewModel vm, std::vector<ScreenId> targets)
    : vm_(std::move(vm)), targets_(std::move(targets)) {}

Action HomeScreen::moveFocus(int delta) {
  const int last = static_cast<int>(vm_.menu.size()) - 1;
  int next = vm_.focusedMenuIndex + delta;
  // Clamp rather than wrap. Home's menu is short enough that wrapping would be
  // pleasant, but Library will hold hundreds of books and a list that jumps
  // silently from the last item to the first is indistinguishable from a stuck
  // button. One rule for every list.
  if (next < -1) next = -1;
  if (next > last) next = last;
  if (next == vm_.focusedMenuIndex) return Action::none();
  vm_.focusedMenuIndex = next;
  return Action::redraw();
}

Action HomeScreen::onEvent(const InputEvent& ev) {
  // Home binds no holds, so a Long here means the mask and the view model
  // disagree. Ignoring it keeps that visible as a dead button rather than
  // papering over it by treating the hold as a press.
  if (ev.kind != PressKind::Short) return Action::none();

  switch (ev.button) {
    case Button::Down:
      return moveFocus(+1);
    case Button::Up:
      return moveFocus(-1);
    case Button::Confirm: {
      const int i = vm_.focusedMenuIndex;
      // Continue: the Reader is Phase 3.
      if (i < 0) return Action::none();
      if (i >= static_cast<int>(targets_.size())) return Action::none();
      return Action::push(targets_[static_cast<size_t>(i)]);
    }
    // Home's board binds Back to "Read" (spec 4.1: there is nothing to go back
    // to), which opens the Reader -- Phase 3.
    case Button::Back:
    default:
      return Action::none();
  }
}

void HomeScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const {
  theme.renderHome(fb, fonts, vm_, plane);
}

}  // namespace reader
```

- [ ] **Step 5: Run and confirm passing**

Run: `make test`

- [ ] **Step 6: Commit**

```bash
git add core/include/reader/screen_home.h core/src/screen_home.cpp test/unit/test_screen_home.cpp
git commit -m "feat(home): Home as a screen with focus navigation and push targets"
```

---

## Task 7: The provisional stub surface and the Input Monitor

**Files:**
- Modify: `core/include/reader/viewmodel.h` (add `StubViewModel`)
- Modify: `core/include/reader/theme.h`, `core/include/reader/theme_quiet.h`, `core/src/theme_quiet.cpp`
- Create: `core/include/reader/screen_stub.h`, `core/src/screen_stub.cpp`
- Create: `core/include/reader/screen_input_monitor.h`, `core/src/screen_input_monitor.cpp`

**This surface is scaffolding and says so on screen.** Phase 2C replaces the Library
and Settings placeholders with their real boards and deletes what is left. It gets no
goldens: pinning provisional pixels only creates churn.

- [ ] **Step 1: Add the view-model**

Append to `core/include/reader/viewmodel.h`, inside the namespace:

```cpp
// A provisional titled-list surface: Phase 2B's Library and Settings
// placeholders and its Input Monitor. It exists so the interaction runtime can
// be navigated and verified before the real screens are built, and Phase 2C
// deletes it. Deliberately plain, and it carries `note` so nobody reads it as a
// design.
struct StubViewModel {
  std::string title;
  std::string note;                    // e.g. "PLACEHOLDER - PHASE 2C"
  std::vector<std::string> lines;
  int focusedLine = -1;                // -1 = nothing focused
  int batteryPercent = 0;
  std::array<std::string, 4> hints{};  // Back, Confirm, Up, Down
  std::array<bool, 4> holds{};
};
```

- [ ] **Step 2: Add the theme virtual**

In `core/include/reader/theme.h`, add `struct StubViewModel;` at **namespace scope**,
directly beneath the existing `struct HomeViewModel;` forward declaration (not inside
the class), then add the virtual to `Theme` after `renderHome`:

```cpp
  // The provisional Phase 2B surface. A virtual on Theme rather than a screen
  // drawing its own pixels, because "screens never draw pixels directly" holds
  // for scaffolding too -- a diagnostic that bypassed the theme would be the
  // precedent that erodes the rule.
  virtual void renderStub(Framebuffer& fb, const FontSet& fonts, const StubViewModel& vm,
                          Plane plane = Plane::Bw) = 0;
```

Declare the override in `core/include/reader/theme_quiet.h` with the same signature.

> This is a pure virtual on an existing abstract class, so **every `Theme`
> subclass must implement it**. `QuietTheme` is the only one today. If a test
> defines its own `Theme` subclass, it needs the new method too — a build error
> will say so.

- [ ] **Step 3: Implement `renderStub` from existing primitives only**

In `core/src/theme_quiet.cpp`, add:

```cpp
void QuietTheme::renderStub(Framebuffer& fb, const FontSet& fonts, const StubViewModel& vm,
                            Plane plane) {
  fb.clear(true);
  // Built only from primitives already matched to boards -- header band, rows,
  // hint bar. Nothing here invents a measurement, so this surface cannot
  // introduce a fidelity defect the real screens would inherit.
  int y = drawHeaderBand(fb, fonts, vm.title, vm.batteryPercent, plane);

  const Font& meta = fonts[Role::Meta400];
  y += kMargin;
  drawText(fb, meta, kMargin, baselineIn(meta, y, meta.lineHeight()), vm.note, Ink::Black,
           trackingEm(meta, kBandLabelEm), plane);
  y += meta.lineHeight() + kMargin;

  const Hint hints[4] = {{&icons::kBack, vm.hints[0], vm.holds[0]},
                         {&icons::kDot, vm.hints[1], vm.holds[1]},
                         {&icons::kUp, vm.hints[2], vm.holds[2]},
                         {&icons::kDown, vm.hints[3], vm.holds[3]}};
  const int barTop = fb.height() - hintBarHeight(fonts, hints);

  for (size_t i = 0; i < vm.lines.size(); ++i) {
    const int rowY = y + static_cast<int>(i) * kRowH;
    // Clip against the hint bar rather than drawing under it. The real screens
    // scroll; this one just stops, which is honest for a placeholder.
    if (rowY + kRowH > barTop) break;
    drawRow(fb, fonts, rowY, vm.lines[i], "", static_cast<int>(i) == vm.focusedLine, nullptr,
            plane);
  }

  int slots[4] = {};
  drawHintBar(fb, fonts, hints, slots, plane);
}
```

> If `kBandLabelEm` is not the constant `theme_quiet.cpp` already uses for band
> label tracking, use the one it does. Do not add a new tracking constant.

- [ ] **Step 4: Write `StubScreen`**

`core/include/reader/screen_stub.h`:

```cpp
#pragma once
#include <optional>
#include <string>
#include <vector>

#include "reader/app.h"
#include "reader/viewmodel.h"

namespace reader {

// One provisional screen, parameterised: a title, a list of rows, and for each
// row an optional screen it opens. Serves both the Library and Settings
// placeholders. Phase 2C deletes it.
class StubScreen : public Screen {
 public:
  struct Row {
    std::string label;
    std::optional<ScreenId> target;
  };

  StubScreen(ScreenId id, std::string title, std::vector<Row> rows);

  ScreenId id() const override { return id_; }
  ButtonMask longPressable() const override { return hintHoldMask(vm_.holds); }
  Action onEvent(const InputEvent& ev) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  int focus() const { return vm_.focusedLine; }

 private:
  Action moveFocus(int delta);

  ScreenId id_;
  std::vector<Row> rows_;
  StubViewModel vm_;
};

}  // namespace reader
```

`core/src/screen_stub.cpp`:

```cpp
#include "reader/screen_stub.h"

#include "reader/theme.h"

namespace reader {

StubScreen::StubScreen(ScreenId id, std::string title, std::vector<Row> rows)
    : id_(id), rows_(std::move(rows)) {
  vm_.title = std::move(title);
  vm_.note = "PLACEHOLDER \xE2\x80\x94 PHASE 2C";
  for (const Row& r : rows_) vm_.lines.push_back(r.label);
  vm_.focusedLine = rows_.empty() ? -1 : 0;
  vm_.batteryPercent = 87;
  vm_.hints = {"BACK", "OPEN", "UP", "DOWN"};
}

Action StubScreen::moveFocus(int delta) {
  if (rows_.empty()) return Action::none();
  const int last = static_cast<int>(rows_.size()) - 1;
  int next = vm_.focusedLine + delta;
  if (next < 0) next = 0;
  if (next > last) next = last;
  if (next == vm_.focusedLine) return Action::none();
  vm_.focusedLine = next;
  return Action::redraw();
}

Action StubScreen::onEvent(const InputEvent& ev) {
  if (ev.kind != PressKind::Short) return Action::none();
  switch (ev.button) {
    case Button::Down:
      return moveFocus(+1);
    case Button::Up:
      return moveFocus(-1);
    case Button::Back:
      return Action::pop();
    case Button::Confirm: {
      const int i = vm_.focusedLine;
      if (i < 0 || i >= static_cast<int>(rows_.size())) return Action::none();
      const auto& target = rows_[static_cast<size_t>(i)].target;
      if (!target) return Action::none();
      return Action::push(*target);
    }
    default:
      return Action::none();
  }
}

void StubScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const {
  theme.renderStub(fb, fonts, vm_, plane);
}

}  // namespace reader
```

- [ ] **Step 5: Write `InputMonitorScreen`**

`core/include/reader/screen_input_monitor.h`:

```cpp
#pragma once
#include "reader/app.h"
#include "reader/viewmodel.h"

namespace reader {

// A diagnostic that prints the classified presses it receives.
//
// This is what proves the phase on hardware. Short-versus-long classification
// and the FAST refresh path are both invisible in a serial log and both visible
// here: press Confirm and read SHORT, hold it and read LONG, at panel speed.
// Fidelity::Mono on purpose -- a diagnostic is the one surface that can afford
// thresholded text, and using it exercises the fast path in this phase rather
// than leaving it untested until the Reader lands.
class InputMonitorScreen : public Screen {
 public:
  InputMonitorScreen();

  ScreenId id() const override { return ScreenId::InputMonitor; }
  Fidelity fidelity() const override { return Fidelity::Mono; }
  ButtonMask longPressable() const override { return hintHoldMask(vm_.holds); }
  Action onEvent(const InputEvent& ev) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

 private:
  static constexpr size_t kMaxLines = 8;

  StubViewModel vm_;
};

}  // namespace reader
```

`core/src/screen_input_monitor.cpp`:

```cpp
#include "reader/screen_input_monitor.h"

#include "reader/theme.h"

namespace reader {
namespace {

const char* buttonName(Button b) {
  switch (b) {
    case Button::Back: return "BACK";
    case Button::Confirm: return "CONFIRM";
    case Button::Left: return "LEFT";
    case Button::Right: return "RIGHT";
    case Button::Up: return "UP";
    case Button::Down: return "DOWN";
    case Button::Power: return "POWER";
    default: return "?";
  }
}

}  // namespace

InputMonitorScreen::InputMonitorScreen() {
  vm_.title = "INPUT MONITOR";
  vm_.note = "DIAGNOSTIC \xE2\x80\x94 HOLD CONFIRM";
  vm_.batteryPercent = 87;
  vm_.hints = {"BACK", "LOG", "UP", "DOWN"};
  // Confirm carries a hold, so this screen's bar shows the ring AND the
  // recognizer is allowed to fire Long for it. One array does both.
  vm_.holds = {false, true, false, false};
}

Action InputMonitorScreen::onEvent(const InputEvent& ev) {
  // Back short-presses out; every other press, including Back held, is logged.
  if (ev.button == Button::Back && ev.kind == PressKind::Short) return Action::pop();

  std::string line = buttonName(ev.button);
  line += ev.kind == PressKind::Long ? " \xC2\xB7 LONG" : " \xC2\xB7 SHORT";
  vm_.lines.insert(vm_.lines.begin(), std::move(line));
  if (vm_.lines.size() > kMaxLines) vm_.lines.pop_back();
  return Action::redraw();
}

void InputMonitorScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                                Plane plane) const {
  theme.renderStub(fb, fonts, vm_, plane);
}

}  // namespace reader
```

- [ ] **Step 6: Run the tests**

Run: `make test`
Expected: all pass. **Home's goldens must not move** — none of this touches
`renderHome`. If they do, `renderStub` disturbed shared state; find out why.

- [ ] **Step 7: Commit**

```bash
git add core/include/reader/viewmodel.h core/include/reader/theme.h \
        core/include/reader/theme_quiet.h core/src/theme_quiet.cpp \
        core/include/reader/screen_stub.h core/src/screen_stub.cpp \
        core/include/reader/screen_input_monitor.h core/src/screen_input_monitor.cpp
git commit -m "feat(screens): provisional stub surface and the input monitor diagnostic"
```

---

## Task 8: Scripted input in the simulator

**Files:**
- Modify: `sim/main.cpp`
- Modify: `CMakeLists.txt`

The simulator is where navigation gets regression-tested (spec §3.4: "driven by
scripted button events"). `reader_sim home` must keep working exactly as it does —
the `sim_home` test and `tools/compare-design.py` both call it.

- [ ] **Step 1: Factor the font loading out of `main`**

`sim/main.cpp` loads ten faces inline in `main`. Extract that into
`static bool loadRamp(reader::FontSet& fonts)` returning false on failure, so both
subcommands use one copy. Keep the existing comment about roles naming weights.

- [ ] **Step 2: Add the app subcommand**

Add to `sim/main.cpp`:

```cpp
#include "reader/app.h"
#include "reader/screen_home.h"
#include "reader/screen_input_monitor.h"
#include "reader/screen_stub.h"

namespace {

// Builds the screen catalogue. The same wiring the shell uses, so what the
// simulator navigates is what the device navigates.
class SimFactory : public reader::ScreenFactory {
 public:
  std::unique_ptr<reader::Screen> create(reader::ScreenId id) override {
    using reader::ScreenId;
    switch (id) {
      case ScreenId::Library:
        return std::make_unique<reader::StubScreen>(ScreenId::Library, "LIBRARY",
                                                    std::vector<reader::StubScreen::Row>{
                                                        {"CLASSICS", std::nullopt},
                                                        {"MIDDLEMARCH", std::nullopt}});
      case ScreenId::Settings:
        return std::make_unique<reader::StubScreen>(
            ScreenId::Settings, "SETTINGS",
            std::vector<reader::StubScreen::Row>{{"INPUT MONITOR", ScreenId::InputMonitor},
                                                {"ABOUT", std::nullopt}});
      case ScreenId::InputMonitor:
        return std::make_unique<reader::InputMonitorScreen>();
      case ScreenId::Home:
        return nullptr;  // the root is never rebuilt
    }
    return nullptr;
  }
};

// "DOWN,CONFIRM,CONFIRM+" -> events. A trailing '+' means a long press, which is
// how a scripted run reaches a hold without a clock.
bool parseKeys(const char* spec, std::vector<reader::InputEvent>& out) {
  const std::string s(spec);
  size_t i = 0;
  while (i <= s.size()) {
    const size_t comma = s.find(',', i);
    std::string tok = s.substr(i, comma == std::string::npos ? std::string::npos : comma - i);
    if (!tok.empty()) {
      reader::PressKind kind = reader::PressKind::Short;
      if (tok.back() == '+') {
        kind = reader::PressKind::Long;
        tok.pop_back();
      }
      reader::Button b;
      if (tok == "BACK") b = reader::Button::Back;
      else if (tok == "CONFIRM") b = reader::Button::Confirm;
      else if (tok == "LEFT") b = reader::Button::Left;
      else if (tok == "RIGHT") b = reader::Button::Right;
      else if (tok == "UP") b = reader::Button::Up;
      else if (tok == "DOWN") b = reader::Button::Down;
      else if (tok == "POWER") b = reader::Button::Power;
      else {
        std::fprintf(stderr, "unknown key '%s'\n", tok.c_str());
        return false;
      }
      out.push_back({b, kind});
    }
    if (comma == std::string::npos) break;
    i = comma + 1;
  }
  return true;
}

}  // namespace
```

In `main`, after parsing `--canvas`, also parse `--keys SPEC`, and add the `app`
branch: build the Home view-model exactly as the `home` branch does (extract it into
one `static reader::HomeViewModel demoHomeVm()` so the two branches cannot drift),
construct `HomeScreen(demoHomeVm(), {ScreenId::Library, ScreenId::Settings})`, wrap it
in an `App` with a `SimFactory`, dispatch every parsed event, then render the top
screen three times (Bw/Lsb/Msb) and write the PNG — the same three-pass sequence the
`home` branch uses, because that is what the panel does.

Print the resulting stack so a scripted run is self-describing:

```cpp
std::printf("wrote %s (%dx%d) screen=%d depth=%d\n", argv[2], w, h,
            static_cast<int>(app.top().id()), app.depth());
```

- [ ] **Step 3: Register a smoke test**

Add to `CMakeLists.txt`, after the existing `sim_home` test:

```cmake
# The scripted-input path, as a smoke test: Home -> Settings -> Input Monitor,
# with a held Confirm logged there. Catches a broken factory or key parser
# without needing a golden.
add_test(NAME sim_app COMMAND reader_sim app ${CMAKE_BINARY_DIR}/sim_app.png
         --keys DOWN,DOWN,CONFIRM,CONFIRM,CONFIRM+)
```

- [ ] **Step 4: Verify both subcommands**

```bash
make test
./build/reader_sim home build/home.png
./build/reader_sim app build/nav.png --keys DOWN,CONFIRM
./build/reader_sim app build/nav_x3.png --keys DOWN,CONFIRM --canvas 528x792
```

Expected: `make test` green including `sim_app`; the second command prints
`screen=0 depth=1` (Home, no keys change that); the third prints `screen=1 depth=2`
(Library). Open `build/nav.png` with the Read tool and confirm it is the Library
placeholder with its `PLACEHOLDER — PHASE 2C` note, not a broken frame.

- [ ] **Step 5: Commit**

```bash
git add sim/main.cpp CMakeLists.txt
git commit -m "feat(sim): drive the app from scripted button events"
```

---

## Task 9: Goldens for Home's focused states

**Files:**
- Create: `test/unit/test_app_golden.cpp`
- Create: `test/golden/home_focus_library.png`, `test/golden/home_focus_library_x3.png`
- Uses: `test/unit/home_vm.h` (created in Task 4 Step 0)

Home with a menu row focused is the one new *product* state this phase renders. The
inverted focused row is not a new visual — `drawRow(..., focused, ...)` already draws
what the Library board specifies — but focus arriving there is new behaviour and
deserves pinning at both geometries.

- [ ] **Step 1: Write the golden test**

`test/unit/test_app_golden.cpp`, following the existing pattern in
`test/unit/test_theme_home_golden.cpp` (same `golden.h` helper, same three-plane
compose):

```cpp
#include "doctest.h"
#include "golden.h"
#include "home_vm.h"
#include "ramp.h"
#include "reader/app.h"
#include "reader/framebuffer.h"
#include "reader/screen_home.h"
#include "reader/theme_quiet.h"

using ramp::Ramp;

namespace {

// Focus reached by PRESSING Down, not by assigning the index: the golden then
// pins the navigation as well as the render.
reader::HomeScreen focusedOnLibrary() {
  reader::HomeScreen h(sampleHome(),
                       {reader::ScreenId::Library, reader::ScreenId::Settings});
  h.onEvent({reader::Button::Down, reader::PressKind::Short});
  REQUIRE(h.focus() == 0);
  return h;
}

}  // namespace

TEST_CASE("Home with the Library row focused matches its golden at both geometries") {
  Ramp r;
  reader::QuietTheme theme;
  struct Case {
    int w, h;
    const char* name;
  };
  // The golden name carries no extension -- golden::goldenPath appends .png.
  for (const Case c : {Case{480, 800, "home_focus_library"},
                       Case{528, 792, "home_focus_library_x3"}}) {
    reader::HomeScreen screen = focusedOnLibrary();
    reader::Framebuffer bw(c.w, c.h), lsb(c.w, c.h), msb(c.w, c.h);
    // All three planes rendered, so the test drives the same sequence the shell
    // and the simulator do; the golden holds the composition of the two planes.
    screen.render(bw, r.fonts, theme, reader::Plane::Bw);
    screen.render(lsb, r.fonts, theme, reader::Plane::Lsb);
    screen.render(msb, r.fonts, theme, reader::Plane::Msb);
    golden::checkGoldenGray(lsb, msb, c.name);
  }
}
```

`HomeScreen` must be movable for `focusedOnLibrary()` to return by value — it holds a
`HomeViewModel` and a `std::vector`, so the implicit move works. If a subagent adds a
member that breaks it, return a `std::unique_ptr<HomeScreen>` rather than deleting the
press-driven setup.

- [ ] **Step 2: Run and confirm failure**

Run: `make test`
Expected: FAIL — the goldens do not exist; the test writes
`build/home_focus_library_candidate.png` and names both paths.

- [ ] **Step 3: Inspect the candidates before blessing**

**Open both candidates with the Read tool** and check, item by item:

- the LIBRARY row is inverted (white text on black), full-bleed to the margins
- its "12" count is white and right-aligned, and vertically centred in the row
- the SETTINGS row below is unchanged, black on white, with its chevron
- the CONTINUE block has lost its fill and now shows its 2px outline, with black
  label and black forward arrow
- the header band, cover placeholder, title, author, progress bar and hint bar are
  identical to `home_quiet.png` / `home_quiet_x3.png`
- no grey on any rule, fill or icon in the composed image

Write down what you see, including anything that looks wrong even if you bless it.

- [ ] **Step 4: Bless**

```bash
cp build/home_focus_library_candidate.png test/golden/home_focus_library.png
cp build/home_focus_library_x3_candidate.png test/golden/home_focus_library_x3.png
make test
```

Expected: green. `home_quiet.png`, `home_quiet_x3.png` and `text_sample.png` must be
untouched — verify with `git status` that only the two new files appear.

- [ ] **Step 5: Commit**

```bash
git add test/unit/test_app_golden.cpp test/golden/home_focus_library.png \
        test/golden/home_focus_library_x3.png
git commit -m "test(golden): Home with the Library row focused, both geometries"
```

---

## Task 10: The shell's input task

**Files:**
- Create: `shell/src/input_task.h`, `shell/src/input_task.cpp`

- [ ] **Step 1: Write the header**

`shell/src/input_task.h`:

```cpp
#pragma once
#include <cstdint>

class InputManager;

// Raw button transitions, sampled off the main loop.
//
// Why a task and not polling in loop(): an e-ink refresh blocks for 0.4-2 s, and
// the driver's busy-wait yields with delay(), so a press that lands during a
// refresh is simply never sampled. The SDK ships beginAsync() for exactly this,
// but it queues PRESS EDGES ONLY -- no releases, no hold duration -- so it
// cannot support a long press. This is beginAsync's own loop (update() on a
// timer) queuing both edges with a timestamp, which is what the recognizer
// needs.
//
// Only this task may call InputManager::update(); it owns the edge state.

struct RawSample {
  uint8_t button;  // InputManager::BTN_*
  bool down;
  uint32_t ms;
};

// Starts the polling task. Safe to call once; later calls are no-ops.
void startInputTask(InputManager& input);

// Drain one queued transition. False when the queue is empty.
bool popRawSample(RawSample& out);

// Transitions dropped because the queue was full.
uint32_t rawSamplesDropped();
```

- [ ] **Step 2: Implement**

`shell/src/input_task.cpp`:

```cpp
#include "input_task.h"

#include <Arduino.h>
#include <InputManager.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace {

constexpr uint32_t kPollMs = 10;
// Short re-poll while a raw change is still inside the SDK's debounce window.
// Its own comment asks for this: a change commits only after two matching
// samples, so a host polling slowly can drop a press that lands in one sample.
constexpr uint32_t kDebouncePollMs = 2;
constexpr int kQueueLen = 32;

QueueHandle_t gQueue = nullptr;
TaskHandle_t gTask = nullptr;
volatile uint32_t gDropped = 0;

void pollTask(void* arg) {
  auto* input = static_cast<InputManager*>(arg);
  static const uint8_t kButtons[] = {
      InputManager::BTN_BACK, InputManager::BTN_CONFIRM, InputManager::BTN_LEFT,
      InputManager::BTN_RIGHT, InputManager::BTN_UP,     InputManager::BTN_DOWN,
      InputManager::BTN_POWER};
  for (;;) {
    input->update();
    const uint32_t now = millis();
    for (const uint8_t b : kButtons) {
      if (input->wasPressed(b)) {
        const RawSample s{b, true, now};
        if (xQueueSend(gQueue, &s, 0) != pdTRUE) ++gDropped;
      }
      if (input->wasReleased(b)) {
        const RawSample s{b, false, now};
        if (xQueueSend(gQueue, &s, 0) != pdTRUE) ++gDropped;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(input->isDebouncePending() ? kDebouncePollMs : kPollMs));
  }
}

}  // namespace

void startInputTask(InputManager& input) {
  if (gTask) return;
  gQueue = xQueueCreate(kQueueLen, sizeof(RawSample));
  if (!gQueue) return;
  xTaskCreate(pollTask, "encre_input", 4096, &input, 2, &gTask);
}

bool popRawSample(RawSample& out) {
  if (!gQueue) return false;
  return xQueueReceive(gQueue, &out, 0) == pdTRUE;
}

uint32_t rawSamplesDropped() { return gDropped; }
```

- [ ] **Step 3: Confirm it compiles**

Run: `make firmware`
Expected: SUCCESS. Nothing calls it yet, so nothing changes on device.

- [ ] **Step 4: Commit**

```bash
git add shell/src/input_task.h shell/src/input_task.cpp
git commit -m "feat(shell): sample raw button transitions off the main loop"
```

---

## Task 11: Wire the shell — setup/loop split, two paint paths, sleep

**Files:**
- Modify: `shell/src/main.cpp`
- Modify: `platformio.ini`

The largest task, and the only one with no desktop test. Take it in the four steps
below and build after each.

- [ ] **Step 1: Add `PowerManager` to the build**

In `platformio.ini`, add to `[base] lib_deps`:

```
  PowerManager=symlink://freeink-sdk/libs/hardware/PowerManager
```

Run: `make firmware` — SUCCESS, unchanged binary behaviour.

- [ ] **Step 2: Move the long-lived objects to file scope**

Everything the render needs currently lives on `setup()`'s stack and dies when it
returns. Move it to file scope, but **keep the frames heap-allocated behind
`std::unique_ptr`**: the existing `largest block < frameBytes * 2` check must still
run before the allocation. A file-scope `Framebuffer` would allocate during static
init, before that check could run, and a failure there is an `abort()` boot loop with
no diagnostic — the exact failure this project already hit.

```cpp
#include <memory>
#include <optional>

#include "input_task.h"
#include "reader/app.h"
#include "reader/input.h"
#include "reader/power.h"
#include "reader/refresh.h"
#include "reader/screen_home.h"
#include "reader/screen_input_monitor.h"
#include "reader/screen_stub.h"

// Sleep after five minutes idle, FULL refresh every fifteen. Both become
// settings in Phase 2C; named here so the numbers are not buried in a
// constructor call.
constexpr uint32_t kSleepAfterMs = 5u * 60u * 1000u;
constexpr int kFullRefreshEvery = 15;

static std::unique_ptr<reader::Framebuffer> gPortrait, gLandscape;
static std::optional<reader::FontSet> gFonts;
static reader::QuietTheme gTheme;
static std::unique_ptr<reader::App> gApp;
static reader::PressRecognizer gPresses;
static reader::RefreshPolicy gRefresh(kFullRefreshEvery);
static reader::IdleTimer gIdle(kSleepAfterMs);
static InputManager gInput;
```

The screen factory, mirroring the simulator's so device and desktop navigate the
same catalogue:

```cpp
class ShellFactory : public reader::ScreenFactory {
 public:
  std::unique_ptr<reader::Screen> create(reader::ScreenId id) override {
    using reader::ScreenId;
    using Row = reader::StubScreen::Row;
    switch (id) {
      case ScreenId::Library:
        return std::make_unique<reader::StubScreen>(
            ScreenId::Library, "LIBRARY",
            std::vector<Row>{{"CLASSICS", std::nullopt}, {"MIDDLEMARCH", std::nullopt}});
      case ScreenId::Settings:
        return std::make_unique<reader::StubScreen>(
            ScreenId::Settings, "SETTINGS",
            std::vector<Row>{{"INPUT MONITOR", ScreenId::InputMonitor},
                             {"ABOUT", std::nullopt}});
      case ScreenId::InputMonitor:
        return std::make_unique<reader::InputMonitorScreen>();
      case ScreenId::Home:
        return nullptr;
    }
    return nullptr;
  }
};
static ShellFactory gFactory;
```

Rework `setup()` to assign into these instead of declaring locals, keeping every
existing check and `mark()` call in place, then build the app and paint once:

```cpp
  gApp = std::make_unique<reader::App>(
      std::make_unique<reader::HomeScreen>(demoHomeVm(),
                                          std::vector<reader::ScreenId>{
                                              reader::ScreenId::Library,
                                              reader::ScreenId::Settings}),
      gFactory);
  gPresses.setLongPressable(gApp->longPressable());
  gInput.begin();
  startInputTask(gInput);
  mark("input-started");
  renderTop();
  mark("first-paint-complete");
```

Run: `make firmware` — SUCCESS. The device still shows Home; nothing responds yet.

- [ ] **Step 3: The two paint paths**

Replace the inline paint block with two functions. `paintGray` is the existing
sequence moved verbatim — every comment in it was earned and must survive.

```cpp
// One render pass: draw the plane portrait-side, then rotate into the landscape
// frame. CCW is correct, verified on X3 hardware.
static void paintPlane(reader::Plane plane) {
  gPortrait->clear(true);
  gApp->top().render(*gPortrait, *gFonts, gTheme, plane);
  reader::rotate90CCW(*gPortrait, *gLandscape);
}

// The 4-level path: base frame, settle pass, two bit-planes, combine, rebase.
// This is `shell/src/main.cpp:196-225` moved verbatim -- the four numbered
// comment blocks and their `mark()` calls included. Every one of them records
// something that was learned by breaking the panel (LSB before MSB, the settle
// pass before the planes, the Bw re-render instead of a third frame). Move it;
// do not retype it.
static void paintGray() { /* ...the existing sequence, calling paintPlane... */ }

// The 1-bit path. Only legitimate where thresholded text is still readable --
// the Reader's body text (Phase 3) and diagnostics. Phase 2A-2 measured
// thresholded CHROME as illegible, so no product chrome screen may use this.
static void paintMono(reader::RefreshMode mode) {
  paintPlane(reader::Plane::Bw);
  display.setFramebuffer(gLandscape->data());
  display.displayBuffer(mode == reader::RefreshMode::Full ? EInkDisplay::FULL_REFRESH
                                                          : EInkDisplay::FAST_REFRESH);
}

static void renderTop() {
  const reader::RefreshMode mode = gRefresh.next(gApp->transition());
  const bool gray = gApp->top().fidelity() == reader::Fidelity::Gray;
  Serial.printf("[paint] screen=%d fidelity=%s mode=%s sinceFull=%d\n",
                (int)gApp->top().id(), gray ? "gray" : "mono",
                mode == reader::RefreshMode::Full ? "FULL" : "FAST", gRefresh.sinceFull());
  Serial.flush();
  if (gray) {
    // The grayscale sequence is inherently a full repaint; the policy's FAST is
    // not available here, and taking it would mean thresholded chrome.
    paintGray();
  } else {
    paintMono(mode);
  }
}
```

Run: `make firmware` — SUCCESS, and the device still paints Home identically.

- [ ] **Step 4: The loop, and sleep**

```cpp
[[noreturn]] static void sleepNow() {
  // The Sleep screen is boarded and belongs to Phase 2C. Painting nothing is
  // not a gap in the picture: e-ink holds its last image with no power, so the
  // device keeps showing whatever you were looking at.
  Serial.printf("[power] sleeping; wake with the power button\n");
  Serial.flush();
  display.deepSleep();
  // Cuts the X3's SD rail (GPIO13) and any other gated rail, latched so the
  // switches stay off through sleep. Without it the card stays powered and
  // drains the battery all night.
  freeink::PowerManager::powerDownRailsForSleep();
  // Waits for release, arms the SoC-correct wake source from the board's power
  // pin and polarity, then sleeps. Wake is a chip RESET, so this never returns
  // and the firmware boots into Home -- restoring the last screen needs the
  // settings store, which is Phase 2C.
  freeink::PowerManager::deepSleepUntilPowerButton();
}

void loop() {
  bool activity = false;
  RawSample s{};
  while (popRawSample(s)) {
    activity = true;
    // Explicit, not a cast. The SDK's BTN_* values happen to match Button's
    // order today, and a silent reinterpret would break the day either changes.
    reader::Button b;
    switch (s.button) {
      case InputManager::BTN_BACK: b = reader::Button::Back; break;
      case InputManager::BTN_CONFIRM: b = reader::Button::Confirm; break;
      case InputManager::BTN_LEFT: b = reader::Button::Left; break;
      case InputManager::BTN_RIGHT: b = reader::Button::Right; break;
      case InputManager::BTN_UP: b = reader::Button::Up; break;
      case InputManager::BTN_DOWN: b = reader::Button::Down; break;
      case InputManager::BTN_POWER: b = reader::Button::Power; break;
      default: continue;
    }
    gPresses.sample(b, s.down, s.ms);
  }
  gPresses.tick(millis());
  if (activity) gIdle.noteActivity(millis());

  reader::InputEvent ev{};
  while (gPresses.pop(ev)) {
    Serial.printf("[input] %d %s\n", (int)ev.button,
                  ev.kind == reader::PressKind::Long ? "LONG" : "SHORT");
    if (ev.button == reader::Button::Power) sleepNow();
    gApp->dispatch(ev);
    // The mask belongs to whatever screen is now on top, which a push or pop
    // just changed. Re-reading it here is what keeps a hold bound only where a
    // ring is drawn.
    gPresses.setLongPressable(gApp->longPressable());
  }

  if (gApp->sleepRequested() || gIdle.tick(millis()) == reader::PowerAction::Sleep) sleepNow();

  if (gApp->dirty()) {
    renderTop();
    gApp->clearDirty();
  }

  static uint32_t beat = 0;
  if (++beat % 200 == 0) {
    Serial.printf("[alive] last-stage=%s heap=%u depth=%d dropped=%lu/%lu\n", stage,
                  (unsigned)ESP.getFreeHeap(), gApp ? gApp->depth() : 0,
                  (unsigned long)rawSamplesDropped(), (unsigned long)gPresses.dropped());
    Serial.flush();
  }
  delay(10);
}
```

Note the `[alive]` heartbeat now prints every ~2 s from a 10 ms loop rather than
`delay(2000)`, so input latency is 10 ms and the log keeps its old cadence.

Run: `make firmware`
Expected: SUCCESS. Record RAM and Flash.

- [ ] **Step 5: Commit**

```bash
git add shell/src/main.cpp platformio.ini
git commit -m "feat(shell): dispatch input to the screen stack, and sleep on idle or power"
```

---

## Task 12: Verify, document, and hand off the flash

- [ ] **Step 1: Full desktop verification**

```bash
make test
make sim
make firmware
make compare COMPARE_ARGS="--only home"
```

Expected: every test green; `make compare` still reports Home implemented and
matching at both geometries; `home_quiet*.png` and `text_sample.png` untouched.

- [ ] **Step 2: Update `CLAUDE.md`**

Add a **Runtime** section after "Rendering model", stating: runtime logic lives in
`core/` and is unit-tested, the shell only supplies samples/panel/sleep; one physical
press is exactly one event; a button outside the long-press mask fires Short on
release however long it was held; the mask and the hint bar's hold ring both come
from the view-model's `holds` array; screens declare `Fidelity` and chrome is always
`Gray` because thresholded chrome is illegible; deep sleep resets the chip and only
the power button can wake it.

- [ ] **Step 3: Update the roadmap**

In `docs/superpowers/plans/2026-08-20-v1-roadmap.md`, mark **2B** DONE with its
outcome, and add to 2C's scope the four things this phase deliberately deferred:
the Sleep screen, restoring the last screen across a wake, replacing the stub
surface with the real Library and Settings boards, and making the sleep timeout and
refresh cadence real settings.

- [ ] **Step 4: Commit**

```bash
git add CLAUDE.md docs/superpowers/plans/2026-08-20-v1-roadmap.md
git commit -m "docs: record the Phase 2B runtime and what 2C inherits"
```

- [ ] **Step 5: Hand the flash to the user**

**Do not flash it yourself** — the permission classifier blocks the upload from an
agent. Give the user the command and name what only the panel can answer:

```bash
cd ~/dev/encre && ~/.platformio/penv/bin/pio run -e xteink -t upload --upload-port /dev/cu.usbmodem1101
```

Ask them to check, in this order:

1. **Does Down move the focus on Home?** The CONTINUE block should lose its fill and
   the LIBRARY row should invert. This is the whole phase in one press.
2. **Does Confirm on LIBRARY push, and Back pop back?** Both should be full,
   ghost-free repaints.
3. **Settings → INPUT MONITOR:** press Confirm and read `CONFIRM · SHORT`; hold it
   and read `CONFIRM · LONG` **before letting go**. That is the only place short and
   long classification is visible.
4. **How does the Input Monitor's refresh compare?** It is the 1-bit FAST path, so it
   should be visibly quicker and visibly harder to read than the grey screens — the
   contrast is the point, and it is the evidence for keeping chrome on the grey path.
5. **Power button:** does it sleep, and does pressing it again wake into Home?

Then read the log yourself before drawing any conclusion:

```bash
cd ~/dev/encre && ~/.platformio/penv/bin/python tools/serial-log.py --seconds 20 --no-reset
```

`[input]`, `[paint]` and `[alive]` lines say what the firmware saw and did.
`dropped=0/0` on `[alive]` confirms nothing was lost between the task and the loop.
