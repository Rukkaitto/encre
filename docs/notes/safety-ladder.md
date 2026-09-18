# The safety ladder (#9, #10)

Extracted from `CLAUDE.md`, which keeps a stub under this heading and is where
the cross-references to it point. Same standing as anything in that file.

**TWO CARDS, ONE MECHANISM.** The low banner and the critical shutdown are two rungs
of one ladder and share the piece that did not exist before them — a battery reading
taken on **every** screen. Building either alone builds the whole watcher and uses one
rung of it, and the second consumer is what proves the abstraction: the second copy is
the extraction point, and here both copies arrived in the same change.

**`BatteryTracker` ANSWERS WHICH RUNG THE PACK IS ON; NOTHING ELSE THRESHOLDS A
PERCENTAGE.** `BatteryLevel { Normal, Low, Critical }` and `level()` ride the same
`update()` as the charge latch above, because two objects fed the same reading would
be a caller list — the shape this file names as a function not yet written — and the
shell would be free to feed one and forget the other. One `update()` cannot be
half-fed. Its consumers are `armBannerIfNewlyLow()` and the `Critical` test at the top
of `loop()`, plus `level=` on the `[alive]` line; the numbers stay in the header with
their derivation, because a threshold spelled at a call site is one that gets spelled
differently at the next.

**THE THREE THRESHOLDS ARE THE X4'S NOTCH TABLE AND NOT ROUND NUMBERS.**
`percentageFromMillivolts` walks `LIION_NOTCH_MV[11]` and returns a **multiple of
ten**, so a threshold that curve cannot express is one that never fires on half the
fleet:

| constant | value | why that value |
|---|---|---|
| `kLowPercent` | 10 | the X4's lowest non-zero notch, ~3.68 V. Anything lower is unreachable there until the pack already reads `0`. |
| `kCriticalPercent` | 3 | X4-reachable only as the notch `0`, which is ≤3.565 V. Read literally on an X3, where it is minutes of runtime rather than a cliff. |
| `kResumePercent` | 15 | the resume gate's floor. It requires the X4's **20%** notch, ~3.71 V. |
| `kCriticalDwellMs` | 10 s | continuous, in `kUnlatchMs`'s idiom. A panel refresh is the heaviest load this device draws and the SDK's 0% anchor is sized to leave headroom for that sag, so one low reading is not a flat pack. The poll already runs only in the `quiet` window, which excludes a sample taken mid-waveform; the dwell is the belt to that braces. |

**THE HYSTERESIS IS THE LOAD-BEARING NUMBER, AND IT IS 145 mV.** Put
`kResumePercent` anywhere an X4 can satisfy at the `0`/`10` boundary and the shutdown
edge and the resume edge become the *same* 3.565 V midpoint — so a device left on the
cable shuts down, charges for a minute, wakes, discharges and shuts down again, all
night. Requiring the next notch up is what makes the two edges different voltages, and
it is what `kResumePercent = 15` buys. The price is stated rather than hidden: **a
device refused at 14% cannot be forced on, on either model, even plugged in.**

**THE BOARD'S `BATTERY LOW · 5%` IS A DISPLAYED VALUE AND WAS NEVER A TRIGGER.** 5 is
an X3 specimen; the X4 has no fuel gauge, so `10%` is the only value its banner can
ever show and `BATTERY LOW · 10%` is the widest string the firmware can produce. That
one fact settled the banner's type role — see below.

**CHARGING SUPPRESSES `Critical` AND NOT `Low`.** A device on the cable must not shut
down, and the battery is still low, so the banner saying so is still true. **An X4
never reports charging at all** (`NO_GAUGE`, no charge pin), so there nothing
suppresses — and shutdown-then-refuse-to-wake is exactly right for a flat X4 on a
cable: the glass says `CHARGE · HOLD POWER TO WAKE`, and once it is charged, holding
power does. **THE `and it does` HERE USED TO BE ATTACHED TO A BARE `CHARGE TO WAKE`, AND
THAT WAS FALSE** — charging alone wakes nothing on this hardware; it only makes the hold
succeed. See the badge paragraph under `ScreenId::BatteryEmpty`. **A reading with
`percentKnown == false` holds the level where it was and cannot advance the dwell**:
"flat" and "did not answer" stay different claims, as they already do for `percent()`'s
`kUnknownPercent`, and a dwell satisfied by silence is a shutdown nothing confirmed.
`Low` has no dwell — the cost of being wrong there is one banner.

**THE POLL RUNS ON EVERY SCREEN, WITH NO `gChargingObservable` GATE.**
`pollBatteryLevel()` is `readBattery()`'s second caller, in `loop()`'s
`quiet` window; `refreshBatteryOnHome()` keeps both of its gates unchanged, because
what it drives is Home's band and the charge-latch repaint. Neither of those gates can
serve a safety mechanism: `homeOnGlass()` means a reader an hour into a book has had no
reading taken at all, and `gChargingObservable` is never true on an X4, so on that
model nothing would ever read the gauge. Both callers go through one `gBattery.update()`,
so the level and the band cannot disagree about the percent.

**AND ITS CADENCE IS NOW ASKED FOR RATHER THAN FIXED (#96) — BECAUSE THE THREE
CONSUMERS OF ONE `update()` DO NOT WANT ONE INTERVAL.** The ticket asked to push the
interval "into the minutes" on the premise that *"polling is only used to show the low
battery banner"*, and **that premise is incomplete in the way that decides the fix**.
The same `gBattery.update()` serves:

| consumer | tolerates |
|---|---|
| the `Low` banner | **minutes.** Being late costs one warning arriving late. |
| `Critical` → `criticalShutdown()` | **not minutes.** It is `[[noreturn]]`: `saveReadingPosition("battery")`, paint `BatteryEmpty`, cut the rails. Every second of extra detection latency is a second in which a brownout beats the save, and the save is what `markSleeping()` then redeems. |
| the charge latch | **not minutes.** Plugging in has to feel immediate, and `kUnlatchMs`'s dwell is the whole anti-flap design. |

**AND THE INTERVAL IS WHAT BOTH DWELLS ARE MEASURED ACROSS, so a coarser cadence does
not lengthen a dwell — it THINS it.** `kCriticalDwellMs` and `kUnlatchMs` are
timestamp arithmetic over a *sequence of samples*, not sample counts: at a 60 s
interval, `kUnlatchMs`'s "60 s of **CONTINUOUS** not-charging" degenerates into "two
samples 60 s apart", which is a claim about two instants rather than about a minute.
**That is the bound, and it is what the slow interval is derived from rather than
picked.**

So `BatteryTracker::pollIntervalMs(bandRepaintPossible)` answers from the STATE:

- **`level() != Normal` → `kPollFastMs` (2000).** Everything downstream of the `Low`
  crossing — the banner, the critical dwell, the shutdown — runs at exactly the
  cadence that shipped. **THIS ARM IS STRUCTURAL, NOT A PREFERENCE:** the critical run
  can only be armed by a reading that has already set `level_ = Low`, so the interval
  chosen after it is fast **by construction** — and the critical dwell is therefore
  never sampled at an interval longer than itself, which it could not survive
  (`kPollSlowMs` is 30 s against a 10 s dwell).
- **the band's repaint could reach the glass → `kPollFastMs`.** That is
  `bandRepaintPossible()` — `gChargingObservable && homeOnGlass()` — so `kUnlatchMs`'s
  dwell is sampled at 2 s in every state where its repaint can actually reach the
  panel, which is also where plugging in has to feel immediate.
- **otherwise → `kPollSlowMs` (30000).**

**`kPollSlowMs` IS `kUnlatchMs / 2`, AND THAT IS WHY THE TICKET'S "MINUTES" IS
DECLINED.** Minutes start at 60 s and `kUnlatchMs` **is** 60 s, so a minute-long
interval is exactly the degenerate case above; half of it is the coarsest cadence at
which no single interval can span that dwell. **It concedes almost nothing, because
the benefit saturates and the guarantee does not**: 2 s → 30 s removes **93.3%** of
the readings and the next doubling to 60 s buys **3.4 points more** while halving the
samples both dwells rest on. If minutes are ever wanted anyway, the honest route is to
lengthen `kUnlatchMs` with it — not to move one number.

**AND SLOWING ONLY THAT CASE IS NOT A COMPROMISE, BECAUSE IT IS WHERE THE READINGS
ACTUALLY ACCUMULATE.** A device idling on Home sleeps after `sleepAfterMs` (300 s by
default), so it can never take more than **~150** readings before the chip resets. A
device awake for an HOUR is one whose buttons are being pressed — somebody reading —
and then the **Reader** is on glass rather than Home, the band's repaint is
unreachable, and the ladder is the only consumer that wants the gauge at all. So the
slow arm catches the long session and the fast arm keeps the short one.

**WHAT IT COSTS, and it is one interval and no more:** a real `Low` crossing is
noticed up to 30 s late. The pack absorbs that — the `Low` band runs from
`kLowPercent` down to `kCriticalPercent`, and on an X4, which reports **10% notches**,
it is a whole notch. Either way it is hours of discharge, so 30 s cannot skip it.
`test_battery_tracker.cpp` drives the **closed loop** — the tracker choosing the
cadence at which it is next fed — and pins the whole bill: a flat pack reaches
`Critical` at `kPollSlowMs + kCriticalDwellMs` with **zero** slow gaps after the run
is armed. Sampling it slowly instead puts the shutdown at 60,000 ms against 40,000 ms,
with the dwell spanned by a single gap; that is the mutation, and it is the naive
one-interval-made-bigger fix.

**AND THE TRACKER DOES NOT DEPEND ON THE SHELL OBEYING IT.** A caller that ignored
`pollIntervalMs` and polled slowly for ever must still shut the device down — later,
never not at all — which is asserted, because a safety mechanism resting on an
interval a caller remembers to ask for is the caller-list shape this file turns into a
function. The one guarantee the slow cadence genuinely weakens is stated rather than
hidden: a dithering `Current()` sign sampled at 30 s could clear the latch spuriously
off Home. **It cannot flicker the panel doing so** — the repaint is gated on the same
predicate, so nothing reaches the glass — and the worst a spurious cycle costs is
`kMaxGrantsPerSession`, which is what that cap is for.

**`bandRepaintPossible()` IS ONE PREDICATE BECAUSE IT HAS TWO CALLERS.** It was
already spelled at the repaint site; the interval chooser needs the same answer, and
two spellings would drift **silently in the worse direction** — a cadence that
believed the repaint reachable while the repaint site did not would hold the fast
interval for a refresh that can never fire, which is precisely the battery this ticket
is about. Same rule as `homeOnGlass()` one line above it.

**WHAT A READING COSTS, DERIVED FROM THE REGISTER MAP AND THE PROFILE — NOT
MEASURED.** On the **X3** `readStatus()` is **three** BQ27220 reads over I2C at
400 kHz (`StateOfCharge` 0x2C, `Voltage` 0x08, `Current` 0x0C), each moving five
address/data bytes with a repeated start — ~115–130 µs of bus time apiece, so
~350–400 µs, and `readBattery`'s own **~450 µs** includes the Wire driver. On the
**X4** it is `NO_GAUGE`, so **one** `analogReadMilliVolts` on GPIO0 and no charge pin
at all. At 2 s that is a **0.02%** duty cycle.

**AND THE HONEST ANSWER TO "WHAT FRACTION OF AN IDLE DEVICE'S POWER" IS THAT IT IS NOT
MEASURABLE FROM HERE, WHICH IS ITSELF THE FINDING.** The only instrument the device
carries is the gauge's own `Current()`, whose resolution is 1 mA against a ~20 mA
awake baseline — roughly **200× coarser** than the effect — and reading it is the thing
being measured. So this needs a **bench current meter in series with the pack, poll at
2 s against the poll stubbed out**, and nothing short of that can produce a figure;
**do not quote one until somebody has.** What settles the decision without it is the
comparison this file already made once: the input task calls `input->update()` every
`kPollMs` (10 ms) and each call is **two `analogRead`s**, so an awake device already
takes **400 ADC conversions per 2-second battery interval against 3 I2C register
reads** — and `kPollMs` is the constant this project **refused to halve** on a
duty-cycle argument. The poll #96 is about is two orders of magnitude below the
sampler that argument was made about, which is why the case for this change is the
*long reading session*, not the idle device.

**THE `gLastBatteryPollMs` DISCIPLINE SURVIVED IT, AND HAD TO.** The stamp stays inside
`homeOnGlass()`, because that stamp and a reading are the **same event** —
`refreshBatteryOnHome()` feeds `gBattery.update()` two lines above it — so a Home paint
really has taken the reading it claims. On an X4, where `bandRepaintPossible()` is
false for ever and Home therefore polls **slowly**, presses on Home feed the ladder at
exactly the rate they arrive. **It was the unconditional stamp that starved the
mechanism, never the interval.**

**AND `polls=` ON `[alive]` CANNOT STAND ALONE ANY MORE, so `pollMs=` is beside it.**
With two intervals 15× apart, a device wrongly pinned to the fast one and a device
correctly on the slow one differ in that count and in **nothing else that reaches a
log** — the same "an instrument that reports on less than it claims" shape this file
records for the listing cache and the ring warm. It is read off `pollIntervalMs()` with
the predicate the loop uses, never re-derived from `level=`, which would be a second
spelling of the choice free to disagree with the one actually made.

**AND WIRING THAT FOUND A REAL DEFECT IN `renderTop()`, WHICH IS EXACTLY THE CLASS THIS
FILE EXISTS TO RECORD.** The stamp `gLastBatteryPollMs = millis()` was unconditional —
harmless while the timer's only consumer was itself gated on Home, since a Reader paint
reset a cadence nothing outside Home was waiting on. With an ungated poll it means every
paint pushes the next reading out by another poll interval, so **a reader turning
pages faster than 2 s starves the safety mechanism on the one screen the banner is drawn
on**. The stamp is inside `homeOnGlass()` now: a paint that takes no reading must not
claim one, which is what the comment above it always said. (The `kBatteryPollMs` this
paragraph used to name **no longer exists** — #96 replaced it with
`BatteryTracker::pollIntervalMs()`, and the 2 s the defect was measured against is
`kPollFastMs`. The rule is unchanged and the paragraph above says how it survived.)

**THE BANNER DRAWS OVER THE PAGE AND NEVER INTO THE COLUMN, AND THAT IS THE WHOLE
DESIGN.** `readerMetrics` derives `columnH` and `PageBuilder` seats
`rowsThatFit(columnH, lineBox)` lines in it, so honouring the board's 78px band *inside*
that column takes a default page from **twelve lines to ten** and re-paginates the whole
chapter — a `relayout` at the exact moment the device has least energy to spend, moving
the reader's page under them. So `renderReader` draws it at
`footerTop - kReadFooterPadTop - kBannerH` and `columnH` is untouched.
`ReaderViewModel::anchorLabel` already carried this reasoning for the footer's third
field, and it is the same rule one band lower. **Verified per row rather than by
eyeballing the render**: exactly **78 contiguous rows** differ from the plain `reader`
render at both geometries and every other row is byte-identical.

**ITS LABEL IS `Role::Meta700` AT `--t-meta`, NOT `Label500` AT `--t-label`, AND THE
RAMP IS HALF THE REASON.** The ramp carries `Label400`/`Label500` at 11pt and
`Meta400/500/700` at 10pt (`font_manifest.h`), so **there is no 23px/700 role** and the
design's first spelling asked for a pre-rendered asset nobody has — unbuildable rather
than merely terse. The X4's fit chose between the two roles that do exist: measured in
Chrome on the board at the widest string the firmware can produce, `Label500` at 23px
leaves a gap of **0.00px** and **wraps both runs to two lines** inside a 78px band,
against **27.11px** at `Meta700` (X3: 42.59 against 75.11). After the firmware's ~3%
wider `.rfnt` advances that X4 gap is **~15.9px**, which is the figure that has to stay
positive on glass. The band's `padding: 0 18px` is the page's own, so its runs align
with the header's book title and the footer's percentage; the 24px it replaced was
orphaned from a pre-rebase board with 40px margins.

**`ANY BUTTON` IS A BINDING, NOT A CAPTION.** `ReaderScreen::setBatteryLow(int)` arms
the latch — **one argument, `-1` to disarm, the same sentinel
`ReaderViewModel::batteryLowPercent` carries**, because a `(bool, int)` pair would spell
the condition twice — and the dismissal at the top of `onGesture` clears it and returns
`Action::redraw()` **whatever the gesture**. The latch is `ReaderScreen`'s rather than
the shell's so a test can reach it; `shell/` is where the bugs hide. The first press is
spent dismissing and does nothing else, which is what the bar promises and matters most
for `Back`, since `Back` on the Reader otherwise pops out of the book. Power is not
swallowed — the shell handles it before dispatch — and the banner goes with the RAM,
which is correct. It re-arms on a **fresh entry** into `Low` rather than per poll, so a
wake shows it again and, when the Reader is what the wake restores, rides that paint for
no extra waveform. A banner armed under a peek or the reader menu simply waits: the
Reader is not on top, so nothing draws it until the overlay pops.

**`ScreenId::BatteryEmpty` IS PAINTED DIRECTLY AND NEVER PUSHED**, on `Sleep`'s
argument — the session record names the top of the stack, so pushing it would make the
next wake restore *into* it. `paintBatteryEmptyScreen()` therefore owns the two things
`App` normally does, the **clear** and `gFrameContentsUnknown`. It is `Fidelity::Mono`,
takes no input and draws no hint bar: the shell paints it and calls deep sleep, so there
is nobody left to press anything. `drawBadge` moved into `components.h` because this
screen's badge is byte-identical to `Sleep`'s — the second copy, and `renderSleep`
migrated to it in the same change. Two new marks,
`kWarning` (32×28, white, for the inverted band) and `kBatteryLarge` (98×52), generated
by `iconc.py` from their own boards and disambiguated by `source` exactly as
`kBook`/`kBookLarge` are.

**ITS BADGE SAID `CHARGE TO WAKE` AND BOTH HALVES OF THAT WERE WRONG — FOUND ON GLASS
AND BY NOTHING ELSE.** Reported from an X3: plugging the device in does not wake it, you
have to hold power, and the screen did not say so. It is `CHARGE · HOLD POWER TO WAKE`
now, and the two defects are worth keeping separate because only one of them is a copy
problem.

- **CHARGING CANNOT WAKE THIS HARDWARE, so the old badge promised something no code
  could deliver.** There is no charge-detect wake source: `usbDetect` appears **only** as
  a field declaration in `freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h`
  and **nothing in the SDK reads it** — the same dead field the charge-latch poll exists
  because of — and on the X3 the pin the Xteink profile names for it is the fuel gauge's
  own I2C SDA. **A timer wake cannot substitute either**, and that is the half that
  surprises: on battery the sleep leaves the chip **fully powered down**, which is
  exactly why a resume there reports `ESP_RST_POWERON` rather than `ESP_RST_DEEPSLEEP`,
  so there is nothing left running to fire one. **This is the class of claim this file
  exists to record and the class the battery work already refuses elsewhere** — an unread
  gauge answers `-1` and never `0%`, a book with no reading position gets no demo
  substitute. **A false claim is worse than an absent one**, and a badge is a claim.
- **IT OMITTED THE HOLD, AND THE HOLD GATE RUNS FIRST.** `setup()` calls
  `requireHeldPowerButtonOrSleepAgain` **before** `requireChargeOrSleepAgain`, so a wake
  off this screen needs `kWakeHoldMs` (600 ms) of held power **and** a pack at
  `kResumePercent` — and a tap is refused for the *hold* reason with the gauge never
  read. So the screen named the second gate and not the first, in a state where the first
  is the one the reader keeps failing.
- **THE FIT NEEDED NO NEW MEASUREMENT, WHICH IS WHY THIS STRING AND NOT A SHORTER ONE.**
  It is character-for-character as long as `Sleep`'s `ASLEEP · HOLD POWER TO WAKE`
  (27 each, both `--t-meta` at 0.2em), and that badge **already ships on the X4** — the
  narrower panel. Measured after the change: the label is **427px** and `drawBadge`'s
  box **465px** on the 480px glass, leaving 8px and 7px of margin — against `Sleep`'s
  422/**460**, the 5px being `CHARGE` measuring wider than `ASLEEP` at whole-pixel
  advances. **The 1px margin asymmetry is `centreIn` halving an odd 15px leftover and
  is not a centring defect.**
- **`drawBadge` SIZES TO ITS LABEL, so both `battery_empty` goldens moved and were
  re-blessed** — the badge went **267px wide to 465px**, still centred, still 34px off
  the bottom, still one line. Design-vs-firmware went 1.81%/1.66% to **2.08%/1.91%**
  with both copy changes in, and **the increase is accounted for row by row**: the
  badge's own 45 rows go 563 → **1344** differing pixels, and everything outside them
  goes 6374 → **6646**, the +272 being the one reworded paragraph line and nothing else.
  The badge BOX agrees with Chrome's to 1px at both geometries (465 against 466, same
  top, bottom and right edge) — a longer tracked run of small caps is simply where
  Chrome's subpixel advances and the firmware's whole-pixel ones disagree most per pixel
  of ink. `sd_missing` measured 1.83%/1.67% as a control in the same tree, **unchanged
  to the pixel**, which is what makes the before and after the same instrument.
- **The middle dot is its own string literal** (`std::string("CHARGE ") + kMiddot + " HOLD
  POWER TO WAKE"`), for the reason this file records twice: a C++ hex escape is UNBOUNDED,
  clang rejects `"\xB7H"` and the ESP32's GCC **accepts** it and emits a byte that is not
  U+00B7. That is a sixth local spelling of the two-byte string and deliberately so —
  `screens.cpp` records it as a punctuation choice each board makes rather than a
  constant, and **its `kDot` is unreachable anyway**: `const char* const` in an
  anonymous namespace in a .cpp, and not even the same string (`" · "` with its spaces
  baked in, against `screen_book_end.cpp`'s bare two bytes).

**AND ITS PROSE NAMED A CONNECTOR THE X3 DOES NOT HAVE — THE SECOND FALSE CLAIM ON THE
SAME SCREEN, ALSO FOUND ON GLASS AND BY NOTHING ELSE.** It read *"charge over USB-C to
continue"* and was reported from an X3, **which has no USB-C port**. It is
*"connect a charger to continue"* now, and **the connector is deliberately unnamed
rather than corrected**:

- **ONE BINARY DRIVES BOTH MODELS AND THEY DO NOT SHARE A CONNECTOR**, so naming either
  one is false on the other — swapping `USB-C` for the X3's socket would have moved the
  defect to the X4 rather than fixed it.
- **AND IT COULD NOT BE MADE CONDITIONAL EITHER**, which is the fact worth keeping:
  **nothing in `BoardProfile` describes the socket.** It names the battery ADC, the
  gauge address and the dead `usbDetect` pin, and there is no field anywhere from which
  a screen could ask which port is fitted. So the copy cannot name one *correctly* under
  any amount of work.
- **THE WRAP WAS VERIFIED IN BOTH ENGINES RATHER THAN ASSUMED**, because
  `SdMissing.dc.html` needed `max-width` 400→420 for exactly this — the `.rfnt` faces
  measure ~3% wider than Chrome's. `connect a charger` is 17 characters as
  `charge over USB-C` was, and both engines wrap the paragraph to **four lines at the
  same three break positions** (after *The*, after *down*, after *to*) at both
  geometries, before and after. `max-width` did not move. **The dash that used to sit
  at that second break is gone** — see **The rule that governs COPY** — and the breaks
  did not move with it.

**THE RESUME GATE IS BEFORE `display.begin()`, AND THAT IS THE WHOLE COST OF THE
FEATURE.** `requireChargeOrSleepAgain()` sits in `setup()` immediately after
`requireHeldPowerButtonOrSleepAgain` — the cheaper refusal first, so a bag-brush is
refused for the *hold* reason without spending an I2C transaction — and still before the
panel bring-up. E-ink holds its last image, so the glass is already showing the
`BATTERY EMPTY` screen the shutdown painted: **a refusal repaints nothing and spends no
waveform.** One line later and every brush against a flat device's power button costs a
flash. It is after `detectAndSelectBoard()` because it needs the profile, and safely so
for the reason `BatteryMonitor`'s constructor is safe above: `readStatus()` tests
`BoardConfig::ACTIVE.batteryGauge.gaugeAddr` **live**.

- **THE `critShut` NVS FLAG IS WHAT MAKES A STRICT `≥15%` LEGAL.** Without it the gate
  would have to sit at the critical threshold itself, which flaps — and a `15%` gate
  applied to *every* boot would refuse a device sitting at a perfectly usable 10%. It
  fires only for a device that shut itself down.
- **Read-and-cleared, and given back on a refusal**, exactly as `slept` is: one flag
  buys one resume, a boot that sets out to refuse and then panics must not refuse for
  ever, and a refused wake spent neither flag. Dropping the `slept` half would make the
  **next** wake — the real one, once charged — read as a cold start, and the reader would
  lose their page to something that looks like the restore failing.
- **A reading that did not answer lets the device boot.** Fail open here, fail safe in
  the loop: a failed gauge would otherwise refuse every wake for ever, and the ladder
  shuts the device down again ten seconds later if the pack really is flat.
- `[boot] battery pct=N critShut=1 -> RESUME|refused` prints the whole decision, in
  `[boot] reset reason=… slept-flag=…`'s idiom. Read that line before believing anything
  about a device that will not turn on.

**`criticalShutdown()` IS `sleepNow()`'s SHAPE AND IS `[[noreturn]]`**, reached from
`loop()` before the idle-sleep check: `saveReadingPosition("battery")` → paint →
`display.deepSleep()` → `powerDownRailsForSleep()` → `markSleeping()` **and**
`markCriticalShutdown()` → flush → `deepSleepUntilPowerButton()`. `markSleeping()` too,
deliberately: once charged, the wake should restore the reader's page rather than
starting cold, which is the other half of the board's *"Your page is saved"* — the save
makes the promise and this flag redeems it. **A cold boot on a flat pack spends one Home
paint before the dwell fires**, because a device that appears to do nothing when you
press power is indistinguishable from a brick.

**`ENCRE_BATTERY_FAKE_PERCENT=n` IS HOW THE LADDER IS WALKED ON GLASS**, in
`ENCRE_FS_SELFTEST`'s shape and absent by default (+108 bytes when present). Draining a
real pack to 3% on demand is not practical, and without it none of the five things only
the panel can answer is reachable. **It overrides the percent and nothing else**, so an
X3 on the cable still suppresses `Critical` — which is what makes the on-cable check a
real test rather than a tautology. `docs/on-device-smoke-checklist.md` §10 is the list.

**CONFIRMED ON GLASS (2026-09-07), WHICH IS THE ONLY PLACE MOST OF IT COULD BE.** The
whole ladder was walked on an X3 with `ENCRE_BATTERY_FAKE_PERCENT`: the banner over a
page, the shutdown's paint completing before the rails go down, the resume gate refusing
three times in a row and repainting nothing, the restore of the reader's page once
charged, and `charging` suppressing `Critical` on the cable. **1,319 green test cases
said nothing about any of it** — `shell/` has no harness, so not one line of the two
gates or the shutdown's ordering is executed by the desktop suite.

**AND THE GLASS FOUND TWO FALSE CLAIMS NOTHING ELSE COULD**, both in this screen's copy
and both the defect class this file already refuses for an unread gauge (`-1`, not `0%`):

- **`CHARGE TO WAKE` promised what the hardware cannot do.** There is no charge-detect
  wake source — `usbDetect` is a field declaration nothing in the SDK reads, and on the
  X3 it names the fuel gauge's own SDA — and no timer wake can substitute, because on
  battery the sleep leaves the chip **fully powered down** (which is why a resume reports
  `POWERON`). It also omitted the hold, which `requireHeldPowerButtonOrSleepAgain`
  requires and which runs FIRST, so a tap refuses for the *other* reason and never
  reaches the charge gate at all. The badge is `CHARGE · HOLD POWER TO WAKE` now, 27
  characters — the width is proven by `Sleep.dc.html`'s shipping badge of exactly that
  length rather than by a fresh measurement.
- **The prose named USB-C, which the X3 does not have.** One binary serves both models
  and nothing in the board profile knows the connector, so the copy may not name one:
  *"connect a charger to continue"*. Both engines still break the paragraph at the same
  four places, so `max-width` did not move.

**Four live comments still asserted the old promise**, one of them verbatim —
`battery_tracker.h` said *"the glass says CHARGE TO WAKE, and it does"*, inside the
header that owns the thresholds. A rule restated at several sites is one that drifts, so
all four were corrected rather than just the string.

**MEASURED AGAINST THE BOARDS:** `low_battery` 5.03% (X4) / 6.02% (X3) against the
untouched `reader`'s 5.34%/6.38% — compare it against the other **grayscale** screens,
for the reason recorded under the peek. `battery_empty` is **2.05%/1.89%** against
`sd_missing`'s 1.83%/1.67%, measured as a control in the same tree with the same
instrument. It read **2.08%/1.91%** until the connecting dash came out of its
paragraph (see **The rule that governs COPY**), and the control reproduced its own
recorded pair to the digit across that change, which is what says the two readings
are one instrument rather than two. It read 1.81%/1.66% before the badge's copy changed, and the increase is
accounted for row by row: **the badge's box agrees with Chrome's to 1px**, so what moved
is a wider tracked-caps run rasterising differently, not geometry drifting. `make
compare` reports 30/36 implemented and both ids `firmware ok`.

**ONE STATED LIMIT, PRE-EXISTING RATHER THAN INTRODUCED:** `criticalShutdown()` flushes
the card log **after** `powerDownRailsForSleep()` has cut the X3's SD rail. That is
`sleepNow()`'s ordering verbatim and the flag ordering it inherits is right — the flag
is what the next boot needs and the log is only what a human needs — but it means
`[log] battery empty` may never reach `/encre.log` on an X3, so the serial capture is the
authority for this path.
