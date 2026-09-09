# On-device smoke checklist

**What this is for.** The desktop suite, the goldens and `make compare` all run
at `Rotation::None` on a host toolchain with no SD card, no inflater and no
panel. This list is the other half: the things that pass every desktop check and
can still be wrong on the glass. It is organised by *failure class*, not by
screen, because that is how the defects have actually arrived.

Run it after any change that touches drawing primitives, the paint sequence,
storage, power, or the reader — and in full before a release
(`docs/releasing.md`).

**Budget: about 20 minutes**, plus the deep-sleep wait if you test the idle
timeout rather than pressing power.

## Before you start

- A card with **a real library** — at least one folder, and at least one book of
  novel length. A flat three-book card exercises almost none of this; every
  device measurement in `CLAUDE.md` was taken on a flat card, which is exactly
  why the folder-listing defect stayed invisible for two phases.
- At least one book **never opened before**, for the cold cover decode.
- A charging cable you can plug and unplug.
- A capture running:

      ~/.platformio/penv/bin/python -m platformio device monitor -e xteink | tee run.log

**Two rules that make the results mean anything:**

1. **Never conclude anything about the firmware from the panel alone.** E-ink
   holds its last image with no power and nothing clears the glass at boot, so a
   screen that looks right can be a crash loop and a screen that looks frozen
   can be a device that never painted. Read the log.
2. **Some of this can only be checked unplugged.** Attaching a serial logger can
   turn a wake into a cold boot, and every timing over USB is inflated by the
   cable. The checks marked **[unplugged]** must be run with nothing attached,
   from the panel alone.

---

## 1. It booted, and it is the hardware you think

- [ ] **1.1** The log reaches `[alive]` with a stable heap. Anything short of
      that means `setup()` never returned; the last `[stage]` names the call it
      hung in.
- [ ] **1.2** `[detect] i2c verdict=` names the right model, and
      `[detect] active controller=` reads **6** (UC8279) or **2** (UC8253).
      A `Wait complete` reporting exactly `30000 ms` is the wrong controller
      driver, not a dead panel.
- [ ] **1.3** `[boot] waited Nms for USB CDC` — a large number here is a USB
      question, not a firmware one.
- [ ] **1.4** `[boot] settings ...` says `loaded`, `DEFAULTED` or `CORRECTED`,
      and the reason matches what is on the card. This line is the only way a
      hand-edited settings file's rejection is ever visible.

## 2. Rotation — the five byte-wise primitives

Under CCW a logical row is a physical *column*. `veilRect`, `fillRect`, the
glyph blit, `ditherRect` and the cover blit each walk the physical store
directly, and **each one passes every golden and every board comparison while
smearing on glass**, because the whole desktop is `Rotation::None`. Symptom to
look for: diagonal streaking, or ink displaced along one axis.

- [ ] **2.1 Glyph blit.** Any screen with text. Letters are letters, not
      diagonal hash. A column of notdef boxes is a *lifetime* bug, not a
      rotation one — it means a wrap outlived its string.
- [ ] **2.2 Fills.** Move the focus down the Library. The focused row is a clean
      full-bleed inverted block with square edges.
- [ ] **2.3 Veil.** Hold Confirm on a Library row to open the item-actions
      overlay. The veil is an even stipple over the whole frame — not a
      diagonal smear, not a smudge.
- [ ] **2.4 Dither.** The sleep screen tints the whole panel. Even field, no
      banding, no diagonal drift.
- [ ] **2.5 Cover blit.** Sleep on a book that has a cover. The picture is
      upright and undistorted.

## 3. The overlay stack

- [ ] **3.1** The item-actions overlay shows the **Library underneath it**,
      veiled. A panel floating on white means the shell called `top().render`
      instead of `App::render`, and nothing on the desktop can catch that — the
      simulator and every golden go through `App::render`.
- [ ] **3.2** Move the focus inside the overlay. Only the panel repaints; the
      frame beneath does not flash. Focusing the **last** row of the actions
      panel and moving off it again is the one case that legitimately takes the
      full path (its panel changes height by a pixel) — no stale border should
      be left standing either way.
- [ ] **3.3** Open the reader menu over a page. The page beneath is
      hard-thresholded for those frames — that is the declared trade, not a
      regression.

## 3b. The corrupt-book dialog (#5)

**Nothing below is executed by the desktop suite** — `shell/` has no harness, so
the whole of the raise path and the delete latch are unverified until this runs.

Prepare a card with a **truncated `.epub`** (copy a real one and cut it short —
`head -c 40000 book.epub > broken.epub`). That is the reproducible case; a
zero-byte file is not, since it fails differently.

- [ ] **3b.1** Confirm on the broken book **from the Library**. The dialog
      appears over the veiled Library, saying the file "appears to be damaged".
      Before this, the press produced no visible change at all.
- [ ] **3b.2** The **warning triangle** still reads as a warning mark. It is
      three diagonals, and `Mono` thresholding treats a thin diagonal worst —
      `kChevron` is the recorded precedent for coming out a notch lighter.
- [ ] **3b.3** `OK` and `Back` both close it, and the Library is exactly as it
      was.
- [ ] **3b.4** **The veil over HOME.** Get the dialog from Home's CONTINUE (make
      `last.json` name the broken book). **No board draws this** — every overlay
      board in the repo veils a list, and Home is the first parent with a 67px
      numeral and a dither block under the stipple.
- [ ] **3b.5** **`DELETE FILE…` from Home's CONTINUE.** This is the whole reason
      `DeleteConfirmScreen` took `Facts`, and it was a dead button until the
      shell primed them. The confirmation must name the right book, and
      confirming must land on **Home**, not the Library.
- [ ] **3b.6** The stale-facts sequence: fail to open a book, dismiss with `OK`,
      then `Delete…` a **different** book from the Library's actions panel. The
      confirmation must name the book you just long-pressed, **not** the corrupt
      one. Without `clearDeleteFacts()` it named the corrupt one and removed it.
- [ ] **3b.7** Pull the card between a Library listing and a press, if you can
      catch it. The dialog must say the file **could not be read**, never that it
      is damaged — `pollCardPresence` takes 2–25 s to notice, and that window is
      the whole reason there are two copy shapes.

## 4. Sleep and wake — **[unplugged]**

The decisive test needs no logger, and attaching one invalidates it: deep sleep
powers down USB, so re-enumeration can reset the chip and turn the wake you are
investigating into a cold boot that looks exactly like a bug.

- [ ] **4.1** Open a book, read a few pages, press power. The sleep screen
      paints before the device goes down.
- [ ] **4.2** **Hold** power ~1s. **The screen you left comes back.** If it does,
      the wake works and any earlier contrary evidence was the logger. A brief
      tap must leave the glass untouched: the chip's wake source is
      level-triggered and cannot require a dwell, so the hold is enforced after
      the wake by a gate that refuses one it was not held through and sleeps
      again. A refusal runs before `display.begin()`, so it paints nothing.
- [ ] **4.3** Sleep from Home, from the Library, and from inside a book. Each
      wakes back to where it was, focus included.
- [ ] **4.4** Sleep with a peek open. Waking to the **page underneath** is
      correct and deliberate — a peek is a transient "am I sure?".
- [ ] **4.5** **The wake's first frame, at both `Shows` settings — this is the one
      that has been wrong.** Set Settings › SLEEP SCREEN › `Shows` to **DETAILS**,
      sleep on a book, wake. Then set it to **COVER + DETAILS**, sleep on the same
      book twice (so the cover is cached — see 8.2), and wake again. **A pass is the
      same REFRESH both times**: one clean black flash resolving directly to the
      settled frame, with nothing in between. What that frame holds differs by mode
      and both are correct — DETAILS gives the reading card with `WAKING` on it,
      COVER gives the cover repainted in one bit with the waking badge over it.
      **A fail is a band pattern that settles**, and the tell is that the two
      settings differ — DETAILS banding while the cover flashes cleanly was #94, and
      it is the controller's DTM1 baseline going unseeded, not a panel fault (7.4 is
      the same symptom stated generically). The panel keeps its image with no power
      and the controller's baseline does not, so nothing may assert one before this
      paint; both modes let the driver seed DTM1 white and take the GC. If it
      returns, the suspect is a `skipInitialResync()` reached **before**
      `showOnePass` in `setup()`'s waking-paint block, not after it.
      **Count the flashes while you are here**: a wake should show one, or two on a
      card with `fullOnTransition` left on (Home's own transition GC). Three means
      the boot clear budget is not being spent — `[power] waking paint:` names which
      mode it took.
- [ ] **4.6** Now plug in and read the log: `[boot] reset reason=... slept-flag=...`
      prints the whole decision. On battery a resume is a `POWERON`, so the
      slept flag is what distinguishes it from a first-ever boot.

## 5. The card

- [ ] **5.1** The first paint **after a mount** is clean. A bad refresh with a
      card in and a clean one without is the double `SPI.begin()` on the shared
      bus and nothing else.
- [ ] **5.2** With the device on Home, **pull the card**. Within ~2 s the
      SD-missing screen appears. `[sd] THE CARD IS NO LONGER ANSWERING` names
      which mechanism noticed.
- [ ] **5.3** That line should credit the **fast probe**. If it credits the
      25 s FAT-scan backstop, the fast probe is being answered from cache — the
      exact defect that shipped once and made the SD-missing screen
      unreachable.
- [ ] **5.4** Put the card back and press RETRY. The device **restarts**, because
      boot is the only path that re-runs the mount — a card pulled after a
      successful mount cannot be re-mounted in place.
- [ ] **5.5** Boot with **no card at all**. SD-missing, and a wake still works.

## 6. Reading a real book

- [ ] **6.1** Open a book **through the Library**, and another **through Home's
      CONTINUE**. Both must work: the Library sits resident underneath, so the
      two routes leave about 34 KB of different heap, and cover decode and the
      page ring are sized against the smaller.
- [ ] **6.2** Page forward and backward through a chapter boundary in both
      directions. No blank page, no repeated page, no page that differs from its
      forward self.
- [ ] **6.3** Page **backward from deep inside a long chapter** (page 90+). It
      is slow by design — a rewind costs what page you are on — but it must
      arrive, and it must arrive on the right page.
- [ ] **6.4** A long chapter's footer reads `3 / —` and the total arrives a
      couple of seconds later: `[index] counted pages=N`. A short chapter is
      counted before its first paint and never shows the dash.
- [ ] **6.5** Press a button **during** a page count. The press is served; the
      log says `[index] abandoned`. Buttons must never go dead for seconds.
- [ ] **6.6** Open the table of contents and jump to a chapter. Confirm the peek
      panel shows that chapter's text over your page, `CLOSE` leaves your page
      untouched, and `GO HERE` commits. Landing a page off deep in a long
      chapter is [#48](https://github.com/Rukkaitto/encre/issues/48) — known,
      not a new finding.

## 6b. An over-long paragraph ([#37](https://github.com/Rukkaitto/encre/issues/37), [#90](https://github.com/Rukkaitto/encre/issues/90))

**This is #37's on-glass validation, and #90 is why it could not be done before.**
`kMaxBlockBytes` was 64 KB against a 42,152-byte reading floor, so a block anywhere
near it was an `abort()` — and under `-fno-exceptions` that is a reboot with no
diagnostic, which this project has twice had reported as *"opening a book goes back to
Home"*. The cap is 8 KB now and the buffer is reserved rather than grown, so the peak
is 18,308 bytes whatever the book says. **All of that is desktop arithmetic and a
desktop corpus; the heap is the one thing only the device can answer**, and this repo
has measured cover decode peaking **17–25 KB above its desktop figure** because the
allocator differs.

Put a book with an over-long paragraph on the card. Two of the 225-book corpus have
one over 64 KB (`The Number "e"`, `The 32nd Mersenne Prime`) and seven have one over
16 KB — `Paradise Lost` is the easiest to read, since one whole book of the poem is a
single 50,983-byte block.

- [ ] **6b.1** The book **opens and reads to the end**, through the **Library**
      (the smaller heap: 203 books resident, ~42 KB free). Before #90 this was
      the case that rebooted.
- [ ] **6b.2** `[stack]` and the `min` figure on `[alive]` after reading through
      the long paragraph. **Read `mark()`'s stage trail, not `getFreeHeap()`** —
      only `ESP.getMinFreeHeap()` sees a transient. The block builder should cost
      ~18 KB at its peak; the number to compare is the floor **with the long
      paragraph read** against the floor of an ordinary book.
- [ ] **6b.3** No `abort()`, no `MCAUSE 0x2`, no boot landing on Home mid-book.
      That failure is silent by construction, so the serial log is the evidence.
- [ ] **6b.4** The seam is **visible and harmless**: the paragraph continues with
      a 1.5em indent, once per ~15 pages of unbroken text. It must not look like
      a lost line or a repeated one.
- [ ] **6b.5** An ordinary novel is **unchanged**. 208 of the 225 corpus books
      are byte-identical including their block count, so a difference you can see
      on a normal book is a regression, not this change.
- [ ] **6b.6** If the reserve ever fails, the chapter is refused with **"not
      enough memory to read this chapter"** on `BookError.dc.html` — the existing
      copy shape, no new one. Nothing in the corpus can produce it on the desktop.
      **This used to say there was no way to force it, and there is: §6c.**

## 6c. A big library plus a big chapter — how to force a nearly-full heap

**Found the hard way on 2026-09-07: this is a `reason=4 PANIC`, three times.** It is
not a defect in any one book — it is the heap budget, and the Library's residency is
what spends it. Measured on an X3 opening `Digital Minimalism` (chapter 12 is 66,843
bytes) from the Library:

| `/books` entries | heap at the Library | cost of the open | left over |
|---|--:|--:|--:|
| 7 | ~168,300 | 63,552 | ~105 KB |
| **232** | **96,272** | 63,552 | **13,696** |

So the *same* open is comfortable on a small card and 8% from the edge on a large
one. **This is the recipe §6b.6 said did not exist**, and it is the only way this
project has found to drive the reading path to the edge of the heap on purpose.

**Staging it — and the warning is the important half.** Copy the corpus in under a
prefix so removal cannot touch a real book, and `dot_clean` after, or macOS's `._`
sidecars **double** the listing at ~2.7 ms an entry:

    card=/Volumes/<card>
    find ~/.cache/encre-corpus -name '*.epub' | while read f; do \
      cp "$f" "$card/books/zzbulk-$(basename "$f")"; done
    dot_clean "$card/books"
    # afterwards, and this cannot take a real book with it:
    rm -f "$card/books/zzbulk-"*.epub

**A RIG BUILT FOR ONE CHECK CONDITIONS EVERY OTHER CHECK ON THE SAME CARD.** That is
how this was found: the bulk library was staged for §6b and manufactured a crash in an
unrelated book open, which read as a regression in work that had nothing to do with it.
**Bisect the rig before the firmware** — removing the bulk books is one command and no
reflash, where a firmware bisect costs a flash cycle. Do not judge a fault found under
this rig until it has been reproduced without it.

- [ ] **6c.1** With the bulk library staged, open a book with a large chapter from
      the **Library** (not from Home's CONTINUE — that route leaves ~34 KB more).
      `[open]`'s `heap A -> B (cost N) min=M` is the line to read.
- [ ] **6c.2** **It refuses rather than panicking.** `BookError.dc.html`, "not
      enough memory to read this chapter", and the device still usable. A
      `reason=4 PANIC` on the next `[boot]` line is the failure — and it is silent
      by construction, so that line is the only evidence.
- [ ] **6c.3** `[boot] reset reason=` across the whole session names no `4 PANIC`.
      Grep it rather than trusting the screen: the reboot lands on Home and looks
      exactly like a navigation bug, which this project has had reported as one
      **twice**.
- [ ] **6c.4** Remove the bulk books and repeat. The same open must now be
      comfortable — if it is not, the fault is real and is not the rig.
- [ ] **6c.5** *(if a coredump is wanted)* `pio ... -t coredump` needs `gdb`
      installed and the **flashed** ELF, so pull it before rebuilding or the
      SHA256 will not match.

## 7. Grayscale refinement

- [ ] **7.1** Turn a page. Text appears in about half a second, dithered.
- [ ] **7.2** Stop pressing. About five seconds later the page settles to four
      grey levels: `[refine] done total=`.
- [ ] **7.3** Turn pages steadily. The refinement must **not** fire between
      turns and put its ~1.4 s of uninterruptible work in front of your next
      press.
- [ ] **7.4** No banding on the first frame after a wake. Noisy banding there is
      the controller's baseline being skipped, not a panel fault.

## 8. Covers at sleep

- [ ] **8.1** Sleep on a book **opened for the first time**. The reading card
      shows first, then the cover replaces it a few seconds later. That is the
      cold decode and it is by design.
- [ ] **8.2** Wake and sleep again on the same book. The cover now arrives in one
      step.
- [ ] **8.3** `[power] sleep cost save= paint1= probe= decode=` adds up to what
      you watched.
- [ ] **8.4** A book whose cover is refused falls back to the reading card **with
      the badge shown**, and `[cover]` names which of the six refusals it was.
      A progressive JPEG is a stated refusal.
- [ ] **8.5** In Settings, switch `Shows` to `DETAILS` and confirm `Cover fit`
      becomes unreachable — a fit is meaningless with no cover on the glass.

### The upscale cap ([#64](https://github.com/Rukkaitto/encre/issues/64))

**This is the one item on this list that is being asked to settle a question the
desktop cannot even pose.** `kMaxCoverUpscalePercent` is **250**, and that is an
owner override of a bound two measurements put at **200** — the pipeline's own
diffusion grain and this project's measured legibility floor both land on 2 panel
pixels, and at k = 2.5 a source pixel becomes a run of 2 or 3. So past 200 the
sufficiency of nearest-neighbour is **assumed, not measured**. Neither the
simulator nor the goldens can arbitrate: they run this same arithmetic, so they
agree with it by construction. `imagefit.h` carries the whole derivation and every
figure, kept deliberately so this can be moved *back* with evidence.

- [ ] **8.6 A cover that used to sit small now fills the panel.** Any cover
      smaller than the glass on an axis. Before this it was drawn at 1:1 and
      centred; it should now be full-bleed, which is what `SleepCover.dc.html`
      draws. `[cover]` reports the source and destination rectangles.
- [ ] **8.7 THE DECISIVE ONE — does a x2+ enlargement read as a photograph or as
      blocks?** A small cover on the X3 (~260x346 asks x2.29). Look for countable
      2–3px steps on curves and type, the way this file's rotation items look for
      a smear. **If it reads as blocks, 200 is the number the measurements
      support and it is a one-line change back** — say so on
      [#64](https://github.com/Rukkaitto/encre/issues/64) rather than tuning it
      quietly, because the constant is now carrying a judgement and not a
      measurement.
- [ ] **8.8 A cover too small to enlarge is `TooSmall`, and nothing is drawn.**
      Past the cap the reading card comes up behind it and `[cover]` says
      `TooSmall` — **not** `OutOfMemory`, which is the false answer it used to
      arrive as.
- [ ] **8.9 The `FILL` crop of an *enlarged* cover.** The crop arithmetic did not
      change, but the raise widened the set of covers it bites on, so a title or
      author line can now be cut on a book that used to be shown whole.
      `Cover fit = WHOLE` is the existing escape.
- [ ] **8.10 The one-bit cover the WAKE paints**, at these ratios. The `Msb`
      plane is a threshold *through* an already-dithered picture, and hard
      thresholding a photograph is what this project warns about. Pre-existing,
      and reachable by more covers now.

## 9. The battery

- [ ] **9.1** On Home, **plug in**. The charging bolt appears within ~2 s.
- [ ] **9.2** **Unplug.** The bolt clears — after a 60 s dwell, not instantly.
      This is the half that shipped missing: a bolt that stays on glass forever
      is a false claim, and a reader sitting on Home presses nothing that would
      correct it.
- [ ] **9.3** The percentage is not `87%` unless it really is. `[battery] ...
      (I2C gauge|ADC backend)` says which backend answered; an X4 reports in
      multiples of ten and that is not a bug.
- [ ] **9.4** A gauge that does not answer draws the mark **alone**, never `0%`.
- [ ] **9.5** **The cadence is the one you think (#96).** There are two intervals now,
      15× apart, and `[alive] battery ... polls=N pollMs=M` is the only thing that
      says which is in force — a device wrongly pinned to the fast one and a device
      correctly on the slow one differ in `polls=` and in nothing else. Read `pollMs=`
      in each of three states and expect:

      | where you are | X3 | X4 |
      |---|--:|--:|
      | Home, pack `Normal` | **2000** | **30000** |
      | in a book, pack `Normal` | **30000** | **30000** |
      | anywhere, `level=1` or `2` | **2000** | **2000** |

      The X4 column is not a defect: `gChargingObservable` never arms there (no
      charge-status pin), so no bolt can ever appear or clear and the fast cadence
      would be held for a repaint that cannot happen. `observable=0` on the same line
      is what confirms that is the reason. **A `pollMs=2000` in a book with
      `level=0` is the regression to report** — it means `bandRepaintPossible()` and
      the interval have drifted apart, and the whole saving is gone.
- [ ] **9.6** **The count actually falls.** Note `polls=` on two `[alive]` lines while
      **reading** — the state that used to accumulate them — and confirm the rate is
      roughly one per 30 s rather than one per 2 s. This is the only place the change
      is observable at all; nothing on the panel moves.

## 10. Battery states — the safety ladder (#9, #10)

**Not one line of the resume gate is executed by the desktop suite**, and the same
is true of the shutdown's ordering and of the `critShut` flag surviving a chip
reset — `shell/` has no harness. This section needs **its own build**: draining a
real pack to 3% on demand is not practical, so build with

    PLATFORMIO_BUILD_FLAGS="-DENCRE_BATTERY_FAKE_PERCENT=n" make firmware

to reach each rung. `[alive] battery ... level=N` reports which rung the device
thinks it is on (`0` Normal, `1` Low, `2` Critical), and **a silent shutdown with
no `[power] CRITICAL` line is a poll that stopped running, not a ladder that
fired** — the poll is the one part of this that is otherwise invisible.

**EVERY TIMING BELOW IS UNCHANGED BY #96, AND THAT IS WHAT THIS SECTION IS NOW ALSO
TESTING.** The poll has two cadences, but **every rung below `Normal` selects the fast
one**, so once a faked percent has put the device on `level=1` the whole ladder runs at
the 2 s interval it shipped at. Two consequences for how you walk it:

- **A faked build reaches the fast cadence on the very first reading**, because the
  device boots onto Home and that paint feeds the tracker. So none of 10.1–10.6 asks
  you to wait longer than it used to, and `pollMs=2000` on `[alive]` is what confirms
  you are testing the ladder rather than the cadence.
- **What IS up to 30 s slower is the `Normal` → `Low` crossing on a REAL pack**, off
  Home. That is the entire latency the change buys and it is not walkable with a fixed
  fake percent — the flag does not move — so treat 10.1's banner as immediate when
  faked, and expect up to half a minute if you ever see it happen for real in a book.

- [ ] **10.1** `=8`, in a book. The banner appears over the page **without moving
      the text** — count the lines: a default page holds twelve under the band, and
      eleven means the banner was honoured inside `columnH` and the chapter has
      re-paginated. One press of **any** button clears it and does nothing else.
- [ ] **10.2** `=2`. `[power] CRITICAL pct=2` then
      `[power] battery empty: painting the shutdown screen`, then the panel shows
      BATTERY EMPTY. **The paint must COMPLETE before the rails go down** — a
      half-drawn screen here is the last thing the reader sees for hours.
      **READ THE TWO STRINGS ON THAT SCREEN WHILE IT IS UP**, because both were
      wrong once and both were found here and nowhere else: the badge must say
      `CHARGE · HOLD POWER TO WAKE` with a real middle dot (a notdef box means the
      hex escape swallowed a byte — GCC accepts what clang rejects), and the
      paragraph must **name no connector** at all. It said *"charge over USB-C"*,
      and **the X3 has no USB-C port**; one binary drives both models, they do not
      share a socket, and no `BoardProfile` field describes one, so any named
      connector is false on one of the two.
- [ ] **10.3** `=2`, then **hold** power for at least 600 ms — the badge now says
      `CHARGE · HOLD POWER TO WAKE`, and that is the gesture, because
      `requireHeldPowerButtonOrSleepAgain` runs BEFORE the charge gate. Expect
      `[boot] battery pct=2 critShut=1 -> refused` and **nothing repainted**: the
      glass still holds the screen from the step above, because the gate runs before
      `display.begin()` and spends no waveform. Repeat three times and confirm the
      flag is given back each time — a refusal that consumed it would let the fourth
      hold boot a flat device. **A TAP is a separate case and refuses for the OTHER
      reason** (`[wake] refused`, the hold gate), so it proves nothing about
      `critShut`; hold, or you are testing the wrong gate.
- [ ] **10.4** Rebuild **without** the flag on a charged pack and **hold** power: the
      reader's page comes back, not Home. That is `markSleeping()` inside
      `criticalShutdown` redeeming the board's *"Your page is saved"*.
- [ ] **10.5** `=2`, then **plug the device in and leave it alone**. It must stay
      asleep with the BATTERY EMPTY screen on the glass and nothing must repaint.
      **There is no charge-detect wake source on this hardware** — `usbDetect` is a
      declaration nothing in the SDK reads, and on the X3 that pin is the gauge's own
      I2C SDA — and no timer wake either, because on battery the chip is fully
      powered down. This step is what the old `CHARGE TO WAKE` badge promised and
      what an X3 reported as not happening.
- [ ] **10.6** X3 only, `=2` **on the cable**: the device does **not** shut down.
      `charging` suppresses `Critical` and not `Low`, so the banner is still right
      to be up. An X4 has no charge-status pin and cannot show this.
- [ ] **10.7** `=2`, **in a book, on the cable, X3, and leave it for a few minutes**
      (#96). `charging` clears the critical run, so the level rests at `1` — which
      still selects the **fast** cadence, so `pollMs=2000` on `[alive]` even though
      the Reader is on glass and the band's repaint is unreachable. That is the arm
      of `pollIntervalMs()` that keeps the shutdown honest, and this is the only
      state on the device where you can see it hold the fast interval for the
      **ladder** rather than for the bolt. `pollMs=30000` here would mean a flat pack
      is being watched at a cadence three times its own dwell.

## 11. Every hint slot does what it says

Walk each built screen and press all four front buttons plus both side buttons.
This project has shipped a dead button twice — a control that draws and does
nothing reads as a broken device.

- [ ] **11.1** Home, Library, book details, item actions, delete confirm,
      Settings, Typography, the reader, the reader menu, contents, the peek,
      SD-missing.
- [ ] **11.2** The side buttons turn pages **in the direction they point**.
- [ ] **11.3** Hold Up or Down on the Library: it scrolls, accelerating, and
      **rests at the end rather than wrapping**. A single press at the end wraps.
- [ ] **11.4** `UP` on the reader page does nothing when there is no return
      anchor ahead of you. Correct, and it reads as broken — if it bothers you
      on glass, that is a design finding worth a card.

## 12. Progress survives — **[unplugged]**

- [ ] **12.1** Read into a book, sleep, wake, page on, then Back to Home.
      Home's CONTINUE block names the book and a percentage that matches.
- [ ] **12.2** The Library row for that book shows the same percentage, not
      `NEW`.
- [ ] **12.3** **The percentage never goes backwards** — check it across a
      chapter crossing and across the moment a deferred page count lands. This
      has been reported from a device once.
- [ ] **12.4** Power the device down hard mid-chapter. On the next boot the
      position is at most a couple of seconds stale, not a chapter stale.
- [ ] **12.5** Delete a book from the Library and re-add it. Its progress is
      still there.

---

## What to do with what you find

**File a card, not a note.** A deferral with no card is a deferral nobody will
find, and this repo has lost the same finding to a prose bullet more than once.
Put it on [the board](https://github.com/users/Rukkaitto/projects/1) with the
`Kind` that names the skill to fix it, and let the roadmap hold the reasoning.

**Keep the capture.** `run.log` plus `python3 tools/latency.py run.log` is the
evidence a card needs, and the medians are how a "it feels slower" becomes a
number. Median, not mean: one interaction that caught the card doing internal
housekeeping drags a mean somewhere no press ever was.

**A pass here is what moves a card from `On glass` to `Done`.** The desktop
cannot produce that evidence, so no agent can close one of those cards.
