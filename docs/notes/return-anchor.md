# The return anchor

Extracted from `CLAUDE.md`, which keeps a stub under this heading and is where
the cross-references to it point. Same standing as anything in that file.

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
