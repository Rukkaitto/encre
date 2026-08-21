# Phase 2C-2 — Library and its Flows Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Browse the books actually on the card, hold Confirm on one to get its
actions, look at its details, and delete it — with a confirmation that never
touches reading progress.

**Architecture:** Four screens, three of which are **overlays** — the first in
this firmware, and the reason the App has to learn to render more than the top of
its stack. A `BookList` model in `core/` turns a directory listing into rows. The
`Screen` focus accessors deferred from 2C-1 finally earn themselves, because a
Library scroll position is worth restoring across a wake.

**Tech Stack:** C++20, doctest, CMake (desktop) / PlatformIO + Arduino-ESP32.

---

## Scope, and what is deliberately not in it

**In:** the Library screen over real files, item actions, delete confirm, book
details, scrolling, and the session's focus field.

**Out, and each for a stated reason:**

- **Book titles and authors from EPUB metadata.** That needs the zip container and
  the OPF parser, which are Phase 3's first two tasks. Until then a row's title is
  derived from its **filename** and the author line is empty.
- **Cover thumbnails.** Same reason — the cover is an image inside the EPUB. Rows
  draw the dithered placeholder Home already uses.
- **Real reading progress.** Per-book state (`/.reader/state/`) only has anything
  to record once the Reader exists, so every book reads `NEW`.
- **Settings, Sleep, Home's empty and missing variants** — 2C-3.

### How the boards stay honest despite that

Library's board shows `Middlemarch / George Eliot / 6%`. The device will show
filenames and `NEW`, so a naive reading says the screen "does not match its
board". It does, the same way Home already does: **the view-model carries the
fields, and the simulator feeds it the board's own sample content** while the
device fills the same fields from the filesystem. `make compare` and the goldens
therefore keep testing the rendering, which is what they are for, and Phase 3
fills in the data without touching the theme.

If you find yourself tempted to drop `author` or `percent` from the view-model
because nothing populates them yet — don't. The board draws them, the golden
pins them, and the field is where Phase 3 plugs in.

## Design decisions this plan locks in

### 1. Overlays: the App renders a stack, not a screen

`design/LibraryActions.dc.html` puts a centred panel over a **still-visible,
veiled Library**. `DeleteConfirm` and `BookDetails` are the same shape. So
rendering the top screen is no longer enough.

Add `Screen::isOverlay()`, defaulting false. To paint, the App walks down from the
top to the first non-overlay screen, renders that, then renders each overlay above
it in order. An overlay draws its own veil before its panel.

Two things this must get right:

- **Input still goes only to the top screen.** An overlay that let the screen
  underneath see events would move a focus the user cannot see.
- **`Fidelity` and the long-press mask come from the top screen**, as they already
  do. An overlay over a `Dithered` screen is still one paint.

### 2. The veil is its own primitive, from the board's own numbers

The board says:

```css
.dim-veil { background-image: radial-gradient(circle, #ffffff 1.3px, transparent 1.5px);
            background-size: 3px 3px; }
```

A **white** dot ~2.6px across on a **3px** grid. `ditherRect` sets **black** on a
**4px** grid. They are not the same thing and one cannot serve for the other: a
4px grid veils about half as densely, which reads as a screen that is merely
smudged rather than deliberately behind something.

So add `veilRect(fb, x, y, w, h)` beside `ditherRect`, with a 3×3 clustered
pattern that sets pixels **white**. Follow `dither.cpp`'s existing reasoning about
clustered versus dispersed — the board draws one round dot per cell, so the
pattern must grow as a dot, not scatter. And key it on absolute framebuffer
coordinates, as `ditherRect` does, so two veiled regions share one grid.

### 3. A list longer than the screen needs a window, derived not pinned

Home's menu is three rows. `/books` can hold hundreds. Library needs a scroll
window: a first-visible index plus however many rows fit.

**How many fit is computed**, from the panel height minus the header band and the
hint bar, divided by `kRowH`. Not a constant: the two geometries differ by 8px of
height, the header band's height depends on its type role, and this project has
three separate defects from pinning a number the board computes.

Rules worth pinning in tests: focus never leaves the visible window; moving focus
past the bottom scrolls by one, not by a page; the window never scrolls past the
end; a list shorter than the window does not scroll at all; and an empty
directory is a valid state, not a crash.

### 4. Filename to title, and what "a book" is

`/books` may hold anything. A row is a book if its extension is `.epub` or `.txt`
(case-insensitive — FAT is case-preserving but not case-sensitive, and a card
written on a Mac will have `.EPUB` somewhere eventually). Folders are always rows.
Everything else is skipped.

The display title is the filename with its extension removed. Do **not** attempt
to prettify further — no underscore-to-space, no title-casing. It is a placeholder
until Phase 3 reads the real title, and a clever transform would make a wrong
title look deliberate.

### 5. Delete removes the book and nothing else

Spec §4.0: *"delete has a confirmation step and never erases reading progress."*
So `DeleteConfirm` removes the file via `FileSystem::remove` and leaves
`/.reader/state/` alone. That is not laziness — a book that comes back (a card
edited on a computer, a re-copied file) should still know where you were.

The confirm screen must state what it is deleting. A confirmation that does not
name the thing is a confirmation people learn to dismiss.

### 6. The `Screen` focus accessors, finally

2C-1 wrote `Session::focus = 0` always, because `Screen` had no way to report a
focus and adding virtuals to carry a value nothing could produce was speculative.
Library produces one. So add the pair — read the focus, restore it — and wire
`saveWhereWeAre` to the real value.

Watch the thing 2C-1's review flagged: `saveWhereWeAre` compares **screen only**
when deciding whether the record changed, so a focus-only move would not be
stored. Extend the comparison to the pair at the same time, or the field goes
back to being decorative.

## File structure

**New in `core/`:**

| File | Responsibility |
|---|---|
| `core/include/reader/booklist.h` + `core/src/booklist.cpp` | `BookEntry`, `BookList`: a directory listing turned into rows. Filtering, sorting, filename-to-title. |
| `core/include/reader/scrollwindow.h` + `core/src/scrollwindow.cpp` | `ScrollWindow`: focus plus first-visible, and the movement rules. Reusable — Settings and Bookmarks are lists too. |
| `core/include/reader/screen_library.h` + `.cpp` | `LibraryScreen`. |
| `core/include/reader/screen_item_actions.h` + `.cpp` | `ItemActionsScreen` — the overlay. |
| `core/include/reader/screen_delete_confirm.h` + `.cpp` | `DeleteConfirmScreen` — overlay. |
| `core/include/reader/screen_book_details.h` + `.cpp` | `BookDetailsScreen` — overlay. |

**Modified:** `viewmodel.h`, `theme.h` / `theme_quiet.{h,cpp}` (four render
methods), `app.h` / `app.cpp` (`isOverlay`, stacked render, focus accessors),
`dither.h` / `dither.cpp` (`veilRect`), `screens.h` / `screens.cpp`, `sim/main.cpp`,
`shell/src/main.cpp`, `shell/src/session.cpp`.

**Goldens:** `library`, `library_actions`, `delete_confirm`, `book_details`, each
at 480×800 and 528×792 — eight new files.

> **CMake uses `file(GLOB ...)`.** Re-run `cmake -S . -B build` after adding a
> source file; `make test` does it.

---

## Task 1: `ScrollWindow`

**Files:** create `core/include/reader/scrollwindow.h`, `core/src/scrollwindow.cpp`;
test `test/unit/test_scrollwindow.cpp`.

Pure logic, no rendering, so it is all unit-testable. Build it first and the
Library screen becomes mostly presentation.

API shape: constructed with (item count, visible rows); `focus()`,
`firstVisible()`, `moveFocus(delta)`, `setCount(n)`, `setVisibleRows(n)`.
`moveFocus` returns whether anything changed, so a screen can return
`Action::none()` at the end of a list rather than paying a refresh that changes
nothing — the pattern `HomeScreen::moveFocus` already uses.

- [ ] **Step 1: Write the tests first.** Cover: focus starts at 0; focus clamps at
  both ends and reports no change; moving past the bottom scrolls by exactly one;
  moving above the top scrolls by one; the window never shows past the last item;
  a count smaller than the window never scrolls; `setCount` smaller than the
  current focus pulls focus back into range; count 0 gives focus -1 (no
  selection) and a window of nothing; `setVisibleRows` re-clamps; and a
  `visibleRows` of 0 or negative is inert rather than dividing by zero.
- [ ] **Step 2: Run, confirm failures are link errors, implement, confirm passing.**
- [ ] **Step 3: Commit** — `feat(core): a scroll window for lists longer than the screen`

---

## Task 2: `BookList`

**Files:** create `core/include/reader/booklist.h`, `core/src/booklist.cpp`;
test `test/unit/test_booklist.cpp` (against `FakeFileSystem`).

`BookEntry { std::string name; std::string title; bool isDir; uint32_t size; }`,
and a `scan(FileSystem&, path, std::vector<BookEntry>&)`.

- [ ] **Step 1: Write the tests first**, against the fake:
  - `.epub` and `.txt` are included; `.EPUB` and `.Txt` too (case-insensitive)
  - other extensions and extensionless files are skipped
  - directories are always included regardless of name
  - **folders sort before books**, then each group alphabetically,
    case-insensitively — a listing whose order depends on the filesystem is a
    listing that looks different on two cards with the same books
  - the title is the filename minus its final extension; a name with dots inside
    keeps them (`Vol.2.epub` → `Vol.2`); a dotfile (`.hidden.epub`) is skipped
  - an empty directory yields an empty list and **true**, not false — no books is
    a valid state, and the caller distinguishes it from a read failure
  - a path that is not a directory, or a filesystem that is not mounted, yields
    false
  - `scan` clears its output first, so a rescan does not accumulate
- [ ] **Step 2: Run, confirm failure, implement, confirm passing.**
- [ ] **Step 3: Commit** — `feat(core): turn a directory listing into library rows`

---

## Task 3: `veilRect`, and overlay rendering in the App

**Files:** modify `core/include/reader/dither.h`, `core/src/dither.cpp`,
`core/include/reader/app.h`, `core/src/app.cpp`; test `test/unit/test_dither.cpp`,
`test/unit/test_app.cpp`.

- [ ] **Step 1: `veilRect`, from the board's numbers.** White dot, **3px** grid.
  Read `dither.cpp`'s existing comment on clustered-versus-dispersed before
  writing the pattern — the same reasoning applies and the same trap is available.
  Test: a veiled black region keeps roughly 1 − (dot area / 9) of its ink and the
  pattern is continuous across two adjoining calls; veiling white leaves white;
  the grid is keyed on absolute coordinates.
- [ ] **Step 2: `Screen::isOverlay()`**, default false, and the App's render walks
  from the topmost non-overlay upward. Tests: a non-overlay renders alone; one
  overlay renders the screen beneath then itself; two stacked overlays render
  three screens in order; an overlay at the *root* renders only itself (it should
  not happen, but it must not read off the end of the stack).
- [ ] **Step 3: Input and fidelity still come from the top only.** Pin it: with an
  overlay on top, an event reaches the overlay and **not** the screen beneath.
- [ ] **Step 4: Commit** — `feat(core): overlays render the screen beneath them, veiled`

---

## Task 4: The Library screen

**Files:** create `screen_library.{h,cpp}`; modify `viewmodel.h`, `theme.h`,
`theme_quiet.{h,cpp}`; test `test/unit/test_screen_library.cpp`, goldens
`library.png` / `library_x3.png`.

- [ ] **Step 1: Read `design/Library.dc.html`.** It is the authority. Note the
  header band (`LIBRARY` + a count), the folder row (folder mark, name, `FOLDER ·
  N BOOKS`, chevron), the book row (dithered cover placeholder, title, author,
  and a right-hand value that is a percentage, `DONE`, or `NEW`), and the hint bar
  — whose Confirm slot **carries the hold ring**, because long-press opens the
  actions overlay. If the board is wrong, fix the board first.
- [ ] **Step 2: `LibraryViewModel` and `renderLibrary`**, built from existing
  primitives. `drawRow` may not express a two-line row with a leading thumbnail —
  **extend the primitive rather than special-casing the screen.** Say in your
  report what you extended.
- [ ] **Step 3: The screen.** `ScrollWindow` for movement; Confirm on a folder
  descends, on a book returns `Push`/an open action (the Reader is Phase 3, so
  opening a book logs and does nothing yet — say so in a comment); Back ascends
  out of a folder or pops to Home at the top; **long-press Confirm pushes the
  actions overlay**, bound through `hintHoldMask(vm.holds)` so the ring and the
  binding stay one declaration.
- [ ] **Step 4: Goldens at both geometries**, via the shipping 1-bit path. Render
  the candidates, **open them with the Read tool**, walk them against the board,
  and report honestly what you see before blessing — including anything that looks
  wrong that you bless anyway. Check the recurring defects: `baselineIn` for text,
  `iconTopIn` for marks, no pinned height the board computes, correct role *and*
  weight per run, tracking in em, no grey on a rule or fill, and nothing
  overflowing at 480 wide.
- [ ] **Step 5: Commit** — `feat(screens): the Library over real files`

---

## Task 5: The three overlays

**Files:** create `screen_item_actions.{h,cpp}`, `screen_delete_confirm.{h,cpp}`,
`screen_book_details.{h,cpp}`; modify `viewmodel.h`, `theme.h`,
`theme_quiet.{h,cpp}`; tests and six goldens.

Boards: `design/LibraryActions.dc.html`, `DeleteConfirm.dc.html`,
`BookDetails.dc.html`. All three are centred panels over a veiled parent
(spec §4.1c: overlays are vertically centred, not offset from the top).

From the actions board, measured — do not re-derive by eye: the panel is
`left: 70px; width: 340px` on the 480 canvas (so **centred**, and it must centre
on 528 too), `border: 2px`, its header row is `padding: 21px 20px` with a 2px
bottom border, and each action row is `height: 72px; padding: 0 20px`. The focused
action is inverted, and `Open` / `Book details` carry chevrons while
`Mark as finished` / `Delete…` do not.

- [ ] **Step 1: The actions overlay**, with its four rows. `Delete…` pushes the
  confirm overlay; `Book details` pushes details; `Open` is Phase 3's; `Mark as
  finished` writes per-book state, which does not exist yet — **have it do nothing
  and say so**, rather than inventing a state file 2C-3 and Phase 3 will have to
  agree with.
- [ ] **Step 2: Delete confirm**, which names the book and, on confirm, removes the
  file through `FileSystem::remove`, pops both overlays, and rescans the list.
  **It must not touch `/.reader/state/`** (spec §4.0). Test the rescan: deleting
  the last item must pull focus back into range rather than leaving it past the
  end.
- [ ] **Step 3: Book details.** Read the board for which fields it shows; the ones
  that need EPUB metadata are blank until Phase 3, and the view-model carries them
  regardless.
- [ ] **Step 4: Six goldens**, inspected and reported as in Task 4.
- [ ] **Step 5: Commit** — one commit per screen, not one for all three.

---

## Task 6: Focus in the session, and the shell

**Files:** modify `core/include/reader/app.h`, `core/src/app.cpp`,
`screen_library.*`, `shell/src/main.cpp`, `shell/src/session.cpp`,
`core/src/screens.cpp`, `sim/main.cpp`.

- [ ] **Step 1: `Screen::focus()` / `Screen::setFocus(int)`**, default no-op /
  returning 0, overridden by `LibraryScreen`. This is the pair 2C-1 deliberately
  deferred; it is justified now because Library can produce a value.
- [ ] **Step 2: Wire it into the session** — and extend `saveWhereWeAre`'s
  changed-record comparison to the **(screen, focus) pair**, or a focus-only move
  is never stored and the field stays decorative.
- [ ] **Step 3: The shell's catalogue** builds `LibraryScreen` over the real
  `SdFileSystem`, rooted at `/books`. Home's `LIBRARY` row shows the real count.
  A missing `/books` is not an error — offer to create it, or show an empty
  library; **decide, and say which in a comment.**
- [ ] **Step 4: The simulator** gets Library over a `HostFileSystem` rooted at
  `--root`, and the board's sample content when no root is given, so the goldens
  keep pinning the rendering.
- [ ] **Step 5: Commit.**

---

## Task 7: Verify and document

- [ ] `make test`, `make sim`, `make firmware`, and
  `make compare COMPARE_ARGS="--all --only home,sd_missing,library,library_actions,delete_confirm,book_details"`
  — every one implemented and matching.
- [ ] The seven existing goldens unchanged; `text_sample.png` especially.
- [ ] `CLAUDE.md`: overlays render the stack and take input only at the top; the
  two dither patterns are now **three** (clustered black for tints, dispersed
  black for edges, clustered white for veils) and each names its board source;
  `ScrollWindow` is where list movement lives.
- [ ] Roadmap: 2C-2 done, and what 2C-3 inherits.
- [ ] **Do not write the flash handoff into the docs** — report it, and I will hand
  it to the user with what only the panel can answer.
