# Shared primitives: the rest of the abstraction pass

2026-08-22. The remaining items from the codebase-abstraction analysis, after
FocusScreen landed. Each is small; what earns this document is the decisions —
two items changed shape on contact with the code, and the reasons are the part
worth keeping.

## 1. `buildHints` + `kHintSlotMarks` — NOT a HintSlots view-model struct

The analysis proposed fusing the `hints`/`holds` pair into one view-model type.
Rejected on contact: the pair has ~30 touch points (seven VMs, the demo
catalogue, the shell, eighteen test assertions), and the invariant the struct
would guard — a ring always means a bound hold — is already single-sourced,
because `declareHints(vm_.holds)` and the theme's ring both read the same array.
The churn buys no enforcement.

What actually bit is the THEME side: eight hand-rolled `Hint[4]` loops, four
copies of the `{kBack, kDot, kUp, kDown}` marks array, and an inconsistency —
Home/SdMissing/BookDetails suppress a mark on an empty label (the boards' 36px
dead-slot rule) while the overlays, Library and Settings do not. Their labels
are never empty today, so the inconsistency cannot bite yet; it is exactly the
latent drift the helper removes. Two test files carry a third and fourth copy of
the same loop.

So, in **components** (which owns `Hint`, takes plain arrays, and is reachable
from tests):

```cpp
// The standard slot marks, hardware order (Back, Confirm, Up, Down). Home's
// first slot is the one exception (kBook, because its first hint is READ).
inline constexpr const Icon* kHintSlotMarks[4] = {&icons::kBack, &icons::kDot,
                                                  &icons::kUp, &icons::kDown};

// A slot's mark FOLLOWS its label: the boards author a dead button as an empty
// 36px slot, and a mark over one would be an affordance for an action that is
// not there — and it measures 32px where the board measures 36, shifting every
// other slot along.
void buildHints(const Icon* const marks[4], const std::array<std::string, 4>& labels,
                const std::array<bool, 4>& holds, Hint out[4]);
```

All six VM-reading theme sites use it (uniform rule; zero pixel change since
every unconditional site has permanently non-empty labels — the goldens and the
full-screen byte-compare are the proof). The two MEASURING sites
(`libraryVisibleRows`, `settingsMetrics`) are a different job — labelless slots
that must KEEP their marks, because only marks and the type role set the bar's
height — so they share a theme-local `measuringHints()` instead of pretending to
be the same case. `kLibraryMarks` and the two overlay inline arrays collapse
into `kHintSlotMarks`; `kHomeMarks` stays, being genuinely different. The two
test-file copies of the loop use `buildHints` too.

Also: a `drawHintBar` overload without the `slotXOut` out-parameter — all seven
production callers pass a dummy `int slots[4]` they never read (the out-param
exists for tests).

## 2. `ScrollWindow::slice()`

Library's and Settings' `syncVm` both derive the same three numbers — first
visible row, rows on glass, focus-as-slice-index-or-−1 — and the subtlety (a
focus off-glass or a heightless window must yield −1, so the VM cannot name a
row that was not drawn) is documented only on Library's copy while Settings
computes it a second way inside its loop.

```cpp
struct Slice {
  int first = 0;    // index into the whole list of the first row on glass
  int count = 0;    // rows on glass — visibleCount()
  int focused = -1; // the focus as an index into the slice, or -1
};
Slice slice() const;
```

Both `syncVm`s consume it. Phase 3's Contents and Bookmarks are the next
callers.

## 3. `outlineRect` in components

The four-`fillRect` outline is hand-drawn in ~8 places (the sleep-screen-local
`outline()` helper's "two callers is not a shared primitive yet" comment went
stale). Promoted:

```cpp
void outlineRect(Framebuffer& fb, int x, int y, int w, int h, int t, bool white = false);
```

Converted: sleep's card/bar/badge (the local helper is deleted), the cover
placeholder, Home's unfocused CONTINUE block, the action button's outlined
variant, the rail track, the book thumb border (its `white` case is the focused
row), the panel border. NOT converted: Home's progress bar, which is
filled-then-hollowed — its interior must be painted white, which an outline
deliberately does not do.

## 4. `rowRuleFor` in components

The boards' positional rule — every list row carries a bottom rule EXCEPT the
focused one (its fill runs to the next row's edge) and the last drawn one (the
list's bottom edge stays open) — is restated at three call sites and has
produced the compounding one-pixel defect once.

```cpp
constexpr bool rowRuleFor(int i, int rows, bool focused) { return !focused && i != rows - 1; }
```

Settings composes it with its own section rule: `rowRuleFor(...) && !nextIsHeader`.

## 5. `drawCentredText` in components

Sleep's local `centredText` — measure and draw with the SAME tracking, or the
centring is off by the tracking's total — promoted with an `Ink` parameter.
Callers: sleep's three runs, SdMissing's tracked title (the hazard case), and
HomeEmpty's title.

## 6. Shell: `replaceApp()` and `syncRecognizer()`

The recognizer sync pair appears four times and the App-swap ritual
(`forgetLibrary` → new App → sync) twice, with `forgetLibrary`'s ordering
documented as "not local to this class" and maintained as a prose list of
callers. One `replaceApp(std::unique_ptr<Screen> root)` makes the ordering
structural; `syncRecognizer()` serves the two non-swap sites (boot, and after
every dispatch).

## 7. The ramp manifest: `core/include/reader/font_manifest.h`

The eleven-role role↔asset binding exists in three build worlds — the shell's
embedded arrays, the simulator's file loads, the unit tests' `ramp.h` — and
adding a role for Phase 3 means editing all three in lockstep. One X-macro list:

```cpp
#define READER_FONT_RAMP(X)           \
  X(Meta400, spacegrotesk_400_10pt)   \
  ... eleven entries, one per role ...
```

The shell expands it over its `kFont<Role>`/`kFont<Role>Size` embedded arrays
(the generated names already match the Role names); the simulator and the test
ramp expand it over `<stem>.rfnt` file loads. `FontSet::ready()` still catches a
role nothing loaded; the manifest is what makes "the same eleven" one claim
instead of three. The body face (`kFontBodySerif`) is not in it — it is
ScalableFont's, not the ramp's.

## Verification

Every item is either pixel-neutral by construction or pure plumbing, so the
referee is the same as FocusScreen's: the full test suite, the goldens
byte-exact, all nine screens × two geometries byte-identical to the pre-change
build, and a firmware compile. New API (`slice`, `buildHints`, `outlineRect`,
`rowRuleFor`) gets unit tests first; the refactors ride the existing net.
