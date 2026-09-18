# The peek

Extracted from `CLAUDE.md`, which keeps a stub under this heading and is where
the cross-references to it point. Same standing as anything in that file.

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

**IT IS NOT RESTORABLE ACROSS A WAKE, AND #49 MOVED THAT FROM AN ACCIDENT TO A
DECLARATION.** It used to rest on the factory refusing an unprimed `Peek`, so
`App::restore` stopped early and left the Reader standing — true, and indistinguishable
in a log from a screen nobody remembered to prime. `restorability(ScreenId::Peek)` is
`Restore::Never` now, so **`snapshot()` stops before the peek and the record never names
it**: the wake reports a COMPLETE restore onto the page instead of a short one. Persisting
a peeked cursor would still be a card write for a breadcrumb the anchor's own design
declined to pay for.

**RE-ASKED ON GLASS AND CONFIRMED (2026-08-29), so it does not need arguing again.** It
was reported as a defect — "sleeping in the peek takes us back to the book" — and it is
not one: a peek is a transient *am I sure?*, and waking onto your own page is the calmer
default. **The reason given above is weaker than the decision, and that is worth knowing
if it is ever revisited**: the session record USED to store the entry
(`home:0;library:2;reader:0;peek:0`), and that trailing `0` is a focus slot the peek has
no use for, so the peeked SPINE could have ridden there for no new card write at all. The
cost was never the storage; only the peeked *page within the panel* would need one. So the
honest statement is that a peek should not come back, not that it cannot. **#49 has since
made the record stop naming it**, so anyone revisiting this now has to undo a declaration
rather than just read a field — which is the right cost for reversing a decision taken on
glass, and is why the argument is kept here rather than deleted.

**AND THE REPORT WAS RIGHT ABOUT THE MECHANISM even though it was wrong about this
screen** — which was #49, and #49 is answered. Being restorable WAS per-screen tribal
knowledge: one hand-written `namesReader` scan on the wake path primed the book, and the
reader menu and Contents were primed only because `openBookAt` passed them on the way.
Three screens shipped un-restorable by accident and this one is un-restorable on purpose,
and **from the outside those were indistinguishable** — the restore stopped early and the
reader landed somewhere they did not expect. Every `ScreenId` now declares which of the
three it is, behind a `static_assert` on `ScreenId::Count` that a new screen cannot pass
unchanged; see **A factory that substitutes content is worse than one that refuses**.

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
