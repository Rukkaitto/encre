# Encre

From-scratch firmware for the Xteink X4 and X3 e-readers — ESP32-C3, e-ink,
~220 PPI. One binary drives both models; the hardware is identified at boot,
because the panel controller varies by production batch.

It exists because the stock firmware is not the reader we wanted. Design
fidelity is treated as a functional requirement here rather than as polish: the
`design/` boards are the source of truth for the UI, and a tool compares every
built screen against its board.

## Status

**V1 is card transfer only. Wi-Fi is cut** — books get onto the device by
putting the SD card in a computer. That is a scope decision, not a gap waiting
on a driver.

What is built and on glass: Home, Library, book details, the item-actions and
delete-confirm overlays, Settings, the typography panel, the reader (EPUB, with
justification, italics from the book's stylesheet, and a page ring), the table
of contents, the peek overlay, and the sleep screen — including the open book's
cover at four grey levels.

What is designed and **not** built: the low-battery banner, critical shutdown,
the corrupt-book dialog, and the end-of-book screen. Their boards exist; the
screens do not.

Known refusals, each stated rather than discovered:

- **Progressive JPEG covers** are refused. 2 of a 225-book corpus — but 2 of
  the author's own 16, so a real library meets this more often than the corpus
  suggests. The sleep screen falls back to the reading card.
- **A book whose metadata carries an attribute over 512 bytes is refused
  entirely.** Calibre writes these. [#35](https://github.com/Rukkaitto/encre/issues/35)
- **A single text block over 64 KB ends the chapter silently.**
  [#37](https://github.com/Rukkaitto/encre/issues/37)

There is no release yet. `docs/releasing.md` says what v0.1.0 is waiting on.

## Written with Claude Code

Effectively all of this firmware was written by Claude Code, directed and
reviewed by its owner. Most commits carry a `Co-authored-by: Claude` trailer,
and it was applied inconsistently — treat it as a floor rather than a measure.
`CLAUDE.md` is the project's working memory: what was measured, what was tried
and abandoned, and why. It is the most accurate document in the repo.

**What that means if you are going to run this on your own reader.** The checks
are real: unit tests, pixel-exact golden renders at both panel geometries, and a
design-versus-firmware comparison against every board in `design/`. They are
also not enough on their own — `shell/` has no test harness, and this repo has
more than once shipped something that passed every desktop test and was wrong on
the glass: a veil that smeared under rotation, an overlay painted onto white, a
function that could only recurse. That is what
`docs/on-device-smoke-checklist.md` exists for. Flash it expecting to find
things.

## Before you flash

**The device is recoverable.** There is no secure boot and no flash encryption,
so download mode is always available. Back the stock firmware up anyway, before
you write anything — it is a 16 MB read and it is the difference between a bad
afternoon and a dead reader:

    ~/.platformio/penv/bin/python -m esptool --port /dev/cu.usbmodemXXXX \
        read-flash 0 0x1000000 xteink-stock-backup.bin

Restoring is `write-flash 0 xteink-stock-backup.bin` with the same tool. Verify
the backup is 16 MB before you trust it. (esptool before v5 spells these
`read_flash` and `write_flash`.)

Flashing third-party firmware is your own risk. Nobody here has tested this on
every batch of either model, and the X4 in particular is the model this project
does not develop on — see **What is unverified** below.

## Getting the source

    git clone https://github.com/Rukkaitto/encre.git
    cd encre
    git submodule update --init

**The submodule step is not optional.** `freeink-sdk/` holds the MIT display,
input, SD and battery drivers. Without it `make firmware` fails with
`PackageException: not a directory`, which names neither the submodule nor the
fix. The desktop build is unaffected, so a checkout can look healthy and still
not build firmware.

## Building and testing on the desktop

    make test      # build core + run the unit and golden tests
    make sim       # render Home to build/home.png
    make compare   # design-vs-firmware contact sheet, all boards (~3 min)

`core/` is portable C++20 with no Arduino, ESP or host-OS dependency, so it
compiles for macOS and the ESP32 alike. The simulator renders any screen to a
PNG at exact panel size, which is where UI iteration happens.

`reader_sim <screen> out.png --canvas 528x792` renders one screen;
`--bench 200` reports what a render pass costs.

CMake globs its sources, so **re-run `cmake -S . -B build` after adding or
removing a file** or it is silently ignored.

## Building and flashing the firmware

    make firmware

PlatformIO installs outside `PATH`, so the Makefile invokes it through
`~/.platformio/penv/bin/python -m platformio`. Override with
`make firmware PIO=/path/to/pio` if yours lives elsewhere.

To flash, find the port and upload:

    set -- /dev/cu.usbmodem*; echo "$1"

    ~/.platformio/penv/bin/python -m platformio run -e xteink -t upload \
        --upload-port /dev/cu.usbmodem1101

Two things that will otherwise waste an afternoon:

- **`Failed to install Python dependencies into penv` is transient — retry it.**
  The check lives in the platform's builder script and runs `uv pip install
  --upgrade` on every build that has a network, so it can fail on a PyPI hiccup
  or when two builds share the `uv` cache. Do not run two builds at once.
- **E-ink holds its last image with no power, so a frozen screen is not evidence
  that the firmware ran.** Nothing clears the glass at boot. Read the serial log
  before concluding anything from the panel.

## Putting books on it

The card is FAT or exFAT. Books go in `/books` as EPUB — flat or in folders,
both are listed. That is the whole transfer story in V1.

The firmware writes two things of its own:

- `/.reader/settings.json` — created with defaults on the first boot that finds
  no file, and hand-editable. A corrupt or wrong-version file is left exactly as
  you typed it and defaults are used; a single out-of-range value is clamped and
  the rest of the file still loads. The boot log distinguishes those two cases.
- `/.reader/state/` — one small JSON per started book holding the reading
  position, plus `last.json` naming the book last open. Deleting a book from the
  Library never erases its progress.

## Watching what it does

    ~/.platformio/penv/bin/python tools/serial-log.py --seconds 20

    ~/.platformio/penv/bin/python -m platformio device monitor -e xteink | tee run.log
    python3 tools/latency.py run.log      # what each interaction cost, by press

The firmware prints one `[i]` line per interaction, from the button going down
to the panel being finished with it, and `tools/latency.py` groups those by
where the press landed and reports medians.

**Every timing taken over USB is inflated by the cable.** The USB CDC write
blocks until the host takes the bytes, and unplugged it short-circuits and costs
microseconds — which is the device's real behaviour, since it lives on battery.
Each `[i]` line carries `ser=` for exactly this reason; `net = total − ser` is
the number to compare across runs.

A log on the card exists for faults that only happen unplugged, but it is
currently unreachable from the settings file —
[#47](https://github.com/Rukkaitto/encre/issues/47).

## What is unverified

- **The X4 is not the development device.** All device measurements in this repo
  were taken on an X3 with a UC8279 controller. Rotation is measured as
  counter-clockwise on the X3 and unverified on the X4
  ([#23](https://github.com/Rukkaitto/encre/issues/23)); if the X4 renders 180°
  out, that is the reason.
- **Two cover behaviours have not been seen on glass**: the one-bit cover the
  *wake* paints, and whether the sleep badge slicing a cover's own title band is
  tolerable. Most covers set type exactly where the badge sits.

The goldens are *not* on this list: they were blessed on macOS/clang, and CI
re-runs them on Linux/gcc on every push, so the rasteriser agrees across both
toolchains.

## Repo layout

| Path | What it is |
|---|---|
| `core/` | Portable C++20, namespace `reader::`. Framebuffer, fonts, text, icons, dither, layout, view-models, themes. No Arduino, ESP or host-OS dependency. |
| `sim/` | Desktop simulator — renders a screen to PNG at exact panel size. |
| `shell/` | The Arduino layer: device detection, display bring-up, the paint sequence. The only place that touches `freeink-sdk`. |
| `tools/` | Asset generators, the design comparison tool, and the device log readers. |
| `design/` | `*.dc.html` design boards — **the source of truth for the UI**. |
| `docs/` | The spec, the roadmap, per-phase plans, the smoke checklist, the release procedure. |
| `freeink-sdk/` | Submodule. MIT drivers for display, input, SD and battery. Never edited here. |

## Working on it

**A UI change goes into the design HTML first, then the implementation.** Never
only in code, and not the other way round — including when the design itself is
what is wrong: fix the board, then follow it. `make compare` is what keeps the
two honest, and changing only the implementation silently invalidates it.

**Never re-bless a golden to make a test pass.** A failing golden writes
`build/<name>_candidate.png` precisely so the pixels can be looked at, which is
the only way to tell an intended change from a regression.

Branch names and commit subjects are enforced on pull requests. Install the
local mirror of that check so you learn about it before pushing rather than
after:

    make hooks         # commit-msg + pre-push, both bypassable with --no-verify
    make conventions   # run the same check by hand

[Graft](https://github.com/trailhq/Graft) indexes this repo for coding agents.
The graph is a local cache like `build/`, gitignored and regenerable; what is
committed is the wiring in `.claude/`. On a fresh clone:

    npm install -g @nanonets/graft
    graft build

**One of its six tools does not work here, and it is the one you would want
most.** `graft callers <symbol>` answers nothing across files on this tree: a C++
free function is declared in a header and defined in a `.cpp`, which makes the
name ambiguous, and graft drops an ambiguous cross-file edge rather than guessing
at it. Same-file edges are fine. So `callers` cannot give you a blast radius
before a rename here — use `graft grep`, which is exhaustive and groups hits by
the enclosing symbol. Measured against `drawBadge`, whose two call sites
`CLAUDE.md` names: `grep` found both, `callers` found neither.

**It says so rather than reporting a clean zero**, which is the only reason it is
worth having: every such answer names the ambiguity and points at `graft grep`.
An instrument that reports on less than it claims is worse than none, and this
one does not.

`graft build --lsp` is the documented fix for exactly this and **does not help**,
so it is not worth rediscovering: `clangd` is on `PATH` and a
`compile_commands.json` for the desktop build comes from
`cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, and the edges were
unchanged either way. The ambiguity rule sits above the LSP layer. `shell/` has
no desktop compile database at all, so nothing there could have been covered.

What does work is the rest of it — `graft ask "<question>" --source` locates a
flow and inlines the code, `graft skeleton <file>` gives a file's API, `graft
map` orients. Telemetry is anonymous and on by default, a machine-level setting
rather than anything committed here; `graft telemetry disable` turns it off.

Where to read next: `CLAUDE.md` for how the thing actually behaves and why,
`docs/superpowers/plans/2026-08-20-v1-roadmap.md` for the phases, and
`docs/superpowers/specs/2026-08-20-ereader-firmware-v1-design.md` for the spec.
Deferred work lives on [the project board](https://github.com/users/Rukkaitto/projects/1),
which is the only index of it.
