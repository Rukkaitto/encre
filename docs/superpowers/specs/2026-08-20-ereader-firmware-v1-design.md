# E-Reader Firmware V1 — Design Spec

**Date:** 2026-08-20
**Status:** Approved design, pre-implementation
**Name:** Encre (French for "ink")
**Targets:** Xteink X4 and Xteink X3 (ESP32-C3)

## 1. Purpose

A from-scratch, MIT-clean e-reader firmware for the Xteink X4 and X3 whose entire
personality — UI, typography, and features — is under our control. V1 delivers:

- A beautiful, intuitive UI built around the "pick up and read" loop.
- Crisp 1-bit typography with real customization (font family, size, margins,
  line spacing, alignment).
- Effortless book loading: WiFi drag-and-drop in a browser, or plain SD copy.
- Formats: EPUB 2/3 and plain `.txt`.

Everything above the freeink-sdk drivers is new code. CrossPoint Reader
(MIT, https://github.com/crosspoint-reader/crosspoint-reader) is a reference
implementation we may consult, not a base we fork.

## 2. Hardware facts (verified against freeink-sdk / CrossPoint source)

| Fact | X4 | X3 |
|---|---|---|
| SoC | ESP32-C3 (single-core RISC-V, ~400KB RAM, 16MB flash) | same |
| Panel | 800×480, 1-bit monochrome | 792×528, 1-bit monochrome |
| Portrait canvas | 480×800 | 528×792 |
| Extra peripherals | — | BQ27220 fuel gauge, DS3231 RTC, QMI8658 IMU (I2C) |

- Input: 7 physical buttons — Back, Confirm, Left, Right, Up, Down, Power
  (freeink-sdk `InputManager` BTN_* indices 0–6). **No touchscreen.**
- Display over SPI with custom pins; SD card shares the SPI MISO line.
- No USB mass storage possible (ESP32-C3 has USB Serial/JTAG only) — hence
  WiFi upload as the primary loading path.
- E-ink refresh modes available from the driver: FULL (slow, cleans ghosting),
  HALF (~1.7s, balanced), FAST (custom LUT, quick page turns). Async
  (non-blocking) refresh is available on panels that support deferral.

## 3. Architecture

Three layers. The prime directive: **the core is hardware-free and compiles on
a desktop.** Only the shell touches Arduino APIs.

### 3.1 Drivers (freeink-sdk, MIT, reused as-is)

`EInkDisplay`, `InputManager`, `SDCardManager`, `BatteryMonitor`, `BoardConfig`,
consumed as PlatformIO library symlinks exactly as CrossPoint does. Board
variant (X4 vs X3) is detected at boot (I2C probe / BoardConfig) and exposed
through a single `Device` interface: panel dimensions, buttons, battery source,
RTC presence.

### 3.2 Portable core (hardware-free C++20, unit-tested on desktop)

- **Content pipeline:** EPUB container (miniz for zip, expat for OPF/XHTML) →
  a lean internal document model (block/inline tree supporting the HTML/CSS
  subset that matters for books: paragraphs, headings, emphasis, images, lists,
  blockquotes, basic alignment). TXT normalizes into the same model, so all
  formats get identical typography.
- **Layout engine:** paragraph shaping (kerning, soft hyphens + hyphenation
  patterns, justified/ragged) → pages as **display lists** (positioned glyph
  runs and images). Deterministic given (document, typography settings, canvas
  size).
- **Pagination cache:** serialized display lists on SD, keyed by hash of
  (content identity, font family, size, margins, line spacing, alignment,
  canvas). Cache hit → instant open; settings change → re-paginate in the
  background from the current position outward.
- **Font system:** compact bitmap font format (glyph bitmaps, metrics, kerning
  pairs, UTF-8 coverage). Generated offline by a desktop converter tool
  (FreeType-based, part of this repo) from TTF/OTF at a fixed size ramp.
  ~4 curated faces bundled in firmware; user fonts loaded from `/fonts` on SD.
- **State models:** global settings, per-book state (progress, bookmarks,
  typography overrides). Serialized as small JSON files (sidecars under a
  dot-directory on SD; WiFi credentials in ESP32 NVS, not on the removable
  card).

### 3.3 Device shell (thin Arduino/PlatformIO layer)

- **Screen stack + input dispatch:** push/pop screen manager; screens receive
  button events (with short/long press distinction) and draw into the shared
  1-bit framebuffer.
- **Theme layer:** themes own the *entire presentation*, layout structure
  included — not just colors and fonts. Each screen produces a **view-model**
  (semantic content plus interaction state, no geometry): for Home that is
  {current book, author, progress, chapter, menu entries with counts, focused
  element, battery/date, hint set}. The active theme renders the view-model
  however it likes — a cover-led centered column, a vertical spine band with
  a giant numeral, a terminal-style list — and screens never draw pixels
  directly. Two radically different structures (e.g. the "Quiet" default and
  the constructivist "Spine" exploration) are both just themes over the same
  view-models. V1 ships exactly one built-in theme (B2 "Quiet", from the
  design exploration) and no picker, but the screen/theme split is built this
  way from the first commit so themes are additive content, never a refactor.
  Fixed across all themes (ergonomics, not style): hints appear in hardware
  order (Back left, Confirm center, Up/Down right, one hint per button,
  evenly spaced), every screen renders a visible focus state, and the
  view-model's full content must be represented.
- **Refresh policy:** owned by the shell, not by individual screens — FAST for
  page turns, FULL every N page turns (default 15, configurable) and on every
  screen transition, to keep text crisp and ghost-free.
- **WiFi manager:** STA mode (join saved network) with AP-mode fallback
  (device hosts a hotspot; setup screen shows a QR code + URL). WiFi stays
  OFF except during transfer/sync/setup — battery first.
- **HTTP server:** serves the single-page drag-and-drop upload UI (uploads
  stream to `/books` on SD, with progress) and the small JSON API behind it.
- **Power manager:** idle → sleep timers; deep sleep with wake-on-button;
  paints the sleep screen before sleeping; forced clean shutdown at critical
  battery.

### 3.4 Desktop simulator

A native (macOS) build target that links the portable core plus the UI
rendering code against a fake device: it renders any screen to PNG at exact
panel resolution (480×800 / 528×792), driven by scripted button events.
Used for: fast UI iteration, visual-regression tests, and as pixel-exact
ground truth for the visual design pass (Claude Design handoff).

## 4. UX structure

### 4.0 Interaction model (fixed across themes)

- The hint bar always shows **exactly four slots**, one per front button, in
  hardware order: Back, Confirm, Up, Down. A button with no action leaves its
  slot empty (alignment is preserved). **The bar is always exactly one line
  tall.** A long-press variant is a hollow ring mark beside that button's label
  — never a second line and never a fifth hint. Two-line slots were tried and
  rejected: a bar whose height varies by screen also moves every list above it,
  and the word "HOLD" does not fit a four-slot bar at 10pt on the narrower X4
  canvas. Labels carry `white-space: nowrap`, so one that does not fit overflows
  visibly instead of silently reflowing the bar taller. Side buttons
  (Left/Right) turn pages in the Reader and get no on-screen hints.
- **Long-press Confirm on any list item = contextual actions overlay.**
  Library items: Open / Book details / Mark as finished / Delete… (delete has
  a confirmation step and never erases reading progress). Saved Wi-Fi
  networks: hold to forget.
- Actions that need more than a press get their own screen or list row, not a
  hint binding.

### 4.1 Screens

- **Home:** the current book front and center — cover, title, author,
  progress. Confirm resumes reading, and on Home the **Back button also
  opens the current book** ("Read") since there is nothing to go back to.
  Below: Library and Settings.
  - *Empty state (first run / no books):* a welcome screen naming the two
    loading paths (SD copy, Wi-Fi send) with "Send books over Wi-Fi" as the
    focused action; the Library row reads "empty".
  - *Missing current book:* if the last-read file is gone (card edited
    elsewhere), Home falls back to the most recent existing book and shows a
    one-time banner saying why; with no books left it falls back to the
    empty state. Progress/bookmarks of the missing book are retained in case
    the file returns.
- **Library:** file browser over `/books` (folders respected), recent-first
  option, dithered cover thumbnails where available, Confirm opens,
  long-press for item actions (see 4.0).
- **Reader:** full-bleed text. Left/Right (and Up/Down) turn pages. Confirm
  opens the reader menu overlay: chapters, typography panel, go to page,
  bookmarks, book info, exit. Back short-press closes any overlay; Back
  long-press exits straight to Home.
  The typography panel adjusts family / size / margins / line spacing /
  alignment with a live preview line and applies on close (background
  re-pagination from the current position).
- **Settings:** device (sleep timers, full-refresh cadence, button remap for
  page-turn direction), typography defaults, WiFi networks, sleep screen
  mode, about/version.
- **Transfer screen:** shown while the HTTP server runs — URL + QR code,
  upload progress, done/cancel.

### 4.1b Secondary screens (all designed in the canvas, "Flows & States" page)

Reader tools: Typography panel (live preview line, applies from the current
page), Contents (chapter list with current position), Go to page (Up/Down
with hold-to-accelerate), Bookmarks (hold to remove), About this book. **About
this book is a FIXED single-screen summary** — six fields, no list, nothing to
scroll — so its hint bar carries Back and three empty slots, per §4.0's rule that
a button with no action leaves its slot empty. Its board originally labelled Up
and Down; that was corrected (2026-08-21) after the screen was built and the
labels had nothing to move. If it ever needs to scroll, the answer is a scroll
window on the screen, not re-labelling the buttons.
Flows: item-actions overlays and delete confirmation,
Wi-Fi settings (saved networks, join, setup hotspot) and the on-demand
connect dialog ("Wi-Fi turns off when the transfer finishes" is stated in
the UI). Joining a network on-device: scan list (signal + lock indicators, open
networks join directly) → password entry on a button-driven keyboard
(Up/Down between rows, the side page buttons move along a row, Confirm
types, Back deletes / hold cancels; password visible while typing). The
setup hotspot (AP + QR + credentials + web page) is the no-typing
alternative for entering network details. Join failure offers edit / retry /
cancel and states Wi-Fi is off again. Error/empty: first-run Home, missing-book Home, no-SD-card screen,
corrupt-book dialog, low-battery banner (any button dismisses), the
battery-empty shutdown screen (page saved; charge to wake), the boot
splash, and the end-of-book screen (mark finished / back to library).
The web pages the device serves are designed too (canvas page "Web UI"):
the drag-and-drop upload page and the Setup page (Wi-Fi form). Documented
variants without their own board:
Library inside a folder (same layout, path in the header, Back exits the
folder), Transfer before any file arrives (progress block absent), the
sleep screen while charging (small charging glyph on the plaque), and the
connect dialog stepping its label Joining -> Ready. Settings displays Wi-Fi as **"on demand"** —
never "connected" — matching the Wi-Fi policy in 3.3.

### 4.1c Overlay and reading-surface conventions

- **Overlay panels are vertically centred** in the canvas, not positioned from
  the top. Hand-picked offsets put several of them close enough to the hint bar
  to read as crowded, and centring is geometry-independent so one board is
  correct at both 800 and 792 tall.
- **The reading surface uses minimal side margins** (18px). 40px each side cost
  80px of a 528px panel — 15% of the measure — on a device whose entire job is
  showing text. 18px clears the bezel while buying back 44px of line length.

### 4.2 Sleep screens

Deep-sleep image: current book cover (dithered, with optional
title/progress overlay) or user-supplied images from `/sleep` (rotating),
per setting. Falls back to a clean typographic default when neither exists.

## 5. Data layout (SD card)

```
/books/            user EPUBs and .txt (any folder structure)
/fonts/            user-installed converted fonts
/sleep/            optional user sleep-screen images
/.reader/cache/    pagination caches (evicted LRU when space is low)
/.reader/state/    per-book state JSON
/.reader/settings.json
```

NVS (on-chip): WiFi credentials, last-open pointer.

## 6. Error handling

- **No/failed SD card:** full-screen prompt with retry; device never boots
  into a broken UI.
- **Corrupt or unsupported book:** readable error naming the file; parser
  failures are contained (no crash, no reboot loop).
- **Network:** WiFi and transfer errors surface as non-blocking notices;
  reading never depends on the network.
- **Power:** progress saved on page turn (debounced write); low-battery
  warning at threshold; clean shutdown (state flushed, sleep screen painted)
  at critical level.
- **Cache integrity:** pagination cache entries are versioned + checksummed;
  a bad entry is discarded and rebuilt, never trusted.

## 7. Testing

- **Core unit tests** (native, run on macOS/CI): parsers, document model,
  layout/pagination (golden-file tests: input document + settings → expected
  page breaks and glyph positions), cache round-trip.
- **Visual regression:** simulator renders key screens to PNG; diffs against
  approved baselines.
- **On-device:** small manual smoke checklist per release (boot, open book,
  page turns, upload, sleep/wake) — kept small precisely because
  everything above the drivers is tested natively.

## 8. Out of scope for V1 (V2 shelf)

**Instapaper support** (sync the unread queue, read offline, archive/like
from the device) — cut from V1 on 2026-08-20 to keep the first release
achievable. Its UX is already designed end-to-end on the canvas page
"V2 · Instapaper" (Articles list and actions, not-set-up state, browser
sign-in flow, account screen, sync feedback), and the architecture leaves
room for it: the client is a portable-core module with an injected HTTP
transport, and Articles is one more view-model + theme rendering. Needs an
Instapaper API consumer key before work starts.

Also: OPDS browsing, Calibre wireless, dictionary lookups, reading stats,
OTA updates, KOReader sync, RTL/bidi text, tilt page-turn (X3 IMU), audio
anything. Also: the theme
picker and user-installable themes — the theme layer itself ships in V1
(Section 3.3) with the single built-in "Quiet" theme; alternate themes,
including structurally different ones (the constructivist "Spine"/F
direction, an inverted white-on-black theme riding the display driver's
polarity support), arrive as V2 content on top of it.

## 9. Design handoff notes (for the visual design pass)

Constraints the visual design must respect:

- **Canvas:** 480×800 (X4 portrait) and 528×792 (X3 portrait); design for X4
  first, X3 adapts via the same layout system.
- **1-bit monochrome:** pure black/white only. Grays exist solely via
  dithering/halftoning (used for covers and images). Hairlines and large
  solid-black fields are fine; anti-aliasing does not exist.
- **Input:** 7 buttons only (Back, Confirm, Left, Right, Up, Down, Power),
  short and long press. No touch, no scroll gestures — paged navigation and
  focus-based selection everywhere. Every screen needs an obvious focus state.
- **Refresh:** screen transitions cost a visible full refresh (~1s flash);
  design flows to minimize screen hops. Partial in-place updates (e.g. a
  progress bar) are cheap.
- **Typography is the brand:** the bundled font set and the reading screen
  are the product; chrome should recede.

## 10. Open items

- Final choice of the ~4 bundled font faces (licensing must permit embedding;
  candidates: Bookerly-alikes such as Literata, Source Serif, plus a
  humanist sans and a monospace).
- **A Settings toggle for the screen-transition full refresh.** V1 hardcodes it
  on: a screen change takes the panel's FULL (GC) waveform so the outgoing screen
  cannot ghost through, which is what Kindle and Kobo do and what the reference
  firmware notably does not — its settings screen ghosts visibly on this device.
  The cost is measured: ~825 ms for a transition against ~520 ms on the fast
  waveform, so a reader who would rather have quicker navigation than a clean
  slate has a real reason to want it off. Belongs with the other refresh and
  power settings (sleep timers, full-refresh cadence). **Needs a row on
  `design/Settings.dc.html` first** — per the design-first rule, the board gets
  the row before the implementation does.
