# FocusScreen Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make "a screen that reports a focus accepts one back" structural: one `FocusScreen` base class replaces the five per-screen copies of the focus→view-model mirror, and landing rules (Settings' skip-unfocusable) move into `Focus` where every other movement rule lives.

**Architecture:** `Focus` gains a `Gate` interface consulted on landings by new gated overloads of `set`/`move` (ungated callers are bit-for-bit unchanged). `ScrollWindow` passes the gate through and learns the none slot. A header-only `FocusScreen : Screen` owns a `ScrollWindow`, makes `focus()`/`setFocus()` `final`, and exposes `moveFocus(delta)`, a `focusable(int)` virtual (default: everything) and a pure `syncVm()`. The five focused screens derive from it and delete their triads; Settings deletes its hand-rolled skip walk.

**Tech Stack:** C++20, doctest, CMake (file(GLOB) — re-run configure after adding files). Spec: `docs/superpowers/specs/2026-08-22-focus-screen-design.md`.

---

### Task 1: `Focus::Gate` and the gated `set`/`move`

**Files:**
- Modify: `core/include/reader/focus.h`
- Modify: `core/src/focus.cpp`
- Test: `test/unit/test_focus.cpp` (append)

- [ ] **Step 1: Write the failing tests** — append to `test/unit/test_focus.cpp`:

```cpp
namespace {
// A gate over a fixed table, the shape SettingsScreen needs: headers and
// placeholder rows refuse, device rows accept.
struct TableGate : reader::Focus::Gate {
  std::vector<bool> ok;
  explicit TableGate(std::vector<bool> t) : ok(std::move(t)) {}
  bool focusable(int index) const override {
    return index >= 0 && index < static_cast<int>(ok.size()) && ok[static_cast<size_t>(index)];
  }
};
struct AllGate : reader::Focus::Gate {
  bool focusable(int) const override { return true; }
};
}  // namespace

TEST_CASE("a gate that refuses nothing changes nothing -- move and set are the ungated calls") {
  // The gated walk is a second implementation of wrap/clamp/none, so this pins it
  // to the modular arithmetic across every configuration, multi-lap included.
  AllGate all;
  for (const Focus::None none : {Focus::Noneless, Focus::WithNone}) {
    for (const bool wrap : {true, false}) {
      for (int count = 0; count <= 5; ++count) {
        for (int start = -1; start < (count == 0 ? 0 : count); ++start) {
          for (int delta = -12; delta <= 12; ++delta) {
            CAPTURE(none); CAPTURE(wrap); CAPTURE(count); CAPTURE(start); CAPTURE(delta);
            Focus a(count, none); a.setWrapping(wrap); a.set(start);
            Focus b(count, none); b.setWrapping(wrap); b.set(start);
            CHECK(a.move(delta) == b.move(delta, &all));
            CHECK(a.index() == b.index());
          }
          for (int target = -3; target <= count + 3; ++target) {
            CAPTURE(none); CAPTURE(wrap); CAPTURE(count); CAPTURE(start); CAPTURE(target);
            Focus a(count, none); a.setWrapping(wrap); a.set(start);
            Focus b(count, none); b.setWrapping(wrap); b.set(start);
            CHECK(a.set(target) == b.set(target, &all));
            CHECK(a.index() == b.index());
          }
        }
      }
    }
  }
}

TEST_CASE("a gated move skips refused landings, wrapping through them without consuming a step") {
  // The Settings shape: 11 items, only 7..9 focusable.
  TableGate gate({false, false, false, false, false, false, false, true, true, true, false});
  Focus f(11);
  f.set(7);  // seat the focus ungated, as a screen's ctor does
  CHECK(f.move(+1, &gate)); CHECK(f.index() == 8);
  CHECK(f.move(+1, &gate)); CHECK(f.index() == 9);
  // Off the focusable end: wraps through 10, 0..6 and lands on 7.
  CHECK(f.move(+1, &gate)); CHECK(f.index() == 7);
  // ...and back the other way.
  CHECK(f.move(-1, &gate)); CHECK(f.index() == 9);
  // A distance counts focusable landings, not raw positions.
  CHECK(f.move(+2, &gate)); CHECK(f.index() == 8);
}

TEST_CASE("a gated move on a non-wrapping focus stops at the last focusable position") {
  TableGate gate({false, true, true, false});
  Focus f(4);
  f.setWrapping(false);
  f.set(1);
  CHECK(f.move(+5, &gate)); CHECK(f.index() == 2);  // 3 refuses, clamp ends the walk
  CHECK_FALSE(f.move(+1, &gate)); CHECK(f.index() == 2);
}

TEST_CASE("a gate that refuses everything moves nothing, wrapping or not") {
  TableGate none({false, false, false});
  Focus f(3);
  CHECK_FALSE(f.move(+1, &none)); CHECK(f.index() == 0);
  CHECK_FALSE(f.move(-1, &none)); CHECK(f.index() == 0);
}

TEST_CASE("a gate whose only focusable position is the current one moves nothing") {
  TableGate gate({false, false, true, false});
  Focus f(4);
  f.set(2);
  CHECK_FALSE(f.move(+1, &gate)); CHECK(f.index() == 2);
  CHECK_FALSE(f.move(-1, &gate)); CHECK(f.index() == 2);
}

TEST_CASE("a gated set clamps first and refuses an unlandable landing, index unchanged") {
  TableGate gate({false, false, false, false, false, false, false, true, true, true, false});
  Focus f(11);
  f.set(7);
  CHECK_FALSE(f.set(0, &gate));   CHECK(f.index() == 7);   // a header refuses
  CHECK_FALSE(f.set(400, &gate)); CHECK(f.index() == 7);   // clamps to 10, which refuses
  CHECK_FALSE(f.set(-2, &gate));  CHECK(f.index() == 7);   // clamps to 0, which refuses
  CHECK(f.set(8, &gate));         CHECK(f.index() == 8);
  CHECK_FALSE(f.set(8, &gate));   CHECK(f.index() == 8);   // unchanged is false, not a failure
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake -S . -B build && cmake --build build -j 2>&1 | tail -5`
Expected: compile FAILURE — `Focus::Gate` does not exist.

- [ ] **Step 3: Implement.** In `core/include/reader/focus.h`, inside `class Focus`, add above `set`:

```cpp
  // WHETHER A POSITION MAY BE LANDED ON. Consulted by the gated overloads below:
  // move() steps over a refused position without consuming any of its distance,
  // and set() refuses a landing outright. The CURRENT position is never asked --
  // a focus can find itself somewhere it could not land (a screen before its
  // first setFocus), and moving OFF such a place must work.
  //
  // An interface rather than a callable for the reason ScreenFactory is one
  // (app.h): no <functional>, no allocation, and the one implementer is a
  // long-lived screen that can simply be pointed at.
  class Gate {
   public:
    virtual bool focusable(int index) const = 0;

   protected:
    ~Gate() = default;  // never owned, never deleted through this interface
  };
```

Change the two signatures (keep their comments, append the gate sentence):

```cpp
  bool set(int index, const Gate* gate = nullptr);
  bool move(int delta, const Gate* gate = nullptr);
```

Add beside `clampIndex()` in the private section:

```cpp
  // One position in `dir` from `from`, honouring this focus's own wrap, clamp
  // and none-slot rules -- the arithmetic move(+/-1) performs, extracted so the
  // gated walk cannot become a second copy of it. Returns `from` itself at a
  // clamping end.
  int stepOnce(int from, int dir) const;
```

In `core/src/focus.cpp`, replace `set` and `move` and add `stepOnce`:

```cpp
bool Focus::set(int index, const Gate* gate) {
  const int was = index_;
  index_ = index;
  clampIndex();
  // Clamp FIRST, then let the gate refuse the landing: a record naming row 400
  // clamps to the last row, and if that row cannot be landed on the restore is
  // refused with the index unchanged -- which is what "REFUSED rather than
  // clamped" has to mean on a list whose ends are unfocusable. An empty list is
  // never asked: -1 is its only state and there is nothing to refuse into.
  if (gate != nullptr && count_ > 0 && !gate->focusable(index_)) {
    index_ = was;
    return false;
  }
  return index_ != was;
}

int Focus::stepOnce(int from, int dir) const {
  if (count_ <= 0) return from;
  if (!wrap_) {
    int next = from + dir;
    if (next < lowest()) next = lowest();
    if (next > count_ - 1) next = count_ - 1;
    return next;
  }
  const int span = count_ - lowest();
  if (span <= 1) return from;  // one position: nowhere to step to
  int offset = (from - lowest() + dir) % span;
  if (offset < 0) offset += span;
  return lowest() + offset;
}

bool Focus::move(int delta, const Gate* gate) {
  if (count_ <= 0) return false;
  if (gate == nullptr) {
    // The ungated path keeps its O(1) arithmetic, bit-for-bit: every existing
    // caller lands exactly where it always did, multi-lap wraps included.
    if (!wrap_) return set(index_ + delta);
    const int span = count_ - lowest();
    if (span <= 1) return false;  // one position: nowhere to wrap to
    int offset = (index_ - lowest() + delta) % span;
    if (offset < 0) offset += span;
    const int next = lowest() + offset;
    if (next == index_) return false;
    index_ = next;
    return true;
  }

  // The gated walk: one position at a time so each LANDING can be judged. A
  // refused position is stepped over without consuming any distance; the guard
  // bounds the skip at one full lap, so a list with nothing focusable moves
  // nothing instead of spinning -- the full-circle check SettingsScreen used to
  // hand-roll, now in the one place movement rules live.
  const int start = index_;
  const int dir = delta < 0 ? -1 : 1;
  int steps = delta < 0 ? -delta : delta;
  int i = index_;
  while (steps-- > 0) {
    int j = stepOnce(i, dir);
    int guard = count_ - lowest();
    while (j != i && !gate->focusable(j) && guard-- > 0) j = stepOnce(j, dir);
    if (j == i || !gate->focusable(j)) break;  // a clamping end, or nothing to land on
    i = j;
  }
  if (i == start) return false;
  index_ = i;
  return true;
}
```

Delete the old `set` and `move` bodies (the ungated arithmetic moves inside the new `move`).

- [ ] **Step 4: Run to verify pass**

Run: `make test 2>&1 | tail -3`
Expected: all tests pass (the new cases and every existing suite).

- [ ] **Step 5: Commit**

```bash
git add core/include/reader/focus.h core/src/focus.cpp test/unit/test_focus.cpp
git commit -m "feat: Focus learns landing rules -- a Gate refuses positions, move skips them"
```

---

### Task 2: `ScrollWindow` gate pass-through and the none slot

**Files:**
- Modify: `core/include/reader/scrollwindow.h`
- Modify: `core/src/scrollwindow.cpp`
- Test: `test/unit/test_scrollwindow.cpp` (append)

- [ ] **Step 1: Write the failing tests** — append to `test/unit/test_scrollwindow.cpp` (reuse a local TableGate; define one in this file's anonymous namespace, same code as Task 1's):

```cpp
namespace {
struct WindowTableGate : reader::Focus::Gate {
  std::vector<bool> ok;
  explicit WindowTableGate(std::vector<bool> t) : ok(std::move(t)) {}
  bool focusable(int index) const override {
    return index >= 0 && index < static_cast<int>(ok.size()) && ok[static_cast<size_t>(index)];
  }
};
}  // namespace

TEST_CASE("the window follows a gated skip in one move") {
  // 11 items, 3 on glass, only 7..9 focusable: one Down from 7 that skips
  // nothing still scrolls normally, and the wrap off 9 lands back on 7 with the
  // window following the whole way.
  WindowTableGate gate({false, false, false, false, false, false, false, true, true, true, false});
  ScrollWindow w(11, 3);
  w.setFocus(7);
  REQUIRE(w.focus() == 7);
  CHECK(w.moveFocus(+1, &gate));
  CHECK(w.focus() == 8);
  CHECK(w.moveFocus(+2, &gate));  // 9, then wrap-skip to 7
  CHECK(w.focus() == 7);
  CHECK(w.firstVisible() <= 7);
  CHECK(w.firstVisible() + 3 > 7);  // the focus is inside the window
}

TEST_CASE("a gated setFocus that is refused leaves the window alone") {
  WindowTableGate gate({false, true, true});
  ScrollWindow w(3, 2);
  w.setFocus(1);
  const int first = w.firstVisible();
  CHECK_FALSE(w.setFocus(0, &gate));
  CHECK(w.focus() == 1);
  CHECK(w.firstVisible() == first);
}

TEST_CASE("a window built WithNone starts on the none slot and keeps it as a position") {
  ScrollWindow w(3, 3, Focus::WithNone);
  CHECK(w.focus() == -1);
  CHECK(w.moveFocus(-1));       // wraps through the none slot to the last row
  CHECK(w.focus() == 2);
  CHECK(w.moveFocus(+1));       // ...and back onto it
  CHECK(w.focus() == -1);
  w.setCount(5);                // growing the list must not drag -1 onto row 0
  CHECK(w.focus() == -1);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build -j 2>&1 | tail -5`
Expected: compile FAILURE — no three-argument ctor, no gate parameters.

- [ ] **Step 3: Implement.** In `core/include/reader/scrollwindow.h`:

```cpp
  ScrollWindow(int count, int visibleRows, Focus::None none = Focus::Noneless);
  ...
  bool moveFocus(int delta, const Focus::Gate* gate = nullptr);
  bool setFocus(int index, const Focus::Gate* gate = nullptr);
```

(append to `moveFocus`'s comment: "A `gate` is passed through to Focus, whose
landing rules these are; the window only follows.") In `core/src/scrollwindow.cpp`:

```cpp
ScrollWindow::ScrollWindow(int count, int visibleRows, Focus::None none)
    : focus_(count, none), visible_(visibleRows) {
  clampWindow();
}

bool ScrollWindow::moveFocus(int delta, const Focus::Gate* gate) {
  if (focus_.count() == 0 || visible_ == 0) return false;
  const int wasFirst = first_;
  const bool moved = focus_.move(delta, gate);
  clampWindow();
  return moved || first_ != wasFirst;
}

bool ScrollWindow::setFocus(int index, const Focus::Gate* gate) {
  const int wasFirst = first_;
  const bool moved = focus_.set(index, gate);
  clampWindow();
  return moved || first_ != wasFirst;
}
```

- [ ] **Step 4: Run to verify pass**

Run: `make test 2>&1 | tail -3` — all green.

- [ ] **Step 5: Commit**

```bash
git add core/include/reader/scrollwindow.h core/src/scrollwindow.cpp test/unit/test_scrollwindow.cpp
git commit -m "feat: ScrollWindow passes the gate through and can hold a none slot"
```

---

### Task 3: the `FocusScreen` base class

**Files:**
- Create: `core/include/reader/focus_screen.h` (header-only)
- Test: `test/unit/test_focus_screen.cpp` (new)

- [ ] **Step 1: Write the failing test** — `test/unit/test_focus_screen.cpp`:

```cpp
// THE BASE CLASS THAT MAKES THE FOCUS PAIR STRUCTURAL. Its contract is tested
// here on a minimal screen so the five real screens' suites test their content,
// not the mechanism.
#include <vector>

#include "doctest.h"
#include "reader/focus_screen.h"

using namespace reader;

namespace {

struct MiniScreen : FocusScreen {
  int synced = 0;
  int mirrored = -99;
  std::vector<bool> table;  // empty = everything focusable

  explicit MiniScreen(int count, std::vector<bool> t = {},
                      Focus::None none = Focus::Noneless)
      : FocusScreen(count, count, none), table(std::move(t)) {
    syncVm();
  }

  ScreenId id() const override { return ScreenId::Home; }
  ButtonMask longPressable() const override { return 0; }
  Action onEvent(const InputEvent& ev) override {
    if (ev.kind != PressKind::Short) return Action::none();
    if (ev.button == Button::Down) return moveFocus(+1);
    if (ev.button == Button::Up) return moveFocus(-1);
    return Action::none();
  }
  void render(Framebuffer&, const FontSet&, Theme&, Plane) const override {}

  bool focusable(int i) const override {
    if (table.empty()) return true;
    return i >= 0 && i < static_cast<int>(table.size()) && table[static_cast<size_t>(i)];
  }
  void syncVm() override {
    ++synced;
    mirrored = focus();
  }
};

}  // namespace

TEST_CASE("setFocus and moveFocus mirror into the vm exactly when something changed") {
  MiniScreen s(3);
  const int base = s.synced;
  CHECK(s.setFocus(2));
  CHECK(s.mirrored == 2);
  CHECK(s.synced == base + 1);
  CHECK_FALSE(s.setFocus(2));      // unchanged: no mirror call owed
  CHECK(s.synced == base + 1);
  CHECK(s.moveFocus(+1).kind == Action::Kind::Redraw);  // wraps to 0
  CHECK(s.mirrored == 0);
}

TEST_CASE("a clamped screen answers none() at the end instead of a redraw") {
  MiniScreen s(3);
  s.window().setWrapping(false);
  s.setFocus(2);
  CHECK(s.moveFocus(+1).kind == Action::Kind::None);
  CHECK(s.focus() == 2);
}

TEST_CASE("the focusable override skips and refuses through the shared mechanism") {
  MiniScreen s(5, {false, true, true, false, true});
  s.setFocus(1);
  CHECK(s.moveFocus(+2).kind == Action::Kind::Redraw);  // 2, skip 3, land 4
  CHECK(s.focus() == 4);
  CHECK(s.moveFocus(+1).kind == Action::Kind::Redraw);  // wraps: skip 0, land 1
  CHECK(s.focus() == 1);
  CHECK_FALSE(s.setFocus(3));                            // refused landing
  CHECK(s.focus() == 1);
}

TEST_CASE("a WithNone screen keeps -1 as a place the focus can be and report") {
  MiniScreen s(2, {}, Focus::WithNone);
  CHECK(s.focus() == -1);
  CHECK(s.setFocus(1));
  CHECK(s.setFocus(-1));
  CHECK(s.focus() == -1);
  CHECK(s.mirrored == -1);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake -S . -B build && cmake --build build -j 2>&1 | tail -5`
Expected: FAILURE — `reader/focus_screen.h` does not exist. (The configure re-run is what picks the new test file up: file(GLOB).)

- [ ] **Step 3: Implement** — `core/include/reader/focus_screen.h`:

```cpp
#pragma once
#include "reader/app.h"
#include "reader/scrollwindow.h"

namespace reader {

// A SCREEN WITH A MOVABLE SELECTION, and the one implementation of the rule the
// catalogue keeps re-learning: A SCREEN THAT REPORTS A FOCUS ACCEPTS ONE BACK.
//
// Before this class the rule was five copies of a six-line mirror -- report the
// focus, accept one, redraw only if something moved, mirror the index into the
// view-model -- and three screens shipped with half of it, each behind a header
// comment arguing why its own case was the exception (see Screen::focus in
// app.h). Here the pair is final, so half-overriding it is not writable, and
// test_focus_restore.cpp's catalogue walk checks a property the type system now
// enforces rather than being the only thing that does.
//
// WHAT A DERIVED SCREEN SUPPLIES:
//   - its range, at construction. A screen that fits on one panel passes
//     visibleRows == count (a window that never scrolls behaves exactly as a
//     bare Focus); a scrolling list passes what the theme measured, or 0 for
//     "not told yet" -- movement is then refused, matching "a screen must not
//     draw a row it was not given".
//   - syncVm(): mirror focus() -- and, for a windowed list, the visible slice --
//     into the view-model the theme draws. Called exactly when setFocus or
//     moveFocus changed something; a screen's own mutations (a rescan) call it
//     themselves.
//   - focusable(int), only if some positions refuse a landing (Settings'
//     section headers and placeholder rows). Skipping, wrapping through refused
//     ends, and refusing an unlandable restore all come from Focus::Gate -- the
//     hand-rolled walk this replaces silently stopped wrapping once.
class FocusScreen : public Screen, private Focus::Gate {
 public:
  // WHERE THE SELECTION IS, in the whole list -- what the session record stores.
  int focus() const final { return window_.focus(); }

  // PUT A STORED FOCUS BACK. Clamps into range (a record naming row 400 of a
  // three-row list means "as far down as you can go"), refuses a landing the
  // gate does; the bool means "something changed", per Screen::setFocus.
  bool setFocus(int index) final {
    if (!window_.setFocus(index, this)) return false;
    syncVm();
    return true;
  }

 protected:
  FocusScreen(int count, int visibleRows, Focus::None none = Focus::Noneless)
      : window_(count, visibleRows, none) {}

  // Redraw only when the focus actually moved: at the end of a clamping list a
  // press must not cost a ~520 ms panel refresh that changes nothing.
  Action moveFocus(int delta) {
    if (!window_.moveFocus(delta, this)) return Action::none();
    syncVm();
    return Action::redraw();
  }

  // The window, for what stays the screen's own business: setCount on a rescan,
  // setVisibleRows from the theme's box model, the visible slice for the vm.
  ScrollWindow& window() { return window_; }
  const ScrollWindow& window() const { return window_; }

  // Whether `index` may be LANDED on. Default: every position -- a screen with
  // unfocusable rows overrides this and gets skip, wrap and restore-refusal
  // from the one shared mechanism.
  bool focusable(int index) const override {
    (void)index;
    return true;
  }

  // Mirror the focus into the view-model the theme draws. The mirror is the
  // whole of what a screen still does about focus.
  virtual void syncVm() = 0;

 private:
  ScrollWindow window_;
};

}  // namespace reader
```

- [ ] **Step 4: Run to verify pass**

Run: `make test 2>&1 | tail -3` — all green.

- [ ] **Step 5: Commit**

```bash
git add core/include/reader/focus_screen.h test/unit/test_focus_screen.cpp
git commit -m "feat: FocusScreen -- the focus pair becomes final, the mirror becomes one mechanism"
```

---

### Task 4: migrate the two overlays

**Files:**
- Modify: `core/include/reader/screen_item_actions.h`, `core/src/screen_item_actions.cpp`
- Modify: `core/include/reader/screen_delete_confirm.h`, `core/src/screen_delete_confirm.cpp`
- Tests: existing `test_screen_item_actions.cpp`, `test_screen_delete_confirm.cpp`, `test_partial_repaint.cpp`, `test_focus_restore.cpp` are the net — no new tests.

- [ ] **Step 1: ItemActions header.** Include `reader/focus_screen.h` instead of `reader/focus.h`; `class ItemActionsScreen : public FocusScreen`; delete the `int focus() const override` / `bool setFocus(int) override` declarations and their comment (replace with one line pointing at the base: `// focus()/setFocus() are FocusScreen's -- final, one mechanism, see focus_screen.h.`); delete `Action moveFocus(int delta);`, `bool syncFocus(bool moved);`, and the `Focus focus_;` member; add `void syncVm() override;` in the private section. `paintFootprint()` is unchanged (it reads `vm_.focusedAction`).

- [ ] **Step 2: ItemActions cpp.** Ctor gains the base initializer and ends by mirroring; the triad goes:

```cpp
ItemActionsScreen::ItemActionsScreen(const LibraryScreen& library)
    : FocusScreen(kRowCount, kRowCount) {
  ...unchanged vm_ setup...
  // The base's focus starts on the first row, which is kOpen -- the board's own
  // starting selection. syncVm() mirrors it; there is no per-screen focus state
  // left to seed.
  syncVm();
}

void ItemActionsScreen::syncVm() { vm_.focusedAction = focus(); }
```

Delete `syncFocus`, `setFocus`, `moveFocus` definitions and the `focus_ = Focus(kRowCount); focus_.set(vm_.focusedAction);` lines (and the now-stale `vm_.focusedAction = kOpen;` — syncVm supplies it). `onEvent` is unchanged: `moveFocus(±1)` now resolves to the inherited one.

- [ ] **Step 3: DeleteConfirm, same shape.** Header: base class swap, delete the pair declarations + triad + `Focus focus_;`, add `void syncVm() override;`. Cpp:

```cpp
DeleteConfirmScreen::DeleteConfirmScreen(LibraryScreen& library)
    : FocusScreen(kRowCount, kRowCount), library_(library) {
  ...unchanged vm_ setup...
  // The base starts on the first row, which is kCancel -- where a destructive
  // prompt's focus belongs (a press made before reading it cancels).
  syncVm();
}

void DeleteConfirmScreen::syncVm() { vm_.focusedAction = focus(); }
```

- [ ] **Step 4: Run** `make test 2>&1 | tail -3` — all green (partial-repaint pairs included).

- [ ] **Step 5: Commit**

```bash
git add core/include/reader/screen_item_actions.h core/src/screen_item_actions.cpp \
        core/include/reader/screen_delete_confirm.h core/src/screen_delete_confirm.cpp
git commit -m "refactor: the overlays' focus triads become FocusScreen"
```

---

### Task 5: migrate Home

**Files:**
- Modify: `core/include/reader/screen_home.h`, `core/src/screen_home.cpp`
- Tests: existing `test_screen_home.cpp`, goldens, `test_focus_restore.cpp`.

- [ ] **Step 1: Header.** Include `reader/focus_screen.h` (drop `reader/focus.h`); `class HomeScreen : public FocusScreen`; delete the `focus()`/`setFocus()` overrides and their long history comment — fold its lesson into one line: `// focus()/setFocus() are FocusScreen's. -1 is the CONTINUE block, a position; see the ctor for how the variant chooses its range.` Delete `Action moveFocus(int delta);`, `bool syncFocus(bool moved);`, `Focus focus_;`; add private `void syncVm() override;`.

- [ ] **Step 2: Cpp.** Keep the existing ctor comment about WithNone/Noneless (it documents the base-init line now):

```cpp
HomeScreen::HomeScreen(HomeViewModel vm, std::vector<ScreenId> targets)
    : FocusScreen(static_cast<int>(vm.menu.size()), static_cast<int>(vm.menu.size()),
                  vm.libraryEmpty ? Focus::Noneless : Focus::WithNone),
      vm_(std::move(vm)),
      targets_(std::move(targets)) {
  // The view-model may arrive with a focus already set -- the goldens author one
  // -- so it is adopted rather than reset; then the mirror is re-asserted
  // unconditionally, because setFocus reports "moved" and an unmoved adoption
  // still has to leave vm and window agreeing.
  setFocus(vm_.focusedMenuIndex);
  syncVm();
}

void HomeScreen::syncVm() { vm_.focusedMenuIndex = focus(); }
```

Delete `syncFocus`, `setFocus`, `moveFocus` definitions. `onEvent` unchanged.

- [ ] **Step 3: Run** `make test 2>&1 | tail -3` — all green, goldens byte-identical.

- [ ] **Step 4: Commit**

```bash
git add core/include/reader/screen_home.h core/src/screen_home.cpp
git commit -m "refactor: Home's focus mirror becomes FocusScreen; the variant picks its range"
```

---

### Task 6: migrate Library

**Files:**
- Modify: `core/include/reader/screen_library.h`, `core/src/screen_library.cpp`
- Tests: existing `test_screen_library.cpp`, `test_long_title.cpp`, `library_app.h` users.

- [ ] **Step 1: Header.** Swap `reader/scrollwindow.h` include for `reader/focus_screen.h`; `class LibraryScreen : public FocusScreen`; delete the `focus()`/`setFocus()` overrides (keep their comment's -1 sentence on `focusedItem` or fold into the class comment); `visibleRows()` becomes `{ return window().visibleRows(); }`; delete `Action moveFocus(int delta);` and the `ScrollWindow window_;` member; `void syncVm();` becomes `void syncVm() override;`.

- [ ] **Step 2: Cpp.** Both ctors gain `: FocusScreen(0, 0)` first in the init list. Every `window_.` becomes `window().`. Delete `LibraryScreen::setFocus` and `LibraryScreen::moveFocus` (the inherited ones serve; `onEvent`'s `moveFocus(±step)` compiles against the base). `rescan`, `setVisibleRows`, `focusedItem`, `syncVm` keep their bodies with the accessor rename.

- [ ] **Step 3: Run** `make test 2>&1 | tail -3` — all green.

- [ ] **Step 4: Commit**

```bash
git add core/include/reader/screen_library.h core/src/screen_library.cpp
git commit -m "refactor: Library moves onto FocusScreen; the window stays its business"
```

---

### Task 7: migrate Settings — the skip walk becomes a gate

**Files:**
- Modify: `core/include/reader/screen_settings.h`, `core/src/screen_settings.cpp`
- Modify: `test/unit/test_focus_restore.cpp` (fixture only)

- [ ] **Step 1: Fixture first.** In `test_focus_restore.cpp`'s `build()`, beside `setLibraryVisibleRows(7)`:

```cpp
  // Any workable geometry will do, as with the Library's rows above: a Settings
  // whose metrics were never told has a zero-height window, and movement on a
  // window with no height is refused (ScrollWindow's rule, which Settings now
  // shares instead of hand-rolling around).
  b->factory.setSettingsMetrics(700, 55, 45);
```

- [ ] **Step 2: Header.** Swap `reader/scrollwindow.h` for `reader/focus_screen.h`; `class SettingsScreen : public FocusScreen`; delete the `focus()`/`setFocus()` overrides (keep the "refuses rather than clamps" sentence, now pointing at `focusable`); delete `Action moveFocus(int dir);` and `ScrollWindow window_;`; private section becomes:

```cpp
 private:
  Action cycleFocused();
  bool focusable(int index) const override;
  void syncVm() override;
  int firstFocusable() const;
```

- [ ] **Step 3: Cpp.** Ctor:

```cpp
SettingsScreen::SettingsScreen(const Settings& initial, SettingsSink* sink)
    : FocusScreen(static_cast<int>(kItems.size()), 0), settings_(initial), sink_(sink) {
  // The first focusable row, not row 0: row 0 is the TYPOGRAPHY header. syncVm
  // runs unconditionally after, because a table with nothing focusable must
  // still render a readable screen.
  setFocus(firstFocusable());
  syncVm();
}

bool SettingsScreen::focusable(int index) const {
  if (index < 0 || index >= static_cast<int>(kItems.size())) return false;
  const Item& it = kItems[static_cast<size_t>(index)];
  return !it.isHeader && it.field != Field::None;
}

int SettingsScreen::firstFocusable() const {
  for (size_t i = 0; i < kItems.size(); ++i)
    if (focusable(static_cast<int>(i))) return static_cast<int>(i);
  // Unreachable with the table above, and not an assert: a table edited down to
  // nothing focusable should render a readable screen rather than abort a boot.
  return 0;
}
```

Delete `SettingsScreen::setFocus` and `SettingsScreen::moveFocus` entirely — their comments' lessons now live on `Focus::Gate` and the base class. `setMetrics` and `syncVm` rename `window_.` to `window()`; `cycleFocused` reads `focus()` instead of `window_.focus()`. `onEvent` unchanged (`moveFocus(±1)` is the base's).

- [ ] **Step 4: Run** `make test 2>&1 | tail -3` — all green; `test_focus_restore`'s wrap case must still count five wrapping screens.

- [ ] **Step 5: Commit**

```bash
git add core/include/reader/screen_settings.h core/src/screen_settings.cpp test/unit/test_focus_restore.cpp
git commit -m "refactor: Settings' hand-rolled skip walk becomes a Focus gate"
```

---

### Task 8: documentation and final verification

**Files:**
- Modify: `CLAUDE.md` (Focus paragraph + "THE RULE IS" paragraph)
- Modify: `core/include/reader/app.h` (Screen::focus comment)

- [ ] **Step 1: app.h.** In `Screen::focus`'s comment, replace the "OVERRIDE THEM IN PAIRS" paragraph's first sentence with: "OVERRIDE THEM IN PAIRS — and the way to do that is to derive from FocusScreen (focus_screen.h), where the pair is final and cannot be half-taken." Keep the history that follows.

- [ ] **Step 2: CLAUDE.md.** In the `Focus` paragraph, append: the mirror around Focus is now `FocusScreen`'s (`core/include/reader/focus_screen.h`) — screens supply `syncVm()` and, where rows refuse a landing, `focusable()`; Settings' skip walk moved into `Focus::Gate`. In the "THE RULE IS" paragraph, add that the rule is now structural: `focus()`/`setFocus()` are `final` on `FocusScreen`, and `test_focus_restore.cpp` checks what the type system now also enforces.

- [ ] **Step 3: Full verification**

Run: `make test 2>&1 | tail -5` and `make sim && make compare COMPARE_ARGS="--only home library settings" 2>&1 | tail -5`
Expected: tests green; comparison numbers unchanged from main (no pixel moved).

- [ ] **Step 4: Commit**

```bash
git add CLAUDE.md core/include/reader/app.h
git commit -m "docs: the focus pair is structural now; CLAUDE.md and app.h say where it lives"
```
