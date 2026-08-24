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

It was `third_party/stb_truetype.h:2802` — `count = (size < 32 ? 2000 : ...)` — and
the v2 `stbtt__active_edge` is 28 bytes, so stb pre-allocated 2000 slots (56,008
bytes) for the first edge of every glyph, for a text glyph needing ~20.

**THAT LEVER HAS BEEN TAKEN: the count is 128, and the spike is 3,592 bytes.** This
paragraph used to say it was not worth breaking the file's sha256-backed "vendored,
unmodified" claim *while 54 KB is affordable*. It stopped being affordable the moment
the reader shipped: with a page on glass the device has ~87 KB free, and the spike
took **minimum free heap to 18,952 bytes** — the whole margin, on a part where a
failed allocation is `abort()` with no diagnostic.

Three things about the change:

- **`stbtt__hheap_alloc` chains another chunk when one runs out**, so this trades
  memory for allocation count and nothing else. No glyph rasterises differently —
  all 686 tests pass, `text_sample.png` (Literata body text, the golden this file
  says to stop on) included, byte for byte.
- **IT DID NOT RAISE THE OBSERVED MINIMUM, so that attribution was wrong.** `min` was
  18,952 before the patch and 18,948 after — four bytes apart across two builds,
  which is not what a removed 52 KB transient looks like. **The real cause was the
  repeated archive parse in `walkToChapter`**: three `openBook` calls, each building
  a 121-entry `Zip` and a 92-chapter `Epub` on top of the previous chapter's live
  state. Removing it — done for speed, not for memory — took the floor from ~18,950
  to **45,840**, measured on the device across a full session. The stb patch is still
  right (15.6× smaller spike, and faster); it just was not this.
  `mark("open-located")` / `mark("open-paginated")` / `mark("chapter-opened")` /
  `mark("refine-complete")` are what settled it, and the stage where `min` falls is
  the stage that spent it. **Do not reason about heap from code shape; read the
  marks.**
- **It is also FASTER.** Three runs each on the desktop: cold page draw 572/530/500 µs
  against 1008/732/645, warm 372/366/314 against 634/473/414. A 56 KB malloc plus
  touching 56 KB of cold memory costs more than a 3.6 KB one. Note that the first run
  of any freshly built binary is the slowest by a wide margin — a single
  before/after pair here says nothing, and nearly had me report a 1.7× regression
  that did not exist.
- **The claim in the file's header is now false**, and the patch says so at the site.

Recorded here rather than only in the roadmap, because the next person to want heap
will come looking for this lever and needs to find it already spent.

## Shouting a title

**`upperLatin1` REPLACED `upperAscii`, because the device showed `LE FLéAU`.** The old
function was ASCII-only and `text.h` recorded the deferral in as many words — "a table
core/ should not carry", and "the titles that need one arrive with real EPUB metadata
in Phase 3". They arrived.

**It needs no table.** U+00E0..U+00FE is `C3 A0`..`C3 BE` in UTF-8 and the uppercase
U+00C0..U+00DE is `C3 80`..`C3 9E`, so the second byte drops by 0x20 exactly as an
ASCII letter's only byte does. One subtraction.

**It is safe because of what the fonts carry**: `fontc.py`'s `CODEPOINTS` is
`0x20..0x7E` plus **all** of `0xA0..0xFF`, so every accented capital has a real glyph.
That is the load-bearing fact — a correct mapping onto a glyph the subset lacked would
render as a **notdef box**, which is worse than a lowercase letter.

**Three exclusions, each a character whose uppercase is not one byte away:**

| byte | char | why not |
|---|---|---|
| `0xB7` | U+00F7 `÷` | not a letter; −0x20 is U+00D7 `×`, so a divide becomes a times |
| `0xBF` | U+00FF `ÿ` | uppercase is U+0178, outside Latin-1 and outside the subset |
| `0x9F` | U+00DF `ß` | uppercase is `SS` or U+1E9E, neither one byte away |

**Everything past Latin-1 still passes through untouched** — Greek, Cyrillic, Latin
Extended-A. Same rule as before, for the same reason: widening this means widening
`fontc.py`'s subset first.

**The render is pinned as well as the mapping**, and the distinction matters:
`test_text.cpp` compares strings and cannot see a notdef box, so
`home_accented_title` is a golden of `LE FLÉAU` on glass. Note that the acute sits
close to cap height against a 1.05 line-height, so a **wrapped** accented title is the
case to look at if one ever appears.

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
    written — and the factory's Library pointer is the whole arc of that in one
    place. It began as a comment enumerating who remembered to call
    `forgetLibrary`; `replaceApp` replaced the enumeration with one function; and
    the function was still wrong, because the prose it inherited said the pointer
    lived "as long as the App that built it" and a POP destroys the Library while
    the App lives on. Home > Library > Back dangled it, unreachably, for as long
    as the rule was a rule. It is the Library's own destructor now
    (`LibraryWatcher`), which needs no caller to remember anything — so the third
    attempt is the first one that could not have the hole. **A caller list turned
    into a function is only half done if the function still encodes the prose's
    assumptions.**
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
- **THE SIDE BUTTONS ARE MOVERS, and until page turns landed they did nothing at
  all.** `Button::Left` and `Button::Right` are the two side buttons — the shell maps
  the SDK's `BTN_UP`/`BTN_DOWN` onto them, because the SDK's names describe its band
  order and not this device's panel — and `gestureFor` had no case for either, so they
  fell through to `default: return {}`. Spec 4.0 puts page turns on them; the shell's
  own comment said "the sides turn pages in the Reader (Phase 3) and do nothing before
  it — verify when page turns land". Page turns landed and the mapping did not, and
  **nothing in `test_gesture.cpp` mentioned Left or Right**, so adding them broke no
  test. They share Up/Down's code path rather than getting a second one, so they
  inherit its repeat gating, its held flag and its dropped-`Long` rule; the Reader
  declares no auto-repeat, so a held side button turns exactly one page. Which
  physical side is which was never verified against behaviour, because until now there
  was none to verify against — if they turn pages the wrong way, the fix is the two
  `BTN_UP`/`BTN_DOWN` lines in the shell, not `gesture.cpp`.
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
| Home | `Main.dc.html` | Focus starts on the CONTINUE block (`-1`), not the menu. Its title WRAPS. |
| Home / empty | `HomeEmpty.dc.html` | A **variant**, not a screen: same `ScreenId`, same view model, same menu. |
| Home / nothing open | `HomeUnopened.dc.html` | The same variant with different words. What the device actually shows today. |
| Library | `Library.dc.html` | The only list that scrolls today, and the only screen with a rail. |
| Library / scrolled | `LibraryScrolled.dc.html` | Reached by pressing PAST the focused row and back — arriving from above windows it differently. |
| Item actions, Delete confirm | their own boards | Overlays; a focus move repaints the overlay alone. |
| Book details | `BookDetails.dc.html` | Not an overlay, despite covering the Library. Its title **wraps**; everywhere else elides. |
| Settings | `Settings.dc.html` | Draws nine rows and only three respond. |
| Sleep | `Sleep.dc.html` | Painted directly, never pushed — a push would make the wake restore into it. |
| Sleep / nothing open | `SleepIdle.dc.html` | The badge alone. Same screen with its card removed. |
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

**THE SLEEP SCREEN IS PAINTED NOW.** It was written, boarded, themed, simulated and
tested for two phases without ever reaching glass, because **its content did not
exist**: the board is the reading state, and painting it with no Reader would have
been demo fiction or an empty card. Progress persistence supplied the content and
`sleepNow` now renders it.

**IT IS PAINTED WITHOUT BEING PUSHED**, which was the trap recorded here for whoever
wired it and is now the reason `paintSleepScreen` bypasses `App` entirely. The session
record names the top of the stack, so pushing `SleepScreen` would make the next wake
RESTORE INTO IT — press power, get "asleep, press power to wake" back. Bypassing `App`
moves two things it normally owns into that function: the **clear**, and
**`gFrameContentsUnknown`**, because `App`'s partial-repaint record now describes a
frame that no longer exists. Nothing reads it before the chip resets, but leaving a lie
there is a trap for the next person to paint something after it.

It costs one **FULL** waveform (~825 ms) on every sleep — full rather than fast because
this is the last thing the panel does for hours and a differential update would leave
the previous screen's residue under it.

**ASLEEP WITH NOTHING OPEN IS ITS OWN BOARD** (`SleepIdle.dc.html`): the card *is* the
reading state, and the device sleeps from Home or the Library as often as from a book.
Painting the card with a blank title or a 0% bar would claim a book the user is not
reading; painting nothing at all is worse, because e-ink holds its last image and a
Library left on the glass gives no clue the device is asleep rather than frozen — which
is the entire reason this screen exists. So the **badge is the load-bearing half and it
stays**, drawn by the same tail in both states so the two cannot disagree about where
it sits; the card is the half with something to say only sometimes. One screen with and
without its content, not two screens. `SleepViewModel::nothingToContinue` is spelled
exactly as `HomeViewModel`'s, because it is the same fact and one rule should have one
spelling. Measured against its board at **0.27%**, the closest panel on the sheet.

The `6% · CH. 01` line is the percentage and the SPINE POSITION. A chapter *name* would
need a table of contents, which is not built — the same reason the Reader's own footer
says a bare `CH. 03`.

**AND THE MIDDLE DOT CAUGHT THIS PROJECT'S OWN RECORDED TRAP AGAIN.**
`"%d%%\xC2\xB7CH. %02d"` parses `\xB7C` as ONE hex escape, because a C++ hex escape is
unbounded. clang rejects it outright; the ESP32's GCC **accepted it** and would have
emitted a byte that is not U+00B7. This file already recorded the same shape once
(`"\xA0b"` is 0xA0B). Adjacent string literals end the escape.

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

**THE LABEL IS THE CHAPTER'S NAME**, from `toc.h`, and it was a spine position for two
phases because the spine gives an order and no names. This paragraph tracked that in
three states — first "the position is the only thing honestly known", then "a table of
contents now exists but `updateChapterLabel` still prints `CH. %02d`", and now the
wiring. The second of those was a **follow-up recorded in prose**, which is the shape
this file warns about: it stayed true for exactly as long as nobody read it.

**THE POSITION IS STILL THE FALLBACK**, for a book with no contents and for a chapter its
contents does not mention — spine entry 0 of a real book is its cover, and nothing names
that. One slot, the best name available for it.

**AND THE HEADER'S PRIORITY INVERTED WITH IT.** The theme drew the chapter FIRST and
reserved its width, because `CH. 01` was `white-space: nowrap` and the book title was the
run with slack to give up. A NAME is the long run now ("PREMIÈRE PARTIE : À LIRE AVANT
L'ACHAT"), so the board gives it `min-width: 0` and the title keeps its space — with the
title capped so the chapter can never be squeezed below `kReadChapterFloor`, enough for
the fallback form plus an ellipsis. Both runs elide; either can be arbitrarily long on a
real card.

**AND ONE LABEL THAT LOOKS LIKE A BUG IS NOT ONE.** Le Fléau's contents names a chapter
`S...`, and that is the book's own data: the chapter has no title and opens "Sally." with
the S as a drop cap, so the publisher generated the label from its first characters. The
parse is right; the ebook is thin.

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

### A chapter's page count arrives after its first page

Counting a chapter is one decode of it — ~545 ms on the device for a long one — and
paying that before the first page appeared made **crossing into a chapter cost 1.14 s
against 575 ms for an ordinary page turn**. A crossing *is* a page turn from the
reader's side, so it should cost what one costs.

So a forward landing decodes only as far as page one, and the index grows a cursor at
a time as pages are passed. Three consequences, each load-bearing:

- **`ReaderViewModel::pageTotal` is 0 while the count is unknown**, and the footer
  draws an **em dash** for it — `3 / —`. `design/Reader.dc.html`'s footer states the
  form and why: a blank makes the slash read as broken, `0` would be a lie, nothing
  here animates, and a dash is the same width every time so the counter does not
  reflow when the number arrives.
- **`pageCount()` is pages KNOWN, not pages total.** Reporting it as the total would
  count up as the reader advanced — `1 / 1`, `2 / 2` — which is worse than admitting
  it is not known. `indexPending()` is what distinguishes them.
- **The stream decides whether a next page exists, not the index**, because with the
  count unknown the index cannot say. `advance()` returns false only when the
  chapter's blocks are exhausted, and that is also the moment the count becomes known.

**A SMALL CHAPTER IS COUNTED BEFORE ITS FIRST PAINT, and a big one is not.** Folding
the count into the refinement made a **four-page chapter take four seconds** to show
its total — the refinement's 5 s window is sized for an expensive cosmetic repaint,
and counting is neither.

`ReaderScreen::kEagerCountBytes` is **8 KB**, and the number is measured **on the
panel**. It was first set to 64 KB from a desktop figure times a remembered ratio, and
that was wrong by 8×:

- **The desktop figure was right** — 41.1 µs/page, and a 33 KB chapter's count pass
  really is 1.7–1.9 ms there.
- **THE RATIO WAS WRONG.** 37× came from a *render* measurement. This path is not
  render-bound: it is SD reads through SdFat on the display's SPI bus plus an inflate
  on a part with no FPU, and **the desktop does neither**. Against 3.5 ms of desktop
  work for two passes the device spent **484 ms** — ~135×.
- So the constant is device milliseconds per KB: **14.8 ms/KB for the two-pass eager
  open**, ~7.2 ms/KB a pass, which independently matches the ~545 ms this project
  measured counting a long chapter.

At that price **64 KB is 932 ms — 163% of a ~570 ms page turn**, so the eager count
cost more than the turn it was hiding inside: the 1.14 s crossing this design exists
to avoid, reintroduced at a smaller size. 8 KB is 118 ms, ~21% of a turn, and covers
14% of a real book's 92 chapters — the front matter a reader lands on, where a
six-page chapter reading `1 / —` looks like a defect. The median chapter is 53 KB and
gets the dash, as designed. **Bounded by bytes, not by a page budget** — the bytes are
known before any work is done, where a page budget would spend itself on a long
chapter and still have no total.

**TWO PASSES IS INHERENT here, not slop.** One pass ends at the chapter's END, and a
forward turn needs the builder live just after page one — the content and the stream
position cannot both come from one walk. Counting on a second `ChapterReader` would
buy one pass for another 32 KB window against a 45,840-byte floor.

**AND THE LESSON GENERALISES: THIS FILE'S ~37× RATIO IS A RENDER RATIO.** It is quoted
in "What the desktop measures" for cold page draws, where it holds. Applying it to
anything that touches the card or the inflater underestimates by ~4×. The paragraph
below already says the pagination walk "is the part with no desktop analogue worth
trusting"; this is what ignoring that costs.

A chapter over the threshold is counted in a quiet window of its own,
`kCountQuietMs` = 1200 ms, **and then repaints on the FAST path**. That repaint was
originally left out — "counting changes one number in the footer, and a ~570 ms paint
plus a waveform to fill it in is a bad trade; the total appears on the next page turn"
— and the device showed the flaw in it. Measured across a chapter crossing:

```
[chapter] spine=4 bytes=33463 deferred pages=1 in 90ms   <- press at 24226
[index] pages=40 in 440ms (deferred: chapter over 8192B) <- counted by 25782
[refine] done total=1423ms                               <- on glass at 30565
```

**The count finished at 1.56 s and the number was not visible until 6.34 s**, because
the "next page turn" almost never wins the race against the refinement's 5 s window.
So "no extra waveform" bought nothing and cost four seconds of a footer reading
`1 / —` with the answer already in memory. One fast paint (~596 ms) puts it on glass
at ~2.2 s, and the refinement still follows on its own schedule — `renderTop` leaves
it owed for a grayscale screen anyway.

**A press arriving during the count cancels the paint**, checked after the count as
well as before it: the page on glass is already correct, so getting out of the way
beats putting ~596 ms in front of a page turn. The refinement completes the count too,
as a backstop, and applies the same rule to itself.

**A BACKWARD crossing still pays the full count**, and cannot avoid it — landing on
the previous chapter's *last* page means knowing which page that is.

**THE COUNT DECIDES BEFORE LANDING, NOT AFTER**, and the first version got that
backwards: it landed on page one and *then* counted, which decodes page one, then the
whole chapter, then page one again — **three passes where two will do**. On device the
wasted pass is why a small chapter still felt as slow to open as it had before any of
this, and why the em dash never appeared to compensate. The size is known the moment
the stream is begun (the central directory said so), so the branch costs nothing to
take early. Worst eager open over a real book's 58 sub-threshold chapters: 4.4 ms
desktop, ~162 ms at this project's ratio, against 0.6 ms for a deferred one.

**AND THE EAGER SIDE NEEDS ITS OWN LOG LINE.** Only the deferred path had one, so a
device reporting "no dash, and the page is slow again" could not say whether the
count had run or how long it took — the branch was unobservable from the one place
that can measure it. `[chapter] spine=N bytes=B counted|deferred pages=P in Xms` is
printed at both open sites, and `indexPending()` IS the branch.

Two bugs this shape cost, both in restoring state:

- `openChapterAt` moves the index out before walking and puts it back on failure.
  Restoring through the forward landing instead threw the count away, so paging back
  off the front of the book left a counted chapter reading `1 / —` again.
- With **no book behind the screen** (the in-memory constructor), the walk fails
  immediately and the moved-out index was never put back — pressing past the last
  page of the demo chapter left the screen reporting **zero** pages. There is now an
  early return before anything is disturbed.

**The simulator and the goldens both complete the index before rendering**, because
the board shows the settled state. A simulator that rendered the transient one would
put `1 / —` in the comparison sheet against a board that says `53 / 890`, and would
disagree with the goldens — the desktop-diverging-from-the-device trap this project
has hit before.

### Opening a chapter: what the two seconds were

The device reported a 40-page chapter taking ~2 s to open against near-instant page
turns. That is the index pass, and two thirds of it was waste.

| | index pass | forward turn | backward turn |
|---|---|---|---|
| before | 41.4 ms | 1.22 ms | 35.6 ms |
| after | **14.8 ms** | **0.46 ms** | **14.7 ms** |

Desktop, three runs each within 1%. On the device the whole open went **511 → 192 ms**
and its pagination phase **433 → 119 ms**. Two changes, and the second was much the
larger:

- **An index pass wants page BOUNDARIES, not pages** (`PageBuilder::countOnly`). It
  was building a `LaidLine` per line — an owned string copy and a justification
  `measure()` — and then discarding all of them: ~480 of each for a 40-page chapter.
  Worth 41.4 → 35.5 ms, which is less than it sounds like it should be.
- **A 256-entry Latin-1 advance/gid cache in `ScalableFont`**, which the roadmap had
  recorded as a lever and which turned out to be most of the cost: 35.5 → 14.8 ms.
  `advance()` did a cmap binary search per character and `kerning()` did **two**, and
  `measure()` calls both — while `wrapProseLead` grows lines greedily and measures
  every candidate, so every glyph of a chapter was measured several times over. 1 KB,
  cleared by `init()` because the advances are in pixels.

**WHAT DID NOT IMPROVE: the draw.** A page turn's `render` stayed at 125–165 ms on
the device, unchanged by the advance cache — the coverage blit dominates it and
`kerning`'s cmap searches were noise beside it. If a page turn has to get faster than
~570 ms, the blit is the target and the metrics are not.

**The neutrality of counting mode is asserted, not assumed**: a probe indexed all 92
chapters both ways and got 7,968 pages each, 0 chapters differing. An index that
disagreed with what gets rendered would be the worst possible bug here.

`PageBuilder::pageHasContent()` exists because of it. `buildIndex` used
`finish().lines.empty()` to mean "was there a trailing partial page", which in
counting mode is always true — so every chapter's last page vanished from the index,
and a chapter that fits on one page indexed to nothing at all.

### openBook reads the whole spine once

It used to take a chapter index and return that chapter's offsets, so the reader
called it again for every chapter it wanted — and **every call re-reads the
121-entry central directory and re-inflates the 8,472-byte OPF**, about 32 KB of
transient allocation. Reaching this book's first chapter with text means trying
three spine entries, so that is three of them.

The device measured it, once `mark()` was put either side of the open:

```
[stage] open-located    heap=133712 min=85860
[stage] open-paginated  heap=87160  min=41188
```

**The pagination phase took minimum free heap from 85,860 to 41,188** and cost
433 ms of a 511 ms open. So the whole spine is read once and kept: `ChapterSpan` is
12 bytes an entry, **1,104 for a 92-chapter book**, and a chapter change is a row
lookup with no archive, no directory and no OPF.

**IT KEEPS THE LOCAL-HEADER OFFSET, NOT THE DATA OFFSET, and the first attempt got
that wrong.** Resolving one to the other is a 30-byte read, and doing all 92 when the
book opened took `locate` from 76 ms to **444 ms** — more than the pagination it was
meant to make cheap, because the headers are scattered across 12.7 MB and SdFat has
one sector cache. `ChapterReader` resolves the one chapter it is asked for and caches
it, so a rewind does not go back to the card. **Moving work is not removing it.**

**`Epub::open` VALIDATES EVERY SPINE ENTRY** against the manifest and the archive and
refuses the whole book if one is missing — `epub.cpp:186` and `:189`, two distinct
messages. So `ChapterSpan::readable()` is narrower than it looks: the only way to an
unreadable span is `zip.locate()` failing on a corrupt local header.

A comment in `book.cpp` claimed the opposite — that `Epub::open` lets a broken
chapter through so a book with one still opens — and that claim was **never true of
the code**. It was asserted three times, propagated into another header, and finally
into a test expectation, which is what made someone read `epub.cpp`. **A comment
about a neighbouring layer is not evidence about it.**

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
- **THE QUIET WINDOW HAS TO MEAN "STOPPED", NOT "BETWEEN TURNS".** 600 ms did not,
  and it made rapid page turning *worse* than no refinement at all. A paint blocks
  the loop for ~520 ms, so the earliest a second press can be dispatched is ~520 ms
  after the first — steady turning therefore produces gaps clustered just above
  that, and a 600 ms window fired ~80 ms after each paint finished, exactly where
  the next press lands. It then blocked that press for its own ~550 ms.
  `kRefineQuietMs` is **5000 ms**, and the number comes from the asymmetry rather
  than from taste. Measured across twelve consecutive page turns on the device: the
  gap between the panel going free and the next press being painted was a median of
  **72 ms**, with two of the twelve at **898 ms and 1360 ms** — pauses taken while
  still turning. A refinement measured **1408 ms** and cannot be interrupted. So
  firing early costs 1408 ms of dead buttons; firing late costs a page that stays
  dithered a little longer. 5000 ms is ~3.5× both the longest observed pause and the
  cost of being wrong, and still a fifth of the ~23 s a reader spends on twelve
  lines.
- **It also refuses to start with input already queued** (`rawSamplesPending()`).
  The clock alone cannot see a press that arrived during the paint, and starting
  something the panel cannot interrupt in front of one is the defect the window
  exists to avoid.

`[paint] … refine-owed` and a separate `[refine] done total=…` keep the two costs
distinguishable in the log — a page turn is the fast paint, and the refinement is
what the page settles into.

**THE REFINEMENT COSTS MORE THAN THE SINGLE GRAYSCALE PAINT IT REPLACED**, and that
is the honest accounting: 1408 ms (1041 panel + 367 render, four render passes)
against ~1056 ms. It is still the right trade because what the reader waits for is
TEXT, and text arrives at ~570 ms instead of ~1056 — but the extra ~900 ms of panel
work is real, and it is battery and panel wear rather than latency. A reader turning
pages steadily never pays it at all.

Its base pass does take the cheap settle path as intended — 366 ms of `gray_DRF`
with no visible flash — which is what makes the upgrade look like a refinement
rather than a second paint.

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

### Reading progress lives on the card

`/.reader/state/<hash>.json` per book, plus `/.reader/last.json` naming the book last
open. **Card-side, not NVS**, and the reasoning is worth keeping because the session
record went the other way: a wake must work with no card, so *which screen* has to
survive an empty slot — but a reading position does not, because with no card there is
no book to open. What card-side then buys is a correct card swap **by construction**,
where NVS keyed by path would restore page 400 into a different hundred-page novel.
Spec 4.0 had already named `/.reader/state/` when it said deleting a book "never
erases reading progress".

**THE RECORD DEGRADES INSTEAD OF BEING DISCARDED** (`reading_position.h`), three
numbers of decreasing durability:

| field | survives | because |
|---|---|---|
| `spine` | nearly everything | it indexes the OPF's spine, the book's own structure |
| `block` | a re-layout | blocks are `document.h`'s and owe nothing to a column or a ppem |
| `line` | neither | it is a line *within* a block at one ppem and one column width |

So `fitOf` grades a record `Exact` / `Relaid` / `Rebound` / `Unusable`, **weakest
wins**, and `restoreFrom` zeroes what the grade cannot support. The top of the right
paragraph beats the front of the book, which beats nothing; landing on line 9 of a
block that now has four lines is a wrong page that looks like a bug.

`bookBytes` is the identity check — the cheapest one a `FileSystem` with no timestamps
and no hashes can offer, and `DirEntry` already carries it. Not a checksum and it does
not pretend to be.

**THE SIDECAR'S NAME IS A HASH** (FNV-1a, 8 hex) because a book path is not a
filename: it holds `/` by construction, FAT forbids more, and real cards carry
accented 90-character titles. Collisions are handled rather than assumed away — the
path is stored *in* the file and a mismatch reads `Unusable`, so a collision costs one
book its position and can never misapply another's. Refused in two independent places
(`loadPosition` and `fitOf`).

**A SAVE HAS THREE ANSWERS AND THE MIDDLE ONE MATTERS.** `Unchanged` means the card
already says this, so nothing was written — the common case when a save fires on
leaving a book the reader did not move in, and it rests on `serialise()` sorting its
keys. **`Failed` MUST NOT BE TREATED AS FATAL**, and that is the one hazard in the
feature: a card can be readable and refuse writes (a physical write-protect tab), and
`writeAll` calls `noteCardGone()` when a write it had already opened goes wrong, which
`pollCardPresence` turns into an App rooted at `SdMissingScreen`. So acting on a
failed save would throw the reader out of a book they can still read. The shell logs
it and carries on.

**THREE SAVE EDGES, NOT EVERY PAGE TURN**: leaving the book with Back, crossing a
chapter, and sleeping. There were FOUR: the reader menu's `Close book` popped the Reader
from underneath an overlay, which the `leaving` save — fired on Back with the Reader ON
TOP — could not see, so it carried its own `closing` edge. **That row was cut
(2026-08-24) and its edge with it**: Back from the page is the one way out of a book
again. A turn is ~570 ms of panel and a card write on each one would be felt; a
chapter is also the most a power cut can cost. **Leaving is saved BEFORE the
dispatch** — Back pops the Reader and once popped there is no screen left to ask where
the reader was. Back is the only way out (`Gesture::Back` → `Action::pop()`), so this
is one save on the way out rather than a save per event.

**RESTORING COSTS A WALK TO THE READER'S PAGE, NOT A COUNT OF THE CHAPTER.**
`ReaderScreen::openAtCursor` walks page boundaries to the page holding the cursor and
stops — which is what lets the footer say *which* page this is, since a cursor carries
no page number and the number is a count of the boundaries before it. The total then
arrives in the quiet window like any other chapter's, so a restore shows `7 / —` with
a right numerator and an honest denominator. `Cursor{}` is both "no target" and "the
top of the chapter" and takes the cheap path for both — which is also what a `Rebound`
restore asks for.

Tested by the restore equivalent of the strongest paging property here: **a cursor
saved on a page reproduces THAT page, checked for every page of a chapter** — a walk
that stops a boundary early is right at page 1 and wrong everywhere after it.

**PROGRESS IS A FRACTION OF THE BOOK'S BYTES, NOT ITS PAGES**, and that is what makes
it affordable at all. A page-based percentage needs every chapter paginated: 6.94 MB
of inflated XHTML for one real novel, **~49 s of decode** at the measured 7.2 ms/KB.
The byte layout is already in `ChapterSpan`, so `progressPercent` is a sum over 92
integers. It interpolates within the open chapter only when that chapter's count is
known, so the number can sharpen when a deferred count lands — which is honest.

**`last.json` CACHES title, author and percent** so Home can name the real book
without opening an EPUB at boot (a central directory plus an OPF parse, ~100 ms and
~32 KB of transient, for a block the user may not be looking at). The cost is that it
can go stale, so **it is checked against the card** with one `exists` call before
anything is drawn — Home confidently offering to continue a book that cannot be opened
is worse than not offering. `HomeMissing.dc.html` is the boarded state for that case
and is not built, so a stale pointer currently falls back to the nothing-open screen.

**"THE BOOK IS CLOSED" MEANS NO READER IS LEFT ON THE STACK**, not that one is no
longer on TOP — and it asked the wrong question the moment the reader menu existed. The
menu and the contents are pushed ABOVE the Reader, so opening the menu declared the book
closed, cleared `gReading.open`, and with it the gate on the factory priming: pressing
Contents primed nothing, the factory refused (correctly, now that it refuses), and the
device reported "opening Contents does nothing". Scanned rather than tracked, because a
depth count would be a second copy of the stack's own shape.

**A WAKE CANNOT RESTORE THE READER WITHOUT ITS BOOK, and that is why sleeping on a
page woke to the Library.** `App::restore` pushes the record's stack, the Reader's push
goes through the factory, and the factory REFUSES a Reader with no book — deliberately,
since falling through to the demo is how this device once woke into Middlemarch. So the
restore correctly stopped short of a screen that could not be built. Nothing was wrong
with the restore; the book had never been set.

The shell now primes the factory from `last.json` before restoring, when the record
names the Reader anywhere in its stack. **Two records, two jobs:** the session record
says WHICH SCREENS and has never known about a book; the card says where in the book.
`openBookAt(path, bytes, push)` is the one function both a button press and a wake go
through — extracted precisely because they must agree, with `push` false for the wake
because `App::restore` does the pushing.

**EVERY BOOK READ `NEW` IN THE LIBRARY, and `screen_library.h` had already written down
why:** "a real percentage needs `/.reader/state/`, which has nothing to record until the
Reader exists". It does now. Two things had to change together:

- **The sidecar STORES its percentage.** Derived data in a record is usually a smell and
  this is the exception that earns itself: recovering it needs the book's chapter byte
  layout, so the Library would have to OPEN every started book's archive to draw a
  column of numbers. It is exact when written and goes stale only if the book changes —
  which `bookBytes` already detects, and which drops the position anyway.
- **`loadProgressIndex` reads the whole directory once.** One listing plus one read per
  book STARTED — not per book on the card, which is the point: 203 books with three
  started costs a listing and three reads, where asking each row for its own sidecar
  would be 203 opens, most of them misses, on a screen that has to paint. A corrupt
  record is skipped individually, so a save cut by a power loss costs one book its
  percentage and nothing else.

**BOOK DETAILS' ROWS COME FROM THE SAME INDEX**, and this is why the sidecar carries
derived data at all. Its `Progress` row is the percentage and its `Current story` is the
chapter name, both read from the one listing the Library already does — where deriving
either would mean opening the book's archive, and the chapter name would mean parsing its
NCX as well, per row.

**THE AUTHOR CANNOT COME FROM THE SCAN**, and that is the one field that needed a
different answer. It lives in the OPF, so learning it per row is ~100 ms an archive open
and **~20 s for a 203-book library**, on a screen that has to paint. Book details shows
ONE book, so the shell reads it on the press that opens the screen — one open, and there
is heap for it precisely because no Reader is on the stack, the same 48 KB the table of
contents could not find from under a live one.

**TWO FIELDS WERE REMOVED RATHER THAN LEFT BLANK**, because a row that can never be
filled reads as a device that failed to load something:

- **`Added`** wanted a file timestamp and `DirEntry` is `{name, isDir, size}` — no date
  anywhere in `FileSystem`, so filling it is a change across three implementations and
  the contract's 27 clauses rather than a metadata question. SdFat does expose file
  dates, so it is reachable; it is not this screen's work. **Five field rows now, not
  six.**
- **The subtitle** is not in the data at all. Checked across four real books: **not one**
  carries a `title-type=subtitle` refinement or any subtitle marker. `dc:date` is in all
  four, but a publication year is not a subtitle — deriving one from the other would be a
  different fact wearing its clothes. The board's "Fifteen stories · 1914" was authored
  copy.

Removing the subtitle gave the TITLE its line back, because the title's line budget is
the block's room less the column's FIXED runs and the subtitle was one of them. And the
block above the fields did NOT move — the cover is 180px and the column was shorter than
it, so the block's height is the cover's. That was measured off the board's own render
rather than reasoned about, which is why the rule positions in
`test_screen_book_details.cpp` are still 290/291.

**`Current chapter`, not `Current story`.** Every other slot on the device that names this
thing calls it a chapter — the reading page's header, the contents list, Home's counter —
and one screen calling it a story was the odd one out.

**A NEW SIDECAR FIELD READS BLANK ON AN OLD SIDECAR**, and that is worth expecting rather
than diagnosing: `chapter` was added after positions were already being written, so the
row is empty until the book is saved once more. `percent` had the same first run. The
record loads either way — refusing it would cost the reader their place to gain a label.

**AND THE PROGRESS ROW LOST ITS PAGE COUNT** — the third slot on the third board to do
so, after Home's CONTINUE block and Contents' rows, for the same ~49 s reason every time.

`percentFor` returns **-1 for "not started", not 0**: a book at 0% has been opened and
one that has not reads `NEW`, and the board draws those differently.

**HOME'S TITLE WRAPS, AND IT USED TO ELIDE.** The board said `text-overflow:
ellipsis` and the device showed a truncated book name on the one screen whose whole
job is to name the book being read — where an ellipsis on a *list row* hides only
which of seven rows this is. Both Home boards now say `overflow-wrap: anywhere`, and
the theme reuses Book details' mechanism: `wrapProseLead(..., WordBreak::Anywhere)` →
`clampProse` → `drawProse`. `Anywhere` because a title falling back to a filename is
usually one word with no break opportunity at an underscore or a hyphen.

**The line budget is DERIVED, not pinned**: the canvas less the band and the block's
padding, less the bar and the slab with their gaps, less the bottom-anchored menu and
hint bar, less the column's three fixed runs, over the title's line box. It comes out
4 lines on the X4 and 3 on the X3 — different branches of one arithmetic, which is
why both geometries are tested. `renderHome` built its hints twice; the reading path
now reuses the pair built at the top, because the bar's height is an *input* to the
budget.

**IT SHIPPED A USE-AFTER-FREE FIRST, and the way it hid is the lesson.** `Prose::lines`
are `string_view`s into the text handed to the wrap — components.h says "which must
outlive the Prose" — and the theme passed `upperAscii(vm.title)` inline, a temporary
that died at the end of the expression. A title long enough to wrap drew from freed
memory and rendered as a column of **notdef boxes**; a short one rendered correctly,
because the freed bytes were still there. So every golden passed, and so did the
row-counting test written to prove the wrap works — **a notdef box inks rows exactly
like a letter does**. What caught it was rendering a long title to a PNG and looking
at it. `home_long_title` goldens exist at both geometries now, which is the check that
distinguishes ink that spells something from ink that does not.

**`test_long_title.cpp` SAID "FOUR SCREENS" AND HOME WAS NOT ONE OF THEM** — the four
were Library, Book details, the actions panel and the delete panel. So the most
prominent title on the device was the one uncovered by the file that exists for
titles, and changing Home's from eliding to wrapping broke no test. Its cases are
there now.

**HOME'S VIEW MODEL IS BUILT ONCE, AND THAT WAS A BUG.** Home is the App's ROOT, so
returning to it hands back the same instance with the view model it was CONSTRUCTED
with — built at boot, before any pointer existed. The device reported it directly:
after reading a book, going Home still said `NOTHING OPEN YET`.

The whole App is replaced (`buildHomeApp`) rather than the view model swapped, because
the two Home shapes have different **focus rings** — `WithNone` where a CONTINUE block
exists, `Noneless` where it does not — and `Focus::None` is a construction-time
property with no setter. Only ever at depth 1, where the root is the only screen.

**IT IS GATED ON A FLAG, NOT DONE UNCONDITIONALLY, and the reason is the cost:**
`homeVmForCard()` counts `/books`, and a listing is ~2.7 ms an ENTRY on this card —
~1.1 s on a 203-book library, since macOS writes a `._name` beside every file. An
unconditional refresh would put a second's pause on a Back that is currently instant.
`gHomeStale` is set exactly when a reading position is saved, which is the only thing
on the device that changes what that block says.

**The focus is carried across the rebuild.** Otherwise pressing Back from the Library
would move a selection the user never touched. `setFocus` clamps, which is what makes
it safe across a ring that changed shape. It does mean landing back on `LIBRARY`
rather than on the new CONTINUE block — predictable rather than helpful, and the
opposite choice would be a focus jump nobody asked for.

**HOME'S CONTINUE BLOCK LOST ITS PAGE COUNTER, and the board says why.** It drew
`PAGE 53 / 890` over `CH. 01 — MISS BROOKE` and **neither was obtainable**: the first
needs the ~49 s book-wide count, the second needs a table of contents
(`Contents.dc.html`, not built — which is also why the Reader's own footer says a bare
`CH. 03`). Both lines became one that is free and true, `CH. 08 OF 92`, at the 0.16em
counter tracking of the line it replaces. `HomeViewModel::currentPage`/`pageCount` are
gone with it. Re-blessing the four Home goldens was verified the strong way: the change
is confined to rows 283–468 with **0 pixels differing** above or below, so the header
band, cover dither, title, author, the 67px numeral, both menu rows and the hint bar
are bit-identical.

**CONTINUE AND THE BOARD'S `READ` HINT BOTH ANSWER `Action::open()`** — they used to
answer `none()` behind a "the Reader is Phase 3" comment, and a slab that draws and
does nothing is the dead-button defect this project has shipped twice. Two screens can
now ask to open a book and they mean different ones, so `handleOpen` resolves it: the
Library means its selected row, Home means the pointer's path. `Action::Kind::Open`
carries no path deliberately, since `core/` does no storage. Neither fires on a
no-reading-column variant: CONTINUE is unreachable there by the model (the ring is
built `Noneless`) and `READ` is gated on `nothingToContinue`, because those boards draw
an empty first hint slot and a bar that promises nothing must not do something.

### Home has THREE states, and two of them share one mechanism

`Main.dc.html` has a reading position to show. `HomeEmpty.dc.html` has no books.
`HomeUnopened.dc.html` is the gap between them — **books on the card and none of them
open** — and until it existed the shell filled the CONTINUE block with `demoHomeVm()`
for any card with books on it, so a device that had never opened a book showed a
stranger's Middlemarch at 6%. Same defect class as the section below: content
substituted where the honest answer was "there is nothing here yet".

**The two no-reading-column states are ONE mechanism**, and the flag that drives it
is `HomeViewModel::nothingToContinue` — renamed from `libraryEmpty`, which named only
one of its two causes. It replaces the reading column with a centred block and builds
the focus ring `Noneless` so -1 (the CONTINUE block) is unreachable. The states differ
in **what they say** and in the LIBRARY row's value (`EMPTY` against a count), never
in what they draw; a second flag or a second render branch would be two ways to spell
one layout, and the boards say it is one layout.

`test_screen_home.cpp` asserts the structural fields of the two variants **against
each other** rather than against literals, so a change made to one and not the other
fails — that drift is the thing the shared mechanism is supposed to make impossible.

**`demoHomeUnopenedVm()` is unconditionally correct on the device today**, because
nothing persists a reading position: no card has a book in progress. When progress
persistence lands, the third branch appears in `homeVmForCard` and this becomes the
fallback for "books, but none started".

**Rejected: keeping the reading column and offering a book with a START slab.** There
is no non-arbitrary book to pick — nothing has been opened so there is no most-recent,
and `FileSystem` carries no timestamps so there is no newest either — and three of the
block's five fields (the 67px percentage, the page counter, the progress bar) exist
only to describe progress, so they would all blank at once and read as a broken screen
rather than a fresh one.

**THE NO-READING-COLUMN LAYOUT NOW HAS GOLDENS**, at both geometries, and it had none
before: `home_empty` was checked only by `make compare`, which renders both sides fresh
and so cannot see the two drifting together. It is also the layout with the most
arithmetic on screen — a centred 112px mark, a centred title and a wrapped paragraph,
all accumulated in 1/64 px because the prose's height is a fraction (1.55 × 29px =
44.95). Design-vs-firmware mismatch measured **1.34% / 1.23%**, against the ~5.4%/6.4%
this project averages, and the firmware wraps the sentence at the same break as Chrome
— so `max-width: 400px` holds in both engines here, unlike SdMissing's, which needed
420.

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

## The table of contents

The seventh reader layer (`toc.h`), and the last one that reads the archive rather than
the text. The spine gives an ORDER and no names, which is why the Reader's footer says
`CH. 03`, Book details' "Current story" is blank and there is no chapter list to jump
from.

**IT IS THE NCX, NOT THE EPUB 3 NAV DOCUMENT.** Measured over four real books before
writing anything: every one carries an EPUB 2 `toc.ncx` and **not one** has a nav
document. Building the modern form first would have parsed something no book on this
card contains. The nav document is a later job and a small one — `Epub::tocPath()`
already answers "which part is the contents" by media type, so it is the only thing
that would need widening.

**`Epub` NOTES THE NCX DURING THE OPF WALK**, which already resolves every manifest
href — finding it later would mean re-parsing the OPF, and scanning the archive for
`*.ncx` would be a guess where the manifest is a statement. Two routes, both needed:
the spine's `toc` attribute is the formal one and is OPTIONAL (real files omit it), and
the `application/x-dtbncx+xml` media type is what makes an NCX an NCX. The spine's
answer wins where both exist.

**A MEASUREMENT WAS WRONG AND IT CHANGED THE DESIGN.** This section first said real
files are flat, and that `Contents.dc.html`'s two-level grouping "does not exist in
real files". The check was a regex looking for a `navPoint` inside a `navPoint` that
allowed only tags between them — real files put text there, so it reported every book
as flat. Parsed properly:

| book | entries | by depth |
|---|---|---|
| Le Fléau | 96 | **`{1: 10, 2: 84, 3: 2}`** |
| Darkly Dreaming Dexter | 28 | `{1: 28}` |
| …another edition | 31 | `{1: 31}` |

So one book is three levels deep — ten section headers over eighty-four chapters — and
the board was right. `TocEntry::depth` carries it. **The list stays LINEAR**, not a
tree: a tree needs allocation per node and a traversal to draw, where a screen wants
"the Nth visible row", and a depth is all the board's grouping needs. Every entry is a
real target either way, because a section header in an NCX carries its own
`content src`.

**A LOOSE REGEX IS NOT A MEASUREMENT.** This project's habit of measuring before
designing is what caught the nav-document question; the same habit applied carelessly
got the nesting question backwards and wrote the wrong claim into a header. Where the
answer decides a design, parse the thing.

**COMMITTING AN ENTRY HAPPENS AT TWO MOMENTS**, and only handling one lost every
parent: a `navPoint` is complete when it closes AND when a CHILD opens, because the
child's start clears the label the parent had already read. A test caught it. State is
cleared after each commit, so a parent's close adds nothing — verified by deleting the
duplicate rule and confirming the nested case still passes, since it used to be correct
only by accident of that rule.

**AN IDENTICAL ROW TWICE IS NOISE; A DIFFERENT NAME FOR ONE TARGET IS CONTENT.** Real
books produce both, and only the PREVIOUS entry is compared — an NCX is authored in
reading order (0 out-of-order entries across all four), so a repeat is adjacent and a
full scan would be quadratic for a case that cannot happen far apart.

**THE LIMITATION WORTH KNOWING:** an NCX target is a file plus an optional fragment
(`ch3.xhtml#part2`) and the reader positions by spine entry only, so several entries
pointing into one file all land at that file's start. They are kept rather than
merged — their labels are real content — but selecting one is approximate. That is why
Le Fléau has 96 entries for 92 spine entries.

**It re-opens the archive**, deliberately: `OpenedBook` holds twelve bytes a spine entry
and no hrefs, and matching an NCX target to a spine index needs the real paths on both
sides. One central-directory parse and one OPF inflate (~32 KB transient) when Contents
opens, not when a book does. Measured 0.2–0.6 ms on the desktop for 28–96 entries, and
labels total **1,161 bytes for 96 entries** (mean 12.1), so the resident cost is small.

## The reader's menu and the chapter list

`ReaderMenu.dc.html` opens on the page's Activate, and its Contents row opens
`Contents.dc.html`. Between them they are the "go to chapter" the roadmap lists as
`contents`.

**THE MENU IS ASSEMBLY, NOT NEW GEOMETRY.** `components.h` already listed ReaderMenu
among the eight boards sharing the overlay panel box, `kActionsPanelW` is the same 340,
and `drawPanelRow` was already "72 tall, inset on a panel's own 20px padding, discloses
with a chevron". The only thing the menu added to the primitives is a row that states a
VALUE — its `Bookmarks` count — which is the other half of Home's "a row states a
quantity or discloses a screen, never both".

**IT DECLARES `Mono` WHERE THE READER DECLARES `Grayscale`.** Fidelity comes from the
top screen, so the menu paints in one waveform instead of three and its focus moves are
eligible for the overlay-only partial repaint (grayscale never is). The page under the
veil is hard-thresholded for those frames — the trade, and acceptable because the menu
is chrome and the page is the one thing here that wanted four levels. Its
`paintFootprint` is a constant, unlike the actions panel's: all five rows are one
height, so the panel cannot change height when the focus moves and every move takes the
fast path.

**`discloses` CANNOT BE DERIVED FROM AN EMPTY VALUE**, and deriving it drew a chevron on
`Close book` promising a screen that does not exist — that row had neither a value nor a
mark, because it acted in place. So `ListRow` carries the flag explicitly, as
`ItemActionEntry` already did. `Bookmarks` is the surviving instance of the same rule
from the other side: a value where its siblings have marks. Both fixes took the menu from
3.24% to **3.02%** against its board.

**`ListRow::trackingEm1000` NOW HAS NO PRODUCER.** `Close book` was `0.06em` where its
siblings were untracked — 1.5px a gap at Value500, ~15px across that label — and it was
the only letter-spaced row on any panel in this firmware. With the row cut (2026-08-24)
the field, `drawPanelRow`'s `labelTrackingEm1000` and the `trackingEm` call it guards are
**untested capability rather than working behaviour**. Kept because it is a generic
component parameter a board can ask for again; a test asserts every row is `0` so this
stays a stated fact rather than an assumption.

**A MERGE CHANGED THIS BOARD UNDER THE SCREEN, and `make compare` said "firmware ok"
the whole time.** Another branch (`claude/book-character-identification`) added a `Names`
row — its own boarded character index — so the board had SEVEN rows against this screen's
six. The comparison sheet reported it as fine because "ok" means the simulator produced a
frame, not that the frame matches: measured per pixel it was **13.02%** against 3.02%
before the merge. **The percentage is the check; the word is not.**

Two board inconsistencies surfaced with it, both about the page UNDER the veil, which IS
the reader's page — so `ReaderMenu.dc.html` and `Reader.dc.html` have to agree about it.
The menu board still drew a **drop cap** that `Reader.dc.html` drops from V1 with a long
mechanism note, and still said `CH. 01` where the Reader's slot had become a chapter
name. Both fixed on the board; the menu is back to 3.06% / 3.60%.

**`About this book` WAS INERT FOR A REASON THAT WAS FIXABLE.** Book details was built from
the LIBRARY's focused row, which is fine from the Library and wrong from a Reader: a
reader who arrived through Home's CONTINUE has no Library on the stack, so the factory
refused the push. Making the row focusable anyway would have been **a button that works
only sometimes** — worse than one that never does, because nobody can learn the rule.

So the screen takes **facts** (`BookDetailsScreen::Facts`) instead of a Library
reference. The Library answers them from a row and the Reader answers them from the book
it has open, and neither has to know how the other is shaped. Two details worth keeping:

- **The progress and chapter come from the READER, not the sidecar**, when the screen is
  opened from inside a book: the reader has moved since the last save, and a details
  screen opened mid-book should say where they *are*.
- **The Library path CLEARS the facts.** Without that, opening details from the Library
  after opening them from a book would show the book — a stale answer that looks like the
  right screen.

**THREE OF THE MENU'S FIVE ROWS DO NOTHING AND ARE DRAWN ANYWAY** — Settings' rule, and
the board was edited to match before the screen was written: it had focused Typography,
which is not built, so implementing it faithfully would have drawn a selection on a dead
row. `Contents` and `About this book` respond; Typography, Bookmarks and Names do not.

**TWO ROWS WERE CUT ENTIRELY (2026-08-24), NEITHER FOR ROOM.** `Go to page…` because
**nobody navigates an EPUB by page number**: a reflowable book has no stable page to go
to and the number a picker offers moves with the type size, so the honest jump is the
chapter name `Contents` already gives. (Its board and its roadmap entry went too; the
`Peek` spec listed it as one of three callers and now has two.) `Close book` because
**Back from the page already closes the book** — it was a second door to a room with
one, and it cost a fourth save edge to stay correct. Removing it deleted that edge, the
`popTo(Library)` it was the only user of on this screen, and the only producer of row
tracking in the firmware. The enum shrank with it: **a row index is not a stable
numbering** here, because the one thing that persists one is `FocusScreen`'s restore,
and that refuses an index it cannot land on — exactly what a shrunk table produces.

The menu measures **3.10% / 3.60%** against the board after the cut, against 3.06% /
3.60% before: the panel shrank consistently on both sides, so the residual is the same
rasteriser difference rather than new drift. **That the number barely moved is the
check** — a structural mismatch would have shown as a jump.

**THE ROW'S RIGHT SLOT HAS HELD TWO WRONG THINGS.** It was `P. 21`, a page number for a
place in the book, which needs every chapter paginated (~49 s). That became `CH. 01`, the
spine position — free, true, and WORSE on a real book: chapter names carry their own
numbering, so a row read `Chapitre 1.        CH. 09`, two numbering systems side by side
with neither explaining the other. It is `NOW` on the row being read and empty elsewhere:
the NAME is the content of a table of contents, and the full width belongs to it.

**THE LABEL ELIDES, AND `drawDetailRow` DID NOT.** It drew the label at full length from
the left margin, so a long one ran under the value and off the panel. Book details'
labels are field names and never overflowed, which is why it only surfaced when real
chapter names went through the same primitive. Fixed IN the primitive — a row that
overflows its own box is wrong on every screen that draws one.

The test for it first reported the FOCUSED row as an overflow: that row is full-bleed
inverted, so its fill legitimately inks both margins. `x=0` is the discriminator — a
full-bleed fill inks it and an overrunning label never reaches it, since every label
starts at `kMargin`.

**HELD UP OR DOWN SCROLLS**, `declareRepeat` on the two front movers as the Library does.
The SIDE buttons are movers now too, and a held one on a list still resolves as `Long`
and is dropped — worth deciding deliberately for both screens rather than changing one.

**CONTENTS IS SETTINGS' SHAPE**: a header band, a list interleaving section headers with
64px rows, a rail when it overflows, a hint bar. `drawDetailRow`'s own comment was
written anticipating it — "`focused` inverts it, which BookDetails never does and
Contents does on the chapter you are in". The section header turned out to be **byte
identical on both boards** (`--t-meta`, 0.2em/500, `padding: 18px 24px 6px 24px`, a 2px
`border-top` except the first), so it is `drawSectionHeader` now rather than a second
copy — and it returns the height it ACTUALLY drew, because a first header is shorter by
its missing rule and a caller advancing by the nominal height puts every row 2px low.
Settings shipped that exact bug once.

**A DEPTH-1 ENTRY IS A HEADER ONLY IN A BOOK THAT HAS DEEPER ONES.** Two of the four
measured books are flat, and treating depth 1 as a header unconditionally would render
one as nothing but headers — no focusable row, nothing to select. `sectioned()` decides
once, from the list. **A sectioned book therefore always has a focusable row by
construction**, since `sectioned()` requires a depth-2 entry and every such entry is a
row; the only nothing-to-select case is an empty contents. That invariant replaced a
test case written for a state that cannot exist.

**A SECTION HEADER IS ALSO A TARGET AND IS STILL NOT FOCUSABLE.** An NCX header carries
its own `content src`, so jumping to it would work — but the board draws it as a tracked
caps label with its own rule and no value, which is not a row a selection sits on. The
cost is one unreachable target per section, and its first child usually names the same
spine entry anyway.

**GO POPS TO THE READER; THE SHELL MOVES IT.** Contents cannot push a Reader — one is
already under the menu it was opened from, and a second would leave the first below with
its own position. So it answers `popTo(Reader)` and names the chapter, the shell reads
`chosenSpine()` **while Contents is still on top** (the dispatch pops it, and after that
there is no screen left to ask), and calls `ReaderScreen::goToChapter` once the Reader is
back. That lands on page ONE of the target rather than a saved position: a reader who
picked a chapter from a list asked for its beginning.

**THE TOC IS READ WHEN THE BOOK OPENS, AND IT HAD TO BE.** It was read on demand — one
archive re-open when Contents opened, to avoid a resident cost — and on the device that
could not allocate: `loadToc` needs a second `Inflater` (**36,956 bytes** of window and
tables) plus the zip's 121-entry directory and the epub's 92 chapters, about **48 KB**,
against a heap floor with a page on glass of **45,840**. It failed every time, returned
empty, and the factory substituted its demo — so Le Fléau showed Middlemarch's chapters.

**THIS FILE ALREADY HAD THE ANSWER**, under the eager page count: "counting on a second
`ChapterReader` would buy one pass for another 32 KB window against a 45,840-byte
floor". Same window, same floor, one screen later.

At OPEN there is room — `openBook` has released its archive and the Reader's own
inflater does not exist yet, so the heap is ~133 KB — and it is cheap to keep: **1,161
bytes of labels for a 96-entry book**, ~12 a row. So the shell reads it in `openBookAt`
and hands over a copy when Contents opens, with no card work on that press at all.

**AND THE FACTORY MUST NOT SUBSTITUTE.** `contentsToc_.empty() ? demoContents() : …` is
what turned a diagnosable allocation failure into a puzzle. The demo is asked for now
(`setContentsDemo()`, as `setReaderDemo()` is) and an unprimed Contents or reader menu
is **refused** — a refused push leaves the menu standing, which is wrong in a way the
reader can see through, and the log says why. `contentsPrimed_` is its own flag rather
than "the list is non-empty", because a real book with no NCX primes an EMPTY list and
must still build: it reads fine and simply cannot name its chapters.

**`readerBookTitle_` IS NEVER ASSIGNED** — a factory member read by two cases with no
setter anywhere, so Contents' band would have drawn an empty book name. The title comes
from `readerBook_.title`, which is the OPF's own and arrives with the spine.

`App::at(index)` exists because the menu is an overlay and the chapter it marks `NOW`
belongs to the Reader underneath: reached through the stack rather than remembered, since
a chapter crossing while the menu is closed would make a remembered one stale.

**AND TWO STALE DEAD BUTTONS WENT WITH THIS.** The Reader's Activate answered `none()`
behind "ReaderMenu is not built", which was true when written. The actions overlay's
`Open` row answered `none()` behind "the Reader is Phase 3, exactly as Confirm on a
Library row is" — and Confirm on a Library row opens a book, so that row had become a
dead button on a shipped screen while its test kept pinning the placeholder. Both are
live, and both tests now assert the action.

## Editing this repo with scripts

Most edits here are made by heredoc Python over the source. Three separate failures in
one session came from the SAME mistake in that method, and none of them announced
itself:

| what happened | the mechanism |
|---|---|
| CLAUDE.md committed as **0 bytes** | `open(p,'w').write(open(p).read()...)` — Python evaluates `open(p,'w')` first, truncating before the read |
| four TEST_CASEs silently deleted | a slice end found by scanning for a marker that also appears later |
| **`gApp->dispatch(ev)`** and four hooks deleted | `src.index(marker)` searching from the START of the file for a slice that began mid-file |

The third is the sharpest: the loop lost its dispatch, so every button on every screen
did nothing, and the firmware still built and every one of 803 desktop tests still
passed — `shell/` has no harness, so nothing on the desktop touches that loop.

**The rules, each earned:**

- **Read fully, mutate in memory, assert, write ONCE at the end.** Never call
  `open(p,'w')` in an expression that also reads the file.
- **Never compute a slice from `str.index` on a marker that is not unique.** Prefer
  exact-string `replace` of the whole region, with an `assert` that the region is
  present. If a slice is unavoidable, search for its end FROM the start index and
  assert the result is close to it.
- **Check the diff stat before committing.** A 128 KB deletion or a 102-line deletion
  is obvious in one line of `git diff --stat` and invisible in a script's success
  message. Every one of the three above would have been caught by looking.
- **A green suite is not evidence for a shell edit.** The desktop cannot see
  `shell/src/main.cpp`'s loop at all.
- **VERIFY THE WRITE LANDED, every time**, with a `grep` for a marker from the new text.
  A script with several `assert`s writes ONCE at the end, so a later assert failing means
  NONE of the earlier edits were written — and if the next command in the chain is a
  `git commit`, it commits the code without the documentation. That happened twice: a
  save was documented that did not exist, and then this section's own edits were skipped
  while the commit describing them went through. Both times the assert failed for the
  dullest reason — the anchor text had already been edited by a previous commit, so it no
  longer matched what I remembered.
- **A SPECIMEN BOARD MUST NOT PUT A LINE ON THE WRAP BOUNDARY.** `ReaderList` measured
  7.63%/7.90% against 4.5% for its sibling, and the cause was one list item: "Space is
  measured in rows." is **408px against a 406px measure**. Two separate faults sat on
  top of each other. First the board's marker: `padding-left: 38px; text-indent: -38px`
  is the idiomatic CSS and is WRONG by 10px, because it puts the dash and its spaces in
  the TEXT and pulls the first line back by the full 38 -- so the first line gets 416px
  and every line after it 406. The firmware gives every line 406 and draws the marker in
  the gutter, which is what a hanging indent means. Fixing that left the item still
  wrapping differently, because **408px is the FIRMWARE's number**: a board is
  rasterised by Chrome from the real webfont and the device uses the prepped TTF with a
  `kern` table `ttfprep.py` synthesised, so a string within half a percent of the measure
  lands on opposite sides of the break and NO fidelity work closes that. The copy was
  moved to 382px, 24px of clearance.
- **MY OWN NOTE WAS THE FIRST MATCH.** Changing that copy with `replace(old, new, 1)`
  hit the explanatory comment I had just written -- which quoted the string -- and left
  the row untouched, so the board rendered the old text while the note misquoted itself.
  The rule already here ("an anchor is not what you remember writing") now has a second
  form: **when a note quotes the string you are replacing, the note is an occurrence.**
- **THE READER'S MISMATCH IS NOT COMPARABLE TO THE MENU'S.** The menu is 1-bit, so
  counting pixels either side of a threshold is exact and 3.06% means 3.06%. The reader
  declares `Fidelity::Grayscale`, so a threshold-at-128 count over four levels inflates
  the figure -- `reader` itself measures **5.34%/6.38%** that way. Comparing a grayscale
  screen's number against a 1-bit screen's is how a healthy screen gets chased as a
  regression. Compare like with like: `reader_chapter_open` is 4.53%/4.39% and
  `reader_list` 5.20%/6.60%, against `reader`'s 5.34%/6.38%.
- **ONE BOARD CANNOT STATE BOTH GEOMETRIES' PAGE COUNT.** The X3's column is 492px
  against the X4's 444, so a specimen paginating to two pages on one panel makes one on
  the other and the footer reads `1 / 2` at 50% against `1 / 1` at 100%. The boards state
  the X4's, the narrower panel that fails first, and say so. Measured: the footer band
  matches BETTER than the text column, so this is not what the percentage is made of.
- **A CASE'S EARLY-RETURN GUARD IS PART OF THE CHANGE.** `ScreenId::BookDetails` opened
  with `if (library_ == nullptr) return nullptr;` — true while the screen was built from
  a Library reference. The change that removed that requirement replaced the `return`
  and left the GUARD, so `About this book` still did nothing from a Reader opened
  through Home's CONTINUE: refused before the facts were consulted, which is exactly the
  case it was written to fix. **The duplicated guard two lines below was the visible
  tell** — when a replacement leaves a condition stated twice, one of them is stale.
  Every test passed before and after, because every one of them had a Library.
- **AN ANCHOR IS NOT WHAT YOU REMEMBER WRITING.** Read the target region first. This file
  is edited constantly; a paragraph tracked its own subject through three states in one
  session, and each rewrite invalidated the anchor the next one guessed at.

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
