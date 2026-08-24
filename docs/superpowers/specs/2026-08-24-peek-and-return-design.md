# Peek and return — looking somewhere else without losing your place

**Status:** design approved 2026-08-24. Not implemented. Scoped out of a larger
question — see *Where this came from* — and specced alone because it is the piece
everything else waits on.

## Where this came from

The question was: **"who's this character again?"** A reader 300 pages into a novel
meets a name and has no idea whose it is. Kindle answers it with X-Ray, which is
precomputed on Amazon's servers and reached through a touchscreen. Encre has
neither: Wi-Fi is cut from V1, and there are six buttons and no pointer.

Working that feature through produced four separable pieces, in dependency order:

| | Piece | Depends on | Useful alone |
|---|---|---|---|
| **A** | **Peek and return** — go somewhere and come back | nothing | **Yes** |
| **B** | **ReaderMenu** — the sheet `ReaderMenu.dc.html` boards | nothing | Yes |
| **C** | **Name index** — names and their introducing sentence, accumulated as you read | nothing | No |
| **D** | **Names screen** — the list and the entry | A, B, C | — |

**A is this spec, and it is not really about characters at all.** `Contents`,
`GoToPage` and `Bookmarks` are all boarded, all unbuilt, and **all three are
useless without a way back** — a table-of-contents jump that costs you your place
is a jump readers learn not to make. Peek is the primitive those three have been
waiting for; the character feature would be its fourth caller.

This project's rule is that the second copy is the extraction point. Here there are
four callers and zero copies, which is the one time the primitive gets built first.

C and D are recorded in the roadmap so the original question is not lost. Their
design decisions, already taken, are in *What this does not do*.

## What a peek is

**Two mechanisms, because there are two different needs.**

**The peek overlay** — a panel of book text over the veiled page you are reading.
A real `Screen` with `isOverlay()` true, so `App` already knows how to render it:
walk down to the Reader, render it, veil, render the panel. The side buttons turn
pages *inside* the panel. `Back` closes it and your page is untouched, because
nothing was ever committed and so nothing needs restoring.

**The return anchor** — a `{spine, block, line}` triple recording where you were
before you stopped reading linearly. Set by committing a peek, and also by ordinary
backward paging, which is the far more common way to lose your place.

They are separable on purpose: **browsing costs nothing and risks nothing;
committing is explicit and reversible.**

## The return anchor

### The rule

The anchor is **where you were before you stopped reading linearly**. It is a single
optional value, and exactly four transitions touch it:

| Movement | Anchor unset | Anchor set |
|---|---|---|
| Page forward | stays unset | cleared if this reaches or passes it; otherwise unchanged |
| Page backward | **set to the position being left** | unchanged |
| Jump (forward or backward) | **set to the position being left** | **overwritten** with the position being left |

Reading forward never *raises* an anchor, which is the part that is easy to get
wrong. An anchor materialises only when the reader turns back, and from then on it
holds still while they move around below it — so it always names the furthest point
of the current excursion rather than the last thing they did.

**A jump overwrites unconditionally, and that is why the rule needs two cases.**
Commit a peek from chapter 2 into chapter 8 and a pure high-water rule would find
the anchor *behind* the reader and clear it, throwing away the one breadcrumb they
wanted. Distinguishing a departure from a drift is not a special case; it is the
whole distinction.

### THE ANCHOR IS A PAGE — and the comparison still has to work across chapters

**Granularity first, because an earlier draft of this section was read as saying the
opposite.** The anchor names **one page**, not one chapter. Page back three pages
inside a single chapter and the anchor is the page you left; `Up` returns to that
page. Nothing here is chapter-granular.

What it stores is `(spine, block, line)`, and that triple *is* a page position: a
page-start cursor is exactly `(block, line)`, which is what `starts_` holds one of
per page. So the anchor is a page expressed the way this codebase already expresses
a page.

The reason it is not stored as a **page number** is that `at_` is an index into the
*current chapter's* `starts_`, so page 7 means nothing once you are in a different
chapter — two positions in different chapters could not be compared, and the
high-water rule is entirely a comparison. `(spine, block, line)` compares
lexicographically, spine order **is** reading order, so that ordering is the book's
own. It is also the triple `ReadingPosition` already stores, so the anchor needs no
new representation.

The page NUMBER the footer shows is therefore computed, not stored — `openAtCursor`
lands on the page containing a cursor and counts boundaries to name it, which is the
same mechanism the reading-position restore already uses.

### Persistence rides the existing save edges

`saveReadingPosition` already fires on three edges — leaving the book, crossing a
chapter, sleeping (`shell/src/main.cpp:1195`). The anchor goes with it. **No new
card write**, and in particular not one per page turn: losing an anchor to a power
cut costs the reader a shortcut and nothing else, because they are still sitting on
a real page. That is a benign failure and it does not justify a write on an edge
that does not already take one.

The sidecar gains three flat integers. `core/include/reader/json.h` has no nesting
and no arrays, and three ints need neither.

### It degrades with the position, not independently

`fitOf` grades a restored record `Exact` / `Relaid` / `Rebound` / `Unusable`, and
`restoreFrom` zeroes what the grade cannot support. **The anchor's `line` is exactly
as fragile as the position's `line`**, so anything below `Exact` drops the anchor
rather than keeping it. A stale anchor that lands the reader on the wrong page is
worse than no anchor at all — the same reasoning that already zeroes `line` on a
re-layout.

### The affordance, and the fact that constrains it

**The Reader draws no hint bar.** `design/Reader.dc.html` is header, body, footer and
nothing else, so there is no slot that says what a button does. And the anchor now
appears during ordinary backward paging, so it appears *often*. It has to be quiet,
and it has to teach its own button, because nothing else can.

**A third footer field**, present only when an anchor is set. The footer carries
`6%` and `53 / 890` today; a centre field reads the return target and names its
button. It costs no vertical space — which matters more here than anywhere else on
the device, because a footer that changed height would reflow the text column and
**re-paginate the chapter mid-read**.

That is also why this is not a hint bar appearing on demand. A bar whose height
varies moves everything above it; on a list that is untidy, and on the Reader it
silently changes which words are on the page.

`Up` is the binding. The Reader's free buttons are `Up` and `Down` — `Confirm` is
spoken for by `ReaderMenu`, the sides turn pages, `Back` leaves the book — and
neither has a natural "forward to where I was" reading, so the indicator names it
explicitly.

**`Up` does nothing when there is no anchor**, and that is the dead-button defect
this project has shipped twice, so it needs saying rather than discovering: **the
footer field is the promise, and its absence is the absence of a promise.** No
indicator, no expectation. Home's `READ` hint is gated on `nothingToContinue` for
the same reason — a bar that promises nothing must not do something, and a screen
that promises nothing may do nothing.

### The board comes first

Per the project rule, `design/Reader.dc.html` gains the field before any code, and
so does whichever Home-adjacent board shows a reading state if the indicator ever
appears outside the Reader. It does not.

## The peek overlay

### The memory dance, which is what makes this possible at all

The obvious implementation — push a second `ReaderScreen` — does not fit. A live
chapter peaks at **69,884 bytes** with a largest single allocation of **36,956**
(the inflate window and its tables), against a measured heap floor of **45,840**.
Two live chapters leaves single-digit kilobytes on a part where a failed allocation
is `abort()` with no diagnostic.

It fits because **the Reader beneath can release its chapter and get it back**:

| | Reader holds | Overlay holds |
|---|---|---|
| Reading | `chapter_`, `pb_`, `page_`, `starts_`, `spans_` — ~70 KB peak | — |
| Peek opens | releases `chapter_`'s handles and `pb_`; keeps `chapterAt_`, `at_`, `starts_`, `page_`, `spans_` — a few KB | its own ~70 KB |
| Peek closes | `reopenChapter` + `seekTo(at_)` | freed |
| Commit | re-seeks to the peeked cursor instead | freed |

**Only one live chapter at any moment, so the floor never moves.** Three existing
facts carry it, and none of them had to be invented:

- `ChapterReader` holds everything behind `unique_ptr` — `file_`, `bufSrc_`,
  `inflated_`, `blocks_` — and `InflateStream` keeps its window and tables as a
  single `Scratch*`. Releasing a chapter's whole footprint is resetting pointers.
- **`pb_` is already nullable.** Its own comment says "Null when the stream is not
  positioned for a forward turn", and the `Gesture::Next` handler already recovers
  by calling `seekTo(at_)`. Releasing the builder is a supported state, not a new
  one.
- **`page_` is retained through all of it**, with owned `LaidLine` text. That is
  what lets `App::render` draw the Reader underneath the veil without a decode.

Closing costs one `seekTo` — a rewind and forward decode, **33.9 ms desktop for the
worst page in a real book**, comfortably under a single panel refresh.

`reopenChapter` exists for exactly this shape of thing today: "re-establishes a
chapter's stream without touching the index or the page."

### The panel, and what it can honestly say

**There is no board for this and one has to be drawn first.** Nothing in `design/`
shows a panel of book text over a page; the existing overlays (`LibraryActions`,
`DeleteConfirm`) are row lists. Per the project rule the board comes before the
implementation, and this is the largest single design task in the spec.

An inset panel, clearly framed, with the veiled page visible around it. Its text
re-wraps to the narrower column, which means **its pagination is not the book's**,
so it does not show a page number. It shows chapter and percent — `CH. 03 · 12%` —
which is true at any column width.

**Committing is still exact.** `openAtCursor` lands on the page *containing* a
cursor and counts boundaries to name it, so the cursor is what travels and the page
number is computed on arrival. The reader lands on a real book page with a real
number, whatever width they were reading at.

The alternative — a full-width panel in a hairline frame, so that one pagination
serves both — was rejected: it barely reads as an overlay, which leaves the header
and hint bar as the only signals that anything is different.

### Buttons

Sides turn pages within the peek. `Back` closes and discards. `Confirm` commits:
the Reader re-seeks to the peeked cursor, and **the anchor is set to where the
Reader was** — the departure point, not the destination — overwriting any anchor
already standing. Hint bar `CLOSE` / `GO HERE` / — / — in the boards' hardware order
(Back, Confirm, Up, Down), the last two as `kHintEmptySlotW` dead slots.

The peek declares **`Fidelity::Grayscale`**, for the reason the Reader does: this is
body text at reading size, and hard-thresholding a serif face at 32px was judged
worse. It is therefore painted the same way — one fast waveform, refined in the
quiet window — and it is why `renderTopOnly` cannot apply.

### Cost

A peek page turn is a full-stack render: the Reader beneath from its retained
`page_` (~110–140 ms on device, **no decode**), the veil (0.17 ms, byte-wise), then
the panel. Then one fast waveform at ~520 ms, with the refinement following in its
quiet window. **So a peek page turn costs about what a page turn costs.**

`App::renderTopOnly` cannot help: it never applies to `Fidelity::Grayscale`, and a
panel of body text wants grayscale for the same reason the Reader declares it.

## Risks

**The first overlay over a grayscale screen.** Every overlay today —
`ItemActions`, `DeleteConfirm` — sits over the Library, which is `Mono`. A peek sits
over the Reader, so `veilRect` would be applied once per plane for the first time.
It *should* be consistent — white in `Bw` is paper, and 0 in `Lsb`/`Msb` is coverage
0, also paper — but this codebase's own notes are pointed about the difference
between "should be" and "was measured". It gets a test, not a footnote.

**The overlay has no opener until `Contents` lands.** `Contents` is in progress in a
parallel session and is the overlay's first caller. Until it exists a peek is
reachable from the simulator and the goldens only — which is precisely the state the
Sleep screen sat in for two phases, recorded in `CLAUDE.md` as a trap rather than a
pattern. **The anchor does not share this problem**: its caller is ordinary backward
paging, so it is reachable the day it lands.

**A footer that gains a field can crowd.** Three fields at 480px on the X4 is the
geometry to check, and it is a board question before it is a code question.

## What this does not do

**It does not build `ReaderMenu`, `Contents`, `GoToPage` or `Bookmarks`.** They are
callers. The anchor needs none of them.

**It does not identify characters**, which was the question that started this. Those
decisions are taken and are recorded here so they are not re-argued from scratch:

- **The analysis runs on the device, as you read.** No desktop pre-processing step
  and no network. It therefore only ever sees chapters the reader has read, which
  makes it **spoiler-proof by construction** rather than by a rule someone has to
  remember. It also rides a walk that already happens: the pagination pass decodes
  the whole chapter, so a name scan costs a scan of text already in RAM.
- **The screen says `NAMES`, not `CHARACTERS`.** A capitalization-and-frequency
  heuristic on a 160 MHz part cannot tell a person from a place, and separating them
  needs a per-language word list — a table `core/` has twice refused to carry.
  Boulder appearing in the list is then correct rather than a defect, and "where was
  Boulder again?" is a question readers ask too. Same move as the footer's em dash
  and the bare `CH. 03`: say the true thing rather than the impressive one.
- **An entry shows the sentence that introduced the name**, chosen as the best of
  its first few appearances — preferring a name followed immediately by a comma
  (the appositive, which is how novels usually introduce people), preferring a
  longer sentence, penalising one that is mostly quoted dialogue. All computable
  from the sentence itself; no language table. The plain first mention is often the
  useless one (`Stu was in the back of the room`).
- **It is reached from `ReaderMenu`**, as a row beside Contents and Bookmarks, and
  it is a full cast list with a scroll rail — Library's existing mechanism.
- **Its `Go to page` opens a peek**, which is why this spec exists first.

**And the heuristic has since been measured** — `tools/name_probe.cpp`, run over three
real novels. 25 of the top 26 names in `Le Fléau` are real entities; the cast of
`Neuromancien` and of `Darkly Dreaming Dexter` come out whole, so it is not
French-specific. The list is good; the *introducing sentence* is a coin flip, and
better on surnames than forenames, because the introduction happens at the full name
while the reader is stuck on the forename. The roadmap's 3E entry carries the five
rules and the open question.

**It does not make page numbers book-wide.** The Reader's footer is still
chapter-relative and the peek says nothing about it either way.

## Verification

- **The anchor's state machine, as unit tests.** High-water under continuous
  movement; departure point on a jump, in both directions; satisfied-and-cleared by
  reading back up to it; lexicographic comparison across chapters; dropped at any
  fit grade below `Exact`.
- **The strong property, mirroring the best test already here** (*reading backward
  gives exactly the pages reading forward gave*): **paging back N pages and
  returning lands exactly on the page you left, checked for every page of a
  chapter.** A rule that is off by one is right at page 1 and wrong everywhere
  after it.
- **The release is asserted, not reasoned about.** A test that the Reader's chapter
  footprint is genuinely freed when a peek opens — that claim is what the whole
  memory design rests on, and it is the kind of claim that stays true in a comment
  long after it has stopped being true in the code.
- **A round trip through the sidecar**, including a record written by a firmware
  that did not know about the anchor.
- **The veil across all three grayscale planes**, byte-compared, at both geometries
  and both rotations — the pattern `test_dither.cpp` already uses.
- **Goldens at both geometries**: the Reader with the anchor field and without it,
  and the peek panel over a reading page. The without-anchor golden matters most —
  it is the assertion that the common case is the board exactly as it is today.
- **`make compare`** on `Reader.dc.html` after the board change, and on the peek's
  new board.
