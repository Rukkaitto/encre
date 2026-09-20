# Architecture audit — 2026-09-20

Run against `5a8a6a0` on branch `Rukkaitto/Architecture-audit`. Seven candidates
found, four designed in full, five cards filed (#174, #175, #176, #177, and one
`Someday` draft). **The two things worth acting on first are about 45 lines
between them**, and neither is one of the seven.

This file is the reasoning the board is forbidden from holding. `docs/notes/the-board.md`:
*"a card that restates why is a second copy of a paragraph"* — so the five cards
carry a `Source` pointing here and at most two sentences of their own.

**IT CORRECTED ITSELF TWICE AND BOTH CORRECTIONS ARE THE USEFUL PART.** The audit's
own report asserted two things that are false, and the design round caught them; the
design round then asserted two things about the *tree* that are also false, and
verification caught those. Both sets are recorded below rather than quietly fixed,
because the shape — a claim that is true when written and nobody's job to revisit —
is the one this repo records more than any other.

---

## 1. Method, so it can be re-run

Scoped by churn: `git log --oneline -60 --name-only` over the last sixty commits,
ranked by file. The hot spots were `core/src/theme_quiet.cpp` (18 commits),
`shell/src/main.cpp` (17), `core/include/reader/viewmodel.h` (13),
`core/src/screens.cpp` (10), `sim/main.cpp` (9).

Three read-only agents mapped the terrain — the shell, the render trio, the
runtime and storage seams. Four more designed the strong candidates. **Every
load-bearing number in this file was verified independently of the agent that
reported it**, by compiling, measuring or reading the cited line; where a figure
came back different, the difference is recorded rather than reconciled.

The vocabulary is `/codebase-design`'s: **module**, **interface**, **depth**,
**seam**, **adapter**, **leverage**, **locality**, and the **deletion test**. That
matters for one judgement in §3 and is otherwise decoration.

---

## 2. What the seven were

| # | Candidate | Verdict after design |
|---|---|---|
| 01 | A refused transition is unobservable | real, but **instrumentation, not correctness** |
| 02 | Give the shell's decision logic a seam | real, and **smaller than it looks** (−240 of 9,331) |
| 03 | The factory's priming protocol is a caller list | real, and **its own design says try 40 lines first** |
| 04 | "Screen" is spelled in six build worlds | real, and **13 → 10, not 13 → 1** |
| 05 | No screen scaffold — the frame restated 26× | filed, #174 |
| 06 | `Theme` is a hypothetical seam | filed as a `Someday` draft — a decision to record |
| 07 | Tool tests that nothing runs | filed, #175 |

The four measurements that motivated the whole exercise, each verified here:

- **`shell/src/main.cpp` is 9,331 lines and the desktop suite executes none of
  them.** `CMakeLists.txt` links only `reader_core` into `unit_tests`. The phrase
  *"shell/ has no harness"* appears **80 times across 28 files** as a
  justification for putting logic somewhere else.
- **Its entire paint region is ~25 driver statements.** Everything around them is
  sequencing.
- **Seven of the eight non-`main.cpp` files in `shell/` are already adapters at a
  `reader::` seam** — `SdFileSystem`, `ArduinoWifiRadio`, `ArduinoHttpTransport`,
  `NvsWifiSink`, `NvsTokenStore`, `CardFileSink`, `ShellSettingsSink`. `main.cpp`
  is the one that never got one, and it is 73% of the directory.
- **Adapter census**, which is what decides whether a seam is real: `FileSystem` 6,
  `FileHandle` 3, `BodySink` 3, `HttpTransport` 2, `SettingsSink` 2, `ScreenFactory`
  **1**, `CoverSource` 1, `WifiRadio` 1, `Theme` 1 production adapter plus a
  do-nothing test stub.

---

## 3. What the design round changed

### 3.1 Candidate 01 does not do what the audit said it did

The report recommended 01 first, partly on the claim that it closes the V1.1
dead-button class. **It does not close any of it.** All three refusals were
`DemoScreenFactory::create` behaving correctly — `core/src/screens.cpp:786`, `:793`,
`:808` — and the defect was that `shell/` never called the matching setters. Every
assertion a redesigned `App` could make would have been green on the firmware that
shipped those three dead buttons.

What it buys is real and should be described as what it is: it converts *"pressing X
does nothing"* — which costs a bisect through a file with no harness — into one line
in `/encre.log`, readable off an **unplugged** device, which is how all three were
reported. That is the same bargain the wake-refusal counter and `block=` on `[alive]`
already take.

**The shape to copy is four commits old and is in this tree.**
`core/include/reader/wifi_store.h` (#162, in `5a8a6a0`) is this defect one file over:
`SavedNetworks::remember` returned a `bool`, its one caller dropped it, and a refused
ninth network was written to NVS with nothing on the glass. The fix was
`enum class Remembered { Yes, ListFull, BadSsid }` with `[[nodiscard]]`, on the
stated grounds that *"an enum adds that there is no `if (nets.remember(...))` reading
true-ish by accident"*. `App::pushScreen` is the same `bool` with the same two facts
collapsed into one `false` and the same discarding caller
(`core/src/app.cpp:496`).

### 3.2 The audit's own report was wrong about two of candidate 04's six tables

**`Theme`'s virtuals and `NullTheme`'s stubs are already compiler-enforced, and the
report labelled them unbound.** Both classes are instantiated — `QuietTheme theme;`
at `sim/main.cpp:837`, `NullTheme theme;` at `test/unit/test_app.cpp:158` — so a
missing override leaves the class abstract and the build fails. Two more the audit
did not count are bound as well: `kRestorability` (`core/src/app.cpp:159`) and
`kAllScreens` (`test/unit/test_focus_restore.cpp:57`).

So the framing "six tables, one checked" was wrong in the direction that made the
candidate look better than it is. **Three of six are checked**, and what is genuinely
unbound is the simulator's three lists, `compare-design.py`'s three tables, and two
silent fall-throughs.

`Theme` is also **keyed by view-model, not by `ScreenId`** — 28 render virtuals for 29
screens, because `WifiPassword` renders through `renderTextEntry` and
`ArticlesRemoveConfirm` through `renderDeleteConfirm`. A `ScreenId`-keyed manifest
that generated those virtuals would need an exception column. Leave `Theme` out of it.

### 3.3 The honest sizes

- **02 nets −240 lines of 9,331. 2.6%.** The payoff is not volume: the grayscale
  pass order goes from **three copies to one** (`shell/src/main.cpp:5207`,
  `:7788`, and `sim/main.cpp:186`), and #94's false claim becomes *unwritable*
  rather than merely caught — the sequencer asserts the DTM1 baseline itself, after
  a pass it wrote, with no request field able to move it earlier. That is
  `FocusScreen`'s `final` applied to the paint path.
- **04's checklist win is 13 → 10.** Nine of the thirteen are irreducible per-screen
  work — a view-model, a render body, a board, a golden. **#143's 149 files do not
  shrink.** What justifies the manifest is that five of the eliminated places have no
  compile-time bind at all and three of those five have a recorded silent failure.
- **There are two catalogues, not one**: 29 screens against 63 specimens, of which 35
  are state variants (`home_charging`, `wifi_error_not_found`) and `boot` is a board
  with no `ScreenId` behind it. Merging them is the main way that design goes wrong.
- **03 rejected its own headline.** `create(ScreenId, Facts&&)` with a typed sum was
  the proposal; the design rejected it because `create(ScreenId::BookEnd,
  WifiScanFacts{...})` compiles — the id and the alternative are independent runtime
  values, so the mismatch is still representable and the refusal is still a
  `nullptr`. It converts one `nullptr` into a different `nullptr`.

### 3.4 `sizeof(DemoScreenFactory)`

**1,464 bytes measured on the desktop (64-bit); 1,232 reported on the riscv32
target.** Not a contradiction — pointer width. The target figure is the one that
matters: it is permanent `.bss`, and it carries a heap tail, because `contentsToc_`
is a second copy of `gReading.toc` (copied at `shell/src/main.cpp:4665`, copied again
into the screen at `core/src/screens.cpp:705`) — roughly 3 KB on a 92-chapter book,
against an on-glass reading floor of 28,508 bytes.

---

## 4. One false claim, and one precedent that does not reach as far as it is used

Both were found by checking rather than reading. **The first draft of this section
called both of them false, and the second one is not** — the correction is recorded
here rather than made silently, on the same grounds as §3.2.

### 4.1 `[[nodiscard]]` warns here; it does not refuse

`core/include/reader/wifi_store.h` says of `remember()`:

> The compiler refuses the statement form now, on every build including the
> firmware's, **which is the only check `shell/` has** — nothing on the desktop
> executes that branch.

**It warns.** There is no `-Werror` anywhere in this tree, and `CMakeLists.txt:7`
refuses one deliberately: *"No -Werror: a newer compiler must not be able to break
the build."* Verified empirically — a discarded `[[nodiscard]]` return compiles with
`-Wall -Wextra` and exits 0, emitting `-Wunused-result`.

The practical enforcement is that the warning appears in build output beside the diff
that caused it, which is the same bargain `--require-canvas-current` and the mismatch
percentage take. That is worth having and it is not what the comment claims. **The
sentence is load-bearing for #162's whole argument, so it should say "warns" and say
why that is still enough.**

### 4.2 `font_manifest.h` unifies three C++ consumers, not three languages

CLAUDE.md's rule is *"one table spelled in more than one build world is a manifest
(`font_manifest.h`)"*, and the type ramp is offered as the case where this project
already did it.

**That claim is correct and this file's first draft called it false.** The firmware,
the simulator and the test binary are three build worlds under any ordinary reading,
and CLAUDE.md's own longer sentence names them exactly — *"one list expanded by all
three loaders (shell's embedded arrays, the simulator's files, the tests' ramp.h)"*.
Nothing there is wrong.

**What is new is where the manifest stops.** It is expanded in exactly four places,
all C++: `shell/src/main.cpp`, `sim/main.cpp`, `test/unit/ramp.h`,
`tools/sleep_chapter_probe.cpp`. `tools/fontc.py` and `tools/embed_font.py` never
read it, and the `Makefile`'s `fonts:` target hand-spells all twelve stems as
separate command lines — so the *generation* side is still a hand-maintained list,
and the manifest has never crossed a **language** boundary.

That matters only because candidate 04 leans on this precedent to argue a screen
manifest can feed Python and CMake. It can; the precedent just does not demonstrate
it. The generator half would be new work, which is why it needs `versionc.py`'s
discipline (scan rather than list, `--check` in under a second, three exit codes)
and `seed-canvas.mjs`'s byte-identity test rather than being treated as a small
script. **Copying a precedent past where the precedent reaches is how the second
instrument in this repo usually arrives.**

---

## 5. The two cheap things, and why they come first

Neither is one of the seven candidates. Both came out of the design round.

### 5.1 About 40 lines — #176

Assert in `core/` that **every id a screen direct-pushes from its own `onGesture` is
`Restore::Ready`**. Both halves already exist and are already `static_assert`ed
against `ScreenId::Count`: the push targets (15 `Action::push` sites in `core/`, 12
of them literals) and `restorability()` (`core/src/app.cpp:41`, guarded at `:159`).

`Restore::Ready` already means *buildable from boot configuration alone*
(`core/include/reader/app.h:222`), which is exactly the property a direct push
requires, because `App::dispatch` pushes inside the same call that ran `onGesture`
(`core/src/app.cpp:492`) and the shell gets no moment to prime.

It would have failed the day `core/src/screen_wifi_settings.cpp:140` and `:157` were
written — **two of the three V1.1 dead buttons**. It does not catch the third:
Settings' `Wi-Fi` row was configuration never wired at all, and `WifiSettings` is
`Restore::Ready`, so the assertion passes over it.

### 5.2 Two lines

Delete `return kNames[0];` (`core/src/session_record.cpp:205`) and `return "?";`
(`core/src/app.cpp:273`). Those are the literal mechanisms behind two recorded silent
failures — screens serialising as `"home"` after an append satisfied a bound that
named a member instead of the sentinel (#42), and Sleep missing from `screenName` so
every log line naming it said nothing.

**A reviewer of the manifest should be made to compare against this.** Two lines close
both recorded failures. What they do not close is the simulator's three lists and
`compare-design.py`'s tables, which have no cheap fix and are where the next one will
be — but that is a judgement, not a proof.

---

## 6. The latent fourth — #177

Looking for more instances of the dead-button shape found one that has not fired.

`core/src/screen_reader.cpp:1149` is `return Action::push(endScreen_);`, and
`endScreen_` is `BookEnd` or `ArticleEnd`, **both `Restore::NeedsPriming`**
(`core/src/app.cpp:83`, `:134`). Structurally identical to the two that shipped dead.

**It is safe today because of a coupling nothing states.** `setBookEndFacts` and
`setArticleEndFacts` have exactly one shell call site each —
`shell/src/main.cpp:4726` and `:4758` — and both are inside `openBookAt`, which is
the only route to a Reader. So every Reader on the stack has primed its end screen.

That is true and it is written down nowhere as a requirement. It breaks on a second
route to a `ReaderScreen`, a third `endScreen_` value, or an early return appearing
in `openBookAt` between the walk and line 4726. The failure mode is the one this
project has shipped twice: the last page of a book does nothing.

The assertion in #176 fails on this line, which is the point. The remedy is a real
decision — latch at end-of-book (correct by the rule, costs a loop iteration before
the end screen appears), or make the end screen's facts *per-open configuration*
(keeps today's behaviour, admits a lifetime category that does not exist yet:
set once per open, not once per boot).

---

## 7. Sequencing

Two pieces of advice from two designs looked contradictory and compose:

1. the two cheap fixes (§5)
2. **#175** — independent of everything, and the release gate is currently guarded by
   1,683 lines of test that nothing runs
3. reassess
4. **04 before 03.** If the factory redesign lands first it will invent its own
   29-row list and the repo acquires a tenth table.
5. then 02, then 01, then #174
6. the `Theme` draft is a decision to record, not work to do. If the answer is keep,
   it wants `docs/adr/`, which does not exist — this would be its first entry.

---

## 8. What this audit did not look at

Said plainly, because an audit that reports on less than it claims is the shape this
repo records most:

- **`core/` below the screen layer.** The reader's six layers, the zip/epub/document
  stack, layout, the glyph cache, the image decoders. Sampled for seams, never
  reviewed.
- **Anything the panel can be wrong about.** Every judgement here is desktop-side.
  `docs/on-device-smoke-checklist.md` remains the authority and nothing in this file
  is evidence about glass.
- **The design boards.** 75 `.dc.html` files, reviewed only where a candidate touched
  the compare tables.
- **Performance.** `docs/notes/interaction-cost.md` was read for constraints and not
  audited.
- **Whether `shell/` should get a harness.** Every honest answer in the design round
  ended there, and none of the seven candidates changes it. It is the largest standing
  gap in the repo and it is not on the board.
