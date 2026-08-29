# CI workflows

**Status:** design approved 2026-08-29.

## Why

This repo has no CI. Every check that exists -- `make test`, `make firmware`,
`make compare` -- is run by hand, which means "the tests pass" is a claim about
whoever last remembered to run them. Three of the defects CLAUDE.md records
were caught late for exactly that reason, and one of them (`gApp->dispatch(ev)`
deleted by a scripted edit) built cleanly and passed all 803 desktop tests
because `shell/` has no harness and nothing on the desktop compiles it.

## What runs

One workflow file, `.github/workflows/ci.yml`, three jobs in parallel.

| job | command | needs | catches |
|---|---|---|---|
| `test` | `cmake -S . -B build && cmake --build build -j && ctest` | a C++20 compiler | unit tests, goldens, the sim smoke tests |
| `firmware` | `pio run -e xteink` | the `freeink-sdk` submodule, the ESP32 toolchain | anything in `shell/`, which the desktop cannot compile |
| `compare` | `make compare` | Chrome, Pillow | a board deleted from disk, a sim subcommand that no longer renders |

**Triggers:** `pull_request` (any base) and `push` to `main`, with
`concurrency: cancel-in-progress` keyed on the ref, so a force-push supersedes
its own earlier run instead of queueing behind it. Work on a `claude/*` branch
costs nothing until it opens a PR.

### `test`

No submodule and no Python: every generated asset (`assets/built/*.rfnt`, the
body TTFs, `core/src/icons_data.h`, `core/src/entity_table.h`) is committed, so
a bare checkout builds. `ubuntu-latest`.

On failure it uploads `build/*_candidate.png`. A golden test that fails writes
the candidate it produced, and CLAUDE.md's rule for inspecting one is to look at
the pixels; a CI run that only says "golden_home failed" cannot be acted on
without reproducing it locally.

**Known risk, to be settled by the first run:** the goldens were blessed on
macOS/clang and this job is Linux/gcc. Layout accumulates in fixed point and
should be bit-identical, but `stb_truetype`'s rasteriser is float, so a
coverage value could differ by one. If the first run reddens, the candidates it
uploads are the evidence, and the fallback is to run the job on `macos-latest`
instead of re-blessing anything. **The goldens are not to be re-blessed to make
CI green** -- that is the rule in CLAUDE.md and CI does not change it.

### `firmware`

`submodules: recursive` on the checkout, because a fresh worktree has an empty
`freeink-sdk/` and `pio run` then fails with `PackageException: not a
directory`, which names neither the submodule nor the fix.

`~/.platformio` is cached on `platformio.ini` + `partitions.csv`. The toolchain
is ~1 GB, so a cold run is ~8-10 min and a warm one ~3.

Two things this job cannot do anything about, recorded so a red run is read
correctly: the platform's `penv_setup.py` runs `uv pip install --upgrade` on
every build that has a network, so a build that worked yesterday can pull a new
esptool today; and that install failing is transient and exits 1. A `firmware`
job that goes red on `Failed to install Python dependencies into penv` is a
PyPI hiccup, not a code change -- re-run it.

### `compare`

**This job is a narrow gate and must not be read as a fidelity check.**

`tools/compare-design.py` exits non-zero for exactly two conditions today: an
`--only` id matching nothing, and a board named in its list but absent from
disk. A simulator subcommand that crashes makes `render_sim` return `None`, the
sheet prints `firmware not implemented`, and the run exits **0**. Wired as-is
it would pass through the regression it exists to catch -- the same shape as the
card probe answered from cache, and the `make compare` default that skipped four
screens.

So this design adds one flag rather than one job:

- `--require-implemented` fails the run if a screen whose board exists on disk
  renders as not-implemented. That is the regression CI can detect without
  inventing per-screen thresholds. It is off by default, so a human running
  `make compare` on a branch mid-implementation is unaffected; CI passes it.
- `CHROME` becomes an environment override over the hardcoded macOS path, since
  the current path cannot exist on a Linux runner.

The contact sheet is uploaded as an artifact on every run, pass or fail.

**Issue #41 stays open.** Printing a real mismatch percentage and failing on a
threshold needs per-screen baselines -- CLAUDE.md is explicit that a grayscale
screen's threshold-at-128 count is not comparable to a 1-bit screen's, so one
number cannot gate both. That is its own piece of work and this design does not
claim to have done it.

## What is deliberately not here

- **No `-Werror`.** CMakeLists.txt states the reason: a newer compiler must not
  be able to break the build.
- **No lint or format job.** There is no `.clang-format` in the repo, so a
  format gate would mean choosing a style for a 20k-line codebase as a side
  effect of adding CI.
- **No release or flashing job.** Flashing must be run by the user, and
  `On glass` -> `Done` needs device evidence an agent cannot produce.
- **No `make fonts` / `make icons` check.** Both need Chrome or freetype-py and
  their outputs are committed; a job asserting the committed asset matches a
  regeneration would be checking the generators are deterministic, which is a
  different question from whether the firmware is correct.

## Success criteria

1. A PR against `main` runs three jobs.
2. `test` reproduces what `make test` reports locally, and uploads candidates
   when a golden fails.
3. `firmware` builds `shell/` -- verified by confirming it goes **red** on a
   deliberately broken `shell/src/main.cpp`, since a job that never compiled the
   thing it claims to would look identical to a passing one.
4. `compare` goes **red** when a screen's sim subcommand is broken, and green
   otherwise. Verified the same way: by breaking one.
5. `make compare` with no flags behaves exactly as it does today.
