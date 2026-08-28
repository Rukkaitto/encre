# Typography panel with live re-pagination — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Typography panel reached from the reader's menu that changes the body
face's size, margins, line spacing and alignment, previews them live, persists
them to `/.reader/settings.json`, and re-paginates the open chapter from the page
the reader was on.

**Architecture:** Four new fields on `reader::Settings`. A new
`TypographyScreen` in `core/` holding a `Settings` copy and a `SettingsSink*`,
the same pair `SettingsScreen` holds. A two-mode focus machine (browse / edit)
whose value steppers wrap. `DONE` answers `popTo(ScreenId::Reader)`, the Contents
pattern verbatim, and the shell re-inits the body faces, recomputes
`Theme::readerMetrics` and re-paginates at the current block cursor.

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
| `design/TypographyEditing.dc.html` | The panel's edit state — chevrons and the second hint set |
| `core/include/reader/screen_typography.h` | `TypographyScreen`: the two-mode focus machine and the value tables' declaration |
| `core/src/screen_typography.cpp` | The value tables, the label helpers, the stepper |
| `test/unit/test_screen_typography.cpp` | The mode machine, wrapping, chevrons, hints |

**Modified**

| File | Change |
|---|---|
| `design/Typography.dc.html` | Fixed preview height; browse state draws no chevrons; specimen completed |
| `design/Settings.dc.html` | `Size` value `18 PT` → `15 PT` |
| `tools/compare-design.py` | Add `typography_editing` |
| `core/include/reader/settings.h` | Four fields, their ranges, `validate()` doc |
| `core/src/settings.cpp` | `validate()` clamps; load/save the four |
| `core/include/reader/layout.h` | `PageMetrics::justify` |
| `core/src/layout.cpp` | Gate justification on it |
| `core/include/reader/viewmodel.h` | `TypographyViewModel` |
| `core/include/reader/theme.h` | `renderTypography`, and `readerMetrics` taking the `Settings` |
| `core/src/theme_quiet.cpp` | `renderTypography`, a file-local `typographyPreviewBoxH`, and `readerMetrics` reading the settings |
| `core/include/reader/theme_quiet.h` | The two overrides |
| `core/include/reader/app.h` | `ScreenId::Typography` |
| `core/src/app.cpp` | `screenName` |
| `core/src/session_record.cpp` | `"typography"` |
| `core/include/reader/screens.h` + `core/src/screens.cpp` | The factory case |
| `core/src/screen_reader_menu.cpp` | The `Typography` row goes live |
| `core/src/screen_settings.cpp` | The five TYPOGRAPHY rows read `settings_` |
| `sim/main.cpp` | `typography` and `typography_editing` subcommands |
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

**A UI change goes into the design HTML first, then the implementation.** This
phase touches no C++ at all.

**### STOP AT THE END OF PHASE 1.** Task 5 is a hard checkpoint: the user reviews
the rendered boards before any code is written. Do not begin Phase 2 until they
have said the design is good.

---

### Task 1: Typography.dc.html — fixed preview, browse state, full specimen

**Files:**
- Modify: `design/Typography.dc.html`

Three changes, each with its own reason:

1. **The preview box gets a fixed height.** Content-sized, the box grows with the
   type and every row below it moves on every press. Fixed, the rows never move.
   The height is the panel less the fixed runs; on the X4 that leaves **264px of
   text area**, so the box is `height: 292px` (264 + 2×2px border + 2×12px
   padding) with `box-sizing: border-box` and `overflow: hidden`.
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

# 1 + 3: the preview box gets a fixed height and the full sentence.
edits.append((
  '<div style="margin: 16px 24px 0 24px; border: 2px solid #000000; padding: 12px 16px; font-family: Literata, Georgia, serif; font-size: 32px; line-height: 1.7; text-align: justify;">Miss Brooke had that kind of beauty which seems to be thrown into relief.</div>',
  # THE BOX IS FIXED, NOT CONTENT-SIZED. Content-sized it grows with the type and
  # walks all five rows down the panel on every press; on e-ink that reads as the
  # whole screen jumping. 292px is the panel less every fixed run (band, LIVE
  # PREVIEW label, five rows, footnote, hint bar) -- 264px of text plus this box's
  # own 4px of border and 24px of padding. The firmware DERIVES the same number
  # from the same runs rather than reading this one; see typographyPreviewBoxH in
  # core/src/theme_quiet.cpp.
  #
  # AND IT CANNOT PREVIEW THE MARGINS. This box is chrome geometry -- 396px of
  # measure -- where the reading column is panelW - 2*margins, 444px by default.
  # Four of the five settings show here faithfully; Margins never will.
  '<div style="margin: 16px 24px 0 24px; border: 2px solid #000000; padding: 12px 16px; height: 292px; box-sizing: border-box; overflow: hidden; font-family: Literata, Georgia, serif; font-size: 32px; line-height: 1.7; text-align: justify;">Miss Brooke had that kind of beauty which seems to be thrown into relief by poor dress.</div>'))

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
panel on every press. Fixed at 292px -- the panel less every other run -- the
rows never move, which is the scroll-rail gutter trade again: a reflow you see
every time loses to a fixed cost you see once.

The chevrons go with it. They belong to the edit state, which is its own
board, and drawing them here promised a step on a browse-mode row.

The specimen is Middlemarch's real sentence now, because the box it has to
fill is fixed and the truncated form left a quarter of it empty.

Co-authored-by: Claude <claude@anthropic.com>"
```

---

### Task 2: TypographyEditing.dc.html — the edit state

**Files:**
- Create: `design/TypographyEditing.dc.html`

A second state of one screen, as `LibraryScrolled.dc.html` is of
`Library.dc.html`. It is what pins the two things Task 1's board cannot show:
the chevrons, and the second hint set.

**It edits `Size`, not `Font`.** `Font` has one value and therefore no chevrons,
so it is the one row that cannot illustrate the edit state. `Size` shows
`< 18 PT >` — the board's own stated value, which is also the step that answers
the roadmap's open question about the reader being under-sized.

**Only the Confirm slot's LABEL changes**, `EDIT` → `OK`. The hint MARKS are per
slot, not per label, so the filled dot stays.

- [ ] **Step 1: Create the file**

Copy Task 1's finished board and change exactly four things: the header label
stays `TYPOGRAPHY`; the `Font` row is no longer focused (white, weight 500); the
`Size` row is focused (black fill, white text) and its value reads
`&lsaquo; 18 PT &rsaquo;`; the Confirm hint label reads `OK`.

```bash
cp design/Typography.dc.html design/TypographyEditing.dc.html
```

- [ ] **Step 2: Apply the four changes**

```python
#!/usr/bin/env python3
import pathlib
p = pathlib.Path("design/TypographyEditing.dc.html")
src = p.read_text()
edits = [
  # The Font row stops being focused: it is a plain row again.
  ('<div style="display: flex; justify-content: space-between; align-items: center; height: 50px; padding: 0 24px; background: #000000; color: #ffffff;">\n  <div style="font-size: var(--t-value); font-weight: 700;">Font</div>\n  <div style="font-size: var(--t-value); font-weight: 700;">LITERATA</div>\n</div>',
   '<div style="display: flex; justify-content: space-between; align-items: center; height: 50px; padding: 0 24px; border-bottom: 1px solid #000000;">\n  <div style="font-size: var(--t-value); font-weight: 500;">Font</div>\n  <div style="font-size: var(--t-value); font-weight: 700;">LITERATA</div>\n</div>'),
  # ...and the Size row takes it, WITH the chevrons -- which is the whole point of
  # this board. Both are drawn because the values WRAP, so a step always exists in
  # both directions. A one-value row draws neither; that is Font, above.
  ('<div style="display: flex; justify-content: space-between; align-items: center; height: 50px; padding: 0 24px; border-bottom: 1px solid #000000;">\n  <div style="font-size: var(--t-value); font-weight: 500;">Size</div>\n  <div style="font-size: var(--t-value); font-weight: 700;">18 PT</div>\n</div>',
   '<div style="display: flex; justify-content: space-between; align-items: center; height: 50px; padding: 0 24px; background: #000000; color: #ffffff;">\n  <div style="font-size: var(--t-value); font-weight: 700;">Size</div>\n  <div style="font-size: var(--t-value); font-weight: 700;">&lsaquo; 18 PT &rsaquo;</div>\n</div>'),
  # The Confirm slot's LABEL, not its mark: the marks are per slot.
  ('white-space: nowrap;">EDIT</div>', 'white-space: nowrap;">OK</div>'),
]
for old, new in edits:
    n = src.count(old)
    assert n == 1, f"count {n} for {old[:70]!r}"
    src = src.replace(old, new, 1)
p.write_text(src)
print("ok")
```

- [ ] **Step 3: Verify**

```bash
grep -c "lsaquo; 18 PT &rsaquo" design/TypographyEditing.dc.html
```
Expected: `1`

```bash
grep -c ">OK</div>" design/TypographyEditing.dc.html; grep -c ">EDIT</div>" design/TypographyEditing.dc.html
```
Expected: `1` then `0`

- [ ] **Step 4: Commit**

```bash
git add design/TypographyEditing.dc.html
git commit -m "design: TypographyEditing, the state that pins the chevrons

A second state of one screen, as LibraryScrolled is of Library, and it exists
because Typography.dc.html cannot show either of the two things the edit mode
adds: the chevrons, and the Confirm slot reading OK.

It edits Size rather than Font, because Font has one value and therefore no
chevrons -- it is the one row that cannot illustrate this state. Size shows
18 PT, which is the board's own stated value and the step that makes
roadmap:1269 answerable on the panel.

Both chevrons, because the values wrap: a step always exists in both
directions, and a one-value row draws neither.

Co-authored-by: Claude <claude@anthropic.com>"
```

---

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

### Task 4: compare-design.py learns the new board

**Files:**
- Modify: `tools/compare-design.py` — `FLOW_SCREENS`, beside `typography`

`typography` is already in the list. The new state board needs an entry or
`make compare` reports on one screen fewer than exists — the "a check that
reports on less than it claims" shape this repo records three times.

- [ ] **Step 1: Add the entry**

```python
#!/usr/bin/env python3
import pathlib
p = pathlib.Path("tools/compare-design.py")
src = p.read_text()
old = '    ("typography",      "Typography.dc.html",     "Typography"),\n'
n = src.count(old)
assert n == 1, f"count {n}"
new = old + '    # THE EDIT STATE, its own board for the reason every other state board is:\n    # a flag on `typography` would leave the goldens and this sheet unable to name\n    # it. It is what pins the chevrons and the second hint set.\n    ("typography_editing", "TypographyEditing.dc.html", "Typography / editing"),\n'
p.write_text(src.replace(old, new, 1))
print("ok")
```

- [ ] **Step 2: Verify**

```bash
grep -c "typography_editing" tools/compare-design.py
```
Expected: `1`

- [ ] **Step 3: Commit**

```bash
git add tools/compare-design.py
git commit -m "tooling: compare the Typography edit state too

A state board with no entry here is a screen the sheet does not report on,
which is the shape this repo has recorded three times -- the card probe
answered from cache, --only matching nothing, and make compare defaulting to
seven boards while CLAUDE.md called it the check that keeps design honest.

Co-authored-by: Claude <claude@anthropic.com>"
```

---

### Task 5: CHECKPOINT — render the boards and stop

**Files:** none

- [ ] **Step 1: Render all three boards at both geometries**

```bash
make compare COMPARE_ARGS="--only typography,typography_editing,settings --export build/overlay"
```

Expected: three screens, both geometries. `typography` and `typography_editing`
will report as NOT IMPLEMENTED — correct, no screen exists yet. `settings` must
still report a percentage, and it should be within ~0.1pp of what it was before
Task 3.

- [ ] **Step 2: Look at the pixels and say what you see**

Open the exported PNGs and the contact sheet. Check, and write down the answer
for each:

- Does the fixed preview box hold the completed sentence without clipping a line
  at 32px, at BOTH geometries? (The X3 is 528 wide and 792 tall — 8px shorter,
  48px wider, so it wraps differently.)
- Do the five rows sit clear of the footnote at both geometries?
- On `typography`, does any row draw a chevron? (It must not.)
- On `typography_editing`, is the `Size` row the inverted one, with both
  chevrons, and does the Confirm hint read `OK`?

- [ ] **Step 3: STOP. Hand the boards to the user.**

Report: what you rendered, what you saw, and the `settings` percentage before and
after Task 3. **Do not start Phase 2.** The user asked to check the design here.

---

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
// truncated). 27->12, 32->15, 38->18, 42->20, 46->22, all clean.
//
//   * 32 is the default because design/Reader.dc.html says `font-size: 32px`.
//   * 38 is on the list because 18 PT is the Typography board's own stated
//     value, and roadmap:1269 has held "is the reader under-sized by its own
//     spec" open since 2A-2 with the instruction to decide it ON THE PANEL.
//     This does not decide it; it makes it decidable by pressing a button.
//   * 46 is the top because the glyph cache is thrash-free to ppem ~46 (see
//     CLAUDE.md, The glyph cache). Past it the arena stops holding the
//     alphabet's union and every page re-rasterises at ~3,794 us a glyph.
inline constexpr int kBodyPpemSteps[] = {27, 32, 38, 42, 46};
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
// design/Typography.dc.html and design/TypographyEditing.dc.html.
//
// TWO BOARDS, ONE VIEW MODEL, and the difference between them is `editingRow`.
// Same mechanism as Home's three states, which are one layout with different
// words rather than three renders.
//
// It reuses ListRow for the rows, because a label and a right-aligned value is
// exactly ListRow's shape and this would be its fourth copy. What it does NOT
// reuse is a per-row editing flag: only one row can be edited at a time, so the
// state belongs to the SCREEN and a per-row bool would be four fields that must
// always be false.
struct TypographyViewModel {
  std::string title;      // "TYPOGRAPHY"
  // The band's right slot. The panel is only ever reached from inside a book, and
  // this says which book you return to -- the same job ReaderMenu's band does.
  std::string bookTitle;
  // The preview's copy. A FIXED specimen, not the book's text: see
  // screen_typography.h, which owns the string and the reason.
  std::string specimen;
  std::vector<ListRow> rows;
  int focusedRow = 0;
  // WHICH ROW IS BEING EDITED, or -1 for browse mode. The whole difference
  // between the two boards, and what tells the theme to draw chevrons.
  int editingRow = -1;
  // WHETHER THE EDITED ROW HAS ANYWHERE TO STEP. False for a one-value row --
  // `Font`, while one face is vendored -- and it is what keeps that row honest:
  // it is focusable, it enters edit mode, and it draws no chevrons because there
  // is nowhere to go. The only place the chevrons carry information beyond "this
  // is the edit mode", which is why it is computed from the value COUNT rather
  // than hardcoded in the theme.
  bool editingHasSteps = false;
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
git commit -m "viewmodel: TypographyViewModel, two boards and one model

editingRow is the whole difference between Typography.dc.html and
TypographyEditing.dc.html -- one layout with a different state, as Home's
three variants are one layout with different words.

editingHasSteps is computed from the value COUNT rather than hardcoded,
because it is the one thing keeping the Font row honest while a single body
face is vendored: focusable, enters edit mode, and draws no chevrons because
there is nowhere to step.

Co-authored-by: Claude <claude@anthropic.com>"
```

---

### Task 11: TypographyScreen — rows, focus, browse mode

**Files:**
- Create: `core/include/reader/screen_typography.h`
- Create: `core/src/screen_typography.cpp`
- Test: `test/unit/test_screen_typography.cpp`

**Remember the GLOB:** three new files, so `make test` (which re-runs `cmake -S .
-B build`), never a bare `cmake --build build`.

- [ ] **Step 1: Write the failing test**

Create `test/unit/test_screen_typography.cpp`:

```cpp
// The Typography panel: five rows over four settings, a two-mode focus machine,
// and value steppers that wrap.
#include <string>

#include "doctest.h"
#include "reader/screen_typography.h"
#include "reader/settings.h"

namespace {

using reader::Button;
using reader::PressKind;

// A sink that records what it was handed, so a test can assert the screen
// commits -- and can assert it commits the value it is SHOWING, which is the one
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
  reader::TypographyScreen scr(reader::Settings{}, nullptr, "Middlemarch", nullptr);
  const reader::TypographyViewModel& vm = scr.vm();
  REQUIRE(vm.rows.size() == 5);
  CHECK(vm.rows[0].label == "Font");
  CHECK(vm.rows[1].label == "Size");
  CHECK(vm.rows[2].label == "Margins");
  CHECK(vm.rows[3].label == "Line spacing");
  CHECK(vm.rows[4].label == "Alignment");
  // No headers and no chevrons: every row edits in place, none discloses.
  for (const reader::ListRow& r : vm.rows) {
    CHECK_FALSE(r.isHeader);
    CHECK_FALSE(r.discloses);
    CHECK(r.focusable);
    // The tracking column has no producer anywhere in this firmware and must not
    // acquire one here by accident.
    CHECK(r.trackingEm1000 == 0);
  }
  CHECK(vm.bookTitle == "Middlemarch");
  CHECK_FALSE(vm.specimen.empty());
}

TEST_CASE("the values shown are the settings', in the board's forms") {
  reader::TypographyScreen scr(reader::Settings{}, nullptr, "Middlemarch");
  const reader::TypographyViewModel& vm = scr.vm();
  CHECK(vm.rows[0].value == "LITERATA");
  // 32 * 72 / 150 = 15.36, truncated -- and 15 PT is what Settings.dc.html now
  // states for the same default.
  CHECK(vm.rows[1].value == "15 PT");
  CHECK(vm.rows[2].value == "COMFORTABLE");
  CHECK(vm.rows[3].value == "1.7");
  CHECK(vm.rows[4].value == "JUSTIFIED");
}

TEST_CASE("browse mode moves the focus and wraps, like every other list") {
  reader::TypographyScreen scr(reader::Settings{}, nullptr, "Middlemarch");
  CHECK(scr.focus() == 0);
  CHECK(scr.vm().editingRow == -1);  // browse

  scr.onEvent(kDown);
  CHECK(scr.focus() == 1);
  for (int i = 0; i < 3; ++i) scr.onEvent(kDown);
  CHECK(scr.focus() == 4);  // the last row
  scr.onEvent(kDown);
  CHECK(scr.focus() == 0);  // WRAPPED
  scr.onEvent(kUp);
  CHECK(scr.focus() == 4);  // and the other way
}

TEST_CASE("browse mode changes nothing") {
  RecordingSink sink;
  reader::TypographyScreen scr(reader::Settings{}, &sink, "Middlemarch");
  for (int i = 0; i < 8; ++i) scr.onEvent(kDown);
  CHECK(sink.commits == 0);
}

TEST_CASE("Back leaves the panel for the Reader, in either mode") {
  // ONE MEANING FOR BACK, and that is what having no cancel buys. A Back that
  // left edit mode in one mode and the screen in the other would be one action
  // reachable two ways -- the second-door problem that got `Close book` deleted.
  //
  // AND IT POPS TO THE READER, not one screen back: the panel is pushed from the
  // reader menu, so a plain pop would leave the menu standing over a page the
  // change has not reached.
  SUBCASE("from browse") {
    reader::TypographyScreen scr(reader::Settings{}, nullptr, "Middlemarch", nullptr);
    const reader::Action a = scr.onEvent(kBack);
    CHECK(a.kind == reader::Action::Kind::PopTo);
    CHECK(a.target == reader::ScreenId::Reader);
  }
  SUBCASE("from edit") {
    reader::TypographyScreen scr(reader::Settings{}, nullptr, "Middlemarch", nullptr);
    scr.onEvent(kConfirm);
    REQUIRE(scr.vm().editingRow == 0);
    const reader::Action a = scr.onEvent(kBack);
    CHECK(a.kind == reader::Action::Kind::PopTo);
    CHECK(a.target == reader::ScreenId::Reader);
  }
}

TEST_CASE("the screen reports the right id") {
  reader::TypographyScreen scr(reader::Settings{}, nullptr, "Middlemarch");
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

// design/Typography.dc.html, and design/TypographyEditing.dc.html for its edit
// state.
//
// A FULL SCREEN, NOT AN OVERLAY, despite being reached from an overlay: the board
// has its own header band, its own hint bar and an opaque background, so it
// clears the framebuffer and nothing of the page under it is visible. That is
// also what makes the live preview affordable -- see the apply path in
// shell/src/main.cpp: the reader's page and metrics are stale for as long as this
// screen stands, and nothing draws them.
//
// --- IT SHARES SettingsSink, AND THAT IS DELIBERATE ---------------------------
//
// The typography fields live in `Settings` with everything else, so the sink that
// applies and persists a Settings change is already the right shape. A second
// interface would be a second thing to wire, a second null case, and a second
// place for "applied but not saved" to be got wrong.
//
// The sink's contract is unchanged and it is the contract that matters here: it
// APPLIES and PERSISTS, in that order, and a refused write still leaves the new
// value on screen -- because the change HAS taken effect, and reverting the
// display would make a read-only card look like a screen that ignores its
// buttons.
//
// --- TWO MODES ----------------------------------------------------------------
//
// Browse: Up/Down move the focus, wrapping like every other list. Confirm enters
// edit mode on the focused row. Back leaves.
//
// Edit: the row draws `< VALUE >`, Up/Down step the value (wrapping), Confirm
// returns to browse. Back still leaves.
//
// THERE IS NO CANCEL, AND THAT IS WHAT MAKES BACK UNAMBIGUOUS. Every step commits
// immediately, as Settings' CHANGE does, so there is nothing for a cancel to
// undo -- and Back therefore means the same thing in both modes. A Back that
// dismissed the edit in one mode and the screen in the other would be one action
// reachable two ways, which is the shape that got `Close book` deleted.
//
// --- THE VALUES WRAP ----------------------------------------------------------
//
// Every list in this firmware wraps, and the recorded hazard is not the wrap: it
// is AUTO-REPEAT, where "a wrap belongs to a press and a hold rests at the end",
// which is why Focus::move(delta, held) clamps for a held button.
//
// THIS SCREEN DECLARES NO REPEAT, as Settings does not, and it must not -- every
// step re-inits the body face, so a held Up would race through the sizes
// re-rasterising the alphabet each time. One step per press, and the sharp edge
// on wrapping never arises.
class TypographyScreen : public FocusScreen {
 public:
  // WHAT THE PREVIEW SAYS. A FIXED specimen, not the book's own text.
  //
  // The book's text would make the band and the `LIVE PREVIEW` label agree with
  // each other, and it would mean reaching down the stack for the Reader's laid
  // page on a screen that is otherwise independent of it. A fixed string is
  // predictable at every size, which is what lets the preview box have a fixed
  // height.
  //
  // Middlemarch's opening sentence, which is the board's own copy, completed:
  // it has to fill a box that is now fixed, and the truncated form left a
  // quarter of it empty at the default size.
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
  TypographyScreen(const Settings& initial, SettingsSink* sink, std::string bookTitle,
                   const GlyphSource* body);

  ScreenId id() const override { return ScreenId::Typography; }
  Fidelity fidelity() const override { return Fidelity::Mono; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const TypographyViewModel& vm() const { return vm_; }
  const Settings& settings() const { return settings_; }
  // WHETHER A ROW IS BEING EDITED. The shell does not need it; the tests and the
  // simulator do, and the simulator needs it to render the second board.
  bool editing() const { return editing_; }

  // Which setting a row edits. The board's order, which is the only thing that
  // makes the table in the .cpp checkable against design/Typography.dc.html by
  // eye.
  enum class Field { Font, Size, Margins, LineSpacing, Alignment };

 private:
  // Steps the focused row's value by `delta`, wrapping. Commits.
  Action stepFocused(int delta);
  // Every row is focusable: there are no headers, and `Font` is reachable on
  // purpose -- it is a row that shows you there is nowhere to step rather than a
  // row you cannot reach.
  bool focusable(int index) const override;
  void syncVm() override;
  // How many values the field on this row offers. 1 for Font while one face is
  // vendored, which is what drives TypographyViewModel::editingHasSteps.
  static int valueCount(Field f);

  Settings settings_;
  SettingsSink* sink_;
  std::string bookTitle_;
  const GlyphSource* body_;
  bool editing_ = false;
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
// board draws one flat block -- so unlike Settings there is nothing here the focus
// has to skip.
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

// THE MARGIN LABELS, one per kMarginSteps entry and in the same order. Parallel
// arrays rather than a struct because kMarginSteps is public API in settings.h
// and validate() iterates it; a struct here would be a second table to keep in
// step with that one, where a static_assert on the LENGTHS cannot drift.
constexpr const char* kMarginLabels[] = {"TIGHT", "COMFORTABLE", "WIDE"};
static_assert(std::size(kMarginLabels) == std::size(kMarginSteps),
              "a margin step was added without its label");

// pt = ppem * 72 / 150, TRUNCATED, exactly as sleepLabel truncates minutes: the
// row describes a size the user is looking at, so rounding up would name a size
// the panel is not showing. Every offered ppem truncates cleanly (27->12, 32->15,
// 38->18, 42->20, 46->22), which is part of why those five were chosen.
std::string sizeLabel(int ppem) { return std::to_string(ppem * 72 / 150) + " PT"; }

// `1.7`, not `1.70`: design/Typography.dc.html's own form. em x 1000 with the
// trailing zero dropped, and the tenth is never zero across kLineSpacingSteps --
// asserted below rather than assumed, because "1.5" and "1.50" are a visible
// difference on a row whose whole content is four characters.
std::string leadLabel(int em1000) {
  const int whole = em1000 / 1000;
  const int tenths = (em1000 % 1000) / 100;
  const int hundredths = (em1000 % 100) / 10;
  std::string out = std::to_string(whole) + "." + std::to_string(tenths);
  if (hundredths != 0) out += std::to_string(hundredths);
  return out;
}

// Where `value` sits in an ascending table, or 0 when it is not on it.
//
// Settings::validate() snaps every field onto its table before the screen ever
// sees it, so the fallback is unreachable in practice. It is 0 rather than an
// assert because a screen that cannot be constructed is a device that cannot
// show its settings, and index 0 is a value the stepper can leave.
template <size_t N>
int indexIn(const int (&table)[N], int value) {
  for (size_t i = 0; i < N; ++i)
    if (table[i] == value) return static_cast<int>(i);
  return 0;
}

// A wrapping step over `count` values. Not Focus's: this is an index into a value
// table, not a focus, and borrowing Focus here would mean a second Focus object
// per screen whose range changes with the focused row.
int stepIndex(int at, int delta, int count) {
  if (count <= 1) return at;
  int next = (at + delta) % count;
  if (next < 0) next += count;
  return next;
}

}  // namespace

TypographyScreen::TypographyScreen(const Settings& initial, SettingsSink* sink,
                                   std::string bookTitle, const GlyphSource* body)
    : FocusScreen(static_cast<int>(kItems.size()), 0),
      settings_(initial),
      sink_(sink),
      bookTitle_(std::move(bookTitle)),
      body_(body) {
  // SNAPPED ON THE WAY IN, so the steppers index a table the value is on. The
  // shell's settings have already been through validate(), but the simulator and
  // the tests construct this directly -- and a screen that trusted its caller
  // here would show a value it could not step off.
  settings_.validate();
  setFocus(0);  // Font, and every row is focusable
  syncVm();
}

bool TypographyScreen::focusable(int index) const {
  return index >= 0 && index < static_cast<int>(kItems.size());
}

int TypographyScreen::valueCount(Field f) {
  switch (f) {
    // ONE FACE IS VENDORED. This is the number that makes the Font row draw no
    // chevrons, and it is the one line to change when a second face lands.
    case Field::Font: return 1;
    case Field::Size: return static_cast<int>(std::size(kBodyPpemSteps));
    case Field::Margins: return static_cast<int>(std::size(kMarginSteps));
    case Field::LineSpacing: return static_cast<int>(std::size(kLineSpacingSteps));
    case Field::Alignment: return 2;
  }
  return 1;
}

Action TypographyScreen::stepFocused(int delta) {
  const int f = focus();
  if (f < 0 || f >= static_cast<int>(kItems.size())) return Action::none();
  const Field field = kItems[static_cast<size_t>(f)].field;

  switch (field) {
    case Field::Font:
      // Nowhere to step, and the row says so by drawing no chevrons. Answering
      // none() rather than redraw() keeps a press that changes nothing off the
      // panel -- a ~520 ms waveform to redraw an identical frame.
      return Action::none();
    case Field::Size: {
      const int at = indexIn(kBodyPpemSteps, settings_.bodyPpem);
      settings_.bodyPpem =
          kBodyPpemSteps[stepIndex(at, delta, valueCount(field))];
      break;
    }
    case Field::Margins: {
      const int at = indexIn(kMarginSteps, settings_.margins);
      settings_.margins = kMarginSteps[stepIndex(at, delta, valueCount(field))];
      break;
    }
    case Field::LineSpacing: {
      const int at = indexIn(kLineSpacingSteps, settings_.lineSpacing);
      settings_.lineSpacing =
          kLineSpacingSteps[stepIndex(at, delta, valueCount(field))];
      break;
    }
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
    // ONE STEP WHATEVER THE DISTANCE. This screen declares no repeat, so a
    // gesture never carries more than one -- and `g.steps` is deliberately
    // ignored rather than trusted, because a held button that did arrive here
    // would race through the sizes re-rasterising the alphabet per step.
    case Gesture::Prev: return editing_ ? stepFocused(-1) : moveFocus(-1);
    case Gesture::Next: return editing_ ? stepFocused(+1) : moveFocus(+1);
    case Gesture::Activate:
      // ENTER on a browse press, LEAVE on an edit press. The chevrons appearing
      // and disappearing is the whole visible difference, so both directions owe
      // a redraw.
      editing_ = !editing_;
      syncVm();
      return Action::redraw();
    // POPS TO THE READER, not one screen back, and this is the Contents pattern
    // verbatim. The panel is pushed from the reader MENU, so a plain pop would land
    // back on the menu and the type change would not be visible until that was
    // dismissed too -- a setting that appears not to have taken.
    //
    // It cannot re-paginate the Reader itself: one is already on the stack under the
    // menu, and core/ has no faces to re-rasterise. So it names where to land and
    // the shell does the work -- see the apply path in shell/src/main.cpp.
    //
    // popTo STOPS AT THE ROOT if no Reader is on the stack (app.h states it), which
    // is a safe answer for a panel that is only reachable from a book.
    case Gesture::Back: return Action::popTo(ScreenId::Reader);
    default: return Action::none();
  }
}

void TypographyScreen::syncVm() {
  vm_.title = "TYPOGRAPHY";
  vm_.bookTitle = bookTitle_;
  vm_.specimen = kSpecimen;
  vm_.focusedRow = focus();
  vm_.editingRow = editing_ ? focus() : -1;

  vm_.rows.clear();
  vm_.rows.reserve(kItems.size());
  for (const Item& it : kItems) {
    ListRow row;
    row.label = it.label;
    switch (it.field) {
      // ONE FACE, and the row states its name rather than reading a field. A
      // `font` setting whose only value is its default would be a second
      // spelling of this constant.
      case Field::Font: row.value = "LITERATA"; break;
      case Field::Size: row.value = sizeLabel(settings_.bodyPpem); break;
      case Field::Margins:
        row.value = kMarginLabels[indexIn(kMarginSteps, settings_.margins)];
        break;
      case Field::LineSpacing: row.value = leadLabel(settings_.lineSpacing); break;
      case Field::Alignment: row.value = settings_.justify ? "JUSTIFIED" : "RAGGED"; break;
    }
    row.isHeader = false;
    // NOTHING DISCLOSES. Every row edits in place, so no row draws a chevron of
    // ListRow's kind -- the edit-mode chevrons are the theme's, drawn either side
    // of the VALUE, and they are a different mark for a different promise.
    row.discloses = false;
    row.focusable = true;
    vm_.rows.push_back(std::move(row));
  }

  const Field editedField =
      kItems[static_cast<size_t>(focus() < 0 ? 0 : focus())].field;
  vm_.editingHasSteps = editing_ && valueCount(editedField) > 1;

  // The board's own labels. Only the Confirm slot differs between the two modes:
  // the chevrons are what announce the mode, and a two-slot diff would be a
  // harder change to read on this glass than they are.
  vm_.hints = {"DONE", editing_ ? "OK" : "EDIT", "UP", "DOWN"};
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);
}

void TypographyScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                              Plane plane) const {
  // body_ may be null, and the THEME handles that -- it is what knows the
  // specimen cannot be drawn without a face, and resolving it here would mean
  // inventing a null GlyphSource for a case one branch covers.
  theme.renderTypography(fb, fonts, body_, vm_, plane);
}

}  // namespace reader
```

**`ScreenId::Typography` does not exist yet**, so this will not compile until Task
13. Do Task 13 now if the build blocks you — the two are one commit's worth of
work and the order between them is arbitrary.

- [ ] **Step 5: Add the leadLabel assertion the comment promises**

Append to `test/unit/test_screen_typography.cpp`:

```cpp
TEST_CASE("every line spacing step has a two-character label") {
  // The comment in leadLabel claims the tenth is never zero across the table.
  // Asserted, because "1.5" against "1.50" is a visible difference on a row whose
  // entire content is four characters.
  for (const int em : reader::kLineSpacingSteps) {
    reader::Settings s;
    s.lineSpacing = em;
    reader::TypographyScreen scr(s, nullptr, "Middlemarch");
    const std::string& v = scr.vm().rows[3].value;
    CHECK(v.size() == 4);          // `1.7`, `1.55` -> 3 or 4; see below
    CHECK(v.find('.') != std::string::npos);
  }
}
```

**This test as written is WRONG on purpose — fix it when you run it.**
`kLineSpacingSteps` holds both `1700` (`"1.7"`, 3 chars) and `1550` (`"1.55"`, 4
chars), so `v.size() == 4` fails for three of the five. Replace it with
`CHECK((v.size() == 3 || v.size() == 4))` and add the exact expected strings:

```cpp
  const char* expected[] = {"1.4", "1.55", "1.7", "1.85", "2"};
```

...and note `2000` produces `"2"`, not `"2.0"`, because `leadLabel` drops a zero
tenth. **Decide which the board wants**: the board states `1.7`, so a bare `2` is
consistent with dropping trailing zeros — but `2` reads as an integer rather than
a ratio beside `1.85`. Make `leadLabel` emit `"2.0"` by keeping the tenth
unconditionally and dropping only the hundredth, and assert
`{"1.4", "1.55", "1.7", "1.85", "2.0"}`.

- [ ] **Step 6: Run to verify it passes**

```bash
make test 2>&1 | tail -20
```
Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add core/include/reader/screen_typography.h core/src/screen_typography.cpp \
        test/unit/test_screen_typography.cpp
git commit -m "typography: the panel's five rows and its browse mode

Shares SettingsSink rather than growing a second interface: the fields live in
Settings with everything else, so the sink that applies-and-persists a
Settings change is already the right shape.

Every row is focusable, Font included. It is a row that shows you there is
nowhere to step -- valueCount() == 1, so no chevrons -- rather than a row you
cannot reach, and it is one line to change when a second face lands.

The values are snapped in the constructor, not trusted from the caller: the
steppers index a table by position, so a screen handed an off-table value
would show something it could not step off.

leadLabel emits 2.0 rather than 2, because a bare integer reads as a count
beside 1.85 rather than as a ratio.

Co-authored-by: Claude <claude@anthropic.com>"
```

---

### Task 12: Edit mode — wrapping steppers, hints, chevrons

**Files:**
- Test: `test/unit/test_screen_typography.cpp`

The behaviour landed in Task 11. This task is the tests that pin it, and they are
the ones worth writing carefully — the mode machine is where a two-mode screen
goes wrong.

- [ ] **Step 1: Write the tests**

```cpp
TEST_CASE("Confirm enters and leaves edit mode") {
  reader::TypographyScreen scr(reader::Settings{}, nullptr, "Middlemarch");
  CHECK(scr.vm().editingRow == -1);
  CHECK(scr.vm().hints[1] == "EDIT");

  scr.onEvent(kConfirm);
  CHECK(scr.vm().editingRow == 0);
  CHECK(scr.vm().hints[1] == "OK");
  // ONLY the Confirm slot changes.
  CHECK(scr.vm().hints[0] == "DONE");
  CHECK(scr.vm().hints[2] == "UP");
  CHECK(scr.vm().hints[3] == "DOWN");

  scr.onEvent(kConfirm);
  CHECK(scr.vm().editingRow == -1);
  CHECK(scr.vm().hints[1] == "EDIT");
}

TEST_CASE("edit mode steps the value and does NOT move the focus") {
  RecordingSink sink;
  reader::Settings s;
  reader::TypographyScreen scr(s, &sink, "Middlemarch");
  scr.onEvent(kDown);      // focus Size
  scr.onEvent(kConfirm);   // edit it
  REQUIRE(scr.focus() == 1);

  scr.onEvent(kDown);
  CHECK(scr.focus() == 1);                   // the focus did NOT move
  CHECK(scr.settings().bodyPpem == 38);      // 32 -> 38
  CHECK(scr.vm().rows[1].value == "18 PT");
  // AND THE COMMIT CARRIES WHAT THE ROW SHOWS. A screen that stepped its own copy
  // and committed a stale one would be invisible until the next boot.
  CHECK(sink.commits == 1);
  CHECK(sink.last.bodyPpem == 38);
}

TEST_CASE("every stepper wraps off both ends") {
  // WRAPPING, like every other list. The hazard this repo records is auto-repeat
  // -- a hold resting at the end -- and this screen declares no repeat, so it
  // does not arise. Checked on all four multi-value rows, because a stepper is
  // four separate switch arms and three of them passing proves nothing about the
  // fourth.
  struct Case { int row; int count; };
  const Case cases[] = {{1, 5}, {2, 3}, {3, 5}, {4, 2}};
  for (const Case& c : cases) {
    CAPTURE(c.row);
    reader::TypographyScreen scr(reader::Settings{}, nullptr, "Middlemarch");
    for (int i = 0; i < c.row; ++i) scr.onEvent(kDown);
    scr.onEvent(kConfirm);
    const std::string first = scr.vm().rows[static_cast<size_t>(c.row)].value;
    // A full cycle returns to where it started...
    for (int i = 0; i < c.count; ++i) scr.onEvent(kDown);
    CHECK(scr.vm().rows[static_cast<size_t>(c.row)].value == first);
    // ...and one step BACK from the start reaches the last value, which is the
    // half a forward-only test cannot see.
    scr.onEvent(kUp);
    CHECK(scr.vm().rows[static_cast<size_t>(c.row)].value != first);
    for (int i = 0; i < c.count - 1; ++i) scr.onEvent(kUp);
    CHECK(scr.vm().rows[static_cast<size_t>(c.row)].value == first);
  }
}

TEST_CASE("the Font row has nowhere to step, and says so") {
  RecordingSink sink;
  reader::TypographyScreen scr(reader::Settings{}, &sink, "Middlemarch");
  scr.onEvent(kConfirm);  // edit Font
  REQUIRE(scr.vm().editingRow == 0);
  // NO CHEVRONS: the one place the marks carry information beyond "this is edit
  // mode", and the reason editingHasSteps is computed from the value count.
  CHECK_FALSE(scr.vm().editingHasSteps);

  const reader::Action a = scr.onEvent(kDown);
  CHECK(a.kind == reader::Action::Kind::None);  // no waveform for an identical frame
  CHECK(scr.vm().rows[0].value == "LITERATA");
  CHECK(sink.commits == 0);                    // and nothing was written
}

TEST_CASE("a multi-value row in edit mode draws both chevrons") {
  reader::TypographyScreen scr(reader::Settings{}, nullptr, "Middlemarch");
  for (int row = 1; row <= 4; ++row) {
    CAPTURE(row);
    reader::TypographyScreen s2(reader::Settings{}, nullptr, "Middlemarch");
    for (int i = 0; i < row; ++i) s2.onEvent(kDown);
    CHECK_FALSE(s2.vm().editingHasSteps);  // browse: no chevrons on any row
    s2.onEvent(kConfirm);
    CHECK(s2.vm().editingHasSteps);
  }
  (void)scr;
}

TEST_CASE("a refused write still shows the new value") {
  // SettingsSink's contract: the change HAS taken effect in RAM, and reverting
  // the display would make a read-only card look like a screen that ignores its
  // buttons.
  RecordingSink sink;
  sink.answer = false;
  reader::TypographyScreen scr(reader::Settings{}, &sink, "Middlemarch");
  scr.onEvent(kDown);
  scr.onEvent(kConfirm);
  scr.onEvent(kDown);
  CHECK(sink.commits == 1);
  CHECK(scr.settings().bodyPpem == 38);
  CHECK(scr.vm().rows[1].value == "18 PT");
}

TEST_CASE("a held mover carries one step, not its distance") {
  // The gesture layer drops a Long on a mover and only emits Repeat where the
  // screen asked for one -- and this screen asks for none. `steps` is ignored
  // here rather than trusted, because a repeat that did arrive would race
  // through the sizes re-rasterising the alphabet on every one.
  reader::TypographyScreen scr(reader::Settings{}, nullptr, "Middlemarch");
  scr.onEvent(kDown);
  scr.onEvent(kConfirm);
  reader::GestureEvent g;
  g.what = reader::Gesture::Next;
  g.steps = 40;
  g.held = true;
  scr.onGesture(g);
  CHECK(scr.settings().bodyPpem == 38);  // ONE step, not forty
}
```

- [ ] **Step 2: Run**

```bash
make test 2>&1 | tail -20
```
Expected: all pass. If the held-mover case fails, `stepFocused` is reading
`g.steps` — it must not.

- [ ] **Step 3: Prove the wrap test bites**

```bash
# In stepIndex, replace the wrap with a clamp:
#   int next = at + delta; if (next < 0) next = 0; if (next >= count) next = count - 1;
touch core/src/screen_typography.cpp && make test 2>&1 | grep -c "FAILED"
```
Expected: non-zero. Restore, `touch`, re-run green.

- [ ] **Step 4: Commit**

```bash
git add test/unit/test_screen_typography.cpp
git commit -m "typography: pin the mode machine, the wrap and the Font row's silence

The wrap is checked on all four multi-value rows and in BOTH directions,
because a stepper is four switch arms and a forward-only cycle test passes
over a clamp at the bottom end.

The Font row answers none() rather than redraw(): a press that produces an
identical frame must not cost a ~520 ms waveform.

A held mover is asserted to carry ONE step and not its distance. This screen
declares no repeat so one should never arrive, but stepFocused ignores
g.steps rather than trusting that -- forty steps is forty re-rasterisations
of the alphabet.

Co-authored-by: Claude <claude@anthropic.com>"
```

---

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
      return std::make_unique<TypographyScreen>(settings_, settingsSink_, menuTitle_);
    }
```

`screens.h` needs `#include "reader/screen_typography.h"`.

**`menuTitle_` is the right source**: it is what the shell primes for the reader
menu, and the panel is only reachable from that menu — so if the menu could be
built, the title is there.

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

### Task 14: test_focus_restore learns the sixth focused screen

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
`menuTitle_` guard. Typography must NOT be refused on an empty title (unlike the
reader menu, whose header IS the title): an empty band is honest. Confirm the
factory case has no such guard.

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

**The chevrons are TYPE, not icons.** `&lsaquo;`/`&rsaquo;` are U+2039/U+203A, and
`tools/fontc.py`'s `CODEPOINTS` already lists both — they sit inline with the
value at the same size and weight, so they are the value's own glyphs.

**Verify that before writing the draw**, because a missing glyph renders as a
notdef box and a notdef box inks rows exactly like a letter does (this repo has
been fooled by that once, on Home's wrapped title):

```bash
grep -n "0x2039" tools/fontc.py && ls -la assets/built/spacegrotesk_700_12pt.rfnt
```

- [ ] **Step 1: Declare it on Theme**

`core/include/reader/theme.h`, beside `renderSettings`:

```cpp
  // design/Typography.dc.html, and design/TypographyEditing.dc.html for its edit
  // state -- ONE method for both, because they are one layout in two states and
  // `vm.editingRow` is the whole difference.
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

Task 11 already declares the fourth constructor argument and holds `body_`, so
`TypographyScreen::render` passes it straight through with no null handling of its
own — the theme owns that, which is where it belongs: the theme is what knows the
specimen cannot be drawn without a face.

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
// THE CHEVRONS' GAP either side of the value. The board writes them inside the
// same run as the value with ordinary spaces, so this is one space's width at the
// value's face -- measured rather than assumed, because the face's space advance
// is what the board's spaces render as.
constexpr int kTypoChevronGapSpaces = 1;
// THE FOOTNOTE'S COPY. In the theme rather than the view model because it is the
// BOARD'S text about how the screen behaves, not a fact about the current state --
// the same call renderSdMissing makes for its paragraph.
constexpr const char* kTypoFootnote =
    "APPLIES FROM YOUR CURRENT PAGE \xE2\x80\x94 THE BOOK RE-PAGINATES IN THE "
    "BACKGROUND.";
```

**The em dash is `\xE2\x80\x94` written as its own adjacent literal boundary.** A
C++ hex escape is unbounded, so `"\x94THE"` would parse `\x94T` as one escape —
the exact trap this repo has recorded twice (`"\xB7C"`, `"\xA0b"`). The string
above is safe because the next character after the escape is a space; if you
reflow it, keep it safe.

- [ ] **Step 3: Write the box-model helper**

In the same anonymous namespace:

```cpp
// THE PREVIEW BOX'S HEIGHT: the panel less every fixed run above and below it.
//
// DERIVED, NOT PINNED, which is Home's title-budget rule -- and it has to be, for
// the reason that rule exists: the two geometries differ by 8px of height, the
// band's height depends on its type role, and the hint bar's on the faces its
// labels are set in. A literal here would be right on one panel.
//
// It is FIXED with respect to the SETTINGS, though, which is the point: the box
// does not grow with the type, so the five rows below it never move. As many
// specimen lines fit as fit, and the rest of the box is slack.
int typographyPreviewBoxH(const Framebuffer& fb, const FontSet& fonts, int bandH,
                          int rowCount, const Hint (&hints)[4]) {
  const Font& meta = fonts[Role::Meta400];
  const int labelH = kTypoLabelPadTop + meta.lineHeight() + kTypoLabelPadBottom;
  // Rules BETWEEN rows only: the last row's bottom edge is the block's end, which
  // is renderSettings' and renderLibrary's rule verbatim.
  const int rowsH = kTypoRowsBorder + rowCount * kTypoRowH +
                    (rowCount > 0 ? (rowCount - 1) * kTypoRuleH : 0);
  // The footnote is two lines at the board's measure on both panels -- but ASKED
  // rather than assumed, because the firmware's whole-pixel advances measure ~3%
  // wider than Chrome's and a board's max-width is a number to check in both
  // engines (SdMissing's had to go 400 -> 420 for exactly this).
  const Prose foot = wrapProseLead(meta, kTypoFootnote, fb.width() - 2 * kMargin,
                                   kTypoFootLeadEm, trackingEm(meta, kTypoFootEm));
  const int footH = foot.height + kTypoFootPadBottom;
  const int fixed = bandH + kTypoPreviewTop + labelH + rowsH + footH +
                    hintBarHeight(fonts, hints);
  const int box = fb.height() - fixed;
  return box > 0 ? box : 0;
}
```

Check `Prose`/`wrapProseLead`/`drawProse`'s real signatures in
`core/include/reader/components.h` before writing this — the field may be
`height` or a line count, and `wrapProseLead` takes a `WordBreak` in some
overloads. **Use the existing signature; do not add an overload.**

- [ ] **Step 4: Write the render**

```cpp
void QuietTheme::renderTypography(Framebuffer& fb, const FontSet& fonts,
                                  const GlyphSource* body, const TypographyViewModel& vm,
                                  Plane plane) {
  fb.clear(true);

  Hint hints[4];
  buildHints(kHintSlotMarks, vm.hints, vm.holds, hints);

  // The band's right slot is the BOOK, which is the same band Settings draws with
  // a version in it and ReaderMenu draws with a title.
  const int afterBand = drawHeaderBand(fb, fonts, vm.title, vm.bookTitle, nullptr, plane);

  const int rowCount = static_cast<int>(vm.rows.size());
  const int boxH = typographyPreviewBoxH(fb, fonts, afterBand, rowCount, hints);

  // --- The preview box ---------------------------------------------------------
  int y = afterBand + kTypoPreviewTop;
  outlineRect(fb, kMargin, y, fb.width() - 2 * kMargin, boxH, kTypoPreviewBorder, plane);
  // AS MANY LINES AS FIT, and whole lines only: a clipped half-line reads as a
  // rendering fault, which is the defect Reader.dc.html's own column had.
  const int textW = fb.width() - 2 * kMargin - 2 * kTypoPreviewBorder - 2 * kTypoPreviewPadX;
  const int textH = boxH - 2 * kTypoPreviewBorder - 2 * kTypoPreviewPadY;
  // NO FACE, NO SPECIMEN -- the box is still drawn, because the box is the board's
  // and an absent preview is not an absent screen.
  if (body != nullptr && !vm.specimen.empty() && textW > 0 && textH > 0) {
    // Justified, as design/Typography.dc.html's box is -- and NOT following the
    // Alignment setting, because the box is chrome geometry and cannot preview the
    // reading measure anyway. Its job is to show the FACE, the SIZE and the LEAD.
    Prose p = wrapProseLead(*body, vm.specimen, textW, /*leadEm1000=*/0, {});
    clampProse(p, textH);
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
    if (focused) fb.fillRect(0, y, fb.width(), kTypoRowH, false);
    const Ink ink = focused ? Ink::White : Ink::Black;
    const Font& lf = focused ? labelFocused : label;

    // THE CHEVRONS ARE PART OF THE VALUE'S RUN, which is how the board writes
    // them -- one run, one measure, so the value stays centred between them
    // without a second alignment to get wrong. Drawn only on the row being
    // EDITED, and only when that row has somewhere to step: `Font` has one value
    // while a single body face is vendored, and a chevron there would promise a
    // step that does not exist.
    const bool chevrons = (i == vm.editingRow) && vm.editingHasSteps;
    std::string shown = row.value;
    if (chevrons) shown = std::string("\xE2\x80\xB9 ") + shown + " \xE2\x80\xBA";

    const int rightEdge = fb.width() - kMargin;
    const int valueW = shown.empty() ? 0 : value.measure(shown);
    // The label truncates and the value keeps its width -- the Library band's
    // rule: the value is the state and the label is what it names.
    const int labelMaxW = rightEdge - kMargin - (valueW > 0 ? valueW + kSettingsLabelGap : 0);
    drawTextElided(fb, lf, kMargin, baselineIn(lf, y, kTypoRowH), row.label, labelMaxW, ink,
                   {}, plane);
    if (valueW > 0)
      drawText(fb, value, rightEdge - valueW, baselineIn(value, y, kTypoRowH), shown, ink,
               {}, plane);

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

**The `‹` and `›` bytes:** `\xE2\x80\xB9` and `\xE2\x80\xBA`. Both escapes are
followed by a character that is not a hex digit (`"` closes, and a space follows),
so neither runs on. **Check this again if you reflow those lines.**

`kTypoChevronGapSpaces` above is unused by this implementation — the board's
literal spaces are used instead. **Delete the constant**; an unused constant is a
claim about a mechanism that does not exist.

- [ ] **Step 5: Build**

```bash
make test 2>&1 | tail -20
```
Expected: compiles; goldens unmoved (nothing else calls this).

- [ ] **Step 6: Commit**

```bash
git add core/include/reader/theme.h core/include/reader/theme_quiet.h \
        core/src/theme_quiet.cpp core/include/reader/screen_typography.h \
        core/src/screen_typography.cpp
git commit -m "theme: renderTypography, one method for both of its boards

vm.editingRow is the whole difference between Typography.dc.html and
TypographyEditing.dc.html, so a second render method would be two spellings
of one layout.

kTypoRowH is 50 and NOT kSettingsRowH's 54. Two boards, two numbers: pinning
one screen's box to another's is the class of defect this project has three
records of, each costing a compounding pixel.

The preview box's height is DERIVED from the panel less every fixed run --
band, label, rows, footnote, hint bar -- because a literal would be right on
one geometry. It is fixed with respect to the SETTINGS, which is what keeps
the five rows still while the type changes.

The chevrons are U+2039/U+203A, already in fontc.py's subset, drawn inside the
value's own run so one measure centres the value between them. Only on the
edited row, and only where a step exists.

Co-authored-by: Claude <claude@anthropic.com>"
```

---

### Task 16: The simulator reaches it by pressing

**Files:**
- Modify: `sim/main.cpp`
- Modify: `CMakeLists.txt` — two `add_test` smoke entries

**Reached by pressing, not by assignment**, which is the rule every other state
board here follows: the render then pins the NAVIGATION too, and getting it wrong
shows up immediately as the wrong screen in the contact sheet.

The route is `reader_menu`'s, plus two presses:

| subcommand | presses after the reader menu opens |
|---|---|
| `typography` | `DOWN` (Contents → Typography), `CONFIRM` |
| `typography_editing` | the above, then `DOWN` (Font → Size), `CONFIRM` |

**This makes the simulator a check on Task 20 as well** — the menu's Typography
row has to be live or the `CONFIRM` does nothing and the sheet shows a reader menu.

- [ ] **Step 1: Add the flags**

Beside `isReaderMenu`, and add both to the `if (!isHome && ...)` rejection list
and to its `fprintf` message. **Both lists** — the message is what a typo reads as.

```cpp
  // design/Typography.dc.html and design/TypographyEditing.dc.html. The SAME
  // journey as `reader_menu` plus the presses that open the panel, so these
  // render the navigation as well as the screen -- and they are what would catch
  // the menu's Typography row going inert again.
  const bool isTypography = std::strcmp(argv[1], "typography") == 0;
  const bool isTypographyEditing = std::strcmp(argv[1], "typography_editing") == 0;
```

- [ ] **Step 2: Extend the body-face condition**

Both need the body face for the preview:

```cpp
  if (isReader || isReaderMenu || isChapterOpen || isReaderList || isAnchored ||
      isTypography || isTypographyEditing) {
```

- [ ] **Step 3: Extend the reader-menu branch**

The existing `if (isReaderMenu)` block builds the App and pushes the menu. Change
its condition to `if (isReaderMenu || isTypography || isTypographyEditing)` and,
after the menu is pushed, add:

```cpp
    // THE BOARD'S OWN VALUES, not a fresh device's -- design/Typography.dc.html
    // states `18 PT`, which is ppem 38. Same call primeForJourney makes for
    // Settings, and for the same reason: the board draws a configured state.
    if (isTypography || isTypographyEditing) {
      reader::Settings shown;
      shown.bodyPpem = 38;
      factory.setSettings(shown);
      // Contents is row 0 and Typography row 1.
      app.dispatch({reader::Button::Down, reader::PressKind::Short});
      app.dispatch({reader::Button::Confirm, reader::PressKind::Short});
      if (app.top().id() != reader::ScreenId::Typography) {
        std::fprintf(stderr, "the reader menu did not open Typography\n");
        return 1;
      }
      if (isTypographyEditing) {
        // Font is row 0 and Size row 1; the second Confirm enters edit mode.
        app.dispatch({reader::Button::Down, reader::PressKind::Short});
        app.dispatch({reader::Button::Confirm, reader::PressKind::Short});
      }
    }
```

**The `setSettings` call must come BEFORE the push**, because the factory hands the
screen its starting values at construction.

**And the body face must be at ppem 38 too**, or the preview is drawn at 32 while
the row says 18 PT — a screen disagreeing with itself. Re-init it beside the
settings:

```cpp
      // The preview shows the SIZE, so the face has to be at it. The shell does
      // this in its sink; here it is one call because nothing else holds a page.
      if (!body.init(bodyTtf.data(), bodyTtf.size(), 38)) {
        std::fprintf(stderr, "body face failed to re-init at ppem 38\n");
        return 1;
      }
```

**Careful:** re-initing `body` after `readerMetrics` was computed from it leaves
`m` stale for the Reader underneath — harmless here because Typography is not an
overlay and the Reader is never drawn, which is exactly the property the shell
relies on. Say so in a comment.

- [ ] **Step 4: Register the smoke tests**

`CMakeLists.txt`, beside the others:

```cmake
# The Typography panel, reached the way the device reaches it: Reader -> menu ->
# Typography. A smoke test on the navigation as much as on the render -- the
# menu's row has to be live for this to arrive anywhere.
add_test(NAME sim_typography COMMAND reader_sim typography
         ${CMAKE_BINARY_DIR}/sim_typography.png --canvas 480x800)
# ...and its edit state, two presses further in.
add_test(NAME sim_typography_editing COMMAND reader_sim typography_editing
         ${CMAKE_BINARY_DIR}/sim_typography_editing.png --canvas 528x792)
```

- [ ] **Step 5: Run**

```bash
make test 2>&1 | tail -20
./build/reader_sim typography build/typo.png --canvas 480x800
./build/reader_sim typography_editing build/typo_edit.png --canvas 480x800
```
Expected: both exit 0 and write a PNG.

- [ ] **Step 6: Look at them**

Open both PNGs. Say what you see. Specifically: is the preview set at the LARGER
size (38, not 32)? Do the chevrons appear on `Size` in the editing render and
nowhere in the other? Is the footnote clear of the hint bar?

- [ ] **Step 7: Commit**

```bash
git add sim/main.cpp CMakeLists.txt
git commit -m "sim: typography and typography_editing, reached by pressing

The reader_menu journey plus the presses that open the panel, so these pin
the NAVIGATION as well as the render -- which makes them the check that would
catch the menu's Typography row going inert again.

The body face is re-inited to ppem 38 beside the settings, because the board
states 18 PT and a preview drawn at 32 under a row reading 18 PT is a screen
disagreeing with itself. Re-initing after readerMetrics leaves the Reader's
metrics stale, which is harmless for exactly the reason the shell's apply
path relies on: Typography is not an overlay, so the page is never drawn
while it stands.

Co-authored-by: Claude <claude@anthropic.com>"
```

---

### Task 17: Goldens, both states, both geometries

**Files:**
- Modify: `test/unit/test_screens_golden.cpp`
- Create: `test/golden/typography.png`, `typography_x3.png`,
  `typography_editing.png`, `typography_editing_x3.png`

- [ ] **Step 1: Write the golden tests**

Append to `test/unit/test_screens_golden.cpp`, mirroring the reader-menu case's
structure (it already builds the Reader-rooted App with a body and italic face):

```cpp
// --- The Typography panel, and its edit state ---------------------------------
//
// design/Typography.dc.html and design/TypographyEditing.dc.html.
//
// REACHED BY PRESSING, through the reader menu, exactly as the simulator reaches
// it -- so these are a check on the menu's row being live as well as on the
// render. Two states of one screen, which is why they share a lambda: a second
// copy would be the place they drift apart.
TEST_CASE("QuietTheme renders the Typography panel to golden, reached by pressing") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  // ppem 38 IS the board's `18 PT`, and the face has to be at it or the preview
  // disagrees with the row above it.
  Body body(38);
  Italic italic(38);

  auto renderOne = [&](int w, int h, bool editing, const std::string& name) {
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
    if (editing) {
      // Font is row 0, Size row 1; the Confirm enters edit mode on it.
      app.dispatch({reader::Button::Down, reader::PressKind::Short});
      app.dispatch({reader::Button::Confirm, reader::PressKind::Short});
    }
    reader::Framebuffer fb(w, h);
    app.render(fb, ramp.fonts, theme, reader::Plane::Bw);
    golden::checkGolden(fb, name);
  };

  SUBCASE("browse, X4 480x800") { renderOne(480, 800, false, "typography"); }
  SUBCASE("browse, X3 528x792") { renderOne(528, 792, false, "typography_x3"); }
  SUBCASE("edit, X4 480x800") { renderOne(480, 800, true, "typography_editing"); }
  SUBCASE("edit, X3 528x792") { renderOne(528, 792, true, "typography_editing_x3"); }
}

TEST_CASE("the Typography panel declares the fidelity its goldens are drawn at") {
  // ASSERTED BEFORE THE PLANE IS NAMED, the way Home's goldens assert Mono: a
  // change to the shipped path must FAIL a test rather than leave four goldens
  // quietly pinning a path nothing paints.
  reader::TypographyScreen scr(reader::Settings{}, nullptr, "Middlemarch", nullptr);
  CHECK(scr.fidelity() == reader::Fidelity::Mono);
}
```

- [ ] **Step 2: Run — the goldens are missing, so they fail and write candidates**

```bash
make test 2>&1 | grep -A 3 "golden missing" | head -20
```
Expected: four `golden missing - inspect build/<name>_candidate.png` failures.

- [ ] **Step 3: LOOK AT THE FOUR CANDIDATES AND SAY WHAT YOU SEE**

This is the review, not a formality. An icon has passed review twice in this repo
while reading as the letters "OC". For each of the four, write down:

- Does the preview box hold whole lines with no clipped half-line at the bottom?
- Is the specimen visibly LARGER than the reader's default (38 against 32)?
- `typography`: no chevrons anywhere; `Font` is the inverted row.
- `typography_editing`: `Size` is inverted and reads `‹ 18 PT ›` with real
  chevron glyphs — **not notdef boxes**. A notdef box inks rows exactly like a
  letter, so look at the shape.
- Is the footnote two lines, clear of both the last row and the hint bar?
- Confirm hint reads `EDIT` on the browse renders and `OK` on the edit ones.

Compare each against its board (`build/overlay` from Task 5).

- [ ] **Step 4: Bless them**

```bash
cp build/typography_candidate.png test/golden/typography.png
cp build/typography_x3_candidate.png test/golden/typography_x3.png
cp build/typography_editing_candidate.png test/golden/typography_editing.png
cp build/typography_editing_x3_candidate.png test/golden/typography_editing_x3.png
make test 2>&1 | tail -8
```
Expected: all pass.

- [ ] **Step 5: Prove they bite, by mutation**

Each must fail for a reason it is supposed to catch. Run these ONE AT A TIME,
`touch`ing the file after each edit AND after restoring it — a `cp` plus a compile
inside the same second leaves make thinking the object is current, which has
fooled this repo already:

| mutation | expected failures |
|---|---|
| `chevrons` forced to `false` in `renderTypography` | exactly 2 (`typography_editing`, `_x3`) and NOT the browse pair |
| `chevrons` forced to `true` | exactly 2 (the browse pair) |
| `kTypoRowH` 50 → 54 | all 4 |
| the footnote's `footTop` computed from `y` instead of the panel bottom | all 4 |
| `clampProse` call deleted | all 4, or 0 if the box already fits — **if 0, the box is oversized for the specimen and the fixed height is wrong** |

Record the counts. **A mutation that fails 0 tests means the mutation did not
land or the goldens are blind** — check `git diff` against the binary's behaviour
before believing either.

- [ ] **Step 6: Commit**

```bash
git add test/unit/test_screens_golden.cpp test/golden/typography*.png
git commit -m "golden: the Typography panel, both states, both geometries

Reached by pressing through the reader menu, so these pin the navigation and
the render together -- and they are what would catch the menu's row going
inert.

The fidelity is asserted before the plane is named, the way Home's goldens
assert Mono: a change to the shipped path should fail a test rather than
leave four goldens pinning a path nothing paints.

Proved by mutation, one at a time, with the counts recorded: forcing the
chevrons off fails exactly the two edit-state goldens and forcing them on
fails exactly the two browse ones, which is the pair of failures that says
these goldens can tell the two states apart.

Co-authored-by: Claude <claude@anthropic.com>"
```

---

### Task 18: make compare, and read the percentage

**Files:** none

- [ ] **Step 1: Compare the three affected boards**

```bash
make compare COMPARE_ARGS="--only typography,typography_editing,settings"
```

- [ ] **Step 2: Read the PERCENTAGE, not the word**

`ok` means the simulator produced a frame, not that the frame matches. A merge
once changed `ReaderMenu.dc.html` under its screen and the sheet said `ok` the
whole time, at 13.02% against 3.02%.

Expected: both typography boards now report a percentage. This project averages
~5.4%/6.4% on 1-bit chrome screens; the overlay-free full screens do better
(`home_empty` 1.34%/1.23%, `sleep_idle` 0.27%).

**If either is above ~8%, stop and find the structural mismatch** before
proceeding. The likely candidates, in order: the preview box's derived height
disagreeing with the board's 292px; the footnote wrapping to three firmware lines
where Chrome takes two (SdMissing's `max-width` needed 400 → 420 for exactly
this — the firmware's whole-pixel advances measure ~3% wider); `kTypoRowH`.

- [ ] **Step 3: Record the numbers in the commit, and fix the board if the board is wrong**

If the footnote wraps to three lines in the firmware and two in Chrome, **the
board is what changes** — widen its measure or shorten the copy — because a UI
change goes into the design HTML first, including when the board is what is
wrong. Then re-render, re-bless the four goldens, and note the re-bless.

- [ ] **Step 4: Commit whatever moved (possibly nothing)**

```bash
git add -A && git commit -m "design: reconcile Typography against the firmware's own metrics

<state the two percentages, and which side moved and why>

Co-authored-by: Claude <claude@anthropic.com>"
```

---

# PHASE 6 — SETTINGS STOPS SHOWING PLACEHOLDERS

### Task 19: The five TYPOGRAPHY rows read the real settings

**Files:**
- Modify: `core/include/reader/screen_settings.h` — the `Field` enum and its docs
- Modify: `core/src/screen_settings.cpp` — the table and `syncVm`
- Test: `test/unit/test_screen_settings.cpp`

**They stay UNFOCUSABLE.** Settings is not an entry point to the panel; the rows
are a readout. What changes is that they read `settings_` instead of a placeholder
string, because a placeholder is right only while nothing exists behind the row —
and once a setting exists, a placeholder is a screen displaying a stale number.

`Sleep screen` keeps its placeholder: nothing exists behind it.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("Settings' typography rows read the settings, not placeholders") {
  reader::Settings s;
  s.bodyPpem = 46;
  s.margins = 10;
  s.lineSpacing = 1400;
  s.justify = false;
  reader::SettingsScreen scr(s, nullptr);
  scr.setMetrics(2000, 54, 40);  // a window tall enough for every row

  const reader::SettingsViewModel& vm = scr.vm();
  REQUIRE(vm.rows.size() >= 6);
  CHECK(vm.rows[0].label == "TYPOGRAPHY");
  CHECK(vm.rows[1].value == "LITERATA");
  CHECK(vm.rows[2].value == "22 PT");       // 46 * 72 / 150 = 22.08
  CHECK(vm.rows[3].value == "TIGHT");
  CHECK(vm.rows[4].value == "1.4");
  CHECK(vm.rows[5].value == "RAGGED");
}

TEST_CASE("Settings' typography rows are still unreachable") {
  // A READOUT, not an entry point. The panel is reached from the reader's menu,
  // where the band can name the book and the preview means something.
  reader::SettingsScreen scr(reader::Settings{}, nullptr);
  scr.setMetrics(2000, 54, 40);
  for (const reader::SettingsRow& r : scr.vm().rows) {
    if (r.label == "Font" || r.label == "Size" || r.label == "Margins" ||
        r.label == "Line spacing" || r.label == "Alignment")
      CHECK_FALSE(r.focusable);
  }
  // ...and the focus still starts on the first DEVICE row.
  CHECK(scr.vm().rows[static_cast<size_t>(scr.vm().focusedRow)].label == "Sleep after");
}

TEST_CASE("Settings and Typography spell a value the same way") {
  // ONE FACT, ONE SPELLING. Two screens show these five values and a second
  // formatter would be the place they drift -- which is the whole reason
  // typographyValueFor exists rather than each screen having a switch.
  reader::Settings s;
  s.bodyPpem = 38;
  s.margins = 30;
  s.lineSpacing = 1850;
  s.justify = true;
  reader::SettingsScreen a(s, nullptr);
  a.setMetrics(2000, 54, 40);
  reader::TypographyScreen b(s, nullptr, "Middlemarch", nullptr);
  CHECK(a.vm().rows[2].value == b.vm().rows[1].value);  // Size
  CHECK(a.vm().rows[3].value == b.vm().rows[2].value);  // Margins
  CHECK(a.vm().rows[4].value == b.vm().rows[3].value);  // Line spacing
  CHECK(a.vm().rows[5].value == b.vm().rows[4].value);  // Alignment
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
make test 2>&1 | tail -15
```
Expected: FAIL — `rows[2].value` is `"18 PT"`, the board placeholder.

- [ ] **Step 3: Extract the formatter, then use it from both screens**

**The third test above will not pass without an extraction**, and that is
deliberate: two screens now show these five values, and the second copy is the
extraction point rather than the fifth.

Move `sizeLabel`, `leadLabel`, `kMarginLabels` and the margin lookup out of
`screen_typography.cpp`'s anonymous namespace into a shared home. Put them in
**`core/include/reader/settings.h`** beside the step tables they index — the
tables are already public there for exactly this reason, and a new header for four
formatters would be a file with no other purpose:

```cpp
// HOW A TYPOGRAPHY VALUE IS SPELLED, once. Two screens show these -- the
// Typography panel edits them and Settings reads them out -- and a second
// formatter is where the two would drift. Declared here beside the tables they
// index rather than in a header of their own, because these ARE those tables'
// labels.
//
// Sizes are POINTS, truncated: pt = ppem * 72 / 150, exactly as the chrome ramp
// is sized and as sleepLabel truncates minutes. The row describes a size the user
// is looking at, so rounding up would name a size the panel is not showing.
std::string typographySizeLabel(int ppem);
std::string typographyMarginLabel(int margins);
std::string typographyLeadLabel(int em1000);
std::string typographyAlignLabel(bool justify);
```

Define them in `core/src/settings.cpp` (which already owns the tables' snapping,
so the labels and the tables live in one translation unit and a step added without
a label fails the `static_assert` there rather than in a screen).

`settings.h` needs `#include <string>`.

Then have BOTH screens call them. `screen_typography.cpp` loses its local
`sizeLabel`/`leadLabel`/`kMarginLabels`; move that `static_assert` to
`settings.cpp` with the labels.

- [ ] **Step 4: Change the Settings table**

`screen_settings.cpp`'s `kItems` — the five rows get real `Field`s, and the enum
grows. **They stay unfocusable**, so `focusable()` can no longer be
`field != Field::None`:

```cpp
  // WHICH SETTING A ROW SHOWS, and separately whether it can be reached.
  //
  // The typography rows READ a real setting and are still not focusable: the
  // panel that edits them is the reader's (design/Typography.dc.html), where the
  // band can name the book and a preview means something. So `field` and
  // `reachable` are two facts now, where `field != None` used to serve as both --
  // and conflating them again would either make these rows editable here or send
  // them back to placeholders.
  enum class Field {
    None, SleepAfter, FullRefresh, OnTransition,
    Font, Size, Margins, LineSpacing, Alignment
  };

  struct Item {
    const char* label;
    Field field;
    bool isHeader;
    bool reachable;
    // What a row with no setting behind it shows. `Sleep screen` is the only one
    // left: covers are issue #11.
    const char* placeholder;
  };
```

The table:

```cpp
constexpr std::array<SettingsScreen::Item, 11> kItems{{
    {"TYPOGRAPHY", SettingsScreen::Field::None, true, false, ""},
    {"Font", SettingsScreen::Field::Font, false, false, ""},
    {"Size", SettingsScreen::Field::Size, false, false, ""},
    {"Margins", SettingsScreen::Field::Margins, false, false, ""},
    {"Line spacing", SettingsScreen::Field::LineSpacing, false, false, ""},
    {"Alignment", SettingsScreen::Field::Alignment, false, false, ""},
    {"DEVICE", SettingsScreen::Field::None, true, false, ""},
    {"Sleep after", SettingsScreen::Field::SleepAfter, false, true, ""},
    {"Full refresh", SettingsScreen::Field::FullRefresh, false, true, ""},
    {"Refresh on screen change", SettingsScreen::Field::OnTransition, false, true, ""},
    {"Sleep screen", SettingsScreen::Field::None, false, false, "BOOK COVER"},
}};
```

`focusable()` becomes `return !it.isHeader && it.reachable;`.

`syncVm`'s switch gains the five cases:

```cpp
        case Field::Font: row.value = "LITERATA"; break;
        case Field::Size: row.value = typographySizeLabel(settings_.bodyPpem); break;
        case Field::Margins: row.value = typographyMarginLabel(settings_.margins); break;
        case Field::LineSpacing:
          row.value = typographyLeadLabel(settings_.lineSpacing);
          break;
        case Field::Alignment:
          row.value = typographyAlignLabel(settings_.justify);
          break;
```

`cycleFocused`'s switch also needs the five cases — they are unreachable (the
rows cannot be focused), so they `return Action::none()` with a comment saying so,
exactly as `Field::None` already does. **Do not add a `default:`**; the whole
value of the switch is that `-Wswitch` names a field nobody handled.

- [ ] **Step 5: Run**

```bash
make test 2>&1 | tail -20
```
Expected: all pass. **The `settings` and `settings_x3` goldens WILL move** —
`18 PT` became `15 PT`, which is one glyph. Inspect the candidates and confirm
the change is confined to that row's value:

```bash
make test 2>&1 | grep -A 6 "settings"
```

Then bless, and state in the commit that the diff is confined to the `Size` row.

- [ ] **Step 6: Commit**

```bash
git add core/include/reader/settings.h core/src/settings.cpp \
        core/include/reader/screen_settings.h core/src/screen_settings.cpp \
        core/include/reader/screen_typography.h core/src/screen_typography.cpp \
        test/unit/test_screen_settings.cpp test/golden/settings*.png
git commit -m "settings: the typography rows read the setting, and stay unreachable

A placeholder is right only while nothing exists behind the row. Once the
setting exists a placeholder is a screen showing a stale number, and this one
was stale by 3 PT.

`field != None` used to mean both 'has a setting' and 'can be focused'. Those
are two facts now: these rows have a setting and are still not focusable,
because the panel that edits them is the reader's, where the band can name the
book. Conflating them again would either make them editable here or send them
back to placeholders.

The value formatters moved to settings.h beside the tables they index -- two
screens show these five values, so the SECOND copy is the extraction point,
and a test asserts the two screens spell them identically rather than
trusting it.

The settings goldens moved by one glyph, confined to the Size row's value.

Co-authored-by: Claude <claude@anthropic.com>"
```

---

# PHASE 7 — WIRING

### Task 20: The reader menu's Typography row goes live

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

`TypographyScreen::onGesture` already answers `Action::popTo(ScreenId::Reader)`
from Task 11, and `popTo` stops at the root when its target is not on the stack
(`app.h:70`), so there is nothing to change in `core/` here. This task is the
shell's half.

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

**`App::at` returns a const reference**, so this needs a non-const accessor rather
than a `const_cast`. Add `Screen& atMut(int)` to `App` — or better, reuse whatever
the existing `gPendingSpine` path uses to get a mutable `ReaderScreen*` (it uses
`&gApp->top()` after the pop, which is non-const). **Read `app.h` and pick the
existing mechanism; do not add a `const_cast`.**

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
    if (gTypographyDirty && gApp->top().id() == reader::ScreenId::Reader) {
      gTypographyDirty = false;
      auto* rd = static_cast<reader::ReaderScreen*>(&gApp->top());
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

- **The Typography panel is two modes, and there is no cancel.** Every step
  commits, so Back means one thing in both modes. The second-door reasoning.
- **The values WRAP, and this screen is where that is free** — the hazard is
  auto-repeat, and this screen declares none. `setWrapping(false)` still has no
  caller.
- **Both chevrons or neither**, computed from the value count, which is what keeps
  the one-value `Font` row honest.
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
| **The chevrons at 12pt on 220 PPI** | U+2039 is a thin diagonal, and this project records diagonals as where a mark reads too faint under hard 1-bit thresholding (`kChevron` came out a notch lighter). | Look at the edit state. If they read faint, the fix is the board first. |
| **A read-only card** | `writeAll` calls `noteCardGone()` on a write that fails after opening, which `pollCardPresence` turns into the SD-missing screen. A refused typography save must NOT throw the reader out of a book. | Tab the card write-protect, change a size, confirm the value shows and the reader survives. |

- [ ] **Step 5: STOP.** Report what was built, what is on the card, and the list
  above. Do not claim the feature works on the device.

---

## Self-review notes, and two corrections to this plan

Run after writing, kept here because they are corrections a reader needs:

**1. Signatures are consistent as written.** `TypographyScreen`'s constructor
takes four arguments from Task 11 onward; `Theme::renderTypography` takes the body
face as a POINTER with null supported, as `renderReader` takes its italic;
`Action`'s target field is `target`; `Back` answers `popTo(ScreenId::Reader)` from
Task 11 and Task 23 changes nothing in `core/`. The preview height is
`typographyPreviewBoxH`, a file-local helper in `theme_quiet.cpp` — **not** on
`Theme`, because no screen ever asks how tall the box is.

**2. Spec coverage.** Every section of the spec maps to a task:

| spec section | task |
|---|---|
| Where the state lives | 6, 7 |
| The screen / two modes / no cancel | 11, 12 |
| Both chevrons or neither | 12, 15 |
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
