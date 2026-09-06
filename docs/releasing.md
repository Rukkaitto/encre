# Releasing

A release here is an **annotated tag on `main`** and nothing more. There is no
release job in CI (`.github/workflows/ci.yml` is `test`, `firmware` and
`compare`), no published binary, and no artefact to sign. Someone taking a
release builds it themselves from the tag, which is what the README's build
section is for.

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
3. **CI is green on `main`** — all three jobs. `firmware` is the only thing
   anywhere that compiles `shell/`, and `test` runs on Linux/gcc where the
   goldens were blessed on macOS/clang.
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

**Release notes are generated, not kept.** Commit subjects are Conventional
Commits and the convention is enforced on every pull request, so the log *is*
the changelog:

    git log --no-merges --format='- %s' v0.1.0     # or <previous-tag>..v0.1.0

Paste that into the GitHub release, grouped by type if it helps. There is
deliberately **no `CHANGELOG.md`**: a hand-maintained one would be a second copy
of the commit log, free to disagree with it, and disagreeing copies are the
failure this repo keeps paying for.

## Versioning

`v0.1.0` is the first tag; the repo has none today. Semantic-versioning
mechanics are not worth deciding in advance for a project with one user — what
matters is that a tag names a commit somebody actually read a book on.

**One thing a version number here does not mean:** that it runs on an X4. Every
device measurement in this repo was taken on an X3, and rotation is unverified
on the X4 ([#23](https://github.com/Rukkaitto/encre/issues/23)). Say so in the
release notes rather than letting a version number imply otherwise.

## Afterwards

- Move the release card to `Done` — the tag is the evidence, and it is the one
  card where a desktop artefact really is the proof.
- Anything the week of daily-driver reading turned up gets a card. That week is
  the most realistic test this firmware ever gets; its findings should not
  evaporate into the release notes.
