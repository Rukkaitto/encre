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

## What v0.1.0 is waiting on

**Not a list, a query.** A written list of blockers in this file would be a
second copy of the project board, and this project's whole failure history is
second copies drifting from first ones. Run it:

    gh project item-list 1 --owner Rukkaitto --format json --limit 100 \
      | jq -r '.items[] | select(.release=="V1" and .status!="Done")
               | [.status, .content.number // "draft", .kind, .title] | @tsv' \
      | sort

**Everything that prints is a blocker, except the release card itself** — the
card tracking this tag is a V1 card and is not `Done` until the tag exists, so
it always appears. Every other line is real work.

The board is the only index of this project's deferred work; if something is
missing from it, the fix is a card, not a line here.

At the time of writing, that query returns unbuilt V1 screens, open engine
defects, and these docs. It will keep returning things until it does not.

## The gate, in order

Each step is a precondition for the next. Nothing here can be skipped by
agreement — the last two cannot be produced on a desktop at all.

1. **The query above returns nothing but the release card.** Every other V1
   card is `Done`.
2. **`make test` is green** on a clean tree, and `make compare` is green.
   `make conventions` passes for the commits being released.
3. **CI is green on `main`** for the commit being tagged. `firmware` is the only
   thing anywhere that compiles `shell/`, and `test` runs on Linux/gcc where the
   goldens were blessed on macOS/clang. **The release workflow does not re-run
   any of it** — that would be ten minutes to re-answer a question already
   answered on this commit, and it cannot answer steps 4 and 5. So this step is
   a real precondition and not a formality: check the run.
4. **`docs/on-device-smoke-checklist.md` has been run in full**, on hardware,
   with its `run.log` kept. A pass is what moves the last cards from `On glass`
   to `Done`.
5. **The roadmap's Phase 5 exit is met:** *a week of daily-driver reading
   without touching a cable.* This is the criterion the whole of V1 was written
   against and it is the one that cannot be hurried — a week of real use finds
   what twenty minutes of checklist does not.

**Steps 4 and 5 are the owner's, not an agent's.** Flashing is blocked from an
agent by the permission classifier, so device evidence can only come from the
person holding the device. An agent can carry a release card as far as `On
glass` and must stop there.

## Cutting the tag

Once the gate is clear, from a clean checkout of `main`:

    git switch main && git pull --ff-only
    git status --porcelain          # must print nothing
    make test && make compare

    git tag -a v0.1.0 -m "Encre v0.1.0"
    git push origin v0.1.0

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

    python3 tools/release_notes.py --tag v0.1.0

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

`v0.1.0` is the first tag; the repo has none today. Semantic-versioning
mechanics are not worth deciding in advance for a project with one user — what
matters is that a tag names a commit somebody actually read a book on.

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
