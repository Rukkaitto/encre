# Codebase review brief — 2026-08-22, end of Phase 3A

For a reviewer arriving cold. ~13,600 lines across `core/`, `shell/`, `sim/`;
36 test files; 9 suites, all passing; working tree clean.

## Read these two first

`CLAUDE.md` and `docs/superpowers/plans/2026-08-20-v1-roadmap.md`. They are not
courtesy reading — most of what looks wrong here is decided, measured and
recorded, and a review that re-derives those decisions spends itself on settled
ground. **The single most useful thing a fresh reader can do is find what those
documents are WRONG about.** Both have been wrong before: `CLAUDE.md` claimed
icons were coverage-0-or-3 when all ten carry grey, and a plan claimed twelve
assets where there were eleven.

## Where the actual risk is

**`shell/` has no test harness.** `core/` is portable C++20 with 36 test files, a
golden-image suite and a design-comparison tool; `shell/` is Arduino code that
nothing checks except by flashing it. Four bugs have hidden there, each passing
every desktop check:

- `paintPlane` called `top().render()` instead of `App::render()` — would paint
  overlays on white. No desktop test *could* catch it: the simulator and every
  golden go through `App::render`.
- `libraryVisibleRows()` got the native landscape height, so Library showed 4
  rows instead of 7. Found by reading a device log.
- The session record stored a raw `ScreenId` ordinal; inserting screens
  repointed `Settings` at `ItemActions`.
- A `wipeScratch` calling `removeDir` on a stale file, which I then misdiagnosed
  from its own diagnostic output.

The pattern is always the same: state that only exists on the device (the frame,
the panel's geometry, NVS, the card) reached through code that no test observes.
**Look for the fifth one.** `shell/src/main.cpp` is the largest file and the one
worth the most attention.

## Deliberate, with reasons recorded — please don't re-litigate

Each of these looks like a defect and is a decision with a measurement behind it.
Challenge them if the *reasoning* is wrong, not because the shape is unusual.

- **The EPUB engine is ours, not `FreeInkBook`'s** — a complete MIT engine in a
  submodule we already ship, evaluated and rejected on control of the reading
  surface. Spec §3.1b.
- **Chrome is hard 1-bit** (`Fidelity::Mono`), not anti-aliased, and thresholding
  measurably *improved* the small round icons. `Dithered` is implemented, tested
  and kept, not deprecated.
- **A screen transition forces a FULL refresh**; a focus move does not. This is
  deliberately not the reference firmware's behaviour.
- **Three dither patterns, not one**, with opposite arrangements for opposite
  jobs (tints / glyph edges / the overlay veil).
- **`veilRect` is the one routine in `core/` that knows `Rotation` exists.** It
  is byte-wise and 13.6x faster; under CCW a logical row is a physical column.
- **The path normaliser exists in three copies**, one per `FileSystem`
  implementation, in three build worlds.
- **`stb_truetype.h` is vendored unmodified** with a sha256, including the
  `count = (size < 32 ? 2000 : ...)` line that mallocs 56,004 bytes per glyph.
  Decided yesterday; the roadmap has the numbers.
- **Kern records and advances are whole pixels.** Known, quantified (~0.6px
  pairs each rounding to −1), deferred because the fix re-blesses every golden.

## What I would genuinely like fresh eyes on

Ranked by how much a second opinion is worth, not by severity.

1. **The SPI bus discipline.** The SD card shares the display's bus and
   `SDCardManager` does no locking. Every `SdFileSystem` method takes a recursive
   `SpiBusGuard`, so does every `FileHandle` operation, and so does the whole
   paint. Is there a path that touches the card without it? `sd_selftest.cpp`,
   `main.cpp` and `sd_fs.cpp` are the three files that reach the card.
2. **`input_task.cpp` is the only real concurrency boundary** — its own FreeRTOS
   task at priority 2, queueing both button edges with timestamps to the loop.
   Only that task may call `update()`; it owns the edge state. Worth checking
   that claim holds rather than trusting it.
3. **Lifetime of borrowed pixels.** `Glyph::bitmap` is borrowed and valid only
   until the next call into the same `GlyphSource` — a ring-arena cache can
   evict it. Does any caller hold one across a second lookup?
4. **`-fno-exceptions` makes a failed allocation an `abort()` with no
   diagnostic.** Only two `resize` sites and both are bounded, so the obvious
   risk is handled; the ~39 growth sites overall are worth a skim for one driven
   by file content rather than by a list length.
5. **Known-unbounded, all real:** `drawDetailRow`'s value (a Location path),
   `countLibrary` at boot, `elideToWidth` cutting codepoints rather than grapheme
   clusters. Cheap to confirm, and I would rather have them ranked by someone who
   has not been staring at them.
6. **The partial-repaint precondition.** `canRenderTopOnly` refuses unless frame,
   plane, top screen and depth all match what `App` recorded — because the caller
   is the only thing that could have clobbered the frame. `ItemActions`'
   footprint is non-constant over a one-pixel panel height change. This is the
   subtlest logic in `core/` and the place I would most expect to be wrong.

## Verification a reviewer can actually run

```bash
make test                                    # 9 suites, ~3.5s
cmake -S . -B build-asan -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined" \
  && cmake --build build-asan -j && ctest --test-dir build-asan
make compare                                 # design-vs-firmware, needs Chrome + Pillow
make firmware                                # 17.7% flash, 18,564 B RAM
```

Golden tests write `build/<name>_candidate.png` on failure and name both paths.
**Never re-bless a golden to make a test pass** — see `CLAUDE.md`. Flashing has
to be done by the human.

## How to hand the review back

Please write findings to a file in the repo rather than leaving them in a
transcript — `docs/superpowers/2026-08-22-review-findings.md` — with, per
finding: the file and line, what breaks and under what input, and whether it is
confirmed or suspected. That way the next session can act on it without
re-deriving it, which is the failure mode that makes reviews evaporate.
