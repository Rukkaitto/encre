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
make compare    # design-vs-firmware contact sheet, all 28 boards (~2.5 min)
```

`make compare COMPARE_ARGS="--only home --export build/overlay"` writes bare
panel-size PNGs for overlaying in a design tool.

CMake uses `file(GLOB ...)`: **re-run `cmake -S . -B build` after adding or
removing a source file**, or it is silently ignored.

## What V1 is, and is not

**V1 IS CARD TRANSFER ONLY. Wi-Fi is cut.** It was too big, and cutting it took
nine boards out of the comparison sheet with it — Transfer, the five Wi-Fi flows
and SetupHotspot. They are **parked, not deleted**: `V2_SCREENS` in
`tools/compare-design.py` keeps them reachable by `--only` so a V2 design can still
be rendered, without counting them as V1 work nobody is doing. Instapaper was cut
the same way earlier (canvas page "V2 · Instapaper").

Two consequences that are easy to trip over:

- **Settings has no CONNECTIONS section**, which is what brought its list back
  inside the panel — see the scroll rail under **Overlays and lists**.
- **HomeEmpty has no action slab.** Its board's call-to-action was
  `SEND BOOKS OVER WI-FI`, and a primary action that cannot work is worse than
  none, so the copy carries it: *"Put the SD card in your computer and copy EPUB
  files into its /books folder."* The slab returns with Wi-Fi.

## The rule that governs UI work

**A UI change goes into the design HTML first, then the implementation.** Never
only in code, and not the other way round — including when the design itself is
what is wrong (fix the board, then follow it). `make compare` is what keeps them
honest; changing only the implementation silently invalidates it and the goldens
stop meaning anything.

**And it has to actually cover the screen.** Until a cold-read review found it,
`make compare` defaulted to the seven V1 boards, so it compared Home and Library
and skipped four of the six implemented screens — while CLAUDE.md called it the
check that keeps the design honest. `--only <flow screen>` matched nothing at all,
silently. The default is now every board, and `--only` errors on an id it does not
recognise rather than reporting `0/0`. **A check that reports on less than it
claims is worse than no check, because it is trusted** — the same shape as the
card probe that was answered from cache and kept reporting success.

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
- `pio` is not on PATH; use `~/.platformio/penv/bin/python -m platformio run -e
  xteink`, which is what `make firmware` does via `PIO_PY`.
- **"Failed to install Python dependencies into penv" is TRANSIENT — retry it.**
  This note used to say the module entry point *skips* that check. It does not,
  and no entry point can: the check is in the **platform's** builder script,
  `~/.platformio/platforms/espressif32/builder/penv_setup.py`, not in the `pio`
  launcher. Three things it actually does, all worth knowing:
  - It is gated on `has_internet_connection()`. With no network it skips the
    check and says so; with a network it runs `uv pip install --upgrade` for the
    platform's Python deps. So the failure means "the network was there and the
    install failed" — a PyPI hiccup, or two builds sharing the `uv` cache at
    once. **Do not run two builds concurrently.**
  - On failure it is `sys.exit(1)`, so a broken build cannot be mistaken for a
    good one and `make firmware` fails honestly. (Observed once as exit 0 —
    that was `$?` reading a piped `tail`, not PlatformIO.)
  - It runs `--upgrade` on **every** build that has a network. So the toolchain's
    Python side can move under the project at any time without a change on our
    part: a build that worked yesterday can pull a new esptool today. That is the
    real hazard here, not the transient failure, and it is unpinnable from our
    side.
- **The app partition is 6.25 MB**, from the committed `partitions.csv` — the
  board default gave 1.31 MB, which Phase 2C had already half spent. `nvs` and
  `app0` keep the default table's offsets, so the session record survives a
  repartition and an ordinary upload still lands correctly. There is now a
  `coredump` partition too, so a panic can be recovered with
  `make firmware` then `pio ... -t coredump` instead of being reconstructed from
  the serial log.
- **Flashing must be run by the user** — the permission classifier blocks it
  from an agent. Give them the command.
- **ATTACHING A SERIAL LOGGER CAN TURN A WAKE INTO A COLD BOOT.** Deep sleep
  powers down USB, so a resume has to re-enumerate and the host has to reopen the
  port — and on the C3 the USB Serial/JTAG peripheral can reset the chip when that
  happens. Three consecutive attempts to capture a resume came back
  `rst:0x15 (USB_UART_CHIP_RESET)` with `wake cause=0`: cold boots, in captures
  whose whole purpose was a wake. So **a wake may be unobservable by the usual
  route**, and worse, it looks exactly like a bug — a session restore that
  "stopped working" while a logger was attached is the restore correctly declining
  to run, because from the firmware's side it really was a cold boot.
  - `[boot] reset reason=…` distinguishes the cases: `DEEPSLEEP` is a real
    resume, `USB` is the host having reset the chip, `POWERON`/`SW` means it never
    slept. Read that line before believing anything about a wake.
  - The decisive test needs no logger: sleep, press power, and see whether the
    screen you left comes back. If it does, the wake works and the logger was the
    problem.
  - **To get facts off that path, the record must be in NVS, not RTC memory.**
    `RTC_DATA_ATTR` looks right — free to write, survives deep sleep — and does
    not work here: ESP-IDF re-initialises `.rtc.data` on every reset that is not a
    deep-sleep wake, and the reset to survive is precisely the `ESP_RST_USB` the
    host causes by attaching. So plugging in to read the record is what erases it,
    and the log comes back with no `[prev]` line rather than with an error. The
    record lives in the `encre_diag` NVS namespace, written at a few decisive
    points (reason known, mount decided, first probe, card lost, first paint)
    rather than per stage.
- **A fresh git worktree has an EMPTY `freeink-sdk/`**, and `make firmware` then
  fails with `PackageException: not a directory`, which names neither the
  submodule nor the fix. `git submodule update --init` first. `make test` is
  unaffected, so a worktree can look healthy and still not build the firmware.
- E-ink holds its last image with no power, so **a frozen screen does not mean
  the firmware ran**. And **nothing clears the glass at boot** — the cold-boot
  branch calls `display.requestResync()`, which reseeds the CONTROLLER's DTM1
  baseline and never touches the panel, so the previous session's screen stays
  visible until the first paint. That is deliberate (a clear would be an extra
  full flash to show white) but it did once log itself as "clearing the panel",
  which is a claim about the wrong one of the two. The first paint is the earliest
  anything can appear, and it cannot happen before `display.begin()` returns.
- **`setup()` waits for the USB HOST, and it used to wait 2.5 s unconditionally.**
  A `delay(2500)` let USB CDC enumerate before the first print — about 60% of the
  time before the panel could show anything, spent so a serial log nobody was
  reading would be complete, on a device that spends its life unplugged. It now
  leaves as soon as `Serial` reports a host has the port open, and gives up after a
  400 ms grace when `HWCDC::isPlugged()` says nothing is there
  (`ARDUINO_USB_CDC_ON_BOOT=1`, so `Serial` is the USB Serial/JTAG CDC). The
  2500 ms cap is unchanged, so the worst case is the old behaviour, and
  **`[boot] waited Nms for USB CDC` is on the boot line** — a slow boot with a big
  number there is a USB question, not a firmware one.
  - **This line first said the 2.5 s was `XteinkDetect`'s I2C passes.** It was not:
    that figure was read off the first timestamped SDK log line without noticing
    that nothing before it was timestamped at all, and the delay was sitting in
    `setup()` two lines above. The detect passes cost ~66 ms by the SDK's own
    comment. **A log's first timestamp is not the same as time zero.**
- **E-ink persistence is about the PANEL, not the controller.** The glass keeps
  its image with no power; the controller's DTM1 baseline does not. A wake is a
  chip reset, so `initController()` re-runs and `_oldPlaneValid` goes false —
  which is what makes `displayStart()` seed DTM1 white. Telling the driver the
  baseline is still valid (`skipInitialResync()`) skips that seed and leaves the
  waveform diffing against garbage: on device that was a split second of noisy
  banding on every wake. Conflating the two is easy and it looks like a panel
  fault rather than a state bug. It has disguised a crash loop and a bootloader hang as
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

**It costs three panel waveforms: 367 + 366 + 156 ms, and 1363 ms for a focus
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

**Icons are not in that set.** All eleven shipped marks are 2 bpp
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

- **A HELD Up or Down on a list repeats, accelerating, and the step is derived
  from ELAPSED TIME rather than from a count of events.** That is forced by the
  panel: `tick()` only runs from the main loop and a paint blocks it for
  520-825 ms, so a conventional "one row per interval" scheme moves about two rows
  a second whatever interval it asks for — 256 books in over a minute. So
  `InputEvent::steps` carries the distance and one event stands for all the time
  the panel was busy. 6 rows/s ramping to 30 over 1.8 s after a 400 ms delay; the
  delay is what keeps a deliberate hold distinct from a slow tap, and the leftover
  fraction of a row is kept between ticks rather than truncated away.
  - **`autoRepeat` and `longPressable` are mutually exclusive, and `setAutoRepeat`
    ENFORCES it** by clearing the overlap rather than documenting it. A button in
    both has its press consumed by whichever fired first, so the behaviour would
    depend on how long the user held it and on when the loop happened to tick.
  - **It is deliberately NOT derived from the hint bar**, which is where
    `longPressable()` comes from. A hold ring promises a *different* action;
    auto-repeat is more of the *same* one, so it has nothing to announce.
  - A press that repeated emits **nothing** on release, exactly as a `Long` does:
    the repeats were the press, and a trailing `Short` would move the list one
    further row after the user let go. `forgetPresses()` also stops a repeat dead,
    which is what keeps a dropped release from leaving the list scrolling by
    itself.
  - One event's step is **capped**, so a pathological stall cannot cash in five
    seconds of held button as a 150-row jump. The surplus is dropped, not banked.
- **One physical press is exactly one event** — with `Repeat` the one exception
  above, which is why it is a distinct `PressKind` rather than a repeated `Short`. A hold fires `Long` while the
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
- **A RESUME ON BATTERY IS A POWER-ON RESET, and the reset reason cannot tell you
  it was a resume.** Measured on an X3: with USB attached the chip really
  deep-sleeps and returns `ESP_RST_DEEPSLEEP`; on battery the same sleep leaves it
  fully powered down, so pressing power gives `ESP_RST_POWERON` — indistinguishable
  from a first-ever boot. The restore is gated on "did we wake", so on battery it
  correctly declined every time and then cleared a perfectly good record. The
  symptom was "it always comes back to Home", and it was the gate being right
  about a question that had no answer — which is why reading the restore code
  found nothing wrong with it.
  - So the INTENT is recorded, not inferred. `markSleeping()` writes a `slept`
    flag immediately before the sleep call that does not return; `takeSleptFlag()`
    at boot reads **and clears** it. Clearing on read is deliberate: a boot that
    sets out to resume and then panics must not resume again on every boot after
    it. One flag buys exactly one resume.
  - **`[boot] reset reason=… slept-flag=… -> RESUME|cold start` prints the whole
    decision.** Read that line before believing anything about a wake.
- **Deep sleep is a chip reset**, so RAM state is lost — the last screen comes
  back from the NVS session record instead (see **Storage**), and only across a
  genuine wake or a recorded sleep; an unrecorded cold boot starts at Home. Wake is the **power button only**: the six front buttons are
  ADC-ladder bands on GPIO 1/2 and produce no GPIO edge, while power is a real
  GPIO (3, active-LOW). Sleep order is `display.deepSleep()` →
  `PowerManager::powerDownRailsForSleep()` → `deepSleepUntilPowerButton()`. That
  middle call does cut the X3's SD rail (the profile declares
  `sd.powerEnable = 13`), despite the SDK header calling it a no-op on X3/X4.

## Storage

**`core/` sees one interface — `reader::FileSystem`** (`filesystem.h`) — and never
learns what backs it: `exists`, `list`, `readAll`, `writeAll`, `mkdirs`, `remove`,
`openRead`, plus `mounted()`. Three implementations, and the contract is what
keeps them one thing:

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
  *replace* them and silently build for the wrong board). It costs ~29.7 KB and is
  a stub returning **-1** — not 0 — otherwise, so a build without it cannot be
  mistaken for a build that passed. **27 clauses**, ten of them `openRead`'s.
- **The path normaliser exists in three copies**, one per implementation, and
  nothing but the contract's "a redundant or trailing separator addresses the same
  thing" clause holds them together. They live in three build worlds (Arduino,
  desktop-only TU, test header), so the duplication is deliberate — but a fourth
  implementation should extract it rather than copy it again.
- **`readAll` is not the EPUB path.** It is for the small JSON files V1 stores and
  `SdFileSystem` caps it at 64 KB, because `-fno-exceptions` makes a `resize` that
  cannot allocate an `abort()` with no diagnostic. `openRead` is the EPUB path
  (3A-3), and the difference is who owns the buffer: a handle is one fixed ~100-byte
  allocation whatever the file's size, so it has no cap and needs none.
- **The handle is RANDOM ACCESS, not just streaming**, and that is the requirement
  rather than a nicety: a zip's central directory is at the **end** of the file, so
  a forward-only stream cannot read an EPUB at all. `openRead(path)` returns
  `std::unique_ptr<FileHandle>` — null on failure, never an `abort()`, and every
  implementation allocates with `new (std::nothrow)` so even OOM is a null.
  `read(dst, n)` / `seek(offset)` / `size()` / `position()`, and **`seek` past the
  end is REFUSED with `position()` unchanged, not clamped** — that is SdFat's own
  `seekSet` semantics, so the device is the primitive rather than an emulation, and
  it keeps "that offset does not exist" distinct from "I am at the end", which is a
  distinction the end-of-central-directory scan needs.
- **The handle's `SpiBusGuard` is per OPERATION, never per handle lifetime.** A
  reader holds a book open for minutes. Holding the guard across that would not
  deadlock today — the mutex is recursive and both users are on the loop task — but
  the day a handle is held off that task, `renderTop()` would block behind it for
  as long as the book is open: a panel that never repaints, which reads as a
  display fault. Per-operation is sufficient because SdFat holds no bus state
  between calls; an open `FsFile` is a cluster number and an offset in RAM.
  `size()` and `position()` are answered from cached members and touch neither the
  bus nor the guard.
- **`DESTRUCTOR_CLOSES_FILE` is 0 in this SdFat build**, so `~FsFile` does *not*
  close and a leaked handle eventually stops the firmware opening anything at all —
  arriving as a failure on an unrelated screen, long after the leak. So the handle
  owns exactly one `FsFile`, closes it in its own destructor, and is reached only
  through a `unique_ptr`; `FileHandle` deletes copy and move so there can never be
  two owners.

**The settings file is CREATED at boot when the card has none** — defaults
written to `/.reader/settings.json`, logged. Three things want that: the user gets
a hand-editable file rather than an invisible one, 2C-3's Settings screen updates
a file instead of creating one, and the card-presence probe gets a guaranteed
target to read (see below). Only when **absent** — a corrupt or wrong-version file
is left exactly as the user typed it, because `loadAndApplySettings` already
logged `DEFAULTED` and overwriting it would destroy the only copy of their edit.

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
`Preferences` namespace `encre_sess`, keys `ver` / `stack` / `slept` (NVS caps a
key at 15 chars). **The payload is ONE key**, and that is what makes the version
key a real commit record: with `scr` and `focus` as two keys, a cut between them
left a valid-looking mixed record — a review found that the "written last" claim
held only for a namespace's FIRST write, since an update's previous version key is
already valid. One payload plus a version written after it has no such gap.

The wire format is `core/include/reader/session_record.h` —
`home:-1;library:7;item-actions:1`, root first — and it lives in `core/` because
`shell/` has no test harness and this is the only pure logic on the resume path.
Record version is **4**.

**The stored focus is real**, and this paragraph twice said otherwise: it claimed
"always 0" after 2C-2 made that false, and the roadmap said the same. An
inherited-work note is a claim with an expiry date.

**`Focus` (`core/include/reader/focus.h`) IS WHERE MOVING A SELECTION LIVES**, and
until it existed there were five copies of it: `HomeScreen`, `StubScreen` (which
2C-3 has since deleted — it was Settings until the real screen landed), both
overlay panels and `ScrollWindow` each carried the same eight lines — add a
delta, clamp to a range, report whether anything moved — with the range spelled
slightly differently in each. That is why "clamp, do not wrap" had to be written
into four separate comments to stay one rule. A screen now declares its **range**
(`Focus::WithNone` when -1 is a position below the first item, as Home's CONTINUE
block is) and mirrors the focus into its view-model; it holds no clamp at
all. `ScrollWindow` owns a `Focus` plus the window around it, so the two concerns
are separable.

**The mirror around Focus is now `FocusScreen`'s**
(`core/include/reader/focus_screen.h`): the base class owns a `ScrollWindow` (a
window with `visibleRows == count` never scrolls and behaves as a bare `Focus`),
and a screen supplies `syncVm()` — mirror the focus and, for a windowed list, the
visible slice into the view-model — plus `focusable(int)` where some rows refuse a
landing. The triad every screen used to hand-write (`syncFocus`/`setFocus`/
`moveFocus`, three byte-identical copies plus two `ScrollWindow` variants) is one
mechanism now. **Landing rules live in `Focus` too**: `Focus::Gate` is consulted
per landing by the gated `move`/`set` overloads, which is where Settings'
skip-past-headers stepping went — the hand-rolled walk had silently stopped
wrapping while every other list rolled over. The gated walk is pinned to the
ungated arithmetic by an equivalence property in `test_focus.cpp`, so it cannot
drift into a second copy of wrap/clamp/none.

- **`set()` CLAMPS and `move()` WRAPS**, deliberately: `set` is the restore path,
  where a record naming row 400 of a three-row list means "as far down as you can
  go", and wrapping that to row 1 would land the user somewhere unrelated to where
  they were.
- **EVERY LIST WRAPS, and that reversed a decision this project had written down
  four times** — "clamp, do not wrap: a list that jumps silently from the last
  item to the first is indistinguishable from a stuck button". Half that argument
  still stands and it is worth knowing which half. A wrap is now the only thing a
  press at the end can do, so it can never read as a *dead* button — the screen
  always changes. What it costs is the opposite reading, a Down that appears to
  jump a long way, which is unambiguous on a four-row overlay and is the case to
  watch on a several-hundred-book Library. `setWrapping(false)` is the opt-out, on
  `Focus` and passed through by `ScrollWindow`; nothing uses it.
- **AUTO-REPEAT WAS WHERE WRAPPING WAS SHARPEST, AND IT IS NOW SOLVED.**
  `Focus::move(delta, held)` clamps when the flag is set, so a wrap belongs to a
  press and a hold rests at the end. It needed the gesture layer above to exist
  first: before that, nothing on the path knew whether a movement had come from
  holding, which is why the defect belonged to neither half that created it.

**THE RULE IS: A SCREEN THAT REPORTS A FOCUS ACCEPTS ONE BACK.** It was
implemented one screen at a time instead, and each screen that had not been done
yet failed the same silent way — `Screen::focus()` overridden, `setFocus()` left
on the base class's no-op, so the wake stored a real number, handed it back, and
the screen dropped it. No log line, no failing test, just the user waking on the
first row. Library got it in 2C-2, Home after "why does the Library come back
where I left it and Home does not", Settings after the same question again, and
each of the three headers carried a paragraph arguing that *its* screen was the
exception (Home cannot be pushed; the Stub is about to be deleted; no wake can
reach an overlay). Every one of those premises was true and every conclusion was
wrong: the reachability of a screen is a fact about the shell's restore ladder,
and encoding it in a `core/` header is how a change over there leaves a screen
silently one-way. `test/unit/test_focus_restore.cpp` walks **every** `ScreenId`
and asserts the round trip, `static_assert`s its own catalogue against the enum
so an added screen cannot slip past, and **counts** the screens whose focus can
move (five) so it cannot quietly end up testing nothing. **The rule is structural
now**: `focus()` and `setFocus()` are `final` on `FocusScreen`, so a derived
screen cannot take one half without the other — the test checks a property the
type system also enforces, and a sixth focused screen gets the whole contract by
choosing its base class.

**THE RECORD IS THE WHOLE STACK, AND `App` PUTS IT BACK** — `snapshot()` /
`restore()`, root first. It held one screen id through version 3, so
Home > Library > actions came back as **Home**: the restore pushed the overlay
onto a fresh app, the factory refused it (correctly — an overlay reads the focused
row of the Library under it, and there was none), and the user lost both. Three
things about the fix are worth keeping:

- **Order is load-bearing.** Each entry's focus is set BEFORE the next push,
  because an overlay reads its parent's focused row *at construction*. That is
  also what makes an overlay restorable at all.
- **It deleted the special cases.** The shell's restore was a ladder naming Home
  ("already the root, nothing to push") and SD-missing ("the card mounted, so the
  message is no longer true"), and every screen not in the ladder was handled by
  accident — three of them wrongly. Both branches are now one question,
  *does the record's root match this app's root*, and `App::restore` names no
  screen at all. **A restore that stops early keeps what already stands**: a
  record from a newer firmware should not cost the user the Library they were in.
- **The wire format is in `core/`** (`reader/session_record.h`), as
  `home:-1;library:7;item-actions:1`, because `shell/` has no test harness and
  that is the only part of the resume path that is pure logic. One payload key
  also makes the version key a real commit record — with `scr` and `focus` as two
  keys, a cut between them left a valid-looking mixed record.

**`focus` is SIGNED (`int16`), and that is what let Home restore its focus too.**
Restoring onto Home is not a push — Home is already the root — so the ladder
skipped it entirely and every wake from Home landed on CONTINUE whatever row the
user had left selected. Two things had to change together, and the second is the
one worth remembering: the ladder's Home branch sets the focus on the **root**
instead of on a screen it just pushed, and the record had to be able to hold
**-1**. On Library, -1 ("nothing selected", an empty `/books`) survived being
flattened to 0 because 0 clamps straight back to -1 there; on Home, -1 is the
CONTINUE block and 0 is the first menu row, so flattening woke the user somewhere
they were never sitting. **A field that cannot hold the value is not a place to
store it**, and a round trip that is stable on one screen for an accidental reason
is not a round trip. The record went version 2 → 3 with the type, so the first
wake after this firmware lands reads as "no session" and starts at Home.

**Two limitations to know before trusting the card:**

- **A card pulled after a successful mount cannot be re-mounted without a
  reboot.** `SDCardManager::begin()` opens with `if (initialized) return true;` and
  the SPI path exposes no `end()`/`unmount()`, so it reports success without
  touching the hardware. So the shell never accepts `mount()` alone: it requires
  `probe()` to agree. A RETRY that reports success and
  then fails is worse than one that stays put — so **RETRY has two branches**
  (`handleRetry`), told apart by `gSdBeganOnce`: never mounted this boot means the
  in-place attempt is real and is kept, while mounted-then-lost **restarts the
  device** (`esp_restart`), because boot is the only code path that re-runs the
  mount. The restart is forced by the SDK, not a workaround for our own bug.
- **`mounted()` is "the card was there and nothing has since told us otherwise".**
  There is no card-detect GPIO in the board profiles and `SdCard::status()` (the
  one cheap CMD13) is private to `SDCardManager`, so liveness is maintained from
  operation feedback plus the two probes below. Neither is a card-detect: a pull is
  noticed by a read *failing*, not by the slot reporting empty.
- **A pull is detected proactively**, because operation feedback alone never fires:
  V1 does almost no filesystem work after boot, so a card pulled on Home stayed
  invisible. `pollCardPresence()` runs from `loop()` **after** the paint block and
  gated on `!gApp->dirty()`, under the same `SpiBusGuard` — SPI traffic on the
  panel's bus, and battery on a device built to sit idle. A usable → unusable edge
  rebuilds the `App` rooted at `SdMissingScreen` (a fresh `App` is dirty and in
  transition, so it paints as a screen change) and leaves the session record alone.
- **What the probe READS is the load-bearing part, and getting it wrong shipped
  once.** The first version of the poll called `probe()`, which opened `"/"`. The
  root directory's sector is the one sector guaranteed to be in SdFat's cache after
  boot, so the poll was answered out of RAM and kept succeeding with the card in
  the user's hand: no log line, and the SD-missing screen was never reached.
  Hardware confirmed it. Two layers replace it:
  - **The fast probe, 2 s.** `probe()` reads a **byte out of `/.reader/settings.json`**,
    which walks the root directory, then `/.reader`, then a data sector. SdFat here
    has exactly **one 512-byte cache slot** — `FsCache` holds a single
    `m_buffer[512]`, and `USE_SEPARATE_FAT_CACHE` is gated on `__arm__` so it is
    **off** on the RISC-V C3 — so three sectors cannot all be served from RAM, and
    the slot ends up holding the *last* of the three, so the next probe misses on
    its first access. `useFileProbeTarget()` adopts the path only if it opens and
    yields a byte *now*; otherwise the target stays the root-directory read and the
    boot log says **DEGRADED**, because a probe that quietly falls back is the same
    defect again.
  - **The backstop, 25 s.** The above is an *argument* about cache geometry, and an
    argument is what was wrong last time. `deepProbe()` calls
    `SDCardManager::sdUsedBytes()` → `freeClusterCount()`, a whole-FAT scan of
    thousands of sectors that no 512-byte cache can serve. **25 s is a floor, not a
    taste**: the SDK caches that value for 20 s, so anything sooner returns the
    cached number without touching the card. It bounds worst-case detection at
    ~25 s and costs a real FAT scan (hundreds of ms to over a second on a big
    card), so it is rare by design and `armCardProbes` logs the measured scan time.
    `sdUsedBytes()` reports its own failure as **0**, so it is armed only if the
    baseline scan returns non-zero — otherwise it says it is not armed.
  - At most **one** of the two runs per call, and **the backstop wins when both are
    due**: its entire value is not depending on the fast probe being right.
  - The card-lost line **names which mechanism noticed**. If the backstop is doing
    the detecting, the fast probe is being served from cache and this is back.

**The shared SPI bus is handled in exactly two places.** Every public method of
`SdFileSystem` takes a recursive `SpiBusGuard` — and so does each *operation* on a
`FileHandle` it handed out, open and close included, which is the same rule and
not a third place; `renderTop()` in
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
| Meta400 / Meta500 | 10 | 21 | Body400 / Body500 / Body700 | 14 | 29 |
| Label400 / Label500 | 11 | 23 | Title700 | 20 | 42 |
| Value500 / Value700 | 12 | 25 | Display700 | 32 | 67 |

Below ~10pt is not legible on this glass, measured. **A role names size AND
weight**; `FontSet::load` refuses to bind a role to an asset built at a
different ppem or weight, so a mis-binding is a boot failure rather than a
silently wrong screen. `core/` never picks its own fonts — the caller supplies a
`FontSet`, which is how device knowledge stays out of the portable layer.

**Kerning came late, and the reason it was missing is worth not rediscovering.**
Both bundled faces keep their kerning in **GPOS**, and neither has a legacy
`kern` table. FreeType's `FT_Get_Kerning` reads only the legacy table, so
`fontc.py` found nothing and all twelve `.rfnt` assets shipped with **zero kern
pairs** through Phases 1 and 2. `stb_truetype` does read GPOS pair positioning,
but only LookupType 2 with `ValueFormat1 == 4`, and Literata's kern feature is
LookupType 9 (Extension) with `ValueFormat1 == 68` before instancing — so the
body face had none either. `tools/gposkern.py` now reads the pairs properly for
both generators (extension lookups resolved, PairPos formats 1 **and** 2, first
applicable subtable wins); `fontc.py` writes them as `.rfnt` kern records and
`ttfprep.py` writes them as a synthesised format-0 `kern` table, which is the
one form stb reads. Its numbers were checked pair for pair against Chrome's own
GPOS shaping and agree exactly.

Three consequences to know:

- **It costs flash.** 12 bytes a record across the eleven embedded chrome faces
  is ~225 KB, plus 36 KB for the body TTF's 6 bytes a pair: firmware flash went
  898,768 → 1,160,752. A 6-byte `.rfnt` record (the keys fit `uint16`) would
  halve the chrome half if that ever matters.
- **Firmware kerning is quantised to whole pixels** and the boards' is subpixel,
  so a kerned chrome run can land a pixel either side of the board's. A pair
  under half a pixel at its ppem is dropped rather than stored as zero, which at
  21px is about two thirds of the face's pairs.
- **It moved the firmware measurably toward the boards**: design-vs-firmware
  mismatched pixels fell 27.6% across the six implemented screens at both
  geometries, every panel improving. Most of that is prose — chrome's own
  uppercase, tracked labels barely kern at all in Space Grotesk.

## Memory

**`getFreeHeap()` cannot see the largest allocation this firmware makes.**
Rasterising ONE glyph transiently costs ~74 KB, ~56 KB of it a single malloc, and
it is freed before the next line prints — so every `free heap` figure in the boot
log is measured either side of it and reads 229,900 while the real floor is
155,712. `ESP.getMinFreeHeap()` on the `[alive]` line is the only thing that sees
it, and `mark()` carries the heap so the existing stage trail is a heap TRACE: the
stage where `min` falls is the stage that spent it. That is how this was found,
after two wrong guesses about bring-up.

It is `third_party/stb_truetype.h:2802` — `count = (size < 32 ? 2000 : ...)` — and
the v2 `stbtt__active_edge` is 28 bytes, so stb pre-allocates 2000 slots (56,004
bytes) for the first edge of every glyph, for a text glyph needing ~20.
`stbtt__hheap_alloc` chains another chunk when one runs out, so lowering the count
trades memory for allocation count and nothing else. **Not patched**: the file's
header makes a sha256-backed "vendored, unmodified" claim, which is worth more
than 54 KB while 54 KB is affordable. Recorded as a lever, in the roadmap, with
the caveat that the reader's cache, zip directory and page structures all stack on
top of this spike.

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
- **THE SECOND COPY IS THE EXTRACTION POINT, NOT THE FIFTH** — a rule this
  project retrofitted across two whole passes (`FocusScreen` and the
  shared-primitives sweep) instead of following from the start, and the cost of
  retrofitting is why it is a rule now. Every mechanism extracted late had the
  same biography: written in one screen, copied because it was only six lines,
  and the copies then did what copies do — no test caught any of it, because
  each copy passed its own. The clamp existed five times before `Focus`; the
  focus/setFocus pair shipped one-way on three screens, each behind a comment
  arguing its own case was the exception; Settings' hand-rolled skip walk
  silently stopped wrapping while every other list rolled over; eight `Hint[4]`
  loops drifted into two behaviours for an empty slot; the ramp's role↔asset
  binding was three lists in three build worlds. Four signatures, each acted on
  the moment it appears rather than when it hurts:
  - a rule restated in COMMENTS at more than one site is a primitive not yet
    extracted ("clamp, do not wrap" had to be written down four times to stay
    one rule);
  - a contract whose halves can be adopted separately is a mechanism not yet
    made structural (`FocusScreen` made the pair `final`, and the failure mode
    stopped being writable);
  - an ordering or a caller list maintained in PROSE is a function not yet
    written (`replaceApp` replaced a comment enumerating who remembered to call
    `forgetLibrary`);
  - one table spelled in more than one build world is a manifest
    (`font_manifest.h`).
  When building screen N+1, the question is not "what does this screen need"
  but "which parts of screen N were mechanism": extract, migrate BOTH, and give
  the primitive its own test, so a screen's tests are about its content.
  Deliberate duplication stays legal — the path normaliser's three copies are a
  documented decision — but it has to be a decision with its reason written
  down, not a default arrived at six lines at a time.
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
- **The shared-primitives pass swept the remaining drawing repeats into
  `components.h`**, and the next screen should find them rather than reinvent
  them: `outlineRect` (the four-fill border every bordered box shares — NOT for a
  box whose interior must be painted, like Home's progress bar), `buildHints` +
  `kHintSlotMarks` (a view-model's hint arrays as the bar's four slots, with the
  boards' empty-36px-dead-slot rule applied uniformly — eight hand-rolled copies
  had drifted into two behaviours; the theme's `measuringHints` is the different
  job of labelless height-measuring, which deliberately keeps its marks),
  `rowRuleFor` (the positional bottom-rule rule below), `drawCentredText` (the
  measure-and-draw-same-tracking hazard, once), and a `drawHintBar` overload
  without the slot out-param nothing but tests read.
  `ScrollWindow::slice()` is the same pass on the state side: first/count/
  focused-in-slice-or-−1, so a view-model can never name a row that was not
  drawn — Contents and Bookmarks are Phase 3's callers. The type ramp's
  role↔asset binding is `reader/font_manifest.h`, one list expanded by all three
  loaders (shell's embedded arrays, the simulator's files, the tests' ramp.h);
  adding a Phase 3 role is one manifest line plus the generated asset.
- **Three shared primitives landed with the SD-missing screen** (2C-1), and the
  next screen that needs them should find them rather than reinvent them, both in
  `components.h`:
  - `drawActionButton` — the boards' **primary action slab** on a full-screen
    prompt: 68px tall (`kActionH`), no border, one centred Value700 label at
    `letter-spacing: 0.18em`. Five V1 boards draw it and all five state the same
    box; **the width is not shared** (SdMissing pins 260, the overlays take their
    column). It is *not* Home's CONTINUE block, which is 72 tall, left-aligns and
    carries an arrow. **The outlined secondary variant now lives here too** — the
    `filled` flag picks the role, because the boards do: a filled slab's label is
    Value700 and an outlined one's is Label500, same box and same tracking. (This
    said the variant "belongs in this function when DeleteConfirm lands, not
    before"; DeleteConfirm landed and brought it.)
  - `wrapProse` / `drawProse` — the **first paragraph in the firmware**. A
    paragraph's height is a *result* (face × copy × column), not a number the
    board states, so the wrap is a value computed once and then both measured and
    drawn; two calls that each re-wrapped would be two chances to disagree, and
    the disagreement would read as a paragraph drifted off centre. Greedy on
    ASCII spaces, no hyphenation, no CJK breaking — Phase 3's EPUB text is a
    different problem with a different budget.
  - And the reason SdMissing's board says `max-width: 420px` where it used to say
    400: **the wrap follows the firmware's own metrics, not Chrome's.** The
    autohinted `.rfnt` faces have whole-pixel advances and measured ~3% wider, so
    the board's three lines came out as four. 420 is three lines in both engines
    and moves nothing in Chrome. The number was wrong, not the design. **Kerning
    has since closed part of that gap** (see **Type**) — DeleteConfirm's
    paragraph went from five firmware lines to the board's four, at the board's
    own break positions — but not all of it: whole-pixel advances still measure
    wide, so a board's `max-width` is still a number to check in both engines
    rather than to trust from Chrome.

## Overlays and lists

- **The App renders a STACK, not a screen.** `Screen::isOverlay()` marks a panel
  that leaves the screen beneath it visible under a veil; `App::render` walks down
  to the topmost non-overlay, renders that, then renders each overlay above it.
  **The shell must call `App::render`, never `top().render`** — that mistake paints
  an overlay as a panel floating on white, and *nothing on the desktop can catch
  it*: the simulator and all the goldens go through `App::render`, so they pass
  while the device is wrong. It has happened once.
- **Input and fidelity come from the top screen only.** An overlay whose parent
  still received events would move a focus the user cannot see.
- **A focus move inside an overlay repaints the OVERLAY ALONE**, over the frame
  the previous paint left — `App::renderTopOnly`, and `paintPlane` in
  `shell/src/main.cpp` is the one caller. Measured at 528×792 it takes an actions
  overlay repaint from 4.75 ms to 1.58 ms, of which the veil is most (below) and
  skipping the parent's text pass is the rest.
  - **The precondition is about the FRAME, not the stack**, and the frame is the
    one thing `App` cannot see — so `App` records what it painted and where, and
    `canRenderTopOnly` refuses unless the frame, plane, top screen and depth all
    match that record. A caller cannot be trusted with this check, because a
    caller is the only thing that could have clobbered the frame. **The clear
    belongs inside the full-paint branch**: clearing and then partially
    repainting is an overlay panel floating on paper.
  - **A push or a pop is never partial** (`transition()` is the signal) and
    neither is the first frame after boot, on two independent conditions. The
    push/pop rule is conservative on purpose: the pointer comparisons it would
    otherwise rest on are an ABA, since a popped screen's address can be reused
    by the next push.
  - **Grayscale never is.** That path renders three planes plus a rebase, and the
    frame between passes holds a different plane, so the precondition is false for
    every pass but the first. `Dithered` and `Mono` both qualify.
  - **`Screen::paintFootprint()` is the screen's own promise** that equal tokens
    mean the same pixels covered, so the new paint replaces the old one. Zero is
    "no promise" and is the default. `DeleteConfirm`'s is constant;
    **`ItemActions`' is not, and the reason is one pixel**: its panel's height is
    the sum of its rows, and the focused row loses its rule, so focusing the LAST
    row (whose rule is already gone) makes the panel a pixel taller and, being
    centred, a pixel higher. Moving the focus off it shrinks the panel and leaves
    the old top border standing — 226 pixels at y=212 on the X3, and the veil only
    takes 5 of every 9 of them out. So two of its four focus moves take the fast
    path and two do not. `test_partial_repaint.cpp` renders **every ordered pair**
    of both overlays' focus states through both paths and compares bytes.
- **There are THREE dither patterns for three jobs**, each from its own board
  declaration, and `dither.cpp` explains why they cannot be shared:
  `kClustered` black on a 4px grid for tints (`.dither-dots`, and an `Ink` for
  `.dither-dots-inv`), `kBayer` dispersed for glyph and icon edges, and
  `veilRect`'s clustered **white** on a **3px** grid for the overlay veil. A 4px
  veil is half as dense and reads as a smudge.
- **The veil was the most expensive thing on the screen, and it is now byte-wise.**
  A veil covers the WHOLE frame, and the per-pixel form cost four integer
  divisions and a bit-addressed read-modify-write per pixel: 2.33 ms at 528×792
  against `ditherRect`'s 1.12 ms and a full-frame `clear`'s 0.001 ms, so ~150 ms
  of every overlay repaint at this project's ~65× desktop-to-device ratio. It now
  ORs eight columns at a time into the physical store — 0.17 ms, 13.6× — which
  makes it **the one drawing routine in `core/` that knows `Rotation` exists**:
  under CCW a logical row is a physical *column*, so it walks logical **columns**
  instead, and the tile is symmetric under transposition, which is what lets the
  two cases just swap axes. A byte-wise path that assumed a logical row is a
  physical row would pass every desktop test and every golden and smear the veil
  diagonally on glass. `test_dither.cpp` keeps the per-pixel form as its reference
  and asserts byte-identity at both geometries, under both rotations, and for runs
  that start and end mid-byte — the panel widths are multiples of 8, so nothing on
  the device exercises the edge masks.
- **`ScrollWindow` owns list movement** — a `Focus` (see Storage) plus
  first-visible, scrolling by a row rather than a page, and it CLAMPS, which is
  what lets a held button's 40-row step land on the last row instead of past it. `Theme::libraryVisibleRows` derives how many rows fit
  from the panel and the type; the shell must set it before the first Library
  paint or the list correctly renders empty.
- **A scrollable list shows its position as a RAIL** in a 14px gutter
  (`kListGutterW`), not as a number in the header band: the band's right slot
  already means "books, counting one level down" and a position means "rows", so
  putting both there produced `1–7 OF 12` — two units in one expression. A rail
  says *where* without claiming a count. `drawScrollRail`, outlined track with a
  solid proportional thumb.
  - **The gutter exists only when the rail does.** Reserving it on every list was
    tried, to spare a library crossing the visible-row count one reflow of its
    right-aligned values — and it left a white strip beside the FULL-BLEED focused
    row on every list that fits, which reads as a rendering fault. A defect you
    see every time beats a reflow you see once. One condition drives both, and
    `drawScrollRail` refuses the same case independently so they cannot disagree.
  - **A rail cannot live in the outer margin**, which was the first attempt: the
    focused row is full-bleed inverted, so a black thumb crossing it is black on
    black and vanishes, and each row's 1px rule runs straight through the track.
    It needs a column the rows do not enter — that is the real cost of a
    scrollbar here, 14px off every row.
  - **A vertical rail is the BEST case on this glass, not the worst.** It is
    axis-aligned and coverage 0-or-3, so track and thumb are identical in every
    plane and pass. The thin-stroke warning this project records is about
    DIAGONALS (`kChevron`); it was wrongly cited against a rail once.
  - **This governs every scrollable list**, and today that is Library alone.
    Settings scrolled for about an hour: adding its `Refresh on screen change` row
    pushed it past the panel, and then Wi-Fi was cut from V1 and CONNECTIONS went
    with it — eleven items where twelve fit. Phase 3's typography settings will
    push it over again and it will start scrolling **without any code change**,
    because `renderSettings` reads `totalRows > rows` rather than assuming. Contents
    and Bookmarks are Phase 3's and will want it too.
  - **It is compared against its board now**, and for a while it was not: Library's
    golden shows seven rows of seven, so it does not overflow and no rail draws in
    it, and Settings stopped scrolling when Wi-Fi was cut. So the rail shipped with
    unit tests and nothing that looked at a pixel. The `library_scrolled` state
    needs a LONG list — a rail's proportions come from the list's length — so the
    factory takes demo items and the state uses 24 books.
    - **Reaching the board's window takes one press past it and one back.** Twelve
      Downs is the obvious route and gives the wrong window: `ScrollWindow` scrolls
      only as far as it must, so arriving from above lands the focus on the
      window's BOTTOM edge. Both states are real; the board's has list on both
      sides of the thumb, which is the better illustration.
- **The session record stores a screen NAME, not an enum ordinal.** 2C-2 inserted
  three screens into the middle of `ScreenId` and a stored ordinal silently became
  a different screen. Names also mean `nvs_get encre_sess scr str` is readable on
  a device. They are deliberately not `screenName()`'s strings — that is a log
  label, free to be reworded; this is a storage format.

## The chrome screens

V1's chrome is complete as of 2C-3. What each screen is, and the one thing about it
worth knowing before changing it:

| Screen | Board | The thing |
|---|---|---|
| Home | `Main.dc.html` | Focus starts on the CONTINUE block (`-1`), not the menu. |
| Home / empty | `HomeEmpty.dc.html` | A **variant**, not a screen: same `ScreenId`, same view model, same menu. |
| Library | `Library.dc.html` | The only list that scrolls today, and the only screen with a rail. |
| Library / scrolled | `LibraryScrolled.dc.html` | Reached by pressing PAST the focused row and back — arriving from above windows it differently. |
| Item actions, Delete confirm | their own boards | Overlays; a focus move repaints the overlay alone. |
| Book details | `BookDetails.dc.html` | Not an overlay, despite covering the Library. Its title **wraps**; everywhere else elides. |
| Settings | `Settings.dc.html` | Draws nine rows and only three respond. |
| Sleep | `Sleep.dc.html` | **NOT REACHED ON THE DEVICE** — implemented, never painted. |
| Reader | `Reader.dc.html` | The only screen whose content is the BOOK's. `Fidelity::Grayscale`, the only one. |
| SD missing | `SdMissing.dc.html` | RETRY restarts the device when the card was lost after a mount. |

**SETTINGS DRAWS EVERY BOARD ROW AND ONLY THE DEVICE ONES RESPOND.** TYPOGRAPHY
belongs to Phase 3's reader; its five rows carry the board's own placeholder values
so the screen matches the board before the settings behind them exist. The `Size`
row is the one that will cost something to wire: see the glyph-cache table below,
because at 41px the shipped cache budget thrashes.

- **Focus SKIPS them.** A row that cannot be reached cannot mislead, where a row
  that focuses and then ignores CHANGE is the silent no-op this project has been
  bitten by twice. The cost is real and worth watching on glass: focus starts six
  rows down with five unreachable rows above it, and UP there does nothing.
- **An inert row is drawn EXACTLY as an unfocused focusable one.** No dimming —
  `SettingsRow::focusable` is about input, and a visual difference nobody designed
  is worse than none. `renderSettings` deliberately never reads that flag.
- **The theme reports Settings' BOX MODEL, not its row count.** Library's items are
  one height so a theme can answer "how many fit"; Settings interleaves 54px rows
  with taller section headers, so the answer depends on which items are headers —
  and the item table belongs to the screen. `settingsMetrics` hands over three
  heights and the screen counts, so neither side holds a copy of the other's data.
- **`SettingsSink::commit` applies AND persists**, in that order. The user has
  pressed a button and expects the device to behave differently; a card gone
  read-only must not also cost them the change until the next boot. **A refused
  write still shows the new value** — the change has taken effect in RAM, and
  reverting the display would make a read-only card look like a screen that ignores
  its buttons. The shell logs the failure; `core/` never learns why.
- **The factory holds a COPY of the settings**, because it is what constructs the
  screen. It has to be told when the struct changes, or closing Settings and
  reopening it shows the values from before the change — struct right, policy
  right, screen wrong.

**THREE RULES THE SETTINGS BOARD DRAWS AND A NAIVE RENDERER DOES NOT**, all three
found by diffing pixels rather than by looking:

1. The **first** section header has no rule — the header band's own 2px border is
   already there, and a second doubles it into a 4px slab. Positional, not by
   identity: at the top of the window the band is the separation, whichever section
   is scrolled there.
2. The **last row of a section** has none either; the next section's `border-top`
   is the line between them.
3. The **last drawn row** has none, which is `renderLibrary`'s rule verbatim.

Rule 2 was the expensive one: it also advanced `y`, so every row below the DEVICE
header sat a pixel low. **The symptom reported was a line at the top, and the line
at the top was the smaller of the two defects.**

**A FOCUSED MENU ROW KEEPS ITS `border-top`.** Invisible against the fill, and the
point: an unfocused row is 80px plus a 1px rule, so dropping the rule makes the
focused one 80px — and the menu's total height then depends on whether a row is
focused, stepping the whole block a pixel the moment focus enters it. Black on
black costs nothing and holds the pitch at `kRowH`.

**THE SLEEP SCREEN IS IMPLEMENTED AND THE DEVICE NEVER PAINTS IT.** Deliberate,
and deferred to Phase 3 rather than left as an oversight. The shell's sleep path
logs, calls `display.deepSleep()`, powers down the rails and sleeps — it never
renders `SleepScreen`, so the panel keeps whatever screen the user was on. It is a
board, a screen, a theme method, a simulator subcommand and a set of tests, and it
has never been on glass.

Why it waits: **the board's content does not exist yet.** It shows
`NOW READING / MIDDLEMARCH / GEORGE ELIOT / 6% · CH. 01`, and there is no Reader,
so the device would paint either demo fiction or a card with the reading block
empty. The same reasoning that keeps typography out of Settings.

And a trap for whoever wires it: **it must be painted WITHOUT being pushed.** The
session record names the top screen, so pushing `SleepScreen` would make the wake
restore *into* the sleep screen. It wants a direct render after the record is
saved, not a navigation. It also costs a full refresh (~825 ms) on every sleep,
which is not free either.

**IT TAKES NO INPUT AND DRAWS NO HINT BAR**, and neither is an
omission: the shell paints it and then calls deep sleep, so there is nobody left to
press anything, and the bar is a contract about four buttons that do nothing.
`onEvent` answers `none()` even for Back. It exists because e-ink holds its last
image with no power — leaving the previous screen there shows a Library or a
half-read page and gives no clue the device is asleep rather than frozen.

**TWO BOARDS ASKED FOR TYPE THAT IS NOT IN THE RAMP** — Sleep's title at 53px and
HomeEmpty's at 39px — and both now use `--t-title` (42px). A role is a pre-rendered
asset per size AND weight, 15–20 KB of flash each, and these were the only boards
that wanted those sizes. Sleep's soft hyphen went with it: MIDDLEMARCH at 53px had
to break, and at 42px it fits. **If either reads too quiet on glass the fix is a new
role plus a wrap that honours a soft hyphen** — real work, and not worth it until
the panel says so.

**`kBookLarge` is a second asset for the same drawing**, at 112px against `kBook`'s
25px, because these are pre-rendered bitmaps and there is no scaling one up. 3,136
bytes. `iconc.py` disambiguates them by `source` board, since both boards carry the
identical path data.

**THE INPUT MONITOR IS GONE**, deleted with `StubScreen` in 2C-3. It was reachable
only from the stub's first row and no board ever listed it. **What was given up: press
classification is now verified only by `test_input.cpp` on the desktop**, and
`shell/` is where four bugs have hidden. If held-scroll or press classification
needs eyes on glass again, it comes back as a board row, not a hidden gesture.

## The reader

Six layers, each one ignorant of the next. The boundary is the point: every one of
them refuses bad input with a reason rather than aborting, because all of it is
bytes off somebody's card.

| Layer | Holds | Does NOT know about |
|---|---|---|
| `inflate_stream.h` | raw DEFLATE in bounded chunks, a 32 KB window | zip, files |
| `zip.h` | the central directory, entry reads, `EntrySource` | XML, EPUB |
| `xml.h` | a pull tokenizer over a `ByteSource`, entities | nesting, EPUB, documents |
| `epub.h` | container, OPF, spine order | XHTML content |
| `document.h` | blocks, one at a time (`BlockReader`) | pixels, fonts, columns |
| `layout.h` | pages from a block stream (`PageBuilder`), justification | `Framebuffer`, themes |
| `chapter.h` | the whole chain, positioned (`ChapterReader`) | screens, pages |

**EVERY LAYER IS A STREAM, and that is what made a real book openable.** Under 3B
each held its whole input: `Le Fléau`'s longest chapter is 315,852 bytes of XHTML
and 228,849 of blocks, and holding both at once was a **546 KB peak** against ~142 KB
free. Only 62 of its 92 chapters could be opened. Measured after 3C, over the same
book: **69,884 bytes peak for any chapter, largest single allocation 36,956** — the
inflate window and its tables, which is the only sizeable one left. All 92 chapters
open; the whole book is 8,114 pages.

`inflate.h` (the one-shot form over stb) is still there and still used for the small
things — an OPF, a `container.xml`. It is not the reader's path.

`book.h` is the seam that joins them to the filesystem, and it lives in `core/`
**because `shell/` has no test harness** — five bugs have hidden there. "It needs a
filesystem" is not a reason to be untestable: `FileSystem` is an interface and
`fake_fs.h` serves real EPUB bytes through it. **It hands back a LOCATION, not a
chapter** — a path and three numbers — so the archive, its central directory and the
OPF's parse are all released before a block is read, and re-reading the chapter for
a backward page turn needs no central directory at all.

### Why the decoder is ours

stb_image's zlib is one-shot: whole input in, whole output out. There is no way to
get chunks from it, and **every** route to bounded memory needs them — including
inflating to a temp file, which needs incremental output to write incrementally. So
the choice was ours-versus-miniz, and ours won on the grounds 3B's parsers did, plus
one: it removes stb's **6,608-byte single stack frame**, which is 41% of the loop
task's stack and had already panicked the device.

Three things about it worth keeping:

- **The 32 KB window is the format, not a choice.** A DEFLATE match reaches 32,768
  bytes back, so a decoder that does not hold its whole output must retain that much
  of it. That sets the floor for the whole design.
- **Everything big is in ONE heap block**, and that was measured rather than assumed.
  With the window on the heap but the input buffer and Huffman tables as members, an
  `Inflater` declared as a local cost **6,336 bytes of stack** — barely better than
  the thing it replaced, for exactly the same reason. One `Scratch` brings it to
  3,072. A `static_assert` ties the documented `kHeapBytes` to `sizeof(Scratch)`.
- **Canonical counts-and-symbols tables** (zlib's `puff.c` form), ~600 bytes each
  against stb's ~2 KB. That is the whole frame difference.

Validated byte-for-byte against the one-shot decoder that shipped: 216 entry-passes
over `Le Fléau` (every deflated entry, at 2048-byte grain **and one byte at a time**)
and 3,600 over the 200 generated EPUBs, zero disagreements, 8.3 MB decompressed.
**Grain 1 is the load-bearing case** — a source that satisfies every read hides every
resumption bug there is.

### The two things a real book taught the reader

Both were found by flashing, and both looked like the same symptom — a blank page
reading `0 / 0`.

**SPINE ENTRY 0 IS THE COVER.** `Cover.html` in a real EPUB is one `<img>` and no
body text, and `document.h` drops images, so it paginates to NOTHING. Three of
`Le Fléau`'s 92 entries do. Skipping to the first entry with text would only have
moved the dead end to the bottom of that chapter, so `ReaderScreen` owns the BOOK:
paging off the end of a chapter opens the next, off the top opens the previous one's
LAST page, and an entry with no pages is skipped in whichever direction the reader
was already going. Locating a chapter costs a reopen and a directory parse, ~76 ms
on device, against a ~520 ms refresh.

The label is the SPINE POSITION, not a chapter number — spine 2 shows `CH. 03`.
Without a table of contents (`Contents.dc.html`, not built) the position is the only
thing honestly known.

**`<p>&nbsp;</p>` IS HOW AN EBOOK MAKES VERTICAL SPACE**, and it is everywhere: the
first text chapter of `Le Fléau` opens with three of them. Trimming only ASCII space
left each as a two-byte block that took a line of the page and drew blank. A block of
nothing but whitespace is dropped, and **U+00A0 counts for that question only** — it
is kept inside text, because there it is deliberate (French sets one before a colon,
and collapsing it would let the line break in the wrong place).

**A REFUSED CHAPTER TURN MUST LEAVE THE SCREEN WHERE IT WAS.** The walk has to open
each candidate before it can know whether that candidate has pages, so running off
either end of the book left `chapterAt_` on the last thing tried with an empty index
— the device reported "spine 0, page 1/7" for an entry with no pages, with a stale
page still on the panel. `openChapterAt` restores the previous chapter on failure, at
the cost of one extra decode, once, at the book's edge.

And a note on how the first of these was missed: the desktop probe checked
`pagesRead != starts.size()` and `0 == 0` satisfied it, so a chapter that paginated
to nothing passed silently. **A check that reports on less than it claims** — the
same shape as the card probe answered from cache, and the `make compare` default that
skipped four screens.

### A grayscale screen is painted twice: fast, then four levels

`renderTop` paints a `Fidelity::Grayscale` screen with ONE waveform and `loop()`
upgrades it to four levels once the buttons have been quiet for `kRefineQuietMs`
(600 ms). The reference firmware does this and it is the right shape for a reader:
the page wants to be there NOW and the grey edges can arrive a moment later.

From this device's own logs: the grayscale sequence is three waveforms and
**~1056 ms**; one waveform is **~520 ms**. So a page turn shows text in half the
time and the refinement costs what the full sequence would have cost anyway.
Flipping through pages costs 520 ms a turn instead of 1056.

Three things that make it work, and one that does not:

- **IT SHOULD NOT COST A SECOND FLASH.** `Uc8279Driver::displayGrayscaleBase` takes
  its visible "clean base" path only when
  `!_oldPlaneValid || _lsbValid || _forceFullSyncNext || _initialFullsRemaining > 0`.
  After an ordinary one-waveform paint the old plane IS valid and no grayscale
  planes have been written, so the base pass is the cheap settle. That is the whole
  reason this beats simply painting twice.
- **The fast pass is `Dithered`, not `Mono`.** The reader declares Grayscale
  precisely because hard-thresholding a serif face at 32px was judged worse, so the
  transient frame keeps what anti-aliasing one waveform can carry. Same cost, closer
  to the final image, smaller visible upgrade. Its known artifact is the em dash
  combing against the 4×4 grid at body size; `paintMono(mode)` is the one-line
  alternative.
- **It refines from `loop()`, never from a dispatch.** A paint cannot be
  interrupted, so refining between two page turns would put its full cost in front
  of the second one.
- **The worst case is a press landing during a refinement**: that turn pays the
  refinement plus a fresh fast paint, about what the full sequence costs today.
  Never worse, usually half.

`[paint] … refine-owed` and a separate `[refine] done total=…` keep the two costs
distinguishable in the log — a page turn is the fast paint, and the refinement is
what the page settles into.

### Nothing may leave the column

Body text wraps with **`WordBreak::Anywhere`**, and it is the one place that is right
— for a reason the boards never had: a board's copy is text the design chose, so
`Normal`'s "a segment wider than the column sits on its own line and overhangs" is
fine there and is not fine for a book.

Measured over `Le Fléau`: **8 lines of 96,823 ran past the column**, the worst by
683px on a 492px column — off the panel entirely. Two fixes, in order of how much
they were worth:

- **A break after a hyphen** (UAX #14 allows one; Chrome does it), which took 8 to 2
  and moved no golden. Six of the eight were chanted hyphen chains like
  `Jeff-Marty-Helen-Harriett-…`. The hyphen stays at the end of the line, which is
  what makes the break read as typography rather than damage.
- **`Anywhere` as the last resort**, for the two survivors. Both separate their words
  with **U+00A0** — non-breaking by definition, so a browser would overflow rather
  than break, which a panel cannot do. `Anywhere` engages only when a segment cannot
  fit a line at all, which is CSS's `overflow-wrap: break-word`.

**0 of 96,658 lines overhang now.**

### A factory that substitutes content is worse than one that refuses

`ScreenId::Reader` needs a book, and the shell only sets one from a button press — so
a **session restore has nothing set**. The factory used to fall through to the demo
chapter there, and the device woke from sleep showing Middlemarch: fiction from a book
the user was not reading.

The demo now has to be asked for (`setReaderDemo()`, which the simulator and the
goldens call) and a Reader with neither a book nor a demo is **refused**. A refused
push leaves the Library standing — wrong in a way the user can see through, rather
than wrong in a way they cannot.

### Paging: forward is free, backward re-decodes

A DEFLATE stream cannot be seeked and checkpointing one costs 32 KB a checkpoint. So:

- **Opening a chapter costs one decode**, which builds the page index: one start
  Cursor per page, ~8 bytes each, 3,072 bytes for the longest chapter in the book.
  That index is what lets the footer say `3 / 12` at all, and what a backward turn
  decodes *to*.
- **A forward turn continues the live stream** — the reading position keeps its
  `ChapterReader` and its `PageBuilder`. Measured 1.16 ms on the desktop for the
  worst page in the book.
- **A backward turn rewinds and decodes forward** to the recorded cursor. 33.9 ms
  desktop for the worst case, against a ~520 ms panel refresh. Buffers are reused, so
  it allocates nothing — churning 32 KB per turn is how a heap with 142 KB free
  becomes one that cannot serve the next chapter.

The strongest test of all this is `READING BACKWARD GIVES EXACTLY THE PAGES READING
FORWARD GAVE`: it exercises the rewind, the buffer reuse, `startAt`'s discard path
and the index together, and any of them off by a line shows up as a page that differs
from its forward self.

### The lifetime rules that changed, and how they broke things

Both of these are worth knowing because neither failure looks like a lifetime bug.

**`Xml::name()` is a view into a reused buffer now**, where it used to view the
caller's whole document and outlive the parse. `document.cpp`'s tag stack held those
views, and the result was that `<blockquote><p>x</p></blockquote>` came out a plain
paragraph — the stack's `"blockquote"` had become `"p"` — and `<a><b></a></b>` was
**accepted**, because the mismatch check compared two views into the same buffer and
those are always equal. A dangling view here does not crash; it silently agrees with
itself. The stack holds 24-byte truncated copies plus the full length.

**`LaidLine::text` is OWNED**, not a view. A view meant whichever blocks a page
spanned had to outlive the Page, which is a rule the reader would have to enforce
across a page turn while blocks are being dropped behind it. A page is ~12 lines of
~45 bytes, so copying costs ~1 KB against a ~520 ms refresh — and it means a block is
released the moment its last line is laid, so **nothing needs a block window**. Two
tests had recovered a block boundary by comparing `text.data()` pointers; `LaidLine`
carries `block` and `lastOfBlock` now, which the page index needs anyway.

**`xml.h` DOES NOT VALIDATE NESTING, ON PURPOSE.** `<p>unclosed` tokenizes without
complaint. The document builder keeps a stack to know which block it is in, so it
notices an unclosed tag at `Eof` for free; a second stack in the parser would be a
second depth cap and a second allocation for a check the layer above cannot skip.

**EVERYTHING THE XHTML SAYS THAT `document.h` DOES NOT MODEL IS DROPPED, NOT
APPROXIMATED.** A `<table>` becomes its cells in reading order or nothing, never a
guess at a layout. `<style>` and `<script>` text never reaches a page. **Inline
emphasis is not modelled** — `<em>` contributes its text and no marker — because
there is no italic face to render it with, and a field layout must ignore is worse
than an honest gap.

**LAYOUT ASKS `advance()`, NEVER `glyph()`.** That is design decision 3 of 3A and
`layout.h` is the layer it was made for: a scalable face rasterises inside `glyph()`
at ~3,794 µs a glyph, and a page holds ~600 of them, so a measuring pass that
rasterised would cost seconds to decide where to break a line it has not drawn.
Asserted on the cache's own counter, not on discipline — **0 rasterisations across
6,800 pages** of real EPUBs.

**LINE BREAKING IS NOT IN `layout.h`.** It is `wrapProseLead` in `components.h`,
which already breaks greedily on ASCII spaces against these exact metrics; body text
got `firstIndentF26` added to it rather than a second wrap that would inherit none of
its fixes. Same for drawing: `drawText` and `drawTextJustified` are ONE pen loop
differing by one argument.

**JUSTIFICATION REFUSES ON HOW FULL THE LINE IS, NOT ON HOW FAR A GAP STRETCHES.**
A per-gap cap cannot tell a corridor from prose, because the gap count is what turns
slack into stretch: on `Reader.dc.html`'s own copy a three-space-width cap refused
"necklace, and the two of" — 367px of text in a 444px column — because its 77px of
slack fell across four gaps, and set it ragged directly under a line it had
justified. `kMinJustifyFillPercent` asks the question that is actually visible.
Ragged lines went 18% → 3.4% and what remains is almost all paragraph-final.

**A PAGE BOUNDARY MAY LAND INSIDE A PARAGRAPH, AND MUST.** A 12-line page and a
7-line paragraph means most pages end mid-paragraph. `layoutPage` walks FORWARD only;
`ReaderScreen` paginates the chapter once into a list of page-start cursors, which is
what makes the page counter and the previous page possible at all.

**PARAGRAPHS ARE SEPARATED BY AN INDENT, NOT A GAP** (`Reader.dc.html`), and a
paragraph is indented only if the one before it was also a paragraph — a heading or a
quote is itself the break the indent would announce. A gap would cost a line box of a
12-line page.

### The glyph cache

Sized against the UNION across pages, not against a page. A page is small — the worst
of 5,000 real pages used 36 distinct glyphs and 3,272 bytes — but the arena is a RING,
so a page that introduces a capital the last one did not advances the write pointer,
and on wrap it overwrites whatever is oldest, `e` included. Printable ASCII plus the
32 accents and marks every `fontc.py` subset carries, on the shipped face:

| ppem | 29 | 32 | 36 | 41 | 48 |
|---|---|---|---|---|---|
| bytes | 10,378 | **12,292** | 15,359 | 19,284 | 25,854 |

Bytes go as ppem², so the old 8 KB held the set at **no** reading size. 16 KB holds
ppem 32 with 25% spare. **A body-size setting must revisit this** — the budget is a
constructor argument precisely so the caller can size it from the chosen ppem.

### The stack, which is the budget nothing was watching

**stb_image's inflate wants 6,608 bytes in ONE FRAME.** The compiler inlines
`stbi__parse_zlib`, `stbi__compute_huffman_codes` and `stbi__zbuild_huffman` into
`stbi_zlib_decode_noheader_buffer`, so all three `stbi__zhuffman` tables — `fast[512]`
plus `size[288]` plus `value[288]` each — share one frame. Arduino's default
`loopTask` stack is 8,184 usable bytes and the chain above the call spends ~1.5 KB of
it, so **opening any book was a stack-protection fault, every time**, and the reboot
landed back on Home looking like a navigation bug.

`shell/src/main.cpp` therefore carries `SET_LOOP_TASK_STACK_SIZE(16 * 1024)`.

Three things worth keeping:

- **`-DCONFIG_ARDUINO_LOOP_STACK_SIZE` DOES NOTHING.** arduino-esp32 ships
  precompiled, so a `-D` in `build_flags` never reaches its `main.cpp`. The
  weak-symbol override (`SET_LOOP_TASK_STACK_SIZE`, declared in `Arduino.h`) is the
  supported mechanism and the only one that takes effect.
- **Read the frame size off the panic.** `add sp,sp,t0` at the faulting address with
  `T0 = 0xffffe630` is a −6,608-byte allocation; `addr2line` on `MEPC` names the
  function. That is faster and more certain than reasoning about `sizeof`.
- **A stack budget cannot be moved into `core/` to be faked.** The answer to
  "`shell/` has no test harness" has been to move logic where a fake can reach it;
  a stack is not movable, so it is MEASURED instead. `test_inflate.cpp` runs the
  inflate on a pthread with a stack it owns, fills it with a pattern and counts what
  survives — FreeRTOS's own high-water technique. It reports **7,348 bytes** and
  asserts a 10 KB ceiling, so a vendored-library bump that grows the appetite fails
  on the desktop rather than panicking the device.

The `[stack]` serial line reports `uxTaskGetStackHighWaterMark` after an open — the
worst case since boot, inflate included.

### Memory, which is what a real book runs into

**`new` ABORTS under `-fno-exceptions`, with no message and no stack.** The reboot
looks like a navigation bug — twice now it has been reported as "opening a book goes
back to Home". `MCAUSE 0x2` plus `abort() was called` plus `addr2line` on the stack
words is how you get from that to `operator new` → `std::bad_alloc` → `__terminate`.

So every sizeable allocation in the EPUB path is **`std::nothrow`-checked** and
answers with a reason: `Zip`'s central directory and both of its read buffers, and a
pre-flight probe in `book.cpp` before `buildDocument` (whose `std::string`/`std::vector`
growth cannot fail politely — that one is a bound, not a guarantee).

**THE EOCD SCAN NO LONGER ALLOCATES.** It used to take the whole 64 KB comment
window in one `std::string`, which was the largest single allocation in the reader
and the first thing a full-length novel broke: 142 KB free and no contiguous block
that size. It reads 2 KB chunks on the stack, backwards, with a 3-byte overlap so a
signature at a chunk edge still reads whole. **Every zip fixture in the repo is
smaller than one chunk**, so the loop is covered by tests that append comments sized
either side of 2048, 4096 and 65535 — without those the rewrite was untested.

Two things about the numbers:

- **A cap is not a memory check.** `kMaxEntryBytes` is 512 KB, which protects against
  a file that lies about its size and does nothing about a file that is honestly too
  big for a 140 KB heap. Those are different failures and need different code.
- **The largest free BLOCK decides, not the free total.** Every reader buffer is one
  contiguous allocation. The `[open]` refusal line reports both.

**The Library is resident while you read.** It sits below the Reader on the stack, so
its entries stay allocated: 203 books cost ~59 KB (heap 201,576 → 142,560 in the
boot log), taken out of the heap exactly when a chapter needs it. **3C made this stop
mattering** — a chapter now peaks at ~70 KB whatever its length — so it is a saving
available if something later needs it, not a blocker.

### What the desktop measures, and what only the panel can answer

Desktop, 12-line page, 444px column, ppem 32: paginate 349 µs/page, lay out one page
168 µs, draw a page 580 µs cold (29 rasterisations) and 363 µs warm. The device is a
160 MHz RISC-V with no FPU and rasterises at ~3,794 µs a glyph, so a cold page is
~110–140 ms there and the pagination walk is the part with no desktop analogue worth
trusting. The `[open]` serial line reports parse, total, blocks, pages and the heap
cost of an open for exactly this reason.

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
