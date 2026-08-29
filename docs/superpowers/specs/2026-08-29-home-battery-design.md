# Home's battery — a real percentage, and a charging mark

**Status:** design approved 2026-08-29. Not implemented. Issue
[#46](https://github.com/Rukkaitto/encre/issues/46), which is the reading half of
[#25](https://github.com/Rukkaitto/encre/issues/25) (`roadmap:1174`, Phase 5).

**What is wrong today:** `HomeViewModel::batteryPercent` is set to `87` in all three
demo view-models (`core/src/screens.cpp:23,37,66`) and **nothing in `shell/` ever
writes it**, so Home's header band reports a fixed 87% whatever the charge. Home is
the only screen affected: the two consumers are both in `renderHome`
(`core/src/theme_quiet.cpp:134` and `:183`).

**`BatteryMonitor` is already a declared dependency** (`platformio.ini:38`) and has
never been constructed. Nothing needs vendoring; this wires up a library the build
already links.

## What this is not

Scoped to the **percentage and the charging state on Home**. Explicitly out:

- `design/LowBattery.dc.html` and `design/BatteryEmpty.dc.html` — boarded, unbuilt,
  and each needs a threshold policy that is its own decision. Phase 5, their own cards.
- The RTC half of #25.
- Any battery indicator on a screen other than Home. No other board draws one.

## The read path is already proven on this device

**`XteinkDetect::probeBq27220` reads the gauge at every boot** — SoC and voltage off
the BQ27220 at 0x55, validated as `soc <= 100` and `mv` in 2500..5000
(`XteinkDetect.cpp:447`). X3-vs-X4 detection needs two of its three I2C chips to
answer on both passes, so **the panel driver this firmware selects already depends on
this gauge responding.** That is a stronger prior than any desktop test could give.

Two backends behind one API, chosen at *runtime* per profile so one C3 binary serves
both models:

| | X3 | X4 |
|---|---|---|
| backend | BQ27220 I2C gauge, 0x55 on SDA20/SCL0 | ADC on GPIO0, divider 2.0 |
| percentage | true SoC, per 1% | `percentageFromMillivolts`, **multiples of 10 only** |
| charging | sign of the gauge's `Current()` | **impossible** — see below |

`FREEINK_BATTERY_I2C_GAUGE` is auto-on because `platformio.ini` sets
`-DFREEINK_DEVICE_X3=1` (`BoardConfig.h:240`). Nothing to add to the build.

**THE GRANULARITY DIFFERENCE IS REAL AND IS NOT A BUG.** `percentageFromMillivolts`
quantises deliberately — its own comment says voltage "cannot resolve a Li-ion pack
any finer than that, and pretending otherwise just produces a number that wanders
while the battery sits still". So an X3 reads `64%` and an X4 reads `60%`. The boot
log has to name which backend answered, or the next person to compare two devices
will file a bug against the honest one.

**THE CHARGING MARK CAN NEVER APPEAR ON AN X4.** `XTEINK_X4` declares `NO_GAUGE` and
`batteryChargeStatus = PIN_UNASSIGNED`, so `isCharging()` returns `false`
unconditionally there. This is a stated limitation, not a defect to chase.

**`BoardProfile::usbDetect` IS NOT A PLUG-IN SIGNAL AND MUST NOT BE USED AS ONE.**
Both Xteink profiles set it to `20`, positionally, with no comment — where every
other board's entry carries one (`5, // usbDetect GPIO5, HIGH = USB present`).
**Nothing in the SDK reads the field**, and on the X3 **GPIO20 is the fuel gauge's
SDA**. It is inherited, not wiring. Recorded here so it is not rediscovered as a
shortcut.

## What changes on the panel

Home's header band, three states, one mark position, nothing else moving:

| State | Right slot |
|---|---|
| Read succeeded, not charging | `64%` + solid-fill battery — the shipped layout, real number |
| Read succeeded, charging | `64%` + battery with a **white bolt knocked out of the fill** |
| Never read successfully | the battery mark **alone**, no number and no gap |

### Why the bolt is knocked out rather than replacing the fill

Chosen over the alternative (outlined body with a *solid* bolt instead of the solid
block) because it is the conventional treatment and the two idle/charging states share
a silhouette.

**The risk is stated rather than settled:** the knockout is roughly **24×14 device
pixels of white inside black**, at the size this glass serves worst, and CLAUDE.md
records that thin diagonals come out one notch lighter under `Mono`. **This is an
`On glass` item.** If the bolt closes up on the panel, the fallback is the
fill-replacing variant, which is the same board edit and the same `make icons` run.

### Why blank, and not 0%

`homeVmForCard()` already makes exactly this call for the LIBRARY row: `-1` means the
directory could not be read and the row shows **nothing** rather than a `0`, because
*"no books" and "could not look" are different claims, and the second one is not ours
to make on the user's behalf.* A battery is the same shape of claim, and worse to get
wrong — `readPercentage()` returns `0` on a failed read and
`percentageFromMillivolts` maps a failed `0 mV` to `0%` rather than `100%`, so taking
either at face value puts **"flat battery" on the panel of a device that is fine**.
`readPercentageChecked()` exists precisely so a caller need not.

**The blank state gets no board.** It is an absence, not a design — the same call the
LIBRARY row already ships without one. It gets a core unit test instead.

## The boards come first

Per CLAUDE.md a UI change goes into the design HTML before the implementation, and
here `iconc.py` **enforces** it: the generator reads each icon's SVG, viewBox and size
out of its named board at generation time, because it once held copies and silently
swallowed a design fix.

### A new board: `design/HomeCharging.dc.html`

`Main.dc.html` with the charging mark. It has to be its own board:
`iconc.py`'s `battery` entry matches `<rect x="19.5"` — the terminal nub — and a
second battery on `Main.dc.html` makes that match ambiguous, which `iconc.py` reports
as `2 <svg> elements in ... contain ...` and exits on. **`source` is what
disambiguates**, exactly as it already does for `kBook` and `kBookLarge`, which are
the same path at two sizes on two boards.

It also earns its keep as a board rather than a comment: it becomes a `make compare`
row and a golden pair, so the mark is checked against its design.

New `iconc.py` entry:

```
"battery_charging": {
    "symbol": "kBatteryCharging",
    "note": "the header band's charge cell, bolt knocked out: charging",
    "source": "design/HomeCharging.dc.html",
    "match": '<rect x="19.5"',
},
```

`design/canvas.json` gains the board, and `tools/compare-design.py` gains
`("home_charging", "HomeCharging.dc.html", "Home / charging")`.

**The other three battery boards are untouched.** `Main`, `HomeEmpty`,
`HomeUnopened` and `HomeMissing` all draw the idle mark and keep it. The charging
mark is the same mark in the same place on all of them, so boarding it once is
enough; what must not happen is the *code* choosing it in more than one place —
see below.

## `core/`

### `HomeViewModel` (`core/include/reader/viewmodel.h`)

- `batteryPercent` gains the **`-1 = unknown`** convention, and its default flips
  `0` → `-1`. A view model nobody told about the battery must not claim a flat one.
- `bool batteryCharging = false;`

The three demo view-models keep their literal `87`, exactly as they keep the board's
`12` for the LIBRARY row — the board's number is right for the goldens and for the
design comparison, and wrong on a device, which is why the shell patches it. **Every
existing golden is therefore untouched.**

### `HomeScreen::setBattery(int percent, bool charging)`

Mirrors into `vm_`. Same shape and same reason as `LibraryScreen::refreshProgress()`:
a view model built once, and a fact about it that moves afterwards.

### `renderHome` resolves the mark ONCE

```
const Icon& batt = vm.batteryCharging ? icons::kBatteryCharging : icons::kBattery;
```

at the top, used by both draw sites — the header band and the `nothingToContinue`
strip. There are exactly two, and this project's rule is that a choice made in two
places is a choice that will eventually be made differently in the two places.

### `drawHeaderBand` needs NO change — CORRECTED 2026-08-29

**An earlier draft of this spec specified a fix here and the fix was wrong.** It is
recorded rather than deleted, because the wrong version is the one an inattentive
reader will re-derive.

The claim was that `drawHeaderBand`'s

```
const int groupW = vw + (mark ? kBandGap + mark->w : 0);   // components.cpp:101
```

reserves a 7px gap for a number that is not there when the value is empty, and so
pushes the battery 7px off the right margin. **The first half is true and the second
is false.** The icon draws at `groupX + vw + kBandGap` (`:126`), so the phantom gap in
`groupW` **cancels against the same gap** in the icon's own x. Worked at 480 wide,
`kMargin` 24, `kBandGap` 7, `kBattery.w` 38:

| | `groupW` | `groupX` | icon right edge | `labelMaxW` |
|---|--:|--:|--:|--:|
| value `64%` (vw 52) | 97 | 359 | **456** ✓ | 328 |
| empty value (vw 0), as shipped | 45 | 411 | **456** ✓ | 380 |
| empty value, with the proposed "fix" | 38 | 418 | **463** ✗ | 387 |

**The proposed fix introduces the bug it was written to remove.** Correcting it
properly needs *two* edits — `groupW` and the icon's x — and buys `labelMaxW` 380 →
387, which is 7px of extra elision budget for a label that on Home is the literal
`NOW READING` and never elides at all.

**So nothing changes here.** Widening a primitive that eight boards share, to gain
7px on a label that cannot use it, is risk bought for nothing — and the near miss is
the argument: a one-line edit to a shared primitive, reasoned about rather than
computed, was one review away from putting the battery over the margin on every
screen that draws a band.

### `App::requestRepaint()`

`dirty_ = true` **without** setting transition, so the repaint takes the FAST ~439 ms
path and not the 693 ms GC. A charge state appearing is not a screen change and must
not flash like one.

There is no existing way for the shell to dirty a live screen: `App` exposes
`dirty()` and `clearDirty()` and sets `dirty_` only from dispatch and construction.
`LibraryScreen::refreshProgress()` gets away without one only because the pop that
put the Library back on top had already dirtied the app.

Home is not an overlay, so this cannot reach `renderTopOnly`: `canRenderTopOnly`
refuses any non-overlay outright (`core/src/app.cpp:151`). It is a full render of
Home, which is 74 µs desktop.

### `reader::BatteryTracker` (`core/include/reader/battery_tracker.h`)

**In `core/` because it is a latch with a dwell timer and a session cap**, and
`shell/` has no test harness — the reason `ProgressSaveGate` lives there, and the
reason CLAUDE.md records that five bugs have hidden in `shell/`.

```
struct BatteryReading {
  bool percentKnown  = false;
  int  percent       = 0;      // 0..100 when known
  bool chargingKnown = false;
  bool charging      = false;
};

class BatteryTracker {
 public:
  static constexpr uint32_t kUnlatchMs = 60u * 1000u;
  static constexpr int kMaxGrantsPerSession = 3;

  void update(const BatteryReading& r, uint32_t nowMs);
  int  percent() const;         // -1 = never read
  bool charging() const;        // false until a chargingKnown reading arrives
  bool takeRepaintRequest();    // true once per granted rising edge
};
```

Behaviour, each clause doing a specific job:

- **`percent()` is last-good.** A successful read replaces it; a failed read leaves it.
  `-1` only until the first success. `charging()` is last-known on the same rule.
- **The repaint request fires on a rising edge only** — known-not-charging → charging.
  Unplugging never spends a refresh; the stale bolt is corrected by the next Home
  paint, which is the same guarantee the no-polling option would have given.
- **A first reading seeds without firing.** Booting with the cable in must not add a
  refresh to a boot that is already painting Home. The edge requires a prior *known*
  not-charging state.
- **The latch clears only after `kUnlatchMs` of CONTINUOUS not-charging.** This is the
  anti-flap constant and **it works by construction rather than by tuning**: a
  dithering sign never accumulates 60 s of unbroken not-charging, so it never
  unlatches, so it never re-fires. A signal that does hold for a minute and then flips
  is a real state change, not dither.
- **`kMaxGrantsPerSession` grants, then it stops asking.** `ProgressSaveGate`'s own
  idiom, and here it converts a hardware risk that cannot be characterised without a
  bench into a bounded one: at most three extra panel refreshes, ever, per awake
  session.

**Why the anti-flap machinery is needed at all.** On the X3 there is no charger IC
(`chargerAddr == 0`), so charging is `(int16_t)Current() > 0` — **a bare sign test
with no deadband** (`BatteryMonitor.cpp:210`). Charging reads clearly positive and
discharging clearly negative, but **plugged in and full is ≈ 0 mA and its sign
dithers** — and that is the state a device spends all night in. Without the latch, an
edge-triggered repaint would flicker the panel overnight, burning the battery it is
reporting on.

## `shell/`

- One `static BatteryMonitor gBatteryMonitor;` and one `static reader::BatteryTracker
  gBattery;`.
- **`homeOnGlass()` is ONE predicate, written once**, because two sites ask it — the
  paint-time read and the 2 s poll — and a poll that thinks Home is showing while the
  paint site does not would repaint a screen with no battery on it. It is
  `gApp->top().id() == ScreenId::Home`.

  **`top()` is sufficient and `App::render`'s walk is not needed**, because nothing is
  ever pushed as an overlay over Home. There are exactly four overlays — `ItemActions`
  and `DeleteConfirm` on the Library, `ReaderMenu` and `Peek` on the Reader — and Home
  reaches neither of those parents without being pushed off the top first. If that ever
  stops being true the predicate becomes "the topmost non-overlay is Home", and the
  single spelling is what makes that a one-line change.
- **A reading is ONE `readStatus()` call**, mapped field-for-field into a
  `BatteryReading` — `percentageKnown`/`percentage`/`chargingKnown`/`charging` are
  exactly the four the tracker wants, and the per-field `Known` flags are what
  distinguish a valid zero from a failed read.

  Not `readPercentageChecked()` + `isCharging()`, which is the cheaper pair: it is two
  calls that can observe two different instants, and `isCharging()` **discards the
  `known` flag** the poll gate needs. `readStatus()` on the gauge path costs one extra
  I2C transaction (the millivolts nobody here reads), ~150 µs. One call site that
  cannot self-disagree is worth more than that.
- **Read before every Home paint.** At the top of `renderTop()` — before the
  `SpiBusGuard` is taken, since this is I2C and the two buses should be visibly
  separate — when `homeOnGlass()`: take a reading, `gBattery.update(...)`, then
  `HomeScreen::setBattery(gBattery.percent(), gBattery.charging())`. `renderTop()` is
  the single paint entry in the shell, so there is no second site to keep in step.
  Three I2C transactions, ~450 µs at 400 kHz, against a 439 ms panel. The invariant is
  then trivial: **the number on the glass was measured when the glass was painted.**
  - **This site consumes and discards the repaint request.** Otherwise a rising edge
    seen at paint time would fire a second refresh at the next poll, immediately after
    the paint that already showed the bolt. The cap counts grants including that one —
    deliberately, since exact accounting does not matter for a safety net.
- **Poll every 2 s while `homeOnGlass()`**, behind `pollCardPresence`'s existing gate
  (after the paint block, `!gApp->dirty()`). On a granted request, `setBattery` then
  `App::requestRepaint()`.
  - **Skipped entirely when the first reading reported `chargingKnown == false`**,
    which is every X4. A poll that can never observe a change is battery spent for
    nothing on a device built to sit idle.
  - **It needs no `SpiBusGuard`.** It is I2C only and never touches the display's SPI
    bus, so it cannot race a refresh. This is the one thing that makes a 2 s poll
    affordable at all.
- **No retry on a failed read.** Last-good already absorbs a transient miss, and a
  second immediate attempt at a transaction that just NAKed buys little.
- **A `[battery]` boot line** naming percent, charging, `chargingKnown`, and which
  backend answered — because the two models legitimately report at different
  granularity and the log is where that gets settled rather than filed as a bug.

**Last-good is `BatteryTracker`'s, not two more statics in the shell.** It is three
lines on its own and would not justify a header; it comes along because the tracker
already exists for the latch and splitting one subject across two homes is how the
two start disagreeing.

## What the desktop cannot settle

These go to the board's `On glass` column and **must not be closed with `Closes #46`**,
because a desktop pass is not evidence about this panel:

1. **Whether the knocked-out bolt reads at ~24×14 device pixels.** White inside black
   at the size this glass serves worst. Fallback is the fill-replacing variant.
2. **The X4 ADC path.** `batteryAdc = 0` is inherited from the SDK profile with no
   supporting doc and no X4 to check against. The X3 path is the one this device runs.
3. **That plugging in on Home actually shows the bolt within ~2 s**, and that a device
   left on the charger at 100% overnight does **not** repaint. The second is the whole
   reason the latch exists and is the only way to know the constant is right.

## Testing

- `test/unit/test_battery_tracker.cpp` — last-good across a failed read; `-1` before
  any success; seed-without-firing; the rising edge; the latch refusing a second grant
  inside `kUnlatchMs`; the dwell resetting on any charging sample; the session cap.
- Core render tests — the blank state draws no number and keeps the icon's right edge
  on `kMargin`; charging swaps the mark; `setBattery` mirrors into the vm.
- Goldens for `home_charging` at 480×800 and 528×792.
- `make compare` gains the row; the four existing Home boards must not move.

**Every one of these is proved by mutation, and the source is committed before any
mutation is made.** CLAUDE.md records four distinct ways a mutation lies — landing on
an unreachable line, a fixture that cannot reach the branch, a stale object file, and
`git checkout` reverting the change under test along with the mutation.
