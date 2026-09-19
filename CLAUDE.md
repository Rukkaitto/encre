# Encre

From-scratch firmware for the Xteink X4/X3 e-readers (ESP32-C3, e-ink). Built to
have exactly the UI/UX we want, so **design fidelity is a functional
requirement**, not polish.

**THIS FILE IS THE INDEX; `docs/notes/` HOLDS THE REST OF IT.** It outgrew the
budget a session loads it into, so nineteen sections are files of their own — each
left here as its original heading, a few lines saying when you would want it, and
the path. **A stub is not a summary and never supersedes the file it names.** A
bold cross-reference (**Storage**, **Covers**, **The reader**) still resolves to a
heading in this file, which resolves to the file, so nothing in either place had to
be reworded. What stays here is what governs every session whatever you are
touching: the design-first rule, the copy rule, the hardware and rendering facts,
the runtime, the invariants, the goldens, and how to edit this repo with scripts.

## Layout

| Path | What it is |
|---|---|
| `core/` | Portable C++20, namespace `reader::`. **No Arduino, ESP or host-OS dependency** — it compiles for macOS and the ESP32 alike. Framebuffer, fonts, text, icons, dither, view-models, themes. |
| `sim/` | Desktop simulator: renders a screen to PNG at exact panel size. Where UI iteration happens. |
| `shell/` | The Arduino layer. Device detection, display bring-up, the paint sequence. The only place that touches `freeink-sdk`. |
| `tools/` | Asset generators (`fontc.py`, `iconc.py`, `embed_font.py`) and the design comparison tool. |
| `design/` | `*.dc.html` design boards — **the source of truth for the UI**. |
| `docs/superpowers/` | The spec, the roadmap, and per-phase implementation plans. |
| `docs/notes/` | **The rest of this file** — nineteen sections split out for size, each named by a stub under its own heading below. Plus the API notes (`wallabag-api.md`) and the verdicts (`strip-grayscale-verdict.md`). |
| `docs/agents/` | The three agent-facing notes the **Agent skills** section names. |
| `freeink-sdk/` | Submodule. MIT drivers for display/input/SD/battery. Never edit. |

## Commands

```
make test       # build core + run unit and golden tests (this is the fast loop)
make sim        # render Home to build/home.png
make firmware   # build for the ESP32-C3
make fonts      # regenerate the .rfnt type ramp and embedded headers
make icons      # regenerate icon bitmaps from the design boards' SVG
make version    # write reader/version.h's kVersion into every file that repeats
                # it — the boards' `V <n>` slot, ReaderCore's manifest.
                # `make version-check` says whether they agree; CI asks too.
make compare    # design-vs-firmware contact sheet, all 60 boards (~5 min)
                # ...`ok` means A FRAME WAS PRODUCED; the mismatch % beside it
                # is the check. #41 added that figure and this line still said
                # it did not print one.
```

```
pio device monitor -e xteink | tee run.log   # capture a device run, PLUGGED
# ...or unplugged: set logToCard in /.reader/settings.json and read /encre.log
python3 tools/latency.py run.log             # what each interaction cost, by press
reader_sim <screen> out.png --bench 200      # render cost per pass, on the desktop

build/paging_probe book.epub --whole --idle --interrupt 2
                                             # every page of a real book, turned the
                                             # way a reader turns it -- and compared
                                             # with the page before it. --only names
                                             # which quiet-window walk is to blame.
```

`make compare COMPARE_ARGS="--only home --export build/overlay"` writes bare
panel-size PNGs for overlaying in a design tool.

CMake uses `file(GLOB ...)`: **re-run `cmake -S . -B build` after adding or
removing a source file**, or it is silently ignored.

## CI

Three jobs on every PR — `test`, `firmware`, `compare` — plus the conventions
checker and its two git hooks, the PR-title workflow and the release workflow.
**Read it before changing a branch name, a commit subject or a workflow**: the
conventions are enforced, the hooks are the only thing that blocks anything, and
the `compare` job is a narrow gate that is NOT a fidelity check.

**`docs/notes/ci.md`.**

## What V1 is, and is not

**V1 IS CARD TRANSFER ONLY. Wi-Fi is cut.** It was too big, and cutting it took
nine boards out of the comparison sheet with it — Transfer, the five Wi-Fi flows
and SetupHotspot. They are **parked, not deleted**: `V2_SCREENS` in
`tools/compare-design.py` keeps them reachable by `--only` so a V2 design can still
be rendered, without counting them as V1 work nobody is doing. Instapaper was cut
the same way earlier (canvas page "V2 · Instapaper").

**AND V1.1 HAS TAKEN HALF OF IT BACK — THE CONNECT FLOW, NOT TRANSFER.** Six
screens (`WifiSettings`, `WifiPicker`, `WifiPassword`, `WifiConnect`, `WifiError`,
`WifiNetworkActions`) let a network be joined, preferred and forgotten; `Transfer`
and `SetupHotspot` moved to canvas page "V2 · Wi-Fi transfer" and stay parked.
**There is still no way to send a book to the device**, which is the distinction
the two consequences below turn on.

- **Settings HAS a CONNECTIONS section again**, one header and one `Wi-Fi` row that
  opens `WifiSettings`. It does **not** start the list scrolling — eleven items
  where twelve fit, so no rail and no 14px gutter, and `renderSettings` reads
  `totalRows > rows` rather than assuming. **This line said "Settings has no
  CONNECTIONS section" while the board said CONNECTIONS IS BACK**, and the code
  agreed with the line: the six screens shipped with no door, every golden green,
  and `make compare` measuring Settings drifting AWAY from a board that was already
  right.
- **HomeEmpty STILL has no action slab**, and V1.1 does not bring it back. Its
  board's call-to-action was `SEND BOOKS OVER WI-FI`, and a primary action that
  cannot work is worse than none, so the copy carries it: *"Put the SD card in your
  computer and copy EPUB files into its /books folder."* **Wi-Fi existing is not the
  condition — TRANSFER is**, which is why the connect flow landing changed nothing
  here.

## The rule that governs UI work

**A UI change goes into the design HTML first, then the implementation.** Never
only in code, and not the other way round — including when the design itself is
what is wrong (fix the board, then follow it). `make compare` is what keeps them
honest; changing only the implementation silently invalidates it and the goldens
stop meaning anything.

**AND IT PRINTS THE PERCENTAGE NOW, WHICH IS THE QUANTITY THIS FILE HAS ALWAYS
SAID TO READ** (#41). Every panel gets `mismatch 2.30% (8814 px, 1-bit)` beside
its `ok`, in the run log and on the sheet. `ok` only ever meant *a frame was
produced*, and this file's own rule — **the percentage is the check, the word is
not** — was unenforceable by the tool that prints the word: the `ReaderMenu.dc.html`
merge that read `ok` throughout while the screen sat at 13.02% against 3.02% is the
recorded instance, and **every percentage in this file up to that point was a hand
count somebody ran over the `--export` PNGs**.

- **THE ARITHMETIC IS THE HAND COUNT'S, AND IT REPRODUCES IT TO THE DIGIT.**
  Threshold both panels at 128 — **ink is BELOW 128, so grey 127 is ink and 128 is
  paper** — and count the pixels that disagree over the panel's own `w*h`. Verified
  against this file's recorded pairs on every screen that has not changed since its
  figure was written: `contents` **2.30%/2.11%** and its recorded **8,814 pixels**,
  `sd_missing` 1.83%/1.67%, `sleep` 2.32%/2.13%, `battery_empty` 2.05%/1.89%,
  `reader` 5.24%/6.29%, `peek` 3.99%/4.02%, `low_battery` 5.03%/6.02%, `reader_menu`
  3.00%/3.56%, `sleep_cover` 0.00%/0.00% — **nine screens, four of them grayscale.**
  **The boundary is load-bearing rather than a taste**: `<= 128` moves
  `contents` off 8,814 by one and `battery_empty` from 2.05% to 2.06%.
- **A FIGURE THAT DISAGREES IS A FIGURE WHOSE SCREEN MOVED, AND THAT IS CHECKABLE
  NOW.** Six recorded pairs no longer reproduce, and every one of them has a commit
  after it that changed that screen: `home` 3.54%/3.25% → **1.23%/1.13%**, `library`
  3.85%/3.54% → **2.25%/2.07%**, `book_details` 3.57%/3.28% → **0.91%/0.84%**,
  `book_error` 3.44%/3.53% → **3.08%/2.92%** and `book_error_unreadable`
  2.99%/3.17% → **2.61%/2.56%**, all five boarded and re-rendered by #100's
  placeholder-cover removal; and `settings` 1.91%/1.76% → **2.16%/1.98%**, which is
  V1.1 putting the CONNECTIONS section back on that screen. **Those figures are not
  wrong** — they record what the screen measured when it was written, which is what
  they are for. What changed is that the next one can be re-run instead of inherited.
- **THE FIGURE NAMES THE PANEL'S GREY LEVELS, so the two kinds of number stop
  inviting a comparison.** A threshold-at-128 count over the four-level grayscale
  sequence inflates against a one-bit screen's, which this file otherwise has to say
  in prose every time it quotes one — `reader` at 5.24% is not worse than
  `reader_menu` at 3.00%. **Measured off the render, never from a table of which
  screens declare `Fidelity::Grayscale`**: that fact lives in `core/` and a copy in a
  tool would be a second one free to drift, and the render is the more honest
  question anyway, since what inflates the count is the greys the panel actually
  carries. `sleep_cover_waking` declares `Grayscale` and paints ONE pass, and is
  correctly reported **1-bit**.
- **IT IS A READING AND NOT A GATE, and CI passes no new flag.** There is no blessed
  number to fail against, a board and its screen may legitimately move together, and
  **the count can rise while nothing moved** — the half-pixel phase flip the `Names`
  cut and the Sleep card's parity both record. What CI buys is the number in front of
  a reviewer beside the diff that changed it, which is the PR-title job's bargain.
  **No total and no average across screens either**: that is what the tables' own
  comments refuse when they give the styled reader specimens and the two cover modes
  their own rows, and it would be worse here, across screens that do not share a
  fidelity.
- **NO ±1-ROW-TOLERANT SECOND READING SHIPS, AND THE REASON IS THAT THE RECORDED
  ONES CANNOT BE REPRODUCED.** This file quotes tolerant counts twice — `reader_menu`
  at 1.76%/2.20% after the `Names` cut, `sleep` falling 4,326 → 4,152 — and **both
  screens' STRICT figures reproduce to the digit in this tree, so the tree is at the
  state those numbers were taken at.** Nine candidate definitions were tried against
  them (pixelwise forgiveness against the other panel's row above or below, the
  symmetric form, ink-only in each direction and both, per-row best of three
  alignments, whole-image best shift, and dilating both) and **not one lands on either
  target**; the closest misses `reader_menu` by 13% and `sleep` by 26%, in different
  directions, which is what two separate ad-hoc hand counts look like. So the tolerant
  readings in this file were each computed with a definition nobody wrote down, and
  **shipping a tenth would put a number in the tool that disagrees with every tolerant
  figure here** — the second-instrument drift this file is mostly a record of. If one
  is ever wanted, define it in `compare-design.py` first and treat the recorded
  tolerant readings as not comparable. The strict count stays the instrument, which is
  the point: swapping instruments to make a figure look better is how a real
  regression gets hidden.
- **IT IS NOT DECISIVE AND WAS NEVER GOING TO BE.** Fixing `design/Settings.dc.html`'s
  `Size` row — a real defect, three points wrong — moved this figure by **one pixel**
  (8553 → 8554), because that right-aligned run was mismatched either way. **The value
  is in being able to ask**, and nobody could have known that one without computing it.

**And it has to actually cover the screen.** Until a cold-read review found it,
`make compare` defaulted to the seven V1 boards, so it compared Home and Library
and skipped four of the six implemented screens — while CLAUDE.md called it the
check that keeps the design honest. `--only <flow screen>` matched nothing at all,
silently. The default is now every board, and `--only` errors on an id it does not
recognise rather than reporting `0/0`. **A board NAMED in the list and absent
from disk is a hard error too, and that was a second door to the same quiet
pass** — the loop printed one line and `continue`d, which dropped the screen
from the sheet entirely rather than counting it as not-implemented, so
**deleting a board shrank the denominator and made the ratio look BETTER**,
and `--only` on that id was back to exiting 0 with `0/0`. A row outlived its
deleted board by four days that way (the reader menu's cut page-number row).
The list is checked against the disk up front now, so it fails in a second
instead of after three minutes. **A check that reports on less than it
claims is worse than no check, because it is trusted** — the same shape as the
card probe that was answered from cache and kept reporting success.

**`--only` TAKES A LIST, IN BOTH SPELLINGS: `--only home,reader` and
`--only home --only reader` are the same run**, and the unrecognised-id error
applies to **every** element, so an id's position cannot decide whether a typo is
caught. The comma form is primary because it is the one that survives
`make compare COMPARE_ARGS=...`, where `$(COMPARE_ARGS)` is expanded **unquoted**
by make — a spelling needing shell quoting inside a make variable would be a worse
tool. `action="append"` sits under it because **argparse's default for a plain
option is to OVERWRITE**: `--only home --only library` kept only `library`,
dropped `home` without a word, and printed a confident `1/1 screens implemented`.
An **empty** `--only` (`--only ""`, `--only ,`) is an error too, because selecting
nothing finds nothing missing and exits 0 on `0/0` — the same quiet pass reached by
an empty argument instead of an unknown one. Issue #77 reported this against the
COMMA form, which had worked since 2026-08-20; the repeated flag is where the
defect actually was, and it produces the identical `1/1`.

**AND THE SAME COUNT WAS INFLATED FROM THE OTHER SIDE, WHICH NOTHING HAD
REPORTED: `reader_anchored` WAS LISTED TWICE.** Its board was added to
`FLOW_SCREENS` design-first, then the implementation commit added a second row
beside the other styled reader specimens — same id, same board, a **different
label** — so every default `make compare` rendered that board four times instead
of twice, showed the same screen twice under two names, and printed a denominator
of **37 for the 36 screens that exist**. That is the exact mirror of the absent
board that shrank the denominator: in both cases the ratio is over something other
than the set of screens it claims to measure, and here **both halves moved
together**, which is why no ratio ever looked wrong. The tables are checked for a
repeated id up front now, over all three of them on every run rather than over the
selection — a duplicate is an authoring mistake in the table and should not need
the right `--only` to surface. It **errors rather than de-duplicating**, because
the two rows carried different labels: there is a real question about which was
meant, and this tool must not answer it by guessing.

**THE SCRIPT HAS ITS OWN TESTS NOW** — `tools/test_compare_design.py`, plain
`python3`, Chrome and the simulator stubbed out. They assert the **set and the
count**, because that is where all six of these defects lived.
**Deliberately NOT wired into `make test`**, which builds on a bare checkout with
no Python and no submodule; so it is a test that has to be remembered, which is
the honest cost of keeping the fast loop interpreter-free.

- **THIS LINE SAID "never a pixel" AND THE MISMATCH FIGURE MADE THAT HALF FALSE.**
  The rule it was really stating is **never a RENDERED pixel** — a golden in the
  wrong file — and that still holds: the arithmetic's cases are **synthetic panels
  whose answer is known by construction**, a four-pixel row at 126/127/128/129 for
  the boundary and a half-black panel for the rest, with the stubbed simulator
  producing a 2x1 image `normalise` scales to exact half-panel columns at both
  geometries. Every guard is proved by mutation: `<= 128` fails the boundary case,
  measuring the `NOT IMPLEMENTED` placeholder prints `mismatch 0.00%` beside a
  screen nothing draws and fails, reading the levels off the DESIGN panel labels a
  one-bit screen 4-level and fails, a wrong denominator fails three cases, and one
  figure reused across geometries fails on the pixel counts being equal.

**AND THE SAME SHAPE HAD THE REVIEW SURFACE ITSELF: `design/ereader-v1-ui.html`
IS A GENERATED FILE AND ITS GENERATOR WAS LOST** (#60). It is the published design
canvas — one artifact URL, the compiled Claude Design editor plus every
`design/*.dc.html` and `design/canvas.json` seeded into its `appifact-doc` block
— and it is what the design is reviewed from. `seed-canvas.mjs` and
`payload.template.html` lived beside the skill that documented them, in
`.claude/skills/design-change/`, **untracked**: only `SKILL.md` was ever added to
git, so a fresh clone or worktree materialised the instructions and not the tool,
the documented reseed could not be run at all, and the canvas could only be
hand-edited — the wrong operation on a generated file. Reseed with **`make
canvas`**; **`make canvas-check`** says whether the committed file is what the
boards say, and names what drifted.

- **IT WENT SIX BOARDS BEHIND, AND FIVE OF THE SIX WERE *CARRIED*.** Being in the
  file record is **not** being on the canvas: with no `canvas.json` `artboards`
  entry the editor loads a board and never shows it, so `SleepCover`,
  `SleepCoverDetails`, `SleepCoverWaking`, `SleepWaking` and `LibraryOpening` were
  present and invisible, and `BookErrorUnreadable` was absent outright. **Two
  staleness axes, and the quieter one is the layout.** `make canvas` refuses
  rather than placing a board itself — which page it belongs on is a design
  decision — and prints the next free slot in the layout's own 580/900 row-major
  grid. Every state board is on `page-4`.
- **NOTHING COULD HAVE NOTICED, because the doc is ONE 526 KB LINE.** Every commit
  that has ever touched this file is an identical `1 insertion, 1 deletion` in a
  diffstat, so `git diff --stat` — this file's own rule for catching a scripted
  edit gone wrong — is blind to it, and so is review. That is why the check had to
  be a program.
- **`make compare COMPARE_ARGS=--require-canvas-current` IS WHAT MAKES IT LOUD,
  off by default, and CI passes it** — `--require-implemented`'s bargain exactly,
  for its reason: a developer comparing a board mid-edit must not owe a 3 MB
  reseed, and what the flag buys is a red X in front of the one person who can
  still fix it. It is independent of `--only`, because whether a board reached the
  canvas is not a fact about the screens a run selected. It **delegates** to the
  generator rather than reimplementing the comparison, so there is no second
  answer to keep in step, and a missing `node` is an **error rather than a pass**.
- **THE TOOL IS IN `tools/design-canvas/`, TRACKED**, with every other generator
  here, and that placement *is* the fix — the code was never the thing that was
  missing, version control was. It carries `test_seed_canvas.mjs`, plain `node`,
  not wired into `make test` for `test_compare_design.py`'s reason; **its first
  case is the whole proof**: the original's only surviving specification was its
  3 MB output, so the test rebuilds the committed canvas from the content that
  canvas itself carries and demands **byte-identity**. It reproduces it exactly,
  which is what says this is *the* generator and not merely *a* generator. The
  layout guards are proved by driving the CLI against throwaway trees, because a
  guard that has stopped firing looks exactly like a repository with nothing wrong.
- **`payload.template.html` is 2.4 MB of compiled editor this repo cannot
  rebuild**, kept verbatim with the doc block and the title as its only
  placeholders. So a reseed changes content and never the editor — asserted by
  comparing every byte outside the doc block against the published page. **`<` is
  escaped, as a `\u003c` sequence, and nothing else is**: the JSON sits inside a
  `<script>`, so one literal `</script>` in a board would close the block early
  and truncate the canvas at that byte, and every board is HTML.
- **Publishing is still a separate, human step**, with `contract: "0.1.31"` and
  the canvas's own `url` — publishing without it creates a stray duplicate.

**AND ONE THING ON A BOARD IS NOT A PIXEL AT ALL, SO NO AMOUNT OF COMPARING
FINDS IT: THE VERSION (#152).** `design/Settings.dc.html` states `V 0.2.0` and
`core/include/reader/version.h` states `0.2.0`, separately — so when **both** are
stale they agree exactly, Settings measures its usual ~2.3%, and the sheet is
green about a screen that is lying. **v0.2.0 shipped drawing `V 0.1.0`** with the
release gate run faithfully; the tag was deleted and re-cut within the hour
because nothing had been downloaded, which is luck. The board's slot is
**generated** from the header now (`make version`), `make version-check` refuses
drift in under a second, and CI's `compare` job passes
**`--require-version-current`** — `--require-canvas-current`'s bargain exactly,
delegating to the generator rather than holding a second answer.

- **IT WAS FOUR COPIES AND EVERY LIST OF THEM SAID THREE.** `design/Boot.dc.html`
  states the version too, and had been stale since before v0.2.0 — missed by the
  issue, by the release-gate step written for this, and by the comment in
  `test_version.cpp` that enumerated the copies, because the boot screen is not
  implemented so `make compare` draws a placeholder beside that board and
  measures nothing. So `versionc.py` **scans `design/*.dc.html`** rather than
  holding a list: a new board carrying a slot is covered by existing. Two boards
  are named, and only to make **losing** a slot an error — with the slot gone
  there is nothing left to disagree with, and a scan alone would call the tree
  current.
- **`test/unit/test_version.cpp` NO LONGER PINS THE STRING.** A pin that must be
  hand-bumped beside the thing it pins is a second copy with a build failure
  attached. What it asserts now is that the literal is one the generator's regex
  can read — a version it cannot parse makes `versionc.py` refuse and every
  generated copy silently stop being regenerated, and `make test` is the only
  check of that shape which runs on a bare checkout with no Python.
- **`core/library.json` is in the generated set too**, and had sat at `0.1.0`
  through every release: one tag ships the firmware and the library inside it, so
  there is no second cadence for a second number to track.

## The rule that governs COPY

**EVERY STRING A READER SEES GOES THROUGH THE `humanizer` SKILL BEFORE IT SHIPS**
(`/humanizer`), and it goes through it **on the board**, because copy is a UI change
and a UI change goes into the design HTML first. That covers board copy, screen
titles, badges, hint-bar words, prompts, empty states, every refusal's sentence, and
the pages `design/Web*.dc.html` serve — a new string and an edit to an existing one
alike. The skill's reference is Wikipedia's *Signs of AI writing*; what it is here to
catch is the staging half, a `not X but Y` naming an objection nobody made, a one-line
closer restating the line above it, a forced triad, inflated significance, and a dash
standing in for a full stop.

**IT IS A STANDING RULE BECAUSE THIS FIRMWARE IS WRITTEN WITH CLAUDE CODE**, so its
copy carries a model's default habits unless something takes them out, and nothing
else here looks. `make compare` measures the board against the panel and is blind to
what the board SAYS; the goldens pin the pixels of a sentence, not its voice.

**WHAT A FULL PASS OVER THE DEVICE'S COPY ACTUALLY FOUND (2026-09-12): two dashes,
and nothing else.** That is the finding rather than a light touch — the copy was
already short, concrete and active, because this file has been refusing false and
inflated claims one screen at a time since the battery gauge. The two were
`BatteryEmpty`'s *"shutting down — connect a charger"* and the Instapaper setup line's
*"offline — and archive or like them"*, and **what convicted them was the rest of the
app**: every other prompt on the glass states its facts as separate sentences
(`The file leaves the SD card. Your progress and bookmarks are kept...`), so the
connecting dash was the outlier and not the house style. Both are now a full stop and
a comma.

**THREE THINGS THE SKILL DOES NOT REACH, and they are most of the strings in this
repo:**

- **SPECIMEN CONTENT IS THE READER'S VOICE, NOT THE DEVICE'S.** The Middlemarch
  excerpts, `Bookmarks`' quotes, the article titles and `ReaderList`'s essay on line
  boxes all stand in for what a book or a feed holds. `ReaderList`'s specimen opens
  *"A page is not a container that text is poured into. It is a grid of line boxes"*,
  which is a textbook `not X but Y` — and it is a **book's** sentence, so it stays, on
  the same rule that leaves Eliot alone.
- **DEVELOPER ENGLISH NEVER REACHES THE GLASS.** `openBook`'s `why`, `CoverReport`'s
  reason, every `[tag]` line in the log. The screen takes a bounded
  `BookErrorReason` precisely so that prose stays blunt and unstyled.
- **A SEPARATOR IS A GLYPH, NOT A CONNECTOR.** `·` in `ASLEEP · HOLD POWER TO WAKE`
  and the em dash the reader's footer draws for an unknown page total (`3 / —`) are
  punctuation the boards chose. The dash rule is about one joining two clauses.

**AND A COPY EDIT IS A WRAP QUESTION, so it owes the same two measurements any other
copy change owes**: #76's next-word clearance, which `test_book_error_copy.cpp` and
`test_screen_battery_empty.cpp` now make mechanical for two screens, and a
threshold-at-128 count over the `--export` panels with an untouched screen beside it
as the control — because the sheet prints `ok` and not a percentage (#41). The
`BatteryEmpty` edit above kept its four lines at the same four breaks with the
tightest clearance **unchanged at 33px**, moved 3,748 pixels in **two contiguous
27-row bands at both geometries and none outside them**, and measured
**2.08%/1.91% → 2.05%/1.89%** against `sd_missing`'s 1.83%/1.67%, which reproduced
this file's recorded figure to the digit and is what says the two readings are one
instrument.

**ONE CONTRAST WAS KEPT, AND THE EXEMPTION IS THE USEFUL HALF OF THE RULE.**
`InstapaperSignInRefused` says *"Instapaper refused Encre itself, not your account.
Trying again won't help."* — a `not X but Y` by shape, and the skill's own condition
for keeping one is that the negative half corrects a belief the reader actually
holds. A reader who has just been refused a sign-in believes it was their account.
**Removing that clause would cost the sentence its entire job**, which is the test to
apply before deleting a contrast anywhere else.

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
- **LINKING THE WI-FI STACK COSTS 21,328 BYTES OF STATIC RAM, PAID AT BOOT
  WHETHER OR NOT THE RADIO IS EVER SWITCHED ON**, and that is a different
  number from the one V1.1's spec argued about. That spec priced the RUNTIME
  allocation — ~23 KB of driver buffers and two tasks when the radio comes up —
  and concluded the flow may only be entered from Settings, where ~133 KB is
  free. The link-time cost is not on that path: it comes off the heap at every
  instant, including while reading.
  - **Measured by isolation, not inferred**: the same firmware with the radio's
    translation unit stubbed and every new global still present links at
    **24,844** bytes of static RAM against **46,172** with it, so the objects
    this feature added are **96 bytes** and the rest is the stack. Flash goes
    1,547,190 → 2,198,129 (23.6% → 33.5% of the 6.25 MB app partition), which
    is a non-issue.
  - **THE FIGURE TO CHECK IT AGAINST IS THE READING FLOOR**, and
    `docs/on-device-smoke-checklist.md` records the smallest this project has
    ever measured: **13,696 bytes**, reproducibly, opening a 66,843-byte
    chapter from a **232-entry** Library, found as a `reason=4 PANIC` three
    times. Take 21,328 off it and the arithmetic is negative.
  - **MEASURED ON GLASS (2026-09-11): `[stage] open-paginated heap=45140
    min=28508`**, opening from the LIBRARY — which is the conservative path,
    since the Library stays resident under the Reader where Home's CONTINUE
    leaves nothing behind. So the floor on that card is **28,508 bytes**,
    against ~49,836 for the same open without the stack linked: Wi-Fi took
    ~43% of the headroom and what remains is **2.1x the 13,696** that produced
    the recorded panic.
  - **WHAT THAT BUYS, AND IT IS NOT A LOT OF CARD.** The Library's residency is
    ~291 bytes a book (203 books, 201,576 → 142,560), so 28,508 bytes is about
    **98 more books** before the floor reaches zero — a ceiling roughly DOUBLE
    that shelf rather than ten times it. Two things make that optimistic: the
    floor is chapter-dependent and the recorded panic used a 66,843-byte
    chapter, so a longer book can spend the margin before the book COUNT does;
    and `getFreeHeap` cannot see the largest contiguous BLOCK, which is what
    actually decides an allocation.
  - **THE SYMPTOM TO EXPECT IS NOT A WI-FI FAILURE** — it is `abort()` with no
    diagnostic, which is a reboot to Home, a shape this file records having been
    misreported twice already.
  - The escape hatch if it bites is `ENCRE_FS_SELFTEST`'s: an opt-in build flag
    so the default firmware never links the stack. What it costs is that the
    Settings row must not promise Wi-Fi in a build without it, and the only
    clean way to tell the row is to plumb a build capability into `core/`.
- **ONE TLS HANDSHAKE FRAGMENTS THE HEAP FOR THE REST OF THE SESSION, AND
  `getFreeHeap` CANNOT SEE IT.** Measured on an X3 by `ENCRE_WALLABAG_PROBE`
  against a real server: the largest free BLOCK goes **61,428 -> 34,804** on the
  first `WiFiClientSecure` connection and never returns above **36,852** — not
  when the stream closes, not after four more handshakes, not when the radio goes
  down. **A plain `WiFiClient` costs no block at all.** Every figure in the run is
  in `docs/notes/wallabag-api.md` §8.
  - **THE FREE HEAP RECOVERS EVERY TIME, WHICH IS WHY NOTHING SAW IT** — 73,344 /
    73,076 / 72,760 across three connections, with the block falling throughout.
    This file's own rule is that **the largest free BLOCK decides an allocation**,
    and here is the case where the two answer differently and the block is right.
    It is a leak in neither direction: the repeats OSCILLATE (22,516, 19,444,
    36,852, 22,516, 22,516), so a dozen connections cost no more than one.
  - **THE PLATEAU IS 104 BYTES UNDER THE ONE ALLOCATION EVERY BOOK NEEDS.**
    `Inflater::begin` wants **36,956** bytes in one piece on every deflated entry;
    36,852 refuses it. So a device that has used TLS **cannot open a book until it
    is restarted**, and it fails politely — nothrow, `BookErrorReason::OutOfMemory`,
    `BookErrorMemory`'s *"needs more memory than is free right now"* — about a
    book that is fine. **104 bytes is a coin flip, not a margin**: one run, one
    session, and a tree measuring 38 KB tomorrow is the same finding.
  - **AND ~21.5 KB OF FREE HEAP DOES NOT COME BACK AFTER `down()` EITHER**,
    130,744 -> 109,172, reproduced within 1.5 KB across three runs. That is
    **separate from** the 21,328 bytes of static RAM the stack costs at link time
    above. A session that has brought the radio up carries a reading floor ~21 KB
    below the one every figure in this file was measured against.
  - **THE ANSWER TAKEN IS A RESTART, NOT A SMALLER TLS.** A cold boot measures
    61,428, so `esp_restart` provably restores it — `handleRetry`'s own remedy for
    a card lost after a mount, forced by the platform rather than working around
    our own bug. Shrinking mbedTLS means building arduino-esp32 from source, since
    it ships precompiled, and would risk "fails on some servers and not others".
  - **`[alive]` CARRIES `block=` BECAUSE OF THIS.** It reported `heap` and
    `minHeap` and not the number that decides an allocation, so a fragmented heap
    and a healthy one read identically — the reports-on-less-than-it-claims shape
    this file records for the card probe answered from cache and the `make compare`
    default that skipped four screens.
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
  - **AND IT CAME BACK, ON THE ONE SLEEP MODE THAT DOES NOT PAINT A COVER (#94).**
    Reported off an X3 after a week of use: with Settings' `Shows` on **DETAILS** a
    wake showed noisy banding where a cover showed a clean black flash. The
    waking-paint block in `setup()` had re-introduced the call this bullet warns
    about, as `if (!coverOnGlass) display.skipInitialResync();`, and **its argument
    for the no-cover case was backwards**: it reasoned that the frame is the sleep
    screen with one line changed, so "almost every pixel the garbage baseline calls
    unchanged really is unchanged". **Which pixels a refresh calls unchanged is
    decided by DTM1, not by the glass** — with DTM1 holding power-up garbage, the
    set of pixels re-driven is unrelated to the set that differs, whatever the
    frame is. `Uc8279Driver.cpp` says so where it picks the bank: **both** banks
    diff against "the REAL previous frame in DTM1", and `BW_GC` "clears via the
    true old->new transition, not a white baseline".
  - **THE ASYMMETRY WAS NOT THE GRAYSCALE REBASE, WHICH IS THE FIRST THING TO
    SUSPECT AND IS WRONG.** `cleanupGrayscaleBuffers` really does leave the
    controller on a valid B/W baseline after a cover sleep, so a cover sleep and a
    DETAILS sleep end in different controller states — and **neither survives**, because
    a wake is a chip reset and `initController()` resets every one of those flags.
    **No controller state crosses a sleep in either mode.** The whole asymmetry was
    which branch of the wake paint ran.
  - **AND THE CALL BOUGHT NOTHING, which is what made removing it a pure win rather
    than a trade.** What it was for was a DU, and the DU was never reachable:
    `displayStart`'s `useGc` is `(mode != Fast) || !_oldPlaneValid ||
    _forceFullSyncNext || _initialFullsRemaining > 0`, an **OR** — and
    `display.requestResync()` sets `_forceFullSyncNext` ~540 lines earlier in the
    same `setup()`, with **no panel refresh in between** to clear it. So the GC bank
    loaded either way and the assertion's only effect was to make `if
    (!_oldPlaneValid)` false and **skip the DTM1 white seed**. It spent the one thing
    that makes the clear clean and got no cheaper refresh for it. The
    `_darkBackground` rewrite that would otherwise cover for a missing seed cannot
    help: **`setBackgroundHint()` has no call site anywhere in this firmware**, so
    that flag is false for its whole life.
  - **NEITHER MODE ASSERTS A BASELINE BEFORE THAT PAINT NOW, AND BOTH ASSERT ONE
    AFTER IT.** `skipInitialResync()` is right for a caller that has restored the
    baseline first, and after `showOnePass` we have — we wrote the frame ourselves,
    and `displayFinish` has already synced DTM1 to it. It goes there in both modes,
    and `requestResync()` goes nowhere: zeroing the rest of the boot clear budget is
    what keeps a wake to **one** flash, where forcing Home's GC as well would buy a
    second. **A card with `fullOnTransition` on still gets two**, from Home's own
    transition, and that is the pre-existing price rather than a new one.
  - **NOTHING ON THE DESKTOP TOUCHES ANY OF IT.** All of it is driver state driven
    from `shell/src/main.cpp`, which has no harness; `core/` has no notion of
    `_oldPlaneValid`, and the wake paint does not even consult `fidelity()`. So the
    suite says nothing, and **the mode asymmetry is the diagnostic** — see
    `docs/on-device-smoke-checklist.md` §4.5, which walks both `Shows` settings
    precisely because one of them passing is not evidence about the other.
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
path.

**TWO SCREENS DECLARE IT NOW, AND THIS LINE SAID "NO SCREEN DECLARES IT TODAY"
THROUGH BOTH OF THEM.** The Reader falsified it in Phase 3 and nobody came back
here; the sleep screen falsified it again with covers. It was kept on the argument
that it is "the only way to put continuous tone on this glass — Phase 3's question
about book covers and images", and **that question is now answered in the
affirmative by the thing it was reserved for**: a book cover is a photograph, and a
photograph is the one thing on this device that needs four levels. The sequence was
also expensive to get right, and the comments in `paintGray()` were each earned by
breaking the panel — that half of the reason never expired.

**`SleepScreen`'s IS DECIDED PER PAINT, WHICH IS THE FIRST DYNAMIC `fidelity()` IN
THE FIRMWARE.** It answers `Grayscale` only when there is actually a cover to paint
— `cover_ != nullptr && vm_.shows != SleepShows::Details` — so `DETAILS` mode stays
on today's single ~825 ms waveform and is pixel-identical to the screen that
shipped. **It is decided from the SOURCE and never from the load**, because the
shell has to know which sequence to paint before any pass runs: a screen that
declared `Grayscale` and then fell back would spend three waveforms drawing a
one-waveform screen. That is also why the shell hands over a **null** source rather
than a source it knows will refuse.

**WINDOWED GRAYSCALE IS NOT THE ESCAPE HATCH, AND IT IS NOT THE ESCAPE HATCH FOR
THE READER EITHER** — re-asked for page turns as #17 and closed again on stronger
grounds than the first time (`docs/notes/strip-grayscale-verdict.md` has the
working). **The SDK's strip API windows the RAM WRITE ONLY**: `displayGray` opens
with `grayWindowIn()`, whose own comment says it "resets PTL to full after any
per-strip `writeGrayscalePlaneStrip` windows" (`Uc8279Driver.cpp:300`), and
`grayWindowIn` writes a hardcoded full-panel PTL (`:70`). **So no strip path can
buy waveform time**, and the waveform is 889 ms of the refinement's 1408. It also
reaches only **2 of the 6** full-plane writes `paintGray` issues — `GrayPlane` is
`Lsb`/`Msb` (`PanelDriver.h:132`), and the base's and cleanup's DTM1/DTM2 pairs go
through `sendPlaneFlipped`, which has no windowed form. Ceiling **3.4 ms of
1408**, in a pass that runs 5 s after the user stopped pressing.

**The geometry argument that used to stand here was about the wrong API, and it
does not rescue the reader either.** `byteIndex` maps `physY = width_ - 1 - x`
(`framebuffer.cpp:110`), so the gate axis the strip API windows **is the canvas's
x axis** — a strip is a vertical slice of the portrait page. That is a real
difference from chrome, where a focus move is a row band lying on the source axis
the API cannot window at all. It differs in the wrong direction: a page turn
changes the full height of a **492px text column on a 528px canvas**, 93.2% of the
gates, so there is nothing to exclude. The "every gate line is driven anyway"
half is still true and now has a proper home — it is why windowing
`preconditionGrayscale`, the one call that really does window a refresh, is also
worth at most 25 ms.

**`strip=1` on the boot line means the driver has a windowed plane write, not a
windowed refresh**, and reading it as the latter is what kept this question open.
The SDK's own `docs/xteink-x3-uc8279-support.md:51` says `supportsStripGrayscale()`
is false here, which is stale — `Uc8279Driver.h:58` returns true. Neither line is
the authority; the driver body is.

**Rules, fills and dither** have coverage 0 or 3, so they are identical in every
pass — `Bw`, `BwDithered` and all three grayscale planes. That is what makes a
plane bug show up as fringing rather than missing furniture, `test_components.cpp`
pins it for `drawRow`'s hairline and the header band's rule, and it is why each
re-bless of Home — first onto the dithered path, then onto `Mono` — moved **only**
partial-coverage pixels, verified per pixel against the coverage map rather than
by eyeballing totals.

**Icons are not in that set.** All thirteen shipped marks are 2 bpp
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
why** — and `kClustered` still ships, whatever the screen's fidelity, on
**`renderSleep`'s full-panel field, which is now its ONLY production caller**.
This line has named its callers three times and been overtaken twice: it said
"the cover placeholder" while there were three of them, then "Book details' cover
slot and `renderSleep`'s field" once #95 had taken Home's and the Library rows',
and Book details' went too. **Naming the callers is still right — it is what made
each of those revisions loud** — and the argument was never a fact about which
caller draws the tint. `kClustered` is for *tints*: the sleep board's
`.dither-field` is one round dot repeated on a 4px grid, and dispersing that area
into isolated
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
  button is still down; the release then emits nothing. And because `tick()` only
  runs from the main loop, which a gray refresh blocks for ~1.5 s, the **release
  edge classifies the press too**: a hold made entirely inside a repaint would
  otherwise arrive as a `Short`, which on a list means opening the item instead of
  its actions overlay.
- **A `Short` FIRES ON THE DOWN EDGE unless the button binds a hold**, and until
  it did, every press on the device paid its own duration — 80–200 ms of dead time
  in front of a ~520 ms waveform, on every button of every screen. Only a
  long-press binding makes a press ambiguous, and **exactly one button in this
  firmware binds one**: `Confirm`, on the Library (`vm_.holds = {false, true,
  false, false}`). Everything else — every page turn, every Back, every Confirm on
  Home or Settings or the reader menu, every focus move — was waiting on the
  release for an ambiguity it does not have. `ReaderScreen` does not call
  `declareHints` at all, so the *reader*, the screen with the most presses on it,
  had nothing to wait for and waited anyway.
  - **An auto-repeat button fires too, which makes held scroll typematic.** The
    ramp is unchanged (still from `downAt + kRepeatDelayMs`), so what this removes
    is a hold's dead first row: nothing moved until the 400 ms delay *plus* a whole
    row of the 6 rows/s slow rate — ~567 ms before the list acknowledged a held
    button.
  - **Two flags, and they are not the same flag.** `firedShort` says the down edge
    spent the press, so the release owes nothing; `consumed` says a `Long` fired,
    so `tick()` stops looking at the button at all. An auto-repeat press needs the
    first and must not have the second.
  - **POWER NOW SLEEPS ON THE DOWN EDGE, and the SDK already handles the finger
    still being on the button.** `deepSleepUntilPowerButton()` opens with
    `waitForPowerButtonRelease()`, so the wake cannot be satisfied by the press
    that asked for the sleep — which would have been an immediate wake, and would
    have looked like the device refusing to sleep. In practice the question does
    not arise: `sleepNow()` paints the Sleep screen first, and that is a FULL
    waveform (~825 ms), by which time the button is long since up.
  - **`repeatable` is LATCHED at the down edge**, for the reason `consumed`
    already was: the mask follows the top of the stack, and the press that fires on
    the down edge may itself push a screen where that button repeats. Re-reading
    would start scrolling the new screen under a finger that has not yet come up
    from the press that opened it. `tick()`'s long-press branch skips a
    `firedShort` press for the mirror case — Home's `Confirm` opens the Library,
    where `Confirm` IS long-pressable, and the finger is still down when that mask
    arrives.
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
- **THE WAKE REQUIRES A HOLD, AND THE CHIP CANNOT ENFORCE ONE — `setup()` DOES.**
  `armPowerButtonWakeup` arms a **level-triggered** source
  (`esp_deep_sleep_enable_gpio_wakeup` on the C3, ext1 on Xtensa), so the SoC
  resumes the instant the line reaches its active level and there is no dwell
  anywhere on that path nor any way to ask for one. So the badge's
  `HOLD POWER TO WAKE` is made true **after** the wake, by
  `requireHeldPowerButtonOrSleepAgain` refusing one that was not held for
  `kWakeHoldMs` (600) and sleeping again.
  - **CONFIRMED ON GLASS (2026-08-30), which is the only place it could be:
    `shell/` has no harness, so not one line of this gate is executed by the
    desktop suite** — 1250 green test cases say nothing about it. A tap leaves the
    sleep screen exactly as it was and a hold wakes normally.
  - **WHAT THAT CONFIRMATION DOES NOT REACH, so it is not read as covering it:**
    the reset-reason guard below is exercised only by flashing a device that was
    ASLEEP at the time — flashing an awake one leaves no `slept` flag, so the
    branch is never entered and a working boot afterwards proves nothing about it.
    The refusal COUNT and the `at=` figure are likewise separate observations,
    readable off `[wake] refused`/`[wake] held` with a terminal attached.
  - **WHERE IT IS CALLED IS THE WHOLE COST OF THE FEATURE: before
    `display.begin()`**, the earliest anything can reach the panel. E-ink holds its
    last image, so the glass still shows the sleep screen that named the hold — a
    refusal repaints nothing and spends **no waveform**. One line later, past the
    panel bring-up, and a brush against the button in a bag costs a flash.
  - **It is after `detectAndSelectBoard()` because it needs the profile.** Both
    Xteink profiles happen to agree on GPIO 3 active-LOW; that is the same
    coincidence `BatteryMonitor`'s constructor rests on, and not a design.
  - **`fromSleep` ALONE WOULD BRICK A FLASH.** The `slept` flag is NVS and survives
    **any** reset, so a chip that was asleep and is then reset by a host attaching
    (`ESP_RST_USB`), esptool, `esp_restart` or a panic still reports `fromSleep`
    with no finger near the device — and would sleep straight back with a stale
    image and no log. The gate also requires `rst` to be `DEEPSLEEP` (USB attached)
    or `POWERON` (on battery), the two a real button resume produces. A first-ever
    `POWERON` carries no flag, so neither test is redundant.
  - **A REFUSAL GIVES THE `slept` FLAG BACK** (`markSleeping()`). `takeSleptFlag()`
    consumed it on the way in and one flag buys exactly one resume — a refused wake
    did not spend it, and without the re-arm the **next** wake reads as a cold start
    and the reader loses their page. That would be blamed on the restore.
  - **The dwell is measured against `millis()`, whose zero is after the
    bootloader**, so the real hold asked for is a little longer than 600 ms. It also
    assumes boot reaches the gate before the threshold — ~215 ms unplugged, ~470 ms
    plugged into a charger with no terminal. `at=` on the refusal line is what makes
    that readable off a device rather than guessed at.
  - **A refusal is otherwise INVISIBLE — it paints nothing and the log buffer dies
    with the RAM** — so the count rides `RTC_DATA_ATTR` (2 bytes, and it shows on the
    build's `RTC SLOW .data`) and is reported by the wake that finally succeeds.
    Not NVS: a refusal must not cost a flash write. The price is the documented one,
    that `ESP_RST_USB` re-initialises `.rtc.data`, so plugging in to read the count
    erases it.
  - **`kWakeHoldMs = 0` disables it**, and a compile-time constant is the only
    possible escape hatch: every other tunable is a row in
    `/.reader/settings.json`, which is on the **card**, mounted hundreds of lines
    below — a gate that waited for it would already have paid the bring-up it
    exists to avoid.

## What an interaction costs

The budget from the button going down to the panel moving, the `[i]` log line and
`tools/latency.py`, the card log, what a real run measured, and which primitive
spent a render. **Read it before optimising anything, and before quoting a µs
figure from anywhere** — it is where the desktop-to-device ratio traps are
recorded, and where a bench without its build type is declared not a measurement.

**`docs/notes/interaction-cost.md`.**

## Storage

`reader::FileSystem` and its three implementations, the 27-clause contract, the
settings file, the session record and its wire format, `Focus` / `FocusScreen`,
and the two card probes. **Read it before touching anything that reads the card
or restores a wake** — including the rule that a screen reporting a focus accepts
one back, which shipped one-way on three screens before it was made structural.

**`docs/notes/storage.md`.**

## The caches

Every cache in the firmware, in RAM and on the card, with what bounds it, what its
eviction policy IS and why that one rather than the obvious one, and what it costs at
its limit — plus the two stores that are deliberately unbounded and the reason each is.
**Read it before adding anything that holds a derived answer**, and read it before
reaching for #26: the Phase 5 "cache eviction" card meant the spec's `/.reader/cache/`,
which is #19 and was never built, and everything that WAS built already evicts, refuses
or is one-of by construction.

**`docs/notes/caches.md`.**

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
`0x20..0x7E` plus **all** of `0xA0..0xFF` — **and eight punctuation codepoints plus
U+FFFD** that this line used to omit: `0x2013 0x2014 0x2018 0x2019 0x201C 0x201D
0x2026 0x2039 0x203A`. The understatement cost real work: the Typography panel's
`‹ ›` were planned as a `make fonts` pass and a flash cost that did not exist,
because U+2039 and U+203A were already there. So every accented capital has a real
glyph, and so do the quotes, the dashes, the ellipsis and the guillemets.
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
  - **A `max()` OVER TWO HEIGHTS WHERE ONE ALWAYS WINS IS A PINNED NUMBER
    WEARING A DERIVATION'S CLOTHES, AND EVERYTHING UNDER IT IS THEN UNTESTED
    SLACK (#95).** Book details' block was `max(column, cover)` and the cover's
    180px won for **every title the screen can draw** — so the height was a
    constant, the column's runs cost nothing, and this file recorded the
    consequence approvingly: *"the block above did NOT move when the subtitle
    went"*. What was hiding in that slack was a **second `kDetailsColGap`**. A
    flex `gap` sits BETWEEN items, so a title and an author cost one gap and the
    arithmetic charged two; over-reserving 6px only made the title's budget
    conservative, and nothing on the glass could see it. Removing the cover made
    the column the height, and the spare gap would have drawn **every rule on the
    screen 6px below the board's**.
    - **The tell is the losing branch, not the bug.** A `max` whose other arm
      cannot win is a branch no test exercises, so every number feeding it is
      unverified — and the day the winner goes, all of them become load-bearing
      at once. Grep for the *loser* when a height changes.
    - **It also hid an ELISION.** The cover took 140px (120 plus its gutter) off
      the title's measure, so a 67-character real-card filename wrapped to five
      lines and was cut; on the full width it is four lines and **complete**. A
      screen whose whole argument is that the name is the content had been paying
      for an empty box with the name.
    - **The BUDGET and the HEIGHT are two quantities and must not be collapsed**,
      which is the sleep card's chapter reserve one screen over. `blockRoom` is a
      budget and comes from the CANVAS — the band above, the rule, rows and bar
      below — so the title's line count is **not** self-referential even though
      the height is now the column's. The height is a *result*, `<= blockRoom` by
      construction. A budget from the height is a circle; a height from the
      budget leaves the field rules wherever the tallest possible title would
      have put them.
- **Round once.** Positions accumulate in fixed point and round at the end.
  Three truncating divisions put an icon 1.5px low; pre-rounding 2.52px tracking
  to 3 drifted a label ~3px.
- **Fix in the primitives, not the screen.** Every fidelity defect so far was
  found on one screen and belonged in `components.cpp` / `text.cpp` /
  `dither.cpp` / a generator. Special-casing a screen means the next screen
  inherits the bug.
- **WHEN TWO RUNS SHARE A ROW, WHICH ONE TRUNCATES IS A DESIGN DECISION AND IT
  MUST NOT BE THE ONE THE FIRST CALLER HAPPENED TO NEED (#82).**
  `drawHeaderBand` gave the right-hand **value** its measured width first and
  handed the **label** the remainder, which is right for the Library — whose
  label is a subfolder's own name and whose value is a derived count — and
  exactly wrong for Contents, whose value is the **book title**: with `Amusing
  Ourselves to Death` on a real card the screen's own name came out as `C …` at
  480×800 and `C O N T …` at 528×792. **The run that names the screen is the one
  that may never elide**, because unlike a chapter name it is not content, and a
  band that cannot say which screen you are on is worse than a title cut short.
  It is the *third* instance of this shape: the reader header had the priority
  inverted when `CH. 01` became a chapter name, and `drawDetailRow`'s label
  elided at full length until real chapter names went through it.
  - **`labelShare` (`reader/components.h`) IS THE ONE RULE, AND IT IS DECIDED BY
    MEASUREMENT RATHER THAN BY A FLAG**: each run keeps its natural width for as
    long as the other's natural width leaves room for it, and neither may be
    squeezed below half the row they share. That is "the run with slack to give
    up is the one that has more of it", which reproduces what **both** boards
    declare — `Library.dc.html` marks its label as the yielding run,
    `Contents.dc.html` marks its value — with nothing for a caller to remember.
    **A `bool` parameter would be a caller list**, which this file has a rule
    about: seven band call sites, and **five of them have no yielding question at
    all** because both their runs are literals that fit, so five of the seven
    answers would be unverifiable and the sixth would be this defect
    reintroduced. `drawDetailRow` held the second spelling of the old rule and
    shares this one now, pixel-identical for every input a screen can produce.
  - **THE HALF-ROW FLOOR IS THE DERIVED FORM OF `kReadChapterFloor`**, which is
    the same fix one band up and is a **pinned 96** justified by knowing the
    shortest fallback label. A primitive knows neither run's content, so the only
    floor it can derive is an equal division of the row it is dividing. It is a
    **bound, not a rendered behaviour**: it engages only where BOTH runs exceed
    half the row, which no board declares and no screen reaches — the widest band
    label in the firmware is `ABOUT THIS BOOK` at 268px and the value beside it
    is `EPUB`.
  - **`min-width: 0` IS HOW A BOARD SAYS WHICH RUN YIELDS**, and it is the
    vocabulary `Library.dc.html` and `Reader.dc.html` already used: a flex item's
    automatic minimum size is its min-content width, so a `nowrap` run *without*
    `min-width: 0` cannot be shrunk below its text and one with it can. Marking
    one run is the whole of the priority. `Contents.dc.html` declared **neither**,
    so Chrome wrapped the title to two lines into the label while the firmware cut
    the label instead — **both wrong, differently, and invisible on the sheet**
    because the specimen is `MIDDLEMARCH`, which fits. Its `gap: 7px` was missing
    for the same reason and becomes load-bearing the moment the title fills its
    budget. `Bookmarks.dc.html` is the only other board whose value is a book
    title and now declares the same thing, although its screen is V1.1.
  - **AND `WifiPassword.dc.html` HAD THE IDENTICAL OMISSION, FOUND THE IDENTICAL
    WAY — BY REVIEWING A LATER PR WHOSE NEW BOARDS INHERITED THE BAND** (#134).
    It is Contents' case, not Library's: `PASSWORD` names the screen and the value
    is the SSID off the scan. Measured through the real ramp,
    `BT-Hub6-XKQP-5GHz-Guest` is **333px** against **264 (X4) / 312 (X3)** of room
    beside a 161px `PASSWORD`, so it **already came out cut on the device** while
    Chrome without the attributes **wrapped** it, taking the band **64px → 96px**
    at both geometries. The committed `PENDRAGON` is 146px and fits either way, so
    the board is **byte-identical** across the fix and `wifi_password` measures the
    same 3.67%/3.43% (14086/14348 px) before and after — a latent divergence costs
    three attributes and no re-bless, which is worth saying because this one was
    first deferred on an inflated estimate of what changing a compared board costs.
  - **AND IT IS THE ONLY BAND IN THAT FAMILY THE OMISSION CAN REACH, which is what
    stops the next person widening the fix to twenty boards.** Every other band in
    the connect and wallabag flows declares none of the three either and **not one
    of them is wrong**: their values are composed literals — `ON DEMAND`, `5 FOUND`,
    `3 UNREAD`, `SIGNED IN`, `NOT SET UP`, `2 LEFT` — so they are the
    no-yielding-question case above, five of the seven band call sites, arriving
    again at scale. **The question to ask of a bare band is whether its value is
    CONTENT**, not whether it declares the attributes.
  - **THE ONE PLACE THE FIRMWARE DOES NOT FOLLOW THE BOARD IS THE CUT RUN'S
    ALIGNMENT**, and it is deliberate: Chrome keeps the box at the budget and
    left-aligns the truncated text in it, leaving the ellipsis a few pixels short
    of the margin, where the firmware right-aligns the cut run **on** the margin —
    the reader header's own rule for the same run (`right - measure(chapter)`),
    and what keeps a band's right slot flush whatever it holds. It exists only in
    the truncating state, which no board's committed specimen shows.
  - **The proof is a golden and an arithmetic test, not a board state.** The
    boards' committed renders are byte-identical (0 differing pixels, both
    geometries), so `contents` measures **2.30% / 2.11%** before and after —
    8,814 pixels to the digit, the same figure #81 recorded — with `library`
    3.85%/3.54%, `settings` 1.91%/1.76% and `book_details` 3.57%/3.28% as
    controls in the same tree, threshold-at-128 over the `--export` panels. What
    moved is the two `contents_mixed_depths` goldens, whose fixture is a real long
    title, and **every differing pixel is in rows 25–42, the band's one text line,
    with 0 outside it at either geometry**. Same shape as #74's Home wrap, which
    also shipped with goldens and a board declaration and no board state.
- **...AND NOT THE FIRST, EITHER.** The Typography panel's value formatters were
  extracted into `settings.h` while Settings was going to read the same five values
  out; Settings became a single disclosing row instead, leaving one caller, and the
  extraction was undone. "The second copy is the extraction point" is not "extract
  in advance of one" — a shared home for a single caller is a header edge bought for
  nothing.
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

How `App` renders a STACK rather than a screen, the overlay-only partial repaint
and its precondition, the three dither patterns and why they cannot be shared, and
the scroll rail's 14px gutter. **Read it before adding an overlay or a scrolling
list.**

**`docs/notes/overlays-and-lists.md`.**

## The chrome screens

The table of every screen and the one thing about it worth knowing before changing
it, then Settings and the Sleep card in full — the card's four wrapping runs, the
reserve/actual split, and the badge that bounds it. **Read it before editing any
screen that is not the Reader.**

**`docs/notes/chrome-screens.md`.**

## Articles over wallabag

Five cards, one feature: a list on the card, a sync over the radio, and an article
that reads through `openBook` like any other EPUB. The six decisions the code does
not explain by itself, and the one hardware fact that shaped all of them.
`docs/notes/wallabag-api.md` holds the API and the measurements.

**`docs/notes/articles.md`.**

## The battery

The two backends chosen at runtime per board profile, why `-1` is not `0%`, and the
charge latch — the plug-in repaint, its dwell, and the session grant budget.
**Read it with the safety ladder**, which shares its `update()`.

**`docs/notes/battery.md`.**

## The safety ladder (#9, #10)

The low banner and the critical shutdown as two rungs of one mechanism: the
thresholds and why they are the X4's notch table, the hysteresis, the poll cadence
asked for rather than fixed, the resume gate, and `BatteryEmpty`'s two false claims
that only the glass could find. **Read it before changing a battery threshold or
the poll interval.**

**`docs/notes/safety-ladder.md`.**

## Covers

The sleep screen's book cover: why every layer streams, the two planes that serve
three passes, the software 64-bit division found by reading the assembly, the
measured upscale cap, and the stated limits with their incidence. **Read it before
touching image decode or the sleep paint.**

**`docs/notes/covers.md`.**

## The reader

The biggest of these by a wide margin, and the whole EPUB path: six layers each
ignorant of the next, paging and the page ring, the deferred page count, the glyph
cache, reading progress on the card, the lifetime rules that changed, and the stack
and heap budgets a real book runs into. **Read it before changing anything between
the zip and the page.**

**`docs/notes/reader.md`.**

## The table of contents

`toc.h` — the NCX rather than the EPUB 3 nav document, and why that was measured;
`fillTocGaps` for a spine entry no entry names; and the `NOW` marker, which was a
false claim on the majority of real books. **Read it before touching chapter
names**, which four surfaces now draw from one label.

**`docs/notes/table-of-contents.md`.**

## The reader's menu and the chapter list

The menu as assembly rather than new geometry, the four rows cut and why none of
them was cut for room, `drawPanelRow`'s value path with no caller left, and
Contents as Settings' shape with its own rule about section rules.

**`docs/notes/reader-menu.md`.**

## The return anchor

The high-water mark `Up` returns to: one transition rather than three, what the
one rule buys, what it costs, and why the raise lives in `syncVm()`.

**`docs/notes/return-anchor.md`.**

## The peek

The panel of a chapter's text over the page you are on: why it owns a headless
`ReaderScreen`, why the box is the constant and the line count the result, and the
cursor bug that was right about the block and wrong about the line.

**`docs/notes/peek.md`.**

## The typography panel

Five rows over four `Settings` fields, the live specimen, and the re-pagination on
the way out — plus the two leads that are tighter than the face's own ink, and the
second apply the boot path needs.

**`docs/notes/typography-panel.md`.**

## The corrupt-book dialog

Three copy shapes because one sentence would be a lie, the shape with no delete
slab, `Action::replace`, and the wrap-boundary rule `test_book_error_copy.cpp`
makes mechanical for this one screen.

**`docs/notes/book-error.md`.**

## What the shell owes a flow, and two ways it silently owes nothing

A refused push is silent by design and reads as a dead button; `dispatchBack()`
sends a press, so on a latching screen it re-latches. **Read it before adding a
screen that pushes directly from its own `onGesture`.**

**`docs/notes/shell-owes-a-flow.md`.**

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
- **THE BODY FACE USES A THRESHOLD RAMP, CHROME KEEPS THE GAMMA CURVE** (2026-08-24),
  and the split is measured rather than preferred. Judged on the panel, the reference
  firmware's shape reads better for body text: 8-bit coverage to 4 bits, then thresholds
  at (3, 6, 10). **It is not darker** -- 97.7% of the gamma curve's ink on real text,
  94.6% through the whole firmware path. What it removes is the HALO: the gamma curve
  inks a level-1 pixel at coverage **8/255** where this one needs **43**, and that wide
  band of barely-inked edge is what read as haze at reading size. No gamma expresses
  the shape -- it is crisp at the bottom AND dark at the top; gamma 1.0 gets the first
  and gamma 2.5 the second.
  **CHROME MUST NOT FOLLOW, and this is the number that says so.** Small type is mostly
  edge: 44.9% of `Meta400`'s glyph pixels at ppem 21 against 15.7% of the body face's at
  32, so the same ramp costs it **-10.2%** of its ink and `Label500` **-10.4%**, against
  -2.3% for the body. Those two are the hint bar and the row metadata, already at the
  floor this glass holds. Ten percent off them to win 2.3% on the body is the wrong
  trade.
  **Two checks that made the change safe.** Nine goldens moved and only the nine that
  draw body text -- Home, Library, Settings, Contents, Sleep and Book details did not,
  which is what proves chrome untouched. And every changed pixel on those nine is
  ADJACENT TO EXISTING INK (`isolated == 0`), so no glyph moved; the menu's ~120
  isolated pixels are the veil's stipple having already blanked their neighbours.
  Board fidelity went 0.1pp the RIGHT way on all six measurements, so abandoning the
  Chrome-matched gamma did not drift from the boards.
- **THE SDK'S ASYNC OVERLAP CANNOT BE DONE ON THIS HARDWARE** (investigated 2026-08-24,
  not built). `freeink-sdk/docs/deferred-refresh-migration.md` measures "page turn
  1274 ms -> 822 ms by overlapping the grayscale plane rendering with the BW waveform
  via the no-shadow split", and it is the largest single number anywhere in the SDK's
  docs. It does not transfer, for two independent reasons and a third that makes it
  moot:
  1. **Busy staging is unsupported by our drivers.** The mechanism needs
     `supportsBusyGrayscaleStaging()`, whose contract is that the driver "must not touch
     SPI in either `writeGrayscalePlaneStrip()` or `prepareGrayscaleTarget()`". Only
     `PaperMonoDriver` returns true. `Uc8279Driver` (our X3) and `Uc8279X4Driver` DO
     override `supportsStripGrayscale()` and `writeGrayscalePlaneStrip`, so strips work
     -- they just touch SPI, so they cannot run under a BUSY waveform.
  2. **The shadow is unaffordable, and this file already proved it.** We build
     `-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1`, where `displayBufferAsync` allocates a
     **48 KB shadow** so the caller may redraw immediately. The ToC finding above
     measured a **48 KB** allocation against the **45,840-byte** floor: "it failed every
     time". A second frame is 52 KB and worse. `displayBufferAsyncNoShadow` needs the
     frame intact until the wait, which is the opposite of what overlap needs.
  3. **The arithmetic is already spent.** The SDK's figure is against a firmware paying
     the full grayscale sequence per turn. Dithered-first already took that win: our
     turn is ~570 ms, ~478 of it waveform. A perfect render overlap saves the ~92 ms
     render pass and no more -- 16% of a turn, against physics for the rest.
  **AND THE ONE THING THAT LOOKED AVAILABLE IS NOT** (investigated 2026-08-28). This
  bullet used to propose hiding the position save under the waveform: "`displayStart`/
  `displayFinish` deferral, which both our drivers support. The loop blocks ~520 ms per
  turn doing nothing... Hidden under the waveform it could run every turn." **The
  deferral is real and the card write is not allowed in it.** The SD card is on the
  DISPLAY'S bus, and the SDK states the contract in three independent places --
  `PanelDriver.h:70` "the caller does non-SPI CPU work in the gap and issues no other
  bus op until `displayFinish()`", plus `FreeInkDisplay.h:187` and `Uc8253X3Driver.h:57`.
  `Uc8279Driver::displayStart` also leaves a `PARTIAL_IN` window open for
  `displayFinish` to close, and that function's own comment says the DTM1 sync "MUST
  happen while still inside" it.
  **One correction that came out of reading the bus rather than the summary:** every
  `EpdBus` operation is CS-balanced (`beginTransaction` / CS LOW / transfer / CS HIGH /
  `endTransaction`), and `beginTxn()` even drives the co-resident device's CS high --
  so the bus is electrically FREE at this seam. CLAUDE.md's reason for `renderTop()`
  holding `SpiBusGuard` across the whole paint ("the driver keeps the display's CS
  asserted across them") describes the BUSY waits *inside* a driver call, not the gap
  between two. The guard is still right; the stated mechanism was not.
  **It was moot anyway.** The save was moved into `loop()`'s quiet window instead, which
  costs the reader the same nothing, breaks no contract, and reuses the pattern the page
  count, the refinement, the ring warm and the card log all already use. See **Reading
  progress lives on the card**. What is still genuinely available in the gap is
  **non-SPI CPU work only** -- and there is little of it left worth moving.
- **EVERY BUILT SCREEN HAS A GOLDEN NOW** (2026-08-24). Seven of the seventeen did not:
  `reader_menu`, `contents`, `reader_chapter_open`, `reader_list`, `settings`,
  `home_empty`, `library_scrolled` -- checked by unit tests and by `make compare` and by
  nothing that looked at a pixel in CI. Four were the reader family, which is what made
  it urgent rather than tidy: the emphasis work restructured the shared text path, and a
  screen with no golden cannot report a regression in it.
- **A GOLDEN THAT PASSES WITHOUT BITING IS WORTHLESS, so mutate and watch it fail.**
  Every golden added here was proved by breaking the code it defends: blockquote no
  longer italic fails 2, heading no longer centred fails 2, blank rows between blocks
  off fails 4, the menu's `Bookmarks` value removed fails 2 (**that mutation is no longer
available** — the row is cut, and the value path is pinned at the primitive instead; see
the reader-menu section), and the section header's
  positional rule dropped fails exactly 4 -- `contents` and `settings` at both
  geometries and none of the other three. **One of my mutations was a no-op** and looked
  like a passing test: I patched `rowRuleFor` in components.cpp where it lives in
  theme_quiet.cpp, so "0 failures" meant "nothing was changed", not "the goldens are
  blind". Check the mutation landed before believing what it tells you.
  **TWO MORE WAYS A MUTATION LIES, both hit in one session:** it can land on a line the
  input never reaches — breaking the unknown-name path proved nothing about `&nbsp;`,
  which the table *finds* — and a restore can fail to rebuild, because `cp` and the
  previous compile inside the same second leave make thinking the object is current, so
  the fixed source tests as though it were still mutated. `touch` the file, or check
  `git diff` against the binary's behaviour before believing either result.
  **AND A THIRD WAY, which is the sharpest because the mutation was written by a
  REVIEWER to prove a hole existed** (2026-08-28). A review of the Typography work
  made `!justify` also drop the paragraph indent and reported that it passed all
  638,823 assertions — true, and it proved nothing: `indentedAfter` returns false
  for `isFirst`, the test's document was ONE paragraph, so the mutated branch could
  never differ. The prescribed fix — "compare more fields" — was then a fix to the
  ASSERTION when the hole was in the FIXTURE: `CHECK(r.x == j.x)` was comparing
  `18 == 18`, correct and unable to reach the line it was added to defend. What
  closed it was a second document whose second paragraph IS indented, plus a
  `REQUIRE` in front asserting the fixture still reaches the indent. **A mutation
  that fails nothing tells you about your INPUT before it tells you about your
  test**, and the same session produced two more instances: a ring-eviction
  mutation invisible because the walk refilled the ring, and a last-line
  justification mutation invisible because the specimen's last line was under the
  fill threshold anyway.
  **AND A FOURTH, WHICH DESTROYS THE WORK RATHER THAN LYING ABOUT IT: `git checkout`
  TO UNDO A MUTATION IN A FILE YOU HAVE NOT COMMITTED.** It reverts the file to HEAD,
  which is the mutation *and the change being tested* — so the next run reports
  numbers for code that no longer exists, and reports them as a pass. Caught during
  the anchor rewrite only because the following build behaved impossibly. **COMMIT
  BEFORE YOU MUTATE**, and restore with a `cp` of a backup taken before the edit, never
  with `git checkout`. The three ways above make a mutation lie about the TEST; this
  one makes it lie about the SOURCE, and it is the only one that also loses work.
- **A SCRIPTED REPLACE WITH NO COUNT REWROTE A FUNCTION INTO A CALL TO ITSELF**, and
  it reached the device as a stack-protection fault. Rewriting the call sites
  `anchor_.jumped(from, here())` into `anchorJumped(from)` used `s.replace(a, b)`
  without a count -- and the new helper's OWN BODY was character-for-character one of
  those call sites, because it used the same parameter name `from`. So it replaced
  itself with a call to itself. Its two siblings escaped ONLY because their call sites
  happened to say `fromNext` and `fromPrev`. (All three helpers are gone — the anchor
  has ONE transition now; see **The return anchor**. The lesson is about scripting and
  survives them.)
  **873 TESTS PASSED OVER A FUNCTION THAT COULD ONLY EVER RECURSE**, because nothing
  exercised `goToChapter` -- the jump, which is what Contents does. Two lessons, and
  the second is the one that costs:
  - **Count every scripted replace, and check the anchor is not inside what you just
    wrote.** This is the THIRD instance today: a note quoting the string it documented
    was the first match; a helper's body was a call site; and earlier a stale guard
    survived a replacement that only touched the `return`. The family is always the
    same -- a pattern matching more than was meant.
  - **A helper extracted from N call sites needs a test per call site, not per helper.**
    The extraction looks like one change and is N+1.
- **THE CRASH DUMP NAMED THE BUG IN THREE LINES.** `RA` repeating with an unchanging
  frame pointer in 16-byte frames all the way down 16 KB is infinite recursion and not
  a deep call tree, and `riscv32-esp-elf-addr2line -pfiaC -e .pio/build/xteink/firmware.elf
  <MEPC> <RA>` resolved it to the function and line. Check the report's
  `ELF file SHA256` against `shasum -a 256 .pio/build/xteink/firmware.elf` FIRST -- it
  matched here, which is what made it worth debugging rather than reflashing.
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
for a screen on the grayscale path, and **this line said "nothing uses it today"
through THREE separate screens acquiring it**: the Reader on 2026-08-22, then the
peek and the sleep covers on 2026-08-29. It is `test_theme_reader_golden.cpp`,
`test_theme_peek_golden.cpp` and `test_theme_sleep_cover_golden.cpp` today.

**TWO OF THOSE THREE FILES EACH CALL THEMSELVES ITS FIRST CALLER IN A COMMENT**, and
the later one is simply wrong — `git log -S"checkGoldenGray(lsb, msb"` settles it in
one command and is the check to run before writing "the first" about anything here.
The pattern is this file's own: a note that says *nothing uses this yet* is true when
written and is nobody's job to revisit, so the next author reads it, believes it, and
writes the same sentence again. **"Nothing uses it today" is a claim with an expiry
date and no owner** — prefer naming the callers, which goes stale loudly.

**The check that made the last two re-blesses trustworthy** was not a visual one:
compose `Plane::Lsb` and `Plane::Msb` into the 4-level coverage map (that map's
level per pixel *is* the coverage the renderer computed), then assert that **no
pixel of coverage 0 or 3 moved**. Both re-blesses came out at exactly zero, which
proves the header band, every rule, the cover's dither block, the progress bar,
the inverted block and row fields and every hint-slot position are bit-identical,
without needing to trust an eyeball on 384000 pixels. Note the level→byte mapping
is `0..3 → white..black`, so **byte 170 is coverage 1 and byte 85 is coverage 2** —
easy to get backwards, and it inverts the conclusion if you do.

## The board

`https://github.com/users/Rukkaitto/projects/1` — the only index of this project's
deferred work: the five fields, the six stages with their two entry doors, what an
agent may and may not close, and the field ids. **Read it before filing or moving a
card**, and note that neither direction of the issue/card link is automatic.

**`docs/notes/the-board.md`.**

## Graft, and the one query it cannot answer here

`graft/` indexes this repo and `.claude/skills/graft/SKILL.md` tells you to reach
for it before grepping. Do — `ask`, `grep`, `skeleton` and `map` all earn their
place on this tree. **But `graft callers` returns nothing across files here, and
it is the query this project's own rules ask for most** (`--depth all` before a
refactor, `--depth 2` before a rename).

The cause is C++ rather than the tool: a free function is DECLARED in a header
and DEFINED in a `.cpp`, so its name is ambiguous, and graft drops an ambiguous
cross-file edge instead of guessing. Same-file edges resolve fine —
`PageBuilder::add` correctly shows `layoutPage` calling it, both being in
`layout.cpp`. `drawBadge` shows neither of the two callers this file names, and
neither does `veilRect`, `Focus::move` or `ScrollWindow::slice`.

**Use `graft grep <symbol>` for a blast radius.** It is exhaustive and groups
hits by enclosing symbol, which is the shape a caller list wants anyway.

**`--lsp` does NOT fix it and the lever is spent** — `clangd` is on `PATH`,
`cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` writes the 164-entry
desktop compile database, and the edges are unchanged, because the ambiguity rule
sits above the LSP layer. `shell/` has no desktop compile database at all, so
`main.cpp` was never in reach of it either way.

**What makes it usable anyway is that it refuses out loud**: every empty answer
names the ambiguity, says it may undercount, and points at `graft grep`. That is
the distinction this file draws everywhere else — an absent claim beats a false
one — and it is why the tool is wired in rather than removed. A `callers` that
had answered a confident empty set would be the card probe answered from cache
again.

## Where to look

`docs/superpowers/plans/2026-08-20-v1-roadmap.md` — phases, and two sections
worth reading before starting anything: "What Phase 2A-2 established" and the
on-device bring-up findings.

`docs/on-device-smoke-checklist.md` — **what only the panel can be wrong about**,
grouped by failure class rather than by screen: the five byte-wise primitives
that are invisible to the whole desktop under rotation, the overlay stack, wake
versus USB reset, the card probes, the refinement, the battery latch. This is
what a card's move from `On glass` to `Done` is evidence of, and it is the list
to run after touching a drawing primitive, the paint sequence, storage or power.

`docs/releasing.md` — what a release is here (an annotated tag on `main`, which
`.github/workflows/release.yml` then turns into a published release), the gate in
order, what the three attached images are for, and the blocker list as a **query
rather than a written list**, because a list here would be a second copy of the
board. It also records why there is deliberately no `CHANGELOG.md`.

**THAT QUERY IS `tools/release_blockers.py --release <R>` NOW, AND IT WAS A
`gh ... | jq` PIPELINE THAT COULD NOT FAIL.** It passed `--limit 100` against a
board that reached 114, and `gh project item-list` truncates **silently** — so
the release gate was reading a prefix of the board and reporting a verdict.
Measured when it was replaced: **6** V1.1 blockers seen where there were **17**,
the issue asking for the fix among the eleven dropped. Three things it now does
that a pipeline could not, each the shape this file records elsewhere: the size
is **asked for** rather than capped (`totalCount` does not shrink with
`--limit`, so there is no constant to outgrow and a disagreement is a refusal);
an **unknown release name is an error**, not the empty answer a typo would
otherwise turn into a pass, which is `compare-design.py`'s `--only` rule; and
the three outcomes are **three exit codes** (0 clear, 1 blockers, 2 could not
answer), because `gh ... | jq ... | sort` reports `sort`'s status and a `jq`
that refused mid-stream leaves a pipeline that printed nothing and exited 0.
`tools/test_release_blockers.py` is its test, plain `python3` with `gh` stubbed,
not wired into `make test` for `test_release_notes.py`'s reason.

`README.md` — the outward-facing one: what works, what is stated-refused, how to
back up the stock firmware before flashing, and what "written with Claude Code"
means for someone about to run this on their own reader.

## Agent skills

### Issue tracker

GitHub Issues on `Rukkaitto/encre`, via the `gh` CLI — with the project board's
own rules about what an agent may close. See `docs/agents/issue-tracker.md`.

### Triage labels

The five canonical roles, each label string equal to its name. See
`docs/agents/triage-labels.md`.

### Domain docs

Single-context: `CONTEXT.md` and `docs/adr/` at the repo root, neither of which
exists yet. See `docs/agents/domain.md`.
