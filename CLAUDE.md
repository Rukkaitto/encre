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
make compare    # design-vs-firmware contact sheet, all 37 boards (~2.8 min)
                # ...and it prints `ok`, NOT a percentage -- see #41
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

`.github/workflows/ci.yml`, three jobs on every PR and on pushes to `main`,
cancelling a ref's own earlier run. `test` is `make test` on a bare checkout --
no submodule and no Python, because every generated asset is committed.
`firmware` is the only thing anywhere that compiles `shell/`; it checks out
submodules (an empty `freeink-sdk/` fails with `PackageException: not a
directory`, which names neither the submodule nor the fix) and caches the ~1 GB
toolchain.

**THE `compare` JOB IS A NARROW GATE AND IS NOT A FIDELITY CHECK.** It fails on
three things: a board named in `compare-design.py` and absent from disk, a screen
id named by **two** rows of those tables (#77 — it would be rendered and counted
twice), and a screen the SIMULATOR KNOWS that will not render. It does **not**
measure how close the render is -- the sheet still prints `ok` rather than a
percentage, which is #41. A board with no screen behind it stays fine; that is
**five of the 37** — measured, not inherited: a full run with the gate on reports
`32/37 screens implemented` and exits 0 (Bookmarks, Boot, Home / missing book,
Names, Names / empty). **Both figures move whenever a board lands** — this line has
said 36 and 31; `BookErrorMemory.dc.html` is what took them to 37 and 32, and the
five with nothing behind them are unchanged. Note the denominator read **37 before
#77 as well**, and for the opposite reason: the extra row there was one board counted
twice, not a thirty-seventh board.

**Wiring it at all needed the script to be able to fail.** `render_sim` returned
a bare `None` for both "the simulator has never heard of this id" and "the
simulator knows it and crashed", so a broken subcommand printed
`firmware not implemented` and the run exited **0** -- the same
reports-on-less-than-it-claims shape as the card probe answered from cache and
the `make compare` default that skipped four screens. It returns a status now,
and `--require-implemented` fails on the second. The flag is **off by default**,
so comparing mid-implementation is unaffected; CI passes it. Proved by mutation:
breaking `home` in the simulator takes the gate to exit 1 naming both
geometries, while `--only boot` (a real board with no screen) stays green.

`$CHROME` overrides the board rasteriser's path, which was hardcoded to macOS
and cannot exist on a Linux runner, and `$CHROME_FLAGS` carries a runner's
`--no-sandbox` -- set by the workflow that knows it is one rather than by
sniffing `$CI` in the script, so a developer's Chrome keeps its sandbox.

**BRANCH NAMES AND COMMIT SUBJECTS ARE ENFORCED ON PRs**, by
`tools/check_conventions.py` -- runnable as `make conventions`, which is the
point: a convention enforced only by CI is one you are told about after pushing,
which is the worst moment to be asked to rewrite a commit message.

**Commit subjects are Conventional Commits with the ELEVEN STANDARD TYPES**
(`feat fix docs style refactor perf test build ci chore revert`) and a free-form
scope. **The house style writes the SUBSYSTEM as the type** -- `peek:`,
`design:`, `reader:`, `shell:` -- and that is a scope wearing a type's clothes:
`feat(peek):` says the same thing, validates against a stock config, and carries
the one bit the bare area name never did. Measured when this landed: **358 of
main's 513 subjects already passed**, and of the 155 that did not, **128 failed
that one way** and the remaining **27 were merge commits**, which are exempt
because git wrote their subject. **History is not re-litigated** -- the check
runs on the commits a PR adds.

The scope vocabulary is deliberately **not** restricted (a list of allowed
scopes needs a line per subsystem and conflicts every time a screen lands), and
subject **length** is not enforced (Conventional Commits says nothing about it
and this project writes long explanatory subjects on purpose).

**Branch names take git-flow's vocabulary plus `claude/`.** `feature` `bugfix`
`hotfix` `release` `support` `chore` `docs` `ci` `refactor` `test` `perf`, then
`/<lowercase-slug>`. **`claude/` is in the list because Claude Code NAMES ITS
OWN BRANCHES**, so a pattern without it rejects every agent branch -- including
the one that added the check -- and buys a rename before every PR rather than
any clarity. **There is no `develop` branch and this does not invent one**: full
git flow is a change to how the project is developed, not a CI check.

**THE SAME CHECK RUNS AS TWO GIT HOOKS**, tracked in `.githooks/` and installed
by `make hooks` (one `git config core.hooksPath`, which lives in the common
`.git/config` and so covers every worktree at once). `commit-msg` validates the
subject you just wrote, when the fix is `git commit --amend` rather than an
interactive rebase; `pre-push` validates the branch name and every commit the
push would add. Both run `tools/check_conventions.py`, so they cannot drift from
the gate they mirror, and both are bypassable with `--no-verify` **by design** --
they are a fast local mirror, not a second source of truth.

**`commit-msg` ALLOWS `fixup!` AND PUSH AND CI DO NOT.** `git commit --fixup`
writes one, and it is a legitimate local state whose whole purpose is to be
squashed later; rejecting it at commit time would break the workflow. It stays
rejected at the two moments it must not survive. The hook is also skipped for a
merge, a revert and a cherry-pick, whose messages git wrote.

**`pre-push` TAKES ITS RANGE FROM GIT'S STDIN, NOT FROM `origin/main..HEAD`.**
git hands the hook the remote sha it negotiated for each ref, live; a
remote-tracking ref can be STALE, and a stale one drags already-merged history
into the range -- where **128 of main's commits predate this rule** and would
fail it. For a branch the remote does not have yet that sha is all zeros, and
the fallback is "commits on no branch of this remote".

**AND THE NEGOTIATED SHA ALONE WAS NOT ENOUGH: IT SAYS "SINCE THE REMOTE'S TIP OF
THIS BRANCH", WHICH AFTER `git merge origin/main` INCLUDES EVERY COMMIT THE MERGE
BROUGHT IN** (#71). One non-conforming subject on `main` therefore rejected the
push of a branch that did not write it, and poisoned every future merge until the
base moved past it -- while `make conventions` passed on the same tree at the same
moment, because `origin/main..HEAD` excludes exactly what the merge brought. **The
two arms of the range also DISAGREED**: a never-pushed branch took the `--remotes`
fallback and passed, and the same graph after one push failed. Both arms now
exclude what this remote already has (`$local_sha --not --remotes=$remote
$remote_sha`), and the exchange is stated where it is made, in `.githooks/pre-push`:
the negotiated sha is **still** an exclusion so nothing local can WIDEN the range
back into merged history, a stale ref is one that is BEHIND and so excludes FEWER
commits than the truth -- the direction that cannot hide anything -- and **a merge
can only bring in what a local ref points at**, so excluding the tracking refs
excludes exactly what was merged. What is given up: a tracking ref AHEAD of the real
remote (the remote branch rewound since the last fetch) would exclude commits being
pushed, which were on the remote once and were checked by the push that put them
there.

**THE PR TITLE IS THE SUBJECT SQUASH-MERGE ACTUALLY WRITES, AND NOTHING CHECKED IT.**
The gate runs on commits; GitHub's squash-merge writes the PR **title** as the merge
commit's subject, so the title reached `main` by a path none of the three checkers
covered -- **five** of `main`'s subjects got there that way, and the issue was filed
when there were two. `check_conventions.py --subject` is its own mode (a title is a
string, and `--message-file` SKIPS lines beginning with `#`, so a PR titled `#71 …`
would have read as an empty message and passed), an empty value is an **error**
rather than a pass, and `--allow-fixup` does not reach it -- there is no later squash
to absorb a `fixup!` when the squash IS the merge.
`.github/workflows/pr-title.yml` runs it. **Its own workflow, not a fifth job in
`ci.yml`**, and both reasons are the `edited` trigger type: a title is fixed after
the PR opens and `edited` is not in `pull_request`'s defaults, so without it a
corrected title stays red; and `types` is settable only per workflow, where `ci.yml`'s
`cancel-in-progress` concurrency group would let a title edit cancel a running
firmware build.

**PUSHING AN ANNOTATED TAG PUBLISHES A RELEASE**, `.github/workflows/release.yml`
— its own workflow for pr-title's reason one trigger over, since `on: push:
tags:` in `ci.yml` would drag `test`, `firmware` and `compare` onto every tag,
and because it is the one thing in `.github/` that is not `contents: read`.
`docs/releasing.md` is the gate it sits at the end of; the tag records the
judgement and this only carries it out. **It re-runs no tests** — CI already
answered on that commit, and it cannot answer the two steps that decide a
release, both of which need the hardware.

- **GITHUB'S TAG TRIGGER HAS NO BRANCH FILTER**, so "a tag pushed on main" is a
  claim a workflow has to CHECK: `on: push: tags:` fires for a tag on any commit
  in the repository, a never-merged branch included. `git merge-base
  --is-ancestor` against a freshly fetched `origin/main` is what makes the name
  honest, and it is a hard **failure** rather than a quiet skip — a silent no-op
  is indistinguishable from a workflow that did not run, and tagging a stale
  commit is a mistake somebody wants to hear about. The tag must also be
  **ANNOTATED**, which `docs/releasing.md` has always said a release *is* and
  nothing checked.
- **THE FLASH OFFSETS ARE READ FROM `partitions.csv`, NOT PINNED IN THE
  WORKFLOW**, which is the first invariant applied to a file rather than a board:
  that table's own comment says app0 staying at `0x10000` is what keeps an
  ordinary upload landing correctly, so a second copy of it in YAML is the drift
  this project keeps paying for. Only the C3's `0x0` bootloader offset and
  ESP-IDF's `0x8000` partition table are stated there, both being the platform's
  rather than ours. Three assets: the app image, a merged image flashable at
  offset 0, and **the ELF** — a panic from a released build is undebuggable
  without the byte-identical one, and no rebuild months later will match its
  `ELF file SHA256`.
- **THE X4 CAVEAT RETIRES ITSELF.** `docs/releasing.md` instructs that the notes
  must say a version number does not imply the X4 works (#23), and an
  instruction like that carried out by a generator is exactly this file's most
  expensive recurring shape — a claim with an expiry date and no owner, still
  printed the day #23 closes. `tools/release_notes.py` asks the tracker instead,
  and **fails toward INCLUDING it** when the query cannot be made: a limitation
  stated once too often is a smaller wrong than a real one dropped silently.
- **`tools/release_notes.py` HAS ITS OWN TEST**, `tools/test_release_notes.py`,
  plain `python3` against throwaway repositories, not wired into `make test` for
  `test_check_conventions.py`'s reason. The logic lives there rather than in
  YAML because **logic in a workflow is logic nothing tests** — `shell/`'s
  problem in another directory — and the two edges are both `git`'s answers
  rather than formatting: the FIRST tag has no predecessor (`git describe`
  exits non-zero, and that is v0.1.0 rather than an error) and merges are
  excluded (a squash-merge already arrives as one commit with the PR title as
  its subject; what `--no-merges` drops is `git merge main`, which git wrote and
  `check_conventions.py` exempts for the same reason). Every guard is proved by
  mutation, and writing the empty-range case took two goes: the obvious fixture
  — two tags on one commit — cannot reach it, because `describe` is asked of the
  tag's PARENT and skips both, and `commit-tree -p X -p X` collapses to one
  parent, so the "merge" it builds is an ordinary commit that `--no-merges`
  keeps. A mutation tells you about your INPUT before it tells you about your
  test.

**`tools/test_check_conventions.py` IS THE CHECKER'S OWN TEST**, plain `python3`,
**deliberately not wired into `make test`** for `tools/test_compare_design.py`'s
reason -- the fast loop builds on a bare checkout with no Python. It drives the real
`.githooks/pre-push` against real throwaway repositories, because the defect was in
the range the hook computes and not in the regex, and it carries the BEFORE range as
a mutation with an `assert` that the line it patches still exists, so a reworded hook
fails loudly instead of quietly measuring the shipped one twice. It also keeps the
**blind** mutant -- a range selecting nothing -- because "let the merged commits
through" and "let everything through" both make the symptom go away and only one of
them is a fix.

**AND NONE OF IT IS BLOCKING ON GITHUB TODAY.** Branch protection answers
`403: Upgrade to GitHub Pro or make this repository public`, so the check cannot
be made a required status check: a violation shows a red X on the PR and the
merge button still works. **The hooks are currently the only thing that stops
anything**, which is why they exist rather than being belt-and-braces. **That
applies to the PR-title job too**: it cannot block a merge, so what it buys is the
verdict in front of the one person who can still edit the title, at the moment they
can still edit it.

**AN EMPTY COMMIT RANGE IS AN ERROR IN CI** (`--require-commits`), because a
wrong base ref would otherwise check nothing and pass -- the
reports-on-less-than-it-claims shape again. It is only a note locally, where a
branch with no commits yet is an ordinary state.

**CI'S FIRST RUN FOUND A REAL PORTABILITY BUG, AND IT WAS NOT THE GOLDENS.**
`test_scalablefont.cpp` called `std::memcmp` without including `<cstring>`:
libc++ pulls it in transitively and libstdc++ does not, so the file had compiled
on macOS for months and **failed on the first Linux build**. A
transitively-satisfied include is a bug only the other toolchain can see, which
is the whole argument for building somewhere other than the machine that wrote
the code. That first build died before `ctest` ran, which left the
goldens-under-gcc question postponed rather than answered -- **and it is answered
now: it has been green ever since.** See the paragraph below.

**A `\x1f`-SEPARATED `git log` MUST NOT BE `.strip()`ed.** Python counts `\x1f`
as whitespace, so a bare `.strip()` ate the trailing empty field of the last
line -- the ROOT commit, the only one with no parents -- and the parse crashed
on it. Found by running the checker over the real 513-commit history rather than
over its fixtures, every one of which had a parent.

**A GOLDEN IS NEVER RE-BLESSED TO MAKE CI GREEN.** A failing golden uploads its
`build/<name>_candidate.png` as an artifact precisely so the pixels can be
looked at, which is the only way to tell an intended change from a regression.
**The goldens were blessed on macOS/clang and this job is Linux/gcc, AND THAT
QUESTION IS NOW ANSWERED: they pass under both.** It was recorded here twice as
open -- the reasoning being that layout accumulates in fixed point and should be
bit-identical while `stb_truetype`'s rasteriser is float -- and every `Tests and
goldens` run on `main` since has run `ctest` on `ubuntu-latest` and gone green.
The float rasteriser agrees across the two toolchains at these ppem values.
**Nothing was done to make that true, which is why nobody came back to say it
had become true** -- a question with an expiry date and no owner, the same shape
this file records for "nothing uses it today". If a run ever does redden on
goldens alone, the candidates are the evidence and the fix is to move the job to
`macos-latest`, not to bless anything.

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

The budget from the button going down to the panel starting to move, measured or
derived on the X3. **The waveform is not the interesting part** — it is 389 ms
(DU) or 693 ms (GC) and there is no third bank; `RefreshMode` has only
`FULL`/`HALF`/`FAST`, `HALF` maps to GC, and there is no A2. Everything *before*
it is what was worth attacking, and it was ~150–1200 ms depending on the screen.

| stage | cost | where |
|---|---|---|
| press → the input task sees the edge | 0–10 ms poll + ~6 ms debounce | `kPollMs`, the SDK's `DEBOUNCE_DELAY = 5` needing a later sample |
| ~~down → release~~ | ~~80–200 ms~~ **gone** | see the `Short`-on-down rule above |
| queued → the loop looks | ~~0–10 ms~~ **~0** | `waitForRawSample(10)`, which was `delay(10)` |
| pre-dispatch card work | 0 / ~100 ms / **~1100 ms** | details author `openBook`; a `/books` listing |
| dispatch + `saveWhereWeAre` (NVS) | ~2–15 ms | one small `putString` |
| `saveReadingPosition` (2 SD writes) | ~20–100 ms, on Back-from-a-book and chapter crossings | |
| render | chrome ~40 ms, an overlay ~3× that, a reader page 125–165 ms | `[paint] render=` |
| plane write, 52,272 B at 20 MHz | ~21 ms | `Uc8279Driver::displayStart` |
| **the waveform** | **389 ms / 693 ms** | `BW_DU` / `BW_GC` |
| DTM1 sync, a second 52 KB write | ~21 ms, *after* the image is on glass | `displayFinish` |

Desktop render, `reader_sim <screen> out.png --canvas 528x792 --bench 200`, µs a
pass warm: home 252, library 292, settings 208, contents 199, book_details 116,
library_actions 698, reader_menu 764, reader (dithered) 474. **An overlay costs
~3× a bare screen** because it renders the parent, the veil and the panel; that
is what `App::renderTopOnly` exists to avoid on a focus move, and it does not
apply to the push that opens one.

### Reading a run off the device

**One `[i]` line per interaction**, from the button going down to the panel being
finished with it. It exists because the cost used to be spread across log families
that could not be added up — `[input]` said a press happened, `[paint] done` said
what the panel cost, and everything between them had no line at all, which is how
a second of directory listing sat on the critical path of a Back with nobody able
to name it.

```
[i] #12 CONFIRM SHORT from=home to=library ev=1 | wait=7 pre=0 disp=1103 post=2
        render=41 up=24 wave=712 | total=1889ms ser=31 net=1858
```

`tools/latency.py run.log` groups those by where the press landed and reports
medians. **Median, not mean**: one interaction that caught the card doing internal
housekeeping drags a mean somewhere no press ever was.

| field | is |
|---|---|
| `wait` | the event's own timestamp to the loop picking it up — raw queue, loop wake, drain |
| `pre` | work done because of what is on top, *before* the dispatch |
| `disp` | `App::dispatch` — **a push builds a screen, so a Library rescan is here** |
| `post` | what the dispatch made necessary: `handleOpen`, a chapter jump, Home's rebuild, the session record |
| `render` | drawing the frame, all passes |
| `up` | the plane upload to the controller, *before* the waveform starts |
| `wave` | the waveform, plus the ~21 ms baseline sync that follows it |
| `ser` | how much of `total` was this device talking to the USB host |
| `net` | `total − ser`. **The number to compare across runs.** |

**A BURST IS ONE INTERACTION.** Several events can drain before one paint — that
is the coalescing the loop exists to do — so the line reports the FIRST event's
timestamp against the paint that satisfied it, with `ev=N`. Reporting per event
would divide one visible response between N lines and make every one look fast.
`paint=none` marks a press that changed nothing, which is exactly the case worth
having a line for.

**`ser` EXISTS BECAUSE WATCHING THE DEVICE CHANGES IT.** `HWCDC::write` posts what
fits the TX ring and then **blocks** until the host takes the rest; `flush()` spins
`delay(1)` until the ring empties, up to 100 ms. Unplugged, both short-circuit on
`!isCDC_Connected()` and cost microseconds — which is the device's real behaviour,
since it lives on battery, and is why this was never noticed. So **every timing
taken over USB is inflated by the cable**, and this project has already paid once
for a measurement artefact read as a device fact (a `delay(2500)` in `setup()`
recorded as the panel detection's cost, because the first *timestamped* line was
read as time zero). Every print site in `shell/src/main.cpp` goes through `logf()`
/ `logFlush()`, which time themselves into `gLogMs`; `ser` is the delta across the
interaction. Unplugged it reads ~0 and `net == total`, which is the proof that the
numbers either side of it are the device's own.

**`up=` and `wave=` are a split, not a change.** `showOnePass` calls
`triggerDisplay` then `completeDisplay` where it used to call `displayBuffer`, and
for the configuration this firmware builds those are the same two driver calls with
the seam exposed — `Uc8279Driver::display` is literally `displayStart` then
`displayFinish`; single-buffer means both take the `prev == nullptr` branch; and
`_inverted`/`_inversionDirty` are false forever because nothing calls `setInverted`,
which is what kills `triggerDisplay`'s fall-back guard and `displayBuffer`'s
FAST→HALF promotion. What it buys is the one division the log could not make: `up`
is ours and bounded by the 20 MHz SPI clock, `wave` is the panel's.

**`[render] total=… fill=…/N glyph=…/N …`** is the second half of `[paint] done`
and breaks a render down by PRIMITIVE — see "Which primitive spent the render"
below for what it found. `reader::Profile` (`core/include/reader/profile.h`) is
five slots and an injected clock: `core/` has no clock and must not acquire one,
so the shell installs `micros()`, the simulator installs `std::chrono`, and a
build that installs neither pays a load and a branch. Two clock reads per
primitive CALL, never per pixel.

**AND THERE IS A LOG ON THE CARD, because the cable changes the device.**
`logToCard` in `/.reader/settings.json` (off by default, hand-edited — it is a
diagnostic, not a preference, so it has no Settings row and needs no board) tees
everything `logf()` writes to `/encre.log`.

It exists for the one class of fault serial cannot see. `HWCDC::write` and `flush`
short-circuit unplugged and BLOCK when a host is attached, so a timing taken over
USB is not the device's — and attaching after a sleep can reset the chip, turning
the wake being investigated into a cold boot. **A fault that only happens unplugged
is not observable over the wire at all.**

**AND IT HAD NEVER RUN ONCE, THROUGH TWO PHASES OF THIS FILE DESCRIBING IT AS
WORKING INFRASTRUCTURE (#47, #69).** `gLogToCard` was read at four sites in
`shell/src/main.cpp` — the buffer append, the idle flush, the flush before sleep and
the `[alive]` line — and **assigned at none**; `git log --all -S"gLogToCard =" --
shell/` was empty for the whole life of the feature. `core/` parsed the key into
`Settings::logToCard` and the shell never consulted it, so the 4 KB buffer, the idle
flush, the dropped-byte counting and the 256 KB cap were all unreachable. Confirmed
on an X3: `"logToCard": true` applying correctly on the `[boot] settings in force:`
line and no `[log]` line on any `[alive]`, across a full session. **The same shape as
`ListRow::trackingEm1000` and `readerBookTitle_` — a reader with no producer** — and
it was found twice, from two directions, because a diagnostic nobody can turn on
looks exactly like a device with nothing to report.

- **THE FIX IS NOT THE ASSIGNMENT; IT IS WHEN THE QUESTION CAN BE ASKED.** The
  setting is on the CARD, so `loadAndApplySettings()` cannot run before the mount —
  and the lines this feature exists to capture all print before it: `[wake] refused`
  / `[wake] held` (~470 lines earlier), the `[prev]` crumb record, `[boot] reset
  reason=…`, and the storage bring-up itself. A tee armed at the load drops exactly
  the boot it was wanted for. **Nothing can be WRITTEN that early either**, since
  there is no mounted volume, so the only question is whether those lines are still
  in RAM when a flush first becomes legal.
- **SO THE TEE HAS THREE STATES AND STARTS ARMED**
  (`reader::CardLogBuffer`, `core/include/reader/card_log.h`): `Pending` buffers and
  may not write, `Enabled` keeps what `Pending` accumulated, `Disabled` discards it
  and stops. **Buffering by default and discarding is the cheaper of the two
  orderings** — a memcpy per line into a static array that exists either way, against
  a second buffer or a replay mechanism — and it is the only one that can keep a line
  printed before the file was read.
- **AND THE BOOT PREAMBLE IS FLUSHED THE MOMENT THE SETTING IS KNOWN**, in
  `loadAndApplySettings()`, rather than being left to `loop()`'s idle window. Boot
  does not fit in 4 KB: the next legal flush is after the first paint, thousands more
  bytes of stage lines, font timings, library scan and session restore later, so
  without it the file would open with a HOLE precisely where the wake diagnostics
  are. It is safe there for the settings file's own two reasons — the card is mounted
  and nothing has been painted.
- **IT IS `core/`'s LOGIC AND THE SHELL'S ARRAY.** Arming, appending, drop counting
  and the flush threshold are bytes in and bytes out, and `shell/` has no harness —
  five bugs have hidden there. The shell keeps the 4 KB (nothing in `core/`
  allocates) and owns the card write, which is the only part a desktop test cannot
  reach. `applySetting` is idempotent for the same answer, because
  `loadAndApplySettings()` runs a **second** time on the RETRY path and a re-arm that
  discarded would throw away the session so far.
- **THE STATED LOSS IS THE RETRY PATH.** A device that booted with no card decided
  *off* and threw the boot buffer away; if the card that then appears asks for a log,
  the tee arms from that point and the preamble is gone. At the moment the question
  was asked, the default was the only answer available — so the `[log] armed late`
  line says which of the three transitions happened rather than leaving them alike.
- **`logToCard` IS ON THE `[boot] settings in force:` LINE NOW**, and its absence was
  the other half of #69: it was the one field in the struct with no line reporting
  it, so a card asking for a log and a firmware ignoring the request looked
  identical, which is how the request went unimplemented for two phases. **An
  instrument that reports on less than it claims is worse than none** — the card
  probe answered from cache, the `make compare` default that skipped four screens,
  and this.
- **A FLUSH SAYS WHETHER IT LANDED**, because `appendToCard` can fail on a card that
  reads and refuses writes and the bytes are dropped either way: `[log] wrote NB` and
  `[log] COULD NOT WRITE NB` are separate claims, and a line reporting a write that
  did not happen is the false-claim shape this file refuses for the battery gauge and
  the sleep badge.
- **WHAT ONLY THE PANEL CAN ANSWER**, and it is the whole feature: that `/encre.log`
  appears at all, that it opens with the pre-settings lines, and that the ~40 ms boot
  flush does not cost anything visible. `shell/` has no harness, so 1,363 green test
  cases say nothing about any of it.

- **IT MUST NOT MAKE THE DELAY IT IS HUNTING**, which is the whole design. A card
  write costs ~40 ms and takes the DISPLAY'S SPI BUS, so one per line would put tens
  of milliseconds into every interaction and be indistinguishable from the fault. It
  buffers 4 KB in RAM and flushes **only when the panel and the buttons are both
  quiet** — the gate `pollCardPresence` already uses.
- **IT REPORTS ITS OWN WEIGHT**: `[log] wrote NB in Xms` per flush and
  `buffered/dropped/sdTotal` on `[alive]`. Same reason `ser=` exists — an instrument
  that hides its cost lets you attribute it to the device.
- **A DROPPED LINE IS COUNTED, NEVER SILENT.** An overrun between two idle windows
  leaves a HOLE in the log, and a hole must not read as the device having gone quiet.
- **It flushes on the way into sleep**, after `markSleeping()` — the flag is what the
  next boot needs and the log is only what a human needs, so the ordering says which
  one may not be lost.
- **`appendToCard` is a free function in `shell/`, not a `FileSystem` method.**
  `reader::FileSystem` has no append and should not grow one for this: the contract
  is 27 clauses driven by two harnesses, and widening it means widening both for
  something `core/` will never call. It also deliberately does NOT ask
  `SdFileSystem::mounted()` — the reason that object thinks the card has gone is
  exactly the kind of thing worth having in the log.
- The file is capped at 256 KB and restarted past it, so a device left running
  cannot fill the card. Losing the oldest half beats refusing to write, which loses
  the newest.

**`[fs] list … (N.NN ms/entry)`** over 15 ms is the other half. `[i]` can say a
Confirm on Home spent two seconds in `disp=`; only this says the two seconds were
one `list()` over 406 entries. The ~2.7 ms an entry quoted throughout this file is
a figure from one session that has never been re-checked against the card in the
slot, and where a number decides a design this project's rule is to measure the
thing rather than argue about it.

**THE BIGGEST REMAINING NUMBER IS A DIRECTORY LISTING, AND IT IS NOT THE PANEL'S
FAULT.** `SdFileSystem::list` costs ~2.7 ms an ENTRY on the user's card, and a
203-book library is 406 entries because macOS writes a `._name` beside every
file — so **~1.1 s**, paid on the critical path in two places:

- **Home's `LIBRARY` count**, which is `countLibrary`: one listing plus one per
  folder, for one integer. It went on the critical path the moment Home learned to
  rebuild itself (`gHomeStale`), so a Back out of a book cost a second of listing
  with nothing on the panel. **Cached now** — `libraryCountForHome`, keyed on
  `SdFileSystem::removals()` and `gStorageUsable`. The key is sound *because of
  what V1 is*: a book cannot ARRIVE while the firmware runs (transfer is card-only,
  so putting one there means the card is in a computer), which leaves delete as the
  only mutation of `/books`, and every delete goes through `remove`. **V2's Wi-Fi
  transfer is the change that breaks that argument** and it needs a one-line
  invalidation beside whatever writes the file.
- **AND THE "ONE PER FOLDER" HALF OF IT IS ITS OWN DEFECT, WHICH THE CACHED INTEGER
  DOES NOT TOUCH.** `countLibrary` calls `BookList::countBooks` per subfolder, and
  so does `LibraryScreen::rescan()` — for the board's `FOLDER · 6 BOOKS` line — so
  a card with fifty folders paid fifty listings at boot and fifty more on **every**
  Library push. Invisible on a flat card and unbounded on a foldered one; the
  listing cache below does not help, because a six-book folder is under its minimum
  entry count and it has two slots against fifty directories. **FIXED by memoising
  the COUNT, not the listing** (`reader/dir_counts.h`, `DirCountCache`): the rows of
  a subfolder are never wanted, only how many are books, so fifty folders cost
  ~1.5 KB where fifty listings would not fit the 20 KB ceiling. A folder is now
  walked once per card STATE rather than once per caller — the second caller and
  every Library push after it reach the card for nothing.
  - **It hangs off the `FileSystem`, for the same reason the listing cache does**,
    and that is what made it reachable at all: `countLibrary` and `rescan()` share
    no state except the `FileSystem&` they were both handed, so a memo anywhere else
    would have needed a setter plumbed through the factory — a caller list, and this
    file's rule is that a caller list is a function not yet written.
    `FileSystem::dirCounts()` defaults to **null**, which means "I keep nothing" and
    is exactly today's behaviour, so `HostFileSystem` and every test wrapper are
    untouched and the simulator memoises nothing. **The 27-clause contract did not
    widen** — no new behaviour clause, so `test_filesystem.cpp` and
    `sd_selftest.cpp` are unchanged.
  - **`SdFileSystem::forgetCardFacts()` is the invalidation, and it is one function
    on purpose.** `listings_.clear()` already had five call sites; a second thing to
    drop would have made it five chances to half invalidate. Every method that
    CHANGES the card calls it, above its own refusals.
  - **`openRead` deliberately does NOT call it**, and the distinction is worth
    keeping: dropping the listing cache there is **eviction** (10–20 KB against the
    45,840-byte floor a book open measures), not invalidation — opening a file
    changes nothing — and that argument does not reach ~1.5 KB. So Home → open a
    book → Back → Library costs no folder listings at all.
  - **The fake memoises too**, so the desktop suite runs the path the device runs.
    A staleness bug is a failing test rather than a device report — which is the
    whole reason the memo sits on an object `core/` can reach.
  - **A hit is invisible**, because a folder answered from RAM produces no
    `[fs] list` line: `[library] … folders held=N hit=N miss=N` is what tells a memo
    that is working from one that has quietly stopped being called.
- **The Library's own `rescan()`**, on every push — the Library is destroyed by the
  pop that leaves it, so Home → Library → Back → Library lists twice. **FIXED by
  holding the listing, because the walk itself cannot be made cheaper** — and that
  was settled by reading SdFat rather than by arguing, which this file had left as
  an open question between "cache geometry" and "a double LFN walk". It is BOTH:
  - `FatFile::getName8()` is the only API that yields a long name. It opens a
    second `FatFile` over the directory's first cluster and calls
    `cacheDir(m_dirIndex - order)` once per LFN entry with the index **decreasing**,
    and `cacheDir` is `seekSet` + read. `seekSet` takes its "follow the chain from
    the first cluster" branch whenever the target is behind the current position,
    which a decreasing index always is — so **every LFN entry of every name
    re-walks the directory's cluster chain from its start**.
  - `USE_SEPARATE_FAT_CACHE` is gated on `__arm__`, so on the RISC-V C3 the FAT
    sector and the directory sector evict each other in one 512-byte buffer, turn
    for turn.
  - Nothing is left to remove: `getName` is already called once an entry,
    `isDirectory`/`fileSize` are RAM reads off the open handle, and `close()`
    reaches `syncDevice()`, which touches no bus outside a read/write stream. The
    remaining routes are editing a submodule this project does not edit, or
    decoding FAT **and** exFAT directory entries from raw sectors in `shell/`.
  - **exFAT does not have this defect** — `ExFatFile::dirCache` seeks relative to
    the file's own recorded `m_dirPos` with no restart. If a card ever measures
    fast, that is why.

**THE LISTING IS HELD BY `SdFileSystem`, NOT BY THE LIBRARY, AND THAT IS WHAT MAKES
THE INVALIDATION STRUCTURAL.** Every way to change what is on the card is a method
on that one object, so `writeAll`, `mkdirs` and `remove` each drop it and there is
no mutation path that can miss it. That is strictly stronger than the `removals()`
key Home's count uses, whose premise — a book cannot ARRIVE while the firmware runs
— is the one **V2's Wi-Fi transfer breaks**: a transfer writes through `writeAll`
and drops this cache by construction, with no line to remember to add.

- **Two slots, not one**, because `rescan()` lists `/books` and *then*
  `/.reader/state`. With one slot the sidecars would take it on every rescan and the
  Library would hit the cache exactly once, ever — a fix that silently stops working
  on a well-used device.
- **Eviction takes the SMALLEST listing held**, because the cost is per entry. LRU
  would throw `/books` out for the directory read that follows it, and "refuse
  anything smaller than what is held" wedges: one big folder locks `/books` out for
  good.
- **20 KB ceiling, and never resident at the 42–46 KB floor**, because `openRead()`
  drops everything — and `openRead` *is* the EPUB path (`readAll` serves the small
  JSON), so nothing but opening a book reaches it. It adds to the no-book-open peak,
  where there is ~133 KB free. **The folder-count memo does NOT go with it**: that
  clear is eviction rather than invalidation, and 1.5 KB does not need evicting —
  see `forgetCardFacts()`, which is the invalidation and which `openRead` does not
  call.
- **ONE BEHAVIOUR CHANGED: a cache hit never touches the bus**, so `list()` stops
  being a place a dead card is noticed. Affordable because `pollCardPresence()` was
  always the detector — operation feedback never fires in V1, which this file
  already records — and the cost is at most one stale listing inside a window that
  ends at the SD-missing screen.
- **The card probe is NOT answerable from it**, checked rather than assumed:
  `readRootDirectory()` and `readProbeTargetFile()` both go straight to SdFat and
  neither routes through `list()`. That is the recorded defect this would otherwise
  have reintroduced.
- **A HIT PRINTS NOTHING**, which on a device is indistinguishable from the call not
  happening — so `[alive]` carries `listings=N slots/NB hit=N miss=N`. "It got
  faster" and "it stopped being called" must not look the same.

**What was looked at and left alone, with the reason, so it is not re-derived:**

- **`Serial.flush()` in the paint path is free on battery.** `HWCDC::write` and
  `flush` both short-circuit when `isCDC_Connected()` is false. **They are not free
  with a logger attached** — `flush` spins `delay(1)` up to 100 ms — so *every
  timing taken over USB includes serial drain that the device never pays*.
- **The transition FULL refresh (693 ms vs 389 ms) stays**, and the roadmap's own
  measurement is why: the paints that *felt* quicker were the slowest, because the
  GC flash reads as the device acknowledging the press. **On e-ink, feedback and
  speed are separate problems.**
- **Deferring the panel wait** (`triggerDisplay`/`completeDisplay`, which the X3
  driver really does support) buys less than it looks like. The framebuffer must not
  be overwritten between the two calls, so the *next* render cannot overlap; and the
  contract is explicitly "non-SPI work", so no card access can either. What is left
  to overlap is CPU work and NVS — a few milliseconds. The SDK's own headline
  1274→822 ms figure needs `supportsBusyGrayscaleStaging()`, which only
  `PaperMonoDriver` returns true for.
- **The SPI clock is already at the datasheet maximum** (20 MHz, both X3 profiles),
  so the two 52 KB plane writes cannot be shortened without going out of spec.
- **`kPollMs` stays at 10 ms.** Halving it saves ~3 ms of a ~550 ms interaction and
  doubles the ADC duty cycle on a device built to sit idle. The queue peek was the
  free half of that trade; this is the half that costs battery for nothing.
- **`saveWhereWeAre`'s NVS write stays on the critical path.** A ~40-byte
  `putString` into a page with room is ~2–5 ms, and deferring it past the paint
  means `sleepNow()` — which is `[[noreturn]]` and can be reached in the same drain
  loop as the navigation that dirtied the record — has to flush it first. That is a
  correctness hazard bought with 1% of a paint. Measure it first; the
  `[session] stored` line is already there to hang a duration on.

### What a real run measured (2026-08-24, X3/UC8279, 203-book card)

53 interactions off the device, `ser=0%` — the cable was not in the numbers.
`tools/latency.py` medians, milliseconds:

| press | n | net | wait | disp | post | render | up | wave |
|---|--:|--:|--:|--:|--:|--:|--:|--:|
| CONFIRM Library → Reader | 1 | **1753** | 0 | 0 | **1012** | 306 | 25 | 415 |
| UP Reader | 5 | **1355** (max **3401**) | 533 | 0 | 0 | — | — | — |
| CONFIRM Home → Library | 1 | **1214** | 0 | **666** | 4 | 105 | 25 | 415 |
| LEFT Reader (page back) | 7 | **1055** | 97 | **376** | 0 | 155 | 25 | 414 |
| CONFIRM Reader → menu | 2 | 718 | 0 | 0 | 14 | **266** | 25 | 414 |
| RIGHT Reader (page fwd) | 2 | 634 | 38 | 26 | 0 | 131 | 25 | 414 |
| DOWN Library | 16 | 549 | 0 | 0 | 4 | 100 | 25 | 414 |
| CONFIRM Home → Settings | 1 | 515 | 0 | 0 | 4 | 71 | 25 | 414 |
| UP Contents | 2 | 508 | 3 | 0 | 5 | 62 | 25 | 414 |

**THE PANEL IS 439 ms AND IT NEVER VARIES** — `up=25` plus `wave=414` on every
one-pass paint in the run, ±1 ms. `up` is the 52,272-byte plane write at 20 MHz;
`wave` is the SDK's own `8279_DRF (389 ms)` plus the ~25 ms DTM1 baseline write
that follows it. **So an ordinary chrome interaction is 80–87% panel** and chrome
is finished: the only movable part left is a 62–106 ms render.

**Every transition in the run was FAST**, because this card's `settings.json` has
`fullOnTransition=0`. A screen change therefore costs the same 439 ms as a focus
move, and the 693 ms GC appears only on the first paint after boot. The "a
transition costs ~825 ms" arithmetic elsewhere in this file is the *default*
setting's, not this device's.

**`wait=` IS NOT A DEFECT.** It is how much of the *previous* paint the press
landed inside — the loop is blocked for the paint's whole duration, so pressing
faster than 550 ms puts the remainder in front of your press. Library DOWN shows
it directly: median 549 ms, max 937 ms, the difference being entirely `wait`.

**The four things the run actually indicts:**

1. **THE DEFERRED PAGE COUNT BLOCKED THE LOOP FOR 2–3.6 s** (`[index] pages=315 in
   3605ms`), which is *more* than the refinement's 1409 ms, while `kCountQuietMs`
   was 1200 ms — barely two paints. Two presses landed inside a count: one waited
   1304 ms to be noticed and then drew nothing, one took 3401 ms end to end.
   **FIXED, AND WIDENING THE WINDOW WAS NOT THE FIX** — that only made it rarer.
   `completeIndex` takes a stop predicate now; see below.
2. **A BACKWARD PAGE TURN SPENT ~390 ms IN THE DISPATCH**, against 20–33 ms
   forward — so paging back cost 1055 ms against 634 ms, and the rewind was
   comparable to the whole waveform. This file dismissed it as "33.9 ms desktop
   against a ~520 ms panel refresh"; **the desktop→device ratio on the inflate path
   is ~11×, not ~1×**, which is the same "37× is a RENDER ratio" trap recorded
   under the eager page count. **Fixed by the page ring; see below.**
3. **THE OVERLAYS ARE THE MOST EXPENSIVE RENDERS ON THE DEVICE** — the actions
   panel at 256 ms and the reader menu at 260 ms, against Contents' 62 ms. The
   guess recorded here first was the veil, and **the veil was wrong**: it is 10 ms.
   See the breakdown below, which is what settled it.
4. **`/books` LISTS AT 2.90–2.96 ms AN ENTRY**, confirming the figure this file has
   quoted for two phases, and 203 entries is **~600 ms** on every Library push. Not
   the 1.1 s estimated elsewhere here: that assumed 406 entries because macOS writes
   a `._name` beside every file, and **this card has none**. Both are fixed now —
   Home's count by `libraryCountForHome`, the listing itself by holding it, and the
   one-listing-per-FOLDER underneath both by `DirCountCache`; see **Storage**, which
   also names why the walk cannot be made faster. **Note this run's card is FLAT**,
   so it measures none of the folder cost: the defect the memo fixes is invisible on
   exactly the card every device measurement in this file was taken on.

### Which primitive spent the render

`[render]` is the second half of `[paint] done`, and it is per PRIMITIVE rather
than per screen — deliberately, because the primitives are shared: whatever is
expensive here is expensive on every screen that draws one, where a per-screen
breakdown would have to be read once per screen to notice that. Five slots, each
a leaf that touches pixels, so nothing nests and nothing is double-counted;
`outlineRect` is four `fillRect`s and is not a slot, `drawPanelRow` is a fill plus
a run and is not one either. Measured on the device, microseconds:

| screen | total | **fill** | glyph | veil | dither | icon | other |
|---|--:|--:|--:|--:|--:|--:|--:|
| ITEM-ACTIONS | 256 000 | **157 836** (62%) | 57 032 | 9 923 | 5 044 | 8 076 | 18 089 |
| READER-MENU | 260 000 | **138 246** (53%) | 103 621 | 9 965 | — | 5 918 | 2 250 |
| READER, a page | 215 000 | 299 | **212 935** (99%) | — | — | — | 1 766 |
| LIBRARY | 105 000 | 36 054 (34%) | 44 506 | — | 4 939 | 3 719 | 15 782 |
| HOME | 74 000 | 34 225 (46%) | 26 616 | — | 4 974 | 6 048 | 2 137 |

**`Framebuffer::fillRect` WAS 34–62% OF EVERY CHROME RENDER, and it was the same
defect `veilRect` had already been fixed for** — a per-pixel `setPixel` loop, so a
bounds check, a `byteIndex` (a division under rotation), a `bitMask` (a modulo)
and a read-modify-write, per pixel. An overlay panel is ~340×450 and a full-bleed
focused row ~300×72, so one actions-panel frame asked for ~200,000 of them. The
arithmetic that made it unambiguous, and which is the shape to look for next time:

| | pixels | cost | per pixel |
|---|--:|--:|--:|
| `veilRect`, byte-wise, whole frame | 418,176 | 9.9 ms | **24 ns** |
| `fillRect`, per-pixel | ~200,000 | 158 ms | **790 ns** |

Same class of work, **33× apart**, and the difference was entirely `setPixel`.

**IT IS BYTE-WISE NOW**: clip once, resolve the run to a first byte, a last byte
and two edge masks, then per physical row write the masked first byte, `memset`
the middle and write the masked last byte. **A fill has no PHASE** — unlike the
veil and the dither it is keyed on nothing — so its run is identical for every
row and the masks hoist out of the loop, which `veilRect` cannot do. Desktop, µs
a pass: `library_actions` 706 → **351** (fill 441 → 3), `reader_menu` 1004 → **437**
(402 → 4), `library` 277 → **219**, `home` 187 → **137**.

Two things it inherits from the veil and one it does not:

- **The rotation hazard is the same and so is the structure.** Under CCW a logical
  row is a physical column, so the outer loop is the logical **x**. Getting it
  wrong passes every desktop test and every golden and smears on glass.
- **The edge masks need their own tests**, because the panel widths are multiples
  of 8 and *overlay geometry is not* — it is derived by subtraction from a centred
  panel, so a run starting or ending mid-byte, and a negative origin, are real
  cases. `test_framebuffer.cpp` keeps the per-pixel form as its reference and
  asserts byte-identity across 37 rectangles × both rotations × both colours, and
  each of its cases was proved by MUTATION rather than by passing.
- **`ditherRect` WAS the last per-pixel area primitive, and it is byte-wise now
  too — so the family is closed.** It was expected to need the veil's shape,
  because its mask varies per row by tile phase; it needed the FILL's, and the
  difference is one modulus. A byte is eight columns and the tint's tile is
  **four** wide, so `8 % 4 == 0` and every byte of a row carries the identical
  mask — the run hoists out exactly as a fill's does, and only a one-byte table
  lookup varies per row. The veil's tile is **three** wide and `8 % 3 == 2`, which
  is the whole reason its mask has to advance per byte. *Which* primitive a new
  one resembles is decided by that modulus, not by whether it has a phase.
  - **THE SIZE OF IT WAS ESTIMATED FROM THE WRONG CALLER.** This entry named
    Home's cover placeholder, at 15–22 µs desktop — which is right (16–17 µs
    measured) and is not the big one. `renderSleep` tints the **whole panel**
    (`.dither-field`), 418,176 pixels, and measured **329–338 µs of Sleep's
    362.8 µs render — 91%**. Same mistake as the veil, whose cost was also assumed
    small until it was measured: a primitive's bill is set by its widest caller,
    and a full-frame call does not look different from a thumbnail at the call
    site.
  - **IT VECTORISES, WHICH IS WHY IT BEAT THE VEIL BY AN ORDER OF MAGNITUDE.** The
    veil managed 13.6×; this is ~14× on the whole Sleep render and **well over
    100× on the primitive**. The middle of a run is `row[b] |= tile` with `tile`
    constant, so clang emits `orr.16b`/`and.16b` over `q` registers — 16 bytes a
    go — where the veil's per-byte phase advance and a per-pixel loop can emit
    nothing of the kind. Release/-O3, `--bench 200`, three runs each: `sleep`
    362.8 → **25.9**, `sleep_idle` 343.5 → **8.1**, `home` 59.9 → **43.6**,
    `library` 91.5 → **73.2**, `book_details` 54.0 → **35.4**.
  - **THE TILE'S RANKS ARE NOT TRANSPOSE-SYMMETRIC BUT EVERY LEVEL'S SET IS**, and
    that distinction is what lets the two rotations share one mask table the way
    the veil's outright symmetry lets it swap axes. `kClustered[0][1]` is 6 where
    `kClustered[1][0]` is 4, so the matrix is asymmetric; but a threshold only ever
    asks "is this cell below the level", and each of those four sets IS its own
    transpose. It is a `static_assert` in `dither.cpp`, not a comment, because a
    change to `kClustered` that preserved density and broke it would draw the tint
    transposed **under rotation only** — right on every golden, wrong on glass.
  - `test_dither.cpp` keeps the per-pixel form as its reference across 30
    rectangles × both rotations × all four levels × both inks, and every case was
    proved by MUTATION: forcing the unrotated branch for both rotations fails 275
    assertions, dropping the rotation's mirror term 143, keying the rotated branch
    on the wrong axis 153, an off-by-one row phase 320, and each of the four
    edge-mask failures 30–370. **The two rotation mutations are the ones nothing
    else can catch** — all 40 simulator PNGs are byte-identical across the change,
    and every one of them is `Rotation::None`.

**A READER PAGE WAS 99% GLYPH BLIT** — 213 ms of a 215 ms render, the same
per-pixel shape in `drawRunF26`, and the biggest single render cost in the
product because the reader is the screen a user spends their time on. `fill` on
that screen is 299 µs: a page draws almost no furniture.

**IT IS BYTE-WISE TOO NOW.** Clip the glyph box once; decide the plane **per
physical row** as a four-entry "which coverages ink" table, so `BwDithered`'s
Bayer phase stays keyed on absolute panel coordinates; hoist the glyph row
pointer; accumulate a byte of bits and skip bytes that ink nothing. Under CCW the
outer loop walks the glyph's **columns**, because a logical column is a physical
row — **`text.cpp` is the second routine in `core/` that has to know `Rotation`
exists**, for the identical reason as the veil and with the identical trap: it
passes every golden and smears on glass.

Cumulative, µs a pass at 528×792 (`-O3`), over both rewrites:

| screen | was | after `fill` | after `glyph` |
|---|--:|--:|--:|
| reader, one dithered pass | 474 | 474 | **144** |
| reader_menu | 1004 | 437 | **209** |
| library_actions | 706 | 351 | **192** |
| library | 277 | 219 | **113** |
| home | 187 | 137 | **74** |

**THE PROOF THAT MATTERS WAS NOT THE SUITE.** Every simulator screen was rendered
at both geometries against a binary built from the old blit — **36 of 36
byte-identical PNGs** — and the rotated screens verified as `rotate90CCW` of the
unrotated ones, 24 of 24, across all four planes. The reference-implementation
test was then proved by MUTATION: a rotated-row bug fails 860 assertions, the
dither phase 616, partial-byte alignment 1584. That pass also caught a bug in the
TEST, which had been filling the field with the ink's own colour so every
comparison was trivially true — a golden-shaped failure inside the thing checking
the goldens.

**WHAT IS LEFT ON THAT SCREEN IS THE PEN AND THE GLYPH CACHE**, not the blit:
`glyph()` and `measure()` are the remainder. If a page turn has to get faster
again, that is the target, and the note under **Opening a chapter** saying "the
blit is the target" has been spent.

**AND A µs FIGURE MUST SAY WHICH TREE PRODUCED IT.** `cmake -S . -B build` sets no
`CMAKE_BUILD_TYPE`, so a fresh tree builds `core/` at **-O0** and every number
above would be ~5× larger. The figures in this file are `-O3`/`-Os`; they differ
from each other by a few percent and from a default tree by a factor. A bench
quoted without its build type is not a measurement.

**`other` IS A REAL SLOT, NOT A ROUNDING ERROR.** It is the remainder — layout
arithmetic, measuring, wrapping, eliding — and on the Library it is 11–16 ms and
VARIES between renders of the same screen, which is elision measuring real
filenames rather than the boards' short demo titles. Not chased yet.

**And the desktop agrees, which is what makes `--bench` worth trusting for this.**
`reader_sim <screen> --bench N` installs the same profiler through a
`std::chrono` clock and reports the same slots: it called the actions panel 62%
fill against the device's 62%, and the reader page 97% glyph against 99%. So a
change to a drawing primitive can be judged before it is flashed. It does NOT
transfer to anything touching the card or the inflater — see the ratio warnings
above.

**And one dead button the timing exposed rather than the logic**: `UP` on the
Reader is `Gesture::AltPrev`, the way back, and answers `none()` with no way back
to promise — three presses in the run reported `paint=none`, two of them after waiting
out a count. It is correct behaviour and it reads as a broken device, which is a design
question (the hint bar advertises nothing there) rather than a performance one.

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
`home:-1;library:7:/books;item-actions:1`, root first — and it lives in `core/`
because `shell/` has no test harness and this is the only pure logic on the
resume path. An entry is `name:focus` or `name:focus:place`. Record version is
**5**.

**AN ENTRY'S THIRD FIELD IS WHAT ITS FOCUS IS AN INDEX INTO, AND WITHOUT IT THE
RECORD PRODUCED A WRONG ROW THAT LOOKED RIGHT (#14).** The Library can be listing
a **subfolder** of `/books` and the record could not say which, so sleeping in
`/books/Classics` on row 3 woke on `/books` row 3 — which is worse than losing
the position, because nothing on the glass says the restore went wrong and the row
opens a book the reader never chose. A focus is only meaningful relative to the
list it indexes, so the list is stored beside the index into it: `Screen::place()`
/ `setPlace()`, mirrored into `StackEntry::place`.

- **`core/` NEVER LEARNS WHAT A PLACE IS.** The Library's is a directory path;
  `session_record.cpp` knows only that it is bytes and what may not appear in them
  raw. A `path` field would put a filesystem into `App` and name one screen in a
  format that names none — the ladder-of-screen-names shape `App::restore` exists
  to have deleted. **One screen has a place today**, and a count in
  `test_focus_restore.cpp` says so, so the loop cannot quietly test nothing.
- **PERCENT-ESCAPED, BECAUSE A LEGAL FILENAME MUST NOT BREAK THE FORMAT.** `%`,
  `;`, `:` and every control byte become `%XX` — **`;` and `%` are both legal in a
  FAT long name**, so a format that trusted them is not a fix. Everything else
  passes through, **UTF-8 included**, because `nvs_get encre_sess stack str`
  printing `library:7:/books/Le Fléau` is the same property that made the screen a
  NAME rather than an ordinal: a record a person can read off a device is one they
  can diagnose. (`:` cannot occur in a FAT or exFAT name at all, so the only
  escapes a real card produces are `%` and `;`.)
- **A MALFORMED ESCAPE REFUSES THE WHOLE RECORD**, on the unknown-name rule; a
  place TOO LONG is a different question with a different answer. `kPlaceMaxBytes`
  is **128 escaped bytes** — enough for the `/books/<author>/<title>` a real card
  carries, and not enough for what FAT permits, which is stated as a limit rather
  than hidden — and a place over it is **dropped whole, never truncated**, because
  a cut path addresses a *different* directory rather than none. That is
  `Xml::kMaxAttrBytes`'s rule reached from the other side. **The entry's focus goes
  with it, written as `-1`**: a row index without the folder it indexes is the whole
  of #14, and -1 is not a marker but the real "nothing selected" every focused
  screen already accepts.
- **THE BOUND IS ENFORCED ON THE WAY OUT ONLY**, which is what lets
  `sessionStackMaxBytes()` stay derived (1,209 bytes at `kMaxDepth` 8, inside NVS's
  4,000-byte cap for a string, and the read buffer in `shell/src/session.cpp` comes
  from it). Anything that fit that buffer is by definition within the bound on the
  way in, and the SCREEN is the thing entitled to refuse a place — which it does.
- **THE FOCUS IS APPLIED ONLY WHEN THE PLACE WAS HONOURED**, in `App::restore`,
  and that one branch is what makes every failure degrade instead of mislead: a
  folder deleted while the device slept, a card that is not the card the record was
  written on, a `..` component, or **a screen that reports a place and never learned
  to accept one back** all land the user at the top of the list the screen did
  build. `reading_position.h`'s `fitOf` grading is the same rule over
  a different quantity — a book's block rather than a screen's list.
  There is deliberately no `FocusScreen`-style `final` pair here: one screen has a
  place, and a shared base for a single caller is a header edge bought for nothing
  (the Typography formatters' extraction was undone for that reason), so the
  **default `setPlace` returns false** and the half-taken pair costs a row rather
  than putting one in the wrong folder. The second screen to want a place is the
  extraction point.
- **`setPlace`'s BOOL IS NOT `setFocus`'s BOOL**, and the difference is stated at
  the one site: a place is not a coordinate you can be part of the way to, so it
  answers "you are there now", where `setFocus` answers "something moved" because
  `moveFocus` needs to know whether a 520 ms refresh is owed. Asking for the place
  already listed is honoured and **touches no card** — the ordinary case, since the
  constructor lists the root.
- **THE VERSION BUMP IS A DECISION, NOT A NECESSITY.** A version-4 record still
  parses under the new decoder — two fields is the no-place form — and it is
  discarded anyway, because its `library:7` means "row 7 of some directory" and
  honouring that is exactly the wrong row the field exists to stop claiming. The
  cost is the documented one: one wake per device, the first after this firmware
  lands, which starts at Home.
- **A REFUSED PLACE IS VISIBLE IN THE LOG WITHOUT A NEW LINE**, because the restore
  already prints where it LANDED and compares it against what the record named
  (`encodeSessionStack(gApp->snapshot())` against the record's own string) — a
  dropped folder makes those differ, and the parenthetical now names it as one of
  the three reasons they can.
- **WHAT ONLY A CARD CAN EXERCISE, and therefore where this was untestable
  before:** the sample Library the goldens and the comparison sheet use has ONE
  directory and cannot descend, so nothing on the desktop could reach the defect
  until `test_session_restore.cpp` grew a `FakeFileSystem`-backed App. **The
  listing cost is measured there rather than argued**: a wake into a subfolder
  spends **4** listings against **3** for one at the root — `/books`, one
  `countBooks` per folder for the board's `FOLDER · 6 BOOKS` line, and then the
  subfolder — so the place costs exactly one more listing than a Library push
  always has. That is the honest price of the screen being built before it is told
  where it was, and it is why `setPlace` refuses to re-list a directory it is
  already showing. (This line first said "two", from reading the code rather than
  running it, and the folder counts are what it missed.)

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
move (**nine** — this line said five, then seven, then eight, and each figure was
right when written) so it cannot quietly end up testing nothing. **There
are TWO such counts**, `movable` and `wrapping`, and this line only ever mentioned
one. **And the `static_assert` beside them let a screen through**: it compared
against `ScreenId::SdMissing + 1`, a NAMED member rather than the last one, so
appending `Typography` satisfied it unchanged and the guard that exists to force a
new screen into `kAllScreens` said nothing. Caught only because the hand-maintained
counts failed for an unrelated reason; #42 is the fix.

**IT RECURRED WHEN `BookEnd` LANDED, AND THE SECOND INSTANCE WAS THE EXPENSIVE
ONE.** The assert had been advanced to `ScreenId::Peek + 1` — still a named member,
so appending `BookEnd` satisfied it unchanged and said nothing, exactly as
`Typography` had. That is the same defect twice in the same line, which is what
makes it a pattern rather than an oversight. **And the pattern is not confined to
the test**: `core/src/session_record.cpp` had *three* bounds spelled
`<= ScreenId::Peek`, so `sessionWireName` fell through to `return kNames[0]` and
**serialised the new screen as `home`** — a reader idle-sleeping on the end-of-book
screen would have woken on Home, with no failing test and no log line, because the
round-trip test could not see a screen the table was too short to name.
`session_record.cpp`'s table is `static_assert`ed against the enum's END now, so it
cannot be short.

**#42 IS CLOSED, AND THE THIRD APPEND IS WHAT CLOSED IT.** `ScreenId` ends in a
`Count` sentinel, and **all six** bounds name that instead of a member —
`kNames`' assert, its decode loop, both catalogue asserts, and
`test_session_record.cpp`'s two every-id walks. **This line said "all four" and the
two it missed were the two that were still broken**, which is the undercount the
paragraph below this one was written about: the fix reached four bounds, the count
of bounds was four, and the two walks nobody had enumerated kept naming a member.
**The defect was reproduced
before it was fixed**: `BookError` was appended alone and BOTH guards stayed silent,
the only diagnostic being a `-Wswitch` warning, which this project does not build
with `-Werror`. Then the same append was made against the sentinel and failed the
build twice, in sequence — `kNames` first, `kAllScreens` second — until each table
grew. That is the guard doing its job at the moment it was written for, rather than
one append later.

**`Count` IS NOT A SCREEN AND NOTHING MAY MAKE IT ONE.** `sessionWireName` breaks
out, `screenName` answers `"?"` and `DemoScreenFactory::create` returns nullptr —
each **explicitly** rather than by fall-through, so `-Wswitch` keeps working as the
reminder that a new screen needs a case. A sentinel that quietly became
serialisable would be a worse version of the bug it closes.

**IT RECURRED A THIRD TIME WITH `BatteryEmpty`, AND THE SENTENCE ABOVE IS WHY IT
WAS ALLOWED TO.** "`session_record.cpp`'s table is `static_assert`ed against the
enum's END" was believed of that file and was **not true of it**: the assert read
`static_cast<size_t>(ScreenId::BookEnd) + 1` — a NAMED member, exactly the shape
`test_focus_restore.cpp` had — so appending `BatteryEmpty` put `BookEnd + 1` on both
sides and it said nothing. **Its own comment claimed "TIED TO THE ENUM, NOT TO A
NAMED MEMBER"**, which is the worst version of this defect: a guard that documents
itself as the fixed one. The only thing that pointed at the tables was **`-Wswitch`,
three warnings and not errors**, so a build with warnings scrolling past would have
shipped the new screen serialising as `home`. That was the state of the
`BatteryEmpty` branch before it merged `main`'s sentinel.

**THERE WERE SIX HAND-MAINTAINED BOUNDS ACROSS THREE FILES, MAIN'S FIX REACHED FOUR,
AND THE MERGE FOUND THE OTHER TWO.** `session_record.cpp`'s assert **and** its
`decodeName` loop, `test_focus_restore.cpp`'s catalogue **and** its assert, and
`test_session_record.cpp`'s two every-id walks. The sentinel landed on `main`
against the first four; the last two still read `<= ScreenId::BookEnd`, so **`main`
itself carried two walks that did not cover `BookError`** — a screen was appended,
the two guards that name `Count` fired, and these two silently walked fourteen of
fifteen ids. They name `Count` now, so **all six are structural and none can be left
behind by an append.** `grep -n "ScreenId::Count" core/src/session_record.cpp
test/unit/test_focus_restore.cpp test/unit/test_session_record.cpp` is the check to
run before believing any figure here — which is how the two were found, the figure
in this paragraph having been wrong at every previous revision of it.

**AND THE SENTINEL'S FIRST REAL TEST WAS A MERGE, WHICH IS THE CASE #42 EXISTS FOR.**
`BookError` and `BatteryEmpty` were appended on two branches at once and merged, so
both tables and both asserts had to grow in one change. Every guard naming `Count`
failed the build until it did; every guard naming a member would have passed
unchanged, and the loser of the merge would have serialised as `home`. **`-Wswitch`
stayed silent throughout** — the switches were exhaustive because the asserts had
already forced the tables — which is the first time that warning was not the only
thing standing between an append and a wrong wake.

**The rule is structural now**: `focus()` and `setFocus()` are `final` on
`FocusScreen`, so a derived screen cannot take one half without the other — the
test checks a property the
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
  also what makes an overlay restorable at all. **The place now goes in front of
  the focus for the same reason one layer down** — the row is an index into the
  directory, so a Library told which folder only afterwards would caption the
  overlay above it with a book from the wrong one.
- **It deleted the special cases.** The shell's restore was a ladder naming Home
  ("already the root, nothing to push") and SD-missing ("the card mounted, so the
  message is no longer true"), and every screen not in the ladder was handled by
  accident — three of them wrongly. Both branches are now one question,
  *does the record's root match this app's root*, and `App::restore` names no
  screen at all. **A restore that stops early keeps what already stands**: a
  record from a newer firmware should not cost the user the Library they were in.
- **The wire format is in `core/`** (`reader/session_record.h`), as
  `home:-1;library:7:/books;item-actions:1`, because `shell/` has no test harness and
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
  `kClustered` black on a 4px grid for tints (`.dither-dots`), `kBayer` dispersed
  for glyph and icon edges, and
  `veilRect`'s clustered **white** on a **3px** grid for the overlay veil. A 4px
  veil is half as dense and reads as a smudge.
  - **`ditherRect`'s `Ink` PARAMETER HAS NO SCREEN CALLER ANY MORE, and this line
    used to name its one instance.** `.dither-dots-inv` was the FOCUSED Library
    row's placeholder cover — the tint reversed out of the black fill — and #95
    removed the placeholder from the rows. **The description is still accurate and
    `ditherRect` is now down to ONE caller rather than two**: this bullet said
    "both surviving callers pass black (Book details' cover slot, and
    `renderSleep`'s full-panel field)" and Book details' slot went with the rest of
    #95, so the surviving caller is the sleep field, and it passes black. It is
    kept as `ListRow::trackingEm1000` is kept: `test_dither.cpp` drives both inks
    across 30 rectangles × both rotations × all four levels, so this is **tested
    capability rather than working behaviour**, and `Bookmarks.dc.html` is a
    board that asks for a reversed tint again. Stated rather than assumed,
    because a producerless reader is the shape this file has been bitten by from
    two directions. **`core/include/reader/dither.h` said the same thing in the
    present tense and was corrected with this**, which is the half a CLAUDE.md-only
    fix leaves behind — a rule stated in two places is enforced in neither if only
    one is revised.
- **The veil was the most expensive thing on the screen, and it is now byte-wise.**
  A veil covers the WHOLE frame, and the per-pixel form cost four integer
  divisions and a bit-addressed read-modify-write per pixel: 2.33 ms at 528×792
  against `ditherRect`'s 1.12 ms and a full-frame `clear`'s 0.001 ms, so ~150 ms
  of every overlay repaint at this project's ~65× desktop-to-device ratio. It now
  ORs eight columns at a time into the physical store — 0.17 ms, 13.6× — which
  made it **the FIRST drawing routine in `core/` that knows `Rotation` exists**:
  under CCW a logical row is a physical *column*, so it walks logical **columns**
  instead, and the tile is symmetric under transposition, which is what lets the
  two cases just swap axes. A byte-wise path that assumed a logical row is a
  physical row would pass every desktop test and every golden and smear the veil
  diagonally on glass. `test_dither.cpp` keeps the per-pixel form as its reference
  and asserts byte-identity at both geometries, under both rotations, and for runs
  that start and end mid-byte — the panel widths are multiples of 8, so nothing on
  the device exercises the edge masks.
  - **THERE ARE FOUR OF THEM NOW, and this line said "the one" for three
    conversions after it stopped being true.** `Framebuffer::fillRect`, the glyph
    blit in `text.cpp` and `ditherRect` each took the same structure for the same
    reason, and each carried its own paragraph calling itself the first, second or
    only one. They share one hazard, and it is worth stating once: **under CCW the
    outer loop is the logical x, and getting it wrong is invisible to the whole
    desktop** — the simulator, every golden and every comparison sheet are
    `Rotation::None`. Only a byte-identity test run under both rotations, and
    proved by mutation, stands between that mistake and the panel.
  - `PhysRun`/`physRunFor` — clip a run to a first byte, a last byte and two edge
    masks — is `reader/physrun.h`, shared by `fillRect` and `ditherRect`. It was
    written twice before it was a header, which is this file's own second-copy
    rule arriving one copy late again. `veilRect` still computes its own inline,
    because its masks are interleaved with the per-byte phase advance the other
    two do not have.
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
  - **This governs every scrollable list**, and today that is the Library and
    V1.1's Wi-Fi picker — which is the second user of the rail and the first
    since it was written. This line said "Library alone" through the picker
    landing.
    Settings scrolled for about an hour: adding its `Refresh on screen change` row
    pushed it past the panel, and then Wi-Fi was cut from V1 and CONNECTIONS went
    with it — eleven items where twelve fit, the SLEEP SCREEN section took it back
    to nine, and **V1.1's CONNECTIONS row has now put it at eleven again**, which is
    still inside twelve: no rail, no gutter, rows still running to the panel edge.
    That figure has moved four times and is the thing to re-read rather than
    inherit. Phase 3's typography settings will
    push it over and it will start scrolling **without any code change**,
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
| Home | `Main.dc.html` | Focus starts on the CONTINUE block (`-1`), not the menu. Its title WRAPS and its chapter NAME elides — two card-sourced runs in one column, and only the title may grow. **NO COVER: the reading column is the whole content width** (#95). |
| Home / empty | `HomeEmpty.dc.html` | A **variant**, not a screen: same `ScreenId`, same view model, same menu. |
| Home / nothing open | `HomeUnopened.dc.html` | The same variant with different words. What the device actually shows today. |
| Library | `Library.dc.html` | The only list that scrolls today, and the only screen with a rail. A book row is the FOLDER row with a different mark (#95) — one expression picking `kBook` or `kFolder`, and the 44×64 slot stays because `bookRowContentH` takes the max with it. |
| Library / scrolled | `LibraryScrolled.dc.html` | Reached by pressing PAST the focused row and back — arriving from above windows it differently. |
| Item actions, Delete confirm | their own boards | Overlays; a focus move repaints the overlay alone. |
| Book details | `BookDetails.dc.html` | Not an overlay, despite covering the Library. Its title **wraps**; everywhere else elides. **NO COVER, and its block's height is the COLUMN's now** — the third and last placeholder to go (#95). The height was `max(column, cover)` and the cover's 180px won for every title the screen can draw, so it was a CONSTANT and the column's runs were free; each run costs its line box now and the 2px rule below moves from 290 to 203. |
| Settings | `Settings.dc.html` | Nine items, three sections, and every drawn row responds. |
| Sleep | `Sleep.dc.html` | Painted directly, never pushed — a push would make the wake restore into it. Its title, author and chapter **all wrap**; the chapter's two lines are reserved UNCONDITIONALLY, so the title's budget cannot move when the reader crosses a chapter. The badge is drawn first, because its top is the card's bound. |
| Sleep / nothing open | `SleepIdle.dc.html` | The badge alone. Same screen with its card removed. |
| Sleep / cover | `SleepCover.dc.html` | The cover full-bleed, and **the one screen that drops the badge**. `Grayscale`, decided per paint. |
| Sleep / cover + details | `SleepCoverDetails.dc.html` | The same cover with the reading card and the badge over it. Keeps both. Its golden pinned a **truncated** title for two phases. |
| Reader | `Reader.dc.html` | The only screen whose content is the BOOK's — but no longer the only `Fidelity::Grayscale` one. |
| Book error | `BookError.dc.html` | An overlay whose parent may be Home — **the only one whose parent is not a list**, so it is the only veil no board draws. THREE copy shapes: damaged, unreadable, and a book that is fine and did not fit. |
| Book end | `BookEnd.dc.html` | **The only screen a PAGE TURN opens rather than a press** — off the last page, so it must be reachable with no button bound to it. Its leaving slab's LABEL follows what is under the Reader; its ACTION does not. |
| Typography | `Typography.dc.html` | Two doors, and it needs nothing from the book. `CHANGE` cycles in place; `Font` is drawn and unreachable. |
| Reader / battery low | `LowBattery.dc.html` | A **variant**, not a screen: the same Reader with one 78px band drawn OVER the page. `columnH` is untouched, so no chapter re-paginates, and **any** button dismisses it. |
| Peek | `Peek.dc.html` | The only overlay over a `Grayscale` screen. Its column is NOT the reading column, which is why it shows no page number. |
| Battery empty | `BatteryEmpty.dc.html` | Painted and never pushed, on `Sleep`'s argument. The last thing on the glass before a critical shutdown, and what the resume gate leaves standing when it refuses. |
| SD missing | `SdMissing.dc.html` | RETRY restarts the device when the card was lost after a mount. |
| Wi-Fi | `WifiSettings.dc.html` | V1.1. The hub: saved networks, `AUTO`/`SAVED` toggled in place, and a HOLD for FORGET — the **second hold in the firmware**, the Library's being the first. Reached from Settings' `CONNECTIONS` row. |
| Wi-Fi / nothing saved | `WifiSettingsEmpty.dc.html` | A **variant**, not a screen, and its two movers go quiet: one focusable row means UP and DOWN would promise a press that changes nothing. |
| Join network | `WifiPicker.dc.html` | The **second scrolling list and the second user of the rail**. A scan in flight draws NO list and no count — that state is not boarded (drawStatusBar is specified by LibraryOpening and SleepWaking) and is pinned by a golden. |
| Password | `WifiPassword.dc.html` | The **first text entry in this firmware**: a caret, an editable string, three layers whose union is all 95 printable ASCII, and a 46-cell grid over `GridFocus`. The layer key names where it TAKES you — `abc` while the symbols show — because it latches and SHIFT does not. The Confirm hint names what the focused CELL does; Back deletes before the caret and LEAVES when the field is empty. |
| Connecting | `WifiConnect.dc.html` | One state. It used to step to `READY`, which is gone: a successful join leaves for the saved list, and the list with the network in it is the confirmation — at one waveform instead of two. |
| Couldn't join | `WifiError.dc.html` | THREE copy shapes, BookError's argument: wrong password, not found, and didn't finish. `EDIT PASSWORD` is **absent** on the latter two rather than inert. Its slab count is the slab LIST, measured, not a second spelling. |

**SETTINGS IS NINE ITEMS NOW — THREE SECTIONS AND SIX ROWS — AND NOTHING ON IT IS
INERT BY DEFAULT.** Its five inert TYPOGRAPHY rows became one disclosing
`Typography` row in a `READING` section once a screen existed to edit them (see
**The typography panel**), and a `SLEEP SCREEN` section then replaced the last row
that was drawn with nothing behind it: `Sleep screen` / `BOOK COVER` was **issue
#11**, and it is now `Shows` (COVER / COVER + DETAILS / DETAILS) and `Cover fit`
(FILL / WHOLE), two rows that act.

What survives of the paragraph below: the focus still skips what cannot act, an
inert row is still drawn exactly as an unfocused focusable one, and the theme still
reports a box model rather than a row count. What is gone: the five rows, **every
placeholder** — `SettingsScreen::Item::placeholder` was removed outright rather
than left with no writer, and a test asserts every drawn row either discloses a
screen or states a value from `settings_` — and the claim that no row here pushes a
screen.

**AND THE ONE REMAINING UNREACHABLE ROW IS DERIVED, NOT TABULATED.** `Cover fit`
is unreachable while `Shows` reads DETAILS, because a fit is meaningless with no
cover on the glass — `focusable()` asks `settings_`, which is Typography's own
precedent (`Font` is unreachable while one body face is vendored and becomes
reachable the moment a second lands, with no line to remember). The gate is
consulted per landing, so cycling `Shows` changes the answer with nothing to
invalidate.

**`kSettingsVersion` DID NOT MOVE**, for the third time and for the reason at the
top of `settings.h`: an added field takes its default from an older file, and both
defaults are today's behaviour. `CoverFit` comes from `core/include/reader/cover_fit.h`
— a leaf that includes nothing, written so `settings.h` can reach the enum without
including `imagefit.h`: measured, `settings.h` is **895 preprocessed lines**, the
include costs **9**, and `imagefit.h` would have cost **72,962** (it needs
`<vector>`), which is exactly the coupling `settings.h` already refuses at
`bodyPpem`.

**THE LAST-ROW-OF-A-SECTION RULE IS DEFENDED BY A PIXEL FOR THE FIRST TIME.**
`renderSettings` suppresses a row's `border-bottom` when the next item is a header,
and until this section existed the only section boundary on the screen was
`Typography` → `DEVICE` — where the row is FOCUSED, so `rowRuleFor` had already
suppressed it and the golden could not see the `nextIsHeader` term at all. Deleting
that term now fails both Settings goldens; before, it failed nothing.

**SETTINGS DREW EVERY BOARD ROW AND ONLY THE DEVICE ONES RESPONDED.** TYPOGRAPHY
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
RESTORE INTO IT — press power, get "asleep, hold power to wake" back. Bypassing `App`
moves two things it normally owns into that function: the **clear**, and
**`gFrameContentsUnknown`**, because `App`'s partial-repaint record now describes a
frame that no longer exists. Nothing reads it before the chip resets, but leaving a lie
there is a trap for the next person to paint something after it.

It costs one **FULL** waveform (~825 ms) on every sleep — full rather than fast because
this is the last thing the panel does for hours and a differential update would leave
the previous screen's residue under it. **WITH A COVER ON IT THAT IS NO LONGER THE
WHOLE BILL**: a cover makes the screen `Grayscale`, which is three waveforms, and a
cover the cache does not already hold costs a decode of seconds between two paints.
See **Covers**, which prices all three shapes; `[power] sleep cost` is the one line
that adds them up.

**ASLEEP WITH NOTHING OPEN IS ITS OWN BOARD** (`SleepIdle.dc.html`): the card *is* the
reading state, and the device sleeps from Home or the Library as often as from a book.
Painting the card with a blank title or a 0% bar would claim a book the user is not
reading; painting nothing at all is worse, because e-ink holds its last image and a
Library left on the glass gives no clue the device is asleep rather than frozen — which
is the entire reason this screen exists. So the **badge is the load-bearing half and it
stays**, drawn by the same tail in both states so the two cannot disagree about where
it sits; the card is the half with something to say only sometimes. **`SleepCover`
OVERRIDES THAT RULE AND IT IS THE ONLY THING THAT MAY** — see **Covers**, which
records why the override cannot be generalised. One screen with and
without its content, not two screens. `SleepViewModel::nothingToContinue` is spelled
exactly as `HomeViewModel`'s, because it is the same fact and one rule should have one
spelling.

**THE TITLE WRAPS NOW, AND IT USED TO ELIDE (#74).** Home's arc, Home's reason and
Home's mechanism — `wrapProseLead(..., WordBreak::Anywhere)` → `clampProse` →
`drawProse` — because an ellipsis on a *list row* hides only which of seven rows this
is, and here it hides the one fact the screen exists to state. What makes it worse
here than on Home: **this screen holds the glass for HOURS**, so a name cut short is
not a truncation the reader presses past, it is the one they live with.
`Sleep.dc.html`, `SleepWaking.dc.html` and `SleepCoverDetails.dc.html` all gained
`overflow-wrap: anywhere`; the other three sleep boards do not draw the card.

- **THE DEFECT WAS BLESSED INTO A GOLDEN, which is how long it had been there.**
  `test/golden/sleep_cover_details.png` read **`GULLIBLE'S T…`** — its fixture's
  title has never fitted the card — so the repo's own baseline pinned the truncation
  and every run was green. It reads `GULLIBLE'S / TRAVELS` now.
- **THE BADGE IS WHAT BOUNDS THE CARD, AND THE RESERVE IS TAKEN TWICE.** The thing a
  growing card collides with is not the edge of the glass, it is the badge — an
  overrunning title runs UNDER an opaque white box and is hidden by it, an ellipsis
  by another name. And the card is CENTRED, so `centreIn` splits the slack evenly and
  reserving the badge *once* still leaves a tall card hanging half a badge into it:
  the same arithmetic `renderDeleteConfirm` and `renderBookError` each shipped wrong.
  It comes from **`drawBadge`'s own returned top**, not from a second copy of its
  private 34px and note-face line box — deriving a shared edge twice is how the
  header band ended up 6px out. **That is why the badge is now drawn BEFORE the
  card**, and the reorder is pixel-neutral: `sleep_idle` and `sleep_cover` are
  byte-identical across it.
- **THE TITLE'S LINE BOX IS THE BOARD'S `1.1`, NOT THE FACE'S 53px.** `Title700`'s
  own `lineHeight()` is 53 at ppem 42 and the board says 46, and the single line this
  screen used to draw took the face's. A wrap has to be *handed* a lead, so there was
  no way to leave the question unanswered — and `BookDetails.dc.html` states the
  identical `--t-title` at `line-height: 1.1` and already resolves it to 46, so
  `kSleepTitleLineH` is a **documented second copy** of `kDetailsTitleLineH` rather
  than a new number.
- **The card's height is still a RESULT**, now a sum whose title term is the wrap's
  own height. The budget derives to **8 lines on both panels** — X4 `800 − 2×79 =
  642` and X3 `792 − 2×79 = 634`, less the 253px of card that is not the title, over
  46 — where 79 is the badge's 34px offset plus its 45px box.

**MEASURED AGAINST ITS BOARD: 3.28% → 2.42% (X4) and 3.01% → 2.22% (X3)**, and the
board's own render is **byte-identical** before and after, so the whole gain is the
firmware moving toward an unchanged board (honouring the 1.1 closed most of the 7px
card-height gap). `sleep_cover_details` went **3.48% → 2.86%** and **3.18% → 2.62%**.
**THIS LINE USED TO SAY `0.27%`, "the closest panel on the sheet", AND THAT FIGURE IS
NOT REPRODUCIBLE** — a threshold-at-128 count over the `--export` panels puts the
pre-change screen at 3.28%/3.01%, and the same instrument reproduces this file's
recorded `sleep_cover_details` pair (3.48%/3.18%) **to the digit**, which is what says
the instrument is the one this file uses elsewhere and the 0.27% is the outlier. The
sheet still prints `ok` rather than a percentage (#41), so any figure here is a count
someone ran by hand: **quote the method with the number.**

**THE AUTHOR WRAPS TOO, TO TWO LINES, AND IT USED TO DO NEITHER — so a long name left
the card entirely.** `drawCentredText` places a run at `centreIn(0, contentW, w)`, and
`centreIn` returns a **NEGATIVE** half when the run is wider than the box: the name began
left of the card's padding, painted over both 2px borders onto the dither field, and was
clipped by the panel edge. "Fyodor Mikhailovich Dostoevsky" is 540px against a 312px
column and rendered as `ODOR MIKHAILOVICH DOSTOEVS` — cut at BOTH ends with no ellipsis
to say so, sitting on the frame. **Every golden passed, because every golden's author was
short.**

- **THE CAP IS TWO AND IT WAS MEASURED, not chosen.** Every `dc:creator` in the 225-book
  corpus, shouted, at `Label400`/0.22em against this column: **68 of 221 (30.8%) overflow
  one line**, so an ellipsis here is the COMMON case and not the edge one — and of those
  68, **58 (85%) fit WHOLE in two**. Only **five DISTINCT names** in the corpus need a
  third and three of the five are corporate, so **216 of 221 render complete**. **None of
  the 16 books on the user's own shelf overflow at all**, the INVERSE of the
  progressive-JPEG split, which is why the figure is quoted with its sources rather than
  as one number.
- **IT IS A LINE COUNT WHERE THE TITLE'S BOUND IS THE CARD'S ROOM, and the two being
  different KINDS is the design.** Both runs can grow now, and **one budget cannot serve
  two growable runs without saying which yields**. The author yields, FIRST and by a fixed
  amount, so the title — the one fact this screen exists to state — keeps every line left
  over. It is expressed as the ORDER of two statements in `renderSleep` rather than as a
  comment, so it cannot drift from what is drawn. A proportional split would let a
  three-line corporate name eat the hero.
- **AT THE BOUND THE TITLE PAYS, AND THE FIRST TEST OF IT ASSERTED THE OPPOSITE AND
  FAILED AT −17px.** That figure is the arithmetic being right: 29px of author bought
  against a 46px title line the budget then gave back. **A card at its bound cannot
  grow**, so measuring growth there measures the floor in the title's division instead —
  the cap is asserted where there IS slack and the ordering where there is not.
- **THE AUTHOR'S LEAD IS THE FACE'S OWN LINE HEIGHT**, not one of this screen's numbers,
  because the board leaves this run at `line-height: normal` — which is what
  `wrapProseLead` is for, and what makes a one-line author **byte-identical** to the
  `drawCentredText` it replaces. **No golden was re-blessed**: the eight existing sleep
  goldens are untouched and the two `sleep_long_author` files are the only ones added.
  Board fidelity did not move either — **2.42%/2.22%, 9301 differing pixels at both
  geometries**, and `sleep_cover_details` 2.86%/2.62%.
- **THE TEST THIS NEEDED WATCHES THE CARD'S PADDING, NOT THE PANEL EDGE.** The card is
  opaque white and only ever drawn in its content column, so the 42px band between each
  border and that column is **paper by construction** and ink there means a run escaped.
  The edge is where the damage ENDED — a test watching only the edge passes for a name
  that merely eats the frame. Restoring `drawCentredText` fails it with 385–435 stray
  pixels per geometry.
- **CHROME AND THE FIRMWARE DISAGREE ABOUT THIS RUN, which is why the corpus figure is
  the firmware's.** Chrome fits `MARY WOLLSTONECRAFT` in the column and the firmware does
  not, so that name is two lines on the board and **three** on the device. The boards'
  committed specimen is one line in both, so nothing on the sheet moves — but a
  long-author *specimen* would land differently in the two engines, the same ~3% the
  `.rfnt` faces measure wider everywhere else.

**THE CARD'S LAST LINE NAMES THE CHAPTER NOW, AND IT WAS THE LAST PLACE ON THE DEVICE
SHOWING A SPINE POSITION.** It read `6% · CH. 01` — a percentage and `last.spine + 1`,
composed in the shell — and this entry twice recorded a reason for that which had
expired: first that a chapter name "would need a table of contents, which is not
built", false since `Contents` shipped, and then that the run needed a bound and a
design decision, which is what this change made. **It was never Home's false claim**:
`CH. 01` quotes no total, so it invited no arithmetic and was merely *less
informative* than a name. So this was an improvement rather than a fix, and the
constraint was that it must not be bought with a regression.

**THE RUN IS TWO RUNS, BECAUSE A CHAPTER NAME DOES NOT FIT AND DOES NOT NEARLY FIT.**
Measured over 225 real books — 205 with a usable NCX, **8,617 chapter labels**, at the
shipped `Role::Label500` against the card's **312px** content column, which is 312 on
*both* panels because `max-width: 400` is under the X4's `480 - 2·kMargin` as well.
`tools/sleep_chapter_probe.cpp` is that measurement, tracked and re-runnable for
`name_probe`'s reason:

| the name gets | elides |
|---|--:|
| the row, beside `100% · ` (213px) | **52.51%** |
| the row, beside `6% · ` (246px) | 47.82% |
| its own line, 312px at 0.14em | 37.60% |
| **its own line, 312px at 0.10em** | **34.54%** |
| its own line, 0.10em, *shouted* | 36.73% |
| *Home's control*, `Meta400`/0.10em at 304 / 352px | 30.70% / 23.69% |

p50 **217px**, p90 536px, max 1887px, and only **33 of the 205** books have every
label fitting. So the median label overflows the combined run, and sharing that row
would show a cut name **more often than a whole one** — where its own line lands in
the band this project already accepted for the identical string.

**IT WRAPS TO TWO LINES, AND IT SHIPPED ELIDING ON ONE — THIS IS AN OWNER'S DECISION
AND NOT A NEW READING OF THE DATA.** The distribution permits either. What decides it
is the screen: the card holds the glass for **hours**, longer than anything else the
device draws, so a cut name is one the reader *lives with* rather than one they press
past. That is the argument that made the title wrap (#74) and then the author (#86),
arriving at the last run on the card.

**TWO LINES, AND THE DATA STOPS THERE.** Of the 8,617 labels, **2,976 (34.54%)**
overflow one line; of those 2,976, **2,274 (76.41%) fit two lines WHOLE** and 702
(23.59%) need three or more. So two lines elide **702 of 8,617 (8.15%)** where one line
elides 2,976 — a **4.2× reduction in cut names**, which is what earns the second line
and is the same shape of knee the author's cap was earned by (85% there). **A third
line was permitted by the owner and is not taken**: a smaller marginal gain bought out
of another of the title's lines. **Elision is moved, not removed** — `clampProse` cuts
the SECOND line for that 8.15%, so this is a change of degree.

**THE TWO LINES ARE RESERVED IN THE TITLE'S BUDGET WHETHER OR NOT THE NAME USES THEM,
AND THE CARD'S HEIGHT IS WHAT THE NAME ACTUALLY TOOK.** Those are **two quantities and
nothing requires them to be one number**, which is the whole of how a growable run is
safe here — and the first shape of this shipped them as one, so the reserve had to be
the height too.

- **`chapterReserveH` IS THE BUDGET'S**, and the objection it answers is real and is not
  answered by the rate: **A CHAPTER CHANGES WHILE THE BOOK IS BEING READ AND AN AUTHOR
  DOES NOT.** The title takes what the card's room leaves, so a budget counting
  this run's second line only when the name *used* it would make the **title's** budget
  depend on where the reader is standing — cross a chapter boundary and the book's name
  reflows, or newly acquires an ellipsis, *because a page was turned*. A visible defect
  with a baffling cause. Reserved either way, the title's budget is a **constant** and
  the card's LAYOUT is a function of the **book alone**, exactly as it was at one line.
  It is `kSleepGap + kSleepChapterMaxLines * lineHeight()` and it never reads the wrap.
- **`chapterH` IS THE CARD'S, and it is the wrap's own height** —
  `kSleepGap + f26ToPx(chapterProse.heightF26())`, `authorH`'s shape one run down. The
  reserve bought **nothing** in the height: this is the card's LAST run, so nothing
  below it steps up, and a one-line name left **29px of empty box** standing at the foot
  of a card that holds the glass for **hours**. Dead space, not spacing — and the common
  case, at 65.46% of corpus labels and every fixture's own specimen. The wrap is
  therefore **hoisted above `cardH`** rather than built at draw time; that placement is
  the mechanism, and building it at the draw is what made the reserve the only number
  the height could have.
- **THE BOUND HOLDS A FORTIORI**, which is worth saying rather than leaving implied:
  `cardH` can only be **shorter** than the room the budget was divided against, never
  taller, so `cardH <= cardRoom` is satisfied with 29px to spare on a one-line name. The
  card is CENTRED, so what a shorter card does is **re-centre** — no type moves relative
  to any other type.
- **DO NOT MAKE THEM ONE EXPRESSION AGAIN, IN EITHER DIRECTION.** They will read as one
  thing spelled twice. Giving the HEIGHT the reserve puts the dead space back; giving the
  BUDGET the actual reintroduces the reflow. Both sites say which quantity they hold and
  name the other, `design/Sleep.dc.html` says the same thing where its `min-height: 58px`
  used to be, and **each swap has its own test** — see the mutations below, which are the
  whole specification of the split.
- **The name is drawn at the TOP of the room `chapterH` gave it**, which for a one-line
  name is exactly one line: there is no slack under it any more. A short name and a
  two-line name still share a first baseline *relative to the run above them*; what a
  chapter crossing moves is the whole **card**, by 29px, re-centred.

**AND THE TITLE IS THEREFORE CONSERVATIVE BY UP TO ONE LINE, which is the stated cost of
the trade and is measured rather than argued.** A one-line chapter buys the title
nothing, so the only title that can notice is one needing exactly `budget + 1` lines —
and `sleep_chapter_probe` says that is **3 of 225 (1.33%)**: `The Fables of Aesop`, `The
Declaration of Independence of the United States of America` and `The Hacker Crackdown`.
**All three have one-line labels and none has only one-line labels** (39 of 90, 2 of 6,
4 of 23), so in those three books the seventh title line is given up in **45 of their 119
chapters** and kept in the other 74. Every other corpus title either fits its six lines
or would elide at seven as well. The probe answers this jointly — a title's line count
*and* that same book's labels — because the tail alone cannot: read it off
`the conservative budget's cost` at the end of a run.

**THE YIELD ORDER IS THREE-STAGE AND IS STILL THE ORDER OF THE STATEMENTS.** The
chapter yields first and absolutely (two lines *of budget*, always), the author second
by its fixed cap of two, and the title takes every line left over. **This entry used to
add that `chapterH` was "one expression spent twice", on `renderBookError`'s
two-copies-free-to-disagree argument, and that was the defect rather than the safeguard**
— the two spends want different numbers, and collapsing them is what put the dead space
in the card. What they really share is `cardFixedH`, which is one expression for that
reason. The claim that "a mutation that reserves it in the height only is caught by the
*pre-existing* badge-bound tests" was also **false in the direction that matters**: the
card gets SHORTER, so nothing overruns the badge and no bound test can see it.

- **WHAT IT COSTS: the title's derived budget goes 8 lines → 6 on both panels** — X4
  642px of card room and X3 634px, less the 325px that is not the title when the author
  takes one line, over the title's own 46px line box. **9 of the 225 corpus titles
  (4.00%) elide at 6**, against **6 at a budget of 7** and **4 at 8** — so the second
  reserved line costs **3** titles and the whole chapter run costs **5**. Read those off
  the tail `sleep_chapter_probe` prints rather than from here; it printed only the
  8-line answer until this change needed the 6-line one, which is the shape of a
  hardcoded figure in an instrument.

**AND ONE ASSERTION ON THIS CARD WAS MEASURING `mod 46` RATHER THAN THE DESIGN, WHICH
IS WORTH KNOWING BEFORE TRUSTING ANY HEIGHT COMPARISON HERE.** *"At the bound the title
pays"* was asserted as *a two-line author's card is no taller than a one-line author's*,
and that is not a property of the yield order at all. The card's height is
`F + A + C + 46·floor((cardRoom − F − A − C)/46)`, which **collapses to
`cardRoom − (budget mod 46)`** — so whether the author's second line costs the title a
line depends *only* on whether the one-line remainder reaches the 29px that line takes:

| | one-line remainder | a two-line author then |
|---|--:|---|
| with a one-line chapter | 24 (X4) / 16 (X3) | forces a title line back, card **−17px** |
| with a two-line chapter | 41 (X4) / 33 (X3) | fits the slack, card **+29px** |

**Both are correct renders** — 12px (X4) and 4px (X3) inside the bound and clear of the
badge — and the second is the *better* one, because the title keeps all six of its lines
instead of dropping to six from seven. **That assertion has now been wrong in both
directions**: its first version asserted the card GREW and failed at −17px, and the
version that replaced it asserted the card SHRANK and failed at +29px. A property that
flips sign when a neighbouring run takes one more line was an artefact both times. It is
replaced by the remainder-independent statement — **the card never leaves a whole title
line box unused** — and the order itself is asserted where it is observable, by the cap
costing exactly one extra line box where there IS slack.

**AND THAT STATEMENT NEEDED THE CHAPTER'S UNSPENT RESERVE ADDED BACK ONCE THE HEIGHT
STOPPED TAKING IT.** With a one-line chapter the card is 29px shorter than the division
it was budgeted by, so the bare form reads that 29px as room the hero was shortchanged
out of and fails at **70px (X4) / 62px (X3)** — measuring the *conservative budget*,
which is a deliberate trade with its own figures above, rather than the division the
assertion is about. `chapterSlackOf` is an **exact term and not a tolerance**: what is
compared is still the budget's own remainder, still required under one whole title line.
**The table's two rows are now the ONE-LINE and TWO-LINE AUTHOR against a reserve that is
always two**, so the reachable remainder is 41px (X4) / 33px (X3) with a one-line author
and 12px / 4px with a two-line one — and that pair is load-bearing for a *test* now: a
budget change is only visible where the remainder is at least `46 − 29 = 17px`, so the
two-line-author fixture cannot see it and the one-line-author one can.
- **`SleepViewModel::progress` IS GONE RATHER THAN RENAMED.** It held the whole
  composed string, so the figure under the bar and the length of the bar were two
  spellings of one fact that the shell could set independently. The theme composes the
  percentage from `progressPercent` — the field `drawProgressBar` already takes —
  exactly as `renderHome` does, and `chapter` carries `last.chapter`, the string the
  Reader's band, Contents' `NOW` row and Home's meta line all draw.
- **AN EMPTY CHAPTER COSTS NO LINE AT ALL** — not two blank ones — unlike Home's,
  which reserves its line because runs sit below it. This is the card's *last* run, so
  nothing below it steps up and an absent chapter simply shortens the card. An absent
  claim beats a false one: a pointer written before `last.json` carried a chapter must
  not fall back to the position this run has just stopped showing.
- **0.10em ON THE NAME AND 0.14em ON THE PERCENTAGE**, which is Home's split and its
  reason (0.14em is a *counter's* tracking and a name is not a counter) and is 3.06
  points narrower on the one run whose width is the whole problem. **Not shouted**,
  although the title and author on this card are: three screens already name this
  string as authored, `toc.h` hands it over "as authored" for that reason, and shouting
  measures **2.19 points wider**.
- **WEIGHT 500, WHERE THE BOARD SAID 700.** The ramp carries `Label400`/`Label500` at
  11pt and **no `Label700`**, so 700 asked for an asset nobody has — `LowBattery`'s
  band label's defect exactly — and the firmware has always drawn `Label500` here.

**AND THE NAME MAY NOT GO THROUGH `drawCentredText`, which is what this run did while
it was a position.** That function places a run at `centreIn(0, contentW, w)` and
`centreIn` returns a **negative** half for a run wider than its box: an unbounded name
begins left of the card's padding, paints over both 2px borders onto the dither field,
and is clipped by the panel edge with no ellipsis to say so — the defect the AUTHOR
line one run above was fixed for, and at 34.54% it would have been the **common case**.

**THE WRAP IS WHAT KEEPS IT IN THE COLUMN NOW, and `clampProse` is the last resort
rather than the mechanism.** `WordBreak::Anywhere` means every line the wrap emits is at
most `contentW` wide, so `centreIn`'s half cannot go negative for any of them — and that
holds for a name with no space in it, which is what `Anywhere` is for. The clamp then
cuts the second line for the 8.15% that need a third.

**THE TEST WATCHES THE CARD'S PADDING, AND ON THIS SCREEN THAT IS THE ONLY PLACE INK IS
EVIDENCE AT ALL.** The 42px band between each border and the content column is paper by
construction. The two obvious alternatives were *tried against the mutation and both
are blind*: the panel EDGE is inked on every row by the dither field, and the card's own
side BORDERS are inked on every row of the card, so neither a full-row scan nor an
in-card extent separates the run's ink from furniture that belongs there.

**IT IS PROVED BY THREE MUTATIONS AND IT BITES ON ALL OF THEM.** Restoring
`drawCentredText` on the raw name — the original defect — fails on **all four** of its
specimens at both geometries, with **508, 440, 723 and 742** stray pixels; dropping
`WordBreak::Anywhere` to `Normal` fails on the two unbreakable-token names with **723
and 742**, which is the case a real label with spaces cannot reach. Its four names are
two real labels either side of the two-line cap plus a 120-byte token and FAT's 255-byte
maximum, so the wrap and the clamp are both covered and nothing depends on where a space
happens to fall.

**AND THE RESERVE/ACTUAL SPLIT IS PROVED BY TWO MORE, ONE PER DIRECTION, EACH FAILING A
TEST THE OTHER DOES NOT.** Those two mutations *are* the specification: if either passes,
the distinction is not enforced.

| swap | fails |
|---|---|
| `cardH` takes `chapterReserveH` | **14** assertions over 6 cases — **4** in `the card's HEIGHT follows the chapter's actual wrap` plus the **10** sleep goldens. Budget test: **0**. |
| `maxTitleLines` takes `chapterH` | **4** assertions over **1** case, all in `the TITLE's budget does NOT follow it`, reporting the bar moved **46px** — one whole title line — between a one-line chapter and a two-line one. Height test: **0**, **and nothing else in the suite moves, goldens included.** |

- **THE BUDGET SWAP IS INVISIBLE TO EVERY GOLDEN**, which is why it needed a test of its
  own rather than a re-render: every golden's title fits inside its budget's slack, so an
  extra line of budget changes nothing that is drawn. **1,325,843 assertions stayed green
  under it** before the test existed.
- **AND THE BUDGET TEST'S FIRST FIXTURE COULD NOT SEE IT EITHER** — the recorded lesson
  again, that *a mutation tells you about your INPUT before it tells you about your
  test*. It copied `THE AUTHOR IS CAPPED AT TWO LINES`', which maximises the **author**
  too, and a two-line author leaves a remainder of 12px / 4px: 29px more budget still
  floors to the same six lines. With the board's one-line author the remainder is
  41px / 33px and the crossing happens. **Both conditions are asserted now** — the title
  fills its budget, and the remainder is at least `46 − 29` — so the test says so instead
  of going quiet.
- **A FIXTURE GUARD HAS TO BE BLIND TO THE DEFECT IT GUARDS.** Those two `REQUIRE`s are
  measured on the **two-line** chapter render, where the reserve is exactly what the name
  takes and both spellings of the budget give the same card. Taken from the one-line
  render the mutation moves them itself, the remainder goes to **−5px**, and the guard
  fires with *"this fixture cannot see the defect"* about a fixture that sees it
  perfectly well — **a guard that accuses the fixture when the code is wrong is worse
  than no guard.**
- **THE HEIGHT TEST HAD TO BE BLIND TO THE BUDGET AND VICE VERSA**, and the two are made
  so differently. The height test uses the board's **one-line title**, so the division
  above it has slack and cannot change what is drawn however the chapter is counted into
  it. The budget test cannot use the card's height at all — the height is *supposed* to
  move there — so it reads the title's line count off the frame as
  **`barTopOf − cardBox().top`**, the bar's distance below the card's own top, which is
  every fixed term plus the author's height plus the title's with the author held still.

**THE MIDDLE DOT'S TRAP OUTLIVED THE LITERAL THAT CARRIED IT, and the duplicated note
was the tell.** `"%d%%\xC2\xB7CH. %02d"` parses `\xB7C` as ONE hex escape, because a C++
hex escape is **unbounded**: clang rejects it outright and the ESP32's GCC **accepted
it** and emitted a byte that is not U+00B7 (this file records the same shape for
`"\xA0b"`, which is 0xA0B). Adjacent literals end the escape. That `snprintf` is gone
with the composition, and **the note describing it stood in `main.cpp` twice, verbatim**
— which is this file's own rule arriving: when a replacement leaves a condition stated
twice, one of them is stale. The trap is real and now lives only where the middot does,
`screens.cpp`'s `kDot` and the badge's own literals, where the bytes are their own
adjacent literal by construction.

**MEASURED AGAINST THE BOARDS: `sleep` 2.32%/2.13% → 2.30%/2.11%, `sleep_waking`
2.16%/1.99% → 2.15%/1.97%, `sleep_cover_details` 2.74%/2.50% → 2.87%/2.63%.** Two of
the three ended up *better* than before the run wrapped and the third is 0.13pp worse.
Threshold-at-128 counts over the bare `--export` panels, since the sheet still prints
`ok` (#41).

**THE WRAP ALONE COST 1,500 PIXELS A BOARD, AND THE CAUSE WAS THE TITLE'S `1.1`.** With
the wrap in and the title still declaring `line-height: 1.1`, `sleep` went to
**2.71%/2.49%** — and the mechanism is worth keeping because it will catch the next
person who changes a run's height on a centred card:

- **Chrome resolves `1.1` on `--t-title` to a FRACTIONAL 46.2px** where the firmware
  derives `round(1.1 * 42) = 46`. That was the only fraction on this card. It made the
  card's own height fractional, and the card is **centred**, so `centreIn` halved the
  fraction and the runs below the title painted at positions the firmware does not
  compute.
- **It cost nothing until the card changed PARITY.** The reserved second line is 29px —
  **odd** — so it flipped the half pixel the centring had been absorbing, and the
  author, the bar, the percentage and the chapter all went from 0–2px out of register
  to 1–3px. Chrome's painted card grew **+30px** where the firmware's grew +29.
- **IT WAS A REAL REGISTER REGRESSION AND NOT THE `Names` CUT'S COUNTING ARTEFACT, and
  this file's own discriminator is what settled it.** A **±1-row-tolerant** count did
  **not** absorb the rise (5,456 → 6,988), and the two card borders sat **beside** each
  other two pixels apart rather than the design's **straddling** the firmware's. Both
  tests point the same way, which is what makes the verdict safe.
- **The fix was to state the number, not to swap instruments**: the three boards say
  `line-height: 46px`, which is what `1.1` was chosen to mean. **No golden moved** —
  `kSleepTitleLineH` was already 46, so only the board's render moved, toward the
  firmware. Same lesson as the chapter run's own `normal`, and `Peek.dc.html`'s panel
  height.
- **The obvious symmetry is WRONG and was measured rather than assumed**: giving the
  AUTHOR run `line-height: 29px` too makes the screen **worse** (2.45%/2.25% against
  2.30%/2.11%), so Chrome's `normal` there is not the same disagreement. Left alone.
- **`sleep_cover_details`'s residual 0.13pp is the stated cost and is inherent.** Its
  card sits over a **four-level dithered cover** rather than the plain field, so the
  1px the card's painted extent still differs by mismatches cover pixels along both
  boundary bands, where the same offset over the field costs far less. Growing a centred
  card by an ODD number of pixels over a dithered photograph is what the design decision
  buys; there is no even-sized line box to grow it by.
- **AND THE PARITY MOVED ONCE MORE, WHEN THE HEIGHT STOPPED TAKING THE RESERVE — the
  same odd 29px, in the other direction, and it did NOT redden the register.** The board
  dropped `min-height: 58px` and Chrome's card shrank by exactly the 29px the firmware's
  did, so the design's card top sits **1px above** the firmware's before *and* after,
  measured. What the strict count does is what this bullet's own lesson predicts:
  `sleep` **2.30%/2.11% → 2.32%/2.13%** (+64 px, +0.02pp) while the **±1-row-tolerant**
  count *falls* 4,326 → 4,152, and `sleep_waking` the same +64/−174. So the rise is the
  half-pixel phase and not drift — **both discriminators point the same way**, which is
  what makes that verdict safe here as it did for the `Names` cut. `sleep_cover_details`
  improves on **both** counts (2.87%/2.63% → **2.74%/2.50%**, −516 strict, −437
  tolerant), which is this bullet above being paid back: the card it centres over that
  dithered cover is 29px smaller. `sleep_cover` stays **0.00%** and `sleep_idle`
  **0.26%/0.24%** to the pixel — neither draws the card.

- **IT READ AS A REGRESSION FIRST, AND IT WAS NOT THE PHASE ARTEFACT IT LOOKED LIKE.**
  With the chapter run's line box left at `line-height: normal` the Sleep board went to
  **3.03%/2.78%** — worse than before the change — because Chrome resolves `normal` to
  **30px** where the face's own line height at this ppem, which the firmware derives, is
  **29**. The card is CENTRED, so one pixel of height is half a pixel at both ends and
  every rule inside it goes out of register.
- **THE DISCRIMINATOR IS THIS FILE'S OWN, AND IT ANSWERED THE OTHER WAY THIS TIME.**
  The `Names` cut records a rise in this same count caused by a half-pixel phase flip,
  where Chrome rasterises a rule across two rows of grey 127 and the count scores both
  as ink; the test given there is whether the design's rule **straddles** the firmware's
  or sits **beside** it. Here both borders were **solid black two pixels apart**, so it
  was a real geometry disagreement — and the fix was to state the number, not to swap
  instruments. The three boards declare `line-height: 29px`, the card's borders go from
  2px out of register to 1px, and the rows differing over half the panel go **8 → 4**.
  This is the Sleep TITLE's own lesson one run lower (its `1.1` exists because the face
  would have given 53 where the board wanted 46) and `Peek.dc.html`'s, where stating the
  panel's own height "closed a disagreement rather than documenting it".

**FOURTEEN GOLDENS TOUCHED — TWELVE RE-BLESSED AND TWO ADDED — WITH THE EVIDENCE PER
PIXEL.** Every differing pixel sits inside the card's own 400px column — x 40..439 on
the X4 and 64..463 on the X3, **not one pixel outside** — so the dither field, the badge
and (on `sleep_cover_details`) the four-level cover are untouched outside it.
`sleep_idle` and `sleep_cover` produced **no candidate at all**, which is what says the
badge-only and cover-only screens were unaffected.

**AND ON THE TEN WHOSE CHAPTER IS THE ONE-LINE SPECIMEN THE PROOF IS EXACT: the card's
content is BIT-IDENTICAL with 29 blank rows of interior inserted above the bottom
border.** Longest common prefix plus suffix covers **342 of 342** card rows on `sleep`
and `sleep_waking`, 434 of 434 on `sleep_long_title`, 371 of 371 on `sleep_long_author`
and 388 of 388 on `sleep_cover_details` — so nothing was redrawn, restyled or moved
*relative to the card*: one band of paper was pushed in at the foot, the card grew by
it, and being centred it then moved up by half of it (−14px, or −15 where the leftover
is odd). That is a stronger statement than "the difference is confined", and it is the
one worth reaching for when a change should be a pure insert.

- **`sleep_long_chapter` IS THE WRAPPING SPECIMEN NOW, WHERE IT WAS THE ELIDING ONE.**
  `PREMIÈRE PARTIE : À LIRE AVANT L'ACHAT` is 526px against a 312px column — 1.7 lines
  — so it stopped exercising the ellipsis the moment the wrap landed, exactly as the
  author's specimen would have if it had ever come in under two. It renders the whole
  name on two centred lines, broken at the space, and it carries the **accented capital
  path through a WRAP**, which is the case this file flags: the grave sits close to cap
  height, and there is clear paper between it and the cap-tops of the line below.
- **`sleep_elided_chapter` IS THE NEW PAIR, AND `chapterTail` IS WHY IT EARNS ONE.**
  That local exists only on the clamping path — `clampProse`'s elided line is a NEW
  string, not a view into the name — and Home shipped precisely that mistake inline
  once and drew a column of **notdef boxes**: right for a short string and wrong for a
  cut one, which is silently right in exactly the case every other fixture covers. Ink
  that spells nothing inks rows exactly like ink that does, so no geometric assertion
  can see it. **Proved by mutation: removing the clamp is caught by this golden and by
  NOTHING else in the suite** — the padding test cannot see it, because the wrap keeps
  every line inside the column and the overflow goes downward.
  Its specimen is a real Verne heading off the probe's own widest-per-book list,
  1887px, in capitals because the book authored it so and stopping mid-word at `SEEI`
  because it is 128 bytes — `toc.h`'s `kMaxTocLabelBytes` doing its job, so the fixture
  is what the device would actually put on the glass.
- **A guard asserts one specimen wraps whole and the other is cut** (`== 1`, `== 2`,
  `>= 3`), because this run now has TWO boundaries and a fixture either side, so there
  are two ways to go quiet — and neither shows up as a failure anywhere else: both
  goldens would simply be re-blessed onto a shorter render. Shortening the eliding
  fixture fails it with `2 >= 3`.

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

## The battery

**Home is the only screen that reports charge**, and until 2026-08-29 it reported a
hardcoded `87%` -- the board's number, set by all three demo view-models and written
by nothing in `shell/`. `BatteryMonitor` had been a declared `lib_dep` since Phase 2
and had never been constructed.

**The backend is chosen at RUNTIME, per board profile**, which is what lets one C3
binary serve both models -- and it means the two models legitimately report at
different granularity:

| | X3 | X4 |
|---|---|---|
| backend | BQ27220 I2C gauge, 0x55 on SDA20/SCL0 | ADC on GPIO0, divider 2.0 |
| percentage | true SoC, per 1% | `percentageFromMillivolts`, **multiples of 10** |
| charging | sign of the gauge's `Current()` | **never** -- `NO_GAUGE`, no charge pin |

`percentageFromMillivolts` quantises deliberately: voltage cannot resolve a Li-ion
pack finer than that, and pretending otherwise produces a number that wanders while
the battery sits still. So an X3 reading `64%` beside an X4 reading `60%` is not a
bug, and `[battery] ... (I2C gauge|ADC backend)` on the boot line is what settles it.

**THE X3 PATH WAS PROVEN BEFORE IT WAS WRITTEN**: `XteinkDetect::probeBq27220`
already reads that gauge's SoC and voltage on every boot, and X3-vs-X4 detection
needs two of its three I2C chips to answer on both passes -- so the panel driver
this firmware selects already depends on the gauge responding.

**`-1` MEANS THE GAUGE DID NOT ANSWER, and the band then draws its mark alone.** Not
`0%`: `readPercentage()` answers a failed read with `0` and `percentageFromMillivolts`
maps a failed `0 mV` to `0%` rather than `100%`, so a `0` taken at face value puts a
flat battery on the panel of a device that is fine. Same call `homeVmForCard()` makes
for the LIBRARY row's count -- "no books" and "could not look" are different claims,
and so are "flat" and "did not answer". `BatteryTracker` keeps the last good value,
so one transient I2C miss does not blank a number that was right two seconds ago.

**THE CHARGING MARK IS A SECOND ICON, NOT A FLAG ON THE FIRST.** `kBatteryCharging`
is `kBattery` with a bolt knocked out of its fill, generated by `iconc.py` from
`design/HomeCharging.dc.html` -- **its own board**, because `iconc.py`'s battery
matcher keys on the bolt's own path (`M11.4 2`), not the terminal nub
(`<rect x="19.5"`), which is on BOTH batteries and identifies neither. `source` is a
second line of defence, exactly as it is for `kBook`/`kBookLarge`. `renderHome`
chooses between them **once**, at the top, because there are two draw sites (the band
and the `nothingToContinue` strip) and a choice made twice is one that will
eventually be made differently in the two places.

**PLUGGING IN REPAINTS HOME, AND THE LATCH IS THE WHOLE DESIGN.** There is no
plug-in event to hook: `BoardProfile::usbDetect` is `20` on both Xteink profiles, set
positionally with no comment, **nothing in the SDK reads it**, and on the X3 GPIO20 is
the gauge's own SDA. So the shell polls `isCharging()` every 2 s while Home is on
glass — **and "while Home is on glass" is now also what BUYS the 2 s**, since #96
made the cadence follow the state: `bandRepaintPossible()` is one of the two things
that select `kPollFastMs`, so this latch is sampled at exactly the interval it was
designed at wherever its repaint can reach the panel, and at `kPollSlowMs` where it
cannot. See **the cadence** under the safety ladder for what that costs. The hazard
is that the X3 has no charger IC, so `isCharging()` is
`(int16_t)Current() > 0` -- **a bare sign test with no deadband** -- and plugged in at
full charge is ~0 mA with a dithering sign, which is the state a device spends all
night in. `BatteryTracker` answers it four ways: a rising edge (a plug-in), a first
reading that seeds without firing (so booting on the cable adds no refresh), a latch
that clears only after **60 s of CONTINUOUS** not-charging (so a dither can never
accumulate enough to re-arm it), and **three grants per session** as a backstop. It
is in `core/` for `ProgressSaveGate`'s reason -- a latch with a dwell timer and a
session cap, and `shell/` has no harness.

**UNPLUGGING NOW SPENDS A REFRESH TOO, AND IT DID NOT AT FIRST.** The original
design fired on the rising edge only and left the falling edge free, on the stated
argument that "the stale bolt is corrected by the next Home paint" -- which assumes
a button press. Reported from an X3: the bolt appears correctly within ~2 s of
plugging in and then **stays on glass indefinitely after unplugging**, because a
reader sitting on Home reading nothing presses nothing, and nothing else repaints
it. A false "charging" claim is the same defect class this project already refuses
for an unread gauge (`-1`, not `0%`) and a book with no reading position (no demo
substitute) -- a false claim is worse than an absent one, and this was one.

The fix fires the repaint at the moment the **60 s dwell confirms the unplug**,
rather than on the bare falling edge -- a plain falling edge would reintroduce
exactly the flicker the latch exists to prevent, since the dithering sign trips it
every few seconds at full charge. It needs no new constant: the dwell was already
computed to decide whether to clear `latched_` and was simply not acted on. So
`latched_` clearing and the clearing repaint are now the same event, gated the same
way the rising edge always was.

**THE GRANT BUDGET IS SHARED BY BOTH EDGES, NOT ONE EACH.** `latched_` clears
UNCONDITIONALLY -- it tracks reality, and must not stay true just because the
session ran out of repaint budget, or the next real plug-in would be wrongly
refused as "already latched". Only the repaint itself is gated on
`grants_ < kMaxGrantsPerSession`, from the same counter the rising edge spends, so
**a full plug/unplug cycle can now cost up to two grants instead of one**. Accepted
deliberately: three grants was already a backstop against a hardware quirk this
project cannot bench-test, not a promise of exactly one refresh per cycle.

**The poll needs no `SpiBusGuard`**, and that is what makes even the fast cadence
affordable: it is I2C on the sensor bus and cannot race a panel refresh. It is gated on
`gChargingObservable`, which is **STICKY, NOT DECIDED FROM THE FIRST READING**: it is
set by ANY reading that reports `chargingKnown`, because `readStatus()` reads SoC and
charging as two independent I2C transactions, and deciding this from one sample would
let a single glitch on the charging half disable the poll for the rest of the
session on hardware that supports it perfectly well. It never arms at all on an X4,
which has no charge-status pin and so never reports `chargingKnown` from anything.

**THE POLL IS OTHERWISE INVISIBLE, THE SAME SHAPE THIS FILE ALREADY RECORDS FOR THE
LISTING CACHE AND THE RING WARM.** Its only other trace is the one-shot `[battery]`
boot line and an occasional `-> repainting Home`, so a disarmed poll, one pinned by
`kMaxGrantsPerSession`, and one quietly seeing no change all read identically: nothing.
`[alive]` carries `battery observable=%d pct=%d charging=%d polls=%lu`, read straight
off `gChargingObservable`/`gBattery` rather than re-derived, so it can never disagree
with what `setBattery()` just handed the screen.

**`App::markDirty()` IS THE FIRST DIRTY THAT NO PRESS CAUSED.** It does not set
`transition_`: a charge state appearing is not a screen change and takes the 389 ms DU
rather than the 693 ms GC. It also resets the partial-repaint record, because every
earlier route to `dirty_` was a dispatch or a push/pop and `canRenderTopOnly`
quietly rested on that -- with an overlay on top, a bare `dirty_ = true` would have
passed every other clause and left `renderTopOnly` repainting the overlay alone
while whatever `markDirty` was actually for stayed stale on glass.

**`drawHeaderBand` LOOKS LIKE IT HAS A BUG WITH AN EMPTY VALUE AND DOES NOT.** It
computes `groupW = vw + kBandGap + mark->w`, which reserves a gap for a number that is
not there -- but the icon draws at `groupX + vw + kBandGap`, so the phantom gap
**cancels** and the mark lands on the margin exactly. "Fixing" `groupW` alone pushes
it 7px PAST the margin on every screen that draws a band. The only real effect is
`labelMaxW` 380 against 387 at 480 wide, for a label that on Home is the literal
`NOW READING` and never elides. Left alone deliberately; see
`docs/superpowers/specs/2026-08-29-home-battery-design.md`.

**#82 SPLIT THAT EXPRESSION AND CHANGED NEITHER HALF OF IT.** The mark's
reservation is `markW` now and it comes out of the row **before** either run is
measured (`avail`), so the two runs divide what is left; the icon still draws at
`groupX + vw + kBandGap` and the phantom gap still cancels. Home's shared row is
still the 380 above, and `NOW READING` (207px) still takes its natural width
beside a percentage. Nothing on this paragraph became false — read it before
touching that arithmetic, and see the `labelShare` bullet under **Invariants
worth not relearning** for what the runs do with the row once it is theirs.

**`BatteryMonitor`'s CONSTRUCTOR CAPTURES THE BOARD PROFILE BEFORE THE PROBE HAS
RUN, and it is harmless for a reason worth writing down rather than re-deriving.**
`gBatteryMonitor` is a file-scope static, so it is constructed before `setup()` and
therefore before `detectAndSelectBoard()` -- and its constructor copies `_adcPin`,
`_dividerMultiplier` and `_chargeStatusPin` out of `BoardConfig::ACTIVE`, which at
that moment is still the compile-time default. Two independent things keep it from
biting: `readStatus()` tests `ACTIVE.batteryGauge.gaugeAddr` **live** rather than
from a cached member, so a real X3 takes the gauge branch and never consults those
members at all; and the X4's own ADC values happen to equal the default's. **The
second of those is a coincidence, not a design**, so a profile whose `batteryAdc`
differs from the default's would need this object built after the probe instead.

**A PLUG-IN REPAINT CAN BE PRE-EMPTED BY AN ALREADY-DUE SLEEP.** `gIdle` is checked
earlier in `loop()` than the dirty-driven paint, and plugging in does not count as
activity -- so an edge detected in the last seconds before the idle timeout can set
`markDirty()` and then have `sleepNow()` (which is `[[noreturn]]` and bypasses `App`)
fire first. Harmless and self-healing: deep sleep is a chip reset, the flag goes with
RAM, and the first Home paint after the wake reads the gauge fresh. Worth knowing
only because it looks like the latch failing when it is the timer winning.

## The safety ladder (#9, #10)

**TWO CARDS, ONE MECHANISM.** The low banner and the critical shutdown are two rungs
of one ladder and share the piece that did not exist before them — a battery reading
taken on **every** screen. Building either alone builds the whole watcher and uses one
rung of it, and the second consumer is what proves the abstraction: the second copy is
the extraction point, and here both copies arrived in the same change.

**`BatteryTracker` ANSWERS WHICH RUNG THE PACK IS ON; NOTHING ELSE THRESHOLDS A
PERCENTAGE.** `BatteryLevel { Normal, Low, Critical }` and `level()` ride the same
`update()` as the charge latch above, because two objects fed the same reading would
be a caller list — the shape this file names as a function not yet written — and the
shell would be free to feed one and forget the other. One `update()` cannot be
half-fed. Its consumers are `armBannerIfNewlyLow()` and the `Critical` test at the top
of `loop()`, plus `level=` on the `[alive]` line; the numbers stay in the header with
their derivation, because a threshold spelled at a call site is one that gets spelled
differently at the next.

**THE THREE THRESHOLDS ARE THE X4'S NOTCH TABLE AND NOT ROUND NUMBERS.**
`percentageFromMillivolts` walks `LIION_NOTCH_MV[11]` and returns a **multiple of
ten**, so a threshold that curve cannot express is one that never fires on half the
fleet:

| constant | value | why that value |
|---|---|---|
| `kLowPercent` | 10 | the X4's lowest non-zero notch, ~3.68 V. Anything lower is unreachable there until the pack already reads `0`. |
| `kCriticalPercent` | 3 | X4-reachable only as the notch `0`, which is ≤3.565 V. Read literally on an X3, where it is minutes of runtime rather than a cliff. |
| `kResumePercent` | 15 | the resume gate's floor. It requires the X4's **20%** notch, ~3.71 V. |
| `kCriticalDwellMs` | 10 s | continuous, in `kUnlatchMs`'s idiom. A panel refresh is the heaviest load this device draws and the SDK's 0% anchor is sized to leave headroom for that sag, so one low reading is not a flat pack. The poll already runs only in the `quiet` window, which excludes a sample taken mid-waveform; the dwell is the belt to that braces. |

**THE HYSTERESIS IS THE LOAD-BEARING NUMBER, AND IT IS 145 mV.** Put
`kResumePercent` anywhere an X4 can satisfy at the `0`/`10` boundary and the shutdown
edge and the resume edge become the *same* 3.565 V midpoint — so a device left on the
cable shuts down, charges for a minute, wakes, discharges and shuts down again, all
night. Requiring the next notch up is what makes the two edges different voltages, and
it is what `kResumePercent = 15` buys. The price is stated rather than hidden: **a
device refused at 14% cannot be forced on, on either model, even plugged in.**

**THE BOARD'S `BATTERY LOW · 5%` IS A DISPLAYED VALUE AND WAS NEVER A TRIGGER.** 5 is
an X3 specimen; the X4 has no fuel gauge, so `10%` is the only value its banner can
ever show and `BATTERY LOW · 10%` is the widest string the firmware can produce. That
one fact settled the banner's type role — see below.

**CHARGING SUPPRESSES `Critical` AND NOT `Low`.** A device on the cable must not shut
down, and the battery is still low, so the banner saying so is still true. **An X4
never reports charging at all** (`NO_GAUGE`, no charge pin), so there nothing
suppresses — and shutdown-then-refuse-to-wake is exactly right for a flat X4 on a
cable: the glass says `CHARGE · HOLD POWER TO WAKE`, and once it is charged, holding
power does. **THE `and it does` HERE USED TO BE ATTACHED TO A BARE `CHARGE TO WAKE`, AND
THAT WAS FALSE** — charging alone wakes nothing on this hardware; it only makes the hold
succeed. See the badge paragraph under `ScreenId::BatteryEmpty`. **A reading with
`percentKnown == false` holds the level where it was and cannot advance the dwell**:
"flat" and "did not answer" stay different claims, as they already do for `percent()`'s
`kUnknownPercent`, and a dwell satisfied by silence is a shutdown nothing confirmed.
`Low` has no dwell — the cost of being wrong there is one banner.

**THE POLL RUNS ON EVERY SCREEN, WITH NO `gChargingObservable` GATE.**
`pollBatteryLevel()` is `readBattery()`'s second caller, in `loop()`'s
`quiet` window; `refreshBatteryOnHome()` keeps both of its gates unchanged, because
what it drives is Home's band and the charge-latch repaint. Neither of those gates can
serve a safety mechanism: `homeOnGlass()` means a reader an hour into a book has had no
reading taken at all, and `gChargingObservable` is never true on an X4, so on that
model nothing would ever read the gauge. Both callers go through one `gBattery.update()`,
so the level and the band cannot disagree about the percent.

**AND ITS CADENCE IS NOW ASKED FOR RATHER THAN FIXED (#96) — BECAUSE THE THREE
CONSUMERS OF ONE `update()` DO NOT WANT ONE INTERVAL.** The ticket asked to push the
interval "into the minutes" on the premise that *"polling is only used to show the low
battery banner"*, and **that premise is incomplete in the way that decides the fix**.
The same `gBattery.update()` serves:

| consumer | tolerates |
|---|---|
| the `Low` banner | **minutes.** Being late costs one warning arriving late. |
| `Critical` → `criticalShutdown()` | **not minutes.** It is `[[noreturn]]`: `saveReadingPosition("battery")`, paint `BatteryEmpty`, cut the rails. Every second of extra detection latency is a second in which a brownout beats the save, and the save is what `markSleeping()` then redeems. |
| the charge latch | **not minutes.** Plugging in has to feel immediate, and `kUnlatchMs`'s dwell is the whole anti-flap design. |

**AND THE INTERVAL IS WHAT BOTH DWELLS ARE MEASURED ACROSS, so a coarser cadence does
not lengthen a dwell — it THINS it.** `kCriticalDwellMs` and `kUnlatchMs` are
timestamp arithmetic over a *sequence of samples*, not sample counts: at a 60 s
interval, `kUnlatchMs`'s "60 s of **CONTINUOUS** not-charging" degenerates into "two
samples 60 s apart", which is a claim about two instants rather than about a minute.
**That is the bound, and it is what the slow interval is derived from rather than
picked.**

So `BatteryTracker::pollIntervalMs(bandRepaintPossible)` answers from the STATE:

- **`level() != Normal` → `kPollFastMs` (2000).** Everything downstream of the `Low`
  crossing — the banner, the critical dwell, the shutdown — runs at exactly the
  cadence that shipped. **THIS ARM IS STRUCTURAL, NOT A PREFERENCE:** the critical run
  can only be armed by a reading that has already set `level_ = Low`, so the interval
  chosen after it is fast **by construction** — and the critical dwell is therefore
  never sampled at an interval longer than itself, which it could not survive
  (`kPollSlowMs` is 30 s against a 10 s dwell).
- **the band's repaint could reach the glass → `kPollFastMs`.** That is
  `bandRepaintPossible()` — `gChargingObservable && homeOnGlass()` — so `kUnlatchMs`'s
  dwell is sampled at 2 s in every state where its repaint can actually reach the
  panel, which is also where plugging in has to feel immediate.
- **otherwise → `kPollSlowMs` (30000).**

**`kPollSlowMs` IS `kUnlatchMs / 2`, AND THAT IS WHY THE TICKET'S "MINUTES" IS
DECLINED.** Minutes start at 60 s and `kUnlatchMs` **is** 60 s, so a minute-long
interval is exactly the degenerate case above; half of it is the coarsest cadence at
which no single interval can span that dwell. **It concedes almost nothing, because
the benefit saturates and the guarantee does not**: 2 s → 30 s removes **93.3%** of
the readings and the next doubling to 60 s buys **3.4 points more** while halving the
samples both dwells rest on. If minutes are ever wanted anyway, the honest route is to
lengthen `kUnlatchMs` with it — not to move one number.

**AND SLOWING ONLY THAT CASE IS NOT A COMPROMISE, BECAUSE IT IS WHERE THE READINGS
ACTUALLY ACCUMULATE.** A device idling on Home sleeps after `sleepAfterMs` (300 s by
default), so it can never take more than **~150** readings before the chip resets. A
device awake for an HOUR is one whose buttons are being pressed — somebody reading —
and then the **Reader** is on glass rather than Home, the band's repaint is
unreachable, and the ladder is the only consumer that wants the gauge at all. So the
slow arm catches the long session and the fast arm keeps the short one.

**WHAT IT COSTS, and it is one interval and no more:** a real `Low` crossing is
noticed up to 30 s late. The pack absorbs that — the `Low` band runs from
`kLowPercent` down to `kCriticalPercent`, and on an X4, which reports **10% notches**,
it is a whole notch. Either way it is hours of discharge, so 30 s cannot skip it.
`test_battery_tracker.cpp` drives the **closed loop** — the tracker choosing the
cadence at which it is next fed — and pins the whole bill: a flat pack reaches
`Critical` at `kPollSlowMs + kCriticalDwellMs` with **zero** slow gaps after the run
is armed. Sampling it slowly instead puts the shutdown at 60,000 ms against 40,000 ms,
with the dwell spanned by a single gap; that is the mutation, and it is the naive
one-interval-made-bigger fix.

**AND THE TRACKER DOES NOT DEPEND ON THE SHELL OBEYING IT.** A caller that ignored
`pollIntervalMs` and polled slowly for ever must still shut the device down — later,
never not at all — which is asserted, because a safety mechanism resting on an
interval a caller remembers to ask for is the caller-list shape this file turns into a
function. The one guarantee the slow cadence genuinely weakens is stated rather than
hidden: a dithering `Current()` sign sampled at 30 s could clear the latch spuriously
off Home. **It cannot flicker the panel doing so** — the repaint is gated on the same
predicate, so nothing reaches the glass — and the worst a spurious cycle costs is
`kMaxGrantsPerSession`, which is what that cap is for.

**`bandRepaintPossible()` IS ONE PREDICATE BECAUSE IT HAS TWO CALLERS.** It was
already spelled at the repaint site; the interval chooser needs the same answer, and
two spellings would drift **silently in the worse direction** — a cadence that
believed the repaint reachable while the repaint site did not would hold the fast
interval for a refresh that can never fire, which is precisely the battery this ticket
is about. Same rule as `homeOnGlass()` one line above it.

**WHAT A READING COSTS, DERIVED FROM THE REGISTER MAP AND THE PROFILE — NOT
MEASURED.** On the **X3** `readStatus()` is **three** BQ27220 reads over I2C at
400 kHz (`StateOfCharge` 0x2C, `Voltage` 0x08, `Current` 0x0C), each moving five
address/data bytes with a repeated start — ~115–130 µs of bus time apiece, so
~350–400 µs, and `readBattery`'s own **~450 µs** includes the Wire driver. On the
**X4** it is `NO_GAUGE`, so **one** `analogReadMilliVolts` on GPIO0 and no charge pin
at all. At 2 s that is a **0.02%** duty cycle.

**AND THE HONEST ANSWER TO "WHAT FRACTION OF AN IDLE DEVICE'S POWER" IS THAT IT IS NOT
MEASURABLE FROM HERE, WHICH IS ITSELF THE FINDING.** The only instrument the device
carries is the gauge's own `Current()`, whose resolution is 1 mA against a ~20 mA
awake baseline — roughly **200× coarser** than the effect — and reading it is the thing
being measured. So this needs a **bench current meter in series with the pack, poll at
2 s against the poll stubbed out**, and nothing short of that can produce a figure;
**do not quote one until somebody has.** What settles the decision without it is the
comparison this file already made once: the input task calls `input->update()` every
`kPollMs` (10 ms) and each call is **two `analogRead`s**, so an awake device already
takes **400 ADC conversions per 2-second battery interval against 3 I2C register
reads** — and `kPollMs` is the constant this project **refused to halve** on a
duty-cycle argument. The poll #96 is about is two orders of magnitude below the
sampler that argument was made about, which is why the case for this change is the
*long reading session*, not the idle device.

**THE `gLastBatteryPollMs` DISCIPLINE SURVIVED IT, AND HAD TO.** The stamp stays inside
`homeOnGlass()`, because that stamp and a reading are the **same event** —
`refreshBatteryOnHome()` feeds `gBattery.update()` two lines above it — so a Home paint
really has taken the reading it claims. On an X4, where `bandRepaintPossible()` is
false for ever and Home therefore polls **slowly**, presses on Home feed the ladder at
exactly the rate they arrive. **It was the unconditional stamp that starved the
mechanism, never the interval.**

**AND `polls=` ON `[alive]` CANNOT STAND ALONE ANY MORE, so `pollMs=` is beside it.**
With two intervals 15× apart, a device wrongly pinned to the fast one and a device
correctly on the slow one differ in that count and in **nothing else that reaches a
log** — the same "an instrument that reports on less than it claims" shape this file
records for the listing cache and the ring warm. It is read off `pollIntervalMs()` with
the predicate the loop uses, never re-derived from `level=`, which would be a second
spelling of the choice free to disagree with the one actually made.

**AND WIRING THAT FOUND A REAL DEFECT IN `renderTop()`, WHICH IS EXACTLY THE CLASS THIS
FILE EXISTS TO RECORD.** The stamp `gLastBatteryPollMs = millis()` was unconditional —
harmless while the timer's only consumer was itself gated on Home, since a Reader paint
reset a cadence nothing outside Home was waiting on. With an ungated poll it means every
paint pushes the next reading out by another poll interval, so **a reader turning
pages faster than 2 s starves the safety mechanism on the one screen the banner is drawn
on**. The stamp is inside `homeOnGlass()` now: a paint that takes no reading must not
claim one, which is what the comment above it always said. (The `kBatteryPollMs` this
paragraph used to name **no longer exists** — #96 replaced it with
`BatteryTracker::pollIntervalMs()`, and the 2 s the defect was measured against is
`kPollFastMs`. The rule is unchanged and the paragraph above says how it survived.)

**THE BANNER DRAWS OVER THE PAGE AND NEVER INTO THE COLUMN, AND THAT IS THE WHOLE
DESIGN.** `readerMetrics` derives `columnH` and `PageBuilder` seats
`rowsThatFit(columnH, lineBox)` lines in it, so honouring the board's 78px band *inside*
that column takes a default page from **twelve lines to ten** and re-paginates the whole
chapter — a `relayout` at the exact moment the device has least energy to spend, moving
the reader's page under them. So `renderReader` draws it at
`footerTop - kReadFooterPadTop - kBannerH` and `columnH` is untouched.
`ReaderViewModel::anchorLabel` already carried this reasoning for the footer's third
field, and it is the same rule one band lower. **Verified per row rather than by
eyeballing the render**: exactly **78 contiguous rows** differ from the plain `reader`
render at both geometries and every other row is byte-identical.

**ITS LABEL IS `Role::Meta700` AT `--t-meta`, NOT `Label500` AT `--t-label`, AND THE
RAMP IS HALF THE REASON.** The ramp carries `Label400`/`Label500` at 11pt and
`Meta400/500/700` at 10pt (`font_manifest.h`), so **there is no 23px/700 role** and the
design's first spelling asked for a pre-rendered asset nobody has — unbuildable rather
than merely terse. The X4's fit chose between the two roles that do exist: measured in
Chrome on the board at the widest string the firmware can produce, `Label500` at 23px
leaves a gap of **0.00px** and **wraps both runs to two lines** inside a 78px band,
against **27.11px** at `Meta700` (X3: 42.59 against 75.11). After the firmware's ~3%
wider `.rfnt` advances that X4 gap is **~15.9px**, which is the figure that has to stay
positive on glass. The band's `padding: 0 18px` is the page's own, so its runs align
with the header's book title and the footer's percentage; the 24px it replaced was
orphaned from a pre-rebase board with 40px margins.

**`ANY BUTTON` IS A BINDING, NOT A CAPTION.** `ReaderScreen::setBatteryLow(int)` arms
the latch — **one argument, `-1` to disarm, the same sentinel
`ReaderViewModel::batteryLowPercent` carries**, because a `(bool, int)` pair would spell
the condition twice — and the dismissal at the top of `onGesture` clears it and returns
`Action::redraw()` **whatever the gesture**. The latch is `ReaderScreen`'s rather than
the shell's so a test can reach it; `shell/` is where the bugs hide. The first press is
spent dismissing and does nothing else, which is what the bar promises and matters most
for `Back`, since `Back` on the Reader otherwise pops out of the book. Power is not
swallowed — the shell handles it before dispatch — and the banner goes with the RAM,
which is correct. It re-arms on a **fresh entry** into `Low` rather than per poll, so a
wake shows it again and, when the Reader is what the wake restores, rides that paint for
no extra waveform. A banner armed under a peek or the reader menu simply waits: the
Reader is not on top, so nothing draws it until the overlay pops.

**`ScreenId::BatteryEmpty` IS PAINTED DIRECTLY AND NEVER PUSHED**, on `Sleep`'s
argument — the session record names the top of the stack, so pushing it would make the
next wake restore *into* it. `paintBatteryEmptyScreen()` therefore owns the two things
`App` normally does, the **clear** and `gFrameContentsUnknown`. It is `Fidelity::Mono`,
takes no input and draws no hint bar: the shell paints it and calls deep sleep, so there
is nobody left to press anything. `drawBadge` moved into `components.h` because this
screen's badge is byte-identical to `Sleep`'s — the second copy, and `renderSleep`
migrated to it in the same change. Two new marks,
`kWarning` (32×28, white, for the inverted band) and `kBatteryLarge` (98×52), generated
by `iconc.py` from their own boards and disambiguated by `source` exactly as
`kBook`/`kBookLarge` are.

**ITS BADGE SAID `CHARGE TO WAKE` AND BOTH HALVES OF THAT WERE WRONG — FOUND ON GLASS
AND BY NOTHING ELSE.** Reported from an X3: plugging the device in does not wake it, you
have to hold power, and the screen did not say so. It is `CHARGE · HOLD POWER TO WAKE`
now, and the two defects are worth keeping separate because only one of them is a copy
problem.

- **CHARGING CANNOT WAKE THIS HARDWARE, so the old badge promised something no code
  could deliver.** There is no charge-detect wake source: `usbDetect` appears **only** as
  a field declaration in `freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h`
  and **nothing in the SDK reads it** — the same dead field the charge-latch poll exists
  because of — and on the X3 the pin the Xteink profile names for it is the fuel gauge's
  own I2C SDA. **A timer wake cannot substitute either**, and that is the half that
  surprises: on battery the sleep leaves the chip **fully powered down**, which is
  exactly why a resume there reports `ESP_RST_POWERON` rather than `ESP_RST_DEEPSLEEP`,
  so there is nothing left running to fire one. **This is the class of claim this file
  exists to record and the class the battery work already refuses elsewhere** — an unread
  gauge answers `-1` and never `0%`, a book with no reading position gets no demo
  substitute. **A false claim is worse than an absent one**, and a badge is a claim.
- **IT OMITTED THE HOLD, AND THE HOLD GATE RUNS FIRST.** `setup()` calls
  `requireHeldPowerButtonOrSleepAgain` **before** `requireChargeOrSleepAgain`, so a wake
  off this screen needs `kWakeHoldMs` (600 ms) of held power **and** a pack at
  `kResumePercent` — and a tap is refused for the *hold* reason with the gauge never
  read. So the screen named the second gate and not the first, in a state where the first
  is the one the reader keeps failing.
- **THE FIT NEEDED NO NEW MEASUREMENT, WHICH IS WHY THIS STRING AND NOT A SHORTER ONE.**
  It is character-for-character as long as `Sleep`'s `ASLEEP · HOLD POWER TO WAKE`
  (27 each, both `--t-meta` at 0.2em), and that badge **already ships on the X4** — the
  narrower panel. Measured after the change: the label is **427px** and `drawBadge`'s
  box **465px** on the 480px glass, leaving 8px and 7px of margin — against `Sleep`'s
  422/**460**, the 5px being `CHARGE` measuring wider than `ASLEEP` at whole-pixel
  advances. **The 1px margin asymmetry is `centreIn` halving an odd 15px leftover and
  is not a centring defect.**
- **`drawBadge` SIZES TO ITS LABEL, so both `battery_empty` goldens moved and were
  re-blessed** — the badge went **267px wide to 465px**, still centred, still 34px off
  the bottom, still one line. Design-vs-firmware went 1.81%/1.66% to **2.08%/1.91%**
  with both copy changes in, and **the increase is accounted for row by row**: the
  badge's own 45 rows go 563 → **1344** differing pixels, and everything outside them
  goes 6374 → **6646**, the +272 being the one reworded paragraph line and nothing else.
  The badge BOX agrees with Chrome's to 1px at both geometries (465 against 466, same
  top, bottom and right edge) — a longer tracked run of small caps is simply where
  Chrome's subpixel advances and the firmware's whole-pixel ones disagree most per pixel
  of ink. `sd_missing` measured 1.83%/1.67% as a control in the same tree, **unchanged
  to the pixel**, which is what makes the before and after the same instrument.
- **The middle dot is its own string literal** (`std::string("CHARGE ") + kMiddot + " HOLD
  POWER TO WAKE"`), for the reason this file records twice: a C++ hex escape is UNBOUNDED,
  clang rejects `"\xB7H"` and the ESP32's GCC **accepts** it and emits a byte that is not
  U+00B7. That is a sixth local spelling of the two-byte string and deliberately so —
  `screens.cpp` records it as a punctuation choice each board makes rather than a
  constant, and **its `kDot` is unreachable anyway**: `const char* const` in an
  anonymous namespace in a .cpp, and not even the same string (`" · "` with its spaces
  baked in, against `screen_book_end.cpp`'s bare two bytes).

**AND ITS PROSE NAMED A CONNECTOR THE X3 DOES NOT HAVE — THE SECOND FALSE CLAIM ON THE
SAME SCREEN, ALSO FOUND ON GLASS AND BY NOTHING ELSE.** It read *"charge over USB-C to
continue"* and was reported from an X3, **which has no USB-C port**. It is
*"connect a charger to continue"* now, and **the connector is deliberately unnamed
rather than corrected**:

- **ONE BINARY DRIVES BOTH MODELS AND THEY DO NOT SHARE A CONNECTOR**, so naming either
  one is false on the other — swapping `USB-C` for the X3's socket would have moved the
  defect to the X4 rather than fixed it.
- **AND IT COULD NOT BE MADE CONDITIONAL EITHER**, which is the fact worth keeping:
  **nothing in `BoardProfile` describes the socket.** It names the battery ADC, the
  gauge address and the dead `usbDetect` pin, and there is no field anywhere from which
  a screen could ask which port is fitted. So the copy cannot name one *correctly* under
  any amount of work.
- **THE WRAP WAS VERIFIED IN BOTH ENGINES RATHER THAN ASSUMED**, because
  `SdMissing.dc.html` needed `max-width` 400→420 for exactly this — the `.rfnt` faces
  measure ~3% wider than Chrome's. `connect a charger` is 17 characters as
  `charge over USB-C` was, and both engines wrap the paragraph to **four lines at the
  same three break positions** (after *The*, after *down*, after *to*) at both
  geometries, before and after. `max-width` did not move. **The dash that used to sit
  at that second break is gone** — see **The rule that governs COPY** — and the breaks
  did not move with it.

**THE RESUME GATE IS BEFORE `display.begin()`, AND THAT IS THE WHOLE COST OF THE
FEATURE.** `requireChargeOrSleepAgain()` sits in `setup()` immediately after
`requireHeldPowerButtonOrSleepAgain` — the cheaper refusal first, so a bag-brush is
refused for the *hold* reason without spending an I2C transaction — and still before the
panel bring-up. E-ink holds its last image, so the glass is already showing the
`BATTERY EMPTY` screen the shutdown painted: **a refusal repaints nothing and spends no
waveform.** One line later and every brush against a flat device's power button costs a
flash. It is after `detectAndSelectBoard()` because it needs the profile, and safely so
for the reason `BatteryMonitor`'s constructor is safe above: `readStatus()` tests
`BoardConfig::ACTIVE.batteryGauge.gaugeAddr` **live**.

- **THE `critShut` NVS FLAG IS WHAT MAKES A STRICT `≥15%` LEGAL.** Without it the gate
  would have to sit at the critical threshold itself, which flaps — and a `15%` gate
  applied to *every* boot would refuse a device sitting at a perfectly usable 10%. It
  fires only for a device that shut itself down.
- **Read-and-cleared, and given back on a refusal**, exactly as `slept` is: one flag
  buys one resume, a boot that sets out to refuse and then panics must not refuse for
  ever, and a refused wake spent neither flag. Dropping the `slept` half would make the
  **next** wake — the real one, once charged — read as a cold start, and the reader would
  lose their page to something that looks like the restore failing.
- **A reading that did not answer lets the device boot.** Fail open here, fail safe in
  the loop: a failed gauge would otherwise refuse every wake for ever, and the ladder
  shuts the device down again ten seconds later if the pack really is flat.
- `[boot] battery pct=N critShut=1 -> RESUME|refused` prints the whole decision, in
  `[boot] reset reason=… slept-flag=…`'s idiom. Read that line before believing anything
  about a device that will not turn on.

**`criticalShutdown()` IS `sleepNow()`'s SHAPE AND IS `[[noreturn]]`**, reached from
`loop()` before the idle-sleep check: `saveReadingPosition("battery")` → paint →
`display.deepSleep()` → `powerDownRailsForSleep()` → `markSleeping()` **and**
`markCriticalShutdown()` → flush → `deepSleepUntilPowerButton()`. `markSleeping()` too,
deliberately: once charged, the wake should restore the reader's page rather than
starting cold, which is the other half of the board's *"Your page is saved"* — the save
makes the promise and this flag redeems it. **A cold boot on a flat pack spends one Home
paint before the dwell fires**, because a device that appears to do nothing when you
press power is indistinguishable from a brick.

**`ENCRE_BATTERY_FAKE_PERCENT=n` IS HOW THE LADDER IS WALKED ON GLASS**, in
`ENCRE_FS_SELFTEST`'s shape and absent by default (+108 bytes when present). Draining a
real pack to 3% on demand is not practical, and without it none of the five things only
the panel can answer is reachable. **It overrides the percent and nothing else**, so an
X3 on the cable still suppresses `Critical` — which is what makes the on-cable check a
real test rather than a tautology. `docs/on-device-smoke-checklist.md` §10 is the list.

**CONFIRMED ON GLASS (2026-09-07), WHICH IS THE ONLY PLACE MOST OF IT COULD BE.** The
whole ladder was walked on an X3 with `ENCRE_BATTERY_FAKE_PERCENT`: the banner over a
page, the shutdown's paint completing before the rails go down, the resume gate refusing
three times in a row and repainting nothing, the restore of the reader's page once
charged, and `charging` suppressing `Critical` on the cable. **1,319 green test cases
said nothing about any of it** — `shell/` has no harness, so not one line of the two
gates or the shutdown's ordering is executed by the desktop suite.

**AND THE GLASS FOUND TWO FALSE CLAIMS NOTHING ELSE COULD**, both in this screen's copy
and both the defect class this file already refuses for an unread gauge (`-1`, not `0%`):

- **`CHARGE TO WAKE` promised what the hardware cannot do.** There is no charge-detect
  wake source — `usbDetect` is a field declaration nothing in the SDK reads, and on the
  X3 it names the fuel gauge's own SDA — and no timer wake can substitute, because on
  battery the sleep leaves the chip **fully powered down** (which is why a resume reports
  `POWERON`). It also omitted the hold, which `requireHeldPowerButtonOrSleepAgain`
  requires and which runs FIRST, so a tap refuses for the *other* reason and never
  reaches the charge gate at all. The badge is `CHARGE · HOLD POWER TO WAKE` now, 27
  characters — the width is proven by `Sleep.dc.html`'s shipping badge of exactly that
  length rather than by a fresh measurement.
- **The prose named USB-C, which the X3 does not have.** One binary serves both models
  and nothing in the board profile knows the connector, so the copy may not name one:
  *"connect a charger to continue"*. Both engines still break the paragraph at the same
  four places, so `max-width` did not move.

**Four live comments still asserted the old promise**, one of them verbatim —
`battery_tracker.h` said *"the glass says CHARGE TO WAKE, and it does"*, inside the
header that owns the thresholds. A rule restated at several sites is one that drifts, so
all four were corrected rather than just the string.

**MEASURED AGAINST THE BOARDS:** `low_battery` 5.03% (X4) / 6.02% (X3) against the
untouched `reader`'s 5.34%/6.38% — compare it against the other **grayscale** screens,
for the reason recorded under the peek. `battery_empty` is **2.05%/1.89%** against
`sd_missing`'s 1.83%/1.67%, measured as a control in the same tree with the same
instrument. It read **2.08%/1.91%** until the connecting dash came out of its
paragraph (see **The rule that governs COPY**), and the control reproduced its own
recorded pair to the digit across that change, which is what says the two readings
are one instrument rather than two. It read 1.81%/1.66% before the badge's copy changed, and the increase is
accounted for row by row: **the badge's box agrees with Chrome's to 1px**, so what moved
is a wider tracked-caps run rasterising differently, not geometry drifting. `make
compare` reports 30/36 implemented and both ids `firmware ok`.

**ONE STATED LIMIT, PRE-EXISTING RATHER THAN INTRODUCED:** `criticalShutdown()` flushes
the card log **after** `powerDownRailsForSleep()` has cut the X3's SD rail. That is
`sleepNow()`'s ordering verbatim and the flag ordering it inherits is right — the flag
is what the next boot needs and the log is only what a human needs — but it means
`[log] battery empty` may never reach `/encre.log` on an X3, so the serial capture is the
authority for this path.

## Covers

The sleep screen can hold the open book's cover (#11). `SleepCover.dc.html` is the
cover alone, `SleepCoverDetails.dc.html` is the cover with the reading card and the
badge over it, and `Sleep.dc.html` is the card on paper that shipped and is still the
default. Settings' `SLEEP SCREEN` section picks between them — `Shows`
(COVER / COVER + DETAILS / DETAILS) and `Cover fit` (FILL / WHOLE).

**IT IS ON GLASS, AND THE QUESTION IT TURNED ON IS ANSWERED: A FOUR-LEVEL COVER READS
AS A PHOTOGRAPH, NOT AS NOISE.** Confirmed on the X3, 2026-08-29, with no ghosting of
the card's text under the picture. That was the decision deferred to the panel when
`Grayscale` was chosen over 1-bit Floyd–Steinberg, and it went the way the design
assumed — which is worth recording precisely because this project has been wrong about
this panel from desktop evidence three times.

**What the glass also corrected, and it is the sharper half:** the heap. A deflated
JPEG peaks at **81,088 bytes on the device against the desktop's 63,560** for the same
work — every case measured 17–25 KB above its desktop figure, because the allocator is
simply different. That is the ratio trap in a third disguise, after time-on-the-card and
`__divdi3`. See **Sleep releases the whole `App`** below for what it cost.

**Two things remain untested on glass** and are honest gaps rather than oversights: the
**one-bit cover the WAKE paints** (the Msb plane is a threshold *through* an already
dithered picture, and hard thresholding a photograph is exactly what this file warns
about), and whether the badge slicing a cover's own title band is tolerable.

### A cover is universal, and it can never be held

225 real EPUBs from `~/.cache/encre-corpus`, parsed for the cover their OPF declares
(`<meta name="cover">` first, `properties="cover-image"` second):

| | |
|---|---|
| books declaring a cover | **225 / 225** |
| median cover | **1400 × 2100 = 2.94 MP** |
| largest | 3133 × 5000 = 15.66 MP |
| median compressed bytes | 246 KB (max 1.36 MB) |

2.94 MP decoded to 8-bit grey is **2.9 MB**, against 133 KB free with no book open,
76,476 B reading through Home's CONTINUE, and **42,152 B reading through the
Library** — on a part with **no PSRAM**. The decoded image is **22× larger than
everything the device has**, so this was never a tuning problem and no amount of care
with a one-shot decoder reaches it.

**So every layer streams and downscales, exactly as the reader's six do.** JPEG one
MCU row at a time (`jpegd.h`, over vendored TJpgDec); PNG one scanline at a time over
our own `inflate_stream.h` (`pngd.h`); box-filtered straight down into a panel-sized
destination (`imagefit.h`); `cover.h` drives the chain. **The output is PLANES, not an
image** — the picture never exists anywhere in one piece.

**TJpgDec is vendored and the PNG decoder is ours, and the asymmetry is deliberate.**
JPEG's edge cases are numerous and a wrong upsample is a *subtly wrong picture* rather
than a failure, which is the worst kind of bug to own; PNG is DEFLATE plus per-row
unfiltering and **we already own the hard half**, so vendoring a second decoder to get
~200 lines of unfiltering would buy nothing.

### Two planes serve three passes

`Plane::Bw` inks where coverage ≥ 2, which is **exactly "MSB set"** — so the grayscale
base pass and the `Msb` pass ask for the *same* plane. That identity is what makes
four levels affordable at the 42,152-byte reading floor: the cache is two planes'
worth of bytes rather than three, and **each pass is one blit straight onto the frame,
so painting a cover costs no extra RAM at all**. Holding a 2 bpp image of a 480×800
panel would be 96 KB, which does not exist.

`CoverSource` (`screen_sleep.h`) is an interface rather than a buffer for that reason,
the same shape as `SettingsSink` and for the same reason — `core/` never learns what a
filesystem is. An implementation that answered `Bw` with anything other than its `Msb`
plane would give a base pass that disagrees with the refinement painted over it.

**A `false` FROM `loadPlane` DOES NOT PROMISE THE FRAME IS UNTOUCHED**, and pretending
otherwise would be a contract no streaming implementation can keep — it finds out the
card is gone half way down the picture. What makes that safe is the *caller*:
`renderSleep` clears and draws the dither field whenever the load refuses, so a
partial write is overwritten rather than shown.

### The cache holds LOGICAL rows, and a `memcpy` would pass the whole desktop suite

**IT IS NOT A `memcpy` INTO `data()`, AND THAT IS THE HALF THAT IS ONLY WRONG ON THE
DEVICE.** The cache holds **logical raster rows**, because a streaming row-major
downscale can emit nothing else — `imagefit.h` produces destination row *n* and then
destination row *n+1*, and it has no picture left to transpose. The shell binds
`Rotation::Ccw`, under which **one logical row is a physical COLUMN**. So the blit
goes row by row through `Framebuffer::writePackedRow`, which is **the one function in
this feature that knows `Rotation` exists**.

**Under `Rotation::None` that reduces to the `memcpy` and the difference is
invisible** — and `Rotation::None` is the entire simulator and every golden. So the
wrong version passes `make test`, passes every golden at both geometries, passes
`make compare`, and smears diagonally on glass. **This is the fifth time this project
has met that hazard**: `veilRect`, `Framebuffer::fillRect`, the glyph blit in
`text.cpp` and `ditherRect` each took the same structure for the same reason and each
carries the same warning. Nothing on the desktop stands between that mistake and the
panel except a test written to run under both rotations.

### A software 64-bit division, found by reading the assembly

The obvious spelling of `CoverFitter::addRow`'s column map (`imagefit.cpp`) is
`(int)((long long)j * dstW / srcW)`, one per source pixel. On x86-64 that is a
hardware `idiv` and **benchmarks at 3.19 ms a cover against 3.19 for the form that
shipped — no difference at all**. **RV32IMC HAS NO 64-BIT DIVIDER.** Compiled with the
project's own toolchain (`riscv32-esp-elf-g++ -Os`) that line emitted `mulh`/`mul` and
a **`call __divdi3` inside the per-pixel loop body** — a libgcc shift-subtract routine,
~100–200 cycles, run once for every source pixel. A median cover cropped to the X4 is
~2.65 M source pixels: **1.7–3.3 s at 160 MHz, in one line**.

It is stepped instead, carrying the remainder, and **bit-identical rather than
approximate**: `dstW <= srcW` means the quotient advances by 0 or 1 per pixel, so the
carry reproduces the floor exactly. Verified across **113,388 assertions — not one
output bit moved**, which is the standard a "faster and equivalent" claim has to meet
here. The *row* map keeps its divide, because it runs once per source ROW; a CFG cycle
analysis put **0 of the surviving `__divdi3` calls in a loop body**, which is the check
worth repeating rather than the count.

**THE HABIT IS THE POINT, NOT THE INCIDENT.** This is the desktop-to-device ratio trap
in its sharpest form yet: the ratio here is not 37× or 135×, **it is infinite, because
the desktop cost is zero**. A desktop benchmark cannot see an instruction the target
does not have. **When a hot loop is about to run millions of times on the device,
cross-compile it and read the assembly** — `riscv32-esp-elf-g++ -Os -S`, then grep for
`call`. A `call` in a leaf arithmetic loop is a compiler-emitted software routine and
is always worth a look; `__divdi3`, `__udivdi3`, `__moddi3` and the soft-float family
are what to expect on a part with no FPU and a 32-bit divider.

### A small cover is enlarged — to a measured ×2, then to an overridden ×2.5 (#64)

**`fitCover` USED TO NEVER UPSCALE, AND `imagefit.h` STATED THAT AS A PROPERTY RATHER
THAN A TASTE.** Every word of the argument was true — a box filter's support is the
destination pixel's footprint, which when enlarging is *smaller* than a source pixel, so
area-averaging an enlargement is nearest-neighbour however it is spelled; and
one-source-row-to-one-destination-row streaming cannot complete two rows from one push.
**What was written down is what the reader saw as a defect**: a 260×346 cover sat on the
X3's 528×792 as a small picture covering **22% of the glass**, for hours, and
`design/SleepCover.dc.html` says full-bleed. That state matched no board at all — it drew
neither the picture nor the reading card.

**THE CAP WAS MEASURED TWICE AT ×2, AND THE SHIPPED CAP IS ×2.5 — AN OWNER OVERRIDE OF
THAT DERIVATION AND NOT A CORRECTION OF IT.** Both halves have to be read together, and
they are kept apart on purpose: the measurements below bound **200**, they are unchanged
and nothing has falsified either, and `kMaxCoverUpscalePercent` is **250**. Restating the
derivation under the larger number — letting the argument for 200 read as though it had
produced 250 — is the comment-drifted-from-code defect this file records over and over,
and the figures are kept whole so the constant can be moved **back** with evidence.
Both measurements are against the smallest structure this glass carries. Nearest-neighbour
replication at scale *k* introduces structure of period *k* pixels, so the question is
where that stops being absorbed:

- **THE PIPELINE'S OWN GRAIN, off the shipped `CoverFitter`.** A flat field at each of the
  three level midpoints — grey 42/43, 127/128, 212/213, the tones four levels carry worst
  and therefore the patterns with the most contrast — comes out of `emitRow` as a run
  length of **exactly one**, which is a period-**two** alternation. Over all 234 greys that
  need a pattern at all the mean run is **3.20 px**, so 2 px is the floor of that
  distribution and its highest-contrast end. **That picture is confirmed on the X3 to read
  as a photograph** (2026-08-29, at the head of this section), so 2 px is structure this
  panel is *known* to accept.
- **THE PROJECT'S OWN LEGIBILITY FLOOR, already in the repo.** "Below ~10pt is not legible
  on this glass, measured"; 10 pt at 150 DPI is a 21 px ppem whose stem is ~2 px, and the
  whole `Mono` argument is about "a 2px stem fully inked". 2 px is the smallest structure
  this project has measured as carrying meaning here.

Two independent measurements landing on the same number is what makes **200** a derivation
rather than a pick. At *k* ≤ 2 the introduced structure is no coarser than the dither grain
beside it and is absorbed into the diffusion; **at *k* = 2.5 it is not** — a source pixel
becomes a run of **2 or 3** destination pixels, mean 2.5 — so past 200 the sufficiency of
nearest-neighbour is **assumed rather than measured**. That is the stated cost of the
override. Anything smoother needs a reconstruction filter wider than the destination
pixel — real interpolation, a new hot loop, ~520 bytes of held source rows — and that is
the named next step if the glass says the replication reads blocky, which is now a
sharper question than it was at ×2.

**WHY IT WAS RAISED, WHICH IS A DECISION AND NOT A FINDING.** The reporting book —
`Walden ou la vie dans les bois`, 260×346 — asks **×2.29** on the X3 and **×2.31** on the
X4, so 200 refused it and the sleep screen showed the reading card. Both states are
boarded, and the owner's call is that a **soft full-bleed cover beats a card**:
`SleepCover.dc.html` draws a picture, and the card is what the screen falls back to when
there is *none*. The refusal was not a wrong answer, it was the *derived* answer, and it
was overruled on a judgement no measurement in this repo can make. All four of that
shape's combinations are now served, verified through the real `decodeCover` at both
panels: `Ok`, `dst=528×792+0+0` and `480×800+0+0` at `FILL`, `528×703+0+44` and
`480×639+0+80` at `WHOLE`, against the pre-#64 binary's `231×346+148+223`.

**WHAT THE CORPUS SAYS AND WHERE IT IS THE WRONG INSTRUMENT.** Run through the real
`decodeCover` at both panels: 223 of 225 covers have dimensions that parse, and the worst
enlargement any of them asks for is **×1.32** (400×662 on the X3). So **the cap admits
every corpus cover** — `tools/covers.py` reports 223 `Ok` and **0 `TooSmall`** at both
geometries — and the change fills the panel for the **3 (X4) / 4 (X3)** covers that used
to sit centred.

**AND THAT IS WHY THE RAISE TO ×2.5 MOVES NO CORPUS COVER AT ALL.** 200 already admitted
every one of them, so the corpus reports the identical 223 `Ok` / 0 `TooSmall` at both
caps and **all 892 cover renders are byte-identical across the change** — measured, not
argued, by keeping both runs' PNGs. Against the pre-#64 binary the count is unchanged at
either cap: **10 of 892 renders differ** (7 `FILL`, 3 `WHOLE`, four distinct books), which
is exactly the 3/4-per-panel set #64 itself moved. So the corpus has nothing to say for or
against the override, **which is the point rather than a gap**: the case it is for is the
one the corpus does not contain. 260×346 is far smaller than anything in it, the corpus
under-counts this the way it under-counted #35, and **both of the books #35 made openable
are small-cover cases**, so that fix raised this one's incidence.

**A REFUSAL IS `CoverResult::TooSmall`, WHICH IS A SIXTH VALUE AND NOT THE NEAREST
EXISTING ONE.** `Unsupported` is "not an image we read" and this is an image we read
perfectly; `OutOfMemory` is the false `CoverFitter::begin` used to answer, and nothing is
wrong with the memory. Both would be a log line asserting something untrue about a book —
the shape this file already refuses for an unread gauge (`-1`, never `0%`) and for a badge
promising a wake charging cannot deliver. `CoverReport` carries the **1:1 box the cover
would have occupied**, because the reason is a fixed sentence and the geometry is the half
that says by how much it missed.

**NO BOARD CHANGED, AND THAT IS THE ARGUMENT FOR THIS SHAPE.** Both outcomes are already
boarded — full-bleed (`SleepCover.dc.html`) or the reading card (`Sleep.dc.html`) — so the
firmware moved *toward* a board it had been failing rather than a board moving toward it.
The small centred picture was the only unboarded state and it is gone.

**THE INTERFACE HAD TO WIDEN, AND IT WIDENED HONESTLY.** `addRow(src, bool& emitted)` is
now `addRow(src)` plus `nextRow()`, drained in a loop. A bool can say "zero or one"; an
enlargement completes several rows from one push, and a contract promising "at most two"
would have been true only while the cap happened to be 200% — **which it stopped being one
ticket later**, so the honest shape earned itself faster than expected. The one misuse it
introduces —
pushing with rows still pending, which would blend two source rows into one accumulator —
is **refused rather than silent**, and no downscale can reach it, which is why every
shipped caller changed by exactly one `if` becoming a `while`.

**THE ACCUMULATE PATH IS A SECOND PATH AND NOT A SECOND COPY.** A downscale is a forward
**scatter** (walk the source, add each pixel to the cell it lands in) and an enlargement is
an inverse **gather** (walk the destination, read the pixel it sits on): different
operations over one accumulator, with the mean, the diffusion, the packing and every guard
shared. The forward form is kept **verbatim** rather than generalised, because a gather
with the same boundaries rounds its cell edges the other way and would move a byte of
every cover the device has ever drawn.

**AND THE ROW MAP HAD TO CHANGE WITH IT — THE DEFECT THIS NEARLY SHIPPED.** The columns
gather with `floor(c·srcW/dstW)`, so the rows must say the same thing: source row *i* owes
the destination rows *r* with `floor(r·srcH/dstH) == i`, which is `r < CEIL((i+1)·dstH/srcH)`.
Reusing the downscale's **floor** there handed destination row 1 of a 33-to-64 enlargement
to source row 1 while its *columns* were reading source row 0 — **a picture sheared by one
source pixel down its whole height**, still a picture, so nothing but a reference
comparison could see it. It was caught by the reference disagreeing, which is what that
file is for.

**A REPLICATED ROW IS NOT A DUPLICATED ROW**, and this is worth knowing before predicting
what an enlargement looks like. The accumulator is held across the rows one source row
completes, but `err_` advances **per emitted row** — so the copies are the same tone in
*different* dither patterns. Asserted as **zero** identical adjacent destination rows over
a flat midtone at ×1.94 (33 source rows into 64), where 31 of those rows are second
copies. Clearing the accumulator on every emit instead — the obvious spelling — makes the
second copy **paper**, and fails that case with the tone as well as the pattern.

**WHAT IT COSTS: NOTHING NEW IN MEMORY, AND IT IS THE CHEAP DIRECTION IN TIME.** `acc_`,
`count_` and `err_` are sized by `dstW`, which for `FILL` is the panel width — the same
bound a full-panel downscale already pays, so at most **+1,280 bytes** against what a
small cover used to take and **nothing** against the worst case that already ships. The
device's 81,088-byte deflated-JPEG peak is untouched. The gather runs `dstW` times per
source row, so **349,536 iterations** for 400×662 → 528×792 against the **2.65 M** a
median cover's downscale walks. Desktop, three runs each: that cover's decode goes
**4.4 → 5.9 ms** against a median cover's 21 ms.

**WHAT ONLY THE PANEL CAN ANSWER, AND THE OVERRIDE MADE IT THE WHOLE TICKET.** The two
measurements bound the *introduced structure* at the grain the glass has accepted; they do
not say a ×2 enlargement of a photograph reads well, and at ×2.5 they do not reach it at
all. **This panel has corrected desktop reasoning three times**, and nothing on the desktop
can arbitrate here by construction — the simulator and the goldens run this same
arithmetic, so they agree with it whatever it says. So:

1. ~~Whether ×2.29 replication of a 260 px cover across 528 px reads as a photograph or as
   BLOCKS.~~ **ANSWERED ON GLASS (2026-09-07): CONFIRMED on an X3**, on the real book the
   raise was made for — `Walden ou la vie dans les bois`, 260×346, a **deflated PNG**, the
   one corpus format this file says *may* refuse on heap. The verdict was that it *"looks
   good enough"*, and **that wording is the finding rather than a rough note**: it is an
   ACCEPTANCE, not a measurement, so it does not extend the two measurements to ×2.5 —
   they still bound 200. `kMaxCoverUpscalePercent` remains a **one-constant** change in
   either direction and **200 is still the number the measurements support**, so the way
   back stays open and cheap.
2. Whether the `FILL` crop of an *enlarged* cover cuts type the reader wanted — the crop is
   unchanged arithmetic, but it now bites on covers that used to be shown whole, and the
   raise widened the set it bites on.

**Question (2) of the original three is closed, by decision and not by evidence**: it asked
whether refusing at ×2.29 beats a soft full-bleed picture, and the owner answered *no*,
which is what this constant now records. `[cover] TooSmall … dst=…` is still the line that
makes a refusal readable off a device — there is simply less that reaches it.

### Sleep releases the whole `App`, not just the chapter

`ReaderScreen::releaseChapter()` already existed, built for the peek, and it frees the
right **36,956 bytes** — and it is **not enough**. Measured on the X3, a deflated JPEG
peaks at **81,088 bytes**, which is **17.5 KB ABOVE the desktop's 63,560 for the same
work**. Releasing only the chapter leaves:

| the book was opened via | free at sleep | margin over an 81 KB peak |
|---|--:|--:|
| Home → CONTINUE | ~121 KB | ~40 KB |
| **the Library, 203 books** | **~87 KB** | **~6 KB** |

**Six kilobytes, on the commonest way to open a book** — the Library's 203 entries sit
resident under the Reader at ~59 KB. **And the failure would have been SILENT**:
`decodeCover` answers `OutOfMemory`, the screen falls back to the reading card, nothing
looks broken, and it reads as *"covers don't work for some books"* rather than as a
defect anybody reports. Releasing the `App` returns that ~59 KB and takes the margin to
**~65 KB**.

**NOTHING NEEDS THE `App` AFTER THE FIRST SLEEP PAINT**, and each half was checked
rather than assumed: `saveWhereWeAre` wrote the session record at *navigation* time and
not here; `saveReadingPosition` ran while the stack was still standing;
`paintSleepScreen` bypasses `App` by design, because pushing `SleepScreen` would make
the next wake restore *into* it; and the next statement is a chip reset. So **sleep is
the only moment in this firmware where freeing everything is free** — a sentence the
spec carried from the start and the plan under-implemented.

**AND THE HEAP IS WHAT THE GATE ACTUALLY CAUGHT.** Every measured case peaked **17–25 KB
above its desktop figure**, and the cause is neither speed nor a missing instruction —
it is simply **a different allocator**. **A desktop heap figure is not a device heap
figure**, and this one under-predicted in the dangerous direction. That is the ratio
trap in a third disguise, after time-on-the-card and `__divdi3`.

**`inflater_` IS A VALUE MEMBER, AND THIS FILE ALREADY RECORDED THAT TRAP UNDER THE
PEEK.** An "obvious" release that drops the `BlockReader`, the `InflateSource` wrapper,
the buffer view and the file handle frees **none** of the 36,956 bytes, because the
window lives behind `Inflater`'s private `Scratch*` and is freed only by `~Inflater`.
`inflateWindowHeld()` is the observation point; `held()` and `bytesRead()` both go
false either way and can see nothing.

**One incidental correction the probe produced and this file has not absorbed:** the
device reported **173 KB free at boot**, where this file documents ~133 KB at four
older sites (the listing cache's ceiling argument, the CSS sheet cap, the Typography
re-init and the ToC's archive re-open) — and at the head of this section, which
inherited the same figure. All five are *arguments from headroom* and a larger number
only makes them safer, so none was rewritten on the strength of one reading — but
**the next person to size something against ~133 KB should re-measure rather than
inherit it**, and a second reading is enough to correct all five at once.

### The badge rule, and the rule it overrides

**ONE PREDICATE, ASKED ONCE, DRIVES BOTH THE CARD AND THE BADGE** —
`covered && vm.shows == SleepShows::Cover`. `SleepCover.dc.html` is
`SleepCoverDetails` with the card and the badge taken away, so they go together or not
at all. Two conditions spelled separately would drift, and **this project has shipped a
dead button twice from exactly that shape**.

**THE BADGE HALF OVERRIDES A RULE THIS FILE STATES OUTRIGHT** — the badge "is the
load-bearing half and it stays", because e-ink holds its last image and a screen left
on the glass gives no clue the device is asleep rather than frozen. It may go **here**
because a full-bleed book cover is **not a screen the device can otherwise be in**, so
it is unambiguous by itself.

**THAT REASON IS FALSE THE MOMENT NO COVER IS ON THE GLASS, WHICH IS WHY `covered` IS
IN THE EXPRESSION AND NOT JUST `vm.shows`.** Every fallback — no source, a source that
refused, a mode that never asked — puts the badge back. **The override does not
generalise and must not be copied**: it is licensed by one property of one screen, and
a future screen that wants to drop the badge has to earn the same property rather than
cite this precedent.

**AND THE BADGE COLLIDES IN `COVER + DETAILS`, WHICH IS AN OPEN ON-GLASS QUESTION.** It
sits at `bottom: 34px` and a Standard Ebooks cover carries its title band exactly
there, so the cover's author line is sliced by an opaque white box. Most covers set
type low, so this is the common case rather than an unlucky one — and **it is the
strongest argument the boards make for why `COVER` drops the badge at all**. **Do not
move the badge to fix it**: its position is `Sleep.dc.html`'s, and a badge that moved
by mode would be two spellings of one thing.

### `make compare` cannot compare a dithered photograph

**Chrome cannot Floyd–Steinberg.** A board showing a cover the browser dithered its own
way would measure the two rasterisers against each other and say nothing about the
firmware. So **the board's cover is generated by our own pipeline and committed** —
`design/assets/sleep-cover-480x800.png` and `sleep-cover-528x792.png` — and both the
board and the golden read that one file. The simulator *decodes* it and deliberately
does **not** re-fit it: running the source through `CoverFitter` again would dither a
second time and could only differ from the file the board shows. `sleep_cover`
therefore measures **0.00% / 0.00%**, which is the correct answer and not a suspicious
one: the two columns hold the same bytes. `sleep_cover_details` is **3.48% / 3.18%**,
against `sleep`'s 3.29% / 3.02% for the same card on paper.

**`render_board` HAD TO LEARN TO SERVE `design/assets`, AND THE DEMONSTRATION IS THE
POINT.** It wrote only `index.html` into a temp dir and served that, so a relative
`src="assets/..."` 404s. **Measured with the serving removed, `sleep_cover` goes from
0.00% to 82.97% mismatched while the sheet prints `ok` at every step** — the exact
drift this file records under the reader's menu: **the percentage is the check, the
word is not.** A symlink rather than a copy (263 KB × 60 renders would be ~16 MB of
temporary files), and **its absence is a hard error rather than a skip**, for the same
reason a named board missing from disk is.

**Inlining the PNG as a data URI was rejected**, and it is the obvious fix: it would
make the markup a **second copy** of a file whose entire purpose is that the board and
the firmware read the same bytes.

### What a cover costs on the device

Four corpus books spanning the cases, decoded at boot by the (now removed)
`ENCRE_COVER_PROBE`, panel 528×792:

| cover | zip | source | scale | decode | heap SPENT | `heapMin` |
|---|---|---|--:|--:|--:|--:|
| JPEG | deflated | 1400×2100 | 1/2 | **3,074 ms** | 81,088 | 91,660 |
| JPEG | stored | 1424×2048 | 1/2 | **2,987 ms** | 81,560 | 91,660 |
| PNG | stored | 1600×2400 | 1/1 | **6,919 ms** | 81,796 | 91,660 |
| PNG | deflated | 601×918 | 1/1 | **6,648 ms** | 120,248 | 52,744 |

**THE SPENT COLUMN IS `heapBefore − heapMin`, NOT `heapMin`, and they are trivially
swappable** — the `[cover]` line reports both and the spec's table reports the
difference. Quoting 91,660 as "the peak" would say the decode cost nothing.

**PNG IS 2.2× SLOWER THAN JPEG ON A SMALLER IMAGE**, which is the asymmetry the gate
was deliberately split to see: `decodeCover` passes the panel size to TJpgDec as
`atLeast`, so **a JPEG gets free IDCT halving before the box filter ever sees it** — up
to 64× fewer pixels through the filter and the diffusion — and **PNG has no scaled
inflate**, so it walks every source pixel. **A single number would have been a JPEG
number**, and JPEG is 81% of covers, so the 17% that is PNG is the half nobody would
have looked at.

**THE DEFLATED PNG DECODED, AND THE SPEC'S DESKTOP TEXT SAYS IT CANNOT.** Written from
a desktop measurement, the spec states outright that it "does not fit on the device"
and answers `OutOfMemory`; the probe decoded it `Ok` at 120,248 bytes. **Both can be
true, because this probe ran at BOOT** — ~173 KB free — **and a cover is decoded at
SLEEP**, out of whatever a session left behind. The stated limit therefore still says
**"may refuse"**, and that word is deliberate: this is the case where the headroom is
larger than modelled and therefore wrong in the *safe* direction, which is a thing to
say out loud rather than quietly enjoy. **Do not upgrade "may" to either "does" or
"works" until a real sleep has been measured.**

The three shapes a sleep can take, and `[power] sleep cost` is the line that adds them
up: `DETAILS` or nothing open is one MONO paint, ~825 ms; a cover with the cache warm
is one GRAY paint, three waveforms and four render passes; a cover with the cache cold
is a MONO paint, then the probe, then the decode, then a second GRAY paint — **the
expensive one, and the first sleep of a new book is always it.**

### Stated limits

| limit | incidence |
|---|---|
| progressive JPEG refused | 2 / 225 — **but 2 of the user's own 16** |
| deflated PNG may refuse on heap | 1 / 225 |
| interlaced or palette PNG refused | 0 / 225 observed |
| one cached cover; alternating books re-decode | by design |
| X4 crops ~10% of a 2:3 cover's **width** at `FILL` | default, reversible in Settings |
| first sleep of a new book shows the card for a few seconds | by design |
| a cover needing more than **×2.5** to fill the panel is refused `TooSmall` | 0 / 225 |

**WHAT THE CORPUS ACTUALLY YIELDS, run through the built pipeline: `Ok` for 223**,
`Unsupported` for 2 (both progressive JPEGs), and `NoCover` / `ReadFailed` /
`OutOfMemory` / `Abandoned` all **zero**. **The denominator is 225 now, and it read
224 here for as long as one book could not be opened at all** — `openBook` refused it
over a Calibre `user_metadata` `<meta content="…">` against `Xml::kMaxAttrBytes`, so
it never reached a decoder and counting it as a cover failure would have
double-counted it against the EPUB refusal rate. That refusal is gone (an attribute
too long to hold reads as absent — see **The lifetime rules that changed**), the book
opens, and **its cover decodes**, which is why the numerator moved with the
denominator. **The distinction it was drawn for still holds** and is the thing to keep:
a book that cannot be opened is not a book whose cover failed.

**Every one falls back to `DETAILS` with the badge shown, and logs the reason.** That
is the whole reason `CoverResult` distinguishes `NoCover` / `Unsupported` /
`ReadFailed` / `OutOfMemory` / `Abandoned` / `TooSmall` rather than answering a bool: a
refusal that cannot say which of the six it was is indistinguishable from a decoder
that does not work, and this file has paid for that shape more than once. **`TooSmall`
is the sixth and it was added rather than borrowed** — see **A small cover is enlarged**
below, which is exactly this rule applied one refusal later.

**PROGRESSIVE JPEG MATTERS MORE THAN 2/225 SUGGESTS.** It is **12.5% of the user's own
library**, and no small streaming decoder handles it. It is a **stated refusal**, not
an oversight — and the corpus is the wrong instrument for how often a real reader meets
it.

**THE CROP IS AN AXIS QUESTION AND THE PLAN HAD IT TRANSPOSED.** The X4 is 3:5 = 0.600
and a 2:3 cover is 0.667, so a cover is **relatively wider** than the panel: `FILL`
crops its **width** and `WHOLE` leaves bands **above and below**. 160 of 225 covers are
2:3 to within half a percent, so **on the X3 (528×792, 2:3 exactly) `FILL` loses
nothing for 71% of books and the setting is a no-op there**. What earns the setting is
the tail: the squarest corpus cover is 877×973, and `FILL` cuts its title off at both
edges, keeping 584 of 877 columns — a **33.4% loss**.

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
- **AND THE PERCENTAGE BESIDE IT SAID `0%` THROUGH THAT WHOLE WINDOW, FOR TWO PHASES,
  BECAUSE THE EM-DASH RULE WAS APPLIED TO ONE SLOT OF THE TWO IT GOVERNS (#93).** That
  number **is** this counter as a fraction — the board's 53 of 890 is 5.955%, drawn as
  `6%` — so it is divided by the same total and is unknown in exactly the same moments,
  and `syncVm` answered the unknown with a literal `0`. Reported off a device after a
  week of real use. It is `ReaderViewModel::kProgressUnknown` (**-1**) now, drawn `—%`,
  and the fix is at the **producer** because that is where the other two spellings of
  this already are: `pageTotal`'s 0 and `percentFor`'s -1 for "not started".
  - **`0` IS A VALUE THE ARITHMETIC REACHES, which is the whole of why the sentinel is
    not 0.** `(page * 100 + total / 2) / total` is 0 for page 1 from **201 pages up**,
    and that is right — so the unknown was **pixel-identical** to a reader standing at
    the top of the chapter. Not a bounded wrong, either: the count runs only in a quiet
    window, so **a reader turning pages faster than `kCountQuietMs` never lets it fire**
    and can be well into a chapter still being told 0%.
  - **THE CONDITION IS THE COUNTER'S OWN `pageTotal == 0`**, so one test decides both
    slots and they cannot disagree about what is known — and it picks up the degenerate
    complete-but-empty chapter for free, where there is no denominator and the counter
    already read `0 / —`.
  - **THE PROGRESS BAR IS OMITTED RATHER THAN DRAWN EMPTY**, and that is the half a
    dash cannot fix: a bar is a **length** stating the same fraction, and
    `drawProgressBar(..., 0)` paints the exact outline a settled 0% paints. An absent
    claim beats a false one, the call this file already makes for an unread gauge (`-1`,
    never `0%`). **Nothing reflows** — the percentage is placed off the left padding and
    the counter off `fb.width()`, which is the same property that lets
    `ReaderAnchored.dc.html` put the return arrow in that slot.
  - **NO SURFACE BUT THIS ONE WAS AFFECTED, checked rather than assumed.** Every other
    percentage on the device is the **byte-based** `reading_store.h::progressPercent` or
    a value stored from it — the sleep card and Home read `last.percent`, a Library row
    and Book details read the sidecar (and `percentFor`'s -1 draws `NEW`), the reader
    menu's header and the peek's band call the free function. Only
    `ReaderViewModel::progressPercent` is derived from the page count, so only the
    Reader's own footer could say this.
  - **TWO EXISTING ASSERTIONS HAD BLESSED IT, one of them under the comment *"a
    percentage of an unknown is not a number"* while asserting `== 0`** — the rule
    written down beside the defect it forbids, which is this file's most expensive
    recurring shape. **Every golden passed** and could not have failed: they all call
    `completeIndex()` first, because the board draws the settled state.
    `reader_counting{,_x3}` are the transient state's own goldens and are the only thing
    in the suite that can prove `—%` is an em dash rather than a **notdef box** — the
    percentage is `Role::Meta700`, a different generated asset from the counter's
    `Role::Meta400`. Reverting just the theme's half draws a literal **`-1%`**, which
    those two goldens catch and nothing else does.
  - **The board's rendered specimen did not move**: the rule went into
    `design/Reader.dc.html`'s footer as prose beside the settled state it draws, whose
    own note already said the transient state would want its own board file. All four
    `--only reader` panels are byte-identical across the change and `reader` still
    measures **5.24% / 6.29%**.
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

**THE COUNT IS INTERRUPTIBLE, AND THAT — NOT THE QUIET WINDOW — IS WHAT STOPPED IT
BLOCKING.** `completeIndex` takes a `bool (*)(void*)` stop predicate (a function
pointer, not a `std::function`: this is `-fno-exceptions` embedded code), which the
shell answers from `rawSamplesPending()`. The index is built into a SCRATCH vector
and committed only on completion, so an abandoned count leaves `starts_`, `at_` and
the page on glass byte-identical — invisible to everything but the builder.

- **The check is per BLOCK.** It was set to every 8 blocks first, from a "1–3 ms a
  block" estimate; the device's own `pages=315 in 3605ms` over ~600 blocks says
  **~6 ms a block**, so 8 was ~48 ms of latency bought for 0.03% of the walk. Another
  instance of the ratio trap under `kEagerCountBytes`. A block is the floor —
  `chapter_.next()` and `pb.add()` cannot be stopped half way.
- **The one thing it does not restore is the live `PageBuilder`**, because the walk
  rewinds the `ChapterReader` the builder reads from and there is no second stream to
  rebuild it with (another 32 KB window against a 42 KB floor). So the next FORWARD
  turn pays a full `seekTo` — **and this is NOT only the abandoned count's doing,
  which is what the argument for the long window got wrong.** `completeIndex` ends in
  `seekTo(at_)`; `at_` is by definition the page the ring is most certain to hold, so
  the restore leg takes a cache hit — and a hit leaves `pb_` null deliberately,
  because nothing was decoded. **A count that COMPLETES spends the stream too**, so
  every deferred chapter cost one forward turn a rewind whatever the window was. The
  long window was buying nothing. (Also off by one press: the queue drains at the top
  of `loop()`, so the press that interrupted the count IS the next thing dispatched.)
- **`ReaderScreen::restreamAtCurrentPage` PUTS IT BACK, and abandoning THAT is free.**
  It is the same walk `warmPageRing` makes — one private `rewalkToCurrentPage` with
  two gates, not two copies — and it runs **only when `pb_` is already null**, so it
  has no live builder to spend: an interrupted restream leaves exactly the state it
  found. That makes the trade one-sided rather than balanced — it finishes and the
  next forward turn is free, or it is cut and that turn pays what it pays today — and
  **that, not the walk being cheap, is what lets it have a short window** where
  `completeIndex` and `warmPageRing` cannot. `hasLiveStream()` is the gate the shell
  asks first, so an ordinary page turn costs a pointer test and no log line.
  `[restream] ready|abandoned page=N` is what tells a working idle job from a silent
  one.
- **So `kCountQuietMs` IS ITS OWN NUMBER AGAIN, at 2000 ms**, having been
  `kRefineQuietMs` while the two shared a cost. The derivation is in the constant:
  the floor is the **1360 ms** longest pause measured while still turning pages, the
  margin is 1.47× rather than the refinement's ~3.5× because the cost of being wrong
  is now one rewind on one press at most once per chapter rather than 1408 ms of
  uninterruptible dead buttons, and the prize is the total landing on glass at ~3.0 s
  instead of ~6.0 s. **It cannot re-open the percentage-going-backwards bug**, which
  is the other thing this constant has to be checked against: `progressPercent` is
  made of BYTES now and reads `page`/`pageTotal` only where there is no inflater to
  ask, so for a real deflated chapter the count's timing does not enter it —
  `test_reader_restream.cpp` asserts the percentage across the moment the count lands
  and it does not move. On the byte-less fallback path, shortening moves the same
  lever in the direction that made it better.
- **The invisibility property is asserted over a CARD-BACKED book, and it had to be.**
  `ChapterReader::bytesRead()` is `inflated_ ? produced() : 0`, so the in-memory
  fixture reports **0 forever** and `CHECK(chapterBytesRead() == was)` over it is
  `0 == 0` — it passed with a mutant that zeroed the field, which is how the hole was
  found. `test_reader_restream.cpp` builds a real EPUB in memory for that one
  assertion; the trick that makes it cheap is that **DEFLATE has a stored-block mode**,
  so a valid method-8 entry needs a framer and no compressor.
- **There are TWO count sites**, the deferred one in `loop()` and one inside
  `refineNow()`. Fixing one and not the other would have brought the freeze back on
  whichever path the reader happened to take.
- **Both `[index]` lines say `counted|abandoned`.** `pageCount()` on an abandoned
  count is the old partial figure, and the line reported it as the answer — the
  "reports on less than it claims" shape again.

A chapter over the threshold is counted in a quiet window of its own,
`kCountQuietMs`, **and then repaints on the FAST path**. That repaint was
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
`kerning`'s cmap searches were noise beside it. ~~If a page turn has to get faster than
~570 ms, the blit is the target and the metrics are not.~~ **Both halves of that
sentence are now spent, in opposite directions.** The blit was the target, it was taken
(byte-wise now, a page render 215 → ~25–41 ms), and a page turn is ~92% panel — there
is no page-turn work left worth doing. And "the metrics are not" was right about the
DRAW and wrong about everything else: `kerning`'s *kern-table* bisection, which this
sentence never separated from its cmap searches, was **73% of a pagination walk**. See
**The glyph cache**, which now carries the measurement and the fix.

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

**AND IT REFUSES A SPINE, NOT METADATA.** `Epub::open` used to refuse a book whose
`unique-identifier` named an id no `dc:identifier` carried, on the stated grounds that
an unresolved identifier "breaks everything keyed on it — which is what per-book
reading state will be". **That consumer never arrived**: reading state went to the
card as `/.reader/state/<hash of the PATH>.json` with `bookBytes` as the identity
check, and nothing in the firmware has ever read `identifier()`. Measured against the
user's own library the check refused **4 of 16 books** — publisher and Calibre output
alike, a whole `Dune` trilogy among them — and every one of them reads: the reported
book walks 55 spine entries and 197,330 words once it is let in. The identifier is
best-effort now and **empty means the book did not say**, the same call the spine's
`toc` attribute already got.

- **A prediction in a comment is a claim with an expiry date**, and this one was
  restated in four places — `epub.cpp`, the header, `mkepub.py`'s docstring and a test
  name — so nothing in the repo disagreed with it and the design it described had
  changed underneath all four.
- **Removing the refusal made a substitution reachable, and the guard is the fix.**
  An absent `unique-identifier` and a `dc:identifier` with no `id` are **both the empty
  string**, so "does this identifier carry the id the package named" answers *yes* for
  a book that named nothing — reporting an identifier the book never designated.
  `!uniqueIdRef.empty()` is what keeps empty honest, and it is proved by mutation
  rather than by argument.
- **A DIFFERENT BOOK WAS REFUSED FOR A NEIGHBOURING REASON, AND AN ATTRIBUTE TOO LONG
  TO HOLD READS AS ABSENT NOW.** `Xml` capped a tag's attribute bytes at
  `kMaxAttrBytes` (512) and answered `Error` above it, so `Epub::open` reported "the
  OPF is malformed" and the book was gone. What was over the cap was one `<meta>` of
  Calibre custom-column JSON — **574 decoded bytes on `Walden ou la vie dans les bois`
  and 489 on `Le soleil et l'acier`**, both off the same real shelf, both
  three-hundred-page novels lost to a field describing a column in somebody's library
  manager. Same family as the identifier and the unknown entity, and it took all three
  to make the rule visible: **nothing in a tokenizer's bounds is a reason to refuse a
  document.**
  - **THE ARGUMENT FOR THE REFUSAL WAS TRUE AND THE CONCLUSION DID NOT FOLLOW**, which
    is the part worth keeping. A value cannot be SPLIT the way a long text run is,
    because `attr()` answers about the whole tag — and `xml.h` said so and stopped
    there. **Between splitting and refusing sits reporting it ABSENT**, which every
    caller already handles: `hasAttr()` exists precisely to tell absent from empty.
  - **DROPPED WHOLE, NEVER TRUNCATED.** A clamped `href` resolves to a path that is
    *wrong* rather than to nothing, and no caller can tell a short value from a cut
    one. Where the missing attribute really was load-bearing the layer above still
    refuses, by its own rule and with its own message — a spine naming a manifest id
    nothing carries.
  - **THE PACKING OFFSET REWINDS**, so a blob sitting FIRST costs only itself. Without
    that, everything after the dropped attribute is dropped too and a tag loses the
    `href` it needed for the metadata it did not.
  - **`kMaxAttrs` DROPS BY THE SAME RULE**, because running out of slots is the same
    event as running out of bytes and two spellings of one rule is what this file has
    a section about. Observed maximum on one tag across 226 real EPUBs is **eight**, on
    an `<html>` carrying namespace declarations, so that cap has never been reached by
    a real book.
  - **THE CAP DID NOT MOVE, AND RAISING IT WAS THE WRONG FIX** — it is what produced
    512 ("3x the observed worst case") and that was already wrong twice. Measured over
    226 real EPUBs: every tag over 400 bytes of attributes is a Calibre
    `user_metadata` meta, and the fattest tag that is **not** one is that 379-byte
    `<html>`. The failure mode was the bug; the number was fine.
  - **THE CHECK IS THE 224 BOOKS THAT DID NOT MOVE.** The corpus goes 224/225 opened
    to **225/225**, and every other book is byte-identical in title, author, spine
    length, block count and text bytes — the same standard the entity fix was held to,
    where "the unchanged thirteen are the check that matters". `Xml::attrsDropped()`
    is what keeps a drop from being silent.

**AND A BLOCK OVER `kMaxBlockBytes` IS CUT IN TWO, WHICH IS THE THIRD MEMBER OF THE
SAME FAMILY (#37).** 64 KB set `error_`, which stops `BlockReader`, which ends the
chapter — and `next()` returning false is **also** how a chapter ends normally, so
nothing reported it. It was found by the CORPUS and not by the audit, because the audit
was read off the refusal sites and this is a truncation site. **Split rather than
truncate-and-record**, the ticket's other candidate: what the cap protects is the size
of ONE block and both halves are under it, so splitting keeps the bound exactly and
loses no text, where truncation's magnitude is unbounded — a chapter that is one giant
`<div>` with no `<p>` is one block. `BlockReader::blocksSplit()` counts the cuts, in
`Xml::attrsDropped()`'s shape and for its reason.

- **THE TICKET SAID "2 CHAPTERS" AND THE DAMAGE WAS 84–92% OF TWO WHOLE BOOKS**, which
  is the corpus baseline's own stated blind spot arriving: *"`truncated` counts
  chapters, not bytes"*. Measured before and after over all 225: `The 32nd Mersenne
  Prime` 19,411 → **251,869** text bytes and `The Number "e"` 19,494 → **121,991**, so
  the over-long paragraph was the second block of the FIRST chapter and everything after
  it went. Chapter rate 99.97% → **100.00%**, +334,955 bytes, and **223 of 225 books
  byte-identical in every field**.
- **WHAT IT COSTS, stated rather than discovered:** `indentedAfter(Paragraph,
  Paragraph)` is true, so a continuation takes the 1.5em paragraph indent — one spurious
  paragraph break per cap's worth of unbroken text, against text that is simply absent.
  **Raising the cap was refused for #35's reason twice over: the failure mode was the
  bug and the number is fine** — the first half of which is still right and the second
  half of which was wrong in the direction nobody checked. See #90 below.

**AND THE NUMBER WAS NOT FINE: `kMaxBlockBytes` WAS 64 KB AGAINST A 42,152-BYTE FLOOR,
SO THE CAP PROTECTED NOTHING AND THE HEAP GAVE OUT FIRST (#90).** It is **8 KB** now,
and the growth is a **reserve** rather than a `push_back` ladder. #37 is not what
introduced this — the string grew to 64 KB before it too, and only *then* set `error_`,
so the peak was identical — but #37 put recoverable text behind the limit, which is
what made the limit worth calibrating.

- **IT WAS WORSE THAN THE 1.5× THE TICKET STATED, BECAUSE A CAP OF N DOES NOT COST N.**
  `push_back` grows geometrically and libstdc++ — which is what the ESP32 toolchain
  ships — climbs `15·2^k`, so a 64 KB block ended at a capacity of **122,880** and its
  last reallocation held 61,440 and 122,880 **at once**: 184,320 bytes transient, and up
  to 307,200 with the piece already handed to the caller. Under `-fno-exceptions` the
  failing request is `abort()` with no diagnostic — **a reboot onto Home, which this
  file already records as having been misreported twice as "opening a book goes back to
  Home"**.
- **AND BLOCKS FAR BELOW THE CAP WERE ALREADY IMPOSSIBLE, WHICH IS THE FINDING THE
  TICKET DID NOT HAVE.** A natural **16,384**-byte block needs 76,800 bytes — 182% of
  the floor — so the device's real ceiling was a paragraph of about 10 KB, **a sixth of
  the cap**, and **7 of the 225 corpus books (3.1%) sat above it**, not the two #37
  found: `Paradise Lost` (one 50,983-byte block), `The Online World`, `Poetry`, and the
  two Gutenberg mathematics texts. **The cap named none of them**, which is what makes
  this a bound that was fiction rather than a bound that was generous.
- **THE 8 KB IS THREE BOUNDS THAT AGREE**, and `document.h` carries the table. (1) It is
  the corpus's **99.99th percentile**: 69 of the **537,474** blocks 225 real books write
  exceed it, and **208 of the 225 have no block over it at all**. (2) Reserved, the peak
  is two buffers plus one 1,920-byte seam transient — **18,308 B, 43.4% of the floor**,
  largest single request 8,194 B — where 16 KB would be 82% and *"the largest free
  BLOCK decides, not the free total"* is not a rule you satisfy at 82% of a fragmented
  heap. (3) It **is not a regression in the common case**: a caller's `out` keeps its
  capacity between blocks and never shrinks, so a book already pays `2 × ladder(its
  largest paragraph)` — the **median** corpus book pays 15,360 B today and the p90 book
  30,720 B, so this is **+1,028 B on the median book and −475,132 B on the worst**. The
  band was `(Xml::kTextBytes, ~8 KB]`: below 1,024 the one-cut-per-text-node invariant
  `document.cpp` asserts breaks.
- **THE RESERVE IS THE HALF THAT MAKES THE BOUND A BOUND**, not the cap. A cap that
  holds only if the allocator's growth factor is 2 is an argument about a standard
  library this project does not ship — libc++ lands the same block at 12,287 and
  libstdc++ at 15,360. Reserved once, the capacity **is** the cap on both. It is
  **nothrow-PROBED**, because there is no `std::nothrow` spelling of
  `std::string::reserve`: the probe asks the heap the same question, then the reserve
  takes the block it just released, which is `imagefit.cpp`'s shape. **The refusal needs
  no new words** — it is the message `BlockReader`'s own `State` allocation already
  answers with, so no new `BookErrorReason` and no new copy shape on
  `BookError.dc.html`.
- **`take()` SWAPS INSTEAD OF MOVING, so the reserved buffer comes back** and the
  reserve is paid once per reader rather than once per paragraph — 537,474 times over
  the corpus. That is also what `restart()`'s *"reusing the buffers"* has claimed since
  it was written and did not do: `st.cur = Block{}` threw the buffer away every block.
  Nothing can hold a view into the swapped-out value — `LaidLine::text` is OWNED
  precisely so a Page can outlive its blocks — and the caller has by definition already
  consumed it.
- **THE FIRST VERSION LOST TEXT, AND TWO OF #37's OWN TESTS CAUGHT IT.** `roomFor` runs
  once per text NODE and a block spans many, so it swapped a *fresh* reserved buffer
  into a string that was not empty and threw away everything accumulated since the last
  reserve — a **hole in the middle of a rejoined digit run**. `reserve` copies the
  content across by definition, which is the whole reason to use it rather than a swap.
- **MEASURED, IN #37's OWN IDIOM: 208 of 225 books are byte-identical in every field
  INCLUDING the block count**, 225/225 still open, chapter rate still 100.00%. The 17
  that moved take **190 cuts** and **+186 blocks**, and the corpus's text goes
  126,614,534 → **126,614,498** — **36 bytes over 190 cuts, every one of them the single
  space the cut landed on**, which `take()`'s trailing-space trim removes. That is right
  (the pieces render as two paragraphs, so the paragraph break *is* the word boundary)
  and it is now the ONLY thing a cut may lose: `test_document.cpp` pins it with a
  fixture that puts the space **on** the cap, which a run of digits — every other case
  in that file — cannot reach.
- **THE VISIBLE COST: one spurious indent per 8 KB of unbroken text, about once per 15
  pages OF IT**, against #37's once per 121 at 64 KB. **On 208 of the 225 books the rate
  is zero**, because they write no paragraph that long; what is above 8 KB is Gutenberg
  plain-text conversions, where a whole book of the poem is one block and an extra
  indent is the least of it.
- **`ChapterReader::blocksSplit()` EXISTS NOW, and it had to for the cut to be
  observable at all.** `BlockReader::blocksSplit()` shipped with #37 as the
  `Xml::attrsDropped()` counting idiom and **nothing outside `document.h` could reach
  it** — a producer with no reader, the mirror of `ListRow::trackingEm1000`. The
  pass-through is an observation point in `held()`'s sense, and it is **per WALK, not
  per chapter**: a rewind calls `BlockReader::restart()`, which zeroes the counter.
  **Nothing in the FIRMWARE reads it yet** — the corpus probe is its only caller — so a
  cut is still invisible in a serial log, and `[chapter]`'s line is where it would go.
**THE SIBLING BOUNDS STILL HAVE #37's SHAPE — and, after #90, the OTHER shape too: not
one of them is calibrated against a device figure either.** They are cards rather than
paragraphs.

- `kMaxEmphasisPerBlock` (256) is the likeliest of them to meet a real converted book
  and the cheapest to fix — an emphasis run past the cap could be DROPPED, which costs
  one phrase its italics, where today it costs the rest of the chapter. `kMaxBlocks`,
  `kMaxNestDepth` and `kMaxTocEntries` end their stream the same way; 0 corpus hits
  each, which is "no evidence yet" and not "does not happen".
- **AND THE HEAP QUESTION IS OPEN FOR ALL FOUR.** `kMaxEmphasisPerBlock` is 256 `Span`s
  — 2,048 bytes, and it is a `std::vector`, so it climbs the same doubling ladder to
  4,096 with both buffers live at the last step; `kMaxBlocks` (4,096) bounds a
  `Document`, which the reader does not build but `buildDocument` does. Neither is
  anywhere near the block string's old 122,880, which is why #90 stopped at
  `kMaxBlockBytes` — but "small enough not to matter" is the argument that was wrong
  once already, and none of the four has a measurement behind it.

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

**THREE EDGES PLUS A QUIET WINDOW**: leaving the book with Back, crossing a
chapter, and sleeping — each fired unconditionally, because each is a moment the reader
would notice losing. There were FOUR: the reader menu's `Close book` popped the Reader
from underneath an overlay, which the `leaving` save — fired on Back with the Reader ON
TOP — could not see, so it carried its own `closing` edge. **That row was cut
(2026-08-24) and its edge with it**: Back from the page is the one way out of a book
again. **Leaving is saved BEFORE the
dispatch** — Back pops the Reader and once popped there is no screen left to ask where
the reader was. Back is the only way out (`Gesture::Back` → `Action::pop()`), so this
is one save on the way out rather than a save per event.

**IT WAS THREE EDGES ONLY, AND THAT COST A CHAPTER OF READING TO A FLAT BATTERY.** The
reason recorded here was "a turn is ~570 ms of panel and a card write on each one would
be felt", which was right about the cost and wrong about where to put the work: it
bounded a power cut's damage at one chapter, which on a real novel is an hour. The write
is not made cheaper — it is made to happen when the loop is already idle, which is the
answer the page count, the refinement, the ring warm and the card log all reached
before it. `kSaveQuietMs` is **2000 ms**, sized from the device's own twelve-turn
measurement (median 72 ms between turns, longest 898 and 1360) so that **steady page
turning never pays for it at all** and an ordinary reader, who spends ~23 s on a page,
saves about two seconds after every turn.

**IT IS NOT HIDDEN UNDER THE WAVEFORM, and that idea does not work here.** The
`triggerDisplay`/`completeDisplay` seam really does leave ~389 ms of idle CPU, and
`EpdBus` really does balance CS per operation — so the bus is electrically free in the
gap, which is **not** what CLAUDE.md used to say ("the driver keeps the display's CS
asserted across them" is true of the BUSY waits inside a call, not of this seam). It is
still forbidden: the SDK states the contract in three places, `PanelDriver.h`'s "the
caller does non-SPI CPU work in the gap and issues no other bus op until
`displayFinish()`" being the sharpest, and `Uc8279Driver::displayStart` leaves a
`PARTIAL_IN` window open for `displayFinish` to close, so the controller is mid-sequence
throughout. **The quiet window costs the reader the same nothing and breaks no
contract**, so there was never anything to buy by taking the risk.

**`ProgressSaveGate` (`core/include/reader/progress_save_gate.h`) IS WHAT MAKES THE
FOURTH CALLER SAFE**, and it is in `core/` because both of its jobs are exactly the kind
`shell/` has no harness to check:

- **Has the reader moved.** The quiet window is reached on every loop iteration once
  the buttons go quiet, and `savePosition` reaches its `Unchanged` answer by **reading
  both sidecars back off the card first** (`writeIfChanged`) — so an ungated save would
  be two file reads per iteration, forever, on the panel's own SPI bus. Three int
  comparisons replace all of it, and the card is never touched.
- **Has the card earned another attempt.** This is the hazard the feature turns on. A
  card can be readable and refuse writes, and `writeAll` calls `noteCardGone()` on a
  write that fails after opening, which `pollCardPresence` turns into an App rooted at
  `SdMissingScreen` — so a failing save can throw the reader out of a book they can
  still read. Saving ~100× more often would make that ~100× more likely. The gate backs
  off 30 s after a failure and **gives up for the session after three**, at which point
  the behaviour is exactly the three edges that shipped. `forget()` clears the stored
  point on a book change but deliberately **not** the failure count: giving up is a fact
  about the card, not the book, and re-arming per book would hand a read-only card three
  fresh attempts every time one is opened.

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

**AND WITHIN THE OPEN CHAPTER IT IS BYTES TOO, WHICH IT WAS NOT AT FIRST.** It
interpolated on `page`/`pageTotal`, and `pageTotal` is **0 until the deferred count
lands** — so for a long chapter the number did not advance at all for the first
seconds, and did not advance while the reader kept pressing at all. Reported off the
device as "I've advanced but I'm still at 40%", against a Kobo's 42% at the same
place. Two consequences, and the second is the sharper one:

- **A SAVE TAKEN IN THAT WINDOW PERSISTED THE UN-ADVANCED FIGURE**, over a better one
  written by an earlier save that did have the count. That is how the percentage went
  BACKWARDS — 42% on the sleep screen, 40% on Home, then 40% everywhere.
- **Widening `kCountQuietMs` from 1200 ms to 5000 made it worse**, because the count
  lands later. A latency fix and a correctness bug meeting in one constant is worth
  noticing: the constant was right and the thing depending on it was wrong.

`ChapterReader::bytesRead()` is the inflater's `produced()`, which is the SAME
quantity the rest of the sum is made of and needs no count and no walk.
`ReaderScreen` records it at the end of the page on screen — the end rather than the
start, because the page in front of you has been read by the time you leave it — and
**the ring carries it per slot**, since a page served from the ring was decoded long
ago and the stream has moved since.

**`page`/`pageTotal` REMAIN AS THE FALLBACK, not as a second answer.** They are used
only where bytes are unknowable: a stored archive entry and an in-memory chapter have
no inflater to ask. Where both are offered the bytes win, and there is a test that
says so — the alternative is two spellings of one fact, which this file has a rule
about.

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

**AND THE LIBRARY IS BUILT ONCE, WHICH IS THE SAME BUG HOME HAD.** The rows are
derived when the screen is pushed, and the Reader is pushed ON TOP of the Library — so
the pop that leaves a book hands back that same instance with the rows it was born
with, and a book just read to 31% still said `NEW`. Reported off the device, exactly as
Home's "after reading a book, going Home still said NOTHING OPEN YET" was. Two screens
draw reading progress and both had to be told it moved: `saveReadingPosition` sets
`gHomeStale` and `gLibraryStale` together, and each is consumed when ITS screen is
reachable — sharing one flag would let Library, Back, Home clear it before Home used it.

- **The Library's is consumed when the Library is ON TOP**, which is what keeps it off
  the reader's critical path: the position also saves on chapter crossings and in the
  2 s quiet window WHILE READING, and the Reader is on top for all of those. The first
  iteration that can consume it is the pop out of the book.
- **It REFRESHES rather than rescans**, and the difference is ~600 ms. Only
  `/.reader/state` changed — a book cannot ARRIVE while the firmware runs, the same
  premise `libraryCountForHome`'s cache key rests on — and the save has just called
  `forgetCardFacts()`, so a `rescan()` would pay a fresh `/books` listing plus one per
  folder for the counts, on the critical path of a Back.
  `LibraryScreen::refreshProgress()` re-derives over the rows already there and moves
  neither the focus nor the window.
- **`applyProgress` is the one spelling of the derivation**, shared by both callers —
  the second copy is the extraction point, and `to_string(percent) + "%"` appears in
  the row's value AND in Book details' Progress row.
- **A failure leaves the rows alone.** An unreadable index means the card did not
  answer; re-deriving from an empty one would turn every started book back into `NEW`.

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
(`Contents.dc.html`, not built at the time — which is also why the Reader's own footer
says a bare `CH. 03`). Both lines became one, `CH. 08 OF 92`, at the 0.16em counter
tracking of the line it replaces. `HomeViewModel::currentPage`/`pageCount` are gone
with it. Re-blessing the four Home goldens was verified the strong way: the change is
confined to rows 283–468 with **0 pixels differing** above or below, so the header
band, cover dither, title, author, the 67px numeral, both menu rows and the hint bar
are bit-identical.

**AND THIS LINE CALLED THAT COUNTER "FREE AND TRUE". IT WAS FREE AND IT WAS FALSE.**
Reported off an X3: `47% · CH. 14 OF 36` on the CONTINUE block, and Contents then put
the reader at part 2 of a book with **7 chapters in 2 parts** and five selectable rows
left. **A spine is not a chapter list** — it counts the cover, the title page, the
copyright, the contents, the part dividers, the acknowledgements, the notes, the index
and the about-the-author alongside the chapters — so the pair invited an arithmetic the
numbers do not support, and the reader did it. **It is the chapter's NAME now**, and
the sentence above is corrected rather than deleted because *"free and true"* is
exactly the shape this file records as its most expensive recurring defect: a claim
that was checked for obtainability and never for truth.

- **NO BETTER TOTAL EXISTS, and that is why there is none rather than a fixed one.**
  Measured over the 206 corpus books with a usable NCX: **126 (61.2%)** have a spine
  count exceeding the spine entries their TOC names at all — worst **164 against 26**,
  and `Dune - Tome 3` on the reporting user's own shelf is **73 against 38** — and
  **183 (88.8%)** have a spine count differing from the count of entries their TOC
  names as *selectable* chapters (`ContentsScreen`'s own header rule). Only **23
  (11.2%)** have spine, named and selectable all agreeing. **The reported book is not
  even in the 126**: 36 spine entries and 36 named, of which 26 are chapters — so a
  count that matches the TOC exactly is still not a chapter count, which is what makes
  this a fact about spines rather than about untidy books.
- **NUMBERING THE TOC'S OWN ENTRIES WAS REFUSED**, because it is a third numbering
  system beside the book's own — the mistake `Contents`' right-hand slot was already
  fixed for when `Chapitre 1.        CH. 09` shipped. If a name is shown, the name is
  what is shown.
- **IT IS THE READER'S OWN LABEL, NOT A SECOND DERIVATION OF IT.** `ReaderScreen`'s
  header band, Contents' `NOW` row and this line now name the reader's chapter in the
  same words, which is the rule this file already states for the `NOW` marker: two
  screens naming one fact differently is two spellings of it. **A book with no contents
  fills it with `CH. 08`** — `updateChapterLabel`'s fallback, a position with **no
  total**, which is what the Reader's footer says and the only handle such a book
  offers — so the no-TOC case needed no new answer and cannot disagree with the Reader.
- **`LastRead` CACHES IT, AND THE SIDECAR ROUTE WAS PRICED AND REFUSED.**
  `ReadingPosition::chapter` has carried this string since it shipped, so reading the
  per-book sidecar in `readingPointer()` would need no new key and have no first-run
  gap — and it puts a small-file read on the SPI bus at boot **and on every Home
  rebuild**, which is the critical path of a Back out of a book that
  `libraryCountForHome` and `DirCountCache` were both written to clear. Caching it
  costs one string in a file already read and **no card work at all**: the save site
  already has `rd->vm().chapter` in hand for the sidecar, one field up.
- **THE PRICE IS ONE BLANK LINE, ONCE.** `kLastReadVersion` did **not** move — a bump
  makes `loadLastRead` refuse the whole record, so every device would lose its CONTINUE
  block, title, author and percentage together on the first boot after this firmware —
  so the key is optional and an older pointer reads **empty**. Home draws that line
  blank until the book's next save, which is the first time the reader leaves it,
  sleeps in it or crosses a chapter. **Blank rather than the spine position**, because
  the only substitutes available are the claim this replaced and nothing: an absent
  claim beats a false one, which is the call this file already makes for an unread
  gauge (`-1`, never `0%`) and for a badge promising a wake charging cannot deliver.
- **`spineCount` WENT WITH THE LABEL IT EXISTED FOR**, from `LastRead` and from the
  shell's `ReadingPointer`. It was written, read and spent composing `CH. n OF N`;
  keeping it would have left a field with no reader, which is `ListRow::trackingEm1000`
  and `readerBookTitle_` a third time. Dropping the key rewrites every card's pointer
  once and nothing reads it on the way in, so an older pointer still loads.
- **THE RUN ELIDES AT 0.10em, AND BOTH HALVES CAME OFF THE BOARD.** 0.10em is the
  tracking `Main.dc.html` gave the chapter line it drew *before* the counter displaced
  it (`kMetaEm`'s 0.16em was the counter's, and `kTightMetaEm` already existed) — a
  name is not a counter. It elides because the words come off the **card**: unelided,
  `PREMIÈRE PARTIE : À LIRE AVANT L'ACHAT` reached column **479 of 480** and **527 of
  528**, off the panel at both geometries. **It may not WRAP**, and that is the Sleep
  card's lesson one screen over: the title above it already grows into a budget derived
  from everything below the block, and one budget cannot serve two growable runs
  without saying which yields. So the run naming the BOOK keeps every line and the run
  naming where you are in it takes one — which is also what keeps `columnFixedH`
  honest, since it reserves exactly one `meta.lineHeight()`.
- **AN EMPTY LABEL STILL COSTS ITS LINE**, so nothing below it steps up; that is the
  property `test_long_title.cpp` pins by diffing every case against a **blank** render
  rather than against the demo one, and a `ry` advance skipped for the blank fails it
  at both geometries.
- **RE-BLESSED PER PIXEL: all ten Home goldens differ by exactly 1005 pixels, in
  exactly 15 contiguous rows, in columns 153–320** — one line of text in the stats
  column, nothing else, identical column extent at both geometries. The row band is
  283–297 for every state except `home_long_title`, where the wrapped title pushes it
  to 415–429 (X4) and 371–385 (X3) — the derived budget's two branches, moving as they
  should. `home_long_chapter` and `home_long_chapter_x3` are **new**, and they exist
  for `home_long_title`'s reason: the elide test proves the run stops inside its
  column, and a column of notdef boxes stops inside a column too. They read
  `PREMIÈRE PARTIE : À LIR…` and `… À LIRE AV…`, accented capitals and the real
  ellipsis, cut differently at the two widths.
- **AGAINST THE BOARD: 3.59% → 3.54% (X4) and 3.30% → 3.25% (X3)**, 13,780 → 13,582
  and 13,782 → 13,584 differing pixels — threshold-at-128 over the bare `--export`
  panels, since the sheet still prints `ok` rather than a percentage (#41). Small and
  in the right direction, which is what a change should measure when **both** sides move
  to the same content: the residual is the same whole-pixel-versus-subpixel disagreement
  on one line of text. **The instrument is pinned by its control**: `library` reads
  **3.85% / 3.54%** in both trees, 14,800 and 14,801 pixels either side, which
  reproduces this file's own recorded figure for that screen to the digit. And the
  change is confined per row on both sides — the design panel differs only in rows
  **282–298** and the firmware panel only in **283–297**, columns 153–321 and 153–320,
  with **0 pixels differing** anywhere else on either.
- **WHAT ONLY THE PANEL CAN SAY: whether an unshouted name reads right there.** The
  string is drawn as the book wrote it, which is what the Reader's band, Contents' rows
  and Book details' `Current chapter` all do — so mixed case is the consistent answer —
  but it makes this line quieter than the caps runs around it (`NOW READING`,
  `CONTINUE`, `LIBRARY`), and no golden can judge that on glass.

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
- **A backward turn rewinds and decodes forward** to the recorded cursor — **~390 ms
  on the device**, not the 33.9 ms desktop figure this line used to quote against a
  ~520 ms refresh. Buffers are reused, so it allocates nothing — churning 32 KB per
  turn is how a heap with 142 KB free becomes one that cannot serve the next chapter.

**AND THE COST IS PROPORTIONAL TO THE PAGE INDEX, which is what the first fix
missed.** A rewind decodes pages 0..p, so it costs what page you are ON, not what
page you are going to. The device showed it plainly once a real saved position was
restored: at page 38 a backward turn was ~376 ms, and at **page 99 of the same
chapter it was ~1010 ms**. Two consequences, and both are fixed:

- **A RESTORE WALKED THE CHAPTER TWICE.** `openAtCursor` counted boundaries with
  `countOnly()` to find which page holds the cursor, then handed that page index to
  `seekTo()`, which rewound and walked the whole prefix AGAIN to lay one page out.
  On the device that was `post=2269ms` on CONTINUE against ~1010 ms for one walk.
  It is one walk now, with the lines kept: counting mode saves the LINE BUILDING of
  every page it passes, ~15% of a walk, and it was buying that 15% at the price of a
  second whole walk. The builder is left live one page past the target, which is
  `seekTo`'s own postcondition reached once instead of twice.
- **A REWIND NOW KEEPS WHAT IT PASSES.** `seekTo` stops skipping `kPageCacheDepth`
  pages early and caches each one it takes. The inflate, the parse and the wrap for
  those pages are already paid — only the line building was being skipped — so one
  rewind serves a whole ring's worth of backward turns instead of one. Sustained
  backward reading goes from a rewind per page to a rewind per `kPageCacheDepth`.

**A TEST THAT WALKS BACK ONLY `kPageCacheDepth` PAGES CANNOT SEE THAT CHANGE**, and
was written that way first. Reading forward already seeds the ring with the last
`kPageCacheDepth` pages, so such a walk is served entirely by what the forward pass
left behind — the mutation passed. It walks back **twice** the depth now. Same
lesson as the no-op mutation recorded under **Goldens**: check the mutation lands
before believing what it tells you.

**AND A SKIPPING BUILDER MUST NOT CHARGE THE PAGE IT HAS NOT BEGUN — THIS IS THE
DOUBLED PAGE.** Reported off the device as "I turn the page and the same page comes
back with a different number", and reproduced over **8 of the 16 books in one real
library**. `PageBuilder::drain` charges a block's blank rows to `row_` before its
first line, and did so **while `skipping_`** — while the builder is still discarding
everything before the cursor `startAt` gave it. `row_` is how full the page BEING
BUILT is, and there is no page being built there: the skip's own exit zeroes it. So
those rows are charged to a page about to be thrown away, and they ACCUMULATE,
because nothing resets `row_` until the skip ends.

Once `row_` reaches `rows_`, `ready()` — which is exactly `row_ >= rows_` — claims a
page the builder has not begun. `seekTo` loops on that, takes an **empty** page, and
spends an index slot on it: the page that truly began at the target is then cached
under the NEXT page's cursor and handed back as the next page, and `starts_` ends up
holding **two consecutive pages with the same start cursor**. That is the doubled
page, and the index is where it is visible.

- **GUARDING `ready()` IS THE WRONG FIX AND WEDGES THE BUILDER**, which is how it was
  settled which of the two is the cause. The lay loop is gated on `row_ < rows_`, so a
  full `row_` means the skip can never REACH its exit — **the empty take was the only
  thing un-sticking it.** The fix is one word on the increment; the decrement stays
  outside the guard, so exactly what was consumed before is consumed now and no page
  boundary moves.
- **ALL THREE `startAt` CALLERS HAD IT** — `seekTo`, `rewalkToCurrentPage` and
  `layoutPage`, the last of which could return a **blank** page for a start cursor
  deep in a document. One line in the primitive, which is this file's own rule.
- **NO FIXTURE IN THE SUITE COULD REACH IT**, and that is the part worth keeping.
  `blankRowsBefore` charges a row only when a heading, a blockquote or a list
  boundary is crossed, and `readerfix::longChapter` is paragraphs all the way down —
  so `row_` never climbed. **A stream of one block kind is not a chapter**, and every
  paging fixture here was one. The regression test alternates kinds and says so.
- **AND A FORWARD WALK ALONE NEVER SEES IT.** It needs the builder to be null, which
  on a device means an idle walk was INTERRUPTED — the common case while reading, and
  the one a desktop probe turning pages back to back never produces.
  `tools/paging_probe.cpp` is what reproduced it: `--whole` crosses chapters,
  `--idle` runs the three quiet-window walks in the shell's own order, `--interrupt N`
  answers their stop predicate, and `--only` names which of the three is to blame.

**SO THERE IS A RING OF LAID-OUT PAGES, DEPTH 3**, and turning back to the page you
just left now decodes nothing at all: 5,858 → **8.3 µs** desktop for a backward turn,
and a Prev,Prev,Next,Next burst 4,696 → **22.6 µs with zero decodes**.

- **Keyed on the chapter plus the page's start `Cursor`**, NOT its index. An index is
  a position in a list that grows as the chapter is read and is replaced outright by
  a count, so it names a different page before and after one.
- **Pages are COPIES.** `LaidLine::text` is owned exactly so a Page can outlive the
  blocks it was laid from; a ring of views would resurrect the notdef-box lifetime
  bug recorded under **The lifetime rules that changed**.
- **A HIT LEAVES NO LIVE BUILDER, and that is the sharp edge.** `Gesture::Next` tries
  the ring first — the page ahead is exactly the one a reader who came back is
  returning to — and otherwise decodes with `needStream`. Taking a hit where a stream
  was needed is not slow, it is WRONG: `advance()` answers false, and the screen reads
  that as the end of the chapter and turns to the next one from the middle of this
  one. Dropping `needStream` fails seven tests, two of them pre-existing paging
  properties.
- **A live builder still beats the ring** — ~20 ms against ~376 ms to re-establish
  one — so the whole branch sits under a null check.
- **1,471 B a page on the X4 and 1,512 on the X3**, measured; the ring is 4,536 B at
  the default depth of 3, 10.8% of the 42,152-byte floor, with a test asserting the
  ceiling so it cannot drift.

**AND THE REWIND THAT REMAINS HAPPENS WHILE THE USER IS READING.** A deeper ring
alone only postpones the slow turn -- it cannot remove it, because holding a whole
chapter is 315 pages at ~1.5 KB. What removes it is doing the rewind in a quiet
window: `warmPageRing` walks to the page already on screen, caches the depth's worth
of pages ending there, and leaves the builder live exactly where it found it.
**`page_` and `at_` are untouched on every path**, so nothing visible changes -- that
is the property the other three tests rest on and it is asserted first.

- **Gated on headroom, not on a timer.** Reading FORWARD already fills the ring, so a
  warm straight after it correctly finds nothing to do; without that gate it would
  pay a full rewind every quiet window to cache pages it already holds. The first
  version of the test tripped over exactly this and had to spend the headroom first.
- **The depth is the SHELL's**, sized from `ESP.getFreeHeap()` at each warm, because
  the floor moves by 34 KB on nothing but which button opened the book: through the
  Library there are 203 books resident underneath at ~59 KB and **42,152 bytes**
  free; through Home's CONTINUE the same book leaves **76,476**. A constant has to be
  sized for the first and then wastes the second. An eighth of what is free, and the
  default of 3 is the floor, so a heap under pressure keeps the shipped behaviour.
- **Abandoning costs the next FORWARD turn**, because the warm spends the live
  builder and cannot rebuild it -- the identical trade `completeIndex` makes, and why
  **this one still waits for the refinement's window** where the count no longer does.
  The asymmetry is the whole reason there are now three constants and not one: a warm
  is reached with a stream STANDING, so an interrupted warm loses it; a restream is
  reached only with `pb_` already null, so an interrupted restream loses nothing.
  `kRestreamQuietMs` is 1200 ms for exactly that reason and `kRefineQuietMs` stays
  5000.
- **`restreamAtCurrentPage` IS THIS WALK WITH A DIFFERENT GATE**, sharing the private
  `rewalkToCurrentPage` rather than copying it -- the second copy is the extraction
  point, and a builder installed one page off is a reader that skips or repeats a
  page, which two copies would each have to be tested for separately. The shell runs
  the restream FIRST: a landed restream leaves the headroom a warm would have left,
  so the pair costs one rewind rather than two and the warm below correctly finds
  nothing to do. The gates differ in one more place -- a warm refuses page 0 (nothing
  behind it to cache) and a restream accepts it (the cheapest walk there is).
- **It is LOGGED although nothing is visible**, precisely because nothing is: an idle
  optimisation that silently stops working looks exactly like one that is working.
  `[warm] ready|abandoned depth=N headroom A->B` is what tells them apart.
- **WHAT IT STILL DOES NOT FIX**, and the honest limit: skimming backward faster than
  the warm can run -- a turn every ~500 ms against a rewind of one to three seconds --
  still meets a slow turn, and the rewind is proportional to the page index, so it is
  worse deep in a chapter, which is the opposite of what it feels like.

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

**AN ENTITY IT CANNOT DECODE IS TEXT, NOT AN ERROR, AND THAT REVERSED A WRITTEN RULE.**
`decodeEntity` knew the five XML built-ins and numeric references, and errored on
everything else — on the stated grounds that a literal `&nbsp;` in a paragraph "reads as
a rendering bug and is really a parsing one". The first half of that was right and is now
answered by a **generated 252-name HTML 4 table** (`tools/entities.py` →
`core/src/entity_table.h`, from Python's own `html.entities`). The second half was
measured and was false, because **erroring never reported anything**: `document.cpp` stops
on `Node::Error`, `ChapterReader::next()` then returns false, and that is
indistinguishable from the chapter ending. `Dark Plagueis` lost **177 of its 183
chapters** that way — 3,214 bytes of a three-megabyte novel — and read as a book that
opens and is empty.

- **Every exit that is not a decoded character is now text.** Three ways to fail — the
  reference never terminates (`Tom & Jerry`), the name is in no table, the numeric form
  does not parse (`&#zz;`) — and all three emit the bytes the document held. The only
  remaining `false` is "the output buffer cannot hold them", which both callers make
  unreachable by reserving `kMaxEntityBytes + 2` where they reserved 4.
- **A visible wrong beats an invisible one**, which is the call `css.h` already makes for
  over-matched italics. A stray `&unknown;` on the page is a typographic error a reader
  can see and report; a discarded chapter is not.
- **Seven names carry it**: `rsquo` `nbsp` `mdash` `ndash` `ldquo` `rdquo` `lsquo`, every
  distinct named entity across sixteen real books, 50,245 occurrences. The table is all
  252 because the set costs 1,416 bytes of names and typing a subset invites a second
  pass.
- **Two tests in other files pinned the old rule** and had to change with it —
  `test_document.cpp`'s only malformed-markup case *was* `&nbsp;`, and `test_xml.cpp`
  listed `&#xZZ;` among inputs that must error. A rule stated in one place is enforced in
  three.
- **CONFIRMED ON GLASS (2026-08-28)**: `Dark Plagueis` opens and reads on the X3. Both
  this and the identifier fix were desktop-measured first, and desktop evidence has been
  wrong about this panel three times — so the distinction is worth keeping: everything
  above is now a fact about the device, not about the simulator.

**`xml.h` DOES NOT VALIDATE NESTING, ON PURPOSE.** `<p>unclosed` tokenizes without
complaint. The document builder keeps a stack to know which block it is in, so it
notices an unclosed tag at `Eof` for free; a second stack in the parser would be a
second depth cap and a second allocation for a check the layer above cannot skip.

**A REAL BOOK'S ITALICS ARE IN ITS STYLESHEET, NOT IN ITS TAGS.** `document.cpp`
reads `<em>`, `<i>` and `<cite>`, and one chapter of the user's own `Le Fléau`
carries **609 classed inline tags and not one of the three** — its italics are
`<span class="...">` against a publisher sheet, which is what a converted EPUB
usually emits. So without `css.h` most books on a real card rendered no italics at
all, silently, and it read as a regression when nothing had regressed.

- **`collectItalicClasses` IS NOT A CSS ENGINE AND MUST NOT BECOME ONE.** It answers
  one question — which class names carry `font-style: italic` — in a forward scan
  with no tree, no cascade and no specificity. The OUTPUT is a handful of short
  names; that bound is what makes it affordable.
- **IT OVER-MATCHES ON PURPOSE, three ways**: a later rule turning italic back off is
  not modelled, `@media` bodies are scanned like any other, and only classes are
  collected (an element selector would italicise a whole chapter). Over-matching sets
  a run in italic that should be roman, which is a typographic wrong you can see;
  under-matching is invisible, and invisible is what this was.
- **`font-family: "Italic Garamond"` MENTIONS BOTH WORDS AND ASKS FOR NEITHER**, so
  the value has to follow the property through its colon rather than being two
  independent searches. That has its own test.
- **THE CLOSE IS MATCHED BY THE ELEMENT, NOT BY ITS NAME.** A class-italic run ends
  at a `</span>` indistinguishable from every other, so `TagName` carries an
  `openedEmphasis` flag set at the start tag. The tag-based path rides the same flag
  rather than re-testing the name, so the two cannot disagree — and it made that path
  stricter, since an `</em>` whose `<em>` was inside a suppressed element no longer
  decrements a depth it never incremented.
- **IT RIDES `loadToc`'s ARCHIVE OPEN.** Both are "what the book says about itself",
  both are wanted at the same moment, and a second open is ~100 ms and a second
  central-directory parse. The read is capped at 32 KB a sheet — sized against the
  ~133 KB free at book open, not against what CSS can be — and a sheet that will not
  read is skipped rather than fatal.
- **`[css] N italic class(es)` and `[markup] em=N classed=N sample='…'`** are what
  told these apart from the device. A word that should be italic and is not has three
  explanations that look identical on glass: the parse found nothing it reads, the
  wrap lost it, or no italic face is installed. `[page] … emph=N ital=N` separates the
  last two.

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

| ppem | 16 | 24 | 29 | 32 | 36 | 41 | 45 | 48 | 64 |
|---|---|---|---|---|---|---|---|---|---|
| bytes | 3,728 | 7,292 | 10,378 | **12,292** | 15,359 | 19,284 | 23,046 | 25,854 | 44,866 |

Bytes go as ppem², so the old 8 KB held the set at **no** reading size, and 16 KB
holds ppem 32 with 25% spare and **ppem 41 not at all**.

**THE BUDGET IS NOW STATED AT ppem 32 AND DERIVED EVERYWHERE ELSE**, which is what
unblocks the Typography `Size` row. This section used to end "a body-size setting must
revisit this — the budget is a constructor argument precisely so the caller can size it
from the chosen ppem", and that was a deferral rather than a mechanism: the two callers
are `static` globals in `shell/src/main.cpp` built **before `setup()` runs**, so the
chosen ppem does not exist at the moment the constructor is called and no caller could
have obeyed it. `init()` is where the size arrives, so `init()` is where the arena is
sized — `ScalableFont::cacheBytesFor(ppem, budget)`.

- **The curve is `u(p) = 39p² + 300p`, and the linear term is not noise.** A glyph's
  row stride rounds up to a whole byte, which is a cost per GLYPH-ROW rather than per
  pixel, so it scales with the height and not the area. Fitted to the table above it is
  good to 1.5% everywhere; ppem² alone is 9% out at 48, in the expensive direction.
- **It scales the caller's budget rather than replacing it**, so the *margin* is the
  caller's decision, stated once. At ppem 32 `cacheBytesFor` returns exactly 16,384 —
  the shipped number to the byte, which is what makes this change invisible to every
  golden and every board measurement.
- **The ceiling is RELATIVE (150%), because the device has TWO of these faces.** An
  absolute cap cannot keep the roman's 16 KB and the italic's measured-cold 10 KB in
  proportion. Roman → 24,576 B, italic → 15,360 B, so the pair's worst case is 39,936
  against today's 26,624: **+13,312 B, and only at the top of the ramp**. Against the
  42,152-byte floor (a book opened through the Library) that leaves ~28.8 KB, and
  against 45,840 (through Home's CONTINUE) ~32.5 KB. Below ppem 32 it gives memory
  *back* — the pair is 22,303 B at ppem 29.
- **Thrash-free to ppem ~46**, which is 22pt at 150 DPI. Past it the arena stops
  holding the union and the cache does what it is built to do: wrap and re-rasterise.
- **`wraps` is the instrument, not a timing.** At ppem 41 with the old flat 16 KB, a
  second pass over the alphabet took **2 cache hits out of 127** and re-rasterised the
  other 125, at ~3,794 µs a glyph on the panel. `test_scalablefont.cpp` asserts the
  second pass rasterises *nothing*, at every step of the ramp.
- **A grow that cannot be allocated keeps the arena it had.** The new block is taken
  before the old one is released, so a failed `new` is slower and never dead — and the
  only thing that re-inits at a new size is a Settings screen with no book open, where
  the heap is ~133 KB rather than the reading floor.

**AND THE PAIR KERN CACHE IS THE OTHER HALF, WHICH TURNED OUT TO BE THE BIGGER ONE.**
`stbtt_GetGlyphKernAdvance` bisects the face's 6,064-pair legacy `kern` table, and
`wrapProseLead` grows every line greedily and re-measures each candidate — so the same
pairs are bisected over and over. Measured on a 56-page pagination walk (desktop, -O3,
best of 20, three runs):

| | ms | µs/page |
|---|--:|--:|
| before | 31.7–32.0 | 566–571 |
| **512-slot pair cache** | **13.8–14.0** | **247–249** |
| `kerning()` removed entirely (the ceiling) | 8.6–8.7 | 153–156 |

So the bisection was **73% of a pagination walk** and the cache recovers **77%** of
what removing kerning altogether would. 512 slots is the knee of a sweep — 128:18.0,
256:14.5, 512:13.5, 1024:13.7 ms — and 1024 is *worse*, because a chapter's pair
alphabet is a few hundred, not a few thousand. 1,536 B a face.

- **It caches its ZEROES, and that is most of the value.** Only **6.5%** of Latin-1
  pairs kern at all once scaled and rounded to whole pixels, and real prose kerns
  **9.7%** of its adjacent pairs — so ~90% of the bisections were finding nothing and
  being repeated.
- **A kern is in PIXELS, so `init()` drops it**, exactly as it drops the advance cache.
  A cached kern outliving its ppem is text that is uniformly, subtly mis-spaced with no
  glyph wrong — invisible to every golden.
- **IT DOES NOTHING FOR THE RENDER**, measured: `reader_sim reader --bench 200` is
  118–128 µs/pass either way. `drawText` walks a string once, so there is nothing to
  amortise. This is a layout win and it should not be quoted as a page-turn win.

**AND THE FIRST RUN OF A FRESHLY BUILT BINARY IS STILL THE SLOWEST BY A WIDE MARGIN** —
the render bench above read 209.6 µs on its first invocation and 118.7 on its third.
This file already records that trap; it reappeared inside the measurement taken to
check the trap had not been fallen into.

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
  survives — FreeRTOS's own high-water technique. It reports **7,348 bytes** under
  clang and asserts a 10 KB ceiling there, so a vendored-library bump that grows the appetite fails
  on the desktop rather than panicking the device. **THE CEILING IS PER HOST
  COMPILER AND CANNOT BE ONE NUMBER** (`test/unit/stack_ceiling.h`): the same
  chain measures **12,212** under x86-64 gcc, and the streaming decoder 3,072
  against 6,824, so the clang-calibrated ceilings failed the first Linux CI run
  with nothing regressed. Raising one number to cover both was refused — it would
  need clang's appetite to **more than double** before tripping, and clang is
  where nearly all work here happens. **Neither host figure is the device's**:
  the device is gcc-shaped but 32-bit, and its real number is the `[stack]`
  serial line.

The `[stack]` serial line reports `uxTaskGetStackHighWaterMark` after an open — the
worst case since boot, inflate included.

### Memory, which is what a real book runs into

**`new` ABORTS under `-fno-exceptions`, with no message and no stack.** The reboot
looks like a navigation bug — **three times now** it has been reported that way, twice
as "opening a book goes back to Home" and once as a book that **crashed the firmware
on the first press and opened normally on the second**. `MCAUSE 0x2` plus `abort() was
called` plus `addr2line` on the stack words is how you get from that to
`operator new` → `std::bad_alloc` → `__terminate`.

**THIS PARAGRAPH SAID "every sizeable allocation in the EPUB path is
`std::nothrow`-checked and answers with a reason", AND IT WAS TRUE OF EVERY
HAND-ROLLED BUFFER AND FALSE OF EVERY CONTAINER.** There is no nothrow spelling of
`reserve` or `push_back`, and the open path grows five of them from numbers a **FILE**
states — a zip's entry count, a manifest's length, a spine's length, an NCX's entry
count, a stylesheet's size. So the sentence covered the allocations somebody had
written a `Buf` for and silently exempted the ones the standard library makes, which
is the class the third report was. **A comment that overclaims is this project's most
expensive recurring defect** and this is the fourth instance of it recorded here.

**MEASURED, NOT GREPPED**, by replacing global `operator new` and running the real
`openBook` → `loadToc` → chapter walk over all 225 corpus books. Largest **single
contiguous request** per site, which is the number that decides — see "the largest
free BLOCK decides" below:

| bytes | site | who guards it |
|--:|---|---|
| 64,080 | `Epub::open` → the OPF string | `Zip::read`'s probe, since 3A |
| 39,610 | `Zip::open` → the central directory | nothrow `Buf`, since 3A |
| 36,956 | `Inflater::begin` → the window | nothrow, since 3C |
| 32,768 | `loadToc` → `vector<TocEntry>` | `pushOrRefuse` |
| 24,576 | `Epub::open` → the manifest vector | `pushOrRefuse` |
| 17,920 | `Zip::open` → `entries_.reserve(claimed)` | `ensureRoom` |
| 16,640 | `readItalicClasses` → the stylesheet | `appendOrRefuse` |
| 15,408 | `Epub::open` → `chapters_.reserve` | `ensureRoom` |
| 12,288 | `Epub::open` → the spine vector | `pushOrRefuse` |
| 8,194 | `BlockReader::next` → `Block::text` | nothrow probe + `reserve`, **#90** |
| 5,136 | `openBook` → `out.chapters.reserve` | `ensureRoom` |

**AND THE PHASE THAT PEAKS IS `loadToc`, NOT THE CHAPTER WALK**, which is where every
one of those unguarded sites lived. Across the user's own 16 books the toc-and-styles
phase peaks at **47.6–91.8 KB** against a flat **~48–51 KB** for a chapter walk of the
book's longest chapter — so the expensive moment of a book open is the one that reads
what the book says about itself, and the reader's own 36,956-byte window is the
cheaper half. (Desktop figures. The device's cover work measured **17–25 KB above**
its desktop twin for the same allocations, because the allocator is simply different,
so treat these as a floor.)

**`reader/heapguard.h` IS `Zip`'s OWN PROBE, MOVED BEFORE A FOURTH COPY OF IT WAS
WRITTEN.** `canAllocate` had lived in that file's anonymous namespace since 3A with a
comment saying it was "a poor substitute for an interface that could report failure";
`Heap::hasBlock` is the same five lines, plus `ensureRoom` / `pushOrRefuse` /
`appendOrRefuse` over it. Four things about it:

- **It asks for a BLOCK and never a total**, and it asks while the container's OLD
  buffer is still held — which is exactly the state a reallocation is in.
  `getFreeHeap()` answers the wrong question, as this file already says two bullets
  down.
- **It grows GEOMETRICALLY and falls back to the exact size when a doubling is
  refused.** Reserving what was asked for each time would make a 32 KB stylesheet read
  64 reallocations; doubling asks for twice what is needed, so near the limit it would
  refuse a book that fits. **The fallback is the half a mutation catches and nothing
  else does** — the corpus never comes near the ceiling.
- **Failure is INJECTED for the tests**, because the desktop cannot be made to fail an
  8 KB allocation: `Heap::install` swaps the allocator question the way `Profile`
  installs a clock `core/` must not acquire for itself.
- **`test_heapguard.cpp` IS A PROPERTY AND A SITE SET, AND IT NEEDED BOTH.** The
  property refuses every probe the open path makes, from the Nth onward, and demands a
  refusal whose reason maps to `BookErrorReason::OutOfMemory`. That alone **cannot see
  a REMOVED guard** — a deleted guard makes no probe, so the walk has one fewer element
  and every remaining one still passes; deleting the entry-list guard passed all 1,403
  cases. So each site's own words are asserted too. Per-site **words** rather than a
  probe count, because a count is a fact about the standard library's growth ladder and
  libc++ and libstdc++ double from different capacities.

**WHAT IS DELIBERATELY LEFT UNGUARDED, stated rather than implied:**

- **`Block::text` WAS the largest unguarded allocation here at 98,304 bytes, and on
  this tree it is neither.** That figure was measured against a `kMaxBlockBytes` of
  64 KB, which #90 has since derived down to **8 KB** and reserved once through a
  nothrow probe — so the request is **8,194** and it refuses rather than aborting. The
  two changes were written on separate branches and neither touched the other's file,
  which is why the table above needed correcting on the merge rather than either half
  being wrong. **The 98,304 is kept as the before figure**, because it is what #90
  removed and it is the largest single number this path has ever asked for.
- **Everything under ~2 KB**: `cssPaths_` at 8 entries, italic class names at 64,
  `Epub::Chapter`'s two path strings, `Block::emphasis` at 256 `Span`s. They are
  bounded and small, and a guard on each buys a branch rather than a refusal.
- The nothrow sites above are **not** re-guarded. They already refuse.

**TWO FALSE CLAIMS WENT WITH IT, BOTH PRE-EXISTING.** `Epub::open` collapsed "the
entry is absent" and "the entry would not read" into one message, so an out-of-memory
inside the container read arrived as *"this is not an EPUB"* — and would have reached
the panel as `appears damaged`, about a book that is fine. And `Zip::read`'s own
refusals said **"chapter"**, when its one caller is `Epub::readEntry` reading a
container and an OPF, so the noun was wrong at every site it can fire from.

**AND THE `book.cpp` HALF OF THIS SENTENCE HAD BEEN FALSE SINCE 3C.** It read "a
pre-flight probe in `book.cpp` before `buildDocument` (whose `std::string`/`std::vector`
growth cannot fail politely — that one is a bound, not a guarantee)". `openBook` has not
called `buildDocument` since it stopped returning a chapter's blocks and started
returning the spine's geometry; there is no probe in `book.cpp` and there is nothing
there for one to guard. The **claim** it was making survived the move, though, and it
moved down a layer with the work: the growth that cannot fail politely is
`Block::text`'s `push_back`, and *"a bound, not a guarantee"* was exactly right about it
— **a bound of 64 KB against a 42,152-byte floor, which is a bound that cannot be
honoured.** #90 made it a guarantee: the buffer is reserved once through a nothrow
probe, so the growth cannot allocate at all, and the cap is derived from the floor. See
**The lifetime rules that changed**.

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

**AND THE ONE THING IT WOULD OBVIOUSLY BUY, IT DOES NOT BUY: A NAV DOCUMENT NAMES 0 OF
THE 133 SPINE ENTRIES THE NCX SKIPS.** Measured over the whole corpus when a reader
reported the conclusion of `Digital Minimalism` as unreachable — both tables are emitted
by one generator from one source, so a publisher's contents that misses an entry misses
it in **both**. This does not retire the later job (a book with a nav and no NCX is a
real shape), but it removes the reason somebody would reach for it first. Also measured
against the two other places a name could come from: the chapter's own `<h1>`–`<h6>`
names **35 of 133 (26.3%)**, and `<title>` is present for **96.2%** and is not a chapter
name — the reporting book's reads `Continued, Digital Minimalism`. **The name is not
recoverable**, so `fillTocGaps` gives the row a POSITION rather than chasing one.

**A SPINE ENTRY NO ENTRY NAMES IS READABLE BY PAGING AND REACHABLE BY NOTHING ELSE, AND
`fillTocGaps` CLOSES THAT.** The list is what the NCX names; what a reader can be IN is a
spine entry, and where those differ Contents cannot offer the section at all — so the
only way back to it is to remember which chapter it follows and page through. Reported
off `Digital Minimalism`, whose publisher styled the Conclusion's title as a `<p>` where
every real chapter uses an `<h2>`: their generator walks headings, the chapter lost its
navPoint, and the NCX runs `… spine 14, spine 16 …`. The reader sat in the conclusion of
the book with no row marked `NOW`, **the cursor thrown to `Cover`**, and no way back.

- **THE BOUND IS POSITIONAL AND THAT WAS A MEASUREMENT, NOT A TASTE.** A gap is a spine
  entry no row names lying strictly between the first and last the book DID name. That
  is what separates a missing chapter from front and back matter — this book's spine
  carries 15 footnote files and a `next-reads.xhtml` after its last named entry, so an
  unbounded fill adds **16 rows of noise to reach the one chapter that matters**.
  **A SIZE FLOOR WAS MEASURED AND REFUSED**: text length separates cleanly (junk tops out
  at 1,976 characters, real chapters start at 4,510) and is unknowable without decoding
  every gap at book-open; the archive's UNCOMPRESSED SIZE is free and does **not**
  separate — junk reaches 5,210 bytes where a real chapter starts at 6,187 — so any free
  floor either keeps junk or drops a chapter. The stated cost of having none is a couple
  of front-matter rows on a minority of books.
- **ONLY THE LOWER HALF OF THE BOUND IS WRITTEN DOWN.** The upper half is structural: the
  walk emits a gap only in front of an entry that already exists, so it cannot reach past
  the last one. A `next < hi` term read as load-bearing and was implied by the loop's own
  `next < e.spine` — **caught by a mutation that removed it and failed nothing**, which is
  this file's own rule about a branch no test exercises.
- **A SYNTHESISED ROW TAKES THE FOLLOWING ENTRY'S DEPTH, WHICH MAKES IT STRUCTURALLY
  INCAPABLE OF BECOMING A HEADER.** `isHeaderAt` is "the next entry sits deeper than this
  one", and equal depths are not — so the row can never be drawn as a tracked-caps label
  the focus skips, which would be this defect reintroduced by its own fix. **The first
  test of it could not tell the two candidate rules apart**: its fixture gave the gap
  neighbours at equal depths, so taking the PRECEDING entry's depth passed all 1,359,370
  assertions. A part divider followed by its first chapter is the shape that separates
  them. *A mutation tells you about your INPUT before it tells you about your test*, for
  the fourth time in this file.
- **THE LABEL IS `chapterPositionLabel`, ONE FUNCTION AND TWO CALLERS.**
  `ReaderScreen::updateChapterLabel` has composed `CH. %02d` since the header band stopped
  being a spine position; the row now carries the same string, so the list, the band, the
  sleep card and Home say one thing about a chapter none of them can name. Two copies of a
  format string is how those four surfaces drift. The position fallback survives for a
  book with **no** contents at all, which is the only case left that can reach it.
- **WHAT IT COSTS ON REAL BOOKS**, through the built pipeline over `~/.cache/encre-corpus`:
  of 206 books with a usable NCX, **29 gain a row, 134 rows in all**, a median of 2 per
  affected book. The largest is the point rather than the price — **`Dune - Tome 3` gains
  35 rows, 33 of them whole chapters of 7,000–20,000 characters**, a novel navigable today
  only by paging. On the corpus's own `local/` shelf **5 of 16** books gain something.
- **A BOOK WITH NO CONTENTS AT ALL IS LEFT ALONE**: there is no named range to bound the
  fill by, so the only available rule would be "every spine entry", which is a different
  feature with a different argument.
- **IT RUNS IN THE SHELL, NOT INSIDE `loadToc`**, because that function's job is to report
  what the book AUTHORED and this adds rows the book did not write — and it needs the
  spine's length, which `openBookAt` has in hand and the archive read does not. Its second
  row list is guarded with `pushOrRefuse` like `loadToc`'s own, and **a refusal leaves the
  contents exactly as the book wrote them**: a partial fill would make which chapters got
  a row depend on where the heap ran out.

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
`content src`. **The linear form keeps the parent/child relation recoverable and that
is now load-bearing**: children immediately follow their parent, so "does this entry
group others" is `entries[i + 1].depth > entries[i].depth` — which is what #75's fix
asks, and what a flattened list could not have answered.

**A LOOSE REGEX IS NOT A MEASUREMENT.** This project's habit of measuring before
designing is what caught the nav-document question; the same habit applied carelessly
got the nesting question backwards and wrote the wrong claim into a header. Where the
answer decides a design, parse the thing.

**AND FOUR BOOKS IS NOT A DISTRIBUTION, WHICH IS THE SECOND HALF OF THAT LESSON AND
COST 1,635 ROWS (#75).** The table above is right and it is a SAMPLE, and the design
built on it read a `depth` as a level in a hierarchy: `ContentsScreen` made every
depth-1 entry of a sectioned book a section header. Re-measured by parsing all 225 NCXs
in `~/.cache/encre-corpus` — 19 have no usable NCX, **103 are flat and 103 are
sectioned**, an even split, and **98 of the 103 sectioned ones mix entries that GROUP
others with top-level entries that group nothing**. That second shape is what Standard
Ebooks emits for every book with parts (`Titlepage`, `Imprint`, `Colophon`,
`Uncopyright` sitting at depth 1 beside a real `Part I`), it is **9 of the 9 sectioned
books on the user's own shelf**, and the worst case in the corpus loses **362 rows of
384**. So the childless top-level entry is not a tail case; it is the common case, and
the four-book sample happened to contain none of it. **The fix is in
`screen_contents.h`** — a header is an entry that groups others, one lookahead in a list
already walked in document order — and the reachability rule now lives there rather than
being inferred from a depth here.

**A DEPTH IS A NESTING LEVEL, NOT A ROLE.** `toc.h` reports what the NCX authored;
what a level MEANS on a screen is the screen's decision, and the two were conflated for
two phases. This layer is deliberately unchanged by that fix: `TocEntry::depth` is still
the navPoint nesting depth, and nothing here needs to know which entries a screen will
draw as headers.

**COMMITTING AN ENTRY HAPPENS AT TWO MOMENTS**, and only handling one lost every
parent: a `navPoint` is complete when it closes AND when a CHILD opens, because the
child's start clears the label the parent had already read. A test caught it. State is
cleared after each commit, so a parent's close adds nothing — verified by deleting the
duplicate rule and confirming the nested case still passes, since it used to be correct
only by accident of that rule.

**AN IDENTICAL ROW TWICE IS NOISE; A DIFFERENT NAME FOR ONE TARGET IS CONTENT.** Real
books produce both, and only the PREVIOUS entry is compared — an NCX is authored in
reading order (0 out-of-order entries across all four measured), so a repeat is adjacent
and a full scan would be quadratic for a case that cannot happen far apart. **That
premise is also what makes #75's lookahead sound**: an entry's children are the entries
immediately after it, so a document-order list carries the hierarchy without a tree.

**THE LIMITATION WORTH KNOWING:** an NCX target is a file plus an optional fragment
(`ch3.xhtml#part2`) and the reader positions by spine entry only, so several entries
pointing into one file all land at that file's start. They are kept rather than
merged — their labels are real content — but selecting one is approximate. That is why
Le Fléau has 96 entries for 92 spine entries.

**AND THAT LIMITATION IS THE MAJORITY CASE, WHICH IS HOW IT MADE `NOW` A FALSE CLAIM.**
Reported off an X3 on `Discourse on the Method`: **two rows** read `NOW` —
`DISCOURSE ON THE METHOD OF RI…` and `Contents`, whose targets are
`…59-h-0.htm.xhtml#pgepubid00000` and `#pgepubid00001`, both resolving to spine entry
1. `renderContents`' source asked `!row.isHeader && e.spine == spine_` **per row**, so
every entry naming the open spine entry got the marker. `NOW` is a claim about where
the reader is, so more than one of them is the false-claim shape this file refuses for
an unread gauge (`-1`, never `0%`) and for a badge promising a wake charging cannot
deliver.

- **MEASURED OVER `~/.cache/encre-corpus`, and the limitation above under-sells its own
  incidence: 109 of the 206 books with a usable NCX (52.9%) have at least one spine
  entry named twice or more** — **605** such groups, **4,526** rows that would have read
  `NOW` at once. The worst is `standardebooks/f822606a92670aa1.epub`, whose spine entry
  2 is named by **378** navPoints, **373** of them non-headers. **One of the affected
  books is on the user's own shelf** (`local/6eff4fa621681282.epub`, 44 on one spine
  entry), so two rows is the mild version.
- **THE ROW CHOSEN IS THE FIRST OF THE GROUP, AND `tocIndexForSpine` ANSWERED THE LAST
  FOR TWO PHASES.** Its argument — "the later ones are further into the file, so the
  last is the closest thing to where you are" — is true in its premise and needs the
  reader to be at the **END** of the file. The fragment is **stripped** before the
  match, so every member of a group resolves to that file's **start** and nothing on
  this path knows any offset within it: the first entry is the only one that can be
  *proved* not to be **ahead** of the reader, and `reading_position.h` grades the same
  trade the same way, degrading backwards. It was also wrong at the one moment it is
  asked — `updateChapterLabel` runs when a chapter **opens**, which is its first page on
  a jump and on a forward crossing. So the Reader's header band moved with the marker:
  **one rule, because two screens naming the reader's chapter differently is two
  spellings of one fact.** What it costs is stated rather than hidden — a reader deep
  inside a 378-fragment file is named by that file's first fragment, which is stale
  rather than false, and closing that needs a fragment-to-block map `document.h` cannot
  supply.
- **A HEADER MAY NOT TAKE IT, and that gate is the screen's rather than `toc.h`'s** — a
  depth is a nesting level and not a role (#75), so `ContentsScreen::rowForSpine` is
  `tocIndexForSpine`'s rule plus one lookahead, **pinned to it by an equivalence over
  header-free lists**, which is `test_focus.cpp`'s device for `Focus`'s gated walk.
  **A spine entry named ONLY by headers answers −1 and marks nothing** — a `Part I` with
  a file of its own, **108 spine entries across 62 corpus books** — which is the
  pre-existing behaviour and the honest one.
- **THE TRAP IS THAT `syncVm` WALKS THE VISIBLE SLICE (`s.first + i`), NOT THE LIST.**
  A "first match" computed inside that loop is the first match **on screen**: the marker
  would hop between members of the group as the list scrolled and would appear on a row
  that is not the reader's once the real one scrolled out of the window — strictly worse
  than the defect, and **invisible to any single-screenful test**. It is decided **once,
  in the constructor, over `entries_`**, and compared as an absolute index; `entries_`
  and `spine_` have no setters, so there is nothing to invalidate. Proved by mutation: a
  slice-local rule fails only the scrolling case, 11 assertions, while the
  reported-book case stays green.
- **NO FIXTURE COULD REACH IT, INCLUDING THE BOARD'S OWN.** `sectioned()`,
  `mixedDepths()` and `demoContents()` **do** share spine indices, and in every one of
  those pairs one member is a HEADER, which `!row.isHeader` already suppressed;
  `flat()` gives every row a spine of its own. So the two Contents goldens, the
  comparison sheet and a test literally named *the row being read is the only one marked
  `NOW`* all agreed with a rule that marks every match. Same shape as "a stream of one
  block kind is not a chapter". **What those goldens DO defend is the header gate** —
  dropping it moves the marker onto `BOOK I · MISS BROOKE` and reddens both.
- **`design/Contents.dc.html` NEEDED NO CHANGE**: it draws exactly one `NOW` and its own
  copy says the marker is on "the row being read", singular. So the board was already
  right and the firmware moved toward it. Nothing moved on the sheet either —
  **2.30% / 2.11%, 8,814 differing pixels at both geometries**, which reproduces #81's
  recorded figure to the digit.

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
quantity or discloses a screen, never both". **THAT ROW IS CUT AND THE PARAMETER IS
NOT** — see the trackingEm1000 paragraph below, which is where this went next. With
`Names` cut too (#73) the menu now adds **nothing at all** to the shared primitives.

**IT DECLARES `Mono` WHERE THE READER DECLARES `Grayscale`.** Fidelity comes from the
top screen, so the menu paints in one waveform instead of three and its focus moves are
eligible for the overlay-only partial repaint (grayscale never is). The page under the
veil is hard-thresholded for those frames — the trade, and acceptable because the menu
is chrome and the page is the one thing here that wanted four levels. Its
`paintFootprint` is a constant, unlike the actions panel's: every row is one height, so
the panel cannot change height when the focus moves and every move takes the fast path.
(That sentence counted the rows twice and the count was wrong twice; the property is
"one height", and the number belongs in the test.) **AND THE CLAIM IS FALSE — #68 IS THE
OPEN CARD.** The rows really are one height, but `renderReaderMenu` sizes the panel
through `panelRowHeight(rowRuleFor(i, rows, focused))`, and `rowRuleFor` suppresses the
rule for the focused row **and** for the last row — so focusing the LAST row is the one
state where two suppressions coincide and the centred panel moves a pixel. Measured on
the X3: panel top 213 on Contents and Typography, **212** on About this book. That is
the actions panel's own defect, which `ItemActions::paintFootprint` counts borderless
rows for and this does not. **Cutting `Names` did not touch it**: that row was never
focusable and never last, so it always drew its rule — the cut takes 73px off the panel
in every state and leaves the focusable set, and therefore every per-state delta,
exactly as it was.

**`discloses` CANNOT BE DERIVED FROM AN EMPTY VALUE**, and deriving it drew a chevron on
`Close book` promising a screen that does not exist — that row had neither a value nor a
mark, because it acted in place. So `ListRow` carries the flag explicitly, as
`ItemActionEntry` already did. `Bookmarks` WAS the surviving instance of the same rule
from the other side — a value where its siblings have marks — and with that row cut
(#55) **this sheet states no quantity at all and every row on it discloses**. Both fixes
took the menu from 3.24% to **3.02%** against its board.

**`ListRow::trackingEm1000` NOW HAS NO PRODUCER.** `Close book` was `0.06em` where its
siblings were untracked — 1.5px a gap at Value500, ~15px across that label — and it was
the only letter-spaced row on any panel in this firmware. With the row cut (2026-08-24)
the field, `drawPanelRow`'s `labelTrackingEm1000` and the `trackingEm` call it guards are
**untested capability rather than working behaviour**. Kept because it is a generic
component parameter a board can ask for again; a test asserts every row is `0` so this
stays a stated fact rather than an assumption.

**AND `drawPanelRow`'s VALUE PATH JOINED IT, WITH THE OPPOSITE ANSWER.** Cutting
`Bookmarks` (#55) left the value argument in exactly the shape tracking is already in —
a parameter whose last producer walked out — and the two were settled differently on
purpose. **Tracking is a board's typographic request and its absence is invisible; a
value is a MARK, and a wrong one is a number in the wrong place on the glass.** So the
pixels are pinned at the PRIMITIVE instead of at a screen — `test_components.cpp`, "a
panel row states a quantity or discloses a screen, never both" — which needs no caller
to reach them: the value wins over the chevron, it is right-aligned by MEASURING itself
rather than by a fixed offset, and it inverts with the focus. **Proved by mutation, and
the first version of the alignment case did not bite**: it asserted a wider value's LEFT
edge moved, which passes against a hardcoded offset because `1` and `2` have different
side bearings — it was reading the FACE. Two values ending at the same column is the
assertion that is actually about the placement.

**The parameter stays for the same reason tracking's does** — `Bookmarks.dc.html` is a
board that asks for it again in V1.1 — and `ListRow::value` is untouched either way,
since Settings, Contents and Typography all still state values through their own row
primitives. It is `drawPanelRow`'s argument alone that lost its caller.

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

**EVERY ROW ON THE MENU RESPONDS NOW, AND SETTINGS' RULE HAS NO INSTANCE LEFT HERE.**
The board was edited to match the rule before the screen was written — it had focused
Typography, which was not built then, so implementing it faithfully would have drawn a
selection on a dead row — and the last drawn-and-skipped row was `Names`, which is cut
(#73). `Contents`, `Typography` and `About this book` are all that is left and all three
act. `ReaderMenuScreen::focusable()` and `ListRow::focusable` stay, because the rule is
the screen's and the next unbuilt **V1** row gets it by setting one word.

**AND THAT RULE'S LIMIT HAS NOW BEEN REACHED TWICE — `Bookmarks` (#55/#3) AND `Names`
(#73) — AND THIS PARAGRAPH GOT THE SECOND ONE WRONG WHILE STATING THE TEST FOR IT.**
Skipping the focus stops an unbuilt row misleading a reader who PRESSES it; it does
nothing about the row itself promising a feature the release does not have. The
distinction that decides it is **which release the row is waiting on** — and this file
wrote that sentence down and then applied it to `Names` from memory rather than from the
board: it said "`Names` waits on its own screen inside V1, so it is drawn and skipped".
**The Names family is V2**, three `Boarded` cards (the per-chapter index, the list
screen, and the alias-row overflow), so it was `Bookmarks`' case from the moment those
cards were filed and the row should have gone with it. **A rule and its worked example
drifted apart inside one paragraph**, which is the same shape as the guards that named a
member instead of `ScreenId::Count`: the rule was right, the instance was stale, and
nothing but the board could tell them apart. `Names.dc.html` and `NamesEmpty.dc.html`
stay; the row returns with the screen.

**FOUR ROWS HAVE BEEN CUT ENTIRELY, AND NOT ONE OF THEM FOR ROOM** — two of them on
2026-08-24, then `Bookmarks` and `Names` above. `Go to page…` because
**nobody navigates an EPUB by page number**: a reflowable book has no stable page to go
to and the number a picker offers moves with the type size, so the honest jump is the
chapter name `Contents` already gives. (Its board and its roadmap entry went too; the
`Peek` spec listed it as one of three callers and now has two.) `Close book` because
**Back from the page already closes the book** — it was a second door to a room with
one, and it cost a fourth save edge to stay correct. Removing it deleted that edge, the
`popTo(Library)` it was the only user of on this screen, and the only producer of row
tracking in the firmware. The enum shrank with it: **a row index is not a stable
numbering** here, because the one thing that persists one is `FocusScreen`'s restore,
and that refuses an index it cannot land on — exactly what a shrunk table produces. It
has now shrunk three times on that argument with nothing to migrate any of them.

The menu measured **3.10% / 3.60%** against the board after that cut, against 3.06% /
3.60% before: the panel shrank consistently on both sides, so the residual was the same
rasteriser difference rather than new drift. **That the number barely moved is the
check** — a structural mismatch would have shown as a jump.

**AFTER THE `Bookmarks` CUT IT IS 2.46% / 3.02%**, and that one moved the number rather
than holding it — which is the expected direction and worth saying why: the row that
went was the only one carrying a **right-aligned bold numeral**, and a numeral is where
Chrome's subpixel advances and the firmware's whole-pixel ones disagree most per pixel of
ink. **The sheet still prints `ok` and not a percentage (#41)**, so both figures are a
threshold-at-128 count over the bare panel PNGs `--export` writes. Measured in the same
tree, the untouched `reader` reads 5.24% / 6.29% against the 5.34% / 6.38% recorded
elsewhere here — the same ~0.1pp offset this file already notes for the peek, which is
what makes the before and after comparable rather than two instruments.

**AFTER THE `Names` CUT IT IS 3.00% / 3.56%, AND THAT NUMBER WENT THE WRONG WAY FOR A
REASON THAT IS NOT DRIFT.** The panel lost a 72px row and its 1px rule, so it got
*smaller* and *closer* to nothing — and the strict figure ROSE by 0.54pp on both
geometries. **This is the first time on this project that a threshold-at-128 count has
been read as a regression and been an artefact of the count itself**, and the mechanism
is worth having written down because it will happen to the next row anybody cuts from a
centred panel:

- **The board's panel is a HALF PIXEL out of phase now.** Chrome derives the panel's
  height as `2 * border + caption + rows` and its caption block's content height is
  FRACTIONAL, so the sum is fractional. Removing one 73px row flipped the *parity* of a
  centred panel's top edge: the board's panel used to land on an integer y and now lands
  on a half-integer. Chrome then rasterises every 1px rule inside it across **two rows
  of grey 127**, and 127 is under the threshold, so the count scores **both** as ink
  where the firmware inks exactly one. ~340 spurious mismatches per full-width edge,
  five edges, and the +2,062 (X4) / +2,276 (X3) is accounted for.
- **NOTHING MOVED, and that was checked per band rather than argued.** Every full-width
  edge of the design's panel BRACKETS the firmware's: top border design 252(127) /
  253(0) / 254(127) against firmware 253 / 254; the rule design 472(127) / 473(127)
  against firmware 472; bottom border design 545(128) / 546(0) / 547(127) against
  firmware 545 / 546; and the focused row's black block spans the same 74px, offset by
  half of one. Identical story at 528×792. A rule that had really moved would sit
  *beside* the firmware's, not straddle it.
- **A ±1-ROW-TOLERANT COUNT IS WHAT THE FIGURE WOULD BE WITHOUT THE PHASE**, and it
  moves the way a smaller panel should: **1.66% → 1.76% (X4) and 2.11% → 2.20% (X3)**,
  +0.10pp, consistent across both geometries. That is the same order as every other row
  cut here. It is quoted as a second reading and **not** as a replacement — the
  threshold-at-128 number is this project's instrument and swapping instruments to make
  a figure look better is how a real regression gets hidden.
- **THE FIX IS NOT TO PIN THE PANEL'S HEIGHT ON THE BOARD.** That is what
  `Peek.dc.html` did, and it was right *there* because the peek's box is a fixed
  constant by design; this panel's height is the sum of its rows, which is exactly the
  box model CLAUDE.md's first invariant says to derive from and never pin. The
  fractional part lives in the shared overlay caption that **eight boards** draw, so it
  is a `components.h`-level question and not this screen's — and it is worth a card
  rather than a paragraph.

**THE ROW'S RIGHT SLOT HAS HELD TWO WRONG THINGS.** It was `P. 21`, a page number for a
place in the book, which needs every chapter paginated (~49 s). That became `CH. 01`, the
spine position — free, true, and WORSE on a real book: chapter names carry their own
numbering, so a row read `Chapitre 1.        CH. 09`, two numbering systems side by side
with neither explaining the other. It is `NOW` on the row being read and empty elsewhere:
the NAME is the content of a table of contents, and the full width belongs to it. **On
exactly ONE row** — it marked every entry naming the open spine entry, which is a group
in the majority of real books; see the `NOW` bullets under **The table of contents**.

**HOME'S COUNTER WAS THE SURVIVING INSTANCE OF THE SAME DEFECT AND IT IS CLOSED.** Its
CONTINUE block drew `CH. 14 OF 36` — the identical spine position, with a spine COUNT
beside it, which is the worse form because two numbers invite an arithmetic one does
not. It draws the chapter's name now, from the same `toc.h` label this row's `NOW`
follows; see **Home's CONTINUE block** for the corpus figures. **What made it survive
is worth keeping: this paragraph called the position "free, true, and WORSE on a real
book" and Home's own paragraph called the same quantity "free and true"** — the same
fact, judged in two places, and the sentence that got it right was not the one next to
the code.

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
Contents does on the chapter you are in". The section header's **BOX** is byte identical
on both boards (`--t-meta`, 0.2em/500, `padding: 18px 24px 6px 24px`), so it is
`drawSectionHeader` now rather than a second copy — and it returns the height it ACTUALLY
drew, because a header without its rule is 2px shorter and a caller advancing by the
nominal height puts every row 2px low. Settings shipped that exact bug once.

**ITS RULE IS NOT SHARED, AND THIS PARAGRAPH SAID IT WAS — "a 2px `border-top` except
the first", which is SETTINGS' rule and not this board's (#81).** `Contents.dc.html`
gives **neither** header a `border-top` and neither section-final row a `border-bottom`;
`Settings.dc.html` gives every non-first header one. So `renderContents` drew a row's 1px
rule straight into a 2px header rule — a **3px** full-width line where the board draws
none, **1,440 of 13,274 differing pixels at X4 and 1,584 of 13,514 at X3**, ~11% of the
screen's whole mismatch and the largest contiguous band on it. The same false claim stood
in `drawSectionHeader`'s own doc comment, which is what licensed it: **a comment about a
neighbouring file is not evidence about it**, the shape this file already records for the
`book.cpp` comment describing `Epub::open`.

- **THE BOARD WAS RIGHT AND THE TWO ABSENCES ARE ONE DECISION.** A Settings section is a
  change of SUBJECT and a divider says so; a part of a book is a soft hierarchy over one
  continuous reading sequence, carried by the label's own 18/6 padding and tracked caps.
  **And Contents SCROLLS where Settings does not**, so a header that ruled at all would
  make Settings' positional first-versus-later question something this screen has to
  answer correctly at every scroll offset, for a line it wants nowhere. **WHETHER a
  screen's headers rule at all stays at the call site** — Settings passes `i != 0`,
  Contents passes `false` — and `Contents.dc.html` now carries the reasoning, because
  nothing on it said the absences were deliberate and that is why the render copied
  Settings' shape.
- **THE ROW'S HALF IS `rowRuleFor`'s THIRD TERM NOW, AND IT ARRIVED ONE COPY LATE.**
  `renderSettings` carried it hand-written as `rowRuleFor(...) && !nextIsHeader` and this
  file's own primitive carried the rest of it in **PROSE** ("screens with extra reasons to
  drop a rule … AND this together"). **A rule half in a constexpr and half in a sentence
  is a primitive not yet finished, and the sentence is the half that does not get copied
  to the next caller.** `nextIsHeader` defaults to false, which is the answer *by
  construction* for every headerless list rather than merely convenient; the one-line
  lookahead that answers it stays per-screen, because the two sectioned screens hold
  different row types. Proved by mutation: dropping the term reddens both Contents
  goldens, both mixed-depth goldens **and both Settings goldens**, which is what says the
  extraction moved behaviour rather than leaving a dead parameter.
- **MEASURED, and the gain is bigger than the band** because the 3px also put everything
  below the header out of register with the board: **13,274 → 8,814 (3.46% → 2.30%) at X4
  and 13,514 → 8,814 (3.23% → 2.11%) at X3**, no full-width differing row left on either
  panel. Threshold-at-128 over the bare `--export` panels — the sheet still prints `ok`
  rather than a percentage (#41). **Confined to the band and below, checked rather than
  claimed: above y=505 the count is 6,733 before AND after, at both geometries.**

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

## The return anchor

**The anchor is a HIGH-WATER MARK: the most advanced position the reading position has
reached in this book.** It only ever rises. `Up` returns to it, and the footer's third
field (`design/ReaderAnchored.dc.html`) names it — both gated on the mark being **ahead
of where the reader is standing**, because at the furthest point there is nothing to
promise and no field is drawn. Following it does not clear it: you arrive AT it, so it
stops being ahead and the field withdraws itself, and it reappears the moment you page
away. It is cleared only when the book changes, which is structural — the anchor is a
member of `ReaderScreen` and a screen holds one book.

**IT WAS THREE TRANSITIONS AND THE USER FOUND THE HOLE, WHICH IS MEASURED RATHER THAN
ARGUED.** `pagedForward` / `pagedBackward` / `jumped` implemented "where you were before
you stopped reading linearly", so the anchor was only ever set to a **departure** point.
Take a reader in chapter 1 who jumps to chapter 36: `jumped` set the anchor to chapter 1,
*behind* them, and `pagedForward`'s "arriving at or past the anchor means you have read
back up to it" cleared it on the very first page turn. Running the real class:

```
after forward jump ch1->ch36: set=1 spine=0
after ONE forward page turn:  set=0 spine=0
```

So the anchor never advanced to where the reader was, and a forward jump bought a way
back that survived **exactly one press**. Three rules to produce that. The old header
argued at length that the extra cases were load-bearing — "a jump overwrites
unconditionally, and this is why the rule needs two cases at all" — and it was defending
the one case it could not serve.

**WHAT THE ONE RULE BUYS: there is no movement the screen has to classify**, and
therefore none it can classify wrongly. The three transitions were a taxonomy of
presses, and a taxonomy has to be complete to be correct. Forward page, backward page,
chapter crossing, jump and following the mark all go through `note()`.

**WHAT IT COSTS, and it is written down rather than discovered:** committing a peek
**forward** now leaves no way back — the mark rises to the arrival. The old rule
nominally offered one there, and per the measurement above kept it for one press, so
almost nothing real is lost. A **backward** commit is the case that matters and it works
by construction: nothing lowers the mark, so it stands where the reader was.

**THE RAISE IS ONE CALL, IN `syncVm()`, and that it is the right home was checked rather
than assumed.** Every movement of the reading position in `ReaderScreen` ends in a
`syncVm` — both constructors, `setMetrics`, `relayout`, `walkToChapter`'s landing,
`openChapterAt`'s restore-on-failure, `openAtCursor`, `goToPosition`, `goToAnchor`,
`completeIndex` and each branch of `onGesture` — and the three that look like they might
move one and do not are `warmPageRing`, `restreamAtCurrentPage` and
`rewalkToCurrentPage`, which leave the page and the index exactly as found. The old
shape had four call sites for the jump alone, **one of which was missed for a whole
phase** and computed the footer label before the anchor was set.

**PAGING INSIDE A PEEK DOES NOT RAISE IT**, by construction rather than by a gate: the
peek owns its own headless `ReaderScreen` with its own mark, and the outer Reader's
`syncVm` is not called while a peek is up. Browsing costs nothing and risks nothing,
which is the peek's whole design.

**ONE PREDICATE DRIVES THE FIELD, THE BINDING AND THE SIDECAR.** `aheadOf(here())` is
asked by `syncAnchorLabel`, by `Gesture::AltPrev` and by `saveReadingPosition` — this
project has shipped a dead button twice, both times because two conditions were spelled
separately and drifted. `isSet()` is NOT that question: the mark is raised to wherever
the reader stands, so it is set almost always.

**THE SIDECAR IS UNCHANGED — three ints, no version bump, only their meaning moved.**
`anchorSpine = -1` still means none and `restoreFrom` still drops the anchor below an
`Exact` fit (its `line` is exactly as fragile as the position's, and a mark that lands
the reader on the wrong page is worse than none). An old record's *departure* point
reads back as a high-water mark, and a departure is somewhere the reading position
really was — so it is either ahead of the reader (a working way back) or behind it
(hidden, and raised by the landing's own `note`). Harmless either way. It is written
only while the mark is **ahead**, so a reader at their furthest point produces a record
byte-identical to one from before anchors existed.

**`follow()` IS GONE**, with the clearing it existed to do. What replaced it is a
predicate and a getter — see `core/include/reader/return_anchor.h`.

## The peek

`Peek.dc.html`. Contents shipped **jumping straight to a chapter** — safe when the jump
goes BACKWARD, because the return anchor stays where the reader was; see the section
above for what a FORWARD jump costs — and the peek is the panel of that chapter's text
over the page you are on, with `GO HERE` to commit and `CLOSE` to leave your page
untouched. It answers the one question a list of chapter names cannot: *is this the
chapter I meant*. Chapter selection is the first caller; Bookmarks (#3) and Names are
the second and third.

**IT OWNS A HEADLESS `ReaderScreen`.** The panel is inset, so its column is ~368px
against the reading page's 444 — which is both why it **cannot show a page number**
("page 53" of a re-wrapped column is not page 53 of the book, so the band says chapter
and percent, which are true at any width) and why it **cannot reuse the Reader's
already-laid `page_`**, whose lines were measured against the wider column and would
overflow the panel. So it needs its own pagination, and the two alternatives are both
worse: a bespoke pager is a second copy of open/advance/seek — the three routines this
project has spent the most effort on, each carrying rules a copy would have to re-earn —
and extracting a `ChapterPager` is a large refactor of the most performance-critical
code here for a screen that wants a fraction of it. **What owning a Reader buys is the
one property that matters: the BLOCK the peek commits is by construction the one the
Reader restores.** Both sides are `currentCursor` over the same document, so there is no
second spelling of a *block* free to disagree with the first — which is exactly how a
"go here" lands in the wrong paragraph.

**AND IT SAID `the cursor` THERE, WHICH WAS TRUE OF THE BLOCK AND FALSE OF THE LINE
(#48, closed).** A `Cursor`'s line is a line *within a block at one ppem and one column
width* — `reading_position.h` grades exactly that as `Relaid` and zeroes the field, and
`ReaderScreen::relayout` drops it one layer up for the same reason — so the two sides
were one spelling of a block and **two spellings of a line**. `PeekScreen::chosenCursor`
is the third place that question is asked and was the one answering it differently; it
returns `{block, 0}` now, which is the rule the other two already applied to the same
quantity.

**IT WAS WRONG FORWARD, AND THAT IS THE ONLY DIRECTION THAT MATTERED.** The panel is
NARROWER, so a block has MORE lines there and panel line L has consumed LESS text than
reading line L — so handing L across landed the reader **past the passage they pressed
GO HERE on**, with nothing on the screen to say so. Measured over a 600-word paragraph:
a commit from panel page 8 landed on reading page **5** with the peeked text on page
**4**, and one from panel page 18 named **line 130 of a block with 120 reading lines**,
which took `openAtCursor`'s documented "the end of the chapter is the closest honest
answer" exit and put the reader in the **next paragraph** — not a page off, the wrong
paragraph, from a commit made in the middle of the first one.

**THE COST IS MEASURED, NOT ASSERTED, AND IT IS ONE PAGE.** Over real prose the two
answers are the **same** reading page in 20 of `longChapter`'s 45 panel pages and one
page apart in the other 25, because a paragraph is four or five panel lines and the
disagreement is block-relative. What it costs is a long paragraph, where the landing is
its top — the passage is then *ahead* of the reader rather than behind them and one press
reaches it, which is `reading_position.h`'s own ordering: the top of the right paragraph
beats the front of the book, which beats nothing.

**IT LOOKED FINE ON THE DEVICE FOR THE SAME REASON IT LOOKED FINE IN 1,377 GREEN TESTS.**
The run that found it read `[peek] GO HERE spine=54 block=1 line=7: ok` — the peek's
page 2, at `(1, 7)`, which at the reading measure still falls on page 1, because a
17-line page swallows a seven-line offset. And **every paging fixture in this file's
suite was `longChapter`**, whose paragraphs are four or five lines, so its
block-relative index never leaves single figures and no case could reach the defect at
all: the same shape as the mutation that tells you about your INPUT before it tells you
about your test. `test_screen_peek.cpp` now carries a **one-600-word-paragraph** fixture
with a short second block after it, and the case is self-proving — the raw line is
asserted to land strictly *past* the page holding the peeked token, so it cannot pass
by being too shallow.

**THE READER BENEATH RELEASES ITS CHAPTER**, because two live chapters do not fit:
69,884 bytes peak with a 36,956-byte single allocation, against a measured 45,840-byte
floor. It is affordable because `ReaderScreen::render` reads only `page_` and `vm_`, so
the veiled page underneath draws with the chapter gone and **no decode at all**.

**THE 36,956 BYTES ARE NOT BEHIND A `unique_ptr`, AND THIS PROJECT'S OWN NOTE SAID THEY
WERE.** The roadmap's line was that all of `ChapterReader` is behind `unique_ptr`, so
releasing it is resetting pointers. Four of the five are; **`inflater_` is a value
member**, and the window lives behind its private `Scratch* s_` (`inflate_stream.h:166`,
"the one allocation"), freed by `~Inflater` and by nothing else — `inflated_` is only the
~40-byte `InflateSource` wrapper. So the obvious release frees a `BlockReader`, a
wrapper, a buffer view and a file handle, and keeps **every byte the feature exists to
give back**. And **neither obvious observation point can see it**: `held()` reads
`blocks_` and `bytesRead()` gates on the `InflateSource` pointer, so both go false
either way. `inflateWindowHeld()` is what bites, and its fixture has to be **DEFLATED**
or every assertion is `0 == 0` — the same shape as the in-memory book that made
`chapterBytesRead()` report 0 forever.

**`CLOSE` PAYS NO `seekTo`, AND THE DESIGN SPEC SAID IT SHOULD.** The spec budgeted "one
`seekTo` — 33.9 ms desktop", which is this file's own ratio trap: **a rewind costs what
page you are ON**, and the device measured ~376 ms at page 38, ~1010 ms at page 99 and
~3 s deep in a long chapter. On `CLOSE` that would cost more than committing the jump
does. Nothing visible was disturbed, so the only thing a close spends is the **live
builder** — and `pb_ == nullptr` is precisely the state `restreamAtCurrentPage` already
repairs in a quiet window.

**THERE IS NO GATE ON THE IDLE JOBS, AND THAT WAS CHECKED RATHER THAN ASSUMED.** All
three — `completeIndex`, `restreamAtCurrentPage`, `warmPageRing` — plus `refineNow`'s own
count and the quiet-window save are gated on `gApp->top().id() == ScreenId::Reader`, so a
peek on top stops them **by construction**. `readerOnStack`'s three callers are the
book-closed check, the ring shrink and the Typography apply, and the last is unreachable
while a peek is up because the pop that opened it took the menu with it. **A save while
released is safe for a reason worth stating**: `chapterBytesRead()` is `pageBytes_`, a
plain member, where `ChapterReader::bytesRead()` would answer 0 with `inflated_` gone and
push `progressPercent` onto its page/pageTotal fallback — the exact shape of the
percentage-going-backwards bug.

**IT IS NOT RESTORABLE ACROSS A WAKE.** The factory refuses an unprimed `Peek`, so
`App::restore` stops early and leaves the Reader standing — a refused push is wrong in a
way the reader can see through. Persisting a peeked cursor would be a card write for a
breadcrumb the anchor's own design declined to pay for.

**RE-ASKED ON GLASS AND CONFIRMED (2026-08-29), so it does not need arguing again.** It
was reported as a defect — "sleeping in the peek takes us back to the book" — and it is
not one: a peek is a transient *am I sure?*, and waking onto your own page is the calmer
default. **The reason given above is weaker than the decision, and that is worth knowing
if it is ever revisited**: the session record already stores the entry (`home:0;library:2;
reader:0;peek:0`), and that trailing `0` is a focus slot the peek has no use for, so the
peeked SPINE could ride there for no new card write at all. The cost was never the
storage; only the peeked *page within the panel* would need one. So the honest statement
is that a peek should not come back, not that it cannot.

**AND THE REPORT WAS RIGHT ABOUT THE MECHANISM even though it was wrong about this
screen** — see #49. Being restorable is per-screen tribal knowledge: one hand-written
`namesReader` scan on the wake path primes the book, and the reader menu and Contents are
primed only because `openBookAt` passes them on the way. Three screens have shipped
un-restorable by accident and this one is un-restorable on purpose, and **from the outside
those are indistinguishable** — the restore stops early and the reader lands somewhere
they did not expect. That is what makes the question keep coming back.

**THE BOX IS THE CONSTANT AND THE LINE COUNT IS THE RESULT, and it shipped the other way
round.** `kPeekPanelH` is **546px** — the panel is that tall on every device at every
setting — and the count is `floor(columnH / lineBox)`, whole lines, leftover as slack at
the foot of the panel. That is `design/Typography.dc.html`'s preview box's own rule ("the
box's height is DERIVED and fixed with respect to the settings … visible slack at large
sizes"), arrived at one screen later.

**The board argued the inverse for a phase and the argument does not hold.** It ran: a
pinned height "cut the last line in half lengthwise", therefore the panel must be sized by
its text — so `kPeekLines = 8` was the input and the height was `110 + ceil(8 × lineBox)`.
The premise is true and the step to the conclusion is missing: **a pinned height only cuts
a line in half if the count is not floored**, and `PageBuilder` floors it already. It was a
number that happened to be right at the default, not a rule.

**TWO THINGS IT COST, BOTH MEASURED (2026-08-29):**

- **ON GLASS THE PANEL WAS "A LOT SHORTER" THAN THE SIMULATOR SHOWS.** Reported by a
  reader at a smaller ppem and a tighter lead — 17 lines in their reading column where the
  default fits 12. Eight of *their* line boxes is ~310px against 546: a small box adrift in
  a lot of veil, on a screen whose whole job is to read as a modal. **No board and no
  golden could show it**, because nothing renders the reader at non-default typography
  (#40) — which is why the defect reached a device.
- **AT THE TOP OF BOTH RAMPS THE PANEL WAS TALLER THAN THE GLASS.** `kBodyPpemSteps` tops
  out at 46 and `kLineSpacingSteps` at 2000, so the widest line box is **92px**, eight of
  them a **736px** column and an **846px** panel — against 800 (X4) and 792 (X3).
  `centreIn(0, 800, 846)` is **−23**, so the panel began 23px above the top of the glass
  and ran 23px past the bottom, with its border off the screen at both ends. Confirmed by
  walking the real ramps before anything was changed, and again by restoring the old rule
  as a mutation: `e.top > 0` and `e.bottom < h` both fail at both geometries.

**546 IS WHAT THE OLD DERIVATION PRODUCED AT THE DEFAULT, TO THE PIXEL**, which is what
makes this a re-derivation and not a redesign: 4px of border, a 70px band, 16 above the
text and 20 below leaves **436** of column, and 436 holds eight 54.4px boxes. **Neither
peek golden moved.** The board's `height: 546px` is also 2px *more* than Chrome's
content-derived 544, so stating the firmware's own number closed a disagreement rather
than documenting it — design-vs-firmware went **4.20%/4.22% → 3.99%/4.02%**.

**Derived counts, measured at both geometries:** 17 at ppem 25 / lead 1.000, **8** at the
default 32 / 1.700, 9 at 46 / 1.000, 4 at 46 / 2.000. Note **ppem 25 at 2.000 is also 8**,
by coincidence — a `count != 8` guard written to prove the count moves failed there, which
is this file's rule about a mutation telling you about your input first.

**THE COUNT IS A QUERY, NOT A CONSTANT** — `Theme::peekVisibleLines`, beside
`libraryVisibleRows` and `contentsVisibleRows`, for their reason: it depends on the type
ramp *and* on the reader's settings, so nothing can hold it. **It takes no panel size, and
the absence is the statement**: the box is fixed, so the count cannot depend on which glass
it is drawn on.

**THE LINE BOX IS `ppem × lead`, NOT `lineHeight × lead`.** `PageBuilder` uses
`Tracking::em(font.ppem(), leadEm1000)`, which is what `line-height: 1.7` on
`font-size: 32px` means and what the board's measured 54.4px box is. Against `lineHeight()`
it is 48 × 1.7 = 82px, and the panel would reserve room for **twelve** lines while claiming
eight. **And it floors in f26, not in whole pixels**: a column a quarter of a pixel short
of eight boxes holds SEVEN.

**`rowsThatFit` IS THAT ARITHMETIC, ONCE, IN `layout.h`** — called by `PageBuilder`'s
constructor and by the theme alike. `test_theme_peek_metrics.cpp` used to carry it
TRANSCRIBED and said so in its own comment ("change the derivation and this file stays
green while the panel paginates to seven"); the second copy was the extraction point.

**`PeekViewModel::leadEm1000` WENT WITH THE CHANGE.** It existed only so `renderPeek` could
recompute a height that depended on the lead. With the box fixed, **neither `peekMetrics`
nor `renderPeek` reads any typography at all**, so `peekBox` no longer takes a lead and the
field had no other reader — the `ListRow::trackingEm1000` shape, caught this time before it
outlived its producer.

**THE GOLDEN TEST'S TOP-OF-COLUMN CHECK WAS ONLY EVER RUN WHERE IT COULD NOT FAIL.** Its
comment said the face-extent-exceeds-the-lead hazard "is not the hazard this screen has";
extending the walk to the ramp corners found it. `settings.h` records the two tightest
`kLineSpacingSteps` as deliberately tighter than the face's own ink, and at **ppem 46 /
lead 1.000 the first line's nominal top is 12px above the column** — inside the panel, but
outside the column it was asserted against. The bound is split now, and the body's 16px of
top padding is what it must stay inside.

**`Up` AND `Down` ARE DEAD SLOTS ON PURPOSE.** `Up` already means "return to where I was"
on the screen underneath, and one button with two meanings across a single press is worse
than an unbound one — so the **side** buttons page in the peek exactly as they do while
reading. A four-label bar also left only ~4px of slack at 480 wide, against faces that
measure ~3% wider than Chrome's.

**ITS BAND IS ITS OWN, NOT `drawPanelCaption`.** The caption's value is `Meta400` at 21px
on 21px of padding, where this board says `--t-value` (25px) at weight 700 on 18px — so
reusing it draws the band ~6px too tall, which is the header-band defect this project has
already paid for once.

**MEASURED AGAINST ITS BOARD AT 3.99% (X4) / 4.02% (X3)**, having been 4.20%/4.22% until
the board stated its own height (see above — Chrome derived 544 from the content where the
firmware derives 546, and the 2px was the band's rounding). **The sheet prints no number
(#41)**, so both figures are a threshold-at-128 count over the bare panel PNGs
`--export` writes; the method reproduces the older pair exactly on the pre-change board,
which is what makes the two comparable. Compare it against the other
**grayscale** screens and not against `reader_menu`'s ~3% — the peek declares
`Fidelity::Grayscale`, so a threshold-at-128 count over four levels inflates the figure,
and a healthy grayscale screen chased against a 1-bit one is how a healthy screen gets
chased as a regression. In the same tree `reader` reads 5.24%/6.29% and `reader_menu`
2.98%/3.49%, against the 5.34%/6.38% and 3.10%/3.60% recorded elsewhere in this file — a
consistent ~0.1pp, so these are the same instrument. The peek is the closest grayscale
panel on the sheet, which is what a panel with less prose in it should be.

## The typography panel

`Typography.dc.html`. Five rows over four `Settings` fields — `bodyPpem`,
`margins`, `lineSpacing`, `justify` — a live specimen, and a re-pagination of the
open chapter on the way out. **`kSettingsVersion` did NOT move**: an added field
takes its default from an older file, and every default here is the pre-feature
behaviour to the pixel, which is what keeps every reader golden where it is.

**ONE MODE, AND `CHANGE` CYCLES IN PLACE.** Up/Down move the focus, Confirm cycles
the focused value forward and wraps, Back pops. A TWO-MODE design was built first
and rejected off the rendered board: it read `DONE / EDIT / UP / DOWN` browsing and
`DONE / OK / UP / DOWN` editing, and **`DONE` and `OK` are synonyms** — two words
for "finished", nothing to say that one finished the ROW and the other left the
SCREEN. Cycling in place is Settings' own mechanism and its argument transfers
unchanged; what it costs is one direction, at most four presses over a five-value
list. Deleting the mode also deleted a second board, two view-model fields, and a
`‹ ›` marker whose axis contradicted the vertical buttons that stepped it.

**FOCUSABILITY IS DERIVED, NOT TABULATED**: a row is focusable iff its field has
more than one value. So `Font` is unreachable while one body face is vendored and
becomes reachable the moment a second lands, with no line to remember — which is
what replaced the chevron affordance that used to make a one-value row honest.

**THE VALUES AND THE FOCUS BOTH WRAP, and this screen is where that is free.** The
recorded hazard was never the wrap; it is AUTO-REPEAT — "a wrap belongs to a press
and a hold rests at the end". This screen declares no repeat, and must not: every
size step re-rasterises the body face.

**TWO ENTRY POINTS, and the panel needs nothing from the book.** The reader menu's
`Typography` row, and Settings' `READING` section — one disclosing row where five
inert readout rows used to be. Settings could become a door only because the panel
stopped needing an open book: its band names no book (the settings are device-wide,
so naming one contradicted the footnote) and its specimen is fixed.

**THE BAND'S RIGHT SLOT IS EMPTY AND STILL RESERVED ON THE BOARD.** Removing the
div outright shrank Chrome's band by 2px, because Chrome sizes a flex row by its
children while `bandContentH` takes `max(Label500, Value700)` unconditionally — and
a 2px band pushes every row below it out of alignment. **A band's height must not
vary by screen**, for the same reason the hint bar is always one line, so the board
holds the line box with an `&nbsp;`.

**SETTINGS' CONFIRM HINT FOLLOWS THE FOCUSED ROW** — `OPEN` on the `Typography`
row, `CHANGE` on the four `DEVICE` rows. It is **the first hint bar in this
firmware whose text varies within a screen**, and it has to: `screen_settings.cpp`
stated the premise outright ("CHANGE, not OPEN: nothing here pushes a screen") and
the new row makes it false. One slot changes as the focus moves; the alternative is
a Confirm labelled `CHANGE` that opens a screen.

**THE APPLY IS KEYED ON A READER BEING ANYWHERE ON THE STACK, NOT ON TOP**, and
that is load-bearing twice. From Settings there is no Reader and nothing should be
re-paginated. From the reader menu the pop lands on the MENU, which is an overlay —
`App::render` walks down to the topmost non-overlay, paints the Reader, then paints
the overlay over it — so **the Reader's stale page IS drawn on the very next
frame**. "On top" would never fire there. `Back` is a plain `pop()` for the same
reason `popTo(Reader)` was wrong: Settings' stack has no Reader and `popTo` stops at
the root.

**`ReaderScreen::relayout` LANDS AT THE TOP OF THE BLOCK**, dropping the cursor's
line, in one place. `setMetrics` cannot serve — it re-opens the chapter at PAGE ONE,
which is not what a reader who changed their type size asked for. And `fitOf` had
already graded this before the feature existed: it keys on `(ppem, columnW)`, so a
size or margin change reads `Relaid` and zeroes the same field, while line spacing
and alignment change neither and `line` legitimately survives them.

**THE PAGE RING IS GIVEN BACK ON THE WAY IN**, before any face re-init, because
`ScalableFont::init` takes the new arena BEFORE releasing the old: at ppem 46 the
roman alone is 24,576 bytes transient on top of the 16,384 it holds, against a
42,152-byte reading floor.

**A PERSISTED `Size` NEEDS A SECOND APPLY AT BOOT.** The body face is inited ~230
lines before `loadAndApplySettings()` runs, with the constant `kBodyPpem`, because
it must exist before anything can measure with it. So the setting reached the
SETTINGS and never the FACE: margins, lead and justify survived a reboot (
`readerMetrics` is computed after the load) and Size did not — set 22 PT, reboot,
and the page came back at 15 while both screens said 22. `setup()` now re-inits when
the face DISAGREES with the setting, guarded that way so a card holding the default
costs no cache flush.

**THE PREVIEW SHOWS ALL FOUR EDITABLE ROWS, AND IT SHIPPED SHOWING THREE.** The
spec said the box "cannot preview the margins" because the box is chrome geometry —
396px of measure on the X4 where the reading column is 444, so it can never BE the
reading measure — and that framing was wrong. **THE BOX IS THE PAGE AND ITS SIDE
PADDING IS THE MARGIN**, so the padding tracks the setting and the base measure
being narrower than the column is beside the point. Reported off the device as
"changing the margins doesn't update the live preview", which is the argument that
put justification in the box arriving on the one row that had been excluded from it.

- **The delta is EXACT, not scaled**, because the box and the panel are the same
  device pixels: one px of margin narrows the reading column by 2px and this
  padding by 1px each side. `kTypoPreviewPadXBase` is 16 at the tightest step, so
  `margins = 18` renders the board's 24px and WIDE reads 36.
- **ONLY THE MEASURE MOVES, AND THAT IS THE HALF A TEST HAS TO CHECK.** The border
  is placed from `kMargin` and `boxH`, neither of which reads the setting, so no row
  below the box shifts. A fix that inset the whole box instead would keep the box's
  HEIGHT and step every row below it on every press of one row —
  `test_theme_typography.cpp` compares the border's inked COLUMNS between the two
  end steps for exactly that, and it fails 166 assertions when the outline moves.
- **Justified text RIVERS MORE in the preview than on the page** at the default and
  wide settings, because the measure is still narrower than the column. The roadmap
  records rivers at this size as inherent; the preview exaggerates them.
- **The box's height is DERIVED and fixed with respect to the settings**, so the
  five rows never move and there is visible slack at large sizes. A pinned height
  was tried, was wrong by ~42px, and `flex-shrink` hid it — CLAUDE.md's first
  invariant, broken in this feature's first commit.

**THE TWO TIGHTEST LEADS ARE TIGHTER THAN THE FACE'S OWN INK, AND 1.0 CAN TOUCH.**
`kLineSpacingSteps` is seven values now — 1.0 and 1.2 were added below the shipped
floor of 1.4 — and the measurement is in `settings.h` beside the table so nobody
re-derives it: the body face at ppem 32 is `ascent=38 descent=-10 lineHeight=48`, so
its nominal extent is 48px against a 32px line box at 1.0 and 38px at 1.2. The
nominal figure is the face's worst case rather than any real pair of lines — with
real glyph heights a collision needs a box under ~40px — so **1.4 is clear despite
overflowing nominally, 1.2 can touch by ~2px and 1.0 by ~8px**. Offered anyway: it
is a reading-comfort call and this glass is the only place to settle it.

- **`settings.cpp`'s `kLineSpacingSteps[2] == kBodyLeadEm` assert exists to fail
  here**, and did: the default's index moved 2 → 4. A step added below the default
  silently re-indexes it, and the build stopping is what forces the number re-read.
- **IT TURNED A DOCUMENTED NON-PROPERTY INTO A REAL ONE.** `previewLinesThatFit`
  measures INK rather than line boxes, and its comment said plainly that
  `floor(boxH / lead)` agreed with it everywhere reachable and that a mutation to
  floor failed nothing. With these two steps the rules differ in **13 of 210**
  reachable cases, and at **(ppem 42, lead 1000) on the X4 floor draws a fifth line
  whose ink leaves the box** — the slice itself. **No hand-picked sample had that
  pair**: the case list held 25, 32 and 46 at that lead and all three agreed with
  floor, so the test walks the whole space (5 sizes × 7 leads × 3 margins × 2
  panels, 0.44 s) instead. The floor mutation now fails 14 assertions. **Writing
  down that a mutation does not bite is what made it noticeable when it started
  to.**

**`ProseAlign::Justify` EXISTS BECAUSE THE BOX SAYS LIVE PREVIEW.** Without it,
`CHANGE` on the `Alignment` row spends a ~520 ms repaint moving four characters of a
row value while the box does not move — a live preview visibly ignoring one of its
four rows. `stretchFor` and `kMinJustifyFillPercent` moved from `layout` down to the
TEXT layer with it, where `drawTextJustified` already lived; `stretchFor` takes no
`PageMetrics`, no `Block` and no cursor, so it was never pagination's.

**ppem 38 IS `18 PT`, AND `roadmap:1269` IS STILL OPEN.** That entry asks whether the
reader is under-sized by its own spec, having spotted that the board stated `18 PT`
while rendering book text at 32px. Implementing the screen forced the contradiction —
the firmware cannot render both — and it was resolved toward the preview's pixels and
the shipped default, because at ppem 38 the specimen is cut off mid-sentence with
~85px of empty box beneath it. **The board now says `15 PT` and the question is
unchanged**: it is about the DEFAULT, it is answerable only on the panel, and 18 PT
is one press away on the device.

**THE GLYPH CACHE LEVER IS SPENT.** `ScalableFont::cacheBytesFor` was written so a
caller could size the arena from the chosen ppem, and until now had none. The Size
row is that caller, and the pair's worst case is 39,936 bytes at ppem 46 against
26,624 today.

**WHAT THE DESKTOP CANNOT SEE HERE**, and it is most of the shell: that the faces
re-init without OOM at ppem 46 against the reading floor, that the ring shrink buys
the headroom `init` needs, that the apply fires exactly once per panel visit, that
the frame after the pop shows the re-paginated page under the veil, and what
`relayout` costs on a card-backed book — the desktop does no SD reads and no real
inflate, and this file's ~135× ratio warning applies to that walk.

## The corrupt-book dialog

`BookError.dc.html`, `BookErrorUnreadable.dc.html` for the refusal that is not
damage, and `BookErrorMemory.dc.html` for the refusal that is not about the file at
all — **the one shape with no `DELETE FILE…` slab.** Issue #5.

**WHAT IT CLOSES IS A PRESS THAT DID NOTHING.** `openBookAt` refused a book with a
log line and **nothing on the panel**, so Confirm on a damaged book produced no
visible change — worse than a dead button, because the press was correct and the
file is the problem. `book.h` had already anticipated the screen in as many words:
a refusal is "NEVER an abort ... the caller has a screen it can put the reason on".

**RAISED ONLY WHEN `push` IS TRUE**, which is a user press — a Library row or Home's
CONTINUE. The wake restore is excluded deliberately: `App::restore` already stops
short of a Reader it cannot build and leaves Home or the Library standing, which is
wrong in a way the reader can see through, and waking into a modal about a book
nobody just asked for replaces a calm landing with an interruption. The two other
`openBook` call sites are not this screen's and were checked rather than assumed —
the sleep-cover decode answers `CoverResult::ReadFailed` and falls back to the
reading card, and Book details' author lookup is best-effort; neither is a reader
asking to read a book.

**THREE COPY SHAPES, BECAUSE ONE SENTENCE WOULD BE A LIE.** `openBook`'s refusals are
not one event. Most are parse failures; `"cannot open the book file"` is `openRead`
returning null, a file that is gone or a card that is; and the `"not enough memory
to …"` family is a book that is fine on a device that is momentarily short. (This
said "four reasons … the fourth" while there were four; the count moved when the heap
guards added a class, which is why the shapes are named here and the reasons are
not counted.)

**`SdFileSystem::openRead` does not call `noteCardGone()`**; only a handle read that
comes up short does. So a card pulled between the Library's listing and the press is
noticed by `pollCardPresence` between 2 s (the fast probe) and 25 s (the FAT-scan
backstop), and for that whole window a single sentence would tell the reader a
perfectly healthy book "appears to be damaged". **A false claim is worse than an
absent one** — the same call this file already records for the unread battery gauge
(`-1`, not `0%`) and for the charging bolt that spends a refresh on the unplug edge.

**AND THE THIRD IS `OutOfMemory`, WHICH IS THE SAME ARGUMENT ARRIVING ONE REFUSAL
LATER** — `design/BookErrorMemory.dc.html`, which is `BookError.dc.html` with one
sentence changed exactly as `BookErrorUnreadable.dc.html` is. `openBook` can run out
of memory (see **Memory, which is what a real book runs into**), and **neither
existing shape may carry it**: `Damaged` says the bytes are not a book and they are,
`Unreadable` says the card would not answer and it did. The book is fine and the
device was momentarily short, which is `CoverResult::OutOfMemory`'s distinction one
screen over and the reason that enum has six values rather than a bool.

- **The copy says WHAT and not WHAT TO DO**, deliberately: *"…needs more memory than
  is free right now. The file was left untouched on the card."* The reader has no way
  to free memory on purpose — there is no second book to close and no restart control
  — and the one thing that reliably helps, a power cycle, is a promise about the
  resume path this screen is in no position to make. **`right now` is load-bearing the
  way `appears` is**: what the firmware knows is that the heap was short at one
  instant, not that this book is too big for the device. Its second sentence is
  `Damaged`'s character for character.
- **It clears the wrap boundary by 30px** against `test_book_error_copy.cpp`'s 12px
  floor (`Damaged` 19, `Unreadable` 39), so #76's rule was satisfied at authoring time
  rather than measured after the fact — which is what having made that rule mechanical
  for one screen buys.

**AND THIS SHAPE HAS NO `DELETE FILE…` SLAB, WHICH REVERSES THE DECISION THAT SHIPPED
WITH IT.** The slab was drawn and live on all three shapes, and both the header and
this file recorded that as **owed an owner's opinion rather than settled**. The owner
has settled it: **the file is fine.** Offering to delete a good book to fix a
transient shortage is a nudge in the wrong direction, and a reader might take it.
`Damaged` and `Unreadable` keep theirs exactly as they were — on those two, wanting
the file gone is reasonable.

- **THE PRECEDENT IS EXACT AND ALREADY IN THIS FILE: `HomeEmpty` HAS NO ACTION SLAB**,
  because its `SEND BOOKS OVER WI-FI` could not work once Wi-Fi was cut — *"a primary
  action that cannot work is worse than none"*. Same shape one screen over.
- **ABSENT, NOT INERT, AND THAT DISTINCTION IS THE WHOLE LICENCE.** A slab that
  **draws and does nothing** is the `works only sometimes` trap this project has
  shipped twice, and it is the recorded reason the slab is live on `Unreadable` — two
  shapes differing only by a sentence, so a reader meeting a dead slab has nothing to
  learn the rule from. A slab that **is not there** teaches nothing because there is
  nothing to press: the panel simply has one action, as `SdMissing` does. **Do not
  make it inert.**
- **TWO OF THE THREE OBJECTIONS RECORDED AGAINST THIS WERE ALREADY FALSE WHEN WRITTEN.**
  "A fourth board" — the third board exists and is the one edited; nothing was added.
  "A panel whose height depends on which refusal it is reporting" — it already did,
  and `paintFootprint`'s own comment says so: the shapes wrap to different heights, so
  `book_error_unreadable` is a 490px panel against `book_error`'s 531. Height varying
  by shape was the status quo, not a cost of this change.
- **THE ROW COUNT IS THE ONLY GATE.** `rowsFor()` gives this shape **one** row, so the
  focus cannot reach `kDelete` and `onGesture` is deliberately **not** also gated on
  `offersDelete` — a second condition is free to drift from the first, which is the
  class of bug `Focus` was extracted to delete. The view-model flag is what the
  RENDERER asks, and it is an explicit `bool` rather than `deleteLabel.empty()`:
  `ListRow::discloses` is the recorded precedent for why deriving this from an empty
  value is wrong, and a slab is a bigger claim than a chevron.
- **THE PANEL IS 451px, WAS 531px, AND THE 80 IS `kActionH` PLUS THE GAP THAT
  SEPARATED THE TWO SLABS.** Derived, never pinned: `4 + 73 + (18 + 28 + 12 + 210 + 18)
  + actionsH`, where `actionsH` is `2·68 + 12 + 20 = 168` with the slab and
  `68 + 20 = 88` without it. **The gap goes with the slab it separated** — the board's
  actions block is a flex column and a `gap` is BETWEEN items, so one slab has nothing
  for it to separate; keeping it would leave 12px of dead air and put the centred panel
  6px high. `actionsH` is now computed **once** and spent on both the paragraph's
  clamp budget and the panel's height, which had shipped as two copies of one
  expression — the shape that lets a budget and a height disagree.
- **THE BAR FOLLOWS THE PANEL: `CLOSE · OK` and two dead slots.** `SELECT` promises a
  choice and there is nothing to choose between; Up and Down have no second row. The
  Confirm slot is named after the slab it activates, which is `SdMissingScreen`'s own
  rule (`{"", "RETRY", "", ""}`). An empty slot is **36px, not zero**
  (`kHintEmptySlotW`), and the board authors both as the spacer eight other boards use
  — measuring one as nothing draws the two live slots in the wrong places. The bar's
  height did not move: its top rule is row 736 at both geometries, before and after.
- **MEASURED: 3.37% / 3.41%**, from 3.45% / 3.54%. The controls in the same tree are
  `book_error` **3.44% / 3.53%** and `book_error_unreadable` **2.99% / 3.17%**, which
  reproduce this file's recorded figures **to the digit** — that is what says the before
  and after are one instrument rather than two. The sheet still prints `ok` and not a
  percentage (#41), so these are threshold-at-128 counts over the bare `--export`
  panels. It improved because what left the panel is a tracked-caps label, which is
  where Chrome's subpixel advances and the firmware's whole-pixel ones disagree most per
  pixel of ink.
- **THE TWO RE-BLESSED GOLDENS MOVED A LOT AND IN EXACTLY TWO BANDS.** 49,629 px (X4)
  and 49,404 (X3): the panel band (old ∪ new, rows 135–665 / 131–661) and the hint
  bar's label rows (761–779 / 753–771, 1,414 px at both geometries). **Zero differing
  pixels anywhere else** — the veiled Library above and below the panel and the bar's
  own top rule are byte-identical. And `book_error` and `book_error_unreadable` did not
  move by a pixel at either geometry, which is what says the change is in the one shape
  and not in the shared path.

The screen takes a bounded `BookErrorReason`, **never the `why` string**, which is
developer English (`"the spine names no chapters"`), unstyled, unbounded and with no
slot on any board. It still goes to the log, where it is actionable.

**WHICH SHAPE IS `core/`'s NOW, AND WHERE A DELETE RETURNS TO IS STILL THE SHELL'S.**
That split used to read "both are decided in the SHELL", and the mapping half was a
bare `strcmp` against a literal the shell spelled and `book.cpp` spelled again — in
the one directory with no test harness, where five of this project's bugs have hidden.
A third shape would have made it two comparisons; a fourth that nobody remembered to
add reads as "damaged" on a healthy file. `bookErrorReasonFor` is the whole mapping
from developer English to the only vocabulary the panel has, and it lives beside the
enum. The `returnTo` stays the shell's, because only the shell knows which screen
asked.

**THE CLASS OF REFUSAL IS A PREFIX, NOT A CODE.** Every layer on the open path says
`"not enough memory to …"` and then what it was doing, so the class is readable off
`kOpenOutOfMemory` while the log keeps the site. That is a convention rather than a
type, and `test_heapguard.cpp` is what stops it being a convention nobody kept — it
drives real refusals out of every guarded site with an injected allocator and asserts
each one lands on this board. The alternative was a reason code out through
`openBook`'s signature and its five callers, which is worth it if a fourth class ever
appears.

**`DELETE FILE…` IS WHY `DeleteConfirmScreen` TOOK FACTS.** It held a
`LibraryScreen&` and acted through `deleteFocused()`, so it was reachable only from
the Library — and **Home's CONTINUE has none**, which is the likeliest real
corruption path because it is a book the reader was part-way through. A button that
works only sometimes is worse than one that never works, because nobody can learn
the rule. `BookDetailsScreen::Facts` had solved the identical problem for the
identical reason. The removal became a shell latch beside `Open`/`Retry`/`Finish`,
since a delete's consequences — `forgetCardFacts`, the rescan, `gHomeStale`,
`gLibraryStale` — are all the shell's.

- **PRIMING THE FACTS IS PART OF RAISING THE DIALOG, and leaving it out left the
  slab dead in exactly the case the refactor existed for.** `BookError` returns a
  bare `Action::push(ScreenId::DeleteConfirm)`; the factory checks `deleteFactsSet_`
  above its Library fallback, so from the Library it worked *by luck* — the focused
  row happened to be the failing book — and from Home it was refused outright.
  `openBookAt` primes both facts together now.
- **AND THE CLEAR IS NOT OPTIONAL.** Nothing cleared `deleteFacts_`, so: fail to
  open a book, dismiss, then `Delete…` a *different* book from the actions panel,
  and the confirmation named and removed the corrupt one. `clearDeleteFacts()` sits
  beside the `clearDetailsFacts()` that exists for the identical reason on the
  identical press.

**`DELETE FILE…` REPLACES THIS DIALOG RATHER THAN STACKING ON IT, AND A PUSH IS WHAT
SHIPPED FIRST.** `App::render` draws **every** overlay above the topmost non-overlay,
so pushing one overlay from another leaves the asking screen's panel standing under
the new one's veil. That is invisible between `ItemActions` and `DeleteConfirm` — the
confirmation is 380 wide against 340 and taller on both geometries, so it covers the
actions panel completely, which is why no board draws that panel behind it. **This
screen breaks the coincidence in the one direction that shows**: its paragraph makes
its panel TALLER than the confirmation's, so the error dialog stood out above and
below the thing meant to replace it. Reported off the device, and nothing on the
desktop had a reason to look — both goldens pin a single overlay.

- **`Action::replace` is the primitive**, not a special case in the screen. Two
  Actions cannot express it for `Action::popTo`'s own reason: a screen returns ONE
  Action, and one that followed a `Pop` with a `Push` would be reaching into the
  stack.
- **IT PUSHES BEFORE IT REMOVES**, so a factory that refuses leaves the stack exactly
  as it was — popping first would lose the screen that asked and put the reader back
  on the list with nothing to show for the press. From the root it degrades to a
  push, because erasing the root leaves nothing to render and nothing to receive the
  next event.
- The **depth** is the property the tests pin, and mutation says so: a push leaves
  three where a replace leaves two.

**A CENTRED PANEL MUST RESERVE THE HINT BAR TWICE, AND `renderDeleteConfirm` HAD THE
SAME DEFECT.** The clamp budget was `fb.height() - panelFixedH`, the whole canvas.
With a 255-character name — FAT's LFN maximum, so a name a real card can hold — the
panel ran to the canvas bottom, `DELETE FILE…` was sliced by the hint bar and the
panel's bottom border went off-glass. `centreIn` splits the slack evenly, so
reserving the bar **once** still leaves the panel hanging half a bar into it: the
budget is `height - 2 * hintBarHeight(...)`, derived and never pinned.

- **THE TEST THAT EXISTED TO CATCH THIS PASSED VACUOUSLY.** It found the panel by
  the first and last row carrying a `>= panelW` run, and the caption's **rule** spans
  the content width between the two side borders — so all three are contiguous and
  that row measures `panelW` too. With the bottom border off-canvas entirely, the
  "last such row" resolved to the caption's rule and the bound read `82 < 735`. Both
  cases locate the panel by its **side border columns** now, which nothing else on the
  frame inks. Reverting the budget fails 8 assertions; under the old form it failed
  none.

**THE BOARD DECLARES THE WRAP AND THE BOUND**, and for a while the code had both and
the board neither — `overflow-wrap: anywhere` on the paragraph and `max-height: 100%;
overflow: hidden` on the panel. Note this board needs the bound **one run further
down** than `DeleteConfirm.dc.html` does: there the caption carries the filename and
is the part that grows, here the caption is the fixed `CAN'T OPEN FILE` and the
PARAGRAPH carries the name. Getting that backwards — clamping the caption, wrapping
the prose `Normal` — put **391 pixels of a single realistic 67-character filename
outside the panel** on the X3.

**BOTH COPY SHAPES SAT ON THE WRAP BOUNDARY AND BOTH ARE OFF IT NOW (#76).** The
damaged sentence broke after `appears to be` because `damaged` needed **337px
against a 336px column** — one pixel. Chrome fits it, since the firmware's
whole-pixel advances measure ~3% wider, so the firmware wrapped to six lines where
the board wrapped to five, the centred panel was 41px taller, every rule landed
~20px out, and the sheet read **11.12% / 11.70%** against 3.58% for the screen that
differs from it only by a sentence. `appears damaged` clears it by 19px:
**3.44% / 3.53%**.

**THE UNREADABLE SHAPE WAS ON THE SAME EDGE AT 3px** and agreed with Chrome by luck
rather than by clearance — it would have flipped on any change to the face or the
ramp. Dropping `SD` takes it to 39px and to **2.99% / 3.17%**, and makes it agree
with the other shape, which already said "on the card".

**THE SLACK IS THE WRONG METRIC AND CHECKING IT WOULD NOT HAVE CAUGHT THIS.** A line
with 15px of slack is safe when the next word is 130px wide and on a knife edge when
the next word is 14px; what decides a break is by how much the NEXT WORD overflowed.
`test_book_error_copy.cpp` asserts that is at least 12px — ~3% of the column plus a
little — so a copy edit cannot put a line back on the boundary. It is proved by
mutation: the shipped sentences fail it reporting exactly 2 and 3.

**That makes this file's own rule mechanical for one screen** — *a specimen board
must not put a line on the wrap boundary* had no enforcement anywhere, and
`ReaderList`'s "Space is measured in rows." had already been moved by hand for it.
Every other board is still on the honour system.

## What the shell owes a flow, and two ways it silently owes nothing

**A REFUSED PUSH IS SILENT BY DESIGN, AND THAT IS INDISTINGUISHABLE FROM A DEAD
BUTTON.** `App::dispatch`'s Push case ignores `pushScreen`'s `false`, so a
factory that refuses marks nothing dirty and nothing reaches the glass. That is
right for a wake restore — it stops short of a screen it cannot build and
leaves what stands — and it is how V1.1's Wi-Fi flow shipped with **three
separate dead controls**, each reported off the device as "pressing X does
nothing":

| the press | what was not primed |
|---|---|
| Settings' `Wi-Fi` row | `setWifiNetworks` — `shell/` had no Wi-Fi code at all |
| the hub's SETUP row | `setWifiScan`, which is why an EMPTY scan is primed at boot |
| the HOLD on a saved network | `setWifiNetworkFacts`, re-primed every iteration the hub is on top |

**THE COMMON SHAPE IS A SCREEN THAT PUSHES DIRECTLY.** Each of those returns
`Action::push(...)` from its own `onGesture`, so the shell never sees the press
and cannot prime in response to it — whatever the factory needs has to be there
**before** the gesture. A latch would have let the shell prime and then push,
and that is the trade: a push is one line in the screen, a latch is a handler
in the shell. Where the payload is cheap and stateless, prime it continuously.

**AND A SCREEN'S OWN TEST CANNOT SEE ANY OF IT.** `CHECK(a.kind == Push && a.target == X)`
asserts what the screen RETURNS, which was correct in all three cases. Whether
the push SUCCEEDS is a fact about the factory and the shell, and `shell/` has no
harness. The catalogue guard in `test_focus_restore.cpp` requires every screen
to be CONSTRUCTIBLE, not reachable.

**`dispatchBack()` SENDS A PRESS, SO WHAT IT DOES IS WHATEVER THAT SCREEN'S
`onGesture` DOES WITH BACK.** That is right for `DeleteConfirm`, `BookEnd` and
the reader menu, whose Backs return a pop — the screen decides, once, and
nothing in the shell can drift from it. It is wrong for a screen whose Back
LATCHES: every connect-flow screen answers Back with `Action::wifi()`, so a
cancel handler that synthesised a Back **re-latched the request it was
serving** and the screen never left. An infinite loop, reaching the glass as a
hint that does nothing. `App::popScreen()` is the other tool, and
`dispatchBack`'s own header carries the rule for choosing. The note beside
`handleDelete` warning that a Back which does not pop "would spin loop()
forever" was written before either existed, and is exactly what this cost.

**A HEADER NAMED `wifi.h` IN `shell/src/` SHADOWS ARDUINO'S `<WiFi.h>`.** macOS's
filesystem is case-INSENSITIVE by default, so the sibling translation unit's
`#include <WiFi.h>` resolved to ours and the build failed with `'WiFi' was not
declared` against a header the compiler had happily opened. **It would have
built correctly on a case-sensitive volume**, which is the worse half. The file
is `wifi_store_nvs.h`.

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

`https://github.com/users/Rukkaitto/projects/1` — 48 items, and **the only index
of this project's deferred work.** Before it existed, every deferral lived as a
prose bullet inside the roadmap phase that spawned it, which is why this file has
had to record the same class of loss more than once: a follow-up note that
"stayed true for exactly as long as nobody read it", a paragraph that tracked its
own subject through three states in one session, `ListRow::trackingEm1000`
outliving its last producer, and one boarded screen (`Boot`) that the roadmap
does not mention at all. **A deferral with no card is a deferral nobody will
find.** `BookEnd` was the second of those two and is built (#6), which is the
argument working rather than an exception to it: it had a board and no roadmap
line, and what made it findable was the card.

**THE BOARD HOLDS STATUS AND NOTHING ELSE.** The roadmap holds the reasoning; a
card holds a `Source` pointer back to it (`roadmap:832`, `CLAUDE.md`) and at most
two sentences of context. That split is the one rule here and it is not tidiness:
a card that restates *why* is a second copy of a paragraph, and this file's whole
history is second copies drifting from first ones. If a card wants a paragraph,
the paragraph goes in the roadmap and the card gets the line number.

| Field | Holds |
|---|---|
| `Release` | `V1` / `V1.1` / `V1.2` / `V2` / `Someday`. What is left for each is the **Left to do** view, grouped by this |
| `Status` | the six stages below, with two entry doors |
| `Kind` | `Screen` / `Engine` / `Fidelity` / `Perf` / `Hardware` / `Tooling` / `Docs` — **and it names the skill**: `Screen` goes through `implement-screen`, `Fidelity` through `design-change`, an `On glass` move through `flash-device` |
| `Board` | which `.dc.html`, or empty |
| `Source` | `roadmap:<line>` or `CLAUDE.md`. A pointer, never an argument |

**The stages are this project's own gates, not generic kanban, and THERE ARE TWO
ENTRY DOORS** because there are two kinds of work:

- UI: `Needs a board` → `Boarded` → `Building` → `On glass` → `Done`
- everything else: `Todo` → `Building` → `On glass` → `Done`

**`Needs a board` IS FOR `Kind = Screen` AND `Kind = Fidelity` ONLY**, and the
first version of this section got that wrong — it stated the gate as universal, so
`Tag v0.1.0`, `TXT importer` and `Rotation CCW is unverified on the X4` all landed
in a column whose exit condition is "a `.dc.html` exists", which they can never
satisfy. **Eighteen of V1's twenty were parked in a state with no exit**, which is
the dead-button defect this file records twice elsewhere: a control that cannot do
anything reads as broken. Non-UI work enters at `Todo`; `Boarded` never applies to
it.

- **`Needs a board` → `Boarded` needs the `.dc.html` to exist**, because a UI
  change goes into the design HTML first. A `Kind = Screen` card past this stage
  with an empty `Board` field is in the wrong column. Note the reverse is not a
  contradiction: a card can name a board it needs *changed* (`HomeEmpty.dc.html`
  for the cut action slab) and still sit in `Needs a board`.
- **`Building` → `On glass` needs `make test` green.**
- **`On glass` → `Done` NEEDS DEVICE EVIDENCE, AND THEREFORE AN AGENT CAN NEVER
  MAKE THAT MOVE.** Flashing must be run by the user (see **Hardware facts**), so
  the user is the only one who can produce the photo or the serial log. **An agent
  moves a card as far as `On glass` and stops there**, naming what needs
  verifying. This is the column the board exists for: this repo has shipped work
  that passed every desktop test and was wrong on the panel — the `App::render`
  overlay bug, the `anchorJumped` self-recursion, the veil smearing diagonally
  under CCW rotation. `shell/` has no harness and the desktop cannot see the
  glass, so **"the tests pass" is not evidence and must not close a card.**

**WHAT TO DO, AND WHEN:**

- **Starting work:** set the card to `Building` before the first commit, so a
  session that dies mid-task leaves a trace of what it was doing.
- **Finishing something with no hardware surface:** `Closes #N` in the commit or
  PR closes the ISSUE on merge, which is exactly why the V1 items are real issues
  rather than drafts. **It does NOT move the card** — see below.
- **THERE IS NO LINK IN EITHER DIRECTION, AND THIS ENTRY CLAIMED ONE FOR MONTHS.**
  Setting `Status` to `Done` does not close the issue, and `Closes #N` does not
  move the card. **Both halves are hand work, every time.** Observed 2026-09-07 on
  #77: `Closes #77` in PR #84's body, squash-merged, left the issue `CLOSED`
  `COMPLETED` and the card sitting at `Building` through two separate reads, until
  it was set by hand. So a card set by hand — which is what `On glass` → `Done` on
  device evidence is — leaves its issue OPEN, and an issue closed by the keyword
  leaves its card wherever it was; the issue is the half anybody outside the board
  reads, and the card is the half the **Left to do** view counts.
  - **THE SHAPE IS THIS FILE'S OWN, ONE PARAGRAPH APART.** The bullet below already
    records the *other* direction of this claim being wrong, and corrects it from a
    coincidence in time — and left this direction standing on exactly the same
    unexamined basis, in the same entry, having just demonstrated the habit that
    catches it. **A correction is not a licence for the sentence next to it.**
  **Close it, and comment first**: the card records status and the issue is where
  the answer goes, so a card that asked a question (#35's was *skipped or
  truncated*) owes it an answer and a link to the PR that carries it.
  - **`gh issue close --comment` DROPS THE COMMENT if the issue is already
    closed.** It refuses the whole command (`is already closed`) rather than
    posting the comment and skipping the close, so the note is lost behind a
    message that reads like a harmless no-op. `gh issue comment` then
    `gh issue close`, in that order, cannot lose it.
  - **AND `closed_by=Rukkaitto` IN A TIMELINE IS A PERSON, NOT THE BOARD.** This
    entry first said the opposite — that `Done` auto-closes — and the whole basis
    for it was an issue found already closed, by the repo owner's own handle,
    within the same second as the card moved. That is what a maintainer merging a
    PR and closing its issue looks like, and it was read as an automation
    attributed to the owner. **A coincidence in time is not a mechanism**, the same
    shape as the 2.5 s of USB wait once recorded as the panel detection's cost, and
    it went into this file as fact before anyone asked the one person who knew.
- **Finishing anything the panel can be wrong about:** set `On glass`, and do
  **not** write `Closes #N` — it would close the ISSUE on desktop evidence, which
  is the failure above with a keyword attached. The card would stay where it is,
  so the board and the tracker would then disagree as well.
- **Finding something deferrable:** make a card. Not a `TODO`, not a bullet in a
  plan, not a paragraph here. **This is the rule the board is for.**

**A PLANNED RELEASE IS ISSUES; V2 AND SOMEDAY ARE DRAFT ITEMS, DELIBERATELY.**
An issue is what a commit can close, so everything anybody is working towards is
one: **V1 47, V1.1 19, V1.2 24, and not a draft among them**. The eighteen parked
items stay drafts so the spec §8 shelf is not sitting in the tracker as open work
nobody is doing. Same reasoning as `V2_SCREENS` in `tools/compare-design.py`:
reachable, not counted. Promoting a draft is
`convertProjectV2DraftIssueItemToIssue` and **the reverse does not exist**, so
promote when the work starts and not before.

**This said "V1 IS ISSUES" and named "Wi-Fi's nine boards" among the parked
drafts, and V1.1 falsified both halves** — the connect flow came back and
shipped, and its cards were issues from the day they were filed. **The count is
still eighteen and that is a coincidence**, not the figure surviving: the Wi-Fi
drafts left and the Names family and `Restore HomeEmpty's action slab` arrived.
A count that is right for a different reason is the shape this file keeps
recording, so the composition is worth reading off the board rather than off
this line — **V2 is 6 drafts beside 2 real issues now**, which the old sentence
had no room for either.

**`Todo` was missing from this table until 2026-08-28, and it is the entry door for
every non-UI card** — so anyone following this section for a `Kind = Tooling` card
hit a missing id. `Kind`, `Source` and `Phase` were undocumented entirely. `Phase`'s
meaning is still unrecorded: it is not clear whether it names the phase that spawned
a card or the phase that will close it, so the four cards filed on 2026-08-28 leave
it empty.

**Two API limits worth not rediscovering:**

- **Grouping and a board's column field are UI-only.** `createProjectV2View`
  accepts `name`, `layout`, `filter` and `visibleFieldIds` and nothing else, so a
  view's grouping can be neither scripted nor restored. Do not spend a turn
  trying.
- **The token needs the `project` scope**, which is account-wide rather than
  repo-scoped, and **only the user can grant it** — `gh auth refresh -s project`
  is interactive. A missing scope reads as
  `your authentication token is missing required scopes`.

The ids, rediscoverable with `gh project field-list 1 --owner Rukkaitto`.
**`Release` is the one row here that has ever gone stale, and it has done it
twice — re-list that field before trusting this table.** The incidents are in
its own row below rather than restated here, because a history kept in two
places is one that drifts in one of them.

| | id |
|---|---|
| project | `PVT_kwHOAkvc3c4BhZ5g` |
| `Status` | `PVTSSF_lAHOAkvc3c4BhZ5gzhgVwC4` — **`Todo` `ddab514e`**, `Needs a board` `75d83950`, `Boarded` `7ae6b024`, `Building` `000256fb`, `On glass` `5012a8f7`, `Done` `ae97917c` |
| `Kind` | `PVTSSF_lAHOAkvc3c4BhZ5gzhgVwUU` — `Screen` `fe704ca2`, `Engine` `e45425d2`, `Fidelity` `067a44e5`, `Perf` `3906b97c`, `Hardware` `8952abc2`, `Tooling` `09fcefaa`, `Docs` `ac2492c0` |
| `Source` | `PVTF_lAHOAkvc3c4BhZ5gzhgVwX8` (text) |
| `Phase` | `PVTSSF_lAHOAkvc3c4BhZ5gzhgV6lo` — `1` `1af00faf`, `2A` `70d666b6`, `2A-2` `10dd1639`, `2B` `891e6f65`, `2C` `226e8a24`, `3A` `726ae204`, `3B` `e13f494d`, `3C` `edb93849`, `3C+` `40a66d57`, `3D` `8f1728ee`, `3E` `f7ea731c`, `4` `e7a6573a`, `5` `f00b8560` |
| `Release` | `PVTSSF_lAHOAkvc3c4BhZ5gzhgVwUQ` — `V1` `88741031`, `V1.1` `0244a105`, **`V1.2` `f78ca656`**, `V2` `3a9bcb84`, `Someday` `4cd5509e`. **THIS ROW HAS NOW GONE STALE TWICE, THE SAME WAY BOTH TIMES: A RELEASE WAS ADDED AND NOTHING HERE NOTICED.** First `V1.1`, when all four recorded ids had also been rotated and the recorded ids answered `The single select option Id does not belong to the field` — **silently**, because `gh project item-edit` prints a GraphQL error and still **exits 0**, so a scripted `set -e` sweep reports success on the fields it did not set. Then `V1.2`, which is not an empty placeholder: it holds **24 issues**. That one cost nothing only because the missing option was one nobody had tried to set yet, which is luck rather than a property. **So the id is not the fragile part — the OPTION LIST is**, and it changes whenever the owner plans a release, which is not a moment anybody edits this file. Re-read the whole field from `field-list` rather than one id from this table, and verify by reading the item back: a write that failed and a write that landed look identical at the shell. **`Status`, `Kind` and `Phase` have never moved.** |

Moving one card is `gh project item-edit --id <item> --project-id <project>
--field-id <field> --single-select-option-id <option>`; the item id comes from
`gh project item-list 1 --owner Rukkaitto --format json`, matched on
`.content.number` for an issue.

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
