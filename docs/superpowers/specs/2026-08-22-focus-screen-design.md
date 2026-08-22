# FocusScreen: one mechanism for "a screen that reports a focus accepts one back"

2026-08-22. Follows the codebase-abstraction analysis that found the focus→view-model
mirror still copied per screen after `Focus` extracted the arithmetic.

## The problem

`Focus` owns clamping and wrapping, but every screen still hand-writes the wrapper
around it, and the wrapper is where the defects have actually shipped:

- `syncFocus` / `setFocus` / `moveFocus` are byte-identical in `HomeScreen`,
  `ItemActionsScreen` and `DeleteConfirmScreen` (three copies of the same six
  lines, differing only in which view-model field is mirrored).
- `LibraryScreen` and `SettingsScreen` carry the `ScrollWindow` variant of the same
  shape.
- The pairing rule — a screen that reports a focus must accept one back — is
  enforced only by `test_focus_restore.cpp`. Three screens shipped one-way before
  that test existed, each with a header comment arguing why *its* case was the
  exception.
- Settings' skip-unfocusable stepping is its own hand-rolled walk, and it silently
  stopped wrapping while every other list rolled over (recorded in
  `screen_settings.cpp`'s own comment). A sixth screen with unfocusable rows would
  write that walk again.

## Approaches considered

**A. A `FocusScreen` base owning a `ScrollWindow`, with landing rules pushed into
`Focus` via a `Gate` interface.** One state-holder for every focused screen;
`focus()`/`setFocus()` are `final` in the base, so the pairing cannot be
half-overridden; skip-unfocusable becomes a `focusable(int)` virtual consumed by
`Focus` itself. **Chosen.**

**B. An adapter base (`focusIndex()`/`applyFocus()`/`stepFocus()` virtuals),
screens keep their own holder.** Rejected: the three adapter one-liners per screen
roughly equal the boilerplate deleted, and skip-unfocusable cannot be shared —
`stepFocus` moves by a whole delta, and skipping needs a landing check per step. The
sharpest bug source would stay per-screen.

**C. Free helper functions only.** Rejected: removes lines but enforces nothing —
the defect class is a screen forgetting half the contract, which only a base class
making the pair `final` prevents.

## Design

### 1. `Focus::Gate` — landing rules live in Focus, like every other movement rule

```cpp
class Focus {
 public:
  // Whether a position may be LANDED on. Positions that refuse are stepped
  // over by move() and refused by set(); the CURRENT position is never asked
  // (a focus can find itself somewhere it could not land, e.g. before a
  // screen's first setFocus). An interface rather than a callable for the
  // reason ScreenFactory is one: no <functional>, no allocation.
  class Gate {
   public:
    virtual bool focusable(int index) const = 0;
   protected:
    ~Gate() = default;  // never owned or deleted through this interface
  };

  bool set(int index, const Gate* gate = nullptr);
  bool move(int delta, const Gate* gate = nullptr);
  ...
};
```

- `gate == nullptr` (every existing caller): behaviour is bit-for-bit today's —
  `move` keeps its O(1) modular arithmetic, `set` keeps clamp-and-report.
- `move(delta, gate)`: walks one position at a time, `|delta|` times in
  `sign(delta)`'s direction, honouring the focus's own wrap/clamp/none rules
  via a private `stepOnce(from, dir)` helper (the same arithmetic `move(±1)`
  performs, extracted — NOT a second copy of the rules). A landing the gate
  refuses is stepped over without consuming a step. A full lap finding nothing
  focusable stops the walk (a table with nothing focusable must not spin — the
  guard is Settings' full-circle check, moved into the one place movement
  lives). Returns whether the index changed.
- `set(index, gate)`: clamps exactly as today, then if the landing is refused,
  restores the previous index and returns false. On an empty list the gate is
  not consulted: -1 is the only state and there is nothing to refuse into.
  This reproduces Settings' "REFUSED rather than clamped" restore semantics
  through the general rule "clamp into range, then refuse an unlandable
  landing" — a record naming row 400 clamps to the last row, which for
  Settings is unfocusable, so the restore is refused, which is what Settings
  does today by refusing out-of-range indices outright.

### 2. `ScrollWindow` — pass-through, plus the none slot

- `moveFocus(delta, gate = nullptr)` / `setFocus(index, gate = nullptr)` forward
  the gate to `focus_`. Window-follow (`clampWindow`) is unchanged.
- The constructor gains `Focus::None none = Focus::Noneless`, forwarded to the
  `Focus` it owns, so Home (whose -1 is the CONTINUE block) can live in a
  window. `Focus` already implements every none-slot rule (`setCount` keeps -1
  where -1 is a position); this is ctor plumbing only.

### 3. `FocusScreen` — the base class (new: `core/include/reader/focus_screen.h` + `core/src/focus_screen.cpp`)

```cpp
// A screen with a movable selection. THE RULE IS: A SCREEN THAT REPORTS A
// FOCUS ACCEPTS ONE BACK — and here the pair is one mechanism, final, so a
// derived screen cannot override half of it. test_focus_restore.cpp still
// walks the catalogue; this makes the property it checks structural.
class FocusScreen : public Screen, private Focus::Gate {
 public:
  int focus() const final { return window_.focus(); }
  bool setFocus(int index) final;   // window_.setFocus(index, this) + syncVm()

 protected:
  FocusScreen(int count, int visibleRows, Focus::None none = Focus::Noneless);

  // Redraw only when something moved: at the end of a clamping list a press
  // must not cost a ~520 ms refresh that changes nothing.
  Action moveFocus(int delta);      // window_.moveFocus(delta, this) + syncVm()

  ScrollWindow& window();           // for rescan/setCount/setVisibleRows/slices
  const ScrollWindow& window() const;

  // Whether `index` is a position the focus may land on. Default: every
  // position. Settings overrides it (headers and placeholder rows refuse) and
  // gets skipping, wrapping and restore-refusal from the shared mechanism.
  bool focusable(int index) const override { return true; }

  // Mirror the focus (and, for windowed lists, the visible slice) into the
  // view-model the theme draws. Called exactly when setFocus/moveFocus report
  // a change; screens may also call it from their own mutations (rescan).
  virtual void syncVm() = 0;

 private:
  ScrollWindow window_;
};
```

Screens that fit on one panel pass `visibleRows == count` (a window that never
scrolls behaves exactly as a bare `Focus`). `Screen`'s own `focus()/setFocus()`
defaults stay for the three focusless screens (SdMissing, Sleep, BookDetails).

### 4. Screen migrations

| Screen | Base ctor | Deletes | Keeps |
|---|---|---|---|
| Home | `(menu.size(), menu.size(), libraryEmpty ? Noneless : WithNone)` | `focus_`, `syncFocus`, `setFocus`, `moveFocus` | ctor adopt-then-mirror (`setFocus(vm.focusedMenuIndex); syncVm();`) |
| ItemActions | `(kRowCount, kRowCount)` | same triad | ctor ends `syncVm()` |
| DeleteConfirm | `(kRowCount, kRowCount)` | same triad | ctor ends `syncVm()` |
| Library | `(0, 0)` then `window().setCount` via rescan | `setFocus`, `moveFocus`, `window_` | `rescan`, `descend`, `ascend`, `syncVm` (now the override) |
| Settings | `(kItems.size(), 0)` | `setFocus`, `moveFocus` (the hand-rolled walk), `window_` | `focusable(i)` override, `firstFocusable()` (now a loop over `focusable`), `syncVm` |

`syncVm()` for Home/ItemActions/DeleteConfirm is one line (mirror `focus()` into
the vm field). Library's and Settings' existing `syncVm()` become the override.

### 5. Known, accepted behaviour deltas

- Settings with metrics never set (visibleRows 0): the base's `moveFocus` refuses
  (ScrollWindow's rule), where today's hand-rolled walk moved an invisible focus.
  The screen renders nothing in that state either way, and both the shell and the
  simulator set metrics before the first event. The refusal is the more consistent
  answer (Library already behaves this way).
- No pixel changes anywhere: this is state plumbing, the theme is untouched, and
  the goldens plus `make compare` are the referee.

## Testing

- **Equivalence property** (`test_focus.cpp`): for none ∈ {Noneless, WithNone},
  wrap ∈ {on, off}, count 0..5, every start position, delta -12..12: gated
  move/set with an everything-focusable gate must equal the ungated call — index
  and return value both. This pins the per-step walk to the modular arithmetic,
  multi-lap wraps included.
- **Gate cases** (`test_focus.cpp`): skip over refused positions in both
  directions; wrap through refused ends; a table with nothing focusable moves
  nothing; only-current-focusable moves nothing; `set` refuses an unlandable
  landing with index unchanged; the clamped landing being unlandable refuses
  (Settings' row-400 restore).
- **ScrollWindow** (`test_scrollwindow.cpp`): gate pass-through moves the window
  with the skipped-to focus; `WithNone` construction holds -1.
- **Screens**: the existing suites are the regression net and must pass
  unchanged — `test_screen_*` for each of the five, `test_focus_restore.cpp`
  (round trip for every ScreenId, wrap case), `test_app.cpp`,
  `test_partial_repaint.cpp` (ItemActions' footprint depends on the focused row),
  and all goldens.

## Consequences

- A sixth focused screen inherits the whole contract by choosing a base class;
  the failure mode "reports a focus, drops the one handed back" becomes
  unwritable rather than merely tested-for.
- `Focus`'s header claim — "the one place the rules about moving it live" —
  becomes true of landing rules too; Settings' walk was the last movement rule
  living in a screen.
- CLAUDE.md's Focus and "THE RULE IS" paragraphs get a sentence naming
  `FocusScreen`; `Screen::focus()`'s "OVERRIDE THEM IN PAIRS" comment points at
  the base class as the way to get the pair.
- New source file ⇒ re-run `cmake -S . -B build` (file(GLOB) caveat).
