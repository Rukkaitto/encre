# CI

Extracted from `CLAUDE.md`, which keeps a stub under this heading and is where
the cross-references to it point. Same standing as anything in that file.

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
measure how close the render is: every panel carries a mismatch percentage beside
its `ok` since #41, and **nothing fails on it** -- there is no blessed number to
fail against. A board with no screen behind it stays fine; that is
**four of the 60** — measured on 2026-09-17, not inherited: a full run with the gate
on reports `56/60 screens implemented` and exits 0 (Bookmarks, Boot, Names,
Names / empty).

**BOTH FIGURES MOVE WHENEVER A BOARD LANDS, AND THIS LINE WENT STALE BY 23 OF THEM.**
It has said 36/31 and then 37/32, the second recorded when `BookErrorMemory.dc.html`
landed — and V1.1's connect flow and the whole articles feature have landed since,
with nobody coming back here. **Re-run it rather than quoting it**; the count is a
`make compare` away and this paragraph is the standing proof that an inherited one is
worth nothing. #7 is the first thing to move the NUMERATOR alone: `Home / missing
book` left the nothing-behind-it list where every previous move added a board.
Note the denominator read **37 before #77 as well**, and for the opposite reason: the
extra row there was one board counted twice, not a thirty-seventh board.

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
