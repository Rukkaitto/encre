# Encre

From-scratch firmware for the Xteink X4/X3 e-readers (ESP32-C3, e-ink). Built to
have exactly the UI/UX we want, so **design fidelity is a functional
requirement**, not polish.

## Layout

| Path | What it is |
|---|---|
| `core/` | Portable C++20, namespace `reader::`. **No Arduino, ESP or host-OS dependency** — it compiles for macOS and the ESP32 alike. Framebuffer, fonts, text, icons, dither, view-models, themes. |
| `sim/` | Desktop simulator: renders a screen to PNG at exact panel size. Where UI iteration happens. |
| `shell/` | The Arduino layer. Device detection, display bring-up, the paint sequence. The only place that touches `freeink-sdk`. |
| `tools/` | Asset generators (`fontc.py`, `iconc.py`, `embed_font.py`) and the design comparison tool. |
| `design/` | `*.dc.html` design boards — **the source of truth for the UI**. |
| `docs/superpowers/` | The spec, the roadmap, and per-phase implementation plans. |
| `freeink-sdk/` | Submodule. MIT drivers for display/input/SD/battery. Never edit. |

## Commands

```
make test       # build core + run unit and golden tests (this is the fast loop)
make sim        # render Home to build/home.png
make firmware   # build for the ESP32-C3
make fonts      # regenerate the .rfnt type ramp and embedded headers
make icons      # regenerate icon bitmaps from the design boards' SVG
make compare    # design-vs-firmware contact sheet for every screen
```

`make compare COMPARE_ARGS="--only home --export build/overlay"` writes bare
panel-size PNGs for overlaying in a design tool.

CMake uses `file(GLOB ...)`: **re-run `cmake -S . -B build` after adding or
removing a source file**, or it is silently ignored.

## The rule that governs UI work

**A UI change goes into the design HTML first, then the implementation.** Never
only in code, and not the other way round — including when the design itself is
what is wrong (fix the board, then follow it). `make compare` is what keeps them
honest; changing only the implementation silently invalidates it and the goldens
stop meaning anything.

## Hardware facts

- One binary drives both models. **The panel controller varies by production
  batch**, so the firmware must identify the hardware before touching the
  display: I2C fingerprint for X3-vs-X4, board profile select, then a
  display-bus probe that promotes the profile to `XteinkX3Uc8279`. Skipping the
  probe drives a UC8279 with the UC8253 driver: the power-on handshake succeeds
  and the refresh wait then dies at its 30-second timeout.
- The dev device is an **X3 with a UC8279** — 792×528, so a 528×792 portrait
  canvas. The X4 is 800×480 → 480×800. Both are ~220 PPI.
- **Rotation is CCW**, measured. CW renders 180° out. Unverified on X4.
- `pio` is not on PATH: `~/.platformio/penv/bin/pio`. The Makefile handles it.
- **Flashing must be run by the user** — the permission classifier blocks it
  from an agent. Give them the command.
- E-ink holds its last image with no power, so **a frozen screen does not mean
  the firmware ran**. It has disguised a crash loop and a bootloader hang as
  "nothing happened". Read the serial log before believing the panel.
- **The SD card shares the display's SPI bus** (X3: MISO 7, CS 12) and
  `SDCardManager` does **no locking** — there is no mutex or semaphore anywhere in
  it. Its only shared-bus handling is in `begin()`, which drives the display CS
  high before probing because a powered, never-deselected panel breaks card
  detection. So **the caller must keep SD traffic off the bus during a panel
  refresh**; a transfer racing a refresh is the kind of fault that looks random.
  That is `SpiBusGuard` — see **Storage** for the two places that take it.
- `~/encre-device-backup/restore.sh` restores the device to CrossInk.

## Rendering model

Glyph and icon coverage is 2 bpp — 0..3 per pixel — and the framebuffer is
1-bit. How that coverage gets onto the panel is the screen's declared `Fidelity`
(`core/include/reader/refresh.h`), and there are three answers.

**`Fidelity::Mono` — one pass, one waveform, hard threshold. This is what ships,
and it is the default.** `Plane::Bw` inks where coverage ≥ 2 and leaves paper
where it is ≤ 1. No stipple, no grey, no anti-aliasing: chrome is
hard-thresholded 1-bit.

**Why, when 2A-2 measured thresholded chrome as illegible: because the reference
firmware does exactly this, on this glass.** CrossInk (a CrossPoint derivative,
the stock firmware) builds its **UI fonts 1-bit** and only its *reader* fonts
2-bit, and its "Text Anti-Aliasing" setting is read **only** by the EPUB/TXT
reader activities — never by a menu, home, library or settings screen. Its chrome
therefore has no anti-aliasing at all, and it is legible. Compared side by side
on the X3 against our dithered chrome, the hard edge won. What 2A-2 actually
measured was a *px-authored* type ramp that was too small for the panel; the
pt-at-150-DPI ramp fixed that, and every role is now ≥ 21px, where a 2px stem
fully inked is cleaner than a 2px stem inked 5/8 of the way.

Two things the re-bless made concrete, and both are worth knowing before
predicting what thresholding will do:

- **It makes foreground heavier, not lighter.** Coverage 2 is more common than
  coverage 1 on this ramp (3618 px vs 1702 on Home), so thresholding *fills* more
  edge than it *drops*: Home gained 806 pixels of ink. The naive fear — "hard
  1-bit thins the text out" — is backwards here.
- **The exception is a thin diagonal.** `kChevron`'s stroke is mostly
  coverage-1 pixels, so it comes out one notch lighter and much crisper. Diagonals
  are where to look if a mark ever reads too faint.

**`Fidelity::Dithered` — also one pass and one waveform, but stippled.**
`Plane::BwDithered` puts partial coverage through a **dispersed Bayer 4×4**
instead of thresholding it: `cov*16/3 > bayer4(x,y)` inks 5 cells of 16 at
coverage 1 and 10 at coverage 2, keyed on absolute panel coordinates so a mark
and the label beside it share one grid. Full coverage stays solid and zero stays
blank, so **a glyph interior is never stippled** — only its edge is.

It is **implemented, tested and hardware-verified**, and it is not deprecated. It
costs exactly what `Mono` costs, and it is the right answer where a stroke is wide
enough for a stipple to read as tone rather than as grain — Home's 67px `6%`
numeral is the one element on the screen that measurably got worse on `Mono`, its
curves going from smooth to a countable staircase. Chrome ships `Mono` anyway,
because at 21px the same stipple reads as noise on the stroke rather than as a
soft edge, and small type is most of chrome.

**`Fidelity::Grayscale` — three passes plus a rebase, three waveforms.** The
panel shows **four grey levels** from two bit-planes the controller combines;
there is **no 2 bpp framebuffer**, it would not fit, so the screen is drawn
three times into the 1-bit buffer, and a fourth time to rebase the controller
onto a valid B/W baseline afterwards:

| Pass | Emits | Consumed by |
|---|---|---|
| `Plane::Bw` | ink where coverage ≥ 2 | `displayGrayscaleBase` |
| `Plane::Lsb` | bit 0 of coverage | `copyGrayscaleLsbBuffers` |
| `Plane::Msb` | bit 1 of coverage | `copyGrayscaleMsbBuffers` |

**It costs three panel waveforms: 366 + 366 + 156 ms, and 1363 ms for a focus
move measured end to end on the X3**, against one waveform for either one-pass
path. No screen declares it today. It is kept, not deprecated, because it is the
only way to put continuous tone on this glass — Phase 3's question about book
covers and images — and because the sequence was expensive to get right; the
comments in `paintGray()` were each earned by breaking the panel. **Windowed
grayscale is not the escape hatch**: rotation is CCW, so a portrait row band
becomes a full-height landscape column band and every gate line is driven
anyway.

**Rules, fills and dither** have coverage 0 or 3, so they are identical in every
pass — `Bw`, `BwDithered` and all three grayscale planes. That is what makes a
plane bug show up as fringing rather than missing furniture, `test_components.cpp`
pins it for `drawRow`'s hairline and the header band's rule, and it is why each
re-bless of Home — first onto the dithered path, then onto `Mono` — moved **only**
partial-coverage pixels, verified per pixel against the coverage map rather than
by eyeballing totals.

**Icons are not in that set.** All ten shipped marks are 2 bpp
(`core/src/icons.cpp`) because they are generated anti-aliased from the boards, so
they legitimately carry partial coverage at their edges and take whichever
treatment the plane implies. `Icon::bpp == 1` is the opt-in for a mark that wants
hard 1-bit edges, and nothing uses it — it is now redundant for chrome, because
`Plane::Bw` gives every mark hard edges anyway.

**The small round marks are what settled the chrome question.** `kDot`,
`kBattery` and `kBook` are the marks a 4×4 dither serves worst: a 1px rim at
near-constant coverage has no tone to dither, so the Bayer phase just picks which
rim pixels survive, and the mark comes out visibly moth-eaten. On `Mono` the
battery is a clean outline with a solid fill and a solid terminal nub, the book is
an unmistakable two-page spread with a solid spine, and the bullet loses its four
single-pixel whiskers. Thresholding **improved** all three, measurably and
visibly. Same for `kForward`, whose dithered arrowhead was frayed.

**The two dither matrices are deliberately different, and `dither.cpp` says
why** — and `kClustered` still ships, on the cover placeholder, whatever the
screen's fidelity. `kClustered` is for *tints*: the board's cover placeholder is
one round dot repeated on a 4px grid, and dispersing that area into isolated
pixels reads denser and grainier than the blob it is meant to be. `kBayer` is for
*edges*, and is what `Plane::BwDithered` uses — clustering a stroke's edge
coverage would pile the ink against the stroke and read as the stroke thickening,
which is the one thing an anti-aliased edge must not do. Same nominal coverage,
opposite arrangement, opposite jobs.

## Runtime

The interaction runtime is *logic*, so it lives in `core/` and is unit-tested on
the desktop: `PressRecognizer` (input.h), `App` + `Screen` (app.h),
`RefreshPolicy` (refresh.h), `IdleTimer` (power.h). The shell contributes only
what needs hardware — raw button samples, the panel calls, deep sleep.

- **One physical press is exactly one event.** A hold fires `Long` while the
  button is still down; the release then emits nothing. A button *outside* the
  long-press mask fires `Short` on release however long it was held — never
  nothing. And because `tick()` only runs from the main loop, which a gray
  refresh blocks for ~1.5 s, the **release edge classifies the press too**: a
  hold made entirely inside a repaint would otherwise arrive as a `Short`, which
  on a list means opening the item instead of its actions overlay.
- **`InputManager::beginAsync()` cannot support a long press** — it queues press
  edges only, no releases and no durations. `shell/src/input_task.cpp` is its own
  poll loop over `update()`, queuing both edges with a `millis()` timestamp.
  **Only that task may call `update()`**; it owns the edge state.
- **The hint bar's hold ring and the long-press binding read one field** — the
  view-model's `holds` array, via `hintHoldMask()`. So a screen cannot promise a
  hold it has not bound, or bind one with nothing on screen to suggest it. The
  mask follows the top of the stack, so refresh it after every dispatch.
- **Screens declare a `Fidelity` and the default is `Mono`**, so a screen opts
  *in* to a more expensive or more unusual path rather than out of it — nothing
  declares `Dithered` or `Grayscale` today, and the old `Gray` default is what had
  every chrome screen paying 1363 ms a paint by saying nothing. If one ever does
  declare `Grayscale`, `mode=FAST` beside `fidelity=gray` in a `[paint]` line is
  not a contradiction: the grayscale sequence has no differential form, so it
  means the cadence had a fast slot the screen could not use.
- **A screen transition DOES force a FULL refresh, and there is no periodic
  cadence.** `kFullOnTransition = true`, `kFullRefreshEvery =
  RefreshPolicy::kNever`. This is neither the reference firmware's behaviour nor
  the obvious one, so the reasoning matters:
  - CrossInk skips the transition FULL on this panel
    (`ScreenTransitionRefresh::modeFor` returns FULL only for
    `screenChanged && !deviceIsX3()`) and **accepts the ghosting**. That ghosting
    is observable — reported on its settings screen on this device.
  - The distinction is not flash versus no flash. A flash on a **screen change**
    is expected on an e-reader, as Kindle and Kobo do, because a differential
    update there has a whole screen of stale content to ghost through. A flash on
    a **focus move** inside one screen is a defect. Those are separate settings,
    so we take one and not the other.
  - A periodic cadence was tried at 1-in-15 and made things *worse*: it put the
    flash on an arbitrary navigation, which reads as more random than the
    transition flash it replaced. No ghosting has been observed without it, and
    the ink accumulation this project did once see came from a missing grayscale
    settle pass on a path chrome no longer takes.
  - Cost, measured: a transition takes the 693 ms GC waveform against 389 ms for
    the DU, so ~825 ms versus ~520 ms. Focus moves are untouched.
  - Both become Settings rows in 2C; `kFullOnTransition` needs a **new** row on
    the board first (spec §10).
- **`core/include/reader/screens.h` is the one screen catalogue**, shared by the
  simulator and the shell. Two factories would drift, and the drift would be
  invisible because each half keeps passing its own checks.
- **Deep sleep is a chip reset**, so RAM state is lost — the last screen comes
  back from the NVS session record instead (see **Storage**), and only across a
  genuine wake; a cold boot starts at Home. Wake is the **power button only**: the six front buttons are
  ADC-ladder bands on GPIO 1/2 and produce no GPIO edge, while power is a real
  GPIO (3, active-LOW). Sleep order is `display.deepSleep()` →
  `PowerManager::powerDownRailsForSleep()` → `deepSleepUntilPowerButton()`. That
  middle call does cut the X3's SD rail (the profile declares
  `sd.powerEnable = 13`), despite the SDK header calling it a no-op on X3/X4.

## Storage

**`core/` sees one interface — `reader::FileSystem`** (`filesystem.h`) — and never
learns what backs it: `exists`, `list`, `readAll`, `writeAll`, `mkdirs`, `remove`,
plus `mounted()`. Three implementations, and the contract is what keeps them one
thing:

| Implementation | Lives in | In which build |
|---|---|---|
| `FakeFileSystem` | `test/unit/fake_fs.h` | Unit tests. In-memory, injectable failures. |
| `HostFileSystem` | `core/src/host_fs.cpp` | **Desktop only** — the simulator and desktop tests. |
| `SdFileSystem` | `shell/src/sd_fs.cpp` | The device, over `SDCardManager` (SdFat). |

- **`host_fs.cpp` is excluded from the firmware exactly as `png.cpp` is**, via
  `core/library.json`'s `srcFilter`, because `<filesystem>` is a host-OS
  dependency. Belt and braces: the body is also guarded by `READER_DESKTOP`, so a
  filter that silently stopped matching yields an empty object file rather than a
  `<filesystem>` include reaching the ESP32 toolchain.
- **The contract is written once** (`core/include/reader/fs_contract.h`) and
  reported through a callback, so `test/unit/test_filesystem.cpp` drives it with
  doctest on the desktop and `shell/src/sd_selftest.cpp` drives the same clauses
  against a real card over serial. `shell/` has no test harness, so without that
  seam `SdFileSystem` would be the one implementation nothing checks. Build it in
  with `PLATFORMIO_BUILD_FLAGS="-DENCRE_FS_SELFTEST=1" make firmware` (that
  variable **appends** to `platformio.ini`'s flags; `--project-option` would
  *replace* them and silently build for the wrong board). It costs ~16.8 KB and is
  a stub returning **-1** — not 0 — otherwise, so a build without it cannot be
  mistaken for a build that passed.
- **The path normaliser exists in three copies**, one per implementation, and
  nothing but the contract's "a redundant or trailing separator addresses the same
  thing" clause holds them together. They live in three build worlds (Arduino,
  desktop-only TU, test header), so the duplication is deliberate — but a fourth
  implementation should extract it rather than copy it again.
- **`readAll` is not the EPUB path.** It is for the small JSON files V1 stores and
  `SdFileSystem` caps it at 64 KB, because `-fno-exceptions` makes a `resize` that
  cannot allocate an `abort()` with no diagnostic. Streaming (a handle with
  `read(buf, n)`) is **Phase 3's**, and its absence is a decision, not an
  oversight: EPUBs are megabytes against ~230 KB of heap.

**Settings are flat JSON at `/.reader/settings.json`** (spec §5), read by a
minimal one-object parser in `core/` (`json.h` — no nesting, no arrays; Phase 3's
bookmark array will outgrow it). Nothing is vendored and there is no network to
fetch a parser with.

- **A bad file is replaced, not trusted**: missing, unreadable, unparseable or an
  unknown `version` all mean defaults. An out-of-range *value* is different — that
  field is **clamped** and the rest of the file still loads, because refusing to
  boot over one bad number is worse. `loadSettings` returns false either way.
- **The boot log distinguishes those two**, `DEFAULTED` from `CORRECTED`, and names
  the reason. That line is the only way a user ever learns their hand-edited file
  was rejected rather than applied.
- **The `Settings` struct's defaults are the constants the shell used to compile
  in** (`kSleepAfterMs`, `kFullRefreshEvery`, `kFullOnTransition` through 2B), so a
  device with no card behaves exactly as it did. `shell/src/main.cpp` holds the
  *reasoning* for each number; `settings.h` holds the number.

**The wake pointer is in NVS, not on the card**, because a wake must work with no
card in the slot — which is the entire state the SD-missing screen exists for.
`Preferences` namespace `encre_sess`, keys `ver` / `scr` / `focus` (NVS caps a key
at 15 chars). The version key is written **last**, like a commit record, so a write
that dies half way reads back as "no session". Restore happens **only on a genuine
wake**; a cold boot starts at Home and clears the record. **The stored focus is
always 0** — `reader::Screen` has no focus accessor, and the only screen where a
restored focus would show is Library, which lands in 2C-2.

**Two limitations to know before trusting the card:**

- **A card pulled after a successful mount cannot be re-mounted without a
  reboot.** `SDCardManager::begin()` opens with `if (initialized) return true;` and
  the SPI path exposes no `end()`/`unmount()`, so it reports success without
  touching the hardware. So the shell never accepts `mount()` alone: it requires
  `probe()` (a real root-directory read) to agree. A RETRY that reports success and
  then fails is worse than one that stays put — so **RETRY has two branches**
  (`handleRetry`), told apart by `gSdBeganOnce`: never mounted this boot means the
  in-place attempt is real and is kept, while mounted-then-lost **restarts the
  device** (`esp_restart`), because boot is the only code path that re-runs the
  mount. The restart is forced by the SDK, not a workaround for our own bug.
- **`mounted()` is "the card was there and nothing has since told us otherwise".**
  There is no card-detect GPIO in the board profiles and `SdCard::status()` (the
  one cheap CMD13) is private to `SDCardManager`, so liveness is maintained from
  operation feedback plus `probe()`. `probe()` can be satisfied from SdFat's sector
  cache, so it is not a card-detect either.
- **A pull is detected proactively**, because operation feedback alone never fires:
  V1 does almost no filesystem work after boot, so a card pulled on Home stayed
  invisible. `pollCardPresence()` runs `probe()` every `kSdPollMs` (2000) from
  `loop()`, **after** the paint block and gated on `!gApp->dirty()`, under the same
  `SpiBusGuard` — it is SPI traffic on the panel's bus, and it costs battery on a
  device built to sit idle. A usable → unusable edge rebuilds the `App` rooted at
  `SdMissingScreen` (a fresh `App` is dirty and in transition, so it paints as a
  screen change) and leaves the session record alone.

**The shared SPI bus is handled in exactly two places.** Every public method of
`SdFileSystem` takes a recursive `SpiBusGuard`; `renderTop()` in
`shell/src/main.cpp` takes the same guard around the **whole** paint, BUSY waits
included, because the driver keeps the display's CS asserted across them. Today
both run on the Arduino loop task, so this is free insurance — it is there to be
structural rather than a rule someone remembers when a background library scan
moves off that task.

**`SPI.begin()` ends up called twice on one bus** — `detectAndSelectBoard()` with
the display's pins, then `SDCardManager::begin()` with the card's, which is the
reverse of the order the SDK's comments assume. It should be benign (the CS-high
mitigation still applies, and SdFat sets per-transaction `SPISettings`), and the
card is mounted **after** the display is up. **The first paint after a mount is the
thing to watch on hardware**: a bad refresh with a card in and a clean one without
is this call order and nothing else.

## Type

Sized in **points at 150 DPI**, CrossPoint's convention: `ppem = pt * 150 / 72`.

| Role | pt | px | Role | pt | px |
|---|---|---|---|---|---|
| Meta400 / Meta500 | 10 | 21 | Body400 / Body500 | 14 | 29 |
| Label400 / Label500 | 11 | 23 | Title700 | 20 | 42 |
| Value500 / Value700 | 12 | 25 | Display700 | 32 | 67 |

Below ~10pt is not legible on this glass, measured. **A role names size AND
weight**; `FontSet::load` refuses to bind a role to an asset built at a
different ppem or weight, so a mis-binding is a boot failure rather than a
silently wrong screen. `core/` never picks its own fonts — the caller supplies a
`FontSet`, which is how device knowledge stays out of the portable layer.

## Invariants worth not relearning

- **Derive from the board's box model; never pin a number the board computes.**
  Three separate defects came from hardcoding a height: the header band (6px
  out), menu rows (content-box 80 + 1px border = 81, compounding a pixel per
  row), and the hint bar's asymmetric padding. `headerBandHeight()` and
  `hintBarHeight()` derive and return their height.
- **Round once.** Positions accumulate in fixed point and round at the end.
  Three truncating divisions put an icon 1.5px low; pre-rounding 2.52px tracking
  to 3 drifted a label ~3px.
- **Fix in the primitives, not the screen.** Every fidelity defect so far was
  found on one screen and belonged in `components.cpp` / `text.cpp` /
  `dither.cpp` / a generator. Special-casing a screen means the next screen
  inherits the bug.
- **Assets are generated from the design, not transcribed.** `iconc.py` reads
  each icon's SVG and size from its named board at generation time. It once held
  copies and silently swallowed a design fix.
- **The hint bar has exactly four slots and is always one line tall**, one slot
  per front button, in hardware order (Back, Confirm, Up, Down). A button with no
  action gets an empty slot. A long-press variant is a **hollow ring after that
  slot's label** — never a second line and never a fifth hint. Two lines were
  tried and rejected: a bar whose height varies by screen moves every list
  stacked above it, and "· HOLD" does not fit four slots at 10pt on the 480-wide
  X4.
- **An empty hint slot is 36px wide, not zero** (`kHintEmptySlotW`). Eight boards
  author a dead button as `<div style="width: 36px;"></div>` and
  `space-between` divides the leftover around it. Measuring it as 0 is not
  "drawing nothing", it is drawing the *other* slots in the wrong places: on
  SdMissing it moves RETRY 36px left and widens each gap by 12px.
- **Three shared primitives landed with the SD-missing screen** (2C-1), and the
  next screen that needs them should find them rather than reinvent them, both in
  `components.h`:
  - `drawActionButton` — the boards' **primary action slab** on a full-screen
    prompt: 68px tall (`kActionH`), no border, one centred Value700 label at
    `letter-spacing: 0.18em`. Five V1 boards draw it and all five state the same
    box; **the width is not shared** (SdMissing pins 260, the overlays take their
    column). It is *not* Home's CONTINUE block, which is 72 tall, left-aligns and
    carries an arrow. The outlined secondary variant belongs in this function when
    DeleteConfirm or BookError lands, not before.
  - `wrapProse` / `drawProse` — the **first paragraph in the firmware**. A
    paragraph's height is a *result* (face × copy × column), not a number the
    board states, so the wrap is a value computed once and then both measured and
    drawn; two calls that each re-wrapped would be two chances to disagree, and
    the disagreement would read as a paragraph drifted off centre. Greedy on
    ASCII spaces, no hyphenation, no CJK breaking — Phase 3's EPUB text is a
    different problem with a different budget.
  - And the reason SdMissing's board says `max-width: 420px` where it used to say
    400: **the wrap follows the firmware's own metrics, not Chrome's.** The
    autohinted `.rfnt` faces have whole-pixel advances and measure ~3% wider, so
    the board's three lines came out as four. 420 is three lines in both engines
    and moves nothing in Chrome. The number was wrong, not the design.

## Goldens

`test/golden/*.png` are human-approved, pixel-exact baselines. A golden test
that fails writes `build/<name>_candidate.png` and names both paths.

**Never re-bless a golden to make a test pass.** A failure means either an
intended visual change (inspect the candidate, confirm it is right, then bless)
or a regression (find the bug). Blessing to silence a red test destroys the only
protection the rendering has. `text_sample.png` is Literata body text: if it
changes and you did not mean to touch body rendering, stop.

Inspecting a candidate means **looking at the pixels and saying what you see**,
including whatever looks wrong in a change you go on to bless. An icon has passed
review twice while reading as the letters "OC". When a change is meant to move
only some class of pixel — edges, say — the strongest check is to prove that
nothing outside that class moved, per pixel, rather than to eyeball the totals.

Home's four goldens are two-level renders of `Plane::Bw`, via
`golden::checkGolden`, because Home takes the default `Fidelity::Mono`. Both
golden tests assert that fidelity before naming the plane, so a change to the
shipped path fails the test rather than leaving the goldens quietly pinning a path
nothing paints. `golden::checkGoldenGray` composes two planes into a 4-level image
and is for a screen on the grayscale path; nothing uses it today.

**The check that made the last two re-blesses trustworthy** was not a visual one:
compose `Plane::Lsb` and `Plane::Msb` into the 4-level coverage map (that map's
level per pixel *is* the coverage the renderer computed), then assert that **no
pixel of coverage 0 or 3 moved**. Both re-blesses came out at exactly zero, which
proves the header band, every rule, the cover's dither block, the progress bar,
the inverted block and row fields and every hint-slot position are bit-identical,
without needing to trust an eyeball on 384000 pixels. Note the level→byte mapping
is `0..3 → white..black`, so **byte 170 is coverage 1 and byte 85 is coverage 2** —
easy to get backwards, and it inverts the conclusion if you do.

## Where to look

`docs/superpowers/plans/2026-08-20-v1-roadmap.md` — phases, and two sections
worth reading before starting anything: "What Phase 2A-2 established" and the
on-device bring-up findings.
