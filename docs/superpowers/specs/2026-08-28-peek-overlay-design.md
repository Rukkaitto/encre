# The peek overlay — a page of the book over the veiled page

**Status:** design approved 2026-08-28. Not implemented. Issue
[#1](https://github.com/Rukkaitto/encre/issues/1), `roadmap:832`.

**This is the second half of `2026-08-24-peek-and-return-design.md`.** That spec
designed two mechanisms — the *return anchor* and the *peek overlay* — and only the
anchor was built (#2, closed). This one builds the overlay, and it takes two
decisions the earlier spec got wrong or could not have taken, both marked
**CORRECTS THE 08-24 SPEC** below. Everything not restated here still stands: the
memory argument, the board, the button bindings, the reasons the panel is inset and
therefore cannot show a page number.

**The first caller is chapter selection.** The 08-24 spec listed four callers —
`Contents`, `GoToPage`, `Bookmarks`, `Names` — and predicted the peek would be built
before any of them. It was not: `GoToPage` has since been cut (a reflowable book has
no stable page to go to), and `Contents` **shipped without the peek**, jumping
straight to the chapter. So the peek arrives fourth in the list and first in
practice, with a shipped screen to rewire rather than an unbuilt one to wait for.

## What changes for the reader

Pick a chapter in Contents today and you are there, on page one, with an anchor
recording where you left. Pick one after this and you get **a panel of that
chapter's text over your own page, still visible under the veil**. `GO HERE`
commits the jump; `CLOSE` puts you back on your page, which never moved.

The anchor is what made the jump *safe*. The peek is what makes it *cheap to be
wrong about* — you can look at a chapter without spending a jump on it.

## The flow, and the one thing that does not change

**`ContentsScreen` is untouched.** Its `Activate` still answers
`Action::popTo(ScreenId::Reader)`, and the shell still reads `chosenSpine()` while
Contents is on top — before the dispatch, because the choice is made by the press,
and not after, because the pop destroys the screen that knows it. That hand-off
(`gPendingSpine`) already exists and already has the ordering right.

What changes is one branch in the shell: after the pop, prime the factory with the
target spine and **push `ScreenId::Peek`** instead of calling `goToChapter`.

The pop takes the reader menu off with Contents, so the topmost non-overlay is the
Reader and `App::render` draws the reading page, veils it, then draws the panel —
which is `Peek.dc.html` exactly as authored.

| | |
|---|---|
| `isOverlay()` | true |
| `fidelity()` | `Grayscale` — body text at reading size, for the reason the Reader declares it |
| focus | none. Not a `FocusScreen`: there are no rows |
| `Back` | `Action::pop()` — close and discard |
| `Confirm` | commit; see **`GO HERE`** below |
| `Left` / `Right` | page inside the panel |
| `Up` / `Down` | unbound, drawn as `kHintEmptySlotW` dead slots |

`Up` and `Down` are dead **on purpose and the board argues it**: `Up` already means
"return to where I was" on the screen underneath, and one button with two meanings
across a single press is worse than an unbound one. The side buttons page in the
peek exactly as they do while reading.

### It is not restorable across a wake — CORRECTS THE 08-24 SPEC

Which said nothing about the session record, and the machinery has an opinion.

The factory refuses an unprimed `Peek`, the way it already refuses a Reader with no
book — so `App::restore` stops short of it and **leaves the Reader standing**, which
is the existing "a restore that stops early keeps what already stands" behaviour and
needs no new code path.

The alternative is persisting the peeked cursor so a wake can rebuild the panel, and
that is a card write for a breadcrumb the 08-24 spec explicitly declined to pay for
when it decided the *anchor* would ride the existing save edges. A peek is a
transient excursion; waking into one is not a thing to engineer toward.

`Peek` is added to `test_focus_restore.cpp`'s `kAllScreens` regardless — that test
walks every `ScreenId` and its `static_assert` guard is the hole #42 records, so a
new screen has to be put in the list by hand until that is fixed.

## The engine: `PeekScreen` owns a `ReaderScreen`

**The panel is inset, so its column is narrower, so its text re-wraps** — ~368px of
measure inside the 412px panel against the Reader's 444. That is the whole reason
the panel cannot show a page number, and it is also why the peek cannot reuse the
Reader's already-laid `page_`: it needs its own pagination over the peeked chapter.

So `PeekScreen` holds a **headless `ReaderScreen`**, built at `Theme::peekMetrics`
with `setPageCacheDepth(1)`, opened at the chosen spine. `Left`/`Right` are
forwarded into its `onGesture`; the panel reads back `page()`, `chapterIndex()`,
`vm().chapter` and `currentCursor()`.

**Three alternatives were considered and this is the one with no second copy.**

- *A bespoke minimal pager* — its own `ChapterReader`, `PageBuilder` and a vector of
  the page starts actually visited. Smallest memory, ~200 lines, and no index, count,
  ring or idle jobs, none of which a screen with no page number wants. Rejected
  because it is a second copy of open/advance/seek: a backward turn and a chapter
  crossing inside the peek would re-derive logic that took this project several
  passes to get right, and the copies would each pass their own tests.
- *Extracting a `ChapterPager` both screens use* — where this project's own
  second-copy rule points. Rejected on size: `ReaderScreen`'s paging is entangled
  with the page ring, the growing index, byte accounting for the percentage, the
  anchor and two idle jobs, so this is a large refactor of the most
  performance-critical and most-tested code in the repo, for a screen that wants a
  fraction of it. It stays available if a third pager ever appears.
- *Reusing `ReaderScreen`*, which is this. It costs ~2–3 KB of duplicated
  `OpenedBook` spans and chapter names, and it is a `Screen` used as a model, which
  is unusual. What it buys is that **the cursor the peek commits is by construction
  the one the Reader restores** — there is no second spelling of a page position to
  disagree with the first.

Two properties fall out of it rather than being arranged:

- **Paging off either end of the peeked chapter crosses into the next or previous
  one**, because `openChapterAt` already does that, including skipping an entry that
  paginates to nothing. That is what "the side buttons page in the peek exactly as
  they do while reading" has to mean.
- **The inner reader is invisible to the idle jobs.** `completeIndex`,
  `warmPageRing` and `restreamAtCurrentPage` are driven by the shell through
  `readerOnStack(*gApp)`, which walks the App's stack — and the inner reader is not
  on it. So the peek never runs a deferred count for a total it does not display.

**The inner reader has its own `ReturnAnchor` and it is discarded with it.** Paging
around inside the peek moves nothing the reader can come back to; the *outer*
Reader's anchor is touched by exactly one thing, which is `GO HERE`.

## The memory dance

Two live chapters do not fit: a chapter peaks at **69,884 bytes** with a
**36,956-byte** single allocation, against a measured heap floor of **45,840**. So
the Reader beneath **releases its chapter while the peek is up**.

`ReaderScreen` gains `releaseChapter()` / `reacquireChapter()`. The release drops
`ChapterReader`'s stream and keeps `chapterAt_`, `at_`, `starts_`, `page_` and
`spans_` — a few KB. It is safe because **`ReaderScreen::render` reads only `page_`
and `vm_`**: the veiled page draws with the chapter gone and no decode at all.

### The 36,956 bytes are NOT behind a `unique_ptr`, and this spec said they were

An earlier draft of this section said the release "resets `ChapterReader`'s four
`unique_ptr`s (`file_`, `bufSrc_`, `inflated_`, `blocks_`)", on the roadmap's own
line that all of `ChapterReader` is behind `unique_ptr` so releasing it is resetting
pointers. **Four of the five are. `inflater_` is a value member**
(`chapter.h`), and the whole 36,956 bytes live behind *its* private
`Scratch* s_` — `inflate_stream.h:166`, commented "the one allocation" — freed by
`~Inflater` and by nothing else. `inflated_` is only the ~40-byte `InflateSource`
wrapper around it.

So resetting those four pointers frees a `BlockReader`, a wrapper, a buffer view and
a file handle, and **keeps every byte this feature exists to give back**. The release
therefore calls a new `Inflater::release()`, and `ChapterReader` exposes
`inflateWindowHeld()` beside `held()`.

**TWO OBSERVATION POINTS, BECAUSE NEITHER OF THE OBVIOUS ONES CAN SEE THE WINDOW.**
`held()` reads `blocks_`; `bytesRead()` gates on the `InflateSource` pointer. Both go
false when the four pointers are reset, so a release that freed nothing would satisfy
both — and the version of this design that shipped in the plan would have passed its
own tests while the peek allocated its second chapter on top of the first. Proved by
mutation rather than argued: skipping `inflater_.release()` fails
`inflateWindowHeld()` twice and leaves `bytesRead() == 0` passing.

**The fixture has to be DEFLATED for any of it to mean anything.** A stored entry has
no inflater at all, so both observation points answer "released" before the release
and every assertion is `0 == 0` — the shape this file records twice elsewhere. The
test asserts its own premise (`REQUIRE(book.locate(0).deflated)`) rather than
assuming it.

**One live chapter at any moment, so the floor never moves.**

| | Reader holds | Peek holds |
|---|---|---|
| Reading | chapter, builder, index, page — ~70 KB peak | — |
| Peek opens | index, page, spans — a few KB | its own ~70 KB |
| `CLOSE` | reacquires the chapter; see below | freed |
| `GO HERE` | reacquires and walks to the peeked cursor | freed |

### `CLOSE` does no `seekTo` — CORRECTS THE 08-24 SPEC

Which budgeted closing at "one `seekTo` — **33.9 ms desktop for the worst page in a
real book**, comfortably under a single panel refresh".

**That is the ratio trap CLAUDE.md records three times.** A `seekTo` rewinds and
decodes forward, so it costs *what page you are on*, and it is not render-bound: it
is SdFat reads on the display's SPI bus plus an inflate on a part with no FPU. The
device measured **~376 ms at page 38 and ~1010 ms at page 99** of one chapter, and
~3 s deep in a long one. Putting that on `CLOSE` would make discarding a peek cost
more than committing one, on the press where the reader expects to be back
instantly.

It is not needed. `page_`, `at_` and `starts_` were never disturbed, so the page
goes back on glass with no decode. What a `seekTo` would restore is the **live
builder** — and `pb_` being null is an already-handled state whose repair already
has a home: `restreamAtCurrentPage`, which runs in a quiet window and whose
abandonment is free precisely because it only ever runs when `pb_` is already null.
That machinery landed after the 08-24 spec was written.

So `CLOSE` is `reacquireChapter()` and nothing else. The first forward turn after it
either hits the page ring or pays a `seekTo` — exactly as after any abandoned count,
which is the common path already.

### `GO HERE` does pay a walk, and should

Reacquire, `openChapterAt(spine)`, `openAtCursor(cursor)`, then `anchorJumped(from)`
with the Reader's **pre-departure** position — the departure point, not the
destination, overwriting any anchor already standing.

That is a chapter crossing's cost, paid on a press where the user has asked the
screen to change, which is where this project already puts a walk (the typography
apply takes the same trade).

It needs one new public entry point on `ReaderScreen`: a **cursor-granular jump**,
where `goToChapter` lands on page one and `goToAnchor` deliberately does not touch
the anchor. The page number the reader arrives on is *computed* by `openAtCursor`
counting boundaries, which is why the peek can be honest about not having one and
the commit can still be exact.

### Nothing else touches the released chapter, and that was checked rather than assumed

An earlier draft of this section said the three idle jobs "must decline" because
`readerOnStack` finds the Reader *under* the peek. **That is wrong about the
mechanism.** All three — `completeIndex`, `restreamAtCurrentPage`, `warmPageRing` —
are gated on `gApp->top().id() == ScreenId::Reader`, not on `readerOnStack`, so a
peek on top stops all three by construction and no new gate is needed.
`readerOnStack` has three callers and none of them is an idle job: the book-closed
check, which reads nothing; the page-ring shrink; and the Typography apply, which is
unreachable while a peek is up because the pop that opened the peek took the menu
with it.

**A save while released is also safe, and for a reason worth stating** because the
obvious reading of it is alarming. `saveReadingPosition` reads `chapterIndex()`,
`currentCursor()`, `vm()`, `anchor()` and `chapterBytesRead()` — and
`chapterBytesRead()` returns `pageBytes_`, a plain member of the screen, **not**
`ChapterReader::bytesRead()`, which would answer 0 with `inflated_` released and
would push `progressPercent` onto its page/pageTotal fallback. That is the exact
shape of the percentage-going-backwards bug. It cannot happen here because every
field a save reads is retained state. In practice no save fires anyway: with the
peek on top, `saveReadingPosition`'s own `top().id()` guard answers `Unchanged`,
including on the sleep edge — correct, since the Reader has not moved.

So there is **no new gate**, and `ReaderScreen::hasChapter()` exists as an
*observation point* rather than a guard: it is what lets the release test assert the
release happened, which is the claim the whole memory design rests on. The two
paragraphs above are recorded because "no gate needed" is a claim that could
silently stop being true — if an idle job is ever re-gated on `readerOnStack`, it
needs `hasChapter()` in front of it.

## Failure

The only real failure is the card going, and `pollCardPresence` already owns that
case. If the peek's push is refused the reader is left on their own page with the
list gone — nothing lost but the list, the page untouched and the anchor unmoved.

A cover chapter selected from the contents is not a failure: `openChapterAt` skips
an entry that paginates to nothing, continuing in the direction it was going, which
is the behaviour the Reader already has and is right here too.

## The board

`design/Peek.dc.html` exists and is approved; **nothing about it changes.** Two
things it states that the implementation must derive rather than pin, because they
are the invariant this project breaks most often:

- **34px of veil either side is the intent; 412 is not a number to keep.** The board
  is authored at 480 and centred both ways so one board serves both panels; the
  firmware derives the panel width from the canvas.
- **The panel's height is a RESULT of its line count**, the way `headerBandHeight()`
  and `hintBarHeight()` are results. Eight lines is the design — content-sizing alone
  ran to eleven and read as a bordered full screen rather than a modal — so the
  firmware picks the line count and the height follows.

New theme surface: `Theme::peekMetrics(...)` returning the panel's `PageMetrics`,
and `renderPeek(...)`. **No new icons** — `CLOSE` is `kBack` and `GO HERE` is `kDot`,
both shipped.

## Verification

**The issue's named risk first.** `veilRect` **across all three grayscale planes**,
byte-compared, at both geometries and both rotations — the pattern
`test_dither.cpp` already uses. Every overlay today sits over the Library, which is
`Mono`; this is the first over a `Grayscale` screen, so `veilRect` is applied once
per plane for the first time. It *should* be consistent — white in `Bw` is paper,
and 0 in `Lsb`/`Msb` is coverage 0, also paper — but this project's notes are
pointed about the difference between "should be" and "was measured".

Then:

- **The release is asserted, not reasoned about.** The Reader's chapter footprint is
  genuinely freed when a peek opens. That claim is what the whole memory design
  rests on, and it is the kind of claim that stays true in a comment long after it
  has stopped being true in the code.
- **`CLOSE` leaves `page_`, `at_` and `starts_` byte-identical**, and
  `hasLiveStream()` false — the two halves of "nothing visible changed and the
  restream has something to do".
- **`GO HERE` lands exactly**: peek at cursor C, commit, and the Reader's
  `currentCursor()` is C while the anchor is the departure point. Checked across a
  chapter boundary, since that is the case a page-number-based scheme would fail.
- **The peek's page is not the Reader's page.** The narrower column re-wraps, so a
  test that the two disagree is what pins the panel to its own pagination rather
  than to a copy of the Reader's.
- **Goldens at both geometries** for the peek over a reading page.
- **`make compare --only peek`.**
- `Peek` in `test_focus_restore.cpp`'s `kAllScreens`.

**And every one of these gets proved by mutation before it is believed.** This
project has three recorded ways a mutation lies — it lands on a line the input never
reaches, the fixture cannot reach the branch, or the rebuild did not happen — and the
veil test in particular is the shape where a desktop pass means nothing: all 40
simulator PNGs are `Rotation::None`, so only a rotated byte-identity case stands
between a transposed veil and the glass.

## What this does not do

**It does not build `Bookmarks` (#3) or `Names`.** They are the peek's second and
third callers and neither is blocked by anything here.

**It does not give the peek a page number**, for the reason the board argues: the
panel is inset, so its pagination is not the book's. `CH. 01 · 4%` is true at any
column width.

**It does not change the anchor's rules.** They shipped with #2 and `GO HERE` is an
ordinary jump under them.
