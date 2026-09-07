# Battery states: the low banner and the critical shutdown

**Cards:** [#9 Low battery banner](https://github.com/Rukkaitto/encre/issues/9)
(`LowBattery.dc.html`), [#10 Critical shutdown](https://github.com/Rukkaitto/encre/issues/10)
(`BatteryEmpty.dc.html`). Both V1, both `Boarded`. `roadmap:1187` — Phase 5,
"battery states (low banner, critical shutdown)". **Both cards' `Source` says
`roadmap:1089`, which is 98 lines stale**; a line number is a pointer with an expiry
date, and the section it names has moved.

**Two cards, one mechanism.** They are two rungs of one ladder and share the piece
that does not exist yet: a battery reading taken on **every** screen. Building #10
alone would build the whole watcher and use only its top rung, and #9 would then
re-open it to add a second threshold. The second consumer is also what proves the
abstraction — this project's own rule is that the second copy is the extraction
point, and here the second consumer arrives in the same change as the first.

---

## What is there today, and why it cannot serve

`refreshBatteryOnHome()` (`shell/src/main.cpp`) is gated on
`homeOnGlass() && gChargingObservable`:

- **`homeOnGlass()`** — so nothing outside Home ever reads the gauge. A reader who
  has been in a book for an hour has had no reading taken at all.
- **`gChargingObservable`** — never true on an X4, which has no charge-status pin,
  so on that model nothing reads the gauge **ever**.

Both gates are right for what they guard (the charge-latch repaint, which is about
the bolt on Home's band). Neither can be reused for a safety mechanism.

## Three hardware facts that shape everything below

**The X4's resolution is 10% notches.** `percentageFromMillivolts` walks
`LIION_NOTCH_MV[11]` and returns a multiple of ten, rounding at each segment's
midpoint. So the board's `BATTERY LOW · 5%` is the *displayed* value and can never
be a *trigger* on that model — the reachable values below 20% are `10` and `0`.

**`0%` is real and is distinguishable from a failed read.** The ADC path sets
`millivoltsKnown = millivolts > 0` and only then computes a percentage, so a failed
read yields `percentageKnown == false` rather than a confident `0`. The 0% anchor is
3.45 V, deliberately above the cell's protection cut-off — the SDK's own comment says
it "leaves headroom for the sag under an e-ink refresh, which is the heaviest load
this device draws".

**There is no shutdown, only deep sleep.** The board's `CHARGE TO WAKE` cannot be
enforced by the SoC: the wake source is the power button and there is no
charge-detect wake anywhere on that path. It has to be made true **after** the wake,
exactly as `HOLD POWER TO WAKE` is made true by
`requireHeldPowerButtonOrSleepAgain`. And it cannot gate on `charging`, because an
X4 never reports it — so it gates on a **voltage hysteresis band**, which both
backends can answer.

---

## 1 · The watcher

`BatteryTracker` (`core/include/reader/battery_tracker.h`) grows a level ladder. It
already owns *"what the band should say, and whether a change of charge state is
worth a repaint"*, and it already takes the one `BatteryReading` the shell produces.

```cpp
enum class BatteryLevel : uint8_t { Normal, Low, Critical };
```

**Why not a second object.** A `BatteryGuard` fed the same reading would be a caller
list — the shape CLAUDE.md names as "a function not yet written" — and the shell
would be free to feed one and forget the other, which is precisely how a stale claim
reaches the glass. One `update()` cannot be half-fed. It stays in `core/` for
`ProgressSaveGate`'s reason: this is a latch with a dwell and a session-independent
threshold, and `shell/` has no test harness.

### The constants, and why each is the value it is

| constant | value | derivation |
|---|---|---|
| `kLowPercent` | `10` | the X4's lowest non-zero notch, ~3.68 V. A lower figure is unreachable there until the pack is already at `0`. |
| `kCriticalPercent` | `3` | X4-reachable only as `0%`, ≤3.565 V. On an X3 it is read literally and leaves minutes. |
| `kResumePercent` | `15` | the X4 must reach the **20%** notch, ~3.71 V. That is **145 mV of hysteresis** against the 3.565 V shutdown edge. |
| `kCriticalDwellMs` | `10000` | continuous, in `kUnlatchMs`'s idiom. |

**The hysteresis is the load-bearing number.** With `kResumePercent` set anywhere
that an X4 can satisfy at the `0`/`10` boundary, the shutdown edge and the resume
edge are the *same* 3.565 V midpoint — a device on the cable would shut down, charge
for a minute, wake, discharge, and shut down again. Requiring the next notch up is
what makes the two edges different voltages.

### The rules

- **`Critical` requires the dwell.** The condition must hold *continuously* for
  `kCriticalDwellMs`, measured against a caller-supplied `nowMs` with an unsigned
  difference, as every quiet-window gate in the shell is. One sagging reading cannot
  shut the device down.
- **Sag is already largely excluded upstream.** The poll runs only in the `quiet`
  window and the loop is blocked for a whole paint, so no sample is ever taken
  mid-waveform. The dwell is the belt to that braces.
- **Charging suppresses `Critical`.** On an X3 `charging()` is known and a device on
  the cable must not shut down. An X4 never reports it, so there it never
  suppresses — and shutdown-then-refuse-to-wake is exactly the right behaviour for a
  flat X4 on a cable: the glass says `BATTERY EMPTY / CHARGE TO WAKE`, and it does.
- **`percentKnown == false` holds the level where it was.** "Flat" and "did not
  answer" stay different claims, as they already do for `percent()`'s
  `kUnknownPercent`. A transient I2C miss must not shut the device down.
- **`Low` has no dwell.** Its cost of being wrong is one banner, not a shutdown.

## 2 · The reading, on every screen

`readBattery()` gains a second caller. Both go through `gBattery.update()`, so the
level and the band can never disagree about the percent.

- **`pollBatteryLevel()`** — every `kBatteryPollMs` (2 s) in the `quiet` window, on
  **any** screen, with **no** `gChargingObservable` gate. ~450 µs of I2C on the
  sensor bus, which cannot race a panel refresh; that is what already makes 2 s
  affordable and it needs no `SpiBusGuard`.
- **`refreshBatteryOnHome()`** — unchanged. It keeps its Home gate, because what it
  drives is the band and the charge-latch repaint.

`gChargingObservable` keeps its existing job and gains nothing.

## 3 · The shutdown, and the refusal to wake

**`criticalShutdown()`** is `sleepNow()`'s shape, and the ordering is forced by the
same hardware:

```
saveReadingPosition("battery")
paintBatteryEmptyScreen()          // one FULL waveform, ~825 ms
release the App
display.deepSleep()
PowerManager::powerDownRailsForSleep()
markSleeping()  +  markCriticalShutdown()
flush the card log
deepSleepUntilPowerButton()
```

`markSleeping()` **too**, deliberately: once charged, the wake should restore the
reader's page rather than starting cold. The board's copy — *"Your page is saved"* —
is a promise the save above keeps and this flag is what redeems.

### The resume gate

A new gate in `setup()`, immediately **after** `requireHeldPowerButtonOrSleepAgain`
and still **before `display.begin()`**:

> if the `critShut` NVS flag is set and `percent < kResumePercent`, re-arm both flags
> and sleep again.

- **Before `display.begin()` is the whole cost of the feature.** E-ink holds its last
  image, so the glass still shows `BATTERY EMPTY` — a refusal repaints nothing and
  **spends no waveform**. One line later, past the panel bring-up, and every brush
  against the button on a flat device costs a flash. This is the wake gate's argument
  verbatim.
- **After the hold gate**, because that is the cheaper refusal and already stands. A
  bag-brush should be refused for the *hold* reason without spending an I2C read.
- **After `detectAndSelectBoard()`**, because it needs the profile — and because
  `readStatus()` tests `BoardConfig::ACTIVE.batteryGauge.gaugeAddr` **live** rather
  than from `BatteryMonitor`'s cached members, which is what makes the file-scope
  static safe here. The X4's ADC members matching the compile-time default is a
  coincidence this feature inherits and does not create.
- **The flag is what makes a strict `≥15%` legal.** Without it the gate would have to
  sit at the critical threshold itself, which flaps; and a gate at `15%` applied to
  *every* boot would refuse a device sitting at a perfectly usable 10%.
- **Read-and-cleared, and given back on refusal.** One flag buys one resume, exactly
  as `slept` does — a boot that sets out to refuse and then panics must not refuse
  for ever, and a refused wake did not spend it.

`[boot] battery pct=N critShut=N -> RESUME|refused` prints the whole decision, in
`[boot] reset reason=… slept-flag=… -> RESUME|cold start`'s idiom. Read that line
before believing anything about a device that will not turn on.

### The cold boot on a flat pack

No `critShut` flag, so the gate does not fire. The device boots, paints Home, and the
watcher shuts it down `kCriticalDwellMs` later. One waveform is spent; the
alternative is a device that appears to do nothing when you press power, which is
indistinguishable from a brick.

---

## 4 · The banner

### The board comes first, and it is stale

`LowBattery.dc.html` **must be rebased onto `Reader.dc.html` before any code is
written**: it has 40px margins where the Reader uses 18, `CH. 01` where the Reader
now draws a chapter name from the table of contents, and none of the 676/652.8
column reasoning. This is the same fix `ReaderMenu.dc.html` needed when another
branch changed the page under it — and `make compare` said `firmware ok` throughout,
because "ok" means a frame was produced.

The rebased board carries a **full 12-line page** with the banner drawn over its
bottom, so it honestly shows what is covered.

### It draws over the page and never displaces it

`readerMetrics` derives `columnH = panelH - kReadPadTop - headerH - footerH`, and
`PageBuilder` seats `rowsThatFit(columnH, lineBox)` lines in it. Honouring the
board's 78px band *inside* that column takes a default page from 12 lines to 10 and
**re-paginates the whole chapter** — a `relayout` at the exact moment the device has
least energy to spend, and it moves the reader's page under them.

So the banner is drawn at `footerTop - kBannerH`, which is where the board puts it,
and `columnH` is untouched. `ReaderViewModel::anchorLabel` already carries this
reasoning for the footer's third field: *"it costs no vertical space, which matters
more here than anywhere else on the device: a footer that changed height would
reflow the text column and re-paginate the chapter mid-read."*

### It is transient, and `ANY BUTTON` is a binding

The board's right slot says `ANY BUTTON`, and this project's rule is that a bar
cannot promise what nothing has bound. So:

- **The latch is `ReaderScreen`'s, not the shell's.** `setBatteryLow(int percent)`
  arms it — **one argument, `-1` to disarm, the same sentinel the view model
  carries**, because a `(bool, int)` pair would spell the condition twice two
  paragraphs after this spec argues it must be spelled once; `onGesture` clears it and returns `Action::redraw()` **whatever the
  gesture**. The whole mechanism is then inside `core/`, where a test can reach it —
  a shell-side latch would be untestable, and `shell/` is where five bugs have
  hidden.
- **The first press dismisses and does nothing else.** Deliberate, and it is what the
  bar promises. `Back` on the Reader pops out of the book, so swallowing it is the
  case that matters most.
- **Power is not swallowed**, because the shell handles it before dispatch. The
  device sleeps and the banner is gone with the RAM, which is correct.
- **It re-arms on a fresh entry into `Low`**, not per session. A wake is a chip reset,
  so a low battery shows the banner again on every wake — which is the right
  behaviour and, when the Reader is what the wake restores, rides that paint and
  costs no extra waveform.
- **A banner armed under a `Peek` or the reader menu waits.** The Reader is not on
  top, so its `onGesture` is not called; the banner appears when the overlay closes.

### What it draws

`ReaderViewModel` gains `batteryLowPercent`, **−1 meaning no banner** — one field,
so the condition cannot be spelled twice. From the board: full-bleed, 78px, inverted;
`padding: 0 18px`; left group is the warning triangle then a 12px gap then
`BATTERY LOW · N%` at `--t-meta`/700/0.1em; right is `ANY BUTTON` at
`--t-meta`/0.1em.

**This paragraph said `--t-label`/700, and that role does not exist** — the ramp
carries `Label400`/`Label500` at 11pt and `Meta400/500/700` at 10pt
(`font_manifest.h`), so the spec as first written asked for a pre-rendered asset
nobody has, which is unbuildable rather than merely terse. **The X4's fit is what
chose between the two roles that do exist.** The widest string the banner can draw
is `BATTERY LOW · 10%` — the X4 has no fuel gauge and its ADC quantises to 10%
notches, so 10 is the only value it ever shows — and measured in Chrome at that
string the gap between the two runs is **0.00px at `Label500` 23px/500, with both
runs wrapping to two lines inside a 78px band**, against **27.11px at `Meta700`**
(X3: 42.59 against 75.11). `Meta700` also restores the weight the board originally
asked for.

**The `padding: 0 18px` is an alignment, not a shave.** The page's box is
`padding: 20px 18px 0 18px`, so the header and the footer both begin at x=18; the
band is full-bleed and at 24px its runs lined up with nothing. The 24 was orphaned
from the pre-rebase board's 40px margins. The 12px it returns is what takes the X4
clear — and after the firmware's ~3% wider `.rfnt` advances the X4 gap is ~15.9px,
which is the figure that has to stay positive on glass. The 32px icon and the
0.1em tracking are this spec's and are not levers.

## 5 · `BatteryEmpty`

`BatteryEmpty.dc.html` needs no geometric change. It is `SdMissing`'s shape without
the action slab, plus `Sleep`'s badge: a centred column with a 22px gap holding a
98×52 near-empty battery, `BATTERY EMPTY` at `--t-title`/700/0.06em, and a prose
paragraph at `--t-body`/1.55 capped at 400px.

**`drawBadge` gets extracted into `components.h`.** `BatteryEmpty`'s
`CHARGE TO WAKE` badge is byte-identical to `Sleep`'s `HOLD POWER TO WAKE` — 34px
from the bottom, 1px border, 8/18 padding, `--t-meta` at 0.2em. That is the second
copy, which is this project's extraction point, not the fifth. `renderSleep`
migrates to it in the same change.

**Two new assets**, both `iconc.py` entries keyed on `source` exactly as
`kBook`/`kBookLarge` are:

- **`kBatteryLarge`** — the near-empty battery, 98×52, from `BatteryEmpty.dc.html`.
  A second asset for the same drawing, because these are pre-rendered bitmaps and
  there is no scaling one up. `source` has to disambiguate it from `kBattery`, whose
  matcher already keys on path data both boards carry.
- **`kWarning`** — the triangle, 32×28, from `LowBattery.dc.html`. Authored **white**
  for the inverted band, as `kForward` already is.

**`ScreenId::BatteryEmpty` is APPENDED**, for the reason `ReaderMenu`, `Typography`,
`Peek` and `BookEnd` were: the session record stores a screen by name, so an
insertion could not silently become another screen, but appending also leaves every
existing ordinal where it was.

**It is painted directly and never pushed**, on `Sleep`'s argument — the session
record names the top of the stack, so pushing it would make the next wake restore
*into* it. `paintBatteryEmptyScreen()` therefore owns the two things `App` normally
does: the **clear**, and setting `gFrameContentsUnknown`, because `App`'s
partial-repaint record would otherwise describe a frame that no longer exists.

**It takes no input and draws no hint bar.** The shell paints it and calls deep
sleep, so there is nobody left to press anything, and a bar is a contract about four
buttons that do nothing. `onEvent` answers `none()` even for Back.

---

## 6 · What proves it

| | |
|---|---|
| `test_battery_tracker.cpp` | the ladder: the dwell, the hysteresis, charging suppression, and `percentKnown == false` holding the level. Extends the existing file. |
| `test_screen_reader_battery.cpp` | the banner latch — armed, mirrored into the view model, dismissed by **each** gesture in turn, not re-armed until the level leaves `Low`. |
| goldens | `low_battery` and `battery_empty` at **both geometries**, four files. `low_battery` goes through `checkGoldenGray` (the Reader declares `Fidelity::Grayscale`); `battery_empty` through `checkGolden` (`Mono`). |
| `make compare` | both ids stop reporting not-implemented. They are already named in `compare-design.py`, so nothing is added to that list. |
| `ENCRE_BATTERY_FAKE_PERCENT=n` | a build flag over `readBattery()`, in `ENCRE_FS_SELFTEST`'s shape and absent by default. Draining a real pack to 3% on demand is not practical, and without this the whole ladder is unwalkable on glass. |

**Every golden is proved by mutation, not by passing.** Moving the banner's y by a
line box, dropping the `−1` sentinel, and drawing the badge at Sleep's y on a screen
whose prose is a different height must each fail a named count.

### Two guards that will not notice this change

- **`test_focus_restore.cpp`'s `static_assert` compares against
  `ScreenId::Peek + 1`** — a *named* member, not the last one — so appending
  `BatteryEmpty` satisfies it unchanged, exactly as appending `Typography` and then
  `BookEnd` did. #42 is the general fix and stays open; the catalogue is extended by
  hand here.
- **`core/src/session_record.cpp`'s name table must grow with the enum**, or
  `sessionWireName` falls through to `kNames[0]` and the new screen serialises as
  `home`. Its table is `static_assert`ed against the enum's end, so this one fails
  the build rather than going quiet.

## 7 · Where this stops

**Cards #9 and #10 move to `On glass` and no further.** Nothing on the desktop can be
evidence for a deep-sleep path, an NVS flag surviving a chip reset, or an I2C read
taken before `display.begin()` — and `shell/` has no harness, so **not one line of
the resume gate is executed by the desktop suite**. Same standing as the
hold-to-wake gate, which needed glass and got it. `docs/on-device-smoke-checklist.md`
gains the ladder.

What needs verifying on the panel, and cannot be verified anywhere else:

1. The banner appears over the page without moving the text, and one press clears it.
2. `criticalShutdown` reaches the glass — the `BATTERY EMPTY` paint completes before
   the rails go down.
3. The resume gate refuses below `kResumePercent` **repainting nothing**, and the
   `critShut` flag survives the reset and is given back on each refusal.
4. Once charged past `kResumePercent`, the wake restores the reader's page.
5. An X3 on the cable at a critical percentage does **not** shut down.

## 8 · Stated limits

| limit | why |
|---|---|
| A device refused at 14% cannot be forced on, on either model, even plugged in. | `CHARGE TO WAKE` taken literally. It is the price of the hysteresis that stops the flap, and softening it re-opens the flap on the X4. |
| An X4 gets **one notch** of warning — `10%`, then `0%`. | The backend's resolution. A finer threshold is not expressible; a coarser one (`≤20%`) would leave the banner up for a long time on a battery that is not alarming. |
| The banner is the **Reader's only**. | It is where a reader spends their time and where an unannounced shutdown costs most; Home already states the percentage in its band. Putting it on every screen means a banner on every board first, which is a large design change. |
| The first press after the banner appears is spent dismissing it. | What `ANY BUTTON` promises. |
| A cold boot on a flat pack spends one Home paint before shutting down. | The alternative is a device that appears to do nothing when you press power. |
