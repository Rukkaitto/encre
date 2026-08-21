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
- `~/encre-device-backup/restore.sh` restores the device to CrossInk.

## Rendering model

The panel shows **four grey levels** from two bit-planes the controller
combines. There is **no 2 bpp framebuffer** — it would not fit. Instead the
screen is drawn **three times** into a 1-bit buffer:

| Pass | Emits | Consumed by |
|---|---|---|
| `Plane::Bw` | ink where coverage ≥ 2 | `displayGrayscaleBase` |
| `Plane::Lsb` | bit 0 of coverage | `copyGrayscaleLsbBuffers` |
| `Plane::Msb` | bit 1 of coverage | `copyGrayscaleMsbBuffers` |

**Rules, fills and dither** have coverage 0 or 3 and so are identical in all
three passes; that is what makes a plane bug show up as fringing rather than
missing furniture, and `test_components.cpp` pins it for `drawRow`'s hairline
and the header band's rule.

**Icons are not in that set.** All ten shipped marks are 2 bpp
(`core/src/icons.cpp`) because they are generated anti-aliased from the boards,
so they legitimately carry grey at their edges and differ between planes exactly
as glyphs do. `Icon::bpp == 1` is the opt-in for a mark that wants hard 1-bit
edges, and nothing uses it. So grey on an icon edge is correct; grey on a rule,
a fill or a dither cell is a plane bug.

**Chrome must be anti-aliased.** 1-bit thresholding is what made small type
illegible on the panel; it is not a size problem.

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
- **Screens declare a `Fidelity`, and chrome is always `Gray`.** A 1-bit fast
  refresh of chrome is illegible on this glass, so `mode=FAST` in a `[paint]` log
  line beside `fidelity=gray` is not a contradiction — it means the cadence had a
  fast slot the screen could not use. `Mono` is the Reader's path and the input
  monitor's.
- **`core/include/reader/screens.h` is the one screen catalogue**, shared by the
  simulator and the shell. Two factories would drift, and the drift would be
  invisible because each half keeps passing its own checks.
- **Deep sleep is a chip reset**, so RAM state is lost and the firmware boots
  into Home. Wake is the **power button only**: the six front buttons are
  ADC-ladder bands on GPIO 1/2 and produce no GPIO edge, while power is a real
  GPIO (3, active-LOW). Sleep order is `display.deepSleep()` →
  `PowerManager::powerDownRailsForSleep()` → `deepSleepUntilPowerButton()`. That
  middle call does cut the X3's SD rail (the profile declares
  `sd.powerEnable = 13`), despite the SDK header calling it a no-op on X3/X4.

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

## Goldens

`test/golden/*.png` are human-approved, pixel-exact baselines. A golden test
that fails writes `build/<name>_candidate.png` and names both paths.

**Never re-bless a golden to make a test pass.** A failure means either an
intended visual change (inspect the candidate, confirm it is right, then bless)
or a regression (find the bug). Blessing to silence a red test destroys the only
protection the rendering has. `text_sample.png` is Literata body text: if it
changes and you did not mean to touch body rendering, stop.

## Where to look

`docs/superpowers/plans/2026-08-20-v1-roadmap.md` — phases, and two sections
worth reading before starting anything: "What Phase 2A-2 established" and the
on-device bring-up findings.
