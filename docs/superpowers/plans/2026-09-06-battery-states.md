# Battery States Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Warn the reader when the battery is low, and shut the device down before the pack is damaged — closing [#9](https://github.com/Rukkaitto/encre/issues/9) and [#10](https://github.com/Rukkaitto/encre/issues/10).

**Architecture:** `BatteryTracker` grows a `BatteryLevel` ladder in `core/` — one object, one `update()`, so the shell cannot feed the band and forget the ladder. The shell polls it on **every** screen, not just Home. `Low` arms a transient banner the Reader draws **over** its page (never displacing `columnH`, so no chapter re-paginates). `Critical` runs `criticalShutdown()`, which paints `BatteryEmpty` and deep-sleeps; a gate in `setup()` **before `display.begin()`** refuses to resume below a hysteresis threshold, which is what makes the board's `CHARGE TO WAKE` true.

**Spec:** `docs/superpowers/specs/2026-09-06-battery-states-design.md` — read it before Task 1. Every "why" below is short because the spec carries it.

**Tech Stack:** C++20 (`core/`, no Arduino/ESP/host-OS), Arduino/ESP32-C3 (`shell/`), doctest, `iconc.py`, Chrome-rasterised `.dc.html` boards.

---

## Before you start

```bash
git submodule update --init      # a fresh worktree has an EMPTY freeink-sdk/
cmake -S . -B build              # CMake uses file(GLOB): RE-RUN THIS after adding any source file
make test                        # must be green before you touch anything
```

**Read `CLAUDE.md` §"Editing this repo with scripts" before writing any heredoc Python.** Three separate defects in this repo came from the same mistake in that method. Read fully, mutate in memory, assert, write once, then `grep` for a marker from the new text and check `git diff --stat`.

**Never re-bless a golden to make a test pass.** A failing golden writes `build/<name>_candidate.png`; look at it.

**Prove every golden by mutation.** A golden that passes without being able to fail is worthless. Each golden task below names the mutation and the failure count it must produce.

---

## File Structure

| File | Responsibility | New? |
|---|---|---|
| `design/LowBattery.dc.html` | the banner over a real 12-line reading page | rebased |
| `design/BatteryEmpty.dc.html` | the shutdown screen | unchanged |
| `tools/iconc.py` | `kWarning`, `kBatteryLarge` entries | modified |
| `core/src/icons.cpp`, `core/include/reader/icons.h` | generated | regenerated |
| `core/include/reader/battery_tracker.h` | the level ladder | modified |
| `core/include/reader/viewmodel.h` | `batteryLowPercent`, `BatteryEmptyViewModel` | modified |
| `core/include/reader/screen_reader.h` / `core/src/screen_reader.cpp` | the banner latch | modified |
| `core/include/reader/screen_battery_empty.h` / `core/src/screen_battery_empty.cpp` | the shutdown screen | **new** |
| `core/include/reader/app.h` | `ScreenId::BatteryEmpty` | modified |
| `core/src/app.cpp`, `core/src/session_record.cpp` | name tables | modified |
| `core/include/reader/components.h` / `core/src/components.cpp` | `drawBadge` | modified |
| `core/include/reader/theme.h`, `theme_quiet.h`, `core/src/theme_quiet.cpp` | `renderBatteryEmpty`, the banner in `renderReader` | modified |
| `core/include/reader/screens.h` / `core/src/screens.cpp` | demo view model + factory case | modified |
| `sim/main.cpp` | `low_battery`, `battery_empty` | modified |
| `shell/src/session.h` / `session.cpp` | the `critShut` NVS flag | modified |
| `shell/src/main.cpp` | poll, banner wiring, shutdown, resume gate, fake-percent flag | modified |
| `test/unit/test_battery_tracker.cpp` | the ladder | modified |
| `test/unit/test_screen_reader_battery.cpp` | the banner latch | **new** |
| `test/unit/test_screen_battery_empty.cpp` | the screen | **new** |
| `test/unit/test_theme_battery_golden.cpp` | both goldens | **new** |

---

# Phase A — the boards

**A UI change goes into the design HTML first.** Not one line of `core/` may be written before Task 2 is committed.

### Task 1: Rebase `LowBattery.dc.html` onto `Reader.dc.html`

`LowBattery.dc.html` predates two changes to the reading page: it uses 40px margins where `Reader.dc.html` uses `padding: 20px 18px 0 18px`, and it says `CH. 01` where the Reader now draws a chapter name from the table of contents. It also shows a three-line paragraph, so nothing is actually covered by the banner.

**Files:**
- Modify: `design/LowBattery.dc.html`

- [ ] **Step 1: Read both boards side by side**

```bash
diff <(sed -n '28,60p' design/Reader.dc.html) <(sed -n '28,50p' design/LowBattery.dc.html)
```

Note the three differences: outer `padding`, the header's right run, and the column's contents.

- [ ] **Step 2: Copy `Reader.dc.html` to `LowBattery.dc.html` wholesale**

```bash
cp design/Reader.dc.html design/LowBattery.dc.html
```

Starting from the Reader is deliberate: the banner is the *only* thing this board adds, so anything else that differs is drift. `Reader.dc.html`'s comments come with it and are the reason the column is 676px and its inner box 652.8px.

- [ ] **Step 3: Insert the banner over the bottom of the column**

The column box (`<div style="flex-grow: 1; overflow: hidden;">`) becomes a positioning context, and the banner is absolutely positioned against its bottom so it **overlaps** the text rather than displacing it. Change that opening tag to:

```html
  <div style="flex-grow: 1; overflow: hidden; position: relative;">
```

and insert this immediately **before** that div's closing `</div>` (the one just above the footer row):

```html
    <!-- THE BANNER IS DRAWN OVER THE PAGE AND NEVER DISPLACES IT.
         `position: absolute` against the column box is the whole point. This band
         is 78px; the column seats 12 whole lines of a 54.4px box, so a band INSIDE
         the flow takes the page to 10 lines and re-paginates the entire chapter --
         at the moment the device has least energy to spend, and with the reader's
         page moving under them. ReaderViewModel::anchorLabel already carries this
         reasoning for the footer's third field: "a footer that changed height
         would reflow the text column and re-paginate the chapter mid-read."

         So the firmware draws it at `footerTop - 78` with columnH untouched, and
         the last line and a half of the page go under it. This board shows a FULL
         twelve-line page for that reason -- with a short paragraph it would cover
         nothing and would not be comparable to what the device draws.

         `ANY BUTTON` IS A BINDING, not a caption. ReaderScreen::onGesture clears
         the banner and returns Action::redraw() whatever the gesture, so the first
         press dismisses it and does nothing else. A bar cannot promise what
         nothing has bound. -->
    <div style="position: absolute; left: -18px; right: -18px; bottom: 0; background: #000000; color: #ffffff; display: flex; justify-content: space-between; align-items: center; height: 78px; padding: 0 24px; box-sizing: border-box; font-family: 'Space Grotesk', 'Helvetica Neue', Arial, sans-serif;">
      <div style="display: flex; align-items: center; gap: 12px;">
        <svg width="32" height="28" viewBox="0 0 18 16" fill="none"><path d="M9 1 17 15H1z" stroke="#ffffff" stroke-width="1.6"></path><path d="M9 6v4" stroke="#ffffff" stroke-width="1.6"></path><rect x="8.3" y="11.6" width="1.4" height="1.4" fill="#ffffff"></rect></svg>
        <!-- WEIGHT 500, AND THE ORIGINAL BOARD SAID 700. The ramp has no 23px/700
             role: it carries Label400 and Label500 at 11pt, and the nearest 700 is
             Meta700 at 21px. A role is a pre-rendered asset per size AND weight,
             15-20 KB of flash each, and this project has already answered this
             question twice the same way -- Sleep's 53px title and HomeEmpty's 39px
             both moved to a role that exists rather than adding one.

             500 RATHER THAN Meta700, because the band already sets `ANY BUTTON` at
             --t-meta: dropping this run to 21px too would flatten the one hierarchy
             the band has. Size is the half worth keeping. -->
        <div style="font-size: var(--t-label); letter-spacing: 0.1em; font-weight: 500;">BATTERY LOW &middot; 5%</div>
      </div>
      <div style="font-size: var(--t-meta); letter-spacing: 0.1em;">ANY BUTTON</div>
    </div>
```

`left: -18px; right: -18px` cancels the page's own `padding: 20px 18px 0 18px` so the band is **full-bleed**, which is what the original board drew and what an inverted band on this device always is.

- [ ] **Step 4: Render it and look at it**

```bash
python3 tools/compare-design.py --only low_battery --export build/overlay
```

Expected: the board renders; the firmware column reports not-implemented (that is Task 9). Open `build/overlay/low_battery_design.png` and confirm **twelve lines of prose with the band over the last one and a half**, the band running edge to edge, and the footer unmoved.

- [ ] **Step 5: Commit**

```bash
git add design/LowBattery.dc.html
git commit -m "design(low-battery): rebase the board onto Reader.dc.html and float the banner over the page"
```

### Task 2: Confirm `BatteryEmpty.dc.html` needs no change

**Files:**
- Read: `design/BatteryEmpty.dc.html`, `design/Sleep.dc.html`

- [ ] **Step 1: Check the badge against Sleep's**

```bash
grep -o 'padding-bottom: [0-9]*px' design/BatteryEmpty.dc.html design/Sleep.dc.html
grep -o 'padding: 8px 18px' design/BatteryEmpty.dc.html design/Sleep.dc.html
grep -o 'letter-spacing: 0.2em' design/BatteryEmpty.dc.html design/Sleep.dc.html
```

Expected: `34px`, `8px 18px` and `0.2em` on **both**. That identity is what licenses the `drawBadge` extraction in Task 5 — it is the second copy, which is this project's extraction point, not the fifth.

- [ ] **Step 2: No commit**

Nothing changed. This task exists so the extraction in Task 5 rests on a check rather than on a memory.

---

# Phase B — the assets

### Task 3: Generate `kWarning` and `kBatteryLarge`

**Files:**
- Modify: `tools/iconc.py`
- Regenerate: `core/src/icons.cpp`, `core/include/reader/icons.h`

- [ ] **Step 1: Read how `kBookLarge` disambiguates itself**

```bash
sed -n '135,180p' tools/iconc.py
```

`kBook` and `kBookLarge` carry identical path data on two different boards, so `source` is what tells them apart. `kBattery`/`kBatteryLarge` is the same situation.

- [ ] **Step 2: Add both entries to `ICONS`**

Insert into the `ICONS` dict in `tools/iconc.py`:

```python
    # THE WARNING TRIANGLE, 32x28 on design/LowBattery.dc.html and the only mark
    # this firmware draws inside an inverted band. Authored WHITE on the board, as
    # kForward is, because that is the ink it is drawn in -- the generator reads
    # coverage, not colour, so this is about the board being honest rather than
    # about the bitmap.
    #
    # The match keys on the triangle's own outline. `stroke-width="1.6"` appears on
    # all three of its paths and identifies none of them; the closed `M9 1 17 15H1z`
    # appears once on the board and nowhere else in the set.
    "kWarning": {
        "source": "design/LowBattery.dc.html",
        "match": "M9 1 17 15H1z",
    },
    # A SECOND ASSET FOR THE SAME DRAWING, at 98x52 against kBattery's ~22x12 --
    # these are pre-rendered bitmaps and there is no scaling one up. kBookLarge's
    # precedent exactly.
    #
    # `source` HAS TO DISAMBIGUATE IT. kBattery's matcher keys on path data that
    # BatteryEmpty.dc.html also carries (the body rect and the terminal nub), and
    # this board's fill rect (`x="2"`) is the one thing kBattery's board does not
    # have -- but the two boards must not be allowed to match each other's icon, so
    # the board name is the primary key here as it is for kBook/kBookLarge.
    "kBatteryLarge": {
        "source": "design/BatteryEmpty.dc.html",
        "match": 'rect x="2" y="2"',
    },
```

- [ ] **Step 3: Regenerate and check what moved**

```bash
make icons && git diff --stat core/src/icons.cpp core/include/reader/icons.h
```

Expected: **two icons added and no existing icon changed.** If any existing icon's bytes moved, a `match` above is stealing another entry's `<svg>` — fix the match, do not accept the diff.

- [ ] **Step 4: Verify the sizes came off the boards**

```bash
grep -n "kWarning\|kBatteryLarge" core/include/reader/icons.h
grep -n "kWarning\b" -A 2 core/src/icons.cpp | head -6
```

Expected: `kWarning` is 32×28 and `kBatteryLarge` is 98×52 — the boards' own `width`/`height` attributes, read at generation time.

- [ ] **Step 5: Build and commit**

```bash
cmake -S . -B build && make test
git add tools/iconc.py core/src/icons.cpp core/include/reader/icons.h
git commit -m "build(icons): generate kWarning and kBatteryLarge from their boards"
```

Expected: `make test` green — nothing reads the new icons yet.

---

# Phase C — the ladder

### Task 4: `BatteryLevel` on `BatteryTracker`

**Files:**
- Modify: `core/include/reader/battery_tracker.h`
- Test: `test/unit/test_battery_tracker.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `test/unit/test_battery_tracker.cpp`:

```cpp
// --- The level ladder ----------------------------------------------------------

TEST_CASE("the level is Normal until a reading says otherwise") {
  BatteryTracker t;
  // NEVER READ is Normal, not Critical. A device that shut itself down because the
  // gauge had not answered yet would be unusable, and percent() already answers -1
  // for the same state.
  CHECK(t.level() == BatteryLevel::Normal);
  t.update(good(50), 0);
  CHECK(t.level() == BatteryLevel::Normal);
}

TEST_CASE("Low is entered at kLowPercent and needs no dwell") {
  BatteryTracker t;
  t.update(good(11), 0);
  CHECK(t.level() == BatteryLevel::Normal);
  // No dwell: the cost of being wrong is one banner, not a shutdown.
  t.update(good(BatteryTracker::kLowPercent), 1000);
  CHECK(t.level() == BatteryLevel::Low);
}

TEST_CASE("Low is left again when the battery comes back up") {
  BatteryTracker t;
  t.update(good(5), 0);
  CHECK(t.level() == BatteryLevel::Low);
  t.update(good(40), 1000);
  CHECK(t.level() == BatteryLevel::Normal);
}

TEST_CASE("Critical requires the dwell, continuously") {
  BatteryTracker t;
  t.update(good(BatteryTracker::kCriticalPercent), 0);
  // Below the threshold but not yet held: the level is Low, not Critical. A single
  // sagging reading -- and an e-ink refresh is the heaviest load this device draws
  // -- must not be able to shut it down.
  CHECK(t.level() == BatteryLevel::Low);
  t.update(good(2), BatteryTracker::kCriticalDwellMs - 1);
  CHECK(t.level() == BatteryLevel::Low);
  t.update(good(2), BatteryTracker::kCriticalDwellMs);
  CHECK(t.level() == BatteryLevel::Critical);
}

TEST_CASE("one reading above the threshold restarts the dwell") {
  BatteryTracker t;
  t.update(good(1), 0);
  t.update(good(1), BatteryTracker::kCriticalDwellMs - 1);
  CHECK(t.level() == BatteryLevel::Low);
  // The recovery. The dwell is measured from the FIRST reading in an UNBROKEN run,
  // exactly as kUnlatchMs's is, so this one resets it.
  t.update(good(20), BatteryTracker::kCriticalDwellMs);
  CHECK(t.level() == BatteryLevel::Normal);
  t.update(good(1), BatteryTracker::kCriticalDwellMs + 1);
  // Held for the dwell measured from HERE, not from 0.
  t.update(good(1), BatteryTracker::kCriticalDwellMs + 1 + BatteryTracker::kCriticalDwellMs - 1);
  CHECK(t.level() == BatteryLevel::Low);
  t.update(good(1), BatteryTracker::kCriticalDwellMs + 1 + BatteryTracker::kCriticalDwellMs);
  CHECK(t.level() == BatteryLevel::Critical);
}

TEST_CASE("charging suppresses Critical but not Low") {
  BatteryTracker t;
  for (uint32_t ms = 0; ms <= BatteryTracker::kCriticalDwellMs * 2;
       ms += BatteryTracker::kCriticalDwellMs / 4)
    t.update(good(1, /*charging=*/true), ms);
  // A device on the cable must not shut down, however long it has been flat. The
  // banner still shows: the battery IS low, and saying so is true.
  CHECK(t.level() == BatteryLevel::Low);
  // Unplugged, the dwell starts now rather than being satisfied by the time spent
  // charging -- a run that was suppressed was not a run.
  t.update(good(1, /*charging=*/false), BatteryTracker::kCriticalDwellMs * 2 + 1);
  CHECK(t.level() == BatteryLevel::Low);
  t.update(good(1, /*charging=*/false),
           BatteryTracker::kCriticalDwellMs * 2 + 1 + BatteryTracker::kCriticalDwellMs);
  CHECK(t.level() == BatteryLevel::Critical);
}

TEST_CASE("a failed reading holds the level where it was") {
  BatteryTracker t;
  t.update(good(1), 0);
  CHECK(t.level() == BatteryLevel::Low);
  // A transient I2C miss is not a fact about the battery. "Flat" and "did not
  // answer" are different claims -- the same rule percent()'s -1 already keeps --
  // and a dwell satisfied by silence would shut the device down on a bus glitch.
  t.update(failed(), BatteryTracker::kCriticalDwellMs * 2);
  CHECK(t.level() == BatteryLevel::Low);
  t.update(good(50), BatteryTracker::kCriticalDwellMs * 2 + 1);
  CHECK(t.level() == BatteryLevel::Normal);
}

TEST_CASE("a failed reading cannot complete a dwell that was already running") {
  BatteryTracker t;
  t.update(good(1), 0);
  t.update(failed(), BatteryTracker::kCriticalDwellMs);
  // The clock ran, but nothing confirmed the battery is still flat.
  CHECK(t.level() == BatteryLevel::Low);
  t.update(good(1), BatteryTracker::kCriticalDwellMs + 1);
  CHECK(t.level() == BatteryLevel::Critical);
}

TEST_CASE("the ladder's thresholds are ordered and X4-reachable") {
  // The X4's ADC reports 10% notches (LIION_NOTCH_MV), so a threshold it cannot
  // express is a threshold that never fires there. Asserted rather than trusted to
  // a comment, because changing one of these numbers is exactly the edit that would
  // silently disarm the feature on half the fleet.
  static_assert(BatteryTracker::kCriticalPercent < BatteryTracker::kLowPercent, "");
  static_assert(BatteryTracker::kLowPercent < BatteryTracker::kResumePercent, "");
  // Critical must be reachable as the notch `0`.
  static_assert(BatteryTracker::kCriticalPercent < 10, "");
  // Low must be reachable as the notch `10`.
  static_assert(BatteryTracker::kLowPercent % 10 == 0, "");
  // Resume must need the notch `20`, which is a DIFFERENT voltage from the one
  // Critical fires at -- 3.71 V against 3.565 V. Without that gap the shutdown edge
  // and the resume edge are the same midpoint and a device on the cable flaps.
  static_assert(BatteryTracker::kResumePercent > 10, "");
  static_assert(BatteryTracker::kResumePercent <= 20, "");
  CHECK(true);
}
```

- [ ] **Step 2: Run and verify they fail**

```bash
cmake -S . -B build && cmake --build build -j 2>&1 | tail -20
```

Expected: **compile failure** — `BatteryLevel` is not declared, `t.level()` does not exist, `kLowPercent` does not exist. A compile failure is the right first failure here; there is nothing to run yet.

- [ ] **Step 3: Add the ladder to `core/include/reader/battery_tracker.h`**

Add above `class BatteryTracker`:

```cpp
// WHICH RUNG OF THE SAFETY LADDER THE PACK IS ON.
//
// Not a percentage the caller thresholds itself: a threshold spelled at the call
// site is a threshold that can be spelled differently at the next one, and this
// project has shipped a dead button twice from exactly that shape. The shell asks
// which rung; the numbers live here with their derivation.
enum class BatteryLevel : uint8_t { Normal, Low, Critical };
```

Add to the `public:` section of `BatteryTracker`, beside the existing constants:

```cpp
  // --- The ladder ---------------------------------------------------------------
  //
  // THE X4 REPORTS 10% NOTCHES, which is what sets all three of these. Its ADC path
  // walks LIION_NOTCH_MV[11] and returns a multiple of ten, so a threshold that is
  // not expressible on that curve is one that never fires on half the fleet. The
  // board's `BATTERY LOW - 5%` is the value DISPLAYED, never the trigger.

  // The X4's lowest non-zero notch, ~3.68 V. Anything lower is unreachable there
  // until the pack is already at 0.
  static constexpr int kLowPercent = 10;

  // X4-reachable only as the notch `0`, which is <=3.565 V -- the midpoint of the
  // 3.45 V and 3.68 V anchors. The 0% anchor is deliberately above the cell's
  // protection cut-off and leaves headroom for the sag under an e-ink refresh, so
  // this is minutes of runtime rather than the cliff. Read literally on an X3.
  static constexpr int kCriticalPercent = 3;

  // THE HYSTERESIS, AND IT IS THE LOAD-BEARING NUMBER. The resume gate in setup()
  // refuses to wake below this. It must require the X4's *20%* notch, ~3.71 V,
  // because anything the X4 can satisfy at the 0/10 boundary puts the shutdown edge
  // and the resume edge at the SAME 3.565 V midpoint -- and a device on the cable
  // then shuts down, charges for a minute, wakes, discharges and shuts down again.
  // 145 mV between the two edges is what makes them different voltages.
  static constexpr int kResumePercent = 15;

  // CONTINUOUS, in kUnlatchMs's idiom and for its reason. A panel refresh is the
  // heaviest load this device draws and the SDK's own notch table says the 0%
  // anchor leaves headroom for that sag -- so one low reading is not a flat pack.
  // The poll already runs only in the shell's `quiet` window, which excludes a
  // sample taken mid-waveform; this is the belt to that braces.
  static constexpr uint32_t kCriticalDwellMs = 10u * 1000u;

  BatteryLevel level() const { return level_; }
```

- [ ] **Step 4: Implement the ladder inside `update()`**

Insert at the **end** of `update()`, after the existing `if (r.chargingKnown) { ... }` block:

```cpp
    // --- The ladder ---------------------------------------------------------------
    //
    // AFTER the blocks above, so it reads the values this reading has already
    // installed rather than a second copy of them. One update(), one level: two
    // objects fed the same reading would be a caller list, and the shell would be
    // free to feed one and forget the other.
    //
    // A READING THAT DID NOT ANSWER CHANGES NOTHING. It cannot lower the level (a
    // bus glitch is not a flat pack) and it cannot advance the dwell (a dwell
    // satisfied by silence is a shutdown nothing confirmed). "Flat" and "did not
    // answer" stay different claims, exactly as percent()'s kUnknownPercent keeps
    // them.
    if (!r.percentKnown) return;

    if (percent_ > kLowPercent) {
      level_ = BatteryLevel::Normal;
      sawCriticalSinceMs_ = 0;
      inCriticalRun_ = false;
      return;
    }

    level_ = BatteryLevel::Low;

    // CHARGING SUPPRESSES CRITICAL AND NOT LOW. A device on the cable must not shut
    // down; but the battery IS low, and the banner saying so is true. On an X4
    // charging() is never known -- there is no charge-status pin -- so it never
    // suppresses there, and shutdown-then-refuse-to-wake is exactly right for a
    // flat X4 on a cable: the glass says CHARGE TO WAKE, and it does.
    if (percent_ > kCriticalPercent || charging()) {
      sawCriticalSinceMs_ = 0;
      inCriticalRun_ = false;
      return;
    }

    if (!inCriticalRun_) {
      inCriticalRun_ = true;
      sawCriticalSinceMs_ = nowMs;
      return;
    }
    // Unsigned difference, so this is correct across the ~49-day millis() wrap, as
    // kUnlatchMs's dwell and every quiet-window gate in the shell are.
    if (static_cast<uint32_t>(nowMs - sawCriticalSinceMs_) >= kCriticalDwellMs)
      level_ = BatteryLevel::Critical;
```

Add to the `private:` section:

```cpp
  BatteryLevel level_ = BatteryLevel::Normal;
  bool inCriticalRun_ = false;
  uint32_t sawCriticalSinceMs_ = 0;
```

- [ ] **Step 5: Run the tests**

```bash
cmake --build build -j && ctest --test-dir build --output-on-failure 2>&1 | tail -15
```

Expected: PASS, all cases.

- [ ] **Step 6: Prove them by mutation**

Run each of these, confirm the named failure count, then **restore with `cp` from a backup** — never `git checkout`, which would revert the feature along with the mutation.

```bash
cp core/include/reader/battery_tracker.h /tmp/bt.bak
```

| mutation | must fail |
|---|---|
| `>= kCriticalDwellMs` → `> 0` (no dwell) | ≥ 3 assertions |
| drop the `!r.percentKnown` early return | ≥ 2 assertions |
| drop `|| charging()` from the suppression | ≥ 1 assertion |
| `sawCriticalSinceMs_ = nowMs` → leave it at its old value on a restart | ≥ 1 assertion |

```bash
cp /tmp/bt.bak core/include/reader/battery_tracker.h && touch core/include/reader/battery_tracker.h
```

`touch` matters: `cp` and a compile inside the same second leave make thinking the object is current, so the *fixed* source tests as though it were still mutated.

- [ ] **Step 7: Commit**

```bash
make test
git add core/include/reader/battery_tracker.h test/unit/test_battery_tracker.cpp
git commit -m "feat(battery): BatteryTracker answers which rung of the safety ladder the pack is on"
```

---

# Phase D — the banner

### Task 5: Extract `drawBadge`

`BatteryEmpty.dc.html`'s `CHARGE TO WAKE` badge is byte-identical to `Sleep.dc.html`'s `HOLD POWER TO WAKE` (Task 2 checked this). That is the second copy.

**Files:**
- Modify: `core/include/reader/components.h`, `core/src/components.cpp`, `core/src/theme_quiet.cpp:1082-1104`

- [ ] **Step 1: Declare it in `core/include/reader/components.h`**

Add beside `drawActionButton`:

```cpp
// THE BOARDS' BOTTOM BADGE: a 1px-outlined box measured from the BOTTOM of the
// panel, holding one tracked caps label. design/Sleep.dc.html's `HOLD POWER TO
// WAKE` and design/BatteryEmpty.dc.html's `CHARGE TO WAKE` are the same box to the
// pixel -- 34px from the bottom, 1px border, 8/18 padding, --t-meta at 0.2em --
// which is what makes this an extraction rather than a generalisation.
//
// IT SIZES ITSELF TO THE LABEL and is centred on the panel, so the two screens'
// different words need no second set of numbers. Returns the badge's top y, which
// a caller that stacks anything above it needs and neither of today's two do.
//
// The label's tracking is the BADGE's (0.2em), not the hint bar's -- a hint label
// sits beside a mark and this one stands alone. One spelling for one kind of line.
int drawBadge(Framebuffer& fb, const Font& font, std::string_view label, Plane plane);
```

- [ ] **Step 2: Define it in `core/src/components.cpp`**

Move the constants and the body out of `theme_quiet.cpp`:

```cpp
namespace {
// design/Sleep.dc.html and design/BatteryEmpty.dc.html, identically.
constexpr int kBadgeBottom = 34;
constexpr int kBadgePadX = 18;
constexpr int kBadgePadY = 8;
constexpr int kBadgeBorder = 1;
constexpr int kBadgeEm = 200;  // letter-spacing: 0.2em
}  // namespace

int drawBadge(Framebuffer& fb, const Font& font, std::string_view label, Plane plane) {
  const Tracking track = trackingEm(font, kBadgeEm);
  const int labelW = font.measure(label, track);
  const int w = labelW + 2 * (kBadgeBorder + kBadgePadX);
  const int h = font.lineHeight() + 2 * (kBadgeBorder + kBadgePadY);
  const int x = centreIn(0, fb.width(), w);
  const int y = fb.height() - kBadgeBottom - h;
  fb.fillRect(x, y, w, h, true);
  outlineRect(fb, x, y, w, h, kBadgeBorder);
  drawText(fb, font, x + kBadgeBorder + kBadgePadX,
           baselineIn(font, y + kBadgeBorder + kBadgePadY, font.lineHeight()), label, Ink::Black,
           track, plane);
  return y;
}
```

- [ ] **Step 3: Call it from `renderSleep`**

In `core/src/theme_quiet.cpp`, replace the seven lines from `const int noteW = note.measure(...)` through the closing `drawText(...)` of the badge block with:

```cpp
  drawBadge(fb, note, vm.note, plane);
```

Delete `kSleepBadgeBottom`, `kSleepBadgePadX`, `kSleepBadgePadY` and `kSleepBadgeBorder`, and `kSleepNoteEm` **only if nothing else reads it**:

```bash
grep -n "kSleepNoteEm\|kSleepBadge" core/src/theme_quiet.cpp
```

- [ ] **Step 4: The extraction's proof is that NO GOLDEN MOVES**

```bash
cmake --build build -j && ctest --test-dir build --output-on-failure 2>&1 | tail -15
```

Expected: PASS, and in particular `sleep_quiet`, `sleep_quiet_x3`, `sleep_idle`, `sleep_idle_x3`, `sleep_waking`, `sleep_waking_x3`, `sleep_cover_details` and `sleep_cover_details_x3` unchanged. **A moved sleep golden means the extraction changed the drawing and must be fixed, not blessed.**

- [ ] **Step 5: Prove the goldens can see this code at all**

`cp core/src/components.cpp /tmp/c.bak`, change `kBadgeBottom` to `35`, rebuild, and confirm **at least 6 golden failures**. Then `cp /tmp/c.bak core/src/components.cpp && touch core/src/components.cpp`. A refactor whose tests cannot fail proves nothing.

- [ ] **Step 6: Commit**

```bash
make test
git add core/include/reader/components.h core/src/components.cpp core/src/theme_quiet.cpp
git commit -m "refactor(components): drawBadge, because BatteryEmpty is the second copy of Sleep's"
```

### Task 6: The banner latch on `ReaderScreen`

**Files:**
- Modify: `core/include/reader/viewmodel.h`, `core/include/reader/screen_reader.h`, `core/src/screen_reader.cpp`
- Test: `test/unit/test_screen_reader_battery.cpp` (create)

- [ ] **Step 1: Write the failing test**

Create `test/unit/test_screen_reader_battery.cpp`:

```cpp
// The low-battery banner's latch. It lives on ReaderScreen rather than in the
// shell because `shell/` has no test harness and five bugs have hidden there --
// and because `ANY BUTTON` is a BINDING: a bar cannot promise what nothing has
// bound, and the thing that binds it is onGesture.
#include <memory>

#include "doctest.h"
#include "ramp.h"
#include "reader_fixture.h"
#include "reader/screen_reader.h"
#include "reader/theme_quiet.h"

using namespace reader;
using readerfix::Body;
using readerfix::longChapter;

namespace {
// A Reader over the long in-memory chapter, at the X4's own column. The fixture is
// the one every other paging test uses, so a failure here is about the banner.
struct Fixture {
  ramp::Ramp ramp;
  QuietTheme theme;
  Body body;
  PageMetrics m;
  std::unique_ptr<ReaderScreen> scr;

  Fixture() {
    theme.readerMetrics(480, 800, ramp.fonts, body.face, Settings{}, m);
    scr = std::make_unique<ReaderScreen>(longChapter(), body.face, m);
  }
};

GestureEvent g(Gesture what) {
  GestureEvent e;
  e.what = what;
  return e;
}
}  // namespace

TEST_CASE("the banner is absent until it is armed, and -1 is how that is spelled") {
  Fixture f;
  // ONE FIELD, ONE SENTINEL. A separate bool would let the condition be spelled
  // twice, and two spellings of one fact is what this project has a rule about.
  CHECK(f.scr->vm().batteryLowPercent == -1);
  f.scr->setBatteryLow(5);
  CHECK(f.scr->vm().batteryLowPercent == 5);
  f.scr->setBatteryLow(-1);
  CHECK(f.scr->vm().batteryLowPercent == -1);
}

TEST_CASE("EVERY gesture dismisses the banner and does nothing else") {
  // The board's right slot says ANY BUTTON, so every one of them has to clear it --
  // including the ones this screen would otherwise act on. Back POPS the Reader, so
  // it is the case that matters most: without this, the first press after a warning
  // would leave the book.
  for (Gesture what : {Gesture::Next, Gesture::Prev, Gesture::AltNext, Gesture::AltPrev,
                       Gesture::Activate, Gesture::Back}) {
    Fixture f;
    const int pageBefore = f.scr->vm().page;
    f.scr->setBatteryLow(5);
    const Action a = f.scr->onGesture(g(what));
    CHECK(a.kind == Action::Kind::Redraw);
    CHECK(f.scr->vm().batteryLowPercent == -1);
    // AND NOTHING ELSE. The press is spent dismissing, which is exactly what the
    // bar promised.
    CHECK(f.scr->vm().page == pageBefore);
  }
}

TEST_CASE("with no banner up, a page turn is a page turn") {
  // The mutation guard for the case above: if the dismissal branch were
  // unconditional, this screen would never turn a page again.
  Fixture f;
  const int before = f.scr->vm().page;
  const Action a = f.scr->onGesture(g(Gesture::Next));
  CHECK(a.kind == Action::Kind::Redraw);
  CHECK(f.scr->vm().page == before + 1);
}

TEST_CASE("re-arming after a dismissal shows the banner again") {
  // A wake is a chip reset, so a low battery shows the banner again on every wake;
  // within one session the shell re-arms on a fresh entry into Low. Neither is this
  // class's business -- what it must do is accept being armed twice.
  Fixture f;
  f.scr->setBatteryLow(5);
  f.scr->onGesture(g(Gesture::Next));
  CHECK(f.scr->vm().batteryLowPercent == -1);
  f.scr->setBatteryLow(4);
  CHECK(f.scr->vm().batteryLowPercent == 4);
}

TEST_CASE("the banner does not survive a page the screen lays for another reason") {
  // syncVm() rebuilds the view model on every movement of the reading position, so
  // the field has to be one syncVm does not clobber. Asserted because the obvious
  // implementation -- a plain vm_ field written only by the setter -- is right, and
  // the obvious BUG is a syncVm that resets it.
  Fixture f;
  f.scr->setBatteryLow(7);
  f.scr->relayout();
  CHECK(f.scr->vm().batteryLowPercent == 7);
}
```

- [ ] **Step 2: Run and verify it fails**

```bash
cmake -S . -B build && cmake --build build -j 2>&1 | tail -20
```

Expected: compile failure — `setBatteryLow` and `batteryLowPercent` do not exist. (`cmake -S . -B build` is required: CMake uses `file(GLOB)` and will not see the new file otherwise.)

- [ ] **Step 3: Add the view-model field**

In `core/include/reader/viewmodel.h`, inside `struct ReaderViewModel`, after `anchorLabel`:

```cpp
  // --- The low-battery banner, and -1 MEANS THERE IS NONE -------------------------
  //
  // design/LowBattery.dc.html: an inverted 78px band over the bottom of the page.
  // One field with a sentinel rather than a bool and an int, for anchorLabel's
  // reason -- the field IS the condition, so it cannot be spelled twice and the two
  // spellings cannot drift.
  //
  // IT IS DRAWN OVER THE PAGE AND NEVER DISPLACES IT. The band inside the column
  // would take a default page from 12 lines to 10 and re-paginate the whole
  // chapter, at the moment the device has least energy to spend and with the
  // reader's page moving under them -- which is the identical reasoning anchorLabel
  // carries for the footer's third field, turned ninety degrees.
  int batteryLowPercent = -1;
```

- [ ] **Step 4: Add the setter**

In `core/include/reader/screen_reader.h`, beside `setAnchor`:

```cpp
  // ARM OR DISARM THE LOW-BATTERY BANNER. `-1` disarms -- ONE ARGUMENT AND ONE
  // SENTINEL, the same one the view model carries, because a (bool, int) pair would
  // spell the condition twice.
  //
  // It writes the view model directly rather than a shadow field: syncVm() must not
  // clobber it, and the simplest way to guarantee that is for there to be nothing
  // for syncVm() to clobber it from.
  void setBatteryLow(int percent) { vm_.batteryLowPercent = percent; }
```

- [ ] **Step 5: Add the dismissal to `onGesture`**

In `core/src/screen_reader.cpp`, insert as the **first statement** of `ReaderScreen::onGesture`, before the `switch`:

```cpp
  // ANY BUTTON DISMISSES THE BANNER, AND SPENDS THE PRESS. Before the switch and
  // outside it, because it has to reach gestures this screen does not otherwise act
  // on -- and Back is the one that matters: it POPS the Reader, so without this the
  // first press after a warning would leave the book.
  //
  // The press is spent dismissing and does nothing else, which is exactly what
  // design/LowBattery.dc.html's `ANY BUTTON` slot promises. A bar cannot promise
  // what nothing has bound, and this is the binding.
  //
  // Power is not seen here and does not need to be: the shell handles it before
  // dispatch and sleeps, and the banner goes with the RAM.
  if (vm_.batteryLowPercent >= 0) {
    vm_.batteryLowPercent = -1;
    return Action::redraw();
  }
```

- [ ] **Step 6: Run the tests**

```bash
cmake --build build -j && ctest --test-dir build --output-on-failure 2>&1 | tail -15
```

Expected: PASS.

- [ ] **Step 7: Prove by mutation**

`cp core/src/screen_reader.cpp /tmp/sr.bak`. Drop the `>= 0` guard so the branch is unconditional → **"with no banner up, a page turn is a page turn" must fail**. Move the block *inside* the `switch`'s `Next` case → **the Back and Activate iterations must fail**. Restore with `cp` + `touch`.

- [ ] **Step 8: Commit**

```bash
make test
git add core/include/reader/viewmodel.h core/include/reader/screen_reader.h core/src/screen_reader.cpp test/unit/test_screen_reader_battery.cpp
git commit -m "feat(reader): the low-battery banner's latch, and ANY BUTTON is what binds it"
```

### Task 7: Draw the banner

**Files:**
- Modify: `core/src/theme_quiet.cpp` (`renderReader`)
- Test: `test/unit/test_theme_battery_golden.cpp` (create)

- [ ] **Step 1: Write the failing golden**

Create `test/unit/test_theme_battery_golden.cpp`:

```cpp
// design/LowBattery.dc.html. The Reader declares Fidelity::Grayscale, so this goes
// through checkGoldenGray -- a Mono golden of this screen would pin the wrong thing
// convincingly.
#include <memory>
#include <string>

#include "doctest.h"
#include "golden.h"
#include "ramp.h"
#include "reader/framebuffer.h"
#include "reader/screen_reader.h"
#include "reader/screens.h"
#include "reader/theme_quiet.h"

using namespace reader;

TEST_CASE("QuietTheme renders the low-battery banner to golden at both geometries") {
  ramp::Ramp ramp;
  QuietTheme theme;
  Body body;  // from reader_fixture.h via screens.h's demo path -- see below

  auto renderOne = [&](int w, int h, const std::string& name) {
    PageMetrics m;
    theme.readerMetrics(w, h, ramp.fonts, body.face, Settings{}, m);
    DemoScreenFactory factory;
    factory.setReaderBody(&body.face);
    factory.setReaderMetrics(m);
    factory.setReaderDemo();
    std::unique_ptr<Screen> scr = factory.create(ScreenId::Reader);
    REQUIRE(scr != nullptr);
    REQUIRE(scr->fidelity() == Fidelity::Grayscale);
    auto* r = static_cast<ReaderScreen*>(scr.get());
    // The settled state, as test_theme_reader_golden.cpp renders it: the board draws
    // `53 / 890`, not the transient `1 / —`.
    r->completeIndex();
    // The board's own 5%.
    r->setBatteryLow(5);
    Framebuffer lsb(w, h), msb(w, h);
    scr->render(lsb, ramp.fonts, theme, Plane::Lsb);
    scr->render(msb, ramp.fonts, theme, Plane::Msb);
    golden::checkGoldenGray(lsb, msb, name);
  };

  SUBCASE("X4 480x800") { renderOne(480, 800, "low_battery"); }
  SUBCASE("X3 528x792") { renderOne(528, 792, "low_battery_x3"); }
}

TEST_CASE("the banner does not change how many lines the page holds") {
  // THE PROPERTY THE WHOLE DESIGN RESTS ON. A band inside the column takes a
  // default page from 12 lines to 10 and re-paginates the chapter; drawn over it,
  // the page is byte-for-byte the page that was already there. Asserted on the
  // LINES rather than on the pixels, because the pixels legitimately differ.
  ramp::Ramp ramp;
  QuietTheme theme;
  Body body;
  PageMetrics m;
  theme.readerMetrics(480, 800, ramp.fonts, body.face, Settings{}, m);
  DemoScreenFactory factory;
  factory.setReaderBody(&body.face);
  factory.setReaderMetrics(m);
  factory.setReaderDemo();
  std::unique_ptr<Screen> scr = factory.create(ScreenId::Reader);
  REQUIRE(scr != nullptr);
  auto* r = static_cast<ReaderScreen*>(scr.get());
  r->completeIndex();
  const size_t before = r->page().lines.size();
  const int pageBefore = r->vm().page;
  const int totalBefore = r->vm().pageTotal;
  r->setBatteryLow(5);
  CHECK(r->page().lines.size() == before);
  CHECK(r->vm().page == pageBefore);
  CHECK(r->vm().pageTotal == totalBefore);
}
```

**Note on `Body`:** `test_theme_reader_golden.cpp` gets it from `reader_fixture.h` as `readerfix::Body`. Do the same here — add `#include "reader_fixture.h"` and `using readerfix::Body;` in an anonymous namespace, matching that file exactly.

- [ ] **Step 2: Run and verify it fails**

```bash
cmake -S . -B build && cmake --build build -j && ctest --test-dir build -R battery --output-on-failure 2>&1 | tail -20
```

Expected: FAIL — `test/golden/low_battery.png` does not exist, and the test names `build/low_battery_candidate.png`.

- [ ] **Step 3: Draw the band in `renderReader`**

In `core/src/theme_quiet.cpp`, add the board's constants beside `kReadFooterPadTop`:

```cpp
// design/LowBattery.dc.html's band: full-bleed, 78px, `padding: 0 24px`, a 12px
// gap between the mark and its label.
constexpr int kBannerH = 78;
constexpr int kBannerPadX = 24;
constexpr int kBannerGap = 12;
constexpr int kBannerLabelEm = 100;  // letter-spacing: 0.1em, both runs
```

Insert into `QuietTheme::renderReader`, **after** the footer block and as the last drawing statement of the function:

```cpp
  // --- The low-battery banner ----------------------------------------------------
  //
  // LAST, SO IT IS ON TOP. It is drawn OVER the page and never displaces it: the
  // band inside the column would take a default page from 12 lines to 10 and
  // re-paginate the whole chapter, at the moment the device has least energy to
  // spend and with the reader's page moving under them. So `columnH` is untouched
  // and the last line and a half of the page go under the band -- which is what
  // design/LowBattery.dc.html draws, with the band absolutely positioned against
  // the column's bottom for exactly this reason.
  //
  // FULL-BLEED, so it is placed from 0 and fb.width() rather than from kReadPadX.
  // Every inverted band on this device is.
  if (vm.batteryLowPercent >= 0) {
    const Font& label = fonts[Role::Label500];  // --t-label at 700 -- see below
    const int top = footerTop - kReadFooterPadTop - kBannerH;
    fb.fillRect(0, top, fb.width(), kBannerH, true);

    const Icon& warn = icons::kWarning;
    const int markY = top + centreIn(0, kBannerH, warn.h);
    // WHITE INK on a filled band. The board authors the triangle white for the same
    // reason, and drawIcon takes the ink rather than the icon carrying it.
    drawIcon(fb, warn, kBannerPadX, markY, Ink::White, plane);

    const Tracking track = trackingEm(label, kBannerLabelEm);
    const std::string text =
        std::string("BATTERY LOW") + "\xC2\xB7" + std::to_string(vm.batteryLowPercent) + "%";
    drawText(fb, label, kBannerPadX + warn.w + kBannerGap,
             baselineIn(label, top, kBannerH), text, Ink::White, track, plane);

    const Tracking anyTrack = trackingEm(meta, kBannerLabelEm);
    const int anyW = meta.measure("ANY BUTTON", anyTrack);
    drawText(fb, meta, fb.width() - kBannerPadX - anyW, baselineIn(meta, top, kBannerH),
             "ANY BUTTON", Ink::White, anyTrack, plane);
  }
```

**Two things to get right, both of which this repo has been bitten by:**

- **The middle dot must be its own string literal.** `"BATTERY LOW\xC2\xB7%d%%"` parses `\xB7` unbounded — a C++ hex escape has no length limit — and the ESP32's GCC *accepts* it while clang rejects it. Adjacent string literals end the escape. The code above already does this; do not "simplify" it into one literal.
- **`Role::Label500` is the role, and Task 1 already moved the board to it.** The ramp has no 23px/700 role (`font_manifest.h` carries `Label400`/`Label500` at 11pt), and the answer was a board change rather than a new asset — 15–20 KB of flash plus a `make fonts` pass, for one run. Do not reintroduce the fork here.

- [ ] **Step 4: Render the candidate and LOOK AT IT**

```bash
cmake --build build -j && ctest --test-dir build -R battery --output-on-failure 2>&1 | tail -10
```

Open `build/low_battery_candidate.png`. Say out loud what you see. Check specifically: the band is edge to edge, the triangle and both labels are **white**, the band's top edge does not touch the footer, and text runs *under* the band rather than stopping above it.

- [ ] **Step 5: Compare against the board before blessing**

```bash
python3 tools/compare-design.py --only low_battery --export build/overlay
```

Open `build/overlay/low_battery_design.png` and `build/overlay/low_battery_firmware.png` together. The band's height, its y, and both labels' baselines must agree. **Compare the percentage against the other GRAYSCALE screens, not against `reader_menu`'s ~3%:** a threshold-at-128 count over four levels inflates the figure. `reader` is the benchmark at 5.34%/6.38%.

- [ ] **Step 6: Bless, then prove the golden bites**

```bash
cp build/low_battery_candidate.png test/golden/low_battery.png
cp build/low_battery_x3_candidate.png test/golden/low_battery_x3.png
cmake --build build -j && ctest --test-dir build -R battery --output-on-failure
```

Then `cp core/src/theme_quiet.cpp /tmp/tq.bak` and run each mutation, confirming the count, restoring with `cp` + `touch` each time:

| mutation | must fail |
|---|---|
| `kBannerH` 78 → 79 | 2 goldens |
| `Ink::White` → `Ink::Black` on the label | 2 goldens |
| drop `- kReadFooterPadTop` from `top` | 2 goldens |
| drop the whole `if` block | 2 goldens |

- [ ] **Step 7: Commit**

```bash
make test
git add core/src/theme_quiet.cpp test/unit/test_theme_battery_golden.cpp test/golden/low_battery.png test/golden/low_battery_x3.png
git commit -m "feat(reader): draw the low-battery banner over the page, never into the column"
```

---

# Phase E — the shutdown screen

### Task 8: `ScreenId::BatteryEmpty` and its screen

**Files:**
- Modify: `core/include/reader/app.h`, `core/src/app.cpp`, `core/src/session_record.cpp`, `core/include/reader/viewmodel.h`
- Create: `core/include/reader/screen_battery_empty.h`, `core/src/screen_battery_empty.cpp`
- Test: `test/unit/test_screen_battery_empty.cpp` (create), `test/unit/test_focus_restore.cpp` (modify)

- [ ] **Step 1: Write the failing test**

Create `test/unit/test_screen_battery_empty.cpp`:

```cpp
#include "doctest.h"
#include "reader/screen_battery_empty.h"
#include "reader/session_record.h"

using namespace reader;

TEST_CASE("BatteryEmpty carries the board's own copy") {
  BatteryEmptyScreen s;
  CHECK(s.id() == ScreenId::BatteryEmpty);
  CHECK(s.vm().title == "BATTERY EMPTY");
  CHECK(s.vm().note == "CHARGE TO WAKE");
  CHECK(s.vm().message.find("Your page is saved") != std::string::npos);
}

TEST_CASE("BatteryEmpty is Mono and takes no input") {
  BatteryEmptyScreen s;
  // Mono: there is no photograph on this screen and no body text, so three
  // waveforms would buy nothing on a pack that has none to spend.
  CHECK(s.fidelity() == Fidelity::Mono);
  // NOBODY IS LEFT TO PRESS ANYTHING. The shell paints this and calls deep sleep,
  // so every gesture answers none() -- including Back, which everywhere else means
  // "go up".
  for (Gesture what : {Gesture::Next, Gesture::Prev, Gesture::AltNext, Gesture::AltPrev,
                       Gesture::Activate, Gesture::Back}) {
    GestureEvent e;
    e.what = what;
    CHECK(s.onGesture(e).kind == Action::Kind::None);
  }
}

TEST_CASE("BatteryEmpty draws no hint bar") {
  // A bar is a contract about four buttons, and this screen's four do nothing.
  BatteryEmptyScreen s;
  for (const std::string& h : s.vm().hints) CHECK(h.empty());
}

TEST_CASE("the session record can name BatteryEmpty") {
  // THE TABLE MUST GROW WITH THE ENUM. session_record.cpp had THREE bounds spelled
  // `<= ScreenId::Peek`, so appending BookEnd made sessionWireName fall through to
  // kNames[0] and serialise the new screen as `home` -- a reader idle-sleeping on
  // it would have woken on Home, with no failing test and no log line.
  CHECK(std::string(sessionWireName(ScreenId::BatteryEmpty)) != "home");
  CHECK(sessionScreenFromWire(sessionWireName(ScreenId::BatteryEmpty)) == ScreenId::BatteryEmpty);
}
```

Check the exact spelling of the two session-record functions before running:

```bash
grep -n "sessionWireName\|sessionScreenFromWire\|FromWire" core/include/reader/session_record.h
```

- [ ] **Step 2: Run and verify it fails**

```bash
cmake -S . -B build && cmake --build build -j 2>&1 | tail -20
```

Expected: compile failure — no `screen_battery_empty.h`.

- [ ] **Step 3: Append the enum member**

In `core/include/reader/app.h`, after `BookEnd`:

```cpp
  ,
  // design/BatteryEmpty.dc.html -- what the panel holds after a critical shutdown.
  // APPENDED for the reason ReaderMenu, Typography, Peek and BookEnd were: the
  // session record stores a screen by NAME, so an insertion could not silently
  // become another screen, but appending also leaves every existing ordinal where
  // it was.
  //
  // PAINTED DIRECTLY AND NEVER PUSHED, on Sleep's argument: the record names the
  // top of the stack, so pushing it would make the next wake restore INTO it.
  //
  // AND test_focus_restore.cpp's static_assert WILL NOT NOTICE THIS APPEND. It
  // compares the catalogue's length against a NAMED member, so appending satisfies
  // it unchanged -- exactly as appending Typography and then BookEnd did. That is
  // #42; the catalogue below is extended by hand.
  BatteryEmpty
```

- [ ] **Step 4: Extend the two name tables**

```bash
grep -n "ScreenId::BookEnd" core/src/app.cpp core/src/session_record.cpp
```

Add `BatteryEmpty` to `screenName()`'s table in `core/src/app.cpp` (`"battery-empty"`) and to `session_record.cpp`'s `kNames` (`"battery-empty"`). `session_record.cpp`'s table is `static_assert`ed against the enum's end, so **the build will fail until you do** — that assert is the guard that works.

- [ ] **Step 5: Add the view model**

In `core/include/reader/viewmodel.h`, beside `SdMissingViewModel`:

```cpp
// design/BatteryEmpty.dc.html. SdMissing's shape without the action slab, plus
// Sleep's badge -- and the hints are all empty, deliberately: the shell paints this
// and calls deep sleep, so there is nobody left to press anything and a bar is a
// contract about four buttons that do nothing.
struct BatteryEmptyViewModel {
  std::string title;    // "BATTERY EMPTY"
  std::string message;  // the paragraph under it, wrapped by the theme
  std::string note;     // the badge's label: "CHARGE TO WAKE"
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};
```

- [ ] **Step 6: Create `core/include/reader/screen_battery_empty.h`**

```cpp
#pragma once

#include "reader/app.h"
#include "reader/viewmodel.h"

namespace reader {

// design/BatteryEmpty.dc.html -- what the panel holds after a critical shutdown.
//
// A SCREEN RATHER THAN A SPECIAL CASE IN THE SHELL, for SleepScreen's reason: the
// simulator and the goldens can then render it like everything else, and `make
// compare` can measure it against its board.
//
// PAINTED DIRECTLY AND NEVER PUSHED, also for SleepScreen's reason -- the session
// record names the top of the stack, so pushing it would make the next wake restore
// INTO it. The shell's paintBatteryEmptyScreen() bypasses App, which means it owns
// the two things App normally does: the clear, and gFrameContentsUnknown.
class BatteryEmptyScreen : public Screen {
 public:
  BatteryEmptyScreen();

  ScreenId id() const override { return ScreenId::BatteryEmpty; }
  // NOBODY IS LEFT TO PRESS ANYTHING: this is painted and then the chip stops.
  // Back answers none() too, which is the one place on this device it does.
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;
  // ONE WAVEFORM. There is no photograph here and no body text, so four levels
  // would spend three waveforms on a pack that has none to spend.
  Fidelity fidelity() const override { return Fidelity::Mono; }

  const BatteryEmptyViewModel& vm() const { return vm_; }

 private:
  BatteryEmptyViewModel vm_;
};

}  // namespace reader
```

- [ ] **Step 7: Create `core/src/screen_battery_empty.cpp`**

```cpp
#include "reader/screen_battery_empty.h"

#include "reader/theme.h"

namespace reader {

BatteryEmptyScreen::BatteryEmptyScreen() {
  // THE BOARD'S OWN COPY, transcribed once and here. "Your page is saved" is a
  // promise saveReadingPosition("battery") keeps and markSleeping() redeems -- the
  // screen must not say it unless both run before it.
  vm_.title = "BATTERY EMPTY";
  vm_.message =
      "Your page is saved. The reader is shutting down \xE2\x80\x94 charge over USB-C to "
      "continue.";
  vm_.note = "CHARGE TO WAKE";
  // hints and holds stay empty -- see the header.
}

Action BatteryEmptyScreen::onGesture(const GestureEvent& g) {
  (void)g;
  return Action::none();
}

void BatteryEmptyScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                                Plane plane) const {
  theme.renderBatteryEmpty(fb, fonts, vm_, plane);
}

}  // namespace reader
```

The em dash is `\xE2\x80\x94` — the board writes `&mdash;`. Check `fontc.py`'s `CODEPOINTS`: U+2014 is in the subset, so this renders rather than showing a notdef box.

- [ ] **Step 8: Extend the focus-restore catalogue**

`test/unit/test_focus_restore.cpp` walks every `ScreenId`. Add `ScreenId::BatteryEmpty` to `kAllScreens` and **advance the `static_assert`'s named member to `ScreenId::BatteryEmpty + 1`**, with a comment saying it is still a named member and still #42.

- [ ] **Step 9: Build and run**

`renderBatteryEmpty` does not exist yet, so add a pure-virtual declaration to `core/include/reader/theme.h` and a stub override in `theme_quiet.h`/`.cpp` that does nothing — Task 9 fills it in. Then:

```bash
cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure 2>&1 | tail -15
```

Expected: PASS.

- [ ] **Step 10: Commit**

```bash
make test
git add core/include/reader/app.h core/src/app.cpp core/src/session_record.cpp core/include/reader/viewmodel.h core/include/reader/screen_battery_empty.h core/src/screen_battery_empty.cpp core/include/reader/theme.h core/include/reader/theme_quiet.h core/src/theme_quiet.cpp test/unit/test_screen_battery_empty.cpp test/unit/test_focus_restore.cpp
git commit -m "feat(battery-empty): the screen a critical shutdown paints, appended to ScreenId"
```

### Task 9: Render `BatteryEmpty`

**Files:**
- Modify: `core/src/theme_quiet.cpp`
- Test: `test/unit/test_theme_battery_golden.cpp`

- [ ] **Step 1: Write the failing golden**

Append to `test/unit/test_theme_battery_golden.cpp`:

```cpp
TEST_CASE("QuietTheme renders BatteryEmpty to golden at both geometries") {
  ramp::Ramp ramp;
  QuietTheme theme;
  BatteryEmptyScreen scr;
  // Mono, asserted before the plane is named -- a golden that pinned Bw while the
  // screen declared Grayscale would quietly test a path nothing paints.
  REQUIRE(scr.fidelity() == Fidelity::Mono);
  auto renderOne = [&](int w, int h, const std::string& name) {
    Framebuffer fb(w, h);
    fb.clear(true);
    scr.render(fb, ramp.fonts, theme, Plane::Bw);
    golden::checkGolden(fb, name);
  };
  SUBCASE("X4 480x800") { renderOne(480, 800, "battery_empty"); }
  SUBCASE("X3 528x792") { renderOne(528, 792, "battery_empty_x3"); }
}
```

Add `#include "reader/screen_battery_empty.h"` at the top.

- [ ] **Step 2: Run and verify it fails**

```bash
cmake --build build -j && ctest --test-dir build -R battery --output-on-failure 2>&1 | tail -10
```

Expected: FAIL — no golden file, candidate written.

- [ ] **Step 3: Implement `renderBatteryEmpty`**

In `core/src/theme_quiet.cpp`, modelled on `renderSdMissing` (read it first: it is at the `QuietTheme::renderSdMissing` definition and its column arithmetic is the pattern):

```cpp
void QuietTheme::renderBatteryEmpty(Framebuffer& fb, const FontSet& fonts,
                                    const BatteryEmptyViewModel& vm, Plane plane) {
  const Font& title = fonts[Role::Title700];
  const Font& body = fonts[Role::Body400];
  const Font& note = fonts[Role::Meta400];
  const Icon& mark = icons::kBatteryLarge;

  // SdMissing's column arithmetic, and for its reasons: `max-width: 400px` is a
  // MAXIMUM, so a narrower panel's margins win; the stack is accumulated in 1/64 px
  // because the paragraph's height is a fraction (1.55 x 29px is 44.95) and
  // rounding before halving would put the whole column half a pixel off centre.
  //
  // NO ACTION SLAB. SdMissing has RETRY; this screen has nothing to offer, because
  // the next statement in the shell is deep sleep. The badge below is not a button.
  const int usableW = fb.width() - 2 * kMargin;
  const int colW = usableW < kProseMaxW ? usableW : kProseMaxW;
  const int colX = centreIn(kMargin, usableW, colW);
  const Prose prose = wrapProse(body, vm.message, colW, kProseLeadEm);

  // The board's own 22px gap, which is NOT kPromptGap -- SdMissing's board says a
  // different number and this one says 22. Two boards, two gaps; a shared constant
  // here would be a claim neither board makes.
  constexpr int kBatteryGap = 22;
  const int stackF26 =
      pxToF26(mark.h + title.lineHeight() + 2 * kBatteryGap) + prose.heightF26();
  // CENTRED ON THE WHOLE PANEL, not on the area above a hint bar: this screen draws
  // no bar, so there is no bar to subtract. The board's centring block is
  // `flex-grow: 1` between the top of the panel and the badge's own padding.
  int yF26 = (pxToF26(fb.height()) - stackF26) >> 1;

  drawIcon(fb, mark, centreIn(kMargin, usableW, mark.w), f26ToPx(yF26), Ink::Black, plane);
  yF26 += pxToF26(mark.h + kBatteryGap);

  // 0.06em, the board's own -- narrower than the hint bar's and the badge's, both
  // of which are their own kind of line.
  constexpr int kBatteryTitleEm = 60;
  drawCentredText(fb, title, kMargin, usableW,
                  baselineInF26(title, yF26, pxToF26(title.lineHeight())), vm.title, Ink::Black,
                  trackingEm(title, kBatteryTitleEm), plane);
  yF26 += pxToF26(title.lineHeight() + kBatteryGap);

  drawProse(fb, body, prose, colX, colW, yF26, Ink::Black, plane);

  // The badge, from components.h -- byte-identical to Sleep's, which is what made it
  // an extraction rather than a generalisation.
  drawBadge(fb, note, vm.note, plane);
}
```

Check `drawCentredText`'s exact signature before writing this — `core/include/reader/components.h:309` — and `baselineInF26`'s at `core/include/reader/text.h:286`. Match the way `renderSdMissing` calls them.

- [ ] **Step 4: Render and LOOK AT IT**

```bash
cmake --build build -j && ctest --test-dir build -R battery --output-on-failure 2>&1 | tail -10
```

Open `build/battery_empty_candidate.png` and say what you see. Specifically: the battery mark reads as a **nearly empty battery** and not as a smudge or a pair of letters — an icon has passed review twice in this repo while reading as "OC". Check the paragraph wraps to three lines and the badge sits 34px off the bottom.

- [ ] **Step 5: Compare against the board**

```bash
python3 tools/compare-design.py --only battery_empty --export build/overlay
```

This screen is `Mono`, so its percentage is comparable to `sd_missing`'s and `home_empty`'s (~1.2–1.4%), **not** to the reader family's. A figure far above that means the column arithmetic is wrong, not that the rasterisers differ.

- [ ] **Step 6: Bless and prove by mutation**

```bash
cp build/battery_empty_candidate.png test/golden/battery_empty.png
cp build/battery_empty_x3_candidate.png test/golden/battery_empty_x3.png
cmake --build build -j && ctest --test-dir build -R battery --output-on-failure
```

Mutations (`cp` backup, mutate, run, `cp` + `touch` restore): `kBatteryGap` 22 → 23 must fail 2; `>> 1` → `/ 2` must fail **0** (identical on positive values — this one proving nothing is the expected answer and worth confirming); dropping `drawBadge` must fail 2.

- [ ] **Step 7: Commit**

```bash
make test
git add core/src/theme_quiet.cpp test/unit/test_theme_battery_golden.cpp test/golden/battery_empty.png test/golden/battery_empty_x3.png
git commit -m "feat(battery-empty): render the board, on SdMissing's column and Sleep's badge"
```

---

# Phase F — the simulator

### Task 10: `low_battery` and `battery_empty` subcommands

`tools/compare-design.py` already names both ids, so nothing is added to that list — they simply stop reporting not-implemented.

**Files:**
- Modify: `sim/main.cpp`, `core/src/screens.cpp`, `core/include/reader/screens.h`

- [ ] **Step 1: Confirm both are already on the sheet**

```bash
grep -n "low_battery\|battery_empty" tools/compare-design.py
```

Expected: two rows around lines 174–175.

- [ ] **Step 2: Do NOT add a demo view model**

Every other screen here has one, and this one must not. `BatteryEmptyScreen`'s constructor already holds the board's copy and the screen takes **no data from the device at all**, so a `demoBatteryEmptyVm()` would have exactly one possible body — construct the screen and copy its `vm()` — and **no consumer**: the factory case below constructs the screen directly, as does the golden.

That is `ListRow::trackingEm1000`'s shape, a parameter whose last producer walked out, caught here before it ever had one. Write nothing.

- [ ] **Step 3: Add the factory case**

In `core/src/screens.cpp`'s `create()` switch:

```cpp
    case ScreenId::BatteryEmpty:
      // A screen nothing can navigate TO still needs a source for its values, the
      // same way Sleep does -- the simulator and the goldens go through the factory.
      // The shell does NOT: paintBatteryEmptyScreen() constructs one directly,
      // because pushing it would make the next wake restore into it.
      return std::make_unique<BatteryEmptyScreen>();
```

- [ ] **Step 4: Add the two simulator subcommands**

In `sim/main.cpp`, beside the existing `isReader`/`isAnchored` flags:

```cpp
  const bool isLowBattery = std::strcmp(argv[1], "low_battery") == 0;
  const bool isBatteryEmpty = std::strcmp(argv[1], "battery_empty") == 0;
```

`low_battery` takes exactly the `reader` path (it *is* the reader) with one extra call before the render — follow whatever `isAnchored` does, and add:

```cpp
    // design/LowBattery.dc.html's own 5%. The board's number, so the sheet compares
    // the same string the design states.
    if (isLowBattery) static_cast<reader::ReaderScreen&>(app.top()).setBatteryLow(5);
```

`battery_empty` goes through the factory and renders one `Plane::Bw` pass, following the `sleep`/`sleep_idle` path.

- [ ] **Step 5: Update the usage string**

`sim/main.cpp:734` lists the subcommands. Add both. (#57 is open about that string being wrong in other ways; do not fix those here, but do not make it worse.)

- [ ] **Step 6: Render both and compare**

```bash
cmake --build build -j
./build/reader_sim low_battery /tmp/lb.png --canvas 528x792
./build/reader_sim battery_empty /tmp/be.png --canvas 528x792
python3 tools/compare-design.py --only low_battery
python3 tools/compare-design.py --only battery_empty
```

Expected: both render, neither reports not-implemented.

- [ ] **Step 7: Run the whole sheet**

```bash
make compare
```

~2.8 min. Expected: no board named-and-absent errors, and no screen the simulator knows failing to render.

- [ ] **Step 8: Commit**

```bash
make test
git add sim/main.cpp core/src/screens.cpp core/include/reader/screens.h
git commit -m "feat(sim): render low_battery and battery_empty, so make compare stops skipping them"
```

---

# Phase G — the shell

**Nothing from here on is executed by the desktop suite.** `shell/` has no harness. Every task below ends at `On glass`, and `make test` being green is not evidence for any of it. Build with `make firmware` and read the code you wrote.

### Task 11: Poll the battery on every screen

**Files:**
- Modify: `shell/src/main.cpp` (`refreshBatteryOnHome` ~3222, the poll site ~6463)

- [ ] **Step 1: Add the second caller**

Beside `refreshBatteryOnHome()`:

```cpp
// THE LADDER'S READING, ON ANY SCREEN. refreshBatteryOnHome() cannot serve it: it
// is gated on homeOnGlass() AND on gChargingObservable, so nothing outside Home
// ever reads the gauge and on an X4 -- which has no charge-status pin -- nothing
// reads it at all. Both gates are right for what they guard (the band, and the
// charge-latch repaint); neither can be reused for a safety mechanism.
//
// It goes through the SAME gBattery.update(), so the level and the band can never
// disagree about the percent.
//
// NO SpiBusGuard, and that is what makes 2 s affordable: this is I2C on the sensor
// bus and cannot race a panel refresh.
static void pollBatteryLevel() { gBattery.update(readBattery(), millis()); }
```

- [ ] **Step 2: Call it from the loop**

At the poll site, **replace** the `gChargingObservable && homeOnGlass()` condition's body so both jobs run off one timer:

```cpp
  if (quiet && static_cast<uint32_t>(millis() - gLastBatteryPollMs) >= kBatteryPollMs) {
    gLastBatteryPollMs = millis();
    ++gBatteryPolls;
    // THE LADDER FIRST AND UNCONDITIONALLY. It is the safety mechanism and must not
    // sit behind either of the band's two gates.
    pollBatteryLevel();
    // THE BAND'S REPAINT, still behind its own gates -- what it drives is the bolt
    // on Home, which is a Home question. refreshBatteryOnHome takes its own reading;
    // that is one extra I2C transaction on Home only, and one call site that cannot
    // disagree with itself is worth ~150 us (readBattery's own comment).
    if (gChargingObservable && homeOnGlass() && refreshBatteryOnHome()) {
      gApp->markDirty();
      logf("[battery] charging=%d -> repainting Home\n", gBattery.charging() ? 1 : 0);
      logFlush();
    }
  }
```

- [ ] **Step 3: Report the level on `[alive]`**

The `[alive]` line already carries `battery observable=%d pct=%d charging=%d polls=%lu`. Add `level=%d`, read straight off `gBattery.level()` — **an instrument that hides its state lets you attribute a silent shutdown to the wrong thing**, and a ladder that has quietly stopped being polled reads identically to one that is fine.

- [ ] **Step 4: Build**

```bash
make firmware 2>&1 | tail -5
```

Expected: success. ("Failed to install Python dependencies into penv" is transient — retry, and do not run two builds concurrently.)

- [ ] **Step 5: Commit**

```bash
git add shell/src/main.cpp
git commit -m "feat(shell): poll the battery ladder on every screen, not only on Home"
```

### Task 12: Arm the banner

**Files:**
- Modify: `shell/src/main.cpp`

- [ ] **Step 1: Add the arming logic beside the poll**

```cpp
// ARM THE BANNER ON A FRESH ENTRY INTO Low, and only while the Reader is on TOP.
//
// `gWasLow` is the edge, not the state: re-arming on every poll would put the
// banner back the moment the reader dismissed it, which is the dead-button defect
// with the sign flipped. It re-arms when the level leaves Low and comes back -- and
// a wake is a chip reset, so a low battery shows the banner again on every wake.
// That is the right behaviour and, when the Reader is what the wake restores, it
// rides that paint and costs no extra waveform.
//
// ON TOP rather than on the stack, unlike the Typography apply: the banner is drawn
// by renderReader, so with a Peek or the menu over it there is nothing to see. An
// armed banner under an overlay simply waits -- ReaderScreen holds the field and
// the overlay's pop reveals it.
static bool gWasLow = false;
static void armBannerIfNewlyLow() {
  const bool low = gBattery.level() != reader::BatteryLevel::Normal;
  const bool edge = low && !gWasLow;
  gWasLow = low;
  if (!edge || !gApp || gApp->top().id() != reader::ScreenId::Reader) return;
  static_cast<reader::ReaderScreen&>(gApp->top()).setBatteryLow(gBattery.percent());
  gApp->markDirty();
  logf("[battery] low pct=%d -> banner armed\n", gBattery.percent());
  logFlush();
}
```

Call it immediately after `pollBatteryLevel()` in the loop.

**`level() != Normal`, not `== Low`:** a device that reaches `Critical` without a poll landing on `Low` in between must still warn. The shutdown is ~10 s away and the banner is what explains it.

- [ ] **Step 2: Build and commit**

```bash
make firmware 2>&1 | tail -3
git add shell/src/main.cpp
git commit -m "feat(shell): arm the low-battery banner on a fresh entry into Low"
```

### Task 13: `criticalShutdown()`

**Files:**
- Modify: `shell/src/main.cpp`

- [ ] **Step 1: Read `sleepNow()` and `paintSleepScreen()` first**

```bash
sed -n '5126,5180p' shell/src/main.cpp   # paintSleepScreen
sed -n '5272,5440p' shell/src/main.cpp   # sleepNow
```

The ordering below is forced by the same hardware: the decode needs the card and the paint needs the panel, and `display.deepSleep()` then `powerDownRailsForSleep()` take both away.

- [ ] **Step 2: Write `paintBatteryEmptyScreen()`**

```cpp
// BYPASSES App, exactly as paintSleepScreen does and for its reason: pushing this
// screen would make the next wake RESTORE INTO IT.
//
// That moves two things App normally owns into this function -- the CLEAR, and
// gFrameContentsUnknown, because App's partial-repaint record would otherwise
// describe a frame that no longer exists. Nothing reads it before the chip stops,
// and leaving a lie there is a trap for the next person to paint after it.
static void paintBatteryEmptyScreen() {
  reader::BatteryEmptyScreen scr;
  SpiBusGuard bus;
  logf("[power] battery empty: painting the shutdown screen\n");
  logFlush();
  gFrame->clear(true);
  scr.render(*gFrame, *gFonts, gTheme, reader::Plane::Bw);
  gFrameContentsUnknown = true;
  // FULL, not fast: this is the last thing the panel is asked to do, for as long as
  // the pack stays flat, and a differential update would leave the previous screen's
  // residue under it.
  showOnePass(reader::RefreshMode::FULL);
}
```

Check `showOnePass`'s exact parameter spelling against `paintSleepScreen`'s FULL call before building.

- [ ] **Step 3: Write `criticalShutdown()`**

```cpp
// The pack is flat. Save, say so, and stop.
//
// [[noreturn]] like sleepNow, and reached from loop() rather than from a dispatch:
// a paint cannot be interrupted, so this must not run inside one.
[[noreturn]] static void criticalShutdown() {
  logf("[power] CRITICAL pct=%d charging=%d -> shutting down\n", gBattery.percent(),
       gBattery.charging() ? 1 : 0);
  logFlush();

  // FIRST, AND THE BOARD'S COPY DEPENDS ON IT. "Your page is saved" is a promise,
  // and this is what keeps it.
  saveReadingPosition("battery");
  paintBatteryEmptyScreen();

  display.deepSleep();
  freeink::PowerManager::powerDownRailsForSleep();

  // BOTH FLAGS. markSleeping() so that once charged the wake RESTORES the reader's
  // page rather than starting cold -- the other half of "your page is saved".
  // markCriticalShutdown() is what licenses setup()'s strict >= kResumePercent gate:
  // without it the gate would have to sit at the critical threshold itself (which
  // flaps), or refuse every boot below 15% (which would refuse a perfectly usable
  // 10% battery that never shut anything down).
  markSleeping();
  markCriticalShutdown();

  if (gLogToCard && gLogLen > 0) {
    logf("[log] battery empty\n");
    flushLogToCard();
  }
  freeink::PowerManager::deepSleepUntilPowerButton();
}
```

- [ ] **Step 4: Call it from `loop()`**

Immediately **before** the existing sleep check at `shell/src/main.cpp:6043`:

```cpp
  // BEFORE THE IDLE SLEEP, because a flat device should say why it stopped rather
  // than showing the ordinary sleep screen. Both are [[noreturn]]; whichever runs
  // first is the one the user sees.
  if (gBattery.level() == reader::BatteryLevel::Critical) criticalShutdown();
```

- [ ] **Step 5: Build and commit**

```bash
make firmware 2>&1 | tail -3
git add shell/src/main.cpp
git commit -m "feat(shell): criticalShutdown saves the page, paints BatteryEmpty and stops"
```

### Task 14: The `critShut` flag and the resume gate

**Files:**
- Modify: `shell/src/session.h`, `shell/src/session.cpp`, `shell/src/main.cpp`

- [ ] **Step 1: Add the flag**

In `shell/src/session.h`, beside `markSleeping`/`takeSleptFlag`:

```cpp
// THE CRITICAL-SHUTDOWN FLAG, and it is what makes `CHARGE TO WAKE` enforceable.
//
// The SoC cannot wake on charge -- the wake source is the power button and there is
// no charge-detect anywhere on that path -- so the board's promise is kept AFTER the
// wake, exactly as HOLD POWER TO WAKE is: setup() refuses a resume below
// BatteryTracker::kResumePercent and sleeps again.
//
// A SEPARATE FLAG FROM `slept`, and it has to be. The gate is strict (>= 15%), and a
// gate that strict applied to EVERY boot would refuse a device sitting at a
// perfectly usable 10% that never shut anything down. This flag is what scopes it to
// "the last shutdown was this one".
//
// TAKING IT CLEARS IT, for takeSleptFlag()'s reason -- one flag buys one refusal --
// and the refusal path gives it back, because a refused wake did not spend it.
//
//   key: "critShut"  uint8  1 = the last shutdown was a critical battery shutdown
bool markCriticalShutdown();
bool takeCriticalShutdownFlag();
```

In `shell/src/session.cpp`, add `constexpr const char* kKeyCritShut = "critShut";` beside `kKeySlept` (NVS caps a key at 15 chars — this is 8) and copy `markSleeping`/`takeSleptFlag` verbatim with the new key and their own log lines.

- [ ] **Step 2: Add the gate to `setup()`**

Immediately **after** `requireHeldPowerButtonOrSleepAgain(fromSleep, rst);` and **before** `display.begin();`:

```cpp
  // MAY NOT RETURN. See the definition: a resume on a pack that is still flat is
  // refused here, BEFORE display.begin(), so it costs no waveform and nothing on
  // the glass changes -- e-ink holds its last image, which is still the BATTERY
  // EMPTY screen the shutdown painted. This is requireHeldPowerButtonOrSleepAgain's
  // argument verbatim, one line later.
  requireChargeOrSleepAgain();
```

- [ ] **Step 3: Write the gate**

```cpp
// Returns only when the resume is accepted. A refusal re-arms both flags, powers the
// rails back down and does not return.
//
// AFTER THE HOLD GATE, because that is the cheaper refusal and already stands: a
// brush against the button in a bag should be refused for the HOLD reason without
// spending an I2C transaction.
//
// AFTER detectAndSelectBoard(), because it needs the profile -- and safely so:
// readStatus() tests BoardConfig::ACTIVE.batteryGauge.gaugeAddr LIVE rather than
// from BatteryMonitor's cached members, which is what makes the file-scope static
// (constructed before setup() runs, from the compile-time default) correct here.
static void requireChargeOrSleepAgain() {
  if (!takeCriticalShutdownFlag()) return;

  const reader::BatteryReading r = readBattery();
  const int pct = r.percentKnown ? r.percent : -1;

  // A READING THAT DID NOT ANSWER LETS THE DEVICE BOOT. The alternative is a brick:
  // a gauge that has failed would refuse every wake for ever, and the ladder in
  // loop() will shut the device down again in ten seconds if the pack really is
  // flat. Fail open here, fail safe there.
  if (pct < 0 || pct >= reader::BatteryTracker::kResumePercent) {
    logf("[boot] battery pct=%d critShut=1 -> RESUME (needs %d)\n", pct,
         reader::BatteryTracker::kResumePercent);
    logFlush();
    return;
  }

  logf("[boot] battery pct=%d critShut=1 -> refused, needs %d. Sleeping again; nothing "
       "was painted\n",
       pct, reader::BatteryTracker::kResumePercent);

  // GIVE BOTH FLAGS BACK. takeCriticalShutdownFlag() consumed one on the way in and
  // setup() consumed `slept` a few lines above; this wake spent neither, because the
  // device is going straight back to the state that set them. Without the `slept`
  // half the NEXT wake -- the real one, once charged -- reads as a cold start and the
  // reader loses the page they were on, which would be blamed on the restore.
  markCriticalShutdown();
  markSleeping();
  logFlush();

  // NO display.deepSleep(): begin() has not run, so there is no initialised driver
  // to ask, and the controller was put into DSLP by the shutdown this is returning
  // to. Cutting the rails is what holds the current down. Identical to the hold
  // gate's refusal path, and for the same reasons.
  freeink::PowerManager::powerDownRailsForSleep();
  freeink::PowerManager::deepSleepUntilPowerButton();
}
```

- [ ] **Step 4: Build**

```bash
make firmware 2>&1 | tail -3
```

- [ ] **Step 5: Re-read the flag ordering before committing**

Confirm by reading, not by memory:
1. `takeSleptFlag()` runs once, early, and this gate does **not** call it again.
2. The refusal path calls `markCriticalShutdown()` **and** `markSleeping()`.
3. The accept path calls neither — both flags stay consumed.

- [ ] **Step 6: Commit**

```bash
git add shell/src/session.h shell/src/session.cpp shell/src/main.cpp
git commit -m "feat(shell): CHARGE TO WAKE, enforced after the wake by a hysteresis gate"
```

### Task 15: `ENCRE_BATTERY_FAKE_PERCENT`

Draining a real pack to 3% on demand is not practical, and without this the whole ladder is unwalkable on glass.

**Files:**
- Modify: `shell/src/main.cpp`

- [ ] **Step 1: Add the override to `readBattery()`**

At the end of `readBattery()`, immediately before `return r;`:

```cpp
#ifdef ENCRE_BATTERY_FAKE_PERCENT
  // THE ONLY WAY TO WALK THIS LADDER ON GLASS. ENCRE_FS_SELFTEST's shape: absent by
  // default, so a normal build has neither the branch nor the log line.
  //
  // IT OVERRIDES THE PERCENT AND NOTHING ELSE. `charging` stays whatever the gauge
  // said, so an X3 on the cable still suppresses Critical -- which is one of the
  // five things that needs verifying on glass and would be untestable if this faked
  // it too.
  r.percentKnown = true;
  r.percent = ENCRE_BATTERY_FAKE_PERCENT;
  static bool announced = false;
  if (!announced) {
    announced = true;
    logf("[battery] FAKE percent=%d -- this is not a real reading\n", r.percent);
    logFlush();
  }
#endif
```

- [ ] **Step 2: Build both ways**

```bash
make firmware 2>&1 | tail -3
PLATFORMIO_BUILD_FLAGS="-DENCRE_BATTERY_FAKE_PERCENT=2" make firmware 2>&1 | tail -3
```

`PLATFORMIO_BUILD_FLAGS` **appends**; `--project-option` would *replace* `platformio.ini`'s flags and silently build for the wrong board. Do not run the two builds concurrently — they share the `uv` cache.

- [ ] **Step 3: Commit**

```bash
git add shell/src/main.cpp
git commit -m "test(shell): ENCRE_BATTERY_FAKE_PERCENT, so the ladder can be walked on glass"
```

---

# Phase H — documentation and handoff

### Task 16: Record it where the next person will look

**Files:**
- Modify: `docs/on-device-smoke-checklist.md`, `CLAUDE.md`

- [ ] **Step 1: Add the five on-glass checks**

Append to `docs/on-device-smoke-checklist.md`, in its failure-class grouping:

```markdown
## Battery states (#9, #10)

Not one line of the resume gate is executed by the desktop suite. Build with
`PLATFORMIO_BUILD_FLAGS="-DENCRE_BATTERY_FAKE_PERCENT=n" make firmware` to reach
each rung; `[alive] battery ... level=N` reports which rung the device thinks it is
on, and a silent shutdown with no `[power] CRITICAL` line is a poll that stopped
running rather than a ladder that fired.

- [ ] `=8`: the banner appears over the page without moving the text, and one press
      clears it. Check the page still holds twelve lines under the band.
- [ ] `=2`: `[power] CRITICAL` then `[power] battery empty` then the panel shows
      BATTERY EMPTY. The paint must COMPLETE before the rails go down.
- [ ] `=2`, then press power: `[boot] battery pct=2 critShut=1 -> refused`, and
      **nothing is repainted** — the glass still shows the screen from the step
      above. Repeat three times and confirm the flag is given back each time.
- [ ] Rebuild without the flag on a charged pack and press power: the reader's page
      comes back, not Home. That is `markSleeping()` in `criticalShutdown` working.
- [ ] X3 only, `=2` on the cable: the device does **not** shut down. `charging`
      suppresses `Critical`; an X4 cannot show this.
```

- [ ] **Step 2: Add a CLAUDE.md section**

Add under **The battery**, after the charging-latch paragraphs. State the ladder, the three constants **with the X4 notch derivation**, that the banner draws over the page and why (`columnH`, 12 lines → 10, `anchorLabel`'s precedent), that the resume gate is before `display.begin()` and therefore costs no waveform, and that `ENCRE_BATTERY_FAKE_PERCENT` exists. **Name the callers rather than writing "nothing uses this today"** — that is a claim with an expiry date and no owner, and this file records three screens acquiring `checkGoldenGray` while a comment said nothing used it.

- [ ] **Step 3: Verify the whole gate**

```bash
make test && make compare && make conventions && make firmware
```

- [ ] **Step 4: Commit**

```bash
git add docs/on-device-smoke-checklist.md CLAUDE.md
git commit -m "docs(battery): the ladder, the banner's overlay rule, and what only glass can answer"
```

- [ ] **Step 5: Move both cards to `On glass` — NOT to `Done`**

Do **not** write `Closes #9` or `Closes #10` in the PR: that would close the cards on desktop evidence, which is the failure this column exists to prevent.

```bash
gh project field-list 1 --owner Rukkaitto     # RE-READ the option ids; Release's have gone stale once
gh project item-list 1 --owner Rukkaitto --format json --limit 100
```

Then for each item id, with `Status` = `On glass` (`5012a8f7`):

```bash
gh project item-edit --id <item> --project-id PVT_kwHOAkvc3c4BhZ5g \
  --field-id PVTSSF_lAHOAkvc3c4BhZ5gzhgVwC4 --single-select-option-id 5012a8f7
```

`gh project item-edit` prints a GraphQL error and still **exits 0**, so check each result rather than trusting `set -e`.

---

## What is deliberately not in this plan

- **A banner on any screen but the Reader.** By the design-first rule that means a banner on every board first, which is a large design change. Home already states the percentage in its band.
- **A Settings row for the thresholds.** This is a safety mechanism, not a preference — `kWakeHoldMs`'s argument.
- **Fixing #42.** Task 8 advances the named member by hand and says so. Fixing it generally is its own card.
- **`millivolts` on `BatteryReading`.** The percent ladder is expressible on both backends and the screen prints a percentage; carrying a second quantity would let the trigger and the display disagree.
