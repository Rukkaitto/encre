# The board

Extracted from `CLAUDE.md`, which keeps a stub under this heading and is where
the cross-references to it point. Same standing as anything in that file.

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
| `Release` | `v0.1.0` / `v0.2.0` / `v0.3.0` / `v0.4.0` / `V2` / `Someday` — **spelled as the tags**, since that is what a release here is. What is left for each is the **Left to do** view, grouped by this |
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
one: **v0.1.0 47, v0.2.0 25, v0.3.0 8, v0.4.0 22, and not a draft among them**. The eighteen parked
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

**THE OPTIONS ARE SPELLED AS TAGS NOW, BECAUSE THE MAPPING LIVED IN NOBODY'S
HEAD** (2026-09-19). `V1` shipped as `v0.1.0` and `V1.1` as `v0.2.0`, and
nothing anywhere wrote that down: the gate in `docs/releasing.md` takes a
release NAME and what it clears is a TAG, so every run of it was a translation
from memory. They are `v0.1.0`, `v0.2.0`, `v0.3.0`, `v0.4.0`, then `V2` and
`Someday`, which keep their names because they are shelves and not releases.
**The rename is `updateProjectV2Field` with each option's OWN id passed back**,
which renames in place and leaves every card's value alone — the mutation takes
the whole option list, so an omitted id creates a new option instead and drops
that release's cards on the floor. Read the counts back before believing it
landed: 118 cards in, 118 cards out, and `--release V1.2` now **errors** where
before the rename it would have been the only spelling that worked.

**AND v0.3.0 IS A SCOPE, WHERE `V1.2` WAS A BACKLOG WEARING A RELEASE NAME**
(2026-09-19). It held 25 open cards — everything not yet done — so the gate's
one question could only ever be answered `no`, and the release it named was
whenever all the deferred work ran out. v0.3.0 is the Names family (#156, #157,
#158) with the reader-menu row that gives it a door (#159), three technical
cards (#152, #26, #105) and the tag (#160). The other 22 moved to `v0.4.0`
untouched, which is the same bucket with an honest name on it: **the scope is
the cards somebody means to finish, and the rest of the board is not a release
just because it is next.**

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
| `Release` | `PVTSSF_lAHOAkvc3c4BhZ5gzhgVwUQ` — `v0.1.0` `88741031`, `v0.2.0` `0244a105`, `v0.3.0` `f78ca656`, **`v0.4.0` `088b0e9f`**, `V2` `3a9bcb84`, `Someday` `4cd5509e`. **The first three were RENAMED on 2026-09-19 and kept their ids**, so a recorded id outliving the name it was written beside is now a thing that has happened here. **THIS ROW HAS NOW GONE STALE TWICE, THE SAME WAY BOTH TIMES: A RELEASE WAS ADDED AND NOTHING HERE NOTICED.** First `V1.1`, when all four recorded ids had also been rotated and the recorded ids answered `The single select option Id does not belong to the field` — **silently**, because `gh project item-edit` prints a GraphQL error and still **exits 0**, so a scripted `set -e` sweep reports success on the fields it did not set. Then `V1.2`, which is not an empty placeholder: it holds **24 issues**. That one cost nothing only because the missing option was one nobody had tried to set yet, which is luck rather than a property. **So the id is not the fragile part — the OPTION LIST is**, and it changes whenever the owner plans a release, which is not a moment anybody edits this file. Re-read the whole field from `field-list` rather than one id from this table, and verify by reading the item back: a write that failed and a write that landed look identical at the shell. **`Status`, `Kind` and `Phase` have never moved.** |

Moving one card is `gh project item-edit --id <item> --project-id <project>
--field-id <field> --single-select-option-id <option>`; the item id comes from
`gh project item-list 1 --owner Rukkaitto --format json`, matched on
`.content.number` for an issue.
