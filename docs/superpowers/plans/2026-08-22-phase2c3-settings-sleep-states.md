# Phase 2C-3 — Settings, Sleep, Home's states Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the last four V1 chrome screens — a real Settings that edits the
three settings that exist, the Sleep screen, Home's empty and missing-book
variants — and delete `StubScreen`, which was scaffolding.

**Architecture:** Screens live in `core/` as `Screen` subclasses over view-models;
the theme draws. Settings mutates a `reader::Settings` and persists through a
callback the shell supplies, because `core/` must not know what a filesystem is.

**Tech Stack:** C++20 in `core/`, doctest + golden PNGs, `make compare` against
the boards.

---

## Decisions taken before writing this

**Settings draws all nine board rows; only the DEVICE rows respond.** Focus SKIPS
rows with nothing behind them, so an inert row cannot be selected and CHANGE can
never appear to do nothing. Typography lands with Phase 3's reader and Wi-Fi with
Phase 4; until then those rows read as information. The alternative — shipping
only the DEVICE section — would leave `make compare` with a large permanent
Settings mismatch, on exactly the screen most likely to drift, and that check has
already failed once by quietly covering less than it claimed.

**The Input Monitor is deleted, not rehomed.** It was reachable only from the stub
and no board lists it. Cost accepted and recorded: press classification is now
verified only by `test_input.cpp` on the desktop, and `shell/` is where four bugs
have hidden. If held-scroll or press classification needs eyes on glass again, it
comes back as a board row rather than as a hidden gesture.

**Removing `ScreenId::InputMonitor` does NOT bump the session record version.**
`session.cpp` says removing a wire name "is a version bump", and following that
letter would discard EVERY stored record — so a user sleeping on Library would
lose their place to a change that has nothing to do with them. Dropping just the
row makes a record naming `input-monitor` undecodable, which routes to "no usable
record" and Home: the same defined outcome, scoped to the one name that went away.

## File Structure

| File | Responsibility |
|---|---|
| `design/Settings.dc.html` | **Modify.** New `Refresh on change` row; `Full refresh` value corrected to the shipped default. |
| `core/include/reader/settings.h` | **Modify.** `sleepScreen` field if the Sleep-screen row is to work; otherwise unchanged. |
| `core/include/reader/screen_settings.h` / `core/src/screen_settings.cpp` | **Create.** The real screen: rows, focus that skips inert ones, CHANGE cycling values, a persist callback. |
| `core/include/reader/screen_sleep.h` / `core/src/screen_sleep.cpp` | **Create.** The Sleep screen. |
| `core/include/reader/viewmodel.h` | **Modify.** `SettingsViewModel`, `SleepViewModel`; Home's variants reuse `HomeViewModel`. |
| `core/src/theme_quiet.cpp` | **Modify.** `renderSettings`, `renderSleep`; delete `renderStub`. |
| `core/include/reader/screen_stub.h`, `core/src/screen_stub.cpp` | **Delete.** |
| `core/include/reader/screen_input_monitor.h`, `core/src/screen_input_monitor.cpp` | **Delete.** |
| `core/include/reader/app.h` | **Modify.** Drop `ScreenId::InputMonitor`. |
| `shell/src/session.cpp` | **Modify.** Drop the `input-monitor` wire row and its switch case. |
| `core/src/screens.cpp` | **Modify.** Build the real screens; drop the stub and the monitor. |
| `test/unit/test_screen_settings.cpp`, `test_screen_sleep.cpp` | **Create.** |

## Task 1: The Settings board gains the transition row

**Files:** Modify `design/Settings.dc.html`

- [ ] **Step 1: add the row and correct the stale value**

Add a `Refresh on screen change` row in DEVICE beside `Full refresh`, value `ON`.

**And do NOT "correct" `Full refresh`'s `EVERY 15 PAGES`**, which an earlier draft
of this plan called stale. It is not: `fullRefreshEvery` still exists as a setting
and defaults to 0; what was removed in 2B was 1-in-15 as the shipped DEFAULT, not
the setting. Boards mock plausible configured states throughout — `Sleep after`
shows `10 MIN` against a 5-minute default on the same screen — so a value that
differs from the default is the board working as intended.

- [ ] **Step 2: render both geometries and LOOK at them**

```bash
make compare COMPARE_ARGS="--only settings --export build/overlay"
```

Expected: `design ok`, and the DEVICE section is four rows. Open
`build/overlay/settings_x4_design.png` and confirm the new row's label does not
collide with its value at the X4's 480px width — the longest value in that column
is `EVERY 15 PAGES`, and the new label is longer than any existing one.

- [ ] **Step 3: commit the board alone**

The rule: a UI change goes into the design HTML first, then the implementation.
Committing separately keeps the board's history readable as design history.

## Task 2: `SettingsScreen`, over the three real settings

**Files:** Create `core/include/reader/screen_settings.h`, `core/src/screen_settings.cpp`; modify `core/include/reader/viewmodel.h`

- [ ] **Step 1: write the failing test for focus skipping inert rows**

```cpp
TEST_CASE("focus skips rows with nothing behind them") {
  reader::Settings s;
  reader::SettingsScreen scr(s, [](const reader::Settings&) { return true; });
  // The first focusable row is DEVICE's first, not TYPOGRAPHY's Font.
  CHECK(scr.vm().rows[scr.vm().focusedRow].label == "Sleep after");
  // Down from the last focusable row stays put rather than landing on Wi-Fi.
  scr.onEvent({reader::Button::Down, reader::PressKind::Short});
  scr.onEvent({reader::Button::Down, reader::PressKind::Short});
  scr.onEvent({reader::Button::Down, reader::PressKind::Short});
  CHECK(scr.vm().rows[scr.vm().focusedRow].label == "Refresh on change");
}
```

- [ ] **Step 2: run it, confirm it fails to compile (no such class)**

```bash
cmake -S . -B build && cmake --build build -j 2>&1 | grep screen_settings
```

- [ ] **Step 3: implement the screen**

Rows are a static table: label, section, and an optional `Field` naming which
setting it edits. `Field::None` means inert. CHANGE cycles the focused field
through a fixed list of values (sleep: 1/5/15/30 min; full refresh: never/5/15
pages; transition: on/off) and calls the persist callback. The callback returns
bool so a failed write can be logged by the shell without `core/` knowing why.

- [ ] **Step 4: run the tests**

```bash
ctest --test-dir build --output-on-failure
```

- [ ] **Step 5: commit**

## Task 3: `renderSettings` in the theme

**Files:** Modify `core/src/theme_quiet.cpp`, `core/include/reader/theme.h`, `core/include/reader/theme_quiet.h`

- [ ] **Step 1: implement it from the board's box model**

Section headers are `--t-meta` tracked caps; rows are label at `--t-label` with a
right-aligned value at `--t-value` 700. Derive the row height from the fonts as
`drawBookRow` does — do not pin it.

- [ ] **Step 2: bless a golden after looking at it**

```bash
ctest --test-dir build --output-on-failure
make compare COMPARE_ARGS="--only settings"
```

Inspect `build/settings_candidate.png` and say what is in it before blessing.
An inert row must be visually identical to a focusable one that is not focused —
if they differ, that is a design decision nobody made.

- [ ] **Step 3: commit**

## Task 4: The Sleep screen

**Files:** Create `core/include/reader/screen_sleep.h`, `core/src/screen_sleep.cpp`; modify `core/src/theme_quiet.cpp`

- [ ] **Step 1: read the board and implement it**

`design/Sleep.dc.html`. It takes no input — it is what is on the glass while the
device is asleep, so it has no hint bar and no focus. The shell paints it and
then sleeps.

- [ ] **Step 2: golden, inspected, then commit**

## Task 5: Home's empty and missing-book variants

**Files:** Modify `core/src/screens.cpp`, `core/src/theme_quiet.cpp`

- [ ] **Step 1: drive them from the existing `HomeViewModel`**

`design/HomeEmpty.dc.html` and `HomeMissing.dc.html` are Home with a different
CONTINUE block. They are states of one screen, not new screens — so no new
`ScreenId`, and `homeVmForCard` picks the variant from what the card holds.

- [ ] **Step 2: goldens for both, inspected, then commit**

## Task 6: Delete the scaffolding

**Files:** Delete `screen_stub.*`, `screen_input_monitor.*`; modify `app.h`, `screens.cpp`, `theme.h`, `theme_quiet.*`, `shell/src/session.cpp`

- [ ] **Step 1: delete the four files and the `renderStub` virtual**

- [ ] **Step 2: remove `ScreenId::InputMonitor`**

`-Wswitch` will point at every switch that handled it, which is the intended
prompt. `session.cpp`'s `wireNameOf` is one of them; drop the row from
`kWireNames` and the case, and do NOT bump `kVersion` — see the decision above.

- [ ] **Step 3: re-run cmake, because it globs**

```bash
cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
```

- [ ] **Step 4: confirm the firmware still builds and nothing grew**

```bash
~/.platformio/penv/bin/python -m platformio run -e xteink
```

Deleting two screens should REDUCE flash. If it does not, something still
references them.

- [ ] **Step 5: commit**

## Task 7: Docs and the flash handoff

- [ ] `CLAUDE.md`: Settings' inert rows and why focus skips them; the Input
      Monitor's deletion and what verification was given up with it.
- [ ] Roadmap: 2C-3 done, V1 chrome complete, what Phase 3 inherits.
- [ ] Report the flash command and name what only the panel can answer: whether an
      inert row reads as information rather than as a broken control, and whether
      the Sleep screen survives the actual sleep sequence.

## Self-Review

- Every board row in the table above maps to a `Field` or to `Field::None`.
- `SettingsViewModel` is defined in Task 2 and consumed in Task 3 under the same
  member names.
- No task references `StubViewModel` after Task 6.
- The persist callback's signature is identical in Task 2's test and its
  implementation.
