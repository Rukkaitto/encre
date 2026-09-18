# Releasing

A release here is an **annotated tag on `main`**. Pushing one is the whole act:
`.github/workflows/release.yml` does the rest, and there is nothing to click
afterwards.

**That workflow does not decide whether the work is releasable.** The gate below
does, and its last two steps cannot be produced on a desktop at all. The tag is
where that judgement is recorded; everything after it is mechanical. What the
workflow contributes is the two refusals a human gate cannot make reliably —
that the tag is **annotated**, and that it is **contained in `main`**, which
GitHub's tag trigger does not check on its own — plus the notes and the build.

Nothing here is signed.

## What a release is waiting on

**Not a list, a query.** A written list of blockers in this file would be a
second copy of the project board, and this project's whole failure history is
second copies drifting from first ones. Run it, naming the release being cut:

    python3 tools/release_blockers.py --release V1.1

One TSV line per card in that release that is not `Done` — status, issue number
(or `draft`), kind, title — and everything it prints is real work. It exits
**0** when the release is clear, **1** when blockers remain, and **2** when it
could not answer. That third code is the point of the rewrite: a gate that
cannot tell a complete answer from a missing one has no business clearing a
release.

**IT USED TO BE A `gh ... | jq` PIPELINE, AND THE PIPELINE COULD NOT FAIL.** It
passed `--limit 100` against a board that has since reached 114 items, and
`gh project item-list` truncates silently — so the gate was reading a prefix of
the board and reporting it as a verdict. That is the
reports-on-less-than-it-claims shape this project refuses for the card probe
answered from cache, for the `make compare` default that skipped four screens,
and for `logToCard`.
Measured at the moment it was replaced: the old query saw **6** V1.1 blockers
where there were **17**, and among the eleven it dropped was the issue asking
for this fix.

- **The limit is gone rather than raised.** 200 would reintroduce the same
  defect one board-year later. `totalCount` is the board's own count and does
  not shrink with `--limit`, so the size is *asked for*: one call to learn it, a
  second asking for exactly that many, and a refusal if the two disagree.
- **The release is a parameter, and an unknown one is an error.** A typo selects
  nothing and would otherwise exit 0, which reads exactly like a release with
  nothing left to do. The name is checked against the board's own `Release`
  options, read live, for the reason `compare-design.py`'s `--only` errors on an
  id it does not recognise rather than reporting `0/0`.
- **It hides nothing, including the release card.** If a card tracks the tag
  being cut, that one line is not a blocker: it closes when the tag does. That
  is a judgement for whoever reads the line and not a row the tool drops — a
  script guessing which card is the release card, by title, there being nothing
  else to go on, would hide a genuine blocker the first time a title matched.
  v0.1.0's card is the only one there has ever been, and it is `Done`, so at the
  time of writing no release has such a card and nothing is exempt.
- **It has its own tests**, `python3 tools/test_release_blockers.py`,
  deliberately not wired into `make test` for `tools/test_release_notes.py`'s
  reason. Its refusals are the reason it is a tracked script rather than four
  lines in this file: `gh ... | jq ... | sort` reports `sort`'s exit status, so
  a `jq` that refused mid-stream leaves a pipeline that printed nothing and
  exited 0 — the fix for a gate that passes silently must not itself pass
  silently.

The board is the only index of this project's deferred work; if something is
missing from it, the fix is a card, not a line here. A card carrying no
`Release` appears in no release's gate at all, so the script names those on
stderr rather than leaving them to be found by looking.

## The gate, in order

Each step is a precondition for the next. Nothing here can be skipped by
agreement — the last two cannot be produced on a desktop at all.

1. **The query above exits 0**, having named the release being cut. Every card
   in that release is `Done`, bar a release card if one exists — which is the
   one line above that is not work, and the only one.
2. **`README.md`'s checklist agrees with the board**, reconciled against the
   query you have just run. A ticked box that stopped being true is a lie on the
   front page; an unticked one that shipped only understates, so the ticks are
   the half to check hardest. This is the one thing the README states that can
   drift, and it states it deliberately -- every other claim on that page was
   chosen because a merge cannot falsify it. The rendered screens go with it: if a
   UI change reached any screen named in the Makefile's `README_SCREENS`, run
   `make readme-images` and commit what moves. That variable IS the list, so
   naming the screens here would be a second copy of it, free to go stale the
   first time the set changes -- which it did, the first time one was added.
3. **The version the DEVICE draws is the version being tagged.** Settings'
   header band states it, and it is **three hardcoded copies** that must move
   together:

   | | holds |
   |---|---|
   | `core/include/reader/version.h` | `kVersion`, which `syncVm` draws as `V <n>` |
   | `design/Settings.dc.html` | the same string, so the board has something to be compared against |
   | `test/unit/test_version.cpp` | a pin on the literal, which is the only one that fails loudly |

   **`make compare` CANNOT CATCH THIS AND WILL REPORT GREEN WHILE IT IS WRONG**,
   because the board carries its own copy: bump neither and the two agree, and
   the sheet measures a stale version against a stale version. Bumping the
   header alone fails `make test`; bumping the board alone fails `make compare`.
   Only bumping all three passes both, which is the point of listing them.

   **v0.2.0 shipped saying `V 0.1.0`** -- the whole six-step gate was run
   faithfully and none of it mentioned the version, so the tag was cut, the
   release published, and the firmware on it misreported itself. The tag was
   deleted and re-cut within the hour because nothing had been downloaded yet;
   that is luck, not a procedure. This step is what replaces the luck.

4. **`make test` is green** on a clean tree, and `make compare` is green.
   `make conventions` passes for the commits being released.
5. **CI is green on `main`** for the commit being tagged. `firmware` is the only
   thing anywhere that compiles `shell/`, and `test` runs on Linux/gcc where the
   goldens were blessed on macOS/clang. **The release workflow does not re-run
   any of it** — that would be ten minutes to re-answer a question already
   answered on this commit, and it cannot answer steps 6 and 7. So this step is
   a real precondition and not a formality: check the run.
6. **`docs/on-device-smoke-checklist.md` has been run in full**, on hardware,
   with its `run.log` kept. A pass is what moves the last cards from `On glass`
   to `Done`.
7. **The roadmap's Phase 5 exit is met:** *a week of daily-driver reading
   without touching a cable.* This is the criterion the whole of V1 was written
   against and it is the one that cannot be hurried — a week of real use finds
   what twenty minutes of checklist does not.

**Steps 6 and 7 are the owner's, not an agent's.** Flashing is blocked from an
agent by the permission classifier, so device evidence can only come from the
person holding the device. An agent can carry a release card as far as `On
glass` and must stop there.

## Cutting the tag

Once the gate is clear, from a clean checkout of `main`:

    git switch main && git pull --ff-only
    git status --porcelain          # must print nothing
    make test && make compare

    git tag -a <tag> -m "Encre <tag>"
    git push origin <tag>

`<tag>` is written out rather than shown as `v0.1.0`, which has shipped: a
worked example naming a tag that already exists is one a reader can run and be
told only that it exists.

**`-a` is not optional and the workflow enforces it**, because this file has
always said a release *is* an annotated tag and nothing checked. A lightweight
tag is refused with the commands to re-cut it.

That push is the last manual step. Watch the run; on success there is a
published release with notes and three files on it.

## What the workflow publishes

**Release notes are generated, not kept.** Commit subjects are Conventional
Commits and the convention is enforced on every pull request, so the log *is*
the changelog. `tools/release_notes.py` is that log — `git log --no-merges
--format='- %s'`, over `<previous-tag>..<tag>`, or over the whole history when
there is no previous tag, which is v0.1.0's case. Run it yourself before tagging
if you want to see what it will say:

    python3 tools/release_notes.py --tag <tag>

It has its own tests, `python3 tools/test_release_notes.py`, deliberately not
wired into `make test` for `tools/test_check_conventions.py`'s reason.

There is deliberately **no `CHANGELOG.md`**: a hand-maintained one would be a
second copy of the commit log, free to disagree with it, and disagreeing copies
are the failure this repo keeps paying for.

**Three files are attached**, and the difference between the first two matters
to whoever downloads them:

| asset | is |
|---|---|
| `encre-<tag>-xteink.bin` | the app partition alone — an **update** for a device already partitioned, written at `app0`'s offset |
| `encre-<tag>-xteink-full.bin` | bootloader, partition table, otadata and app in one image, flashable at **offset 0** on a bare chip |
| `encre-<tag>-xteink.elf` | the symbols for that exact build |

The **ELF is not an afterthought**. A panic from a released build is
undebuggable without the byte-identical one: CLAUDE.md's crash procedure opens
by checking the report's `ELF file SHA256` against this file, and nobody can
reproduce a matching rebuild months later. Building from the tag is still
supported and still what the README describes — the images are for someone who
does not want a toolchain.

**The offsets come from `partitions.csv`**, read at merge time rather than
copied into the workflow, so a repartition cannot leave a released image
flashing to the wrong place. Only the two that are the platform's — the C3's
`0x0` bootloader offset and ESP-IDF's `0x8000` partition table — are stated in
the workflow, and they are commented as such.

## Versioning

`v0.1.0` was the first tag and shipped on 2026-09-11. Semantic-versioning
mechanics are not worth deciding in advance for a project with one user — what
matters is that a tag names a commit somebody actually read a book on. **This
line said the repo had no tags, and stayed that way through the release that
falsified it**, which is why the blocker query above no longer names a release
either: a claim about *which* release is being cut goes stale the moment one is.

**One thing a version number here does not mean:** that it runs on an X4. Every
device measurement in this repo was taken on an X3, and rotation is unverified
on the X4 ([#23](https://github.com/Rukkaitto/encre/issues/23)).

**The notes say so on their own, and they stop saying it on their own.**
`release_notes.py` asks the tracker whether #23 is still open and emits the
caveat only while it is. That is deliberate rather than clever: an instruction
in this file to "say so in the release notes", carried out by a generator, is a
claim with an expiry date and no owner — it would still be printed the day #23
closes and nobody would come back here to notice. If the query cannot be made at
all, the caveat is **included**: a limitation stated once too often is a smaller
wrong than a real one dropped silently.

## Afterwards

- Move the release card to `Done` — the tag is the evidence, and it is the one
  card where a desktop artefact really is the proof.
- Anything the week of daily-driver reading turned up gets a card. That week is
  the most realistic test this firmware ever gets; its findings should not
  evaporate into the release notes.
