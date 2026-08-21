# Phase 3A — Reader Foundations Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Free the memory the reader will need, stop paying for a framebuffer
twice, teach the filesystem to stream, and give body text a face that can be any
size — so 3B and 3C can be about EPUBs and typesetting rather than plumbing.

**Architecture:** Four independent foundations, in dependency order. Nothing here
renders a new screen; the fifteen goldens must not move. The one architectural
change is that `drawText` starts taking an interface rather than a concrete
`Font`, so chrome's pre-rendered ramp and the reader's scalable face share one
text path.

**Tech Stack:** C++20, doctest, CMake (desktop) / PlatformIO + Arduino-ESP32,
`stb_truetype` (vendored).

---

## Why these four, and why now

Phase 3's reader is ours to write (spec §3.1b). Three of these four are debts
that make that harder, and all three were found by the spike that led to the
decision — they are worth paying off whatever the reader turns out to be:

- **99,008 bytes of heap** go on two `std::unordered_map`s per face
  (`font.h:64-65`) indexing 20-byte records whose bitmap payload is already in
  memory-mapped flash. Measured: free heap 229,980 before the ramp loads,
  130,972 after. Pagination will want 100 KB+; this is where it comes from.
- **Two full framebuffers are live.** `FreeInkDisplay::begin()` allocates its own
  52,272-byte frame, and `setFramebuffer()` memcpys ours into it every paint.
- **`FileSystem` cannot stream.** 2C-1 left that gap deliberately and named Phase
  3 as its owner: an EPUB is megabytes against ~71 KB of free heap, so `readAll`
  is not the EPUB path.

The fourth is the decision this phase exists to make real: **book CSS asks for an
unbounded set of type sizes** (measured: 16, 32, 41, 51, 64 px, differing per
book), so a pre-rendered ramp cannot serve body text.

## Design decisions this plan locks in

### 1. Body text rasterises at runtime; chrome keeps its ramp

A pre-rendered `.rfnt` per size × weight × style is unbounded, and snapping a
publisher's heading to the nearest built size draws it at the wrong size inside a
line box reserved for the right one. So body text comes from a TTF rasterised on
demand, and **chrome keeps the pre-rendered ramp**, where the boards are
pixel-exact and the fifteen goldens live.

`stb_truetype` — public domain / MIT dual, single header, already vendored inside
`freeink-sdk/libs/book/FreeInkBook/third_party/stb/`. **Copy it into our
`third_party/`** beside `stb_image.h`; never reach into the submodule's tree,
which is a build-order dependency on someone else's layout.

The TTF stays in memory-mapped flash as a `const` array, so the font data costs
**zero RAM** — only the glyph cache does.

### 2. One text path, so kerning and planes cannot drift

`drawText` today takes `const Font&`. Body text needs the same tracking,
the same `Ink`, the same `Plane`/dither handling and the same kerning walk — so it
must go through the same function. Introduce a small interface (`GlyphSource` or
similar) that both `Font` and the new scalable face satisfy, and have `drawText`,
`measure` and the wrapping helpers take it.

**Two paths would drift.** Every fidelity fix in this project landed in
`text.cpp`/`components.cpp`; a second private copy for body text inherits none of
them and none of the future ones.

**But measure the cost before accepting it.** Virtual dispatch per glyph on a
160 MHz core is not free, and chrome repaints are already the thing the user
notices. Benchmark a chrome render before and after on the desktop; if the
regression is material, say so and propose the alternative (a template, or
`Font` staying concrete with the interface only on the reader's path) rather than
shipping a slowdown to satisfy a tidiness argument.

### 3. `measure()` must never rasterise

A scalable face rasterising during measurement would make laying out a paragraph
cost as much as drawing it, and `measure` is called far more often than
`drawText` — every wrap decision, every ellipsis, every centring.

So **advances come from the font's metrics, separately from bitmaps.** `stb_truetype`
reads advances from `hmtx` without touching an outline. Keep `advance(cp)` and
`glyph(cp)` distinct on the interface, and make it impossible for a measurement
walk to populate the bitmap cache.

This also settles the cache's shape: it only ever has to hold the glyphs of one
*drawing* pass, not one measuring pass.

### 4. The glyph cache is arena-backed, and eviction is a status not a crash

The device build is `-fno-exceptions`, so a container that cannot allocate calls
`abort()` with no diagnostic. The cache therefore gets a caller-supplied budget
and evicts within it — never grows to fit. A full cache must degrade to "slower"
and never to "dead".

Size it small to start (a few KB) and make it a constructor argument, so 3C can
tune it against measured page-render times rather than a guess made now.

### 5. Reclaiming the font maps must not change one rendered pixel

`Font::glyph()` becomes a binary search over the blob's glyph records instead of a
hash lookup. **Verify the records are actually sorted by codepoint** — the spike
asserted it from reading `fontc.py`, which is not the same as checking the bytes.
If they are not sorted, sort them in the generator and regenerate, which is a
change to eleven committed assets and needs its own careful pass.

The fifteen goldens are the proof. If one moves, the lookup is wrong.

## File structure

| File | Change |
|---|---|
| `core/include/reader/font.h`, `core/src/font.cpp` | `unordered_map` → sorted-record search; extract the interface |
| `core/include/reader/glyphsource.h` | the interface both faces satisfy |
| `core/include/reader/scalablefont.h` + `core/src/scalablefont.cpp` | `stb_truetype` face + glyph cache |
| `third_party/stb_truetype.h` | vendored copy |
| `core/include/reader/framebuffer.h`, `core/src/framebuffer.cpp` | a constructor viewing external memory |
| `core/include/reader/filesystem.h` | a streaming read handle |
| `core/include/reader/fs_contract.h` | contract clauses for it, so the device self-test covers it |
| `core/src/host_fs.cpp`, `test/unit/fake_fs.h`, `shell/src/sd_fs.cpp` | implement the handle |
| `core/src/text.cpp`, `components.cpp`, `theme_quiet.cpp` | take the interface |
| `shell/src/main.cpp` | render into the driver's frame; drop ours |
| `assets/fonts/`, `Makefile` | a body TTF as an embedded array |

> **CMake uses `file(GLOB ...)`.** Re-run `cmake -S . -B build` after adding a
> source file; `make test` does it.

---

## Task 1: Reclaim the font maps

**Files:** `core/include/reader/font.h`, `core/src/font.cpp`; test `test/unit/test_font.cpp`, `test_font_load.cpp`.

- [ ] **Step 1: Verify the records are sorted.** Write a throwaway check over all
  eleven committed `.rfnt` assets asserting codepoints ascend. **Report the
  result** — if any is unsorted, stop and say so; sorting means changing
  `fontc.py` and regenerating committed assets, which is a separate task.
- [ ] **Step 2: Write the failing test.** `glyph()` returns the same record for
  every codepoint in the asset as the current implementation does, and `nullptr`
  for absent ones including boundary cases: below the first, above the last,
  and gaps in the middle. Add `kerning()` equivalently — it is a second map.
- [ ] **Step 3: Replace both maps with a binary search** over the blob. Zero
  copies, zero nodes: the `Glyph` returned points into the blob exactly as it does
  now. `FontSet::load`'s validation must be unchanged.
- [ ] **Step 4: Prove it.** `make test` green, **all fifteen goldens byte-identical**,
  `make firmware` builds. Then benchmark `measure()` on a realistic string: a
  binary search over ~200 records is ~8 comparisons against a hash lookup, so a
  small regression is expected and acceptable — **report the number.** If it is
  large, an indexed direct lookup for the ASCII range is the fallback.
- [ ] **Step 5: Commit.**

---

## Task 2: One framebuffer

**Files:** `core/include/reader/framebuffer.h`, `core/src/framebuffer.cpp`, `shell/src/main.cpp`; test `test/unit/test_framebuffer.cpp`.

- [ ] **Step 1: A constructor that views external memory** — `(uint8_t* data, int
  width, int height, Rotation)`. It must not own or free the pointer, and
  `sizeBytes()` must describe the same layout the owning constructor produces, or
  the driver's memcpy contract breaks.
- [ ] **Step 2: Tests.** A viewing framebuffer and an owning one of the same
  geometry produce **byte-identical** buffers for the same drawing calls, under
  both rotations. A view over a too-small buffer must be refused or clamped —
  decide, pin it, and note that this is the one place a wrong answer corrupts
  memory outside the buffer rather than drawing wrongly.
- [ ] **Step 3: The shell renders into the driver's frame.** `display.getFrameBuffer()`
  after `begin()`. Remove `gFrame` and the `setFramebuffer()` memcpy from the
  paint path.

  **Two hazards, both real:** `getFrameBuffer()` returns null while
  `lendBuildStorage()` is outstanding, and the grayscale path calls
  `setFramebuffer()` as part of its own sequence — read `paintGray` carefully and
  keep every comment in it. If the grayscale path cannot be made safe this way,
  leave it using an owned frame and say so; it has no caller today.
- [ ] **Step 4: Measure.** Report the freed heap from a device-shaped calculation
  and the `copy=` field from the `[paint]` log, which should go to zero. Goldens
  unchanged.
- [ ] **Step 5: Commit.**

---

## Task 3: A streaming read handle

**Files:** `core/include/reader/filesystem.h`, `fs_contract.h`, `core/src/host_fs.cpp`, `test/unit/fake_fs.h`, `shell/src/sd_fs.{h,cpp}`, `shell/src/sd_selftest.cpp`.

2C-1's interface note says why this was left out and that Phase 3 owns it: an
EPUB is megabytes against ~71 KB of heap, so `readAll` is not the EPUB path.

- [ ] **Step 1: Design the handle.** Something like `openRead(path) ->
  std::unique_ptr<FileHandle>` with `read(void*, size_t) -> size_t`,
  `seek(uint32_t)`, `size()`, and `position()`. Random access matters: a zip
  central directory is at the *end* of the file, so a forward-only stream cannot
  read an EPUB at all.
- [ ] **Step 2: Contract clauses in `fs_contract.h`**, so the same cases run
  against the fake, the host implementation **and the real card** via
  `sd_selftest.cpp`. That seam already caught a `mkdirs` bug nothing else could
  see; the device implementation is again the one nothing else will check.

  Clauses worth having: a full read matches `readAll`; a partial read returns what
  it got and advances; reading past the end returns 0 and does not fail; `seek`
  beyond the end is refused or clamped (decide); interleaved seek/read lands at the
  right bytes; opening a directory fails; opening a missing file fails; a handle
  outlives nothing it shouldn't; and **two handles open at once** both work, since
  a zip reader will hold the archive open while reading an entry.
- [ ] **Step 3: Implement for all three.** On the device, mind that
  `SDCardManager` has no locking and the card shares the display's SPI bus, so
  every handle operation needs the `SpiBusGuard` — a read racing a panel refresh
  is the fault that looks random.
- [ ] **Step 4: Verify**, including a note in the report that the user should run
  `PLATFORMIO_BUILD_FLAGS="-DENCRE_FS_SELFTEST=1"` once to exercise the new
  clauses on the real card.
- [ ] **Step 5: Commit.**

---

## Task 4: A scalable face for body text

**Files:** `third_party/stb_truetype.h`, `core/include/reader/glyphsource.h`, `core/include/reader/scalablefont.{h,cpp}`, `core/src/text.cpp`, `components.cpp`, `theme_quiet.cpp`, `assets/fonts/`, `Makefile`; tests and one new golden.

- [ ] **Step 1: Vendor `stb_truetype.h`** into `third_party/`, copied from the
  submodule. Note its provenance and licence in a comment. Confirm it compiles for
  the ESP32 target — it is a single header but it is not small, and it must not
  drag in `<math.h>` behaviour we cannot afford.
- [ ] **Step 2: Extract the interface.** `Font` implements it unchanged. `drawText`,
  `measure` and the wrap/elide helpers take the interface. **Benchmark a chrome
  render before and after** and report it — see Design decision 2, including the
  fallback if virtual dispatch costs too much.
- [ ] **Step 3: `ScalableFont`.** `init(ttfBytes, len, sizePx, cacheBudgetBytes)`;
  advances from metrics without rasterising; bitmaps rasterised on demand into the
  bounded cache; `coverage()` reporting the same 0–3 scale the rest of the
  renderer speaks, so `Plane` and the Bayer dither work unchanged.

  Tests: metrics match `stb_truetype`'s own for a known face and size; the same
  string measures identically twice (the cache does not perturb metrics); a cache
  too small to hold a string still draws it correctly, only slower; **advances
  never populate the bitmap cache** (assert on cache statistics, or the design
  decision is unenforced); and rendering the same glyph at two sizes gives
  different bitmaps.
- [ ] **Step 4: One golden for body text**, rendered through the shipping 1-bit
  dithered path at a realistic reading size, so 3C has a baseline to break. This
  is new rendering, so **inspect the candidate with the Read tool and report
  honestly what you see** before blessing. `text_sample.png` is the existing
  Literata golden on the `.rfnt` path and must not move.
- [ ] **Step 5: Commit.**

---

## Task 5: Verify, measure on device, document

- [ ] `make test`, `make sim`, `make firmware`, `make compare COMPARE_ARGS="--all"`
  — 6/28 implemented, all matching, fifteen goldens unchanged.
- [ ] **Add `ESP.getMinFreeHeap()` to the `[alive]` line.** There is no high-water
  instrumentation in the firmware today, and every memory claim in this plan and
  the next two wants it.
- [ ] `CLAUDE.md`: body text rasterises and chrome does not, and why; one text path
  through an interface; the glyph cache evicts rather than grows; the framebuffer is
  the driver's; the filesystem streams.
- [ ] Roadmap: 3A done, what 3B and 3C inherit, and the measured heap before and
  after so the next phase starts from a number rather than a hope.
- [ ] **Do not write the flash handoff into the docs** — report it, and name what
  only the panel can answer.
