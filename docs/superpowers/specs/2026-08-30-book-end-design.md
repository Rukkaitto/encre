# The end of a book

**Status:** design approved 2026-08-30. Not implemented. Issue
[#6](https://github.com/Rukkaitto/encre/issues/6), `Kind = Screen`, card in
`Boarded`, `Board = BookEnd.dc.html`.

**The roadmap does not mention this screen at all.** CLAUDE.md already records it
as one of two boarded screens the roadmap never named (`BookEnd`, `Boot`), which
is why the card has no `Source` line to point at. Nothing needs correcting; the
absence is the fact.

## What is on the glass today

**Paging forward off the last page of the last chapter is a dead button.**
`Gesture::Next` exhausts the chapter, calls `openChapterAt(chapterAt_ + 1, false)`,
the walk runs out of spine entries, and the branch returns `Action::none()`
(`screen_reader.cpp:1084`). The panel does not move and the log records nothing.
This project has shipped a dead button twice and each time the note beside it was
correct when written; this is the third, and it is the one the board was drawn for.

**And `Mark as finished` on the item-actions overlay is the second half of the same
gap.** `screen_item_actions.cpp:52` returns `Action::none()` behind a comment that
is worth quoting because it names its own expiry date:

> "Finished" is per-book state, and per-book state does not exist:
> `/.reader/state/` has no format yet because there is no Reader to write one.

There is a Reader, `/.reader/state/` has a format (`reading_position.h`), and the
row is still dead. **Both callers are in scope**, because the thing they were both
waiting for is one mechanism and shipping it for one of them would leave the other
dead with no reason left to be.

## The board states two facts this device cannot know

`design/BookEnd.dc.html`'s meta line reads **`890 PAGES · FINISHED AUG 20`** and
neither half is obtainable:

- **A book-wide page count is ~49 s of decode.** 6.94 MB of inflated XHTML for one
  real novel at the measured 7.2 ms/KB. This is the FOURTH slot on the FOURTH board
  to ask for one: Home's CONTINUE block drew `PAGE 53 / 890`, Contents' rows drew
  `P. 21`, and Book details' Progress row drew a page count, and all three were cut
  for this exact number.
- **There is no clock.** Nothing in `core/` or `shell/` reads a wall time; the only
  `RTC` in the tree is a comment about `RTC_DATA_ATTR` in `shell/src/main.cpp:940`,
  which is deep-sleep memory and not a calendar.

**The date half is PARKED, not impossible.** Issue
[#25](https://github.com/Rukkaitto/encre/issues/25) is *X3 bring-up: fuel gauge and
RTC*, and its fuel-gauge half has already landed as the battery work — so a
`FINISHED <date>` line becomes writable the day the RTC half does. It gets a card
then; it does not get a placeholder now, because a field the device fills with
nothing is the shape this file already refuses for an unread gauge (`-1`, not `0%`)
and for a book with no reading position (no demo substitute).

The line becomes **`24 CHAPTERS`**, which `OpenedBook::chapterCount()` already
holds — **spine entries, one of which is usually the cover**, exactly the number Home
already says `OF` in `CH. 08 OF 92`. Not a count of chapters with text: getting that
means paginating every entry, which is the ~49 s this line is being rewritten to
avoid. One device, one spelling of the number — the same free-and-true substitution Home's CONTINUE block made when it
replaced `PAGE 53 / 890` with `CH. 08 OF 92`. **24 and not 92**: `Main.dc.html`
gives the demo Middlemarch `CH. 01 OF 24`, and two boards drawing the same demo
book must agree about it. A merge once left `ReaderMenu.dc.html` disagreeing with
`Reader.dc.html` about the page beneath its veil and `make compare` said `ok`
throughout.

## Three more board fixes, each against a rule already written down

| The board says | It becomes | Why |
|---|---|---|
| slabs `height: 72px` | `68px` | `kActionH`. **Measured across every board that draws the slab**: SdMissing, DeleteConfirm, BookError and WifiError all state 68 and BookEnd is the only one that does not. Pinning 72 here would be a screen-local number for a shared box. |
| `THE END` at `46px` | `var(--t-title)` (42px) | A role is a pre-rendered asset per size AND weight, 15–20 KB of flash each. Sleep's 53px title and HomeEmpty's 39px took exactly this fix; 46px would be a third one-off. |
| `Middlemarch · George Eliot` at `--t-value` with no `font-weight` | add `font-weight: 500` | CSS default is 400 and **there is no `Value400` in the ramp** (`fontset.h:48`). Three boards carry a weightless `--t-value` run — BookEnd, Bookmarks, InstapaperConnect — and none is implemented, so there is no precedent to follow and this sets it. |

**The filled/outlined pair needs no change and no new primitive.** `components.h`
states the rule outright: "Which variant a button gets is the FOCUS, not the
button's identity: the boards fill exactly the slab the focus is on." The board
fills `MARK AS FINISHED` and outlines `BACK TO LIBRARY`, which is the focus resting
on the first slab. `drawActionButton(..., filled = (focus == i))` draws both.

**The width is not shared and must not be.** SdMissing pins 260 and the overlays
take their column; BookEnd's slabs are full usable width (`padding: 0 24px`).

## `finished` is a flag on the sidecar

`ReadingPosition` gains one field and **`kPositionVersion` DOES NOT MOVE**.

```
bool finished = false;
```

**The key is WRITTEN ONLY WHEN TRUE**, which is the anchor's own rule three keys
above it in `serialise()` and it is what makes everything else here work.
`json.h` has `setBool`/`getBool`, so this is a real bool and not an int standing in
for one.

**Why a flag and not `percent == 100`.** Those are different claims. Reading to the
last byte of a book whose final 8% is an appendix, an index and a colophon is not
the same as the reader saying they are done, and a reader who abandons a book in its
endnotes would be silently marked finished. The flag records an ASSERTION; the
percentage records a MEASUREMENT. Conflating them is the shape this file already
refuses where "no books" and "could not look" are kept apart, and "flat" from "did
not answer".

**WHY NOT, AND AN EARLIER DRAFT OF THIS SPEC SAID 1 → 2.** The argument for bumping
was that a downgraded firmware would read a record whose `finished` key it ignores.
It would — and that is BENIGN: the reader's position is intact and the book merely
stops saying `DONE`. The bump's own cost is not benign. `parsePosition` gates on the
version and is `total`, so **every reading position on every card would read as
absent once** and every reader would lose their place in every book, to protect
against a downgrade whose failure mode is a missing label.

`settings.h`'s rule is the one that applies: an added field takes its default from an
older file, and the default is exactly today's behaviour. `kSettingsVersion` has
declined to move three times on this reasoning.

**And writing the key only when true is what makes the record byte-identical.**
`savePosition` goes through `writeIfChanged`, which reads the card back and compares
bytes — so a `finished: false` written unconditionally would change every sidecar's
text and rewrite the lot on their next save, a card write per book to record nothing.
Absent-when-false means an unfinished record is the same bytes it was before this
feature existed, which is precisely why the anchor's three keys are written that way.

**READING THE BOOK AGAIN IS WHAT UN-MARKS IT**, and this has to be stated because
it is otherwise decided by accident: `savePosition` writes the whole record, so
whether a reopened book stays finished depends on whether the shell builds that
record fresh or loads and preserves. **It builds fresh, so a position save from the
Reader clears `finished`.**

The alternative is a sticky flag, and there is no board for a toggle — so a book
marked finished would be finished forever, which is worse than the cost of clearing.
**That cost is real and is stated rather than discovered**: opening a finished book
and immediately leaving it also fires a save and also clears the flag. It is
recoverable in two presses from the item-actions overlay, and it is visible — the
Library row changes — which is the difference between this and a silent loss.

`ProgressEntry` carries `finished` too, so the Library learns it from **the one
listing it already does** — `loadProgressIndex` is one listing plus one read per
book STARTED, and adding a second pass over the card for this would undo the whole
reason that function is shaped the way it is.

## What the Library draws

**THE FINISHED ROW IS ALREADY BOARDED, AND ITS WORD IS `DONE`.**
`Library.dc.html` draws five rows — `6%`, **`DONE`**, `48%`, `NEW`, `31%` — and
`LibraryRow::value`'s own comment has said `"6%", "DONE", "NEW"` since the screen
landed. So this design does NOT get to pick the copy, and `FINISHED` (which is what
this spec said before the board was read properly) would have been invented copy
overriding a state the design already had.

**Nothing on the board changes and no Library golden re-blesses.** `demoLibraryItems()`
already types `"DONE"` straight into Jane Eyre's row, so the board, the golden and the
comparison sheet have agreed about this state all along. **What is missing is only the
DEVICE path**: `applyProgress` — the ONE spelling of the derivation, shared by the scan
and by `refreshProgress()` so the two cannot disagree — has no way to produce `DONE`,
because the flag it would read did not exist. It gains one branch.

That is the whole Library change: a finished book reads `DONE` where an unfinished
started book reads its percentage and an unopened one reads `NEW`.

**This removes churn this spec previously accepted.** An earlier draft planned board
edits and four re-blessed PNGs on two screens unrelated to this issue, and warned about
them. Reading the board rather than assuming what it must say deleted all of it — which
is the design-first rule paying for itself in the direction it is least often credited
for, by making the change SMALLER.

Book details' Progress row reads the same index and is **left alone in this change**.
`BookDetails.dc.html` states no value for it at all, so unlike the Library there is
nothing boarded to follow, and a row invented in code is the thing CLAUDE.md's first
rule forbids. It gets a card.

## Reaching the screen

**`walkToChapter`'s bool conflates three outcomes** and only one of them is the end
of a book: nothing to page into (no `fs_`, no book, no body — the in-memory demo
Reader), the walk ran out of spine entries, and `chapter_.begin` failed on a corrupt
local header. Pushing `BookEnd` on a bare `false` would put "THE END" on the glass
for an archive fault, which is a wrong claim of the kind this project keeps paying
for — and it would fire on the demo Reader in the simulator.

So it reports which:

```
enum class WalkResult : uint8_t { Landed, RanOff, Failed };
```

`openChapterAt` propagates it. `Gesture::Next` pushes `BookEnd` on **`RanOff` going
forward only** and keeps `Action::none()` for `Failed`, which is today's behaviour
unchanged. Running off the FRONT of the book stays `none()`: there is no board for
the beginning of a book and no reason to invent one.

**The trailing-empty-chapter case falls out for free**, and it is the case a naive
`chapterAt_ + 1 >= chapterCount()` test gets wrong. A real EPUB's last spine entries
can paginate to nothing — `Le Fléau` has three such entries and spine 0 is a cover
with one `<img>` — so "is the next index in range" is not "is there another page".
The walk is what knows, because skipping empty candidates is what it does.

**`BookEnd` is PUSHED over the Reader, not swapped for it.** That is what makes the
board's `BACK` slot mean something — it returns to the last page, so a reader who
wanted to re-read the ending can. It also keeps the book open, which the
`MARK AS FINISHED` slab needs.

## Two slabs, two actions, and storage stays in the shell

`core/` does no storage, so marking follows `Retry` and `Open` exactly: the screen
asks, `App` latches, the shell answers.

```
Action::finish()   ->  App::finishRequested()
```

**It carries no path, for the reason `Action::open()` carries none**: a `std::string`
in every `Action` returned by every gesture on every screen, to serve one kind. Two
screens ask and mean different books — BookEnd means the open book, ItemActions
means the Library's focused row — and the shell resolves it exactly as `handleOpen`
already resolves `Action::open()` for the same two-caller shape.

**MARK AS FINISHED** → `finish()`. The shell:

1. loads the book's position (or builds a minimal record — `bookPath`, `bookBytes`,
   `finished` — for a book marked finished from the Library that was never opened,
   which is a legitimate thing for a reader to assert),
2. sets `finished`, saves,
3. **clears `last.json` only if it names THIS book.** Unconditionally clearing it
   would take an unrelated book off Home's CONTINUE block, which is a different
   book's state destroyed by this book's button,
4. sets `gHomeStale` and `gLibraryStale` — both, and separately, because they are
   consumed when their own screen is reachable and one shared flag lets Library,
   Back, Home clear it before Home uses it,
5. leaves the book.

**A failed save is logged and not fatal.** `writeAll` calls `noteCardGone()` on a
write that fails after opening, which `pollCardPresence` turns into an App rooted at
`SdMissingScreen` — so treating it as fatal would throw a reader out of a book they
can still read, over a flag. `reading_store.h` states this hazard for `savePosition`
already and it applies unchanged.

**BACK TO LIBRARY / BACK TO HOME** → `popTo(Library)`, which "stops at the root if
`target` is not on the stack" — so it always leaves the book, landing on the Library
when one is beneath and on Home when the reader arrived through CONTINUE.

**The label follows the stack and is a view-model string.** The button always works;
only its name could be wrong, and a slab that says LIBRARY and lands on Home is the
`About this book` shape — a control correct in the common case and quietly wrong in
the other, which nobody can learn. The shell fills it from what is actually beneath
the Reader, the way it already fills the reader menu's header rather than having an
overlay reach down the stack. **Precedent for text varying within a screen exists**:
Settings' Confirm hint is `OPEN` on the Typography row and `CHANGE` on the device
rows.

**The board states `BACK TO LIBRARY` and gets no second board.** `SleepWaking.dc.html`
is the precedent — one run differing between two states "is a NOTE and nothing else",
needing no field, no flag on the theme and no second render path.

**ItemActions' `Mark as finished`** → `finish()`, then the overlay pops. In place,
which is what its board's missing chevron promises: "Open and Book details lead
somewhere, Mark as finished and Delete... act in place."

## The screen itself

`ScreenId::BookEnd`, **appended**. The session record stores a screen by NAME
(`session_record.h`) so an insertion could not silently become another screen, but
appending also leaves every existing ordinal where it was.

**The `static_assert` in `test_focus_restore.cpp` WILL bite, and CLAUDE.md says it
will not.** That file records the guard comparing against a named member rather than
the last one, so an append satisfied it unchanged — issue
[#42](https://github.com/Rukkaitto/encre/issues/42). Read today, `test_focus_restore.cpp:49`
names `ScreenId::Peek`, which IS the last member, so appending `BookEnd` fails the
build until both `kAllScreens` and the assert move. **The code is the authority and
the note is stale for at least the append case.** #42 is not closed by this work and
is not touched by it.

`BookEndScreen : public FocusScreen(2, 2)`. Two focusable slabs, `Focus` clamped to
the pair, wrapping as every list here wraps. `Fidelity::Mono` — chrome, one waveform,
and fidelity comes from the top screen so the `Grayscale` Reader beneath costs
nothing while this is up. Not an overlay: the board is a full screen with its own
header band and hint bar, so it clears the frame.

Hints: `BACK` / `SELECT` / `UP` / `DOWN`, no holds, so no slot shows a ring.
`Gesture::Back` is `Action::pop()` — back to the last page of the book, which is the
whole reason this screen is pushed rather than swapped in. `Up`/`Down` move between
the two slabs and **wrap**, as every list here wraps; on a two-row screen a wrap is
unambiguous, which is the case CLAUDE.md already calls settled for a four-row
overlay.

**It is not restorable across a wake, and that is deliberate rather than an
oversight.** The factory refuses an unprimed `BookEnd`, so `App::restore` stops early
and leaves the Reader standing on the last page — which is the calmer place to wake
and is the same call the peek makes. A refused push is wrong in a way the reader can
see through. Issue [#49](https://github.com/Rukkaitto/encre/issues/49) covers the
general problem that being restorable is per-screen tribal knowledge; this screen
does not make it worse.

**What the factory is handed**, following the `setDetailsFacts` shape rather than a
reach into the Reader: title, author, chapter count, and the second slab's label.
`setBookEndDemo()` for the board's own content, **asked for and never substituted** —
the rule `setReaderDemo`, `setContentsDemo` and `setPeekDemo` each established after
a silent substitution turned a diagnosable failure into a puzzle.

## One new icon

`kCheck`, from this board's own checkmark: `viewBox="0 0 16 12"`, `stroke-width="2"`,
rendered at 26×21. `iconc.py` gains a row keyed on the path `M1 6l5 5L15 1`, which is
unique within `BookEnd.dc.html` — the board's other four SVGs are the hint bar's.

**It is a thin diagonal, which is the one shape `Mono` treats worst.** CLAUDE.md:
`kChevron`'s stroke "is mostly coverage-1 pixels, so it comes out one notch lighter
and much crisper. Diagonals are where to look if a mark ever reads too faint." At
26×21 the stroke resolves to ~3.25px against the chevron's thinner one, so it should
survive — but **that is an argument and this project has been wrong about this panel
from desktop evidence three times.** It goes in the on-glass list.

## Testing

- **Goldens at both geometries**, 480×800 and 528×792. `Fidelity::Mono`, so
  `golden::checkGolden` over `Plane::Bw`, and the test asserts the fidelity before
  naming the plane so a change to the shipped path fails rather than leaving the
  golden pinning a path nothing paints.
- **No Library golden moves, and that is an assertion rather than an expectation.**
  `demoLibraryItems()` is unchanged, so `library` and `library_scrolled` must be
  byte-identical at both geometries after this work. A moved Library golden means
  `applyProgress` has started overriding demo values that were typed in directly, which
  is a real bug this would otherwise hide.
- **A unit test that `applyProgress` yields `DONE` for a finished record**, `NEW` for an
  unstarted one and a percentage otherwise — the device path the goldens cannot reach,
  because the demo rows never go through it.
- **The `RanOff` / `Failed` distinction gets its own cases**, because a bool that
  conflated them is what the enum replaces and a test that only checks "the end of
  the book pushes BookEnd" cannot see a corrupt chapter pushing it too.
- **A trailing chapter that paginates to nothing** — the case a range test gets
  wrong. `fake_fs.h` serves a real EPUB, so this is buildable on the desktop.
- **`finished` round-trips through `serialise`/`parsePosition`**, AND a record with
  `finished == false` serialises to bytes IDENTICAL to one from before the field
  existed. That second assertion is the one that matters — it is what keeps
  `writeIfChanged`'s `Unchanged` answer true and stops a card write per book.
- **A pre-feature record parses**, with `finished` defaulting false rather than the
  parse failing.
- **`last.json` is cleared only when it names the marked book** — the assertion that
  distinguishes this from the unconditional version.
- **`test_focus_restore.cpp`** gains `BookEnd` in `kAllScreens`, the assert moves to
  name it, and both hand-maintained counts (`movable`, `wrapping`) go 8 → 9.
- **Every new golden and every re-bless is proved by MUTATION before it is trusted.**
  A golden that passes without biting is worthless, and this file records four
  distinct ways a mutation lies — check the mutation LANDED, commit before mutating,
  and never `git checkout` to undo one.

## What only the panel can answer

1. **Does `kCheck` survive `Mono` thresholding**, or does it read faint the way
   `kChevron` does. The desktop cannot tell; the argument above is geometry, not
   evidence.
2. **Does `THE END` at 42px carry the weight the board's 46px intended**, on glass at
   220 PPI. If it does not, the fix is a new role plus its asset — real work, and not
   worth it until the panel says so, which is the same call Sleep's 53px title got.
3. **Does the screen actually appear** at the end of a real book on the card, rather
   than at the end of a chapter — the trailing-empty-spine-entry case is desktop
   testable but the card is where it is real.

## Stated limits

| limit | why |
|---|---|
| No `FINISHED <date>` | no RTC; parked on #25 |
| Book details still shows a percentage for a finished book | its board states no value at all; gets a card |
| `BookEnd` does not survive a wake | refused unprimed, by design; wake lands on the last page |
| Opening a finished book and leaving clears the flag | a save from the Reader builds the record fresh; recoverable in two presses, and visible |
| `24 CHAPTERS` counts spine entries, cover included | the same number Home already says `OF`; a text-chapter count is the ~49 s walk |
