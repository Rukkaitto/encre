# A desktop harness for `shell/src/main.cpp`

Designed 2026-09-20 against `5a8a6a0`. The epic is #178; the five phases are
#179–#183. This file is the reasoning those cards point at, and it exists because
`docs/notes/the-board.md` forbids a card from holding it.

**`shell/src/main.cpp` is 9,331 lines and the desktop suite executes none of
them.** The phrase *"shell/ has no harness"* appears **80 times across 28 files**
as a justification for putting logic somewhere else. Five defects have shipped
from that gap; three of them are this design's acceptance criteria.

**The approach is scaffold-first** — build a stubbed desktop build purely to
characterize, extract underneath the net, delete the stubs as real ports take
over. The alternatives were weighed: extracting with no net is how
`gApp->dispatch(ev)` was deleted at `:8375` while 803 tests passed, and a
permanently-faked firmware is a second build whose divergence nothing checks.

---

## 1. Why it is feasible, which was not obvious

Three facts, each verified rather than assumed:

- **`main.cpp` defines no `main()`.** `setup()` (`:6009`) and `loop()` (`:8071`)
  are the entry points. The harness declares them and owns `main()`. No
  `#include "main.cpp"`, no `#define private public`.
- **86 of the 89 top-level definitions are file-static.** The three with external
  linkage are `setup`, `loop` and `readSleepCoverHeader` (`:7368`). So black-box
  driving through two functions is **forced rather than chosen** — and all three
  target defects live inside them, which is the argument that the forced
  interface is also the right one.
- **The SDK surface is small**: 47 distinct method calls, 8 free Arduino/ESP
  functions, 10 types. 19 of the methods are `display.`.

**The file is not split first.** Splitting 9,331 lines of untested code is the
operation the net exists to make safe, and it is the operation CLAUDE.md's
scripted-edit section records three disasters from — including the deleted
dispatch that is acceptance criterion #1.

---

## 2. The three seams, which are three different kinds

Getting this distinction right is what keeps the scaffold from growing:

1. **The Arduino/SDK seam is a compile-time seam — an include path, not an
   interface.** `main.cpp` names `display`, `SdMan` and `millis()` as free
   globals; there is no pointer to swap. The adapter is a directory of fake
   headers placed ahead of the SDK on one target's include path. This is the
   crudest seam in the design and it is temporary by construction.
2. **The shell-adapter seam is a link-time seam.** `input_task.h` declares five
   free functions; `sd_fs.h` declares `SdFileSystem`, `SpiBusGuard` and
   `appendToCard`. Same declarations, a different `.cpp` in the target. Nothing
   behind them needs faking.
3. **The scenario seam** — the harness's own `main()`, which owns the loop that
   Arduino owns on device.

---

## 3. THE RULE THAT KEEPS THE SCAFFOLD HONEST

> **Nothing gets faked in order to compile an already-adapted shell file.**

Seven of the eight non-`main.cpp` files in `shell/` are already adapters at a
`reader::` seam. `input_task.cpp` (80 lines) and `sd_fs.cpp` (773) are replaced by
desktop twins at those existing seams rather than faked, and **that is the only
reason the scaffold is ~1,890 lines and not double**. The moment someone fakes
`SdFat` properly in order to compile the real `sd_fs.cpp`, the scaffold roughly
doubles and design 02's objection — that a fake large enough to host `setup()` is
a second firmware whose divergence nothing checks — becomes decisive.

That objection is correct. The line above is what answers it, and it belongs in
the fake directory's README rather than only here.

---

## 4. Size

| Piece | Lines |
|---|---|
| Fake headers (`Arduino.h`, `EInkDisplay.h`, `BoardConfig.h`, `SdFat.h`/`SDCardManager.h`, `Preferences.h`, and six smaller) | ~1,040 |
| `input_task_host.cpp` | ~70 |
| `sd_fs_host.cpp` | ~280 |
| `wifi_radio_host.cpp`, `http_transport_host.cpp` | ~150 |
| `harness_main.cpp`, scenario registry, transcript normaliser | ~350 |
| **Total** | **~1,890** |

No `WiFi.h`, `HTTPClient.h`, `NetworkClientSecure.h`, `freertos/*` or `WString.h`
is faked. The first three are reached only from the two Arduino transport
adapters (replaced by twins) and from `main.cpp`'s `ENCRE_WALLABAG_PROBE` block,
which the desktop does not compile. FreeRTOS is reached only from `input_task.cpp`
and `sd_fs.cpp`, both replaced.

### Five things harder than they look

- **`BoardConfig::ACTIVE` is a mutable global written and then read back.**
  `:5016` selects the X3 profile, `:5019`'s probe mutates
  `ACTIVE.displayController`, and `:5026` reads it to decide the `XteinkX3Uc8279`
  promotion. The fake needs that write-then-read or the promotion branch is
  untestable. **And nothing checks the field names**: if the SDK renames one, the
  fake keeps compiling and the desktop keeps passing. Only `make firmware`
  catches it, which is why every PR must be green on both builds.
- **`InputManager`'s async queue is not faked at all**, and that is the single
  largest saving. The cost, stated: the SDK's debounce and the 32-deep queue's
  drops are not modelled, so `rawSamplesDropped()` is **scripted, not emergent**.
  The loop's dropped-edge branch stays drivable; the desktop can never *discover*
  a drop.
- **The `Preferences` fake should refuse a key over 15 characters**, which the
  real API does not — a too-long key fails silently on device. That makes the fake
  **stricter** than the real thing, which is the opposite direction from
  `fake_fs.h`'s standing warning that a forgiving fake makes every test above it
  meaningless. Strictness is right here and needs a comment saying so, or the next
  reader will "fix" it.
- **`getFrameBuffer()` must return real memory, and that is a feature.**
  `bindFrameToDriver` refuses a view whose `sizeBytes()` disagrees with
  `getBufferSize()` (`:5135`), so the fake has to be dimensionally honest — and in
  exchange every render really writes pixels, so the shell's paint path becomes
  golden-testable for the first time.
- **One scenario per process.** Every piece of state in `main.cpp` is a
  file-static with a boot-time initialiser and there is no reset function; writing
  one would be a change to untested code before the net exists. Each scenario is
  its own `add_test`, which is the idiom `reader_sim` already uses — nine
  `add_test`s over one binary.

---

## 5. Determinism by construction, not by filtering

A post-filter is a second place the truth lives, so the transcript is made
deterministic at the source:

- **The clock is virtual**, 0 at entry, advanced only by the panel model's
  per-waveform charge and explicit script steps. Every `%lums` field becomes a
  checkable number, and the `[i]` line's `wait= pre= disp= post=` fields become a
  *behavioural* assertion about ordering rather than a timing one.
- **The heap is a scripted model**, so `heap=`/`min=` are deterministic *and* the
  heap-floor branches become drivable.
- **Exactly one field is normalised**: `[frame] ... viewing driver framebuffer
  %p` (`:5147`). Say in the normaliser's comment that it is the only one.

**A golden of the timing fields without a fixed clock would be a flaky golden, and
a flaky golden gets re-blessed** — which the Goldens section forbids for exactly
this reason. The virtual clock is not a convenience; it is what keeps the
transcripts honest.

`[[noreturn]]` paths throw. The desktop build has exceptions — the firmware's
`-fno-exceptions` comes from the Arduino framework, not from `CMakeLists.txt` —
so `deepSleepUntilPowerButton()` and `esp_restart()` throw, the harness catches,
and a sleep/wake scenario re-enters `setup()` with the NVS fake intact and the
reset reason changed. It lives entirely in the fake; if it ever leaks into
`shell/src/`, `make firmware` fails, which is the check.

---

## 6. #94, caught two ways, and the one that needs no model

The panel fake carries a model of `Uc8279Driver`'s seven-flag state machine, so
its per-refresh record is one line: `<panel> refresh bank=GC|DU seed=white|none`.
That reproduces CLAUDE.md's own #94 analysis exactly — with the assertion
re-inserted, `_forceFullSyncNext` is still true from `:6196` with no refresh in
between, so `bank` is unchanged and only `seed` flips `white → none`: *"it spent
the one thing that makes the clear clean and got no cheaper refresh for it."*

**But the model is a second copy of private state, and that is the design's
largest fidelity risk** (§7). So the fake also asserts a weaker property that is
true regardless of the driver's internals:

> **No caller may call `skipInitialResync()` unless this process has completed a
> refresh since the last `begin()`.**

**There are exactly two live call sites and both satisfy it** — `:6831` (after
`showOnePass`) and `:7056` (after `renderTop`). Seven other mentions in the file
are comments. The #94 form at `:6796` sat 661 lines after `begin()` at `:6135`
with no refresh between, and violates it.

That assertion is about what the **shell** knows, which is what the shell is
responsible for, and it survives any SDK change.

---

## 7. Fidelity: where `fs_contract.h`'s shape transfers, and where it stops

`core/include/reader/fs_contract.h` is this repo's existing answer to *how do you
know a fake behaves like the real thing*: 28 clause functions over a report sink
that knows nothing about doctest or Serial, driven by `test_filesystem.cpp:79` on
the desktop and by `sd_selftest.cpp:187` against a real card over Serial.

`panel_contract.h` copies it. Six clauses are assertable from both sides: buffer
size equals `w*h/8`; `getFrameBuffer()` non-null after `begin()` and stable across
refreshes; geometry matching the profile `selectDevice` chose; `triggerDisplay`/
`completeDisplay` pairing and a second trigger before a finish being refused; the
grayscale call order refusing an out-of-order use; and
`supportsStripGrayscale()` reporting what the driver **body** says — which pins
which of the SDK's two disagreeing sources is the authority.

**Where it stops.** `_oldPlaneValid`, `_forceFullSyncNext`,
`_initialFullsRemaining`, `_lsbValid` and `_inGrayscaleMode` are private in
`Uc8279Driver.h:92-107` with no accessor, and `freeink-sdk/` must never be edited.
**No runner can read them back**, so §6's model is a second copy of a state
machine nothing can verify. That belongs in a comment at the top of the fake.

One indirect check exists: the GC-vs-DU bank decision has an observable
consequence on glass, because `EpdBus.cpp:293` prints the waveform duration and
693 ms distinguishes GC from DU's 389 ms. **That is an inference and must be
labelled one** — the same kind of claim as reading a controller identity off a
30-second timeout.

**The cheap check that ships for free**: the fakes are on one target's include
path and `make firmware` compiles the same `main.cpp` against the real headers on
every PR. So **signatures are checked and semantics are not.** That split is the
honest statement.

---

## 8. Coverage, with its denominator

`main.cpp` is 9,331 lines; non-blank and non-comment-only is **3,482**. The
desktop does not compile three `#ifdef` regions — `ENCRE_WALLABAG_PROBE` (208
code lines), `ENCRE_LIBRARY_PROBE` (41), `ENCRE_BODY_SWEEP` (43). **Working
denominator: 3,190.**

**Excluded on principle — executed, never evidence.** The ~73 hardware statements,
plus roughly 120 lines of bring-up *ordering* whose correctness is a fact about
this silicon: why `SPI.begin()` runs inside `detectAndSelectBoard()` before the
driver owns the pins, why the card mounts after `display.begin()`, why the wake
gate and the charge gate both sit *before* it so a refused wake spends no
waveform. A desktop test asserting those would be asserting the fake.

So the **ceiling is 2,997 lines, 94% of the compiled file** — a ceiling, not a
target. After the first ten scenarios, **~36%**; after the full ~35-scenario set,
**~75%**, the remainder being the wallabag/sync handlers and the cover-cache
writer, each needing its own fixture.

**The number that matters is not the percentage.** Today the desktop executes
**0** of those 3,190 lines.

---

## 9. What this does not claim

**It does not make `shell/` safe. It makes `shell/` characterized** — the
transcripts say what the firmware does today, not what it should do.

**Every scenario would have been green on the firmware that shipped the three
V1.1 dead buttons**, because those were configuration never wired, not behaviour
that changed. That is `architecture-audit.md` §3.1's finding and it applies here
unchanged.

**A green transcript is a new way to feel covered** — the `make compare`
`ok`-versus-percentage shape, the card probe answered from cache, the `--only`
that matched nothing and exited 0. Two structural mitigations: the transcript is
deterministic *by construction*, so a scenario that stops exercising a line
produces a **different** transcript rather than the same one — the `--only`
failure mode is not available; and `docs/on-device-smoke-checklist.md` gains one
sentence naming what the harness does not replace.

**The harness must never be quoted as evidence for a card moving from `On glass`
to `Done`.**

Unassertable from the desktop at all, so nobody has to re-derive it: the waveform,
ghosting, ink accumulation, the DTM1 baseline on glass; **rotation**, because the
whole desktop is `Rotation::None` and the five byte-wise primitives pass every
golden while smearing on glass; SPI contention between card and panel (the harness
can assert the guard is *held*, not produce the fault); USB-CDC enumeration and
the `ESP_RST_USB` wake confusion; real FAT behaviour; and every timing in
`docs/notes/interaction-cost.md`.

---

## 10. The retirement, and the failure mode it guards

**The real risk of scaffold-first is that the extraction never happens** and the
repo is left with the permanently-maintained second firmware the approach
promised to avoid. #183 is what makes that visible rather than quiet:

> `wc -l test/shell/fake_arduino/*.h` must strictly decrease at every phase-4 PR,
> and CI prints the number the way `make compare` prints the mismatch percentage —
> a figure in front of a reviewer, not a gate.

The six ports are ordered by the deletion test: extract the port whose fake, once
the real port exists, has nothing left to fake. `reader::Panel` first, because it
is also audit candidate 02 — the grayscale pass order goes from three copies to
one, and #94's false claim becomes **unwritable** rather than merely caught.
**The scaffold and that candidate are the same work, done in the right order.**

The invariant that keeps both halves honest mid-flight: every PR green on
`make test` **and** `make firmware`, and **the transcripts do not change except in
the PR that intends them to**. A transcript that moves under a refactor is either
a behaviour change — stop, it was meant to be a refactor — or a transcript pinning
an implementation detail, which is a granularity bug to fix and say so. That is
`make compare`'s bargain, one level down.

When all six land there is no fake SDK, only test adapters at `reader::`/`shell::`
seams, exactly like `FakeFileSystem` and `FakeHttpTransport` are today.

**And the sentence gets rewritten, not deleted.** *"shell/ has no harness"* stops
being true in its absolute form at phase 2; its useful form — that the panel, the
bus, rotation and the silicon are still unreachable — stays true. 80 occurrences
across 28 files makes that a scripted edit, with the scripted-edit rules in full.
