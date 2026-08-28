# Typography panel with live re-pagination — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Typography panel reached from the reader's menu that changes the body
face's size, margins, line spacing and alignment, previews them live, persists
them to `/.reader/settings.json`, and re-paginates the open chapter from the page
the reader was on.

**Architecture:** Four new fields on `reader::Settings`. A new
`TypographyScreen` in `core/` holding a `Settings` copy and a `SettingsSink*`,
the same pair `SettingsScreen` holds. **One mode:** Up/Down move the focus,
`CHANGE` cycles the focused value in place and wraps — Settings' own mechanism.
`BACK` answers a plain `pop()`, and the shell re-inits the body faces,
recomputes `Theme::readerMetrics` and re-paginates at the current block cursor
whenever a Reader is anywhere on the stack. **Two entry points:** the reader's menu
and a `READING` row in Settings.

**Tech Stack:** C++20 (`-fno-exceptions`, no RTTI on device), doctest, CMake,
Chrome-rendered `.dc.html` design boards, Python asset/compare tooling.

**Spec:** `docs/superpowers/specs/2026-08-28-typography-panel-design.md` — read it
first. It carries the reasoning; this plan carries the steps.

---

## Before you start

- **`make test` is the fast loop.** It runs `cmake -S . -B build`, builds and runs
  `ctest`. Everything in this plan except the firmware build is checked by it.
- **CMake uses `file(GLOB ...)`.** After ADDING or REMOVING a source or test file
  you MUST re-run `cmake -S . -B build`, or the file is silently ignored. `make
  test` does this for you; a bare `cmake --build build` does not.
- **A fresh worktree has an empty `freeink-sdk/`.** Only `make firmware` needs it
  (`git submodule update --init`). Nothing in this plan needs the firmware to
  build except Task 24.
- **Never re-bless a golden to make a test pass.** Every default in this feature
  is today's behaviour to the pixel, so **any existing golden that moves is a bug
  in this feature**, not a change to approve. The two NEW goldens are blessed by
  looking at the candidate PNG and saying what you see.
- **Scripted edits:** read fully, mutate in memory, assert the anchor was found,
  write ONCE at the end, then `grep` for a marker from the new text to verify the
  write landed, then check `git diff --stat` before committing. Count every
  `replace`. If a comment quotes the string you are replacing, the comment is an
  occurrence.

---

## File structure

**Created**

| File | Responsibility |
|---|---|
| `core/include/reader/screen_typography.h` | `TypographyScreen`: the two-mode focus machine and the value tables' declaration |
| `core/src/screen_typography.cpp` | The value tables, the label helpers, the stepper |
| `test/unit/test_screen_typography.cpp` | The five rows, the value cycle, the focus skipping `Font`, the one hint set |

**Modified**

| File | Change |
|---|---|
| `design/Typography.dc.html` | Derived preview height; `BACK / CHANGE` hints; plain footnote; no book title; focus on `Size` |
| `design/Settings.dc.html` | `Size` value `18 PT` → `15 PT` |
| `core/include/reader/settings.h` | Four fields, their ranges, `validate()` doc |
| `core/src/settings.cpp` | `validate()` clamps; load/save the four |
| `core/include/reader/layout.h` | `PageMetrics::justify` |
| `core/src/layout.cpp` | Gate justification on it |
| `core/include/reader/viewmodel.h` | `TypographyViewModel` (no book title, no editing state) |
| `core/include/reader/theme.h` | `renderTypography`, and `readerMetrics` taking the `Settings` |
| (no components.h change) | the preview's line count is a file-local helper in `theme_quiet.cpp` |
| `core/src/theme_quiet.cpp` | `renderTypography`, a file-local `typographyPreviewBoxH`, and `readerMetrics` reading the settings |
| `core/include/reader/theme_quiet.h` | The two overrides |
| `core/include/reader/app.h` | `ScreenId::Typography` |
| `core/src/app.cpp` | `screenName` |
| `core/src/session_record.cpp` | `"typography"` |
| `core/include/reader/screens.h` + `core/src/screens.cpp` | The factory case |
| `core/src/screen_reader_menu.cpp` | The `Typography` row goes live |
| `core/src/screen_settings.cpp` | Five rows become a `READING` door; the Confirm hint follows the focus |
| `sim/main.cpp` | the `typography` subcommand |
| `CMakeLists.txt` | Two `add_test` smoke entries |
| `test/unit/test_screens_golden.cpp` | Two goldens × two geometries |
| `test/unit/test_focus_restore.cpp` | `kAllScreens` row; both counts 7 → 8 |
| `test/unit/test_settings.cpp` | The four fields |
| `test/unit/test_layout.cpp` | `justify=false`; lead and margin move boundaries |
| `test/unit/test_screen_settings.cpp` | The five rows show real values |
| `shell/src/main.cpp` | Sink re-inits faces; the DONE apply path; ring shrink |
| `CLAUDE.md`, roadmap | What was decided and why |

---

# PHASE 1 — THE BOARDS

**PHASE 1 IS COMPLETE (2026-08-28), AND THE REVIEW CHANGED THE DESIGN.** Read
this before reading Tasks 1–5: their bodies are the work as it was *planned*, and
two later commits superseded parts of it. The current board is the authority, and
Task 5 records what the review found.

| what Tasks 1–4 said | what shipped, and where |
|---|---|
| preview box `height: 292px` | **`flex: 1`** — the pin was wrong by ~42px and `flex-shrink` hid it (`522beb3`) |
| a second board for the edit state | **deleted** — the mode went (`e734124`), so Tasks 2 and 4 are marked REMOVED |
| hints `DONE / EDIT` and `DONE / OK` | **`BACK / CHANGE`**, one mode (`e734124`) |
| footnote "…RE-PAGINATES IN THE BACKGROUND." | **"APPLIES TO EVERY BOOK. YOUR PLACE IS KEPT."** (`e734124`) |
| band right slot `MIDDLEMARCH` | **empty, slot still reserved** (`3b92ab8`) |
| focus on `Font` | **on `Size`**; `Font` has one value so the focus skips it (`e734124`) |

**A UI change goes into the design HTML first, then the implementation.** Phase 1
touched no C++ at all, and that is why every one of those six changes cost a board
edit and a re-render rather than a rewrite of working code.

---

### Task 1: Typography.dc.html — preview box, chevrons, full specimen (DONE, partly superseded)

**Files:**
- Modify: `design/Typography.dc.html`

Three changes, each with its own reason:

1. **The preview box stops being content-sized.** Content-sized, the box grows
   with the type and every row below it moves on every press; filling the leftover
   instead, the rows never move.
   **`flex: 1; min-height: 0; box-sizing: border-box; overflow: hidden` — NOT a
   pinned height.** A pinned one was tried and was wrong by ~42px, because it was
   computed from a footnote assumed to be two lines that the board renders in
   three; `flex-shrink`'s default of 1 then absorbed the error silently, so the
   board looked correct while stating a number it was not drawing. Pinning a height
   the board computes is CLAUDE.md's first invariant and it has caused three
   defects here already.
   **The measured result, which the firmware has to derive:** the box is 250px on
   the X4 and 241px on the X3 (outer, borders included), so 222px and 213px of text
   area.
2. **The focused row loses its chevrons.** This board depicts BROWSE mode, where
   Up/Down move the focus and no value is being stepped. Chevrons belong to the
   edit state, which is Task 2's board.
3. **The specimen is completed to Middlemarch's real opening sentence** — `…thrown
   into relief by poor dress.` — because it has to fill a box that is now fixed:
   the truncated form is 3 lines of a 4-line box at the default size, and a board
   showing a third of its box empty invites someone to shrink the box back.

- [ ] **Step 1: Read the file first, then apply the three edits**

The anchors below are exact. Read the file before trusting them — this repo is
edited constantly and an anchor is not what you remember writing.

```bash
sed -n '30,40p' design/Typography.dc.html
```

- [ ] **Step 2: Apply the edits**

```python
#!/usr/bin/env python3
import pathlib
p = pathlib.Path("design/Typography.dc.html")
src = p.read_text()
edits = []

# 1 + 3: the preview box fills the leftover, and the full sentence.
edits.append((
  '<div style="margin: 16px 24px 0 24px; border: 2px solid #000000; padding: 12px 16px; font-family: Literata, Georgia, serif; font-size: 32px; line-height: 1.7; text-align: justify;">Miss Brooke had that kind of beauty which seems to be thrown into relief.</div>',
  # THE BOX FILLS THE LEFTOVER; IT IS NOT CONTENT-SIZED AND IT IS NOT PINNED.
  # Content-sized it grows with the type and walks all five rows down the panel on
  # every press; on e-ink that reads as the whole screen jumping. So it takes the
  # panel less every fixed run (band, LIVE PREVIEW label, five rows, footnote, hint
  # bar) -- which measures 250px on the X4 and 241px on the X3, and which the
  # firmware DERIVES from the same runs rather than reading a number off this file.
  # See typographyPreviewBoxH in core/src/theme_quiet.cpp.
  #
  # A PINNED HEIGHT WAS TRIED AND WAS WRONG BY ~42px, computed from a footnote
  # assumed to be two lines that this board renders in three -- and flex-shrink's
  # default of 1 absorbed the error, so the board looked right while stating a
  # number it was not drawing. That is CLAUDE.md's first invariant.
  #
  # AND IT CANNOT PREVIEW THE MARGINS. This box is chrome geometry -- 396px of
  # measure -- where the reading column is panelW - 2*margins, 444px by default.
  # Four of the five settings show here faithfully; Margins never will.
  '<div style="margin: 16px 24px 0 24px; border: 2px solid #000000; padding: 12px 16px; flex: 1; min-height: 0; box-sizing: border-box; overflow: hidden; font-family: Literata, Georgia, serif; font-size: 32px; line-height: 1.7; text-align: justify;">Miss Brooke had that kind of beauty which seems to be thrown into relief by poor dress.</div>'))

# 2: the focused row is BROWSE mode, so no chevrons.
edits.append((
  '<div style="font-size: var(--t-value); font-weight: 700;">&lsaquo; LITERATA &rsaquo;</div>',
  '<div style="font-size: var(--t-value); font-weight: 700;">LITERATA</div>'))

for old, new in edits:
    n = src.count(old)
    assert n == 1, f"count {n} for {old[:70]!r}"
    src = src.replace(old, new, 1)
p.write_text(src)
print("ok")
```

- [ ] **Step 3: Verify the write landed**

```bash
grep -c "poor dress" design/Typography.dc.html
```
Expected: `1`

```bash
grep -c "lsaquo" design/Typography.dc.html
```
Expected: `0`

- [ ] **Step 4: Commit**

```bash
git add design/Typography.dc.html
git commit -m "design: Typography's preview box is fixed, and its board is the browse state

Content-sized, the box grows with the type and walks all five rows down the
panel on every press. Filling the leftover instead -- flex: 1, never a pinned
number -- the rows never move, which is the scroll-rail gutter trade again: a
reflow you see every time loses to a fixed cost you see once.

The chevrons go with it. They belong to the edit state, which is its own
board, and drawing them here promised a step on a browse-mode row.

The specimen is Middlemarch's real sentence now, because the box it has to
fill is fixed and the truncated form left a quarter of it empty.

Co-authored-by: Claude <claude@anthropic.com>"
```

---

### Task 2: REMOVED — there is no edit state to board

**Done and then undone on 2026-08-28**, at the Task 5 checkpoint. This task
created `design/TypographyEditing.dc.html` for the panel's edit mode; the review
of the rendered boards removed the mode, so the board was deleted (commit
`e734124`) and this task with it.

**Why it is left here as a numbered gap rather than renumbered:** the other 23
tasks are referred to by number in this file and in the commits already made, and
renumbering to close a hole is churn that invalidates every one of those
references. The gap is the audit trail.

The reason the mode went is in the spec under *One mode, one word per button*, and
the short version is that `DONE` and `OK` are synonyms, so the two-mode hint bar
gave two words for "finished" and no clue which scope each acted on.


### Task 3: Settings.dc.html — the Size value stops being wrong

**Files:**
- Modify: `design/Settings.dc.html`

Settings' five TYPOGRAPHY rows will read the REAL settings (Task 20), so their
board values have to be the DEFAULTS rather than placeholders. Four of the five
already are. `18 PT` is not: the default is ppem 32, which is `32 * 72 / 150 =
15.36` truncated to **15 PT**.

- [ ] **Step 1: Confirm the other four already state the defaults**

```bash
grep -o '>\(LITERATA\|18 PT\|COMFORTABLE\|1\.7\|JUSTIFIED\)<' design/Settings.dc.html
```
Expected: all five, and only `18 PT` disagrees with the defaults
(`LITERATA`, margins 18 = `COMFORTABLE`, lead 1700 = `1.7`, justify = `JUSTIFIED`).

- [ ] **Step 2: Change it**

```python
#!/usr/bin/env python3
import pathlib
p = pathlib.Path("design/Settings.dc.html")
src = p.read_text()
old = ">18 PT<"
n = src.count(old)
assert n == 1, f"count {n}"
p.write_text(src.replace(old, ">15 PT<", 1))
print("ok")
```

- [ ] **Step 3: Verify**

```bash
grep -c ">15 PT<" design/Settings.dc.html; grep -c ">18 PT<" design/Settings.dc.html
```
Expected: `1` then `0`

- [ ] **Step 4: Commit**

```bash
git add design/Settings.dc.html
git commit -m "design: Settings' Size row states the default, not a placeholder

A placeholder is right only while nothing exists behind the row. Once the
setting exists, a placeholder is a screen displaying a stale number -- and
this one is stale by 3 PT: the default is ppem 32, which is 15 PT truncated,
not 18.

The other four rows already stated their defaults, which is why only one
number moves here.

Co-authored-by: Claude <claude@anthropic.com>"
```

---

### Task 4: REMOVED — with the board it was for

**Done and then undone on 2026-08-28.** This task added `typography_editing` to
`tools/compare-design.py`'s `FLOW_SCREENS`. The board it named was deleted at the
Task 5 checkpoint, so the entry went with it (commit `e734124`) — a list entry
naming a board that does not exist is exactly the defect this task was written to
prevent, pointed the other way.

`typography` remains in the list, where it already was.

**Found while doing this, and still open:** `tools/compare-design.py` lists
`GoToPage.dc.html`, which was deleted when the reader menu's `Go to page…` row was
cut. That is its own card, not this feature's.


### Task 5: CHECKPOINT — DONE (2026-08-28)

**The boards were rendered, reviewed, and changed as a result.** This is what the
checkpoint was for, and it earned its place: three defects and one design failure
came out of looking at the pixels.

- [x] Rendered `typography` and `settings` at both geometries, exported bare
  panels, and measured the mismatch by hand.
- [x] **`make compare` cannot print a percentage** — it prints `ok` /
  `not implemented` only. Every percentage in CLAUDE.md was computed by hand from
  the `--export` PNGs, and so was this one. Worth a card, because this plan's Task
  18 says "read the percentage".
- [x] **`settings` measures 2.23% (X4) / 2.05% (X3)**, and the `18 PT` → `15 PT`
  fix moved it by **one pixel** — 8553 → 8554. That value run is right-aligned and
  Chrome and the firmware rasterise those digits differently, so it was ~412 of a
  55px band mismatched either way. **The percentage could not have caught the
  defect Task 3 fixed**, which is a real limit on this project's headline check.
- [x] **The pinned preview height was wrong by ~42px** and `flex-shrink` hid it.
  Fixed to `flex: 1`; renders byte-identically, which is the proof. Measured: the
  box is 250px on the X4, 241px on the X3.
- [x] **The clamp rule had to be defined in terms of INK, not the line box** — on
  the X3 four line boxes are 217.6px in a 213px content area, and Chrome draws the
  fourth line because its ink ends 10px clear. A line-box clamp would render one
  line fewer than the board on one geometry only.
- [x] **The two-mode design was rejected** on the rendered hint bar, and the
  footnote's copy was replaced. See the spec.

**Board state as reviewed and committed:** one board,
`design/Typography.dc.html`, hints `BACK / CHANGE / UP / DOWN`, footnote
`APPLIES TO EVERY BOOK. YOUR PLACE IS KEPT.`, focus on `Size`, no chevrons
anywhere, preview box derived.


# PHASE 2 — THE SETTINGS FIELDS

### Task 6: Four fields, their ranges, and validate()

**Files:**
- Modify: `core/include/reader/settings.h`
- Modify: `core/src/settings.cpp` — `Settings::validate()`
- Test: `test/unit/test_settings.cpp`

`kSettingsVersion` **stays 1**. An added field takes its default from an older
file; `settings.h` states that rule and this is the case it was written for.

**`validate()` clamps to the nearest OFFERED value, not into a range.** A range
clamp would let a hand-edited `bodyPpem: 35` survive, and the stepper indexes a
list — so the screen could never leave 35. Snapping in the loader means the
stepper only ever indexes a list it is on.

- [ ] **Step 1: Write the failing tests**

Append to `test/unit/test_settings.cpp`:

```cpp
TEST_CASE("the typography fields default to today's behaviour") {
  // THE PROPERTY THE WHOLE FEATURE RESTS ON. Every reader golden is pinned at
  // these values, so a default that moved would re-bless nine goldens and
  // silently change what every book looks like.
  const reader::Settings s;
  CHECK(s.bodyPpem == reader::kBodyPpem);   // 32
  CHECK(s.margins == 18);
  CHECK(s.lineSpacing == reader::kBodyLeadEm);  // 1700
  CHECK(s.justify);
}

TEST_CASE("validate snaps the typography fields to an offered value") {
  // SNAPPED, not range-clamped. The stepper indexes a list, so a value that is
  // in range but not ON the list would be a value the user could never leave.
  SUBCASE("a size between two steps snaps") {
    reader::Settings s;
    s.bodyPpem = 35;  // between 32 and 38
    CHECK_FALSE(s.validate());
    CHECK(s.bodyPpem == 38);  // nearest; ties go up
  }
  SUBCASE("a size below the smallest step") {
    reader::Settings s;
    s.bodyPpem = 4;
    CHECK_FALSE(s.validate());
    CHECK(s.bodyPpem == 27);
  }
  SUBCASE("a size above the largest step") {
    reader::Settings s;
    s.bodyPpem = 200;
    CHECK_FALSE(s.validate());
    CHECK(s.bodyPpem == 46);
  }
  SUBCASE("margins and line spacing snap the same way") {
    reader::Settings s;
    s.margins = 25;       // between 18 and 30
    s.lineSpacing = 1900; // between 1850 and 2000
    CHECK_FALSE(s.validate());
    CHECK(s.margins == 30);
    CHECK(s.lineSpacing == 1850);  // 1900 is 50 from 1850 and 100 from 2000
  }
  SUBCASE("a value already on the list is left alone and reports ok") {
    reader::Settings s;
    s.bodyPpem = 42;
    s.margins = 10;
    s.lineSpacing = 1400;
    s.justify = false;
    CHECK(s.validate());
    CHECK(s.bodyPpem == 42);
    CHECK(s.margins == 10);
    CHECK(s.lineSpacing == 1400);
    CHECK_FALSE(s.justify);
  }
}

TEST_CASE("every offered typography value survives validate") {
  // The screen may only offer values validate accepts, or a step would be
  // undone by the save that follows it. Asserted over the whole table rather
  // than sampled, because one bad entry is one row the user cannot select.
  for (const int p : reader::kBodyPpemSteps) {
    reader::Settings s;
    s.bodyPpem = p;
    CHECK(s.validate());
    CHECK(s.bodyPpem == p);
  }
  for (const int m : reader::kMarginSteps) {
    reader::Settings s;
    s.margins = m;
    CHECK(s.validate());
    CHECK(s.margins == m);
  }
  for (const int l : reader::kLineSpacingSteps) {
    reader::Settings s;
    s.lineSpacing = l;
    CHECK(s.validate());
    CHECK(s.lineSpacing == l);
  }
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
make test 2>&1 | tail -25
```
Expected: a COMPILE failure — `bodyPpem` is not a member of `reader::Settings`,
`kBodyPpemSteps` not declared.

- [ ] **Step 3: Add the fields and the step tables**

In `core/include/reader/settings.h`, after `kFullRefreshEveryMax`:

```cpp
// --- THE TYPOGRAPHY STEPS ----------------------------------------------------
//
// Public for the reason kSleepAfterMsMin is: the Typography screen has to know
// what it is allowed to offer, and a second copy of these numbers would drift
// from these ones. Ascending, which validate() and the stepper both rely on.
//
// EVERY DEFAULT BELOW IS TODAY'S BEHAVIOUR TO THE PIXEL. That is what keeps
// every reader, sleep and book-details golden where it is -- a golden that
// moves because of this feature is a bug in it.
//
// SIZE: ppem, labelled in points as the chrome ramp is (pt = ppem * 72 / 150,
// rounded -- and 25->12, 32->15, 38->18, 42->20, 46->22 come out the same
// truncated, which is the point of 25 rather than 27: 27 is 12.96 pt, so it
// labelled as "12 PT" while being nearer 13, and under the chrome ramp's own
// convention 12 PT is ppem 25. Both formulas now agree on all five, so the
// choice between them cannot drift into a wrong label.
//
//   * 32 is the default because design/Reader.dc.html says `font-size: 32px`.
//   * 38 is on the list because 18 PT is the Typography board's own stated
//     value, and roadmap:1269 has held "is the reader under-sized by its own
//     spec" open since 2A-2 with the instruction to decide it ON THE PANEL.
//     This does not decide it; it makes it decidable by pressing a button.
//   * 46 is the top because the glyph cache is thrash-free to ppem ~46 (see
//     CLAUDE.md, The glyph cache). Past it the arena stops holding the
//     alphabet's union and every page re-rasterises at ~3,794 us a glyph.
inline constexpr int kBodyPpemSteps[] = {25, 32, 38, 42, 46};
// MARGINS: the reader column's side padding in px. 18 is design/Reader.dc.html's
// own, which is why it is the middle step and carries the board's own label.
inline constexpr int kMarginSteps[] = {10, 18, 30};
// LINE SPACING: em x 1000, as PageMetrics::leadEm1000 is. 1700 is the board's
// `line-height: 1.7`.
inline constexpr int kLineSpacingSteps[] = {1400, 1550, 1700, 1850, 2000};
```

In the `Settings` struct, after `logToCard`:

```cpp
  // --- Typography (design/Typography.dc.html) --------------------------------
  //
  // Four fields, no `font`: one body face is vendored, so a font field's only
  // value would be its default -- a second spelling of a constant, which this
  // project has a rule about. The row reads a constant instead.
  //
  // kSettingsVersion is NOT bumped. An added field takes its default from an
  // older file, which is the rule stated at the top of this header, and this is
  // the case it was written for: a card carrying today's file loads and behaves
  // identically.
  int bodyPpem = kBodyPpem;
  int margins = 18;
  int lineSpacing = kBodyLeadEm;
  bool justify = true;
```

`settings.h` must now include `reader/layout.h` for `kBodyPpem` and
`kBodyLeadEm`. **Check for a cycle first** — `layout.h` includes
`reader/glyphsource.h` and `reader/document.h` and must not reach `settings.h`:

```bash
grep -n '#include' core/include/reader/layout.h
```

If `layout.h` is clean of `settings.h` (it is), add `#include "reader/layout.h"`
to `settings.h`.

- [ ] **Step 4: Implement the snap in validate()**

In `core/src/settings.cpp`, in the anonymous namespace:

```cpp
// SNAPS to the nearest value in an ascending table, returning false when the
// value was not already on it. Ties go UP: a value exactly between two steps has
// no better answer, and rounding up is the direction that never leaves a reader
// with type smaller than they asked for.
//
// A RANGE CLAMP WOULD NOT DO. The Typography screen steps through the table by
// index, so a value in range but off the table is a value the stepper can never
// leave -- the same trap cycleFocused handles by landing on index 0's successor,
// solved one layer earlier because a nearest-value answer exists here.
template <size_t N>
bool snapToTable(int& field, const int (&table)[N]) {
  int best = table[0];
  int bestDist = -1;
  for (const int candidate : table) {
    const int d = candidate > field ? candidate - field : field - candidate;
    // `<=` rather than `<`, over an ASCENDING table, is what makes a tie go up.
    if (bestDist < 0 || d <= bestDist) {
      bestDist = d;
      best = candidate;
    }
  }
  if (best == field) return true;
  field = best;
  return false;
}
```

In `Settings::validate()`, before `return ok;`:

```cpp
  if (!snapToTable(bodyPpem, kBodyPpemSteps)) ok = false;
  if (!snapToTable(margins, kMarginSteps)) ok = false;
  if (!snapToTable(lineSpacing, kLineSpacingSteps)) ok = false;
  // `justify` is a bool: there is no invalid value to snap.
```

`settings.cpp` needs `#include <cstddef>` for `size_t` if it does not already
compile.

- [ ] **Step 5: Run to verify it passes**

```bash
make test 2>&1 | tail -15
```
Expected: all tests pass. **If any golden moved, stop** — a default changed.

- [ ] **Step 6: Commit**

```bash
git add core/include/reader/settings.h core/src/settings.cpp test/unit/test_settings.cpp
git commit -m "settings: four typography fields, snapped rather than range-clamped

Version stays 1: an added field takes its default from an older file, which
is the rule at the top of settings.h and this is the case it was written for.

Every default is today's behaviour to the pixel, which is the property that
keeps every reader golden where it is.

validate() SNAPS to the nearest offered value instead of clamping into a
range, because the screen steps the table by INDEX -- so a value in range but
off the table would be one the user could never leave. Same trap
cycleFocused handles by landing on index 0's successor, solved a layer
earlier because a nearest-value answer exists for a number and did not for a
cycle.

ppem 38 is on the list because 18 PT is the Typography board's own stated
value, and roadmap:1269 has asked since 2A-2 whether the reader is
under-sized by its own spec, with the instruction to decide on the panel.
This does not answer it. It makes it answerable by pressing a button.

Co-authored-by: Claude <claude@anthropic.com>"
```

---

### Task 7: The four fields round-trip through the card

**Files:**
- Modify: `core/src/settings.cpp` — `loadSettings`, `saveSettings`
- Test: `test/unit/test_settings.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `test/unit/test_settings.cpp`. Follow the file's existing fixture
style for building a `FakeFileSystem` with a settings file — read the top of the
file and reuse whatever helper is already there rather than writing a second one.

```cpp
TEST_CASE("the typography fields survive a save/load round trip") {
  FakeFileSystem fs;
  reader::Settings out;
  out.bodyPpem = 46;
  out.margins = 10;
  out.lineSpacing = 2000;
  out.justify = false;
  REQUIRE(reader::saveSettings(fs, out));

  reader::Settings back;
  CHECK(reader::loadSettings(fs, back));
  CHECK(back.bodyPpem == 46);
  CHECK(back.margins == 10);
  CHECK(back.lineSpacing == 2000);
  CHECK_FALSE(back.justify);
  // The whole struct, so a field that round-tripped by accident of its default
  // cannot pass: this is the fixed-point property saveSettings documents.
  CHECK(back == out);
}

TEST_CASE("a settings file from before typography loads with the defaults") {
  // THE BACK-COMPAT CASE, and the reason kSettingsVersion did not move. This is
  // byte-for-byte a file today's firmware writes.
  FakeFileSystem fs;
  REQUIRE(fs.writeAll(reader::kSettingsPath,
                      "{\"version\":1,\"fullOnTransition\":true,"
                      "\"fullRefreshEvery\":0,\"logToCard\":false,"
                      "\"sleepAfterMs\":300000}"));
  reader::Settings s;
  CHECK(reader::loadSettings(fs, s));  // TRUE: an absent field is not a failure
  CHECK(s.bodyPpem == reader::kBodyPpem);
  CHECK(s.margins == 18);
  CHECK(s.lineSpacing == reader::kBodyLeadEm);
  CHECK(s.justify);
}

TEST_CASE("an off-table size in a hand-edited file is corrected and reported") {
  FakeFileSystem fs;
  REQUIRE(fs.writeAll(reader::kSettingsPath,
                      "{\"version\":1,\"bodyPpem\":35,\"sleepAfterMs\":300000}"));
  reader::Settings s;
  CHECK_FALSE(reader::loadSettings(fs, s));  // CORRECTED, not DEFAULTED
  CHECK(s.bodyPpem == 38);
  // ...and the rest of the file still loaded, which is the whole reason a bad
  // value clamps instead of failing the load.
  CHECK(s.sleepAfterMs == 300000u);
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
make test 2>&1 | grep -A 5 "typography fields survive" | head -20
```
Expected: FAIL — the fields are not written or read, so `back.bodyPpem` is 32.

- [ ] **Step 3: Implement**

In `loadSettings`, beside the other `readClampedInt` calls. **The bounds here are
the TABLE's ends, not the offered set** — `validate()` does the snapping, and it
is called just below, so these only keep an absurd number out of an int:

```cpp
  // The table's ends as the range; validate() below does the snapping to an
  // actual step. Two layers rather than one because readClampedInt is shared
  // and knows nothing about tables, and because `ok` has to be cleared either
  // way -- a value that was out of range and a value that was merely off the
  // table both mean CORRECTED in the boot log.
  readClampedInt(o, "bodyPpem", parsed.bodyPpem, kBodyPpemSteps[0],
                 kBodyPpemSteps[std::size(kBodyPpemSteps) - 1], ok);
  readClampedInt(o, "margins", parsed.margins, kMarginSteps[0],
                 kMarginSteps[std::size(kMarginSteps) - 1], ok);
  readClampedInt(o, "lineSpacing", parsed.lineSpacing, kLineSpacingSteps[0],
                 kLineSpacingSteps[std::size(kLineSpacingSteps) - 1], ok);
  readBool(o, "justify", parsed.justify, ok);
```

`settings.cpp` needs `#include <iterator>` for `std::size`.

In `saveSettings`, beside the other `setInt`/`setBool` calls. **Keys sorted is
not required by json.h but the byte-stability contract is** — check how
`JsonObject::dump` orders keys and place these so two saves of equal `Settings`
still produce byte-identical files:

```cpp
  o.setInt("bodyPpem", valid.bodyPpem);
  o.setInt("margins", valid.margins);
  o.setInt("lineSpacing", valid.lineSpacing);
  o.setBool("justify", valid.justify);
```

- [ ] **Step 4: Run to verify it passes**

```bash
make test 2>&1 | tail -15
```
Expected: all pass.

- [ ] **Step 5: Prove the round-trip test bites**

A round-trip test over defaults passes trivially. Confirm it does not:

```bash
# Comment out the four setInt/setBool lines in saveSettings, then:
touch core/src/settings.cpp && make test 2>&1 | grep -c "FAILED"
```
Expected: non-zero, and the round-trip case among them. **Restore the lines,
`touch` the file again** (a same-second rebuild has fooled this repo before) and
re-run to confirm green.

- [ ] **Step 6: Commit**

```bash
git add core/src/settings.cpp test/unit/test_settings.cpp
git commit -m "settings: the typography fields round-trip, and an old file still loads

The back-compat case is asserted against a byte-for-byte copy of the file
today's firmware writes, and it returns TRUE -- an absent field is not a
failure, which is what lets version stay 1.

An off-table value from a hand-edited file reads as CORRECTED rather than
DEFAULTED, and the rest of the file still loads. That distinction is the only
way a user learns their edit was adjusted rather than discarded.

Co-authored-by: Claude <claude@anthropic.com>"
```

---

# PHASE 3 — THE LAYOUT LEVERS

### Task 8: PageMetrics::justify

**Files:**
- Modify: `core/include/reader/layout.h` — the `PageMetrics` struct
- Modify: `core/src/layout.cpp` — the one `justifiable(kind_)` site
- Test: `test/unit/test_layout.cpp`

`kMinJustifyFillPercent` is untouched. It answers *"is this line full enough to
stretch"*; this answers *"does this reader want stretch at all"*. Two questions,
two mechanisms.

- [ ] **Step 1: Write the failing test**

Append to `test/unit/test_layout.cpp`, using that file's own `Body` and
`boardMetrics` helpers:

```cpp
TEST_CASE("ragged alignment leaves every line unstretched") {
  Body body;
  PageMetrics m = boardMetrics();

  // The same long paragraph laid twice, differing only in `justify`. Comparing
  // the two runs rather than asserting absolute numbers is what makes this a
  // test of the FLAG: a wrap change would move both sides together.
  Document d = docOf({"Miss Brooke had that kind of beauty which seems to be thrown "
                      "into relief by poor dress, and her hand and wrist were so "
                      "finely formed that she could wear sleeves not less bare of "
                      "style than those in which the Blessed Virgin appeared."});

  auto lay = [&](bool justify) {
    PageMetrics mm = m;
    mm.justify = justify;
    reader::PageBuilder pb(body.face, mm);
    for (size_t i = 0; i < d.blocks.size(); ++i) pb.add(d.blocks[i], static_cast<int>(i));
    return pb.finish();
  };

  const Page justified = lay(true);
  const Page ragged = lay(false);

  REQUIRE(justified.lines.size() == ragged.lines.size());
  REQUIRE(justified.lines.size() > 2);  // or there is nothing to justify

  // EVERY ragged line is unstretched...
  for (const LaidLine& ln : ragged.lines) CHECK(ln.extraPerGapF26 == 0);
  // ...and at least one justified line WAS stretched, or the comparison is
  // vacuous and would pass over a paragraph too short to justify.
  int stretched = 0;
  for (const LaidLine& ln : justified.lines)
    if (ln.extraPerGapF26 != 0) ++stretched;
  CHECK(stretched > 0);

  // THE WRAP IS IDENTICAL. Justification is applied AFTER the greedy wrap, so
  // turning it off must not move a single break -- which is also why a saved
  // position's `line` legitimately survives an alignment change while it does
  // not survive a size or margin change (reading_position.h grades exactly that).
  for (size_t i = 0; i < ragged.lines.size(); ++i)
    CHECK(ragged.lines[i].text == justified.lines[i].text);
}

TEST_CASE("justify defaults to true, which is design/Reader.dc.html's own") {
  const PageMetrics m;
  CHECK(m.justify);
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
make test 2>&1 | grep -B 2 -A 6 "justify" | head -25
```
Expected: COMPILE failure — `justify` is not a member of `PageMetrics`.

- [ ] **Step 3: Implement**

In `core/include/reader/layout.h`, in `PageMetrics` after `tracking`:

```cpp
  // WHETHER BODY LINES ARE STRETCHED TO THE MARGIN. design/Reader.dc.html says
  // `text-align: justify`, so true is the board's own and the default.
  //
  // NOT kMinJustifyFillPercent's job. That constant answers "is this line full
  // enough that stretching it will not open a corridor"; this answers "does this
  // reader want stretch at all". Folding them together would mean expressing a
  // user preference as a threshold, and 0 or 100 would each be a number that
  // happens to work rather than the question being asked.
  //
  // IT MOVES NO LINE BREAK. The greedy wrap runs first and justification is
  // applied to the finished line, which is why reading_position.h grades an
  // alignment change as leaving `line` intact where a size or column change
  // does not.
  bool justify = true;
```

In `core/src/layout.cpp`, the single site:

```cpp
    if (!ln.lastOfBlock && justifiable(kind_) && m_.justify)
      ln.extraPerGapF26 = stretchFor(*font_, text, columnWFor(kind_) - f26ToPx(xIndentF26),
                                     blockTracking_);
```

- [ ] **Step 4: Run to verify it passes**

```bash
make test 2>&1 | tail -15
```
Expected: all pass, **no golden moved** (the default is unchanged).

- [ ] **Step 5: Prove the mutation lands**

```bash
# Drop `&& m_.justify` from layout.cpp, then:
touch core/src/layout.cpp && make test 2>&1 | grep -c "FAILED"
```
Expected: non-zero. Restore, `touch` again, re-run green.

- [ ] **Step 6: Commit**

```bash
git add core/include/reader/layout.h core/src/layout.cpp test/unit/test_layout.cpp
git commit -m "layout: alignment is a metric, not a threshold

kMinJustifyFillPercent answers 'is this line full enough to stretch without
opening a corridor'. This answers 'does this reader want stretch at all'.
Folding them together would express a preference as a threshold, where 0 and
100 are numbers that happen to work rather than the question being asked.

The test compares two layouts of one paragraph rather than asserting absolute
stretch values, so it is a test of the flag and not of the wrap -- and it
asserts the BREAKS are identical, which is the fact reading_position.h relies
on when it grades an alignment change as leaving \`line\` usable.

Co-authored-by: Claude <claude@anthropic.com>"
```

---

### Task 9: readerMetrics reads the settings

**Files:**
- Modify: `core/include/reader/theme.h` — `readerMetrics` doc
- Modify: `core/src/theme_quiet.cpp` — `QuietTheme::readerMetrics`
- Modify: `core/include/reader/theme_quiet.h` — the signature
- Test: `test/unit/test_theme_reader_golden.cpp` or wherever `readerMetrics` is
  currently exercised; grep for it.

`readerMetrics` currently hardcodes `kReadPadX`, `kBodyLeadEm` and
`kBodyIndentEm`. It has to take the three settings that move.

**It takes the `Settings`, not three ints.** Three loose ints at a call site are
three chances to pass them in the wrong order, and the struct is already the one
thing both the shell and the screen hold.

- [ ] **Step 1: Find every caller first**

```bash
grep -rn "readerMetrics" --include=*.cpp --include=*.h . | grep -v '^./build'
```

Expected callers: `shell/src/main.cpp`, `sim/main.cpp`, several tests,
`theme_quiet.cpp`. **Every one has to be updated in this task** — a signature
change with a missed caller is a compile error, which is the good case, but a
DEFAULT ARGUMENT would hide it, so do not add one.

- [ ] **Step 2: Write the failing test**

Add to the file that already tests `readerMetrics` (grep above). If none does,
create `test/unit/test_theme_reader_metrics.cpp` — and remember the GLOB, so
`make test` rather than `cmake --build`.

```cpp
TEST_CASE("readerMetrics follows the typography settings") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  Body body;

  reader::Settings s;  // the defaults
  reader::PageMetrics base;
  theme.readerMetrics(480, 800, ramp.fonts, body.face, s, base);

  // THE DEFAULTS ARE THE BOARD'S. 480 less 18px each side is 444, which is the
  // number design/Reader.dc.html states and every reader golden is laid at.
  CHECK(base.columnLeft == 18);
  CHECK(base.columnW == 444);
  CHECK(base.leadEm1000 == reader::kBodyLeadEm);
  CHECK(base.justify);

  SUBCASE("wider margins narrow the column from both sides") {
    s.margins = 30;
    reader::PageMetrics m;
    theme.readerMetrics(480, 800, ramp.fonts, body.face, s, m);
    CHECK(m.columnLeft == 30);
    CHECK(m.columnW == 480 - 2 * 30);
  }
  SUBCASE("tighter margins widen it") {
    s.margins = 10;
    reader::PageMetrics m;
    theme.readerMetrics(480, 800, ramp.fonts, body.face, s, m);
    CHECK(m.columnLeft == 10);
    CHECK(m.columnW == 480 - 2 * 10);
  }
  SUBCASE("line spacing and alignment pass straight through") {
    s.lineSpacing = 2000;
    s.justify = false;
    reader::PageMetrics m;
    theme.readerMetrics(480, 800, ramp.fonts, body.face, s, m);
    CHECK(m.leadEm1000 == 2000);
    CHECK_FALSE(m.justify);
  }
  SUBCASE("the column's HEIGHT does not depend on the margins") {
    // The band and the footer are full-bleed on the board -- their padding is
    // kReadPadX but their HEIGHT is type -- so a margin change must not move the
    // column's top or shorten it. Getting this wrong loses a line of every page
    // at one margin setting and nothing at another, which is the hardest kind of
    // layout bug to attribute.
    s.margins = 30;
    reader::PageMetrics m;
    theme.readerMetrics(480, 800, ramp.fonts, body.face, s, m);
    CHECK(m.columnTop == base.columnTop);
    CHECK(m.columnH == base.columnH);
  }
}
```

- [ ] **Step 3: Run to verify it fails**

```bash
make test 2>&1 | tail -20
```
Expected: COMPILE failure — `readerMetrics` takes 5 arguments, not 6.

- [ ] **Step 4: Implement**

`core/include/reader/theme.h`, replacing the `readerMetrics` declaration:

```cpp
  // Reader's COLUMN, the same split as settingsMetrics: the theme owns the box
  // model, the screen owns what goes in it.
  //
  // Takes the body face as well as the ramp: the column's height is a whole
  // number of the BODY face's line boxes, and the body face is a ScalableFont
  // rasterised at a runtime size, not one of FontSet's twelve fixed roles.
  //
  // AND TAKES THE SETTINGS, because three of them are box-model numbers now --
  // margins, line spacing and alignment (design/Typography.dc.html). The struct
  // rather than three ints: three loose ints at a call site are three chances to
  // pass them in the wrong order, and both the shell and the Typography screen
  // already hold this struct.
  //
  // NO DEFAULT ARGUMENT, deliberately. A defaulted Settings would let a caller
  // that should have been updated compile and silently lay the page out at the
  // defaults -- which on this device is a book that ignores the reader's own
  // settings, and looks like the settings not being saved.
  virtual void readerMetrics(int panelW, int panelH, const FontSet& fonts,
                             const GlyphSource& body, const Settings& settings,
                             PageMetrics& out) const = 0;
```

`theme.h` needs `#include "reader/settings.h"` — or a forward declaration will
not do, because the parameter is by const reference to a complete type used by
value in implementations. Add the include.

`core/src/theme_quiet.cpp`:

```cpp
void QuietTheme::readerMetrics(int panelW, int panelH, const FontSet& fonts,
                               const GlyphSource& body, const Settings& settings,
                               PageMetrics& out) const {
  // Both bands are one line of --t-meta plus their padding. The header's two runs
  // are `align-items: baseline` and the same size, so the row is one line high;
  // the footer's tallest child is its text, not the 5px bar.
  const Font& meta = fonts[Role::Meta400];
  const int headerH = meta.lineHeight() + kReadHeaderPadBottom;
  const int footerH = kReadFooterPadTop + meta.lineHeight() + kReadFooterPadBottom;

  // THE MARGIN IS THE SETTING NOW, and kReadPadX is its default -- see
  // Settings::margins, whose middle step is this constant. The band and the
  // footer keep kReadPadX for their own padding: they are full-bleed runs whose
  // HEIGHT is type, so a margin change must not move the column's top or shorten
  // it, and renderReader is what draws them.
  out.columnLeft = settings.margins;
  out.columnTop = kReadPadTop + headerH;
  out.columnW = panelW - 2 * settings.margins;
  out.columnH = panelH - kReadPadTop - headerH - footerH;
  out.leadEm1000 = settings.lineSpacing;
  out.indentEm1000 = kBodyIndentEm;
  out.justify = settings.justify;
  // The body face's own tracking is the face's: the board sets no letter-spacing
  // on the reading column, and a book's text is the one run on this device that
  // must not be tracked -- the chrome's wide spacing is a chrome mannerism.
  out.tracking = {};
  (void)body;
}
```

`core/include/reader/theme_quiet.h`: mirror the new signature.

- [ ] **Step 5: Update every other caller**

For each caller from Step 1, pass the settings it already has:
- `shell/src/main.cpp:3375` — pass `gSettings`.
- `sim/main.cpp` — pass a local `reader::Settings{}` (the simulator renders the
  board's state; the defaults ARE the board's state).
- Tests — pass `reader::Settings{}` unless the case is about a non-default.

- [ ] **Step 6: Run to verify it passes**

```bash
make test 2>&1 | tail -20
```
Expected: all pass, **and no golden moved**. The defaults produce byte-identical
metrics, which is the whole point of `kMarginSteps`' middle step being 18.

- [ ] **Step 7: Commit**

```bash
git add core/include/reader/theme.h core/include/reader/theme_quiet.h \
        core/src/theme_quiet.cpp sim/main.cpp shell/src/main.cpp test/unit
git commit -m "theme: readerMetrics takes the settings, because three of them are box model

Margins, line spacing and alignment are the box model now, and the box model
is the theme's (spec 3.3). It takes the Settings struct rather than three
ints, because three loose ints at a call site are three chances to pass them
in the wrong order.

NO DEFAULT ARGUMENT. A defaulted Settings would let a caller that should have
been updated compile and quietly lay the page out at the defaults -- a book
ignoring the reader's own settings, which on glass looks like the settings not
being saved.

The column's HEIGHT is asserted not to move with the margins: the band and
the footer are full-bleed runs whose height is type, so getting that wrong
would cost a line of every page at one margin setting and nothing at another.

No golden moved, which is what 18 being the middle margin step is for.

Co-authored-by: Claude <claude@anthropic.com>"
```

---

# PHASE 4 — THE SCREEN

### Task 10: TypographyViewModel

**Files:**
- Modify: `core/include/reader/viewmodel.h`

- [ ] **Step 1: Add the view model**

At the end of `viewmodel.h`, before the closing `}  // namespace reader`:

```cpp
// design/Typography.dc.html.
//
// ONE STATE, ONE BOARD, ONE MODEL. An earlier design had a browse mode and an
// edit mode with a second board; the hint bar could not tell them apart (`DONE`
// and `OK` are synonyms) so the mode went, and with it a per-row editing flag and
// a chevron-availability flag that used to live here.
//
// It reuses ListRow for the rows, because a label and a right-aligned value is
// exactly ListRow's shape and this would be its fourth copy.
//
// THERE IS NO BOOK TITLE. The band's right slot is empty: these settings are
// device-wide, and naming one book would contradict the footnote directly below
// it. The slot is still RESERVED on the board -- a band's height must not vary by
// screen -- but nothing here supplies its content.
struct TypographyViewModel {
  std::string title;  // "TYPOGRAPHY"
  // The preview's copy. A FIXED specimen, not the book's text: see
  // screen_typography.h, which owns the string and the reason.
  std::string specimen;
  std::vector<ListRow> rows;
  int focusedRow = 0;
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};
```

- [ ] **Step 2: Build**

```bash
make test 2>&1 | tail -5
```
Expected: all pass — nothing reads it yet.

- [ ] **Step 3: Commit**

```bash
git add core/include/reader/viewmodel.h
git commit -m "viewmodel: TypographyViewModel, one state and no book title

An earlier two-mode design needed an editingRow and an editingHasSteps flag
here. The mode was removed after the board review -- DONE and OK are synonyms,
so the hint bar could not distinguish them -- and both fields went with it.

No bookTitle either: the settings are device-wide, so naming one book in the
band would contradict the footnote below it.

Co-authored-by: Claude <claude@anthropic.com>"
```


### Task 11: TypographyScreen — five rows, one mode

**Files:**
- Create: `core/include/reader/screen_typography.h`
- Create: `core/src/screen_typography.cpp`
- Test: `test/unit/test_screen_typography.cpp`

**Remember the GLOB:** three new files, so `make test` (which re-runs `cmake -S .
-B build`), never a bare `cmake --build build`.

- [ ] **Step 1: Write the failing test**

Create `test/unit/test_screen_typography.cpp`:

```cpp
// The Typography panel: five rows over four settings, one mode, and a value
// cycle that wraps.
#include <string>

#include "doctest.h"
#include "reader/screen_typography.h"
#include "reader/settings.h"

namespace {

using reader::Button;
using reader::PressKind;

// A sink that records what it was handed, so a test can assert the screen commits
// -- and can assert it commits the value it is SHOWING, which is the one
// disagreement that would be invisible on glass.
struct RecordingSink : reader::SettingsSink {
  reader::Settings last{};
  int commits = 0;
  bool answer = true;
  bool commit(const reader::Settings& s) override {
    last = s;
    ++commits;
    return answer;
  }
};

const reader::InputEvent kDown{Button::Down, PressKind::Short};
const reader::InputEvent kUp{Button::Up, PressKind::Short};
const reader::InputEvent kConfirm{Button::Confirm, PressKind::Short};
const reader::InputEvent kBack{Button::Back, PressKind::Short};

}  // namespace

TEST_CASE("the panel draws the board's five rows, in the board's order") {
  reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
  const reader::TypographyViewModel& vm = scr.vm();
  REQUIRE(vm.rows.size() == 5);
  CHECK(vm.rows[0].label == "Font");
  CHECK(vm.rows[1].label == "Size");
  CHECK(vm.rows[2].label == "Margins");
  CHECK(vm.rows[3].label == "Line spacing");
  CHECK(vm.rows[4].label == "Alignment");
  for (const reader::ListRow& r : vm.rows) {
    CHECK_FALSE(r.isHeader);
    // NOTHING DISCLOSES: every row edits in place.
    CHECK_FALSE(r.discloses);
    // The tracking column has no producer anywhere in this firmware and must not
    // acquire one here by accident.
    CHECK(r.trackingEm1000 == 0);
  }
  CHECK_FALSE(vm.specimen.empty());
  CHECK(vm.title == "TYPOGRAPHY");
}

TEST_CASE("the values shown are the settings', in the board's forms") {
  reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
  const reader::TypographyViewModel& vm = scr.vm();
  CHECK(vm.rows[0].value == "LITERATA");
  // 32 * 72 / 150 = 15.36, truncated -- and 15 PT is what Settings.dc.html now
  // states for the same default.
  CHECK(vm.rows[1].value == "15 PT");
  CHECK(vm.rows[2].value == "COMFORTABLE");
  CHECK(vm.rows[3].value == "1.7");
  CHECK(vm.rows[4].value == "JUSTIFIED");
}

TEST_CASE("the focus starts on Size and skips Font") {
  // FONT HAS ONE VALUE, so CHANGE on it would produce an identical frame -- the
  // silent no-op this project has been bitten by twice. The focus skips it, which
  // is Settings' rule for a row with nothing behind it, and it is drawn exactly
  // as any unfocused row.
  reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
  CHECK(scr.focus() == 1);
  CHECK_FALSE(scr.vm().rows[0].focusable);
  for (size_t i = 1; i < 5; ++i) CHECK(scr.vm().rows[i].focusable);
}

TEST_CASE("the focus wraps and never lands on Font") {
  reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
  // Down from 1 to 4, then the wrap must skip row 0 and land on 1.
  CHECK(scr.focus() == 1);
  for (int i = 0; i < 3; ++i) scr.onEvent(kDown);
  CHECK(scr.focus() == 4);
  scr.onEvent(kDown);
  CHECK(scr.focus() == 1);   // WRAPPED, over Font
  scr.onEvent(kUp);
  CHECK(scr.focus() == 4);   // and the other way, also over Font
  // Exhaustively: forty presses either way never reach row 0.
  for (int i = 0; i < 40; ++i) { scr.onEvent(kDown); CHECK(scr.focus() != 0); }
  for (int i = 0; i < 40; ++i) { scr.onEvent(kUp); CHECK(scr.focus() != 0); }
}

TEST_CASE("Back leaves the panel for the Reader") {
  // POPS TO THE READER, not one screen back: the panel is pushed from the reader
  // menu, so a plain pop would leave the menu standing over a page the change has
  // not reached.
  reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
  const reader::Action a = scr.onEvent(kBack);
  CHECK(a.kind == reader::Action::Kind::PopTo);
  CHECK(a.target == reader::ScreenId::Reader);
}

TEST_CASE("the hint bar is the board's, and never changes") {
  reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
  const std::array<std::string, 4> want{"BACK", "CHANGE", "UP", "DOWN"};
  CHECK(scr.vm().hints == want);
  scr.onEvent(kConfirm);
  CHECK(scr.vm().hints == want);
  scr.onEvent(kDown);
  CHECK(scr.vm().hints == want);
  for (const bool h : scr.vm().holds) CHECK_FALSE(h);
}

TEST_CASE("the screen reports the right id and fidelity") {
  reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
  CHECK(scr.id() == reader::ScreenId::Typography);
  // Chrome, not the reader: one waveform, not three.
  CHECK(scr.fidelity() == reader::Fidelity::Mono);
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
make test 2>&1 | tail -10
```
Expected: COMPILE failure — `reader/screen_typography.h` does not exist.

- [ ] **Step 3: Write the header**

Create `core/include/reader/screen_typography.h`:

```cpp
#pragma once
#include <string>

#include "reader/focus_screen.h"
#include "reader/screen_settings.h"  // SettingsSink -- the same sink, see below
#include "reader/settings.h"
#include "reader/viewmodel.h"

namespace reader {

// design/Typography.dc.html.
//
// A FULL SCREEN, NOT AN OVERLAY, despite being reached from one: the board has its
// own header band, its own hint bar and an opaque background, so it clears the
// framebuffer and nothing of the page under it is visible. That is also what makes
// the live preview affordable -- see the apply path in shell/src/main.cpp: the
// reader's page and metrics are stale for as long as this screen stands, and
// nothing draws them.
//
// --- IT SHARES SettingsSink, AND THAT IS DELIBERATE ---------------------------
//
// The typography fields live in `Settings` with everything else, so the sink that
// applies and persists a Settings change is already the right shape. A second
// interface would be a second thing to wire, a second null case, and a second
// place for "applied but not saved" to be got wrong.
//
// Its contract is unchanged and it is the contract that matters: it APPLIES and
// PERSISTS, in that order, and a refused write still leaves the new value on
// screen -- because the change HAS taken effect, and reverting the display would
// make a read-only card look like a screen that ignores its buttons.
//
// --- ONE MODE, AND CHANGE CYCLES IN PLACE -------------------------------------
//
// Up/Down move the focus, wrapping. Confirm (`CHANGE`) cycles the focused row's
// value forward, wrapping. Back leaves for the Reader.
//
// AN EARLIER DESIGN HAD TWO MODES and it was rejected on the rendered board: it
// read `DONE / EDIT / UP / DOWN` browsing and `DONE / OK / UP / DOWN` editing, and
// `DONE` and `OK` are synonyms -- two words for "finished" and nothing to say that
// one finished the ROW and the other left the SCREEN. Cycling in place is what
// Settings already does, and its argument transfers unchanged: "this is one
// button, so there is no way back except round. Five sleep steps and three
// cadences keeps a full cycle short enough to be usable on a panel that costs
// ~520 ms a repaint." Two screens editing a value list two different ways would
// have been two mechanisms for one job.
//
// WHAT IT COSTS is one direction: five values is at most four presses from any
// value to any other, the same worst case Settings accepted.
//
// --- THE VALUES AND THE FOCUS BOTH WRAP ---------------------------------------
//
// Every list in this firmware wraps, and the recorded hazard is not the wrap: it
// is AUTO-REPEAT, where "a wrap belongs to a press and a hold rests at the end",
// which is why Focus::move(delta, held) clamps for a held button.
//
// THIS SCREEN DECLARES NO REPEAT, as Settings does not, and it must not -- every
// size step re-inits the body face, so a held button would race through the sizes
// re-rasterising the alphabet on each one. One step per press, and the sharp edge
// on wrapping never arises.
class TypographyScreen : public FocusScreen {
 public:
  // WHAT THE PREVIEW SAYS. A FIXED specimen, not the book's own text.
  //
  // The book's text would mean reaching down the stack for the Reader's laid page
  // on a screen that is otherwise independent of it, and it would make the box's
  // height depend on content that changes. A fixed string is predictable at every
  // size, which is what lets the box be sized by the panel instead.
  //
  // Middlemarch's opening sentence, which is the board's own copy, completed: it
  // has to fill a box the panel sizes, and the truncated form left a quarter of it
  // empty at the default size.
  //
  // AND IT CANNOT PREVIEW THE MARGINS. The box is CHROME geometry -- the board's
  // 24px page margins less its own border and padding -- where the reading column
  // is `panelW - 2 * margins`. Different numbers, always. Four of the five
  // settings show here faithfully and `Margins` never will; making the box track
  // it would move the border on every step and still be wrong by 48px, which is
  // worse than not trying.
  static constexpr const char* kSpecimen =
      "Miss Brooke had that kind of beauty which seems to be thrown into relief "
      "by poor dress.";

  // `sink` may be null -- the simulator and the golden tests have nowhere to
  // persist to, exactly as SettingsScreen's may be.
  //
  // AND `body` MAY BE NULL, which is a supported state and not an oversight: it
  // means the preview box is drawn empty, which is what a test that only checks
  // the view model wants. The same call PageMetrics::italic makes -- a
  // degradation, not a failure.
  //
  // NO BOOK TITLE. The band's right slot is empty because these settings are
  // device-wide; naming one book would contradict the footnote below it. The slot
  // is still reserved ON THE BOARD, because a band's height must not vary by
  // screen, but that is the board's business and not this screen's.
  TypographyScreen(const Settings& initial, SettingsSink* sink, const GlyphSource* body);

  ScreenId id() const override { return ScreenId::Typography; }
  Fidelity fidelity() const override { return Fidelity::Mono; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const TypographyViewModel& vm() const { return vm_; }
  const Settings& settings() const { return settings_; }

  // Which setting a row edits. The board's order, which is the only thing that
  // makes the table in the .cpp checkable against design/Typography.dc.html by
  // eye.
  enum class Field { Font, Size, Margins, LineSpacing, Alignment };

 private:
  // Cycles the focused row's value forward, wrapping. Commits.
  Action cycleFocused();
  // A ROW IS FOCUSABLE IFF IT HAS MORE THAN ONE VALUE, which is derived rather
  // than tabulated -- so `Font` becomes reachable the moment a second body face is
  // vendored, with no line to remember to change here. Consumed by FocusScreen
  // through Focus::Gate, the same path Settings' section headers take.
  bool focusable(int index) const override;
  void syncVm() override;
  int firstFocusable() const;
  // How many values the field on this row offers. 1 for Font while one face is
  // vendored, which is the single fact driving focusability above.
  static int valueCount(Field f);

  Settings settings_;
  SettingsSink* sink_;
  const GlyphSource* body_;
  TypographyViewModel vm_;
};

}  // namespace reader
```

- [ ] **Step 4: Write the implementation**

Create `core/src/screen_typography.cpp`:

```cpp
#include "reader/screen_typography.h"

#include <array>
#include <iterator>
#include <string>

#include "reader/theme.h"

namespace reader {
namespace {

// THE BOARD'S ROWS, IN THE BOARD'S ORDER. Five rows, no section headers -- the
// board draws one flat block.
struct Item {
  const char* label;
  TypographyScreen::Field field;
};
constexpr std::array<Item, 5> kItems{{
    {"Font", TypographyScreen::Field::Font},
    {"Size", TypographyScreen::Field::Size},
    {"Margins", TypographyScreen::Field::Margins},
    {"Line spacing", TypographyScreen::Field::LineSpacing},
    {"Alignment", TypographyScreen::Field::Alignment},
}};

// Where `value` sits in an ascending table, or 0 when it is not on it.
//
// Settings::validate() snaps every field onto its table before the screen ever
// sees it, so the fallback is unreachable in practice. It is 0 rather than an
// assert because a screen that cannot be constructed is a device that cannot show
// its settings, and index 0 is a value the cycle can leave.
template <size_t N>
int indexIn(const int (&table)[N], int value) {
  for (size_t i = 0; i < N; ++i)
    if (table[i] == value) return static_cast<int>(i);
  return 0;
}

// The next index in a wrapping cycle. Not Focus's: this is an index into a value
// table, not a focus, and borrowing Focus here would mean a second Focus object
// per screen whose range changes with the focused row.
int nextIndex(int at, int count) { return count <= 1 ? at : (at + 1) % count; }

}  // namespace

TypographyScreen::TypographyScreen(const Settings& initial, SettingsSink* sink,
                                   const GlyphSource* body)
    : FocusScreen(static_cast<int>(kItems.size()), 0),
      settings_(initial),
      sink_(sink),
      body_(body) {
  // SNAPPED ON THE WAY IN, so the cycle indexes a table the value is on. The
  // shell's settings have already been through validate(), but the simulator and
  // the tests construct this directly -- and a screen that trusted its caller here
  // would show a value it could not cycle off.
  settings_.validate();
  // The first focusable row, not row 0: row 0 is `Font`, which has one value.
  setFocus(firstFocusable());
  syncVm();
}

int TypographyScreen::valueCount(Field f) {
  switch (f) {
    // ONE FACE IS VENDORED. This is the number that makes the Font row
    // unreachable, and it is the one line to change when a second face lands --
    // focusability is derived from it, so nothing else has to be touched.
    case Field::Font: return 1;
    case Field::Size: return static_cast<int>(std::size(kBodyPpemSteps));
    case Field::Margins: return static_cast<int>(std::size(kMarginSteps));
    case Field::LineSpacing: return static_cast<int>(std::size(kLineSpacingSteps));
    case Field::Alignment: return 2;
  }
  return 1;
}

bool TypographyScreen::focusable(int index) const {
  if (index < 0 || index >= static_cast<int>(kItems.size())) return false;
  return valueCount(kItems[static_cast<size_t>(index)].field) > 1;
}

int TypographyScreen::firstFocusable() const {
  for (size_t i = 0; i < kItems.size(); ++i)
    if (focusable(static_cast<int>(i))) return static_cast<int>(i);
  // Unreachable with the table above, and not an assert: a table edited down to
  // nothing focusable should render a readable screen rather than abort a boot.
  return 0;
}

Action TypographyScreen::cycleFocused() {
  const int f = focus();
  if (f < 0 || f >= static_cast<int>(kItems.size())) return Action::none();
  const Field field = kItems[static_cast<size_t>(f)].field;

  switch (field) {
    case Field::Font:
      // Unreachable: a one-value row is not focusable. Answers none() rather than
      // asserting, because a restored focus is the one way a number could arrive
      // here from outside -- and FocusScreen refuses an unlandable restore for
      // exactly that reason.
      return Action::none();
    case Field::Size:
      settings_.bodyPpem = kBodyPpemSteps[nextIndex(
          indexIn(kBodyPpemSteps, settings_.bodyPpem), valueCount(field))];
      break;
    case Field::Margins:
      settings_.margins = kMarginSteps[nextIndex(
          indexIn(kMarginSteps, settings_.margins), valueCount(field))];
      break;
    case Field::LineSpacing:
      settings_.lineSpacing = kLineSpacingSteps[nextIndex(
          indexIn(kLineSpacingSteps, settings_.lineSpacing), valueCount(field))];
      break;
    case Field::Alignment:
      settings_.justify = !settings_.justify;
      break;
  }

  // The value is shown whether or not the write succeeded -- see SettingsSink.
  if (sink_ != nullptr) sink_->commit(settings_);
  syncVm();
  return Action::redraw();
}

Action TypographyScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    // ONE ROW AT A TIME whatever the distance: this screen declares no repeat, so
    // a gesture never carries more than one step, and stepping past the
    // unfocusable row is what moveFocus is for.
    case Gesture::Prev: return moveFocus(-1);
    case Gesture::Next: return moveFocus(+1);
    case Gesture::Activate: return cycleFocused();
    // A PLAIN POP, and it was popTo(ScreenId::Reader) until Settings became a
    // second door. From Settings there is no Reader on the stack and popTo stops at
    // the root (app.h), so it would have dumped the user on Home and lost Settings.
    //
    // The panel cannot re-paginate the Reader itself in either case: core/ has no
    // faces to re-rasterise. The shell does it, keyed on a Reader being ANYWHERE on
    // the stack rather than on top -- which the reader-menu route needs anyway,
    // because popping here lands on the MENU and the menu is an overlay, so
    // App::render paints the stale page beneath it on the very next frame.
    case Gesture::Back: return Action::pop();
    default: return Action::none();
  }
}

void TypographyScreen::syncVm() {
  vm_.title = "TYPOGRAPHY";
  vm_.specimen = kSpecimen;
  vm_.focusedRow = focus();

  vm_.rows.clear();
  vm_.rows.reserve(kItems.size());
  for (const Item& it : kItems) {
    ListRow row;
    row.label = it.label;
    switch (it.field) {
      // ONE FACE, and the row states its name rather than reading a field. A
      // `font` setting whose only value is its default would be a second spelling
      // of this constant.
      case Field::Font: row.value = "LITERATA"; break;
      case Field::Size: row.value = typographySizeLabel(settings_.bodyPpem); break;
      case Field::Margins: row.value = typographyMarginLabel(settings_.margins); break;
      case Field::LineSpacing:
        row.value = typographyLeadLabel(settings_.lineSpacing);
        break;
      case Field::Alignment:
        row.value = typographyAlignLabel(settings_.justify);
        break;
    }
    row.isHeader = false;
    // NOTHING DISCLOSES. Every row edits in place, so no row draws a chevron.
    row.discloses = false;
    row.focusable = valueCount(it.field) > 1;
    vm_.rows.push_back(std::move(row));
  }

  // The board's own labels, and they do not change: there is one mode.
  vm_.hints = {"BACK", "CHANGE", "UP", "DOWN"};
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);
}

void TypographyScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                              Plane plane) const {
  // body_ may be null, and the THEME handles that -- it is what knows the specimen
  // cannot be drawn without a face, and resolving it here would mean inventing a
  // null GlyphSource for a case one branch covers.
  theme.renderTypography(fb, fonts, body_, vm_, plane);
}

}  // namespace reader
```

**The four `typography*Label` functions are LOCAL to this file** -- put them in the
anonymous namespace above, beside `kItems`.

An earlier draft extracted them into `settings.h`, because Settings was going to
read the same five values out. It does not any more: its five rows became one door
(Task 19), so there is exactly ONE caller. The rule is "the second copy is the
extraction point", not "extract in advance of one".

`typographySizeLabel` truncates: `pt = ppem * 72 / 150`, exactly as `sleepLabel`
truncates minutes, because the row describes a size the user is looking at and
rounding up would name a size the panel is not showing. Every offered ppem
gives the same label as rounding it (25->12, 32->15, 38->18, 42->20, 46->22),
which is why 25 and not 27 -- see the step table's own comment. `typographyLeadLabel` emits `2.0` rather than `2`, because
a bare integer reads as a count beside `1.85` rather than as a ratio. The margin
labels are a parallel array to `kMarginSteps` with a `static_assert` on the
lengths, so a step added without a label fails to compile.

**`ScreenId::Typography` is Task 13.** Do it now if the build blocks you.

- [ ] **Step 5: Run to verify it passes**

```bash
make test 2>&1 | tail -20
```
Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add core/include/reader/screen_typography.h core/src/screen_typography.cpp \
        test/unit/test_screen_typography.cpp
git commit -m "typography: the panel's five rows, one mode, CHANGE cycles in place

Shares SettingsSink rather than growing a second interface: the fields live in
Settings with everything else, so the sink that applies-and-persists a
Settings change is already the right shape.

FOCUSABILITY IS DERIVED, not tabulated: a row is focusable iff its field has
more than one value. So Font is unreachable while one body face is vendored
and becomes reachable the moment a second one lands, with no line here to
remember to change. That is what replaced a chevron affordance the two-mode
design used to carry.

The values are snapped in the constructor, not trusted from the caller: the
cycle indexes a table by position, so a screen handed an off-table value would
show something it could not cycle off.

Co-authored-by: Claude <claude@anthropic.com>"
```


### Task 12: The cycle, the commit, and the refusals

**Files:**
- Test: `test/unit/test_screen_typography.cpp`

The behaviour landed in Task 11. This task is the tests that pin it, and they are
the ones worth writing carefully — a value cycle is four switch arms and three of
them passing proves nothing about the fourth.

- [ ] **Step 1: Write the tests**

```cpp
TEST_CASE("CHANGE cycles the focused value and commits what it shows") {
  RecordingSink sink;
  reader::TypographyScreen scr(reader::Settings{}, &sink, nullptr);
  REQUIRE(scr.focus() == 1);  // Size

  scr.onEvent(kConfirm);
  CHECK(scr.settings().bodyPpem == 38);      // 32 -> 38
  CHECK(scr.vm().rows[1].value == "18 PT");
  CHECK(scr.focus() == 1);                   // the focus did NOT move
  // AND THE COMMIT CARRIES WHAT THE ROW SHOWS. A screen that cycled its own copy
  // and committed a stale one would be invisible until the next boot.
  CHECK(sink.commits == 1);
  CHECK(sink.last.bodyPpem == 38);
}

TEST_CASE("every cycle returns to where it started, on every multi-value row") {
  // Checked on all four, because the cycle is four switch arms. And the whole
  // table is walked rather than one step taken: a cycle that skipped a value or
  // stuck at the end would pass a single-step test.
  struct Case { int row; int count; };
  const Case cases[] = {{1, 5}, {2, 3}, {3, 5}, {4, 2}};
  for (const Case& c : cases) {
    CAPTURE(c.row);
    reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
    while (scr.focus() != c.row) scr.onEvent(kDown);
    const std::string first = scr.vm().rows[static_cast<size_t>(c.row)].value;

    // Every intermediate value is DISTINCT from the first, so a cycle that
    // returned early -- or never moved -- fails here rather than at the end.
    for (int i = 1; i < c.count; ++i) {
      scr.onEvent(kConfirm);
      CHECK(scr.vm().rows[static_cast<size_t>(c.row)].value != first);
    }
    scr.onEvent(kConfirm);
    CHECK(scr.vm().rows[static_cast<size_t>(c.row)].value == first);  // WRAPPED
  }
}

TEST_CASE("the size cycle visits every offered step, in the table's order") {
  // The one row where the ORDER is visible to the reader, and where a wrong order
  // would read as a broken control rather than as a different design.
  reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
  REQUIRE(scr.focus() == 1);
  const char* want[] = {"18 PT", "20 PT", "22 PT", "12 PT", "15 PT"};  // wraps at 46 -> 25
  for (const char* w : want) {
    scr.onEvent(kConfirm);
    CHECK(scr.vm().rows[1].value == std::string(w));
  }
}

TEST_CASE("CHANGE on the Font row cannot happen, and costs nothing if it does") {
  // The row is not focusable, so a press cannot reach it. Asserted through the
  // GESTURE anyway, because a restored focus is the one way a number could arrive
  // from outside -- and a waveform spent on an identical frame is the cost.
  RecordingSink sink;
  reader::TypographyScreen scr(reader::Settings{}, &sink, nullptr);
  CHECK_FALSE(scr.setFocus(0));               // refused: not focusable
  CHECK(scr.focus() == 1);                    // still on Size
  CHECK(sink.commits == 0);
}

TEST_CASE("a refused write still shows the new value") {
  // SettingsSink's contract: the change HAS taken effect in RAM, and reverting the
  // display would make a read-only card look like a screen that ignores its
  // buttons.
  RecordingSink sink;
  sink.answer = false;
  reader::TypographyScreen scr(reader::Settings{}, &sink, nullptr);
  scr.onEvent(kConfirm);
  CHECK(sink.commits == 1);
  CHECK(scr.settings().bodyPpem == 38);
  CHECK(scr.vm().rows[1].value == "18 PT");
}

TEST_CASE("a held mover carries one row, not its distance") {
  // The gesture layer drops a Long on a mover and only emits Repeat where the
  // screen asked for one -- and this screen asks for none. `steps` is ignored here
  // rather than trusted, because a repeat that did arrive would walk the focus
  // forty rows through a five-row list.
  reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
  reader::GestureEvent g;
  g.what = reader::Gesture::Next;
  g.steps = 40;
  g.held = true;
  scr.onGesture(g);
  CHECK(scr.focus() == 2);  // ONE row, not forty
}

TEST_CASE("every value the screen can reach survives validate") {
  // The screen may only offer values validate accepts, or a step would be undone
  // by the save that follows it. Walked through the SCREEN rather than over the
  // tables, so it covers the cycle and the tables together.
  for (int row = 1; row <= 4; ++row) {
    CAPTURE(row);
    reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
    while (scr.focus() != row) scr.onEvent(kDown);
    for (int i = 0; i < 6; ++i) {
      scr.onEvent(kConfirm);
      reader::Settings s = scr.settings();
      CHECK(s.validate());          // already valid: nothing to snap
      CHECK(s == scr.settings());
    }
  }
}
```

- [ ] **Step 2: Run**

```bash
make test 2>&1 | tail -20
```
Expected: all pass. If the held-mover case fails, `onGesture` is reading
`g.steps` — it must not.

- [ ] **Step 3: Prove the cycle test bites**

```bash
# In nextIndex, replace the wrap with a clamp:
#   return count <= 1 ? at : (at + 1 >= count ? count - 1 : at + 1);
touch core/src/screen_typography.cpp && make test 2>&1 | grep -c "FAILED"
```
Expected: non-zero, including the cycle and the size-order cases. Restore,
`touch` again, re-run green.

- [ ] **Step 4: Commit**

```bash
git add test/unit/test_screen_typography.cpp
git commit -m "typography: pin the cycle, the commit and the two refusals

The cycle is checked on all four multi-value rows and walked all the way
round, with every intermediate value asserted distinct from the first -- a
single-step test passes over a cycle that sticks at the end, and a
walk-to-the-end test passes over one that never moves.

Size's ORDER is pinned too, because it is the one row where the order is
visible to the reader and a wrong one reads as a broken control.

A held mover is asserted to carry ONE row and not its distance. This screen
declares no repeat so one should never arrive, but onGesture ignores g.steps
rather than trusting that.

Co-authored-by: Claude <claude@anthropic.com>"
```


### Task 13: The three catalogues, and the factory

**Files:**
- Modify: `core/include/reader/app.h` — `ScreenId`
- Modify: `core/src/app.cpp` — `screenName`
- Modify: `core/src/session_record.cpp` — the name array
- Modify: `core/include/reader/screens.h` + `core/src/screens.cpp` — the factory

**APPENDED to `ScreenId`, not inserted.** The session record stores a NAME so an
insertion could not silently become another screen, but appending also leaves
every existing ordinal where it was.

- [ ] **Step 1: Add the enum member**

`core/include/reader/app.h`, after `Contents` and before `SdMissing`... **no —
after `SdMissing`.** Appending means last. Read the enum's own comment about
`ReaderMenu` being appended for exactly this reason.

```cpp
  Contents,
  SdMissing,
  // design/Typography.dc.html -- the reader's type panel. APPENDED for the reason
  // ReaderMenu was: the record stores a name, so an insertion could not silently
  // become another screen, but appending also leaves every existing ordinal where
  // it was.
  Typography
};
```

- [ ] **Step 2: Update both name catalogues**

`core/src/app.cpp`'s `screenName`, and `core/src/session_record.cpp`'s array.
Read both first — the record's array is indexed by ordinal, so `"typography"`
goes LAST there, matching the enum:

```bash
sed -n '18,30p' core/src/session_record.cpp
grep -n "screenName" -A 20 core/src/app.cpp
```

Add `"typography"` as the last entry of the record's array and the matching case
or entry in `screenName`. **They are separate facts that happen to agree**: one is
a storage format, the other a log label free to be reworded.

- [ ] **Step 3: Add the factory case**

`core/src/screens.cpp`, beside `case ScreenId::Settings`:

```cpp
    case ScreenId::Typography: {
      // The SAME settings copy and the SAME sink the Settings screen gets: the
      // typography fields live in `Settings`, so there is nothing extra to
      // plumb.
      //
      // THE BOOK TITLE IS NOT SUBSTITUTED. An unprimed title is empty and the
      // band draws nothing there, which is honest; falling back to a demo name
      // is how this device once showed MIDDLEMARCH over a real book. The
      // simulator and the goldens set it explicitly.
      return std::make_unique<TypographyScreen>(settings_, settingsSink_, readerBody_);
    }
```

`screens.h` needs `#include "reader/screen_typography.h"`.

**`readerBody_` is the same face the Reader draws with**, which is what makes the
preview a live preview rather than a second approximation of one. It may be null —
the tests and a Settings-only build have no body face — and the theme draws an
empty preview box for that, which is a degradation and not a failure.

**There is no book title to pass.** The band's right slot is empty because the
settings are device-wide; the earlier design passed `menuTitle_` and that is gone
with it. So this case cannot substitute content, which removes the hazard the
Reader and Contents cases both have.

- [ ] **Step 4: Build and run**

```bash
make test 2>&1 | tail -20
```
Expected: `test_focus_restore.cpp` FAILS — its `static_assert` on
`kAllScreens`' length, then its two counts. That is Task 14 and it is the test
doing its job.

- [ ] **Step 5: Commit**

```bash
git add core/include/reader/app.h core/src/app.cpp core/src/session_record.cpp \
        core/include/reader/screens.h core/src/screens.cpp
git commit -m "screens: ScreenId::Typography, appended, and its factory case

Appended rather than inserted, for the reason ReaderMenu was: the session
record stores a screen by NAME so an insertion could not silently become
another screen, but appending also leaves every existing ordinal alone.

Two catalogues name it and neither has a -Wswitch to lean on -- screenName
for the logs, and session_record's array as a storage format. Separate facts
that happen to agree.

The factory hands over the same settings copy and the same sink the Settings
screen gets, and does NOT substitute a book title: an unprimed title is empty
and the band draws nothing, where a demo fallback is how this device once
showed MIDDLEMARCH over a real book.

Co-authored-by: Claude <claude@anthropic.com>"
```

---

### Task 14: test_focus_restore learns the eighth focused screen

**Files:**
- Modify: `test/unit/test_focus_restore.cpp`

This test is the mechanism that stops a screen reporting a focus and dropping it
— the bug that shipped on three screens, each behind a comment arguing its case
was the exception. It counts, so it cannot end up testing nothing.

- [ ] **Step 1: Add the row and move both counts**

Three edits:
1. `kAllScreens` gains `ScreenId::Typography` (its `static_assert` fails until it
   does).
2. `CHECK(movable == 7)` → `8`, and its comment names Typography.
3. `CHECK(wrapping == 7)` → `8`.

```python
#!/usr/bin/env python3
import pathlib
p = pathlib.Path("test/unit/test_focus_restore.cpp")
src = p.read_text()
edits = [
  ("    ScreenId::ReaderMenu,  ScreenId::Contents,  ScreenId::SdMissing,\n};",
   "    ScreenId::ReaderMenu,  ScreenId::Contents,  ScreenId::SdMissing,\n"
   "    ScreenId::Typography,\n};"),
  ("static_assert(sizeof(kAllScreens) / sizeof(kAllScreens[0]) ==\n"
   "                  static_cast<size_t>(ScreenId::SdMissing) + 1,",
   "static_assert(sizeof(kAllScreens) / sizeof(kAllScreens[0]) ==\n"
   "                  static_cast<size_t>(ScreenId::Typography) + 1,"),
  ("  // claims. SEVEN screens can move their focus today: Home, Library, the two Library\n"
   "  // overlays, Settings, the reader menu and the contents. BookDetails, Sleep, the\n"
   "  // Reader and the SD-missing prompt have one thing on them and legitimately report 0.",
   "  // claims. EIGHT screens can move their focus today: Home, Library, the two Library\n"
   "  // overlays, Settings, the reader menu, the contents and Typography. BookDetails,\n"
   "  // Sleep, the Reader and the SD-missing prompt have one thing on them and\n"
   "  // legitimately report 0."),
  ("  CHECK(movable == 7);", "  CHECK(movable == 8);"),
  ("  CHECK(wrapping == 7);", "  CHECK(wrapping == 8);"),
]
for old, new in edits:
    n = src.count(old)
    assert n == 1, f"count {n} for {old[:60]!r}"
    src = src.replace(old, new, 1)
p.write_text(src)
print("ok")
```

- [ ] **Step 2: Check whether `build()` needs a Typography branch**

`build()` constructs each screen through `DemoScreenFactory`. Typography needs
nothing beyond the factory's `settings_`, which defaults — so it should build with
no branch. **Verify rather than assume:**

```bash
make test 2>&1 | grep -A 8 "typography" | head -20
```

If `build()` returns null for it, the factory refused — most likely the
factory case refusing something. **Typography must NOT be refused for a null body
face** — an empty preview box is a degradation and the screen is still usable and
still reports a focus. Confirm the case has no such guard.

- [ ] **Step 3: Run**

```bash
make test 2>&1 | tail -20
```
Expected: all pass, `movable == 8`, `wrapping == 8`.

- [ ] **Step 4: Commit**

```bash
git add test/unit/test_focus_restore.cpp
git commit -m "test: Typography is the eighth screen whose focus round-trips

Both counts move with it. They exist because a refactor that made every
screen report a fixed focus would leave this loop passing on nothing -- the
'reports on less than it claims' shape -- and because focus()/setFocus()
shipped one-way on three screens, each behind a comment arguing its own case
was the exception.

Typography needs no branch in build(): it takes the factory's settings copy
and a book title that is allowed to be empty.

Co-authored-by: Claude <claude@anthropic.com>"
```

---

# PHASE 5 — THE RENDER

### Task 15: Theme::renderTypography

**Files:**
- Modify: `core/include/reader/theme.h` — the pure virtual
- Modify: `core/include/reader/theme_quiet.h` — the override
- Modify: `core/src/theme_quiet.cpp` — the implementation and its constants

**NO CHEVRONS.** The two-mode design drew `‹ ›` around the edited row's value;
with one mode there is nothing to mark, and the glyphs are gone. Nothing here
needs a codepoint outside the chrome subset.

**AND THE FOOTNOTE IS PURE ASCII NOW** — `APPLIES TO EVERY BOOK. YOUR PLACE IS
KEPT.` The old copy carried an em dash, and this repo has been bitten twice by an
unbounded C++ hex escape swallowing the character after it (`"\xB7C"`, `"\xA0b"`).
There is no escape left in this file's strings to get wrong.

- [ ] **Step 1: Declare it on Theme**

`core/include/reader/theme.h`, beside `renderSettings`:

```cpp
  // design/Typography.dc.html.
  //
  // Takes the body face for the same reason renderReader does: the preview is set
  // in a ScalableFont rasterised at a runtime size, not in one of FontSet's twelve
  // fixed roles, and the whole point of the box is to show that size.
  //
  // A POINTER, AND NULL IS A SUPPORTED STATE -- exactly as renderReader's italic
  // is, and for the same reason: a caller with no body face gets an empty preview
  // box rather than no screen. A reference would force every such caller to invent
  // a null face, which is a class nothing needs.
  virtual void renderTypography(Framebuffer& fb, const FontSet& fonts,
                                const GlyphSource* body, const TypographyViewModel& vm,
                                Plane plane) = 0;
```

- [ ] **Step 2: Write the constants**

`core/src/theme_quiet.cpp`, beside `kReadPadX` and friends:

```cpp
// --- design/Typography.dc.html ------------------------------------------------
//
// Every one of these is the board's own number. Nothing here is derived by this
// theme EXCEPT the preview box's height, which is the panel less all of them --
// see typographyPreviewBoxH.
constexpr int kTypoPreviewTop = 16;      // the box's `margin-top`
constexpr int kTypoPreviewBorder = 2;    // `border: 2px`
constexpr int kTypoPreviewPadY = 12;     // `padding: 12px 16px`
constexpr int kTypoPreviewPadX = 16;
constexpr int kTypoLabelPadTop = 8;      // `LIVE PREVIEW`'s `padding: 8px 24px 10px`
constexpr int kTypoLabelPadBottom = 10;
constexpr int kTypoLabelEm = 120;        // `letter-spacing: 0.12em`
constexpr int kTypoRowsBorder = 2;       // the rows block's `border-top: 2px`
// 50, NOT kSettingsRowH's 54. Two boards, two numbers -- and pinning one to the
// other is exactly the class of defect this project records three times (the
// header band 6px out, menu rows compounding a pixel each, the hint bar's
// asymmetric padding). Derive from the board you are drawing.
constexpr int kTypoRowH = 50;
constexpr int kTypoRuleH = 1;            // `border-bottom: 1px` between rows
constexpr int kTypoFootPadBottom = 10;   // the footnote's `padding: 0 24px 10px`
constexpr int kTypoFootEm = 100;         // `letter-spacing: 0.1em`
constexpr int kTypoFootLeadEm = 1500;    // `line-height: 1.5`
// THE FOOTNOTE'S COPY. In the theme rather than the view model because it is the
// BOARD'S text about how the screen behaves, not a fact about the current state --
// the same call renderSdMissing makes for its paragraph.
//
// It does two jobs. It answers the only question a reader actually has (the place
// IS kept: relayout lands at the top of the current block), and it states that the
// setting is DEVICE-WIDE -- which is why the band's right slot names no book.
constexpr const char* kTypoFootnote = "APPLIES TO EVERY BOOK. YOUR PLACE IS KEPT.";
```

- [ ] **Step 3: Write the box-model helper**

In the same anonymous namespace:

```cpp
// THE PREVIEW BOX'S HEIGHT: the panel less every fixed run above and below it.
//
// DERIVED, NOT PINNED, which is CLAUDE.md's first invariant -- and this feature
// has already paid for ignoring it once: the board pinned `height: 292px`,
// computed from a footnote assumed to be two lines that rendered in three, and
// `flex-shrink`'s default of 1 absorbed the ~42px error silently. The board
// renders 250px on the X4 and 241px on the X3.
//
// MEASURED TARGETS, so a mismatch here is visible immediately rather than at the
// comparison sheet: 250/241 outer, 222/213 of text area.
//
// It is FIXED with respect to the SETTINGS, which is the point: the box does not
// grow with the type, so the five rows below it never move.
int typographyPreviewBoxH(const Framebuffer& fb, const FontSet& fonts, int bandH,
                          int rowCount, const Hint (&hints)[4]) {
  const Font& meta = fonts[Role::Meta400];
  const int labelH = kTypoLabelPadTop + meta.lineHeight() + kTypoLabelPadBottom;
  // Rules BETWEEN rows only: the last row's bottom edge is the block's end, which
  // is renderSettings' and renderLibrary's rule verbatim.
  const int rowsH = kTypoRowsBorder + rowCount * kTypoRowH +
                    (rowCount > 0 ? (rowCount - 1) * kTypoRuleH : 0);
  // The footnote is TWO lines at the board's measure on both panels -- and it is
  // ASKED rather than hardcoded, because the firmware's whole-pixel advances
  // measure ~3% wider than Chrome's and a board's measure is a number to check in
  // both engines (SdMissing's had to go 400 -> 420 for exactly this). The previous
  // copy was three lines, and assuming two is what cost the 42px above.
  const Prose foot = wrapProseLead(meta, kTypoFootnote, fb.width() - 2 * kMargin,
                                   kTypoFootLeadEm, trackingEm(meta, kTypoFootEm));
  const int footH = foot.height + kTypoFootPadBottom;
  const int fixed = bandH + kTypoPreviewTop + labelH + rowsH + footH +
                    hintBarHeight(fonts, hints);
  const int box = fb.height() - fixed;
  return box > 0 ? box : 0;
}
```

Check `Prose`/`wrapProseLead`/`drawProse`/`clampProse`'s real signatures in
`core/include/reader/components.h` before writing this. **Use the existing
signatures; do not add an overload.**

- [ ] **Step 4: Write the render**

```cpp
void QuietTheme::renderTypography(Framebuffer& fb, const FontSet& fonts,
                                  const GlyphSource* body, const TypographyViewModel& vm,
                                  Plane plane) {
  fb.clear(true);

  Hint hints[4];
  buildHints(kHintSlotMarks, vm.hints, vm.holds, hints);

  // THE BAND'S RIGHT SLOT IS EMPTY, and that is the design: these settings are
  // device-wide, so there is no fact about "the book you are looking at" to put
  // there, and naming one book would contradict the footnote below.
  //
  // The band's HEIGHT does not change for it -- bandContentH() takes
  // max(Label500, Value700) unconditionally -- which is deliberate and is what the
  // board reserves a line box to match: a band that shrank when a screen left the
  // slot empty would move every row beneath it.
  const int afterBand = drawHeaderBand(fb, fonts, vm.title, "", nullptr, plane);

  const int rowCount = static_cast<int>(vm.rows.size());
  const int boxH = typographyPreviewBoxH(fb, fonts, afterBand, rowCount, hints);

  // --- The preview box ---------------------------------------------------------
  int y = afterBand + kTypoPreviewTop;
  outlineRect(fb, kMargin, y, fb.width() - 2 * kMargin, boxH, kTypoPreviewBorder, plane);
  const int textW = fb.width() - 2 * kMargin - 2 * kTypoPreviewBorder - 2 * kTypoPreviewPadX;
  const int textH = boxH - 2 * kTypoPreviewBorder - 2 * kTypoPreviewPadY;
  // NO FACE, NO SPECIMEN -- the box is still drawn, because the box is the board's
  // and an absent preview is not an absent screen.
  if (body != nullptr && !vm.specimen.empty() && textW > 0 && textH > 0) {
    // Justified, as the board's box is -- and NOT following the Alignment setting,
    // because the box is chrome geometry and cannot preview the reading measure
    // anyway. Its job is to show the FACE, the SIZE and the LEAD.
    Prose p = wrapProseLead(*body, vm.specimen, textW, /*leadEm1000=*/0, {});
    // AS MANY LINES AS FIT -- AND "FIT" MEANS THE INK FITS, NOT THE LINE BOX.
    //
    // MEASURED ON THE BOARD, and it decides a whole line. Chrome draws a line whose
    // line-box TOP is inside the box and clips whatever hangs below, so at the
    // default setting the X3 gets ceil(213 / 54.4) = 4 lines where floor() gives 3.
    // The firmware cannot clip, so it asks the narrower question -- does the INK
    // fit -- which agrees with Chrome exactly when agreeing is safe: on the X3 the
    // fourth line's box overruns the content area by 4.6px while its ink ends 10px
    // clear. A floor() on the line box would render one line FEWER than the board
    // on the X3 and the same as the board on the X4: a whole line of mismatch on
    // one geometry only, which is the hardest kind to attribute.
    p.lines.resize(static_cast<size_t>(previewLinesThatFit(*body, p, textH)));
    drawProse(fb, *body, kMargin + kTypoPreviewBorder + kTypoPreviewPadX,
              y + kTypoPreviewBorder + kTypoPreviewPadY, p, Ink::Black, {}, plane);
  }
  y += boxH;

  // --- `LIVE PREVIEW` ----------------------------------------------------------
  const Font& meta = fonts[Role::Meta400];
  const Tracking labelTrack = trackingEm(meta, kTypoLabelEm);
  y += kTypoLabelPadTop;
  drawText(fb, meta, kMargin, baselineIn(meta, y, meta.lineHeight()), "LIVE PREVIEW",
           Ink::Black, labelTrack, plane);
  y += meta.lineHeight() + kTypoLabelPadBottom;

  // --- The rows ----------------------------------------------------------------
  fb.fillRect(0, y, fb.width(), kTypoRowsBorder, false);
  y += kTypoRowsBorder;

  const Font& label = fonts[Role::Value500];
  const Font& labelFocused = fonts[Role::Value700];
  const Font& value = fonts[Role::Value700];
  for (int i = 0; i < rowCount; ++i) {
    const ListRow& row = vm.rows[static_cast<size_t>(i)];
    const bool focused = (i == vm.focusedRow);
    // AN UNFOCUSABLE ROW IS DRAWN EXACTLY AS AN UNFOCUSED FOCUSABLE ONE.
    // `row.focusable` is deliberately not read here -- the flag is about input, and
    // a theme that dimmed on it would be inventing a design decision. Settings'
    // render makes the same point in the same words.
    if (focused) fb.fillRect(0, y, fb.width(), kTypoRowH, false);
    const Ink ink = focused ? Ink::White : Ink::Black;
    const Font& lf = focused ? labelFocused : label;

    const int rightEdge = fb.width() - kMargin;
    const int valueW = row.value.empty() ? 0 : value.measure(row.value);
    // The label truncates and the value keeps its width -- the Library band's
    // rule: the value is the state and the label is what it names.
    const int labelMaxW = rightEdge - kMargin - (valueW > 0 ? valueW + kSettingsLabelGap : 0);
    drawTextElided(fb, lf, kMargin, baselineIn(lf, y, kTypoRowH), row.label, labelMaxW, ink,
                   {}, plane);
    if (valueW > 0)
      drawText(fb, value, rightEdge - valueW, baselineIn(value, y, kTypoRowH), row.value,
               ink, {}, plane);

    y += kTypoRowH;
    // A rule BETWEEN rows only, and none under the focused row whose fill runs to
    // the next row's top edge. rowRuleFor is the shared spelling of both.
    if (rowRuleFor(i, rowCount, focused)) {
      fb.fillRect(0, y, fb.width(), kTypoRuleH, false);
      y += kTypoRuleH;
    }
  }

  // --- The footnote, bottom-anchored above the hint bar -------------------------
  const Tracking footTrack = trackingEm(meta, kTypoFootEm);
  Prose foot = wrapProseLead(meta, kTypoFootnote, fb.width() - 2 * kMargin,
                             kTypoFootLeadEm, footTrack);
  const int footTop = fb.height() - hintBarHeight(fonts, hints) - kTypoFootPadBottom -
                      foot.height;
  drawProse(fb, meta, kMargin, footTop, foot, Ink::Black, footTrack, plane);

  drawHintBar(fb, fonts, hints, plane);
}
```

- [ ] **Step 5: `previewLinesThatFit` — a file-local helper, NOT a new primitive**

**DO NOT use `clampProse` and DO NOT add a sibling to it.** Read its real contract
first:

```bash
grep -n "void clampProse" -B 22 core/include/reader/components.h
```

`clampProse(font, prose, maxLines, maxW, tail)` takes a LINE COUNT, not a pixel
height, and it **elides the overflow into the last line with an ellipsis** —
`-webkit-line-clamp`'s behaviour, which Book details and the overlay panels want
for a filename. It is the wrong tool twice over here: the preview's box is not a
line budget, and an ellipsis on a type specimen would imply the reader is being
denied something, where in fact the specimen is only a sample.

`Prose::lines` is a public `std::vector<std::string_view>` and `Prose::leadF26` is
the line box in 1/64 px, so the whole job is a `resize` and needs no primitive at
all. Put this in `theme_quiet.cpp`'s anonymous namespace beside
`typographyPreviewBoxH`:

```cpp
// HOW MANY OF A WRAP'S LINES HAVE THEIR INK INSIDE A BOX `boxH` PX TALL.
//
// Not `clampProse`, which takes a line budget and ellipsises the remainder -- both
// wrong here: the box is a window onto a fixed specimen rather than a budget, and
// an ellipsis on a type specimen reads as content withheld.
//
// AND NOT `floor(boxH / lead)`, which is the obvious answer and drops a line the
// board draws. Chrome keeps a line whose line-box TOP is inside the box and clips
// what hangs below, so the X3's 213px content area takes ceil(213 / 54.4) = 4
// lines at the default setting. The firmware has no clip, so it asks whether the
// INK fits -- which lands on the same 4, because that fourth box overruns by 4.6px
// while its ink ends 10px clear. Measured on the rendered board, both geometries.
//
// Never keeps a line whose ink would overflow, so it cannot produce the sliced
// line design/Reader.dc.html's own column once had.
int previewLinesThatFit(const GlyphSource& face, const Prose& p, int boxH) {
  if (p.leadF26 <= 0) return 0;
  const int boxF26 = pxToF26(boxH);
  int fit = 0;
  for (int i = 0; i < p.lineCount(); ++i) {
    // The ink's bottom edge within line i: the baseline the draw will use, plus
    // the face's descent. baselineInF26 is the same helper drawProse places by,
    // so this cannot disagree with where the glyphs actually land.
    const int top = i * p.leadF26;
    const int inkBottom = baselineInF26(face, top, p.leadF26) + pxToF26(face.descent());
    if (inkBottom > boxF26) break;
    ++fit;
  }
  return fit;
}
```

**Check `baselineInF26`'s and `descent()`'s real signatures before writing this** —
`descent()` may already be in 1/64 px, or may be signed the other way, and getting
its sign wrong silently keeps one line too many. Grep both and say which you found.

Give it a test in the new `test_theme_reader_metrics.cpp` (or a sibling): at the
default setting it must return **4 at both geometries**, and at `lineSpacing = 2000`
with `bodyPpem = 46` it must return fewer and never a count whose ink overflows.

- [ ] **Step 6: Build**

```bash
make test 2>&1 | tail -20
```
Expected: compiles; **no existing golden moves** (nothing else calls this, and
`clampProse` is untouched).

- [ ] **Step 7: Commit**

```bash
git add core/include/reader/theme.h core/include/reader/theme_quiet.h \
        core/src/theme_quiet.cpp core/include/reader/components.h \
        core/src/components.cpp test/unit/test_components.cpp
git commit -m "theme: renderTypography, and a clamp that measures ink

kTypoRowH is 50 and NOT kSettingsRowH's 54. Two boards, two numbers: pinning
one screen's box to another's is the class of defect this project has three
records of, each costing a compounding pixel.

The preview box's height is DERIVED, with the board's measured 250/241 as the
target -- a pinned 292 was tried first and was wrong by 42px, which
flex-shrink hid.

The preview's line count is a file-local helper, not a new primitive and NOT
clampProse -- which takes a line budget and ellipsises the remainder, both
wrong for a window onto a fixed specimen. floor(boxH / lead) is the obvious
answer and drops a line the board draws: Chrome keeps a line whose box top is
inside and clips the overhang, so the X3 takes 4 where floor gives 3. Asking
whether the INK fits lands on the same 4 and can never slice a line.

The band's right slot is empty and its height is unchanged, which is what the
board reserves a line box to match.

Co-authored-by: Claude <claude@anthropic.com>"
```


### Task 16: The simulator reaches it by pressing

**Files:**
- Modify: `sim/main.cpp`
- Modify: `CMakeLists.txt` — one `add_test` smoke entry

**Reached by pressing, not by assignment**, which is the rule every other state
board here follows: the render then pins the NAVIGATION too, and getting it wrong
shows up immediately as the wrong screen in the contact sheet.

The route is `reader_menu`'s plus two presses: `DOWN` (Contents → Typography) then
`CONFIRM`. **One subcommand, not two** — there is one state.

**This makes the simulator a check on Task 20 as well:** the menu's Typography row
has to be live, or the `CONFIRM` does nothing and the sheet shows a reader menu.

- [ ] **Step 1: Add the flag**

Beside `isReaderMenu`, and add it to the `if (!isHome && ...)` rejection list **and**
to its `fprintf` message — both lists, because the message is what a typo reads as.

```cpp
  // design/Typography.dc.html. The SAME journey as `reader_menu` plus the presses
  // that open the panel, so this renders the navigation as well as the screen --
  // and it is what would catch the menu's Typography row going inert again.
  const bool isTypography = std::strcmp(argv[1], "typography") == 0;
```

- [ ] **Step 2: Extend the body-face condition**

The preview needs the body face:

```cpp
  if (isReader || isReaderMenu || isChapterOpen || isReaderList || isAnchored ||
      isTypography) {
```

- [ ] **Step 3: Extend the reader-menu branch**

Change `if (isReaderMenu)` to `if (isReaderMenu || isTypography)` and, after the
menu is pushed, add:

```cpp
    if (isTypography) {
      // THE BOARD'S OWN VALUES, not a fresh device's -- design/Typography.dc.html
      // states `18 PT`, which is ppem 38. Same call primeForJourney makes for
      // Settings, and for the same reason: the board draws a configured state.
      //
      // BEFORE the push, because the factory hands the screen its starting values
      // at construction.
      reader::Settings shown;
      shown.bodyPpem = 38;
      factory.setSettings(shown);
      // THE PREVIEW SHOWS THE SIZE, so the face has to be at it -- a preview drawn
      // at 32 under a row reading 18 PT is a screen disagreeing with itself. The
      // shell does this in its sink; here it is one call.
      //
      // Re-initing after readerMetrics leaves the Reader's metrics stale, which is
      // harmless for exactly the reason the shell's apply path relies on:
      // Typography is not an overlay, so the page is never drawn while it stands.
      if (!body.init(bodyTtf.data(), bodyTtf.size(), 38)) {
        std::fprintf(stderr, "body face failed to re-init at ppem 38\n");
        return 1;
      }
      // Contents is row 0 and Typography row 1.
      app.dispatch({reader::Button::Down, reader::PressKind::Short});
      app.dispatch({reader::Button::Confirm, reader::PressKind::Short});
      if (app.top().id() != reader::ScreenId::Typography) {
        std::fprintf(stderr, "the reader menu did not open Typography\n");
        return 1;
      }
    }
```

**The factory also needs the body face for the screen it builds** — check whether
`DemoScreenFactory` passes `readerBody_` into the Typography case (Task 13), and
that `setReaderBody(&body)` is called before the push.

- [ ] **Step 4: Register the smoke test**

`CMakeLists.txt`, beside the others:

```cmake
# The Typography panel, reached the way the device reaches it: Reader -> menu ->
# Typography. A smoke test on the navigation as much as on the render -- the
# menu's row has to be live for this to arrive anywhere. Rendered at the X3, which
# is the geometry with the tighter preview budget (213px against the X4's 222).
add_test(NAME sim_typography COMMAND reader_sim typography
         ${CMAKE_BINARY_DIR}/sim_typography.png --canvas 528x792)
```

- [ ] **Step 5: Run and look**

```bash
make test 2>&1 | tail -10
./build/reader_sim typography build/typo_x4.png --canvas 480x800
./build/reader_sim typography build/typo_x3.png --canvas 528x792
```

Open both. Say what you see, specifically: is the preview set at the LARGER size
(38, not 32)? Four whole lines with no sliced last line at BOTH geometries? Is
`Size` the inverted row and `Font` plain? Footnote two lines, clear of the hint
bar? Band's right slot empty?

- [ ] **Step 6: Commit**

```bash
git add sim/main.cpp CMakeLists.txt
git commit -m "sim: typography, reached by pressing through the reader menu

Pins the NAVIGATION as well as the render, which makes it the check that
would catch the menu's Typography row going inert again.

The body face is re-inited to ppem 38 beside the settings, because the board
states 18 PT and a preview drawn at 32 under a row reading 18 PT is a screen
disagreeing with itself. Re-initing after readerMetrics leaves the Reader's
metrics stale -- harmless for exactly the reason the shell's apply path relies
on: Typography is not an overlay, so the page is never drawn while it stands.

Registered at the X3, which is the geometry with the tighter preview budget.

Co-authored-by: Claude <claude@anthropic.com>"
```


### Task 17: Goldens at both geometries

**Files:**
- Modify: `test/unit/test_screens_golden.cpp`
- Create: `test/golden/typography.png`, `test/golden/typography_x3.png`

**Two goldens, not four:** there is one state.

- [ ] **Step 1: Write the golden test**

Append to `test/unit/test_screens_golden.cpp`, mirroring the reader-menu case's
structure (it already builds the Reader-rooted App with a body and italic face):

```cpp
// --- The Typography panel ------------------------------------------------------
//
// design/Typography.dc.html.
//
// REACHED BY PRESSING, through the reader menu, exactly as the simulator reaches
// it -- so this is a check on the menu's row being live as well as on the render.
TEST_CASE("QuietTheme renders the Typography panel to golden, reached by pressing") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  // ppem 38 IS the board's `18 PT`, and the face has to be at it or the preview
  // disagrees with the row above it.
  Body body(38);
  Italic italic(38);

  auto renderOne = [&](int w, int h, const std::string& name) {
    reader::Settings shown;
    shown.bodyPpem = 38;
    reader::PageMetrics m;
    theme.readerMetrics(w, h, ramp.fonts, body.face, shown, m);
    m.italic = &italic.face;

    reader::DemoScreenFactory factory;
    factory.setReaderBody(&body.face);
    factory.setReaderItalic(&italic.face);
    factory.setReaderMetrics(m);
    factory.setReaderDemo();
    factory.setContentsDemo();
    factory.setSettings(shown);

    std::unique_ptr<reader::Screen> page = factory.create(reader::ScreenId::Reader);
    REQUIRE(page != nullptr);
    static_cast<reader::ReaderScreen*>(page.get())->completeIndex();
    reader::App app(std::move(page), factory);
    REQUIRE(app.pushScreen(reader::ScreenId::ReaderMenu));
    // Contents is row 0, Typography row 1.
    app.dispatch({reader::Button::Down, reader::PressKind::Short});
    app.dispatch({reader::Button::Confirm, reader::PressKind::Short});
    REQUIRE(app.top().id() == reader::ScreenId::Typography);
    reader::Framebuffer fb(w, h);
    app.render(fb, ramp.fonts, theme, reader::Plane::Bw);
    golden::checkGolden(fb, name);
  };

  SUBCASE("X4 480x800") { renderOne(480, 800, "typography"); }
  SUBCASE("X3 528x792") { renderOne(528, 792, "typography_x3"); }
}

TEST_CASE("the Typography panel declares the fidelity its goldens are drawn at") {
  // ASSERTED BEFORE THE PLANE IS NAMED, the way Home's goldens assert Mono: a
  // change to the shipped path must FAIL a test rather than leave two goldens
  // quietly pinning a path nothing paints.
  reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
  CHECK(scr.fidelity() == reader::Fidelity::Mono);
}
```

- [ ] **Step 2: Run — the goldens are missing, so they fail and write candidates**

```bash
make test 2>&1 | grep -A 3 "golden missing" | head -12
```
Expected: two `golden missing - inspect build/<name>_candidate.png` failures.

- [ ] **Step 3: LOOK AT BOTH CANDIDATES AND SAY WHAT YOU SEE**

This is the review, not a formality. An icon has passed review twice in this repo
while reading as the letters "OC". For each, write down:

- **Four whole lines of specimen, no sliced last line.** This is the case
  `previewLinesThatFit` exists for, and the X3 is where it decides — a
  `floor(boxH / lead)` would show three here.
- The specimen is visibly LARGER than the reader's default (38 against 32).
- `Size` is the inverted row; `Font` is plain and undimmed.
- The band's right slot is EMPTY, and the band's rule sits where every other
  screen's does.
- Footnote is two lines, clear of the last row and the hint bar.
- Hint bar reads `BACK / CHANGE / UP / DOWN`.

Compare each against its board (`build/reserved/` from the checkpoint, or
re-export with `make compare COMPARE_ARGS="--only typography --export build/x"`).

- [ ] **Step 4: Bless them**

```bash
cp build/typography_candidate.png test/golden/typography.png
cp build/typography_x3_candidate.png test/golden/typography_x3.png
make test 2>&1 | tail -8
```
Expected: all pass.

- [ ] **Step 5: Prove they bite, by mutation**

Each must fail for a reason it is supposed to catch. Run these ONE AT A TIME,
`touch`ing the file after each edit AND after restoring it — a `cp` plus a compile
inside the same second leaves make thinking the object is current, which has fooled
this repo already:

| mutation | expected failures |
|---|---|
| `previewLinesThatFit` → `floor(boxH / p.leadF26 / 64)` | **exactly 1**, `typography_x3`, and NOT the X4 — that asymmetry is the whole reason the helper measures ink |
| `kTypoRowH` 50 → 54 | both |
| the footnote's `footTop` computed from `y` instead of the panel bottom | both |
| `drawHeaderBand`'s value changed from `""` to `vm.title` | both |
| the focus forced to row 0 (`Font`) | both |

Record the counts. **A mutation that fails 0 tests means the mutation did not
land or the goldens are blind** — check `git diff` against the binary's behaviour
before believing either.

- [ ] **Step 6: Commit**

```bash
git add test/unit/test_screens_golden.cpp test/golden/typography*.png
git commit -m "golden: the Typography panel at both geometries

Reached by pressing through the reader menu, so these pin the navigation and
the render together -- and they are what would catch the menu's row going
inert.

The fidelity is asserted before the plane is named, the way Home's goldens
assert Mono.

Proved by mutation, one at a time, with the counts recorded. The one that
matters: replacing previewLinesThatFit with floor(boxH / lead) fails the X3
golden and NOT the X4, because four line boxes are 217.6px in the X3's 213px
content area while the ink ends 10px clear. That asymmetry is the whole reason
the helper measures ink, and it is now a failing test rather than a paragraph.

Co-authored-by: Claude <claude@anthropic.com>"
```


### Task 18: make compare, and read the percentage

**Files:** none

- [ ] **Step 1: Compare the two affected boards**

```bash
make compare COMPARE_ARGS="--only typography,settings"
```

- [ ] **Step 2: Compute the PERCENTAGE, because the tool does not print one**

**`make compare` prints `ok` / `not implemented` and NOTHING ELSE.** Every
percentage in CLAUDE.md was computed by hand from the `--export` PNGs, and this is
how — there is no flag for it:

```bash
make compare COMPARE_ARGS="--only typography,settings --export build/cmp"
```

```python
#!/usr/bin/env python3
from PIL import Image
import pathlib
d = pathlib.Path("build/cmp")
for name in ["typography_x4", "typography_x3", "settings_x4", "settings_x3"]:
    a = Image.open(d / f"{name}_design.png").convert("L")
    b = Image.open(d / f"{name}_firmware.png").convert("L")
    assert a.size == b.size, f"{name}: {a.size} vs {b.size}"
    pa, pb = a.load(), b.load()
    w, h = a.size
    bad = sum(1 for y in range(h) for x in range(w)
              if (pa[x, y] < 128) != (pb[x, y] < 128))
    print(f"{name}: {bad}/{w*h} = {100.0*bad/(w*h):.2f}%")
```

**`ok` means the simulator produced a frame, not that the frame matches.** A merge
once changed `ReaderMenu.dc.html` under its screen and the sheet said `ok` the
whole time, at 13.02% against 3.02%.

- [ ] **Step 3: Judge the numbers against like screens**

Expected: this project averages ~5.4%/6.4% on 1-bit chrome, and overlay-free full
screens do better (`home_empty` 1.34%/1.23%, `sleep_idle` 0.27%). Typography is a
full screen with a band, five rows, a hint bar and one prose block — closest in
kind to `settings` (2.23%/2.05%) and `book_details`.

**`settings` must not have got worse.** Its `Size` row now reads `15 PT` on both
sides where before the board said 15 and the firmware said 18, so it should improve
slightly or stay flat.

**If typography is above ~8%, stop and find the structural mismatch** before
proceeding. The likely candidates, in order:

1. **The clamp** — one line more or fewer of specimen than the board. Check the
   line count before anything else; it is the largest single block of pixels on
   the screen.
2. **The preview box's derived height** disagreeing with the board's measured
   250px (X4) / 241px (X3).
3. **The footnote wrapping to three firmware lines where Chrome takes two** — the
   firmware's whole-pixel advances measure ~3% wider, and SdMissing's `max-width`
   needed 400 → 420 for exactly this.
4. `kTypoRowH`, or the band's height.

- [ ] **Step 4: If the board is what is wrong, fix the board**

A UI change goes into the design HTML first, **including when the board is what is
wrong**. Then re-render, re-bless the two goldens, and say in the commit that they
were re-blessed and why.

- [ ] **Step 5: Commit whatever moved (possibly nothing)**

```bash
git add -A && git commit -m "design: reconcile Typography against the firmware's own metrics

<state all four percentages, and which side moved and why>

Co-authored-by: Claude <claude@anthropic.com>"
```

**And make a card:** `make compare` cannot report the number CLAUDE.md calls the
check. That is the "reports on less than it claims" shape aimed at the repo's own
headline tool, and it is not this feature's to fix.


# PHASE 6 — SETTINGS STOPS SHOWING PLACEHOLDERS

### Task 19: Settings' READING section, and a door

**Files:**
- Modify: `core/include/reader/screen_settings.h` — the `Field` enum, the `Item` struct
- Modify: `core/src/screen_settings.cpp` — the table, `focusable`, `onGesture`, `syncVm`
- Modify: `core/src/theme_quiet.cpp` — `renderSettings` draws a chevron
- Test: `test/unit/test_screen_settings.cpp`
- Re-bless: `test/golden/settings.png`, `settings_x3.png`

**THIS REPLACED "the five rows read the real settings".** That was right while
Settings was not an entry point: a placeholder is right only until the setting
exists. With a door, five rows that display the values are redundant — the panel
shows them, one press away.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("Settings' READING row opens the Typography panel") {
  reader::SettingsScreen scr(reader::Settings{}, nullptr);
  scr.setMetrics(2000, 54, 40);  // a window tall enough for every row
  const reader::SettingsViewModel& vm = scr.vm();

  REQUIRE(vm.rows.size() == 7);
  CHECK(vm.rows[0].label == "READING");
  CHECK(vm.rows[0].isHeader);
  CHECK(vm.rows[1].label == "Typography");
  // A CHEVRON AND NO VALUE: Home's menu rows state the rule -- a row states a
  // quantity or discloses a screen, never both.
  CHECK(vm.rows[1].discloses);
  CHECK(vm.rows[1].value.empty());
  CHECK(vm.rows[2].label == "DEVICE");

  // THE FOCUS STARTS HERE. It sat on `Sleep after` only because every row above it
  // was inert.
  CHECK(vm.rows[static_cast<size_t>(vm.focusedRow)].label == "Typography");

  const reader::Action a = scr.onEvent({reader::Button::Confirm, reader::PressKind::Short});
  CHECK(a.kind == reader::Action::Kind::Push);
  CHECK(a.target == reader::ScreenId::Typography);
}

TEST_CASE("the five old typography rows are gone") {
  // They were a readout nobody could act on. Asserted by absence, because a row
  // left behind would be drawn and unreachable forever and nothing else would
  // notice.
  reader::SettingsScreen scr(reader::Settings{}, nullptr);
  scr.setMetrics(2000, 54, 40);
  for (const reader::SettingsRow& r : scr.vm().rows) {
    CHECK(r.label != "Font");
    CHECK(r.label != "Size");
    CHECK(r.label != "Margins");
    CHECK(r.label != "Line spacing");
    CHECK(r.label != "Alignment");
    CHECK(r.label != "TYPOGRAPHY");
  }
}

TEST_CASE("the Confirm hint follows the focused row") {
  // THE FIRST HINT BAR HERE WHOSE TEXT VARIES WITHIN A SCREEN, and it has to:
  // screen_settings.cpp used to state the premise outright -- "CHANGE, not OPEN:
  // nothing here pushes a screen" -- and the READING row makes it false.
  reader::SettingsScreen scr(reader::Settings{}, nullptr);
  scr.setMetrics(2000, 54, 40);
  REQUIRE(scr.vm().rows[static_cast<size_t>(scr.vm().focusedRow)].label == "Typography");
  CHECK(scr.vm().hints[1] == "OPEN");

  // Down to the first DEVICE row, which cycles a value in place.
  scr.onEvent({reader::Button::Down, reader::PressKind::Short});
  REQUIRE(scr.vm().rows[static_cast<size_t>(scr.vm().focusedRow)].label == "Sleep after");
  CHECK(scr.vm().hints[1] == "CHANGE");
  // The other three slots never move.
  CHECK(scr.vm().hints[0] == "BACK");
  CHECK(scr.vm().hints[2] == "UP");
  CHECK(scr.vm().hints[3] == "DOWN");
}

TEST_CASE("CHANGE on a device row still cycles, and OPEN does not") {
  // The two behaviours must not have leaked into each other: a disclosing row that
  // cycled a value, or a value row that pushed a screen, would each be a control
  // doing something other than what its hint says.
  reader::SettingsScreen scr(reader::Settings{}, nullptr);
  scr.setMetrics(2000, 54, 40);
  const reader::Settings before = scr.settings();
  scr.onEvent({reader::Button::Confirm, reader::PressKind::Short});  // on Typography
  CHECK(scr.settings() == before);                                   // nothing changed

  scr.onEvent({reader::Button::Down, reader::PressKind::Short});     // Sleep after
  const reader::Action a = scr.onEvent({reader::Button::Confirm, reader::PressKind::Short});
  CHECK(a.kind == reader::Action::Kind::Redraw);                     // not Push
  CHECK(scr.settings().sleepAfterMs != before.sleepAfterMs);
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
make test 2>&1 | tail -20
```
Expected: FAIL — 11 rows, not 7; `rows[0].label` is `"TYPOGRAPHY"`.

- [ ] **Step 3: Change the enum and the Item struct**

`core/include/reader/screen_settings.h`. **`field != None` used to mean both "has a
setting" and "can be focused"; those are two facts now**, because a disclosing row
has no setting and is focusable:

```cpp
  // Which setting a row edits, or Typography, which edits none and opens the screen
  // that does.
  //
  // `Typography` IS NOT A SETTING and is still focusable, which is why
  // `field != None` can no longer serve as the focusability test -- see
  // `reachable` below.
  enum class Field { None, Typography, SleepAfter, FullRefresh, OnTransition };

  struct Item {
    const char* label;
    Field field;
    bool isHeader;
    // Whether a focus may land here. Not derivable from `field`: a header has no
    // field and cannot be focused, `Sleep screen` has no field and cannot be
    // focused, and `Typography` has no field and MUST be.
    bool reachable;
    // What a row with no setting behind it shows. `Sleep screen` is the only one
    // left: covers are issue #11.
    const char* placeholder;
  };
```

- [ ] **Step 4: Change the table**

`core/src/screen_settings.cpp`. Seven items where there were eleven, and the
board's order:

```cpp
// THE BOARD'S ROWS, IN THE BOARD'S ORDER, and the order is the only thing that
// makes this table checkable against design/Settings.dc.html by eye. Seven items:
// two section headers and five rows, which fits the panel -- so Settings still
// draws no rail.
//
// IT WAS ELEVEN. A TYPOGRAPHY section carried Font, Size, Margins, Line spacing
// and Alignment, drawn and unreachable because the settings behind them did not
// exist. They do now, and they are edited on their own screen -- so five rows that
// merely displayed them became one row that opens it. READING rather than
// TYPOGRAPHY so the section is a sibling of DEVICE and does not repeat the row's
// own word.
constexpr std::array<SettingsScreen::Item, 7> kItems{{
    {"READING", SettingsScreen::Field::None, true, false, ""},
    {"Typography", SettingsScreen::Field::Typography, false, true, ""},
    {"DEVICE", SettingsScreen::Field::None, true, false, ""},
    {"Sleep after", SettingsScreen::Field::SleepAfter, false, true, ""},
    {"Full refresh", SettingsScreen::Field::FullRefresh, false, true, ""},
    {"Refresh on screen change", SettingsScreen::Field::OnTransition, false, true, ""},
    {"Sleep screen", SettingsScreen::Field::None, false, false, "BOOK COVER"},
}};
```

`focusable()` becomes `return !it.isHeader && it.reachable;`.

- [ ] **Step 5: The gesture, and the hint**

`cycleFocused` gains a `Field::Typography` case that must NOT cycle anything:

```cpp
    case Field::Typography:
      // Handled by onGesture before we get here -- this row discloses rather than
      // edits. Listed so the switch stays exhaustive: -Wswitch is what names a
      // field nobody handled, and a `default:` would throw that away.
      return Action::none();
```

`onGesture`'s `Activate` branch answers the push before reaching `cycleFocused`:

```cpp
    case Gesture::Activate: {
      const int f = focus();
      if (f >= 0 && f < static_cast<int>(kItems.size()) &&
          kItems[static_cast<size_t>(f)].field == Field::Typography)
        return Action::push(ScreenId::Typography);
      return cycleFocused();
    }
```

`syncVm` sets the row's `discloses` and the hint:

```cpp
    if (!it.isHeader) {
      row.discloses = it.field == Field::Typography;
      switch (it.field) {
        // A DISCLOSING ROW HAS NO VALUE. Home's menu rows state the rule: a row
        // states a quantity or discloses a screen, never both -- and summarising
        // four typography settings in the right slot would break it and would not
        // fit.
        case Field::Typography: break;
        case Field::SleepAfter: row.value = sleepLabel(settings_.sleepAfterMs); break;
        ...
      }
    }
```

...and the hint bar, replacing the constant `"CHANGE"`:

```cpp
  // THE CONFIRM LABEL FOLLOWS THE FOCUSED ROW, and this is the first hint bar in
  // this firmware whose text varies within a screen.
  //
  // This screen's own comment used to state the premise: "CHANGE, not OPEN: nothing
  // here pushes a screen, every focusable row edits a value in place." The READING
  // row makes that false, and a Confirm labelled CHANGE that opens a screen is the
  // misleading-button defect this project keeps recording. One slot changes as the
  // focus moves; that is the price of a bar that is true of the button it names.
  const int f = focus();
  const bool opens = f >= 0 && f < static_cast<int>(kItems.size()) &&
                     kItems[static_cast<size_t>(f)].field == Field::Typography;
  vm_.hints = {"BACK", opens ? "OPEN" : "CHANGE", "UP", "DOWN"};
```

**Note the Back slot is `BACK`, which is what the board says** — check it rather
than trusting this line, because the old table may have said something else.

- [ ] **Step 6: renderSettings draws a chevron**

It ignores `discloses` today. Add it, reusing `kChevron` — the same mark
`drawPanelRow` uses, right-aligned where a value would be:

```cpp
    // A DISCLOSING ROW DRAWS A CHEVRON WHERE A VALUE WOULD GO, and never both:
    // vm.rows guarantees the value is empty for such a row, but this branch is
    // exclusive anyway so a future table cannot draw a chevron over a value.
    if (row.discloses) {
      drawIcon(fb, kChevron, rightEdge - kChevron.w,
               y + (kSettingsRowH - kChevron.h) / 2, ink, plane);
    } else if (valueW > 0) {
      drawText(fb, value, rightEdge - valueW, baselineIn(value, y, kSettingsRowH),
               row.value, ink, {}, plane);
    }
```

Check `drawIcon`'s real signature and whether it takes an `Ink` — the chevron is
white on the focused row and black elsewhere, and getting that wrong makes it
invisible on the row it is most likely to be on.

- [ ] **Step 7: Run, and re-bless the two Settings goldens**

```bash
make test 2>&1 | tail -20
```
Expected: the `settings` and `settings_x3` goldens FAIL. They must: the screen lost
four rows and a section, gained a chevron, and moved its focus.

**Inspect both candidates and say what you see** before blessing:
- Seven items, `READING` / `Typography ›` / `DEVICE` / four rows.
- The chevron is VISIBLE on the focused (inverted) row — i.e. drawn white.
- The hint bar reads `BACK / OPEN / UP / DOWN`.
- The `DEVICE` header keeps its 2px rule (it is no longer the first item, so the
  positional rule still gives it one).

```bash
cp build/settings_candidate.png test/golden/settings.png
cp build/settings_x3_candidate.png test/golden/settings_x3.png
make test 2>&1 | tail -8
```

- [ ] **Step 8: Prove the new tests bite**

| mutation | expected |
|---|---|
| `onGesture`'s Typography branch removed | the push test fails |
| `opens` forced false | the hint test fails, and both goldens |
| `row.discloses` forced false | both goldens (no chevron) |
| `reachable` set false on the Typography row | the focus test fails |

- [ ] **Step 9: Commit**

```bash
git add core/include/reader/screen_settings.h core/src/screen_settings.cpp \
        core/src/theme_quiet.cpp test/unit/test_screen_settings.cpp \
        test/golden/settings*.png
git commit -m "settings: a READING section with a door, where five inert rows were

The five typography rows were a readout nobody could act on. There is a screen
that edits those settings now and it needs no open book, so Settings can reach
it -- and once there is a door, rows that merely display the values are
redundant.

\`field != None\` used to mean both 'has a setting' and 'can be focused'. Those
are two facts now, because the Typography row has no setting and must be
focusable, so \`reachable\` is its own field.

THE CONFIRM HINT FOLLOWS THE FOCUSED ROW. This screen's own comment stated the
premise -- 'CHANGE, not OPEN: nothing here pushes a screen, every focusable row
edits a value in place' -- and the READING row makes it false. It is the first
hint bar here whose text varies within a screen; the alternative is a Confirm
labelled CHANGE that opens a screen.

renderSettings draws a chevron for a disclosing row, which it had no reason to
until now. The two goldens are re-blessed: four rows and a section gone, a
chevron added, the focus moved.

Co-authored-by: Claude <claude@anthropic.com>"
```


# PHASE 7 — WIRING

### Task 20: The reader menu's Typography row goes live

**One of TWO doors.** Task 19 gives Settings the other. They are separate rows on
separate screens pushing the same `ScreenId`, and each gets its own test — a door
that opens the wrong screen, or nothing, is the defect both of these rows have
shipped before.

**Files:**
- Modify: `core/src/screen_reader_menu.cpp`
- Test: `test/unit/test_screen_reader_menu.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("the menu's Typography row opens the panel") {
  reader::ReaderMenuScreen scr("Middlemarch", "6%");
  // Contents is row 0 and is live, so one Down reaches Typography.
  scr.onEvent({reader::Button::Down, reader::PressKind::Short});
  REQUIRE(scr.vm().focusedRow == reader::ReaderMenuScreen::kTypography);
  const reader::Action a = scr.onEvent({reader::Button::Confirm, reader::PressKind::Short});
  CHECK(a.kind == reader::Action::Kind::Push);
  CHECK(a.target == reader::ScreenId::Typography);
}
```

`Action`'s target field is `target`, not `screen` (`app.h:55` — "meaningful for
Push and PopTo").

- [ ] **Step 2: Run to verify it fails**

Expected: FAIL — the row is not focusable, so the Down lands on `About this book`
or wherever the gate allows.

- [ ] **Step 3: Implement**

`kItems`: `{"Typography", "", false, true}` → `{"Typography", "", true, true}`,
and update the table's comment — it says "Typography and Bookmarks have boards
and no screens", which is now true of Bookmarks alone (issue #3).

`onGesture`'s `Activate` switch gains:

```cpp
        case kTypography:
          return Action::push(ScreenId::Typography);
```

- [ ] **Step 4: Run**

```bash
make test 2>&1 | tail -15
```
Expected: all pass. **The `reader_menu` goldens must NOT move** — a row becoming
focusable changes no pixel, because an inert row is drawn exactly as an unfocused
focusable one. **If they move, something is dimming an inert row**, which is a
design decision nobody made.

- [ ] **Step 5: Commit**

```bash
git add core/src/screen_reader_menu.cpp test/unit/test_screen_reader_menu.cpp
git commit -m "reader menu: the Typography row opens the panel

Its comment said 'Typography and Bookmarks have boards and no screens'. That
is Bookmarks alone now (#3).

The reader_menu goldens do not move, which is the check that inert and
unfocused are still drawn identically: row.focusable is about input, and a
theme that dimmed on it would be inventing a design decision.

Co-authored-by: Claude <claude@anthropic.com>"
```

---

### Task 21: ReaderScreen::relayout — the re-pagination

**Files:**
- Modify: `core/include/reader/screen_reader.h`
- Modify: `core/src/screen_reader.cpp`
- Test: `test/unit/test_reader_relayout.cpp` (new — remember the GLOB)

**This is the feature's core logic and it goes in `core/`**, because `shell/` has
no test harness and five bugs have hidden there. The shell gets one call.

`setMetrics` cannot serve: on the card path it calls `openChapterAt(chapterAt_,
false)`, which lands on **page one**. A reader who changed their type size has not
asked to go to the top of the chapter.

- [ ] **Step 1: Write the failing test**

Create `test/unit/test_reader_relayout.cpp`, reusing whatever
`test_reader_restream.cpp` uses to build a card-backed EPUB (read it first — it
builds a real one in a `FakeFileSystem`, using DEFLATE's stored-block mode so a
valid method-8 entry needs a framer and no compressor).

```cpp
TEST_CASE("relayout keeps the reader in the same block") {
  // The property that matters, and the only one the reader can perceive: after a
  // type change the page in front of them is the page holding the text they were
  // reading -- NOT page one, which is what setMetrics alone would give.
  auto fixture = buildCardBackedBook();  // see test_reader_restream.cpp
  reader::ReaderScreen scr(fixture.fs, fixture.book, /*startChapter=*/1, &body.face);
  scr.setMetrics(metricsAt(32, 444));
  REQUIRE(scr.completeIndex());

  // Read a few pages in, so page one is NOT the answer.
  for (int i = 0; i < 4; ++i)
    scr.onEvent({reader::Button::Right, reader::PressKind::Short});
  const reader::Cursor was = scr.currentCursor();
  REQUIRE(was.block > 0);
  REQUIRE(scr.pageIndex() > 0);

  // A bigger face in a narrower column: both halves of what a size-plus-margin
  // change does, so the page count and every boundary move.
  scr.relayout(metricsAt(46, 420));

  const reader::Cursor now = scr.currentCursor();
  CHECK(now.block == was.block);
  // THE LINE IS DROPPED, not carried. It is a line WITHIN a block at one ppem and
  // one column, so after a re-layout it names a layout that no longer exists --
  // which is exactly how reading_position.h grades this (Relaid). Landing on line
  // 9 of a block that now has four lines is a wrong page that looks like a bug.
  CHECK(now.line == 0);
}

TEST_CASE("relayout re-paginates: the page count changes with the type") {
  auto fixture = buildCardBackedBook();
  reader::ReaderScreen scr(fixture.fs, fixture.book, 1, &body.face);
  scr.setMetrics(metricsAt(32, 444));
  REQUIRE(scr.completeIndex());
  const int small = scr.pageCount();
  REQUIRE(small > 1);

  scr.relayout(metricsAt(46, 420));
  REQUIRE(scr.completeIndex());
  // Bigger type in a narrower column is strictly more pages. Asserted as an
  // INEQUALITY rather than a number, because the exact count is the layout's
  // business and pinning it here would make this a golden in disguise.
  CHECK(scr.pageCount() > small);
}

TEST_CASE("relayout drops the page ring, which was laid at the old metrics") {
  // A cached page is lines measured against one column and one face, so every
  // page in the ring is wrong the moment either moves. Serving one would draw the
  // old layout after the change -- indistinguishable from the change not working.
  auto fixture = buildCardBackedBook();
  reader::ReaderScreen scr(fixture.fs, fixture.book, 1, &body.face);
  scr.setMetrics(metricsAt(32, 444));
  REQUIRE(scr.completeIndex());
  for (int i = 0; i < 4; ++i)
    scr.onEvent({reader::Button::Right, reader::PressKind::Short});
  REQUIRE(scr.backwardHeadroom() > 0);   // the ring holds something

  scr.relayout(metricsAt(46, 420));
  CHECK(scr.backwardHeadroom() == 0);    // ...and now it does not
}

TEST_CASE("relayout with nothing open changes nothing and does not crash") {
  // The in-memory constructor, which the goldens and the simulator use. There is
  // no book to re-paginate; the guard is an early return before anything is
  // disturbed, which is the same shape openChapterAt needed for its own edge.
  reader::ReaderScreen scr(demoChapterBlocks(), &body.face);
  scr.setMetrics(metricsAt(32, 444));
  const int before = scr.pageCount();
  scr.relayout(metricsAt(46, 420));
  CHECK(scr.pageCount() >= 1);
  (void)before;
}
```

- [ ] **Step 2: Run to verify it fails**

Expected: COMPILE failure — no `relayout`.

- [ ] **Step 3: Implement**

`core/include/reader/screen_reader.h`, beside `setMetrics`:

```cpp
  // RE-PAGINATE AT THE PAGE THE READER IS ON, for a type or column change.
  //
  // setMetrics cannot serve: on the card path it re-opens the chapter and lands on
  // PAGE ONE, which is not what a reader who changed their type size asked for.
  // This captures where they are first, applies the metrics, and walks back to it.
  //
  // IT LANDS AT THE TOP OF THE BLOCK, dropping the cursor's LINE. A line index is
  // a line within a block at one ppem and one column width, so after a re-layout
  // it names a layout that no longer exists -- reading_position.h grades exactly
  // this as `Relaid` and zeroes the same field for the same reason. Landing on
  // line 9 of a block that now has four lines is a wrong page that looks like a
  // rendering bug.
  //
  // The page ring goes, because every page in it was measured against the old
  // column and face. So does the index, which is rebuilt by the walk -- so the
  // total returns to UNKNOWN and the footer draws its em dash until the deferred
  // count lands, which is what design/Typography.dc.html's footnote promises when
  // it says the book re-paginates in the background.
  //
  // COSTS ONE WALK to the reader's page, which is a chapter crossing's cost rather
  // than a page turn's. That is the honest price and it is paid on the press that
  // LEAVES the panel, where the user is already expecting the screen to change.
  void relayout(const PageMetrics& m);
```

`core/src/screen_reader.cpp`:

```cpp
void ReaderScreen::relayout(const PageMetrics& m) {
  // WHERE THEY ARE, taken BEFORE anything is disturbed -- and the LINE dropped
  // here rather than by the walk, so the one place that decides how much of a
  // cursor survives a re-layout is this line.
  const Cursor want{currentCursor().block, 0};

  metrics_ = m;
  dropPageRing();

  if (fs_ != nullptr && !book_.path.empty()) {
    // Re-open the chapter at the new metrics, then walk to the block. openChapterAt
    // rebuilds the index and leaves the stream live at page one; openAtCursor then
    // walks forward, which is the same single walk a restore takes.
    if (!openChapterAt(chapterAt_, false)) return;  // leaves the previous chapter standing
    openAtCursor(want);
    syncVm();
    return;
  }

  // THE IN-MEMORY CHAPTER, through the same landing the card path takes -- two
  // paths that paginated differently would mean the goldens and the simulator
  // testing something the device does not do, and this project has been bitten by
  // a desktop path that diverged from the device's before.
  startAt_ = want;
  setMetrics(m);
}
```

**Check `openChapterAt`'s failure contract before relying on that early return** —
it "restores the previous chapter on failure, at the cost of one extra decode",
which is what makes the `return` safe rather than leaving a half-open chapter.

**And check `startAt_`'s consumption**: `setMetrics` clears it before calling
`openAtCursor`, so this cannot leave a stale target.

- [ ] **Step 4: Run**

```bash
make test 2>&1 | tail -20
```
Expected: all pass, **no golden moved** (nothing calls `relayout` yet).

- [ ] **Step 5: Prove the tests bite**

| mutation | expected |
|---|---|
| `want` built as `currentCursor()` (keeping the line) | the `now.line == 0` case fails |
| `dropPageRing()` deleted | the ring case fails |
| `openAtCursor(want)` deleted | the same-block case fails (lands on page one) |

Run one at a time, `touch` after each edit and after each restore.

- [ ] **Step 6: Commit**

```bash
git add core/include/reader/screen_reader.h core/src/screen_reader.cpp \
        test/unit/test_reader_relayout.cpp
git commit -m "reader: relayout(), which re-paginates where the reader is standing

setMetrics re-opens the chapter and lands on PAGE ONE, which is not what a
reader who changed their type size asked for. This captures the cursor first,
applies the metrics, and walks back to it.

It lands at the TOP OF THE BLOCK and drops the line, in one place, because a
line index is a line within a block at one ppem and one column -- exactly what
reading_position.h grades as Relaid and zeroes for the same reason. Landing on
line 9 of a block that now has four is a wrong page that reads as a rendering
bug.

The page ring goes with it: every page in it was measured against the old
column and face, and serving one would draw the old layout after the change,
which is indistinguishable from the change not working.

In core/ rather than the shell because shell/ has no harness and five bugs
have hidden there. The shell gets one call.

Co-authored-by: Claude <claude@anthropic.com>"
```

---

### Task 22: The shell's sink re-inits the faces

**Files:**
- Modify: `shell/src/main.cpp` — `ShellSettingsSink::commit`

The preview has to show the chosen size, and the face the preview draws with is
the face the reader draws with. Re-initing it in place is what makes the preview
genuinely live rather than a second approximation.

- [ ] **Step 1: Add the re-init, guarded on the ppem actually changing**

In `ShellSettingsSink::commit`, after `gSettings = s;` and before
`applySettings()`:

```cpp
  bool commit(const reader::Settings& s) override {
    // THE PPEM IS THE ONE FIELD THAT COSTS SOMETHING TO APPLY, so it is asked
    // about rather than applied blindly: a commit from the SETTINGS screen never
    // touches typography, and re-initing at the same size would flush both glyph
    // caches for nothing -- ~127 glyphs to re-rasterise at ~3,794 us each the next
    // time a page is drawn.
    const int wasPpem = gSettings.bodyPpem;
    gSettings = s;
    applySettings();
    if (gSettings.bodyPpem != wasPpem) applyBodyPpem();
```

And a new function above the sink:

```cpp
// RE-RASTERISE THE BODY FACES AT THE CHOSEN SIZE.
//
// The Typography panel's preview draws with the SAME face object the reader draws
// with, which is what makes it a live preview rather than a second approximation
// of one -- and it is affordable only because that panel is a full screen: for as
// long as it stands the reader's page and metrics are stale, and nothing draws
// them.
//
// THE ARENA GROWS, AND THE PEAK IS BOTH ARENAS AT ONCE. ScalableFont::init takes
// the new block before releasing the old, so ppem 46 costs 24,576 + 16,384 for the
// roman transiently. Against a reading floor of 42,152 bytes that is tight, which
// is why the caller shrinks the reader's page ring on the way INTO the panel --
// see the push handler.
//
// A FAILED init KEEPS THE ARENA IT HAD and returns false, so an out-of-memory
// device is slower rather than dead. It is logged, because a size that silently
// did not take is a screen that looks like it ignored a button.
static void applyBodyPpem() {
  const uint32_t t = millis();
  const bool okBody = gBody.init(kFontBodySerif, kFontBodySerifSize, gSettings.bodyPpem);
  const bool okItalic =
      gItalic.init(kFontBodySerifItalic, kFontBodySerifItalicSize, gSettings.bodyPpem);
  logf("[typo] body ppem=%d roman=%s italic=%s line=%d in %lums free=%u min=%u\n",
       gSettings.bodyPpem, okBody ? "ok" : "FAILED", okItalic ? "ok" : "FAILED",
       gBody.lineHeight(), (unsigned long)(millis() - t),
       (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
  logFlush();
}
```

Check the real names of the italic's embedded array and size
(`kFontBodySerifItalic`/`...Size`) against the existing `init` call around line
3083 before writing this.

- [ ] **Step 2: Extend the log line**

The sink's existing `[settings]` log names three fields. Add the four:

```cpp
    logf("[settings] sleepAfterMs=%lu fullRefreshEvery=%d fullOnTransition=%d "
         "ppem=%d margins=%d lead=%d justify=%d -> %s\n",
         (unsigned long)gSettings.sleepAfterMs, gSettings.fullRefreshEvery,
         (int)gSettings.fullOnTransition, gSettings.bodyPpem, gSettings.margins,
         gSettings.lineSpacing, (int)gSettings.justify,
         wrote ? "applied and saved" : "APPLIED BUT NOT SAVED (...)");
```

...and the same four onto `loadAndApplySettings`'s `[boot] settings in force` line,
because a device booting with a hand-edited size must say so.

- [ ] **Step 3: Build the firmware**

```bash
git submodule update --init
make firmware 2>&1 | tail -20
```
Expected: builds. **"Failed to install Python dependencies into penv" is transient
— retry it, and do not run two builds at once.** Note the flash figure; the plan
expects no measurable change (no new assets).

- [ ] **Step 4: Commit**

```bash
git add shell/src/main.cpp
git commit -m "shell: the sink re-rasterises the body faces when the size changes

The preview draws with the SAME face object the reader draws with, which is
what makes it live rather than a second approximation -- and it is affordable
only because the panel is a full screen: the reader's page and metrics are
stale for as long as it stands and nothing draws them.

Guarded on the ppem actually moving, because a commit from the SETTINGS screen
never touches typography and re-initing at the same size would flush both
glyph caches for nothing.

[typo] carries the heap either side, because init takes the new arena before
releasing the old -- ppem 46 is 24,576 + 16,384 transient for the roman
against a 42,152-byte reading floor, and a failed init keeps the arena it had
rather than aborting.

Co-authored-by: Claude <claude@anthropic.com>"
```

---

### Task 23: The apply path — popTo, re-metrics, relayout

**Files:**
- Modify: `shell/src/main.cpp` — the pre-dispatch and post-dispatch blocks
- Modify: `core/include/reader/app.h` — a non-const stack accessor, if there is
  not one already

`TypographyScreen::onGesture` answers a plain `Action::pop()` from Task 11, so
there is nothing to change in `core/` here. This task is the shell's half, and the
condition it turns on is **"a Reader is anywhere on the stack"**, not "the Reader
is on top".

**That distinction is load-bearing twice.** From Settings there is no Reader at all
and nothing should be re-paginated. From the reader menu the pop lands on the MENU,
which is an overlay -- `App::render` walks down to the topmost non-overlay, paints
the Reader, then paints the overlay over it -- so the Reader's stale page IS drawn
on the next frame. "On top" would never fire there and the frame would be wrong.

- [ ] **Step 1: Shrink the page ring on the way IN**

In the shell's pre-dispatch block, beside the `[toc]` priming (around line 4012),
add:

```cpp
    // ENTERING THE TYPOGRAPHY PANEL: give the page ring back before the faces are
    // re-rasterised at a new size.
    //
    // Every page in it was laid at the CURRENT column and face, so a type change
    // invalidates all of them -- relayout() drops them anyway. Dropping them HERE
    // instead buys the headroom ScalableFont::init needs, because init takes the
    // new arena before releasing the old: at ppem 46 the roman alone is 24,576
    // transient on top of the 16,384 it already holds.
    //
    // At kPageCacheMaxDepth the ring is ~12 KB; depth 1 is the floor
    // setPageCacheDepth clamps to, and the excess is dropped immediately rather
    // than at the next insertion.
    if (ev.button == reader::Button::Confirm &&
        gApp->top().id() == reader::ScreenId::ReaderMenu) {
      const auto& menu = static_cast<const reader::ReaderMenuScreen&>(gApp->top());
      if (menu.vm().focusedRow == reader::ReaderMenuScreen::kTypography) {
        for (int i = 0; i < gApp->depth(); ++i) {
          if (gApp->at(i).id() != reader::ScreenId::Reader) continue;
          auto& rd = static_cast<reader::ReaderScreen&>(
              const_cast<reader::Screen&>(gApp->at(i)));
          rd.setPageCacheDepth(1);
        }
      }
    }
```

**`App::at` returns a const reference**, and BOTH halves of this task need a
mutable `ReaderScreen*` off the stack — this step to shrink the ring, and Step 2 to
relayout. `gPendingSpine`'s existing path cannot serve either: it uses
`&gApp->top()`, which is non-const but only ever the TOP screen, and the Reader is
underneath in both cases here.

So **add one accessor to `App`** and use it in both places:

```cpp
  // The stack, mutably, by index. `at()` is const because a renderer must not
  // move a screen it is drawing; this exists for the shell, which legitimately
  // has to reach a screen BELOW the top -- the Typography panel's apply path
  // re-paginates the Reader under the menu it was dismissed from.
  //
  // A const_cast at the call site would do the same thing and say nothing about
  // why it is allowed, which is the difference worth one method.
  Screen& atMut(int index);
```

Then a small shell helper, so neither caller repeats the walk or the cast:

```cpp
// The Reader anywhere on the stack, or null. Scanned rather than tracked, for the
// reason the book-closed check is scanned: a remembered depth would be a second
// copy of the stack's own shape, and the stack is three deep at most here.
static reader::ReaderScreen* readerOnStack(reader::App& app) {
  for (int i = 0; i < app.depth(); ++i)
    if (app.at(i).id() == reader::ScreenId::Reader)
      return static_cast<reader::ReaderScreen*>(&app.atMut(i));
  return nullptr;
}
```

**Use `readerOnStack(*gApp)` in this step and in Step 2**, in place of the walk
each would otherwise write for itself.

- [ ] **Step 2: Apply after the pop**

Beside the `gPendingSpine` block (which acts after the dispatch, for the identical
reason), add:

```cpp
    // TYPOGRAPHY APPLIED, after the pop that its DONE returns.
    //
    // Read BEFORE the dispatch would be too early -- the last step may be the
    // press being dispatched -- and after the pop the screen is gone, so the flag
    // is set by the sink and consumed here. `gSettings` is already current: the
    // sink applied every step as it happened.
    //
    // ORDERING IS LOAD-BEARING: dispatch, then apply, then paint, in one loop
    // iteration. Between the pop and this call the Reader's page and metrics
    // describe a layout that no longer exists, and a paint in that window would
    // draw old line positions in a new face.
    // A READER ANYWHERE ON THE STACK, not on top. Two reasons, and both are real:
    // from Settings there is no Reader at all and nothing should be re-paginated;
    // from the reader menu the pop lands on the MENU, which is an overlay, so
    // App::render paints the Reader beneath it on the very next frame -- and "on
    // top" would never fire there, leaving that frame drawn from stale metrics.
    reader::ReaderScreen* rd = readerOnStack(*gApp);
    if (gTypographyDirty && rd != nullptr) {
      gTypographyDirty = false;
      reader::PageMetrics m;
      gTheme.readerMetrics(gFrame->width(), gFrame->height(), *gFonts, gBody, gSettings, m);
      m.italic = &gItalic;
      gFactory.setReaderMetrics(m);  // so a later push builds at the new column
      const uint32_t t = millis();
      rd->relayout(m);
      logf("[typo] relaid column=%dx%d ppem=%d page=%d/%d in %lums\n", m.columnW,
           m.columnH, gSettings.bodyPpem, rd->pageIndex() + 1, rd->pageCount(),
           (unsigned long)(millis() - t));
      logFlush();
      // THE POSITION IS WORTH SAVING NOW. The cursor's `line` was just dropped and
      // the record stores the ppem and columnW the line was laid at, so writing it
      // here means the NEXT boot's fitOf grades against the new numbers and reads
      // Exact rather than Relaid a second time.
      saveReadingPosition("typography");
    } else if (gTypographyDirty) {
      // NO READER ON THE STACK -- the Settings route. Nothing to re-paginate, and
      // nothing that needs it: the settings are applied and persisted already. But
      // the FACTORY still has to learn the new column, or the next book opened
      // would be laid out at the old one and the change would look like it had not
      // been saved.
      gTypographyDirty = false;
      reader::PageMetrics m;
      gTheme.readerMetrics(gFrame->width(), gFrame->height(), *gFonts, gBody, gSettings, m);
      m.italic = &gItalic;
      gFactory.setReaderMetrics(m);
      logf("[typo] no reader open; column=%dx%d ppem=%d for the next book\n", m.columnW,
           m.columnH, gSettings.bodyPpem);
      logFlush();
    }
```

Declare `static bool gTypographyDirty = false;` beside the other shell globals,
and set it in `applyBodyPpem`... **no — set it in the sink**, for every typography
field and not just the ppem: margins, line spacing and alignment all change the
column or the layout without touching the face.

```cpp
    // ANY of the four, not just the ppem: margins change the column, and line
    // spacing and alignment change the layout, all without touching a face.
    if (gSettings.bodyPpem != wasPpem || gSettings.margins != wasMargins ||
        gSettings.lineSpacing != wasLead || gSettings.justify != wasJustify)
      gTypographyDirty = true;
```

- [ ] **Step 3: Build**

```bash
make test 2>&1 | tail -10 && make firmware 2>&1 | tail -8
```
Expected: both succeed.

- [ ] **Step 4: Commit**

```bash
git add shell/src/main.cpp core/include/reader/app.h
git commit -m "typography: DONE pops to the Reader, and the shell re-paginates it

The Contents pattern verbatim, and for the same reason: the panel is pushed
from the reader MENU, so a plain pop would land on the menu and the type
change would not be visible until that was dismissed too -- a setting that
appears not to have taken.

Ordering is load-bearing: dispatch, then apply, then paint, in one loop
iteration. Between the pop and the apply the Reader's page and metrics
describe a layout that no longer exists, and a paint in that window draws old
line positions in a new face.

The dirty flag covers all four fields, not just the ppem: margins change the
column and line spacing and alignment change the layout without touching a
face.

The page ring is given back on the way IN, before any re-rasterise, because
ScalableFont::init takes the new arena before releasing the old and ppem 46 is
24,576 bytes transient against a 42,152-byte floor.

The position is saved after the relayout, so the next boot's fitOf grades
against the new ppem and columnW and reads Exact rather than Relaid twice.

Co-authored-by: Claude <claude@anthropic.com>"
```

---

# PHASE 8 — DOCUMENTATION AND THE BOARD

### Task 24: CLAUDE.md and the roadmap

**Files:**
- Modify: `CLAUDE.md`
- Modify: `docs/superpowers/plans/2026-08-20-v1-roadmap.md`

CLAUDE.md is the file the next person reads. **Record what was DECIDED and why,
not what was built** — the code says what was built.

- [ ] **Step 1: Add to CLAUDE.md**

A new section after **The reader's menu and the chapter list**, covering only what
is not derivable from the code:

- **The Typography panel has ONE mode, and the two-mode design that preceded it
  is worth recording as rejected**: `DONE` and `OK` are synonyms, so its hint bar
  gave two words for "finished" and nothing said which scope each acted on.
  `CHANGE` cycles in place, which is Settings' own mechanism for the same job.
- **The values WRAP, and this screen is where that is free** — the hazard is
  auto-repeat, and this screen declares none. `setWrapping(false)` still has no
  caller.
- **Focusability is DERIVED from the value count**, so `Font` is unreachable
  while one face is vendored and becomes reachable when a second lands, with no
  line to remember. That replaced a chevron affordance the two-mode design carried.
- **The band's right slot is empty and still RESERVED on the board.** A band's
  height must not vary by screen — the hint-bar reasoning — and `bandContentH`
  takes `max(Label500, Value700)` unconditionally, so the board holds the line box
  to match. Removing the div outright shrank the board's band by 2px.
- **The preview's line count measures INK, not line boxes**, and the reason is
  one whole line on one geometry only. It is a file-local helper rather than a
  `components.h` primitive, and explicitly NOT `clampProse` — which takes a line
  budget and ellipsises the remainder, both wrong for a window onto a specimen.
- **ppem 38 is on the list because 18 PT is the board's own value**, and that is
  what makes roadmap:1269 answerable on the panel. **State plainly that this does
  NOT answer it.**
- **The preview cannot preview the margins**, and why trying would be worse.
- **`readerMetrics` takes the `Settings` and has no default argument**, and why a
  default would be dangerous.
- **`relayout` lands at the top of the block**, and that `fitOf` already graded
  this before the feature existed.
- **The page ring is given back on the way into the panel**, because `init` peaks
  at both arenas.

- [ ] **Step 2: Correct two stale claims found on the way**

Both are small and both would mislead:

1. **The `upperLatin1` section describes `fontc.py`'s subset as "`0x20..0x7E`
   plus all of `0xA0..0xFF`".** It also carries eight punctuation codepoints —
   `0x2013 0x2014 0x2018 0x2019 0x201C 0x201D 0x2026 0x2039 0x203A` — plus
   `0xFFFD`. That understatement is what made the chevrons look like they needed
   font work; they did not.
2. **`test_focus_restore.cpp` counts SEVEN focused screens, not five.** The
   **Storage** section says "counts the screens whose focus can move (five)". It
   was seven before this feature and is eight after.

- [ ] **Step 3: Add to the roadmap**

Under the 3C-and-beyond scope line, mark the typography panel done, and — the
important part — **update `roadmap:1269`**. Do not delete it: it is still an open
question. Say that the panel now makes it answerable on the panel, that the
default is deliberately unchanged, and that answering it is its own card.

- [ ] **Step 4: Verify the writes landed**

```bash
grep -c "no cancel" CLAUDE.md
grep -c "0x2039" CLAUDE.md
grep -n "counts.*seven\|(seven)\|(eight)" CLAUDE.md
git diff --stat
```

**Check the diff stat before committing.** A 128 KB deletion is obvious in one
line of `git diff --stat` and invisible in a script's success message; this repo
has committed a 0-byte CLAUDE.md once.

- [ ] **Step 5: Commit**

```bash
git add CLAUDE.md docs/superpowers/plans/2026-08-20-v1-roadmap.md
git commit -m "docs: the Typography panel's decisions, and two stale claims

What was decided and why, not what was built.

Two corrections found while building it:

- The chrome subset is not 0x20..0x7E plus 0xA0..0xFF. It also carries eight
  punctuation codepoints and U+FFFD, which is why the board's chevrons needed
  no font work at all -- the understatement had me plan a make fonts pass and
  a flash cost that were not real.
- test_focus_restore counts SEVEN focused screens, not five. It has said seven
  since the reader menu and contents landed; the Storage section still said
  five. Eight now.

roadmap:1269 is UPDATED, not closed. 'Is the reader under-sized by its own
spec' is still open, the default is deliberately unchanged, and the panel's
only contribution is that 18 PT can now be reached by pressing a button.
Answering it is its own card.

Co-authored-by: Claude <claude@anthropic.com>"
```

---

### Task 25: On glass — and STOP

**Files:** none

**`On glass` → `Done` needs device evidence, and an agent can never make that
move.** Flashing must be run by the user; the user is the only one who can produce
the photo or the serial log. **"The tests pass" is not evidence and must not close
this card.**

- [ ] **Step 1: Move the card to On glass**

```bash
gh project item-edit --id PVTI_lAHOAkvc3c4BhZ5gzg36kKk \
  --project-id PVT_kwHOAkvc3c4BhZ5g \
  --field-id PVTSSF_lAHOAkvc3c4BhZ5gzhgVwC4 \
  --single-select-option-id 5012a8f7
```

- [ ] **Step 2: Do NOT write `Closes #4`**

It would close the card on desktop evidence, which is the failure above with a
keyword attached. This repo has shipped work that passed every desktop test and
was wrong on the panel: the `App::render` overlay bug, `anchorJumped`'s
self-recursion, the veil smearing diagonally under CCW rotation.

- [ ] **Step 3: Give the user the flash command**

```bash
~/.platformio/penv/bin/python -m platformio run -e xteink -t upload
```

- [ ] **Step 4: Name exactly what needs verifying, and why the desktop cannot**

| what | why the desktop cannot answer it | how to read it |
|---|---|---|
| **ppem 46 with a book open** | The desktop has no 42 KB floor and no `nothrow` failure path taken. `init` peaks at both arenas. | `[typo] body ppem=46 roman=ok` with `min=` above zero. A `FAILED` means the ring shrink was not enough. |
| **The relayout's cost** | The desktop-to-device ratio on a path that touches the card and the inflater is ~135x, not the ~37x render ratio — this project has already been wrong by 8x here. | `[typo] relaid ... in Xms`, and `[i]` should read like a chapter crossing (~1.1 s) rather than a page turn. |
| **Is `RAGGED` better at this measure?** | The roadmap records rivers at 32px in a ~400px measure as inherent. Ragged is the first thing that can test that claim, and it is a judgement on glass. | Read a page each way. |
| **Is 18 PT the right default?** | roadmap:1269 says decide on the panel, not on a monitor. | Read at 15 and at 18. Its own card either way. |
| **A read-only card** | `writeAll` calls `noteCardGone()` on a write that fails after opening, which `pollCardPresence` turns into the SD-missing screen. A refused typography save must NOT throw the reader out of a book. | Tab the card write-protect, change a size, confirm the value shows and the reader survives. |

- [ ] **Step 5: STOP.** Report what was built, what is on the card, and the list
  above. Do not claim the feature works on the device.

---

## Self-review notes, and two corrections to this plan

Run after writing, kept here because they are corrections a reader needs:

**1. Signatures are consistent as written.** `TypographyScreen`'s constructor
takes four arguments from Task 11 onward; `Theme::renderTypography` takes the body
face as a POINTER with null supported, as `renderReader` takes its italic;
`Action`'s target field is `target`; `Back` answers a plain `pop()` from Task 11
and Task 23 changes nothing in `core/`; the `typography*Label` helpers are local to
`screen_typography.cpp` and are never extracted. The preview height is
`typographyPreviewBoxH`, a file-local helper in `theme_quiet.cpp` — **not** on
`Theme`, because no screen ever asks how tall the box is.

**2. Spec coverage.** Every section of the spec maps to a task:

| spec section | task |
|---|---|
| Where the state lives | 6, 7 |
| The screen / one mode | 11, 12 |
| The Font row is drawn and unreachable | 11, 12 |
| The values | 6, 11 |
| Alignment is one line in layout.cpp | 8 |
| The preview box | 15 |
| The apply path | 21, 22, 23 |
| Settings' TYPOGRAPHY rows | 19 |
| Board work | 1, 2, 3, 4 |
| Testing | 7, 8, 9, 12, 14, 17, 19, 21 |
| Device verification | 25 |

**Not covered by any task, deliberately:** the spec's "republish the design
canvas" board item. It is issue #34's, which already says the canvas is two boards
behind; this feature adds a third. **Say so when handing over rather than
silently leaving it** — and it is one line to add to that issue.
