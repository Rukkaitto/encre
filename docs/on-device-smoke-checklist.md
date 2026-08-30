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

## 4. Sleep and wake — **[unplugged]**

The decisive test needs no logger, and attaching one invalidates it: deep sleep
powers down USB, so re-enumeration can reset the chip and turn the wake you are
investigating into a cold boot that looks exactly like a bug.

- [ ] **4.1** Open a book, read a few pages, press power. The sleep screen
      paints before the device goes down.
- [ ] **4.2** Press power again. **The screen you left comes back.** If it does,
      the wake works and any earlier contrary evidence was the logger.
- [ ] **4.3** Sleep from Home, from the Library, and from inside a book. Each
      wakes back to where it was, focus included.
- [ ] **4.4** Sleep with a peek open. Waking to the **page underneath** is
      correct and deliberate — a peek is a transient "am I sure?".
- [ ] **4.5** Now plug in and read the log: `[boot] reset reason=... slept-flag=...`
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
      the badge shown**, and `[cover]` names which of the five refusals it was.
      A progressive JPEG is a stated refusal.
- [ ] **8.5** In Settings, switch `Shows` to `DETAILS` and confirm `Cover fit`
      becomes unreachable — a fit is meaningless with no cover on the glass.

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

## 10. Every hint slot does what it says

Walk each built screen and press all four front buttons plus both side buttons.
This project has shipped a dead button twice — a control that draws and does
nothing reads as a broken device.

- [ ] **10.1** Home, Library, book details, item actions, delete confirm,
      Settings, Typography, the reader, the reader menu, contents, the peek,
      SD-missing.
- [ ] **10.2** The side buttons turn pages **in the direction they point**.
- [ ] **10.3** Hold Up or Down on the Library: it scrolls, accelerating, and
      **rests at the end rather than wrapping**. A single press at the end wraps.
- [ ] **10.4** `UP` on the reader page does nothing when there is no return
      anchor ahead of you. Correct, and it reads as broken — if it bothers you
      on glass, that is a design finding worth a card.

## 11. Progress survives — **[unplugged]**

- [ ] **11.1** Read into a book, sleep, wake, page on, then Back to Home.
      Home's CONTINUE block names the book and a percentage that matches.
- [ ] **11.2** The Library row for that book shows the same percentage, not
      `NEW`.
- [ ] **11.3** **The percentage never goes backwards** — check it across a
      chapter crossing and across the moment a deferred page count lands. This
      has been reported from a device once.
- [ ] **11.4** Power the device down hard mid-chapter. On the next boot the
      position is at most a couple of seconds stale, not a chapter stale.
- [ ] **11.5** Delete a book from the Library and re-add it. Its progress is
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
