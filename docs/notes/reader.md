# The reader

Extracted from `CLAUDE.md`, which keeps a stub under this heading and is where
the cross-references to it point. Same standing as anything in that file.

Six layers, each one ignorant of the next. The boundary is the point: every one of
them refuses bad input with a reason rather than aborting, because all of it is
bytes off somebody's card.

| Layer | Holds | Does NOT know about |
|---|---|---|
| `inflate_stream.h` | raw DEFLATE in bounded chunks, a 32 KB window | zip, files |
| `zip.h` | the central directory, entry reads, `EntrySource` | XML, EPUB |
| `xml.h` | a pull tokenizer over a `ByteSource`, entities | nesting, EPUB, documents |
| `epub.h` | container, OPF, spine order | XHTML content |
| `document.h` | blocks, one at a time (`BlockReader`) | pixels, fonts, columns |
| `layout.h` | pages from a block stream (`PageBuilder`), justification | `Framebuffer`, themes |
| `chapter.h` | the whole chain, positioned (`ChapterReader`) | screens, pages |

**EVERY LAYER IS A STREAM, and that is what made a real book openable.** Under 3B
each held its whole input: `Le Fléau`'s longest chapter is 315,852 bytes of XHTML
and 228,849 of blocks, and holding both at once was a **546 KB peak** against ~142 KB
free. Only 62 of its 92 chapters could be opened. Measured after 3C, over the same
book: **69,884 bytes peak for any chapter, largest single allocation 36,956** — the
inflate window and its tables, which is the only sizeable one left. All 92 chapters
open; the whole book is 8,114 pages.

`inflate.h` (the one-shot form over stb) is still there and still used for the small
things — an OPF, a `container.xml`. It is not the reader's path.

`book.h` is the seam that joins them to the filesystem, and it lives in `core/`
**because `shell/` has no test harness** — five bugs have hidden there. "It needs a
filesystem" is not a reason to be untestable: `FileSystem` is an interface and
`fake_fs.h` serves real EPUB bytes through it. **It hands back a LOCATION, not a
chapter** — a path and three numbers — so the archive, its central directory and the
OPF's parse are all released before a block is read, and re-reading the chapter for
a backward page turn needs no central directory at all.

### Why the decoder is ours

stb_image's zlib is one-shot: whole input in, whole output out. There is no way to
get chunks from it, and **every** route to bounded memory needs them — including
inflating to a temp file, which needs incremental output to write incrementally. So
the choice was ours-versus-miniz, and ours won on the grounds 3B's parsers did, plus
one: it removes stb's **6,608-byte single stack frame**, which is 41% of the loop
task's stack and had already panicked the device.

Three things about it worth keeping:

- **The 32 KB window is the format, not a choice.** A DEFLATE match reaches 32,768
  bytes back, so a decoder that does not hold its whole output must retain that much
  of it. That sets the floor for the whole design.
- **Everything big is in ONE heap block**, and that was measured rather than assumed.
  With the window on the heap but the input buffer and Huffman tables as members, an
  `Inflater` declared as a local cost **6,336 bytes of stack** — barely better than
  the thing it replaced, for exactly the same reason. One `Scratch` brings it to
  3,072. A `static_assert` ties the documented `kHeapBytes` to `sizeof(Scratch)`.
- **Canonical counts-and-symbols tables** (zlib's `puff.c` form), ~600 bytes each
  against stb's ~2 KB. That is the whole frame difference.

Validated byte-for-byte against the one-shot decoder that shipped: 216 entry-passes
over `Le Fléau` (every deflated entry, at 2048-byte grain **and one byte at a time**)
and 3,600 over the 200 generated EPUBs, zero disagreements, 8.3 MB decompressed.
**Grain 1 is the load-bearing case** — a source that satisfies every read hides every
resumption bug there is.

### The two things a real book taught the reader

Both were found by flashing, and both looked like the same symptom — a blank page
reading `0 / 0`.

**SPINE ENTRY 0 IS THE COVER.** `Cover.html` in a real EPUB is one `<img>` and no
body text, and `document.h` drops images, so it paginates to NOTHING. Three of
`Le Fléau`'s 92 entries do. Skipping to the first entry with text would only have
moved the dead end to the bottom of that chapter, so `ReaderScreen` owns the BOOK:
paging off the end of a chapter opens the next, off the top opens the previous one's
LAST page, and an entry with no pages is skipped in whichever direction the reader
was already going. Locating a chapter costs a reopen and a directory parse, ~76 ms
on device, against a ~520 ms refresh.

**THE LABEL IS THE CHAPTER'S NAME**, from `toc.h`, and it was a spine position for two
phases because the spine gives an order and no names. This paragraph tracked that in
three states — first "the position is the only thing honestly known", then "a table of
contents now exists but `updateChapterLabel` still prints `CH. %02d`", and now the
wiring. The second of those was a **follow-up recorded in prose**, which is the shape
this file warns about: it stayed true for exactly as long as nobody read it.

**THE POSITION IS STILL THE FALLBACK**, for a book with no contents and for a chapter its
contents does not mention — spine entry 0 of a real book is its cover, and nothing names
that. One slot, the best name available for it.

**AND THE HEADER'S PRIORITY INVERTED WITH IT.** The theme drew the chapter FIRST and
reserved its width, because `CH. 01` was `white-space: nowrap` and the book title was the
run with slack to give up. A NAME is the long run now ("PREMIÈRE PARTIE : À LIRE AVANT
L'ACHAT"), so the board gives it `min-width: 0` and the title keeps its space — with the
title capped so the chapter can never be squeezed below `kReadChapterFloor`, enough for
the fallback form plus an ellipsis. Both runs elide; either can be arbitrarily long on a
real card.

**AND ONE LABEL THAT LOOKS LIKE A BUG IS NOT ONE.** Le Fléau's contents names a chapter
`S...`, and that is the book's own data: the chapter has no title and opens "Sally." with
the S as a drop cap, so the publisher generated the label from its first characters. The
parse is right; the ebook is thin.

**`<p>&nbsp;</p>` IS HOW AN EBOOK MAKES VERTICAL SPACE**, and it is everywhere: the
first text chapter of `Le Fléau` opens with three of them. Trimming only ASCII space
left each as a two-byte block that took a line of the page and drew blank. A block of
nothing but whitespace is dropped, and **U+00A0 counts for that question only** — it
is kept inside text, because there it is deliberate (French sets one before a colon,
and collapsing it would let the line break in the wrong place).

**A REFUSED CHAPTER TURN MUST LEAVE THE SCREEN WHERE IT WAS.** The walk has to open
each candidate before it can know whether that candidate has pages, so running off
either end of the book left `chapterAt_` on the last thing tried with an empty index
— the device reported "spine 0, page 1/7" for an entry with no pages, with a stale
page still on the panel. `openChapterAt` restores the previous chapter on failure, at
the cost of one extra decode, once, at the book's edge.

And a note on how the first of these was missed: the desktop probe checked
`pagesRead != starts.size()` and `0 == 0` satisfied it, so a chapter that paginated
to nothing passed silently. **A check that reports on less than it claims** — the
same shape as the card probe answered from cache, and the `make compare` default that
skipped four screens.

### A chapter's page count arrives after its first page

Counting a chapter is one decode of it — ~545 ms on the device for a long one — and
paying that before the first page appeared made **crossing into a chapter cost 1.14 s
against 575 ms for an ordinary page turn**. A crossing *is* a page turn from the
reader's side, so it should cost what one costs.

So a forward landing decodes only as far as page one, and the index grows a cursor at
a time as pages are passed. Three consequences, each load-bearing:

- **`ReaderViewModel::pageTotal` is 0 while the count is unknown**, and the footer
  draws an **em dash** for it — `3 / —`. `design/Reader.dc.html`'s footer states the
  form and why: a blank makes the slash read as broken, `0` would be a lie, nothing
  here animates, and a dash is the same width every time so the counter does not
  reflow when the number arrives.
- **AND THE PERCENTAGE BESIDE IT SAID `0%` THROUGH THAT WHOLE WINDOW, FOR TWO PHASES,
  BECAUSE THE EM-DASH RULE WAS APPLIED TO ONE SLOT OF THE TWO IT GOVERNS (#93).** That
  number **is** this counter as a fraction — the board's 53 of 890 is 5.955%, drawn as
  `6%` — so it is divided by the same total and is unknown in exactly the same moments,
  and `syncVm` answered the unknown with a literal `0`. Reported off a device after a
  week of real use. It is `ReaderViewModel::kProgressUnknown` (**-1**) now, drawn `—%`,
  and the fix is at the **producer** because that is where the other two spellings of
  this already are: `pageTotal`'s 0 and `percentFor`'s -1 for "not started".
  - **`0` IS A VALUE THE ARITHMETIC REACHES, which is the whole of why the sentinel is
    not 0.** `(page * 100 + total / 2) / total` is 0 for page 1 from **201 pages up**,
    and that is right — so the unknown was **pixel-identical** to a reader standing at
    the top of the chapter. Not a bounded wrong, either: the count runs only in a quiet
    window, so **a reader turning pages faster than `kCountQuietMs` never lets it fire**
    and can be well into a chapter still being told 0%.
  - **THE CONDITION IS THE COUNTER'S OWN `pageTotal == 0`**, so one test decides both
    slots and they cannot disagree about what is known — and it picks up the degenerate
    complete-but-empty chapter for free, where there is no denominator and the counter
    already read `0 / —`.
  - **THE PROGRESS BAR IS OMITTED RATHER THAN DRAWN EMPTY**, and that is the half a
    dash cannot fix: a bar is a **length** stating the same fraction, and
    `drawProgressBar(..., 0)` paints the exact outline a settled 0% paints. An absent
    claim beats a false one, the call this file already makes for an unread gauge (`-1`,
    never `0%`). **Nothing reflows** — the percentage is placed off the left padding and
    the counter off `fb.width()`, which is the same property that lets
    `ReaderAnchored.dc.html` put the return arrow in that slot.
  - **NO SURFACE BUT THIS ONE WAS AFFECTED, checked rather than assumed.** Every other
    percentage on the device is the **byte-based** `reading_store.h::progressPercent` or
    a value stored from it — the sleep card and Home read `last.percent`, a Library row
    and Book details read the sidecar (and `percentFor`'s -1 draws `NEW`), the reader
    menu's header and the peek's band call the free function. Only
    `ReaderViewModel::progressPercent` is derived from the page count, so only the
    Reader's own footer could say this.
  - **TWO EXISTING ASSERTIONS HAD BLESSED IT, one of them under the comment *"a
    percentage of an unknown is not a number"* while asserting `== 0`** — the rule
    written down beside the defect it forbids, which is this file's most expensive
    recurring shape. **Every golden passed** and could not have failed: they all call
    `completeIndex()` first, because the board draws the settled state.
    `reader_counting{,_x3}` are the transient state's own goldens and are the only thing
    in the suite that can prove `—%` is an em dash rather than a **notdef box** — the
    percentage is `Role::Meta700`, a different generated asset from the counter's
    `Role::Meta400`. Reverting just the theme's half draws a literal **`-1%`**, which
    those two goldens catch and nothing else does.
  - **The board's rendered specimen did not move**: the rule went into
    `design/Reader.dc.html`'s footer as prose beside the settled state it draws, whose
    own note already said the transient state would want its own board file. All four
    `--only reader` panels are byte-identical across the change and `reader` still
    measures **5.24% / 6.29%**.
- **`pageCount()` is pages KNOWN, not pages total.** Reporting it as the total would
  count up as the reader advanced — `1 / 1`, `2 / 2` — which is worse than admitting
  it is not known. `indexPending()` is what distinguishes them.
- **The stream decides whether a next page exists, not the index**, because with the
  count unknown the index cannot say. `advance()` returns false only when the
  chapter's blocks are exhausted, and that is also the moment the count becomes known.

**A SMALL CHAPTER IS COUNTED BEFORE ITS FIRST PAINT, and a big one is not.** Folding
the count into the refinement made a **four-page chapter take four seconds** to show
its total — the refinement's 5 s window is sized for an expensive cosmetic repaint,
and counting is neither.

`ReaderScreen::kEagerCountBytes` is **8 KB**, and the number is measured **on the
panel**. It was first set to 64 KB from a desktop figure times a remembered ratio, and
that was wrong by 8×:

- **The desktop figure was right** — 41.1 µs/page, and a 33 KB chapter's count pass
  really is 1.7–1.9 ms there.
- **THE RATIO WAS WRONG.** 37× came from a *render* measurement. This path is not
  render-bound: it is SD reads through SdFat on the display's SPI bus plus an inflate
  on a part with no FPU, and **the desktop does neither**. Against 3.5 ms of desktop
  work for two passes the device spent **484 ms** — ~135×.
- So the constant is device milliseconds per KB: **14.8 ms/KB for the two-pass eager
  open**, ~7.2 ms/KB a pass, which independently matches the ~545 ms this project
  measured counting a long chapter.

At that price **64 KB is 932 ms — 163% of a ~570 ms page turn**, so the eager count
cost more than the turn it was hiding inside: the 1.14 s crossing this design exists
to avoid, reintroduced at a smaller size. 8 KB is 118 ms, ~21% of a turn, and covers
14% of a real book's 92 chapters — the front matter a reader lands on, where a
six-page chapter reading `1 / —` looks like a defect. The median chapter is 53 KB and
gets the dash, as designed. **Bounded by bytes, not by a page budget** — the bytes are
known before any work is done, where a page budget would spend itself on a long
chapter and still have no total.

**TWO PASSES IS INHERENT here, not slop.** One pass ends at the chapter's END, and a
forward turn needs the builder live just after page one — the content and the stream
position cannot both come from one walk. Counting on a second `ChapterReader` would
buy one pass for another 32 KB window against a 45,840-byte floor.

**AND THE LESSON GENERALISES: THIS FILE'S ~37× RATIO IS A RENDER RATIO.** It is quoted
in "What the desktop measures" for cold page draws, where it holds. Applying it to
anything that touches the card or the inflater underestimates by ~4×. The paragraph
below already says the pagination walk "is the part with no desktop analogue worth
trusting"; this is what ignoring that costs.

**THE COUNT IS INTERRUPTIBLE, AND THAT — NOT THE QUIET WINDOW — IS WHAT STOPPED IT
BLOCKING.** `completeIndex` takes a `bool (*)(void*)` stop predicate (a function
pointer, not a `std::function`: this is `-fno-exceptions` embedded code), which the
shell answers from `rawSamplesPending()`. The index is built into a SCRATCH vector
and committed only on completion, so an abandoned count leaves `starts_`, `at_` and
the page on glass byte-identical — invisible to everything but the builder.

- **The check is per BLOCK.** It was set to every 8 blocks first, from a "1–3 ms a
  block" estimate; the device's own `pages=315 in 3605ms` over ~600 blocks says
  **~6 ms a block**, so 8 was ~48 ms of latency bought for 0.03% of the walk. Another
  instance of the ratio trap under `kEagerCountBytes`. A block is the floor —
  `chapter_.next()` and `pb.add()` cannot be stopped half way.
- **The one thing it does not restore is the live `PageBuilder`**, because the walk
  rewinds the `ChapterReader` the builder reads from and there is no second stream to
  rebuild it with (another 32 KB window against a 42 KB floor). So the next FORWARD
  turn pays a full `seekTo` — **and this is NOT only the abandoned count's doing,
  which is what the argument for the long window got wrong.** `completeIndex` ends in
  `seekTo(at_)`; `at_` is by definition the page the ring is most certain to hold, so
  the restore leg takes a cache hit — and a hit leaves `pb_` null deliberately,
  because nothing was decoded. **A count that COMPLETES spends the stream too**, so
  every deferred chapter cost one forward turn a rewind whatever the window was. The
  long window was buying nothing. (Also off by one press: the queue drains at the top
  of `loop()`, so the press that interrupted the count IS the next thing dispatched.)
- **`ReaderScreen::restreamAtCurrentPage` PUTS IT BACK, and abandoning THAT is free.**
  It is the same walk `warmPageRing` makes — one private `rewalkToCurrentPage` with
  two gates, not two copies — and it runs **only when `pb_` is already null**, so it
  has no live builder to spend: an interrupted restream leaves exactly the state it
  found. That makes the trade one-sided rather than balanced — it finishes and the
  next forward turn is free, or it is cut and that turn pays what it pays today — and
  **that, not the walk being cheap, is what lets it have a short window** where
  `completeIndex` and `warmPageRing` cannot. `hasLiveStream()` is the gate the shell
  asks first, so an ordinary page turn costs a pointer test and no log line.
  `[restream] ready|abandoned page=N` is what tells a working idle job from a silent
  one.
- **So `kCountQuietMs` IS ITS OWN NUMBER AGAIN, at 2000 ms**, having been
  `kRefineQuietMs` while the two shared a cost. The derivation is in the constant:
  the floor is the **1360 ms** longest pause measured while still turning pages, the
  margin is 1.47× rather than the refinement's ~3.5× because the cost of being wrong
  is now one rewind on one press at most once per chapter rather than 1408 ms of
  uninterruptible dead buttons, and the prize is the total landing on glass at ~3.0 s
  instead of ~6.0 s. **It cannot re-open the percentage-going-backwards bug**, which
  is the other thing this constant has to be checked against: `progressPercent` is
  made of BYTES now and reads `page`/`pageTotal` only where there is no inflater to
  ask, so for a real deflated chapter the count's timing does not enter it —
  `test_reader_restream.cpp` asserts the percentage across the moment the count lands
  and it does not move. On the byte-less fallback path, shortening moves the same
  lever in the direction that made it better.
- **The invisibility property is asserted over a CARD-BACKED book, and it had to be.**
  `ChapterReader::bytesRead()` is `inflated_ ? produced() : 0`, so the in-memory
  fixture reports **0 forever** and `CHECK(chapterBytesRead() == was)` over it is
  `0 == 0` — it passed with a mutant that zeroed the field, which is how the hole was
  found. `test_reader_restream.cpp` builds a real EPUB in memory for that one
  assertion; the trick that makes it cheap is that **DEFLATE has a stored-block mode**,
  so a valid method-8 entry needs a framer and no compressor.
- **There are TWO count sites**, the deferred one in `loop()` and one inside
  `refineNow()`. Fixing one and not the other would have brought the freeze back on
  whichever path the reader happened to take.
- **Both `[index]` lines say `counted|abandoned`.** `pageCount()` on an abandoned
  count is the old partial figure, and the line reported it as the answer — the
  "reports on less than it claims" shape again.

A chapter over the threshold is counted in a quiet window of its own,
`kCountQuietMs`, **and then repaints on the FAST path**. That repaint was
originally left out — "counting changes one number in the footer, and a ~570 ms paint
plus a waveform to fill it in is a bad trade; the total appears on the next page turn"
— and the device showed the flaw in it. Measured across a chapter crossing:

```
[chapter] spine=4 bytes=33463 deferred pages=1 in 90ms   <- press at 24226
[index] pages=40 in 440ms (deferred: chapter over 8192B) <- counted by 25782
[refine] done total=1423ms                               <- on glass at 30565
```

**The count finished at 1.56 s and the number was not visible until 6.34 s**, because
the "next page turn" almost never wins the race against the refinement's 5 s window.
So "no extra waveform" bought nothing and cost four seconds of a footer reading
`1 / —` with the answer already in memory. One fast paint (~596 ms) puts it on glass
at ~2.2 s, and the refinement still follows on its own schedule — `renderTop` leaves
it owed for a grayscale screen anyway.

**A press arriving during the count cancels the paint**, checked after the count as
well as before it: the page on glass is already correct, so getting out of the way
beats putting ~596 ms in front of a page turn. The refinement completes the count too,
as a backstop, and applies the same rule to itself.

**A BACKWARD crossing still pays the full count**, and cannot avoid it — landing on
the previous chapter's *last* page means knowing which page that is.

**THE COUNT DECIDES BEFORE LANDING, NOT AFTER**, and the first version got that
backwards: it landed on page one and *then* counted, which decodes page one, then the
whole chapter, then page one again — **three passes where two will do**. On device the
wasted pass is why a small chapter still felt as slow to open as it had before any of
this, and why the em dash never appeared to compensate. The size is known the moment
the stream is begun (the central directory said so), so the branch costs nothing to
take early. Worst eager open over a real book's 58 sub-threshold chapters: 4.4 ms
desktop, ~162 ms at this project's ratio, against 0.6 ms for a deferred one.

**AND THE EAGER SIDE NEEDS ITS OWN LOG LINE.** Only the deferred path had one, so a
device reporting "no dash, and the page is slow again" could not say whether the
count had run or how long it took — the branch was unobservable from the one place
that can measure it. `[chapter] spine=N bytes=B counted|deferred pages=P in Xms` is
printed at both open sites, and `indexPending()` IS the branch.

Two bugs this shape cost, both in restoring state:

- `openChapterAt` moves the index out before walking and puts it back on failure.
  Restoring through the forward landing instead threw the count away, so paging back
  off the front of the book left a counted chapter reading `1 / —` again.
- With **no book behind the screen** (the in-memory constructor), the walk fails
  immediately and the moved-out index was never put back — pressing past the last
  page of the demo chapter left the screen reporting **zero** pages. There is now an
  early return before anything is disturbed.

**The simulator and the goldens both complete the index before rendering**, because
the board shows the settled state. A simulator that rendered the transient one would
put `1 / —` in the comparison sheet against a board that says `53 / 890`, and would
disagree with the goldens — the desktop-diverging-from-the-device trap this project
has hit before.

### Opening a chapter: what the two seconds were

The device reported a 40-page chapter taking ~2 s to open against near-instant page
turns. That is the index pass, and two thirds of it was waste.

| | index pass | forward turn | backward turn |
|---|---|---|---|
| before | 41.4 ms | 1.22 ms | 35.6 ms |
| after | **14.8 ms** | **0.46 ms** | **14.7 ms** |

Desktop, three runs each within 1%. On the device the whole open went **511 → 192 ms**
and its pagination phase **433 → 119 ms**. Two changes, and the second was much the
larger:

- **An index pass wants page BOUNDARIES, not pages** (`PageBuilder::countOnly`). It
  was building a `LaidLine` per line — an owned string copy and a justification
  `measure()` — and then discarding all of them: ~480 of each for a 40-page chapter.
  Worth 41.4 → 35.5 ms, which is less than it sounds like it should be.
- **A 256-entry Latin-1 advance/gid cache in `ScalableFont`**, which the roadmap had
  recorded as a lever and which turned out to be most of the cost: 35.5 → 14.8 ms.
  `advance()` did a cmap binary search per character and `kerning()` did **two**, and
  `measure()` calls both — while `wrapProseLead` grows lines greedily and measures
  every candidate, so every glyph of a chapter was measured several times over. 1 KB,
  cleared by `init()` because the advances are in pixels.

**WHAT DID NOT IMPROVE: the draw.** A page turn's `render` stayed at 125–165 ms on
the device, unchanged by the advance cache — the coverage blit dominates it and
`kerning`'s cmap searches were noise beside it. ~~If a page turn has to get faster than
~570 ms, the blit is the target and the metrics are not.~~ **Both halves of that
sentence are now spent, in opposite directions.** The blit was the target, it was taken
(byte-wise now, a page render 215 → ~25–41 ms), and a page turn is ~92% panel — there
is no page-turn work left worth doing. And "the metrics are not" was right about the
DRAW and wrong about everything else: `kerning`'s *kern-table* bisection, which this
sentence never separated from its cmap searches, was **73% of a pagination walk**. See
**The glyph cache**, which now carries the measurement and the fix.

**The neutrality of counting mode is asserted, not assumed**: a probe indexed all 92
chapters both ways and got 7,968 pages each, 0 chapters differing. An index that
disagreed with what gets rendered would be the worst possible bug here.

`PageBuilder::pageHasContent()` exists because of it. `buildIndex` used
`finish().lines.empty()` to mean "was there a trailing partial page", which in
counting mode is always true — so every chapter's last page vanished from the index,
and a chapter that fits on one page indexed to nothing at all.

### openBook reads the whole spine once

It used to take a chapter index and return that chapter's offsets, so the reader
called it again for every chapter it wanted — and **every call re-reads the
121-entry central directory and re-inflates the 8,472-byte OPF**, about 32 KB of
transient allocation. Reaching this book's first chapter with text means trying
three spine entries, so that is three of them.

The device measured it, once `mark()` was put either side of the open:

```
[stage] open-located    heap=133712 min=85860
[stage] open-paginated  heap=87160  min=41188
```

**The pagination phase took minimum free heap from 85,860 to 41,188** and cost
433 ms of a 511 ms open. So the whole spine is read once and kept: `ChapterSpan` is
12 bytes an entry, **1,104 for a 92-chapter book**, and a chapter change is a row
lookup with no archive, no directory and no OPF.

**IT KEEPS THE LOCAL-HEADER OFFSET, NOT THE DATA OFFSET, and the first attempt got
that wrong.** Resolving one to the other is a 30-byte read, and doing all 92 when the
book opened took `locate` from 76 ms to **444 ms** — more than the pagination it was
meant to make cheap, because the headers are scattered across 12.7 MB and SdFat has
one sector cache. `ChapterReader` resolves the one chapter it is asked for and caches
it, so a rewind does not go back to the card. **Moving work is not removing it.**

**`Epub::open` VALIDATES EVERY SPINE ENTRY** against the manifest and the archive and
refuses the whole book if one is missing — `epub.cpp:186` and `:189`, two distinct
messages. So `ChapterSpan::readable()` is narrower than it looks: the only way to an
unreadable span is `zip.locate()` failing on a corrupt local header.

A comment in `book.cpp` claimed the opposite — that `Epub::open` lets a broken
chapter through so a book with one still opens — and that claim was **never true of
the code**. It was asserted three times, propagated into another header, and finally
into a test expectation, which is what made someone read `epub.cpp`. **A comment
about a neighbouring layer is not evidence about it.**

**AND IT REFUSES A SPINE, NOT METADATA.** `Epub::open` used to refuse a book whose
`unique-identifier` named an id no `dc:identifier` carried, on the stated grounds that
an unresolved identifier "breaks everything keyed on it — which is what per-book
reading state will be". **That consumer never arrived**: reading state went to the
card as `/.reader/state/<hash of the PATH>.json` with `bookBytes` as the identity
check, and nothing in the firmware has ever read `identifier()`. Measured against the
user's own library the check refused **4 of 16 books** — publisher and Calibre output
alike, a whole `Dune` trilogy among them — and every one of them reads: the reported
book walks 55 spine entries and 197,330 words once it is let in. The identifier is
best-effort now and **empty means the book did not say**, the same call the spine's
`toc` attribute already got.

- **A prediction in a comment is a claim with an expiry date**, and this one was
  restated in four places — `epub.cpp`, the header, `mkepub.py`'s docstring and a test
  name — so nothing in the repo disagreed with it and the design it described had
  changed underneath all four.
- **Removing the refusal made a substitution reachable, and the guard is the fix.**
  An absent `unique-identifier` and a `dc:identifier` with no `id` are **both the empty
  string**, so "does this identifier carry the id the package named" answers *yes* for
  a book that named nothing — reporting an identifier the book never designated.
  `!uniqueIdRef.empty()` is what keeps empty honest, and it is proved by mutation
  rather than by argument.
- **A DIFFERENT BOOK WAS REFUSED FOR A NEIGHBOURING REASON, AND AN ATTRIBUTE TOO LONG
  TO HOLD READS AS ABSENT NOW.** `Xml` capped a tag's attribute bytes at
  `kMaxAttrBytes` (512) and answered `Error` above it, so `Epub::open` reported "the
  OPF is malformed" and the book was gone. What was over the cap was one `<meta>` of
  Calibre custom-column JSON — **574 decoded bytes on `Walden ou la vie dans les bois`
  and 489 on `Le soleil et l'acier`**, both off the same real shelf, both
  three-hundred-page novels lost to a field describing a column in somebody's library
  manager. Same family as the identifier and the unknown entity, and it took all three
  to make the rule visible: **nothing in a tokenizer's bounds is a reason to refuse a
  document.**
  - **THE ARGUMENT FOR THE REFUSAL WAS TRUE AND THE CONCLUSION DID NOT FOLLOW**, which
    is the part worth keeping. A value cannot be SPLIT the way a long text run is,
    because `attr()` answers about the whole tag — and `xml.h` said so and stopped
    there. **Between splitting and refusing sits reporting it ABSENT**, which every
    caller already handles: `hasAttr()` exists precisely to tell absent from empty.
  - **DROPPED WHOLE, NEVER TRUNCATED.** A clamped `href` resolves to a path that is
    *wrong* rather than to nothing, and no caller can tell a short value from a cut
    one. Where the missing attribute really was load-bearing the layer above still
    refuses, by its own rule and with its own message — a spine naming a manifest id
    nothing carries.
  - **THE PACKING OFFSET REWINDS**, so a blob sitting FIRST costs only itself. Without
    that, everything after the dropped attribute is dropped too and a tag loses the
    `href` it needed for the metadata it did not.
  - **`kMaxAttrs` DROPS BY THE SAME RULE**, because running out of slots is the same
    event as running out of bytes and two spellings of one rule is what this file has
    a section about. Observed maximum on one tag across 226 real EPUBs is **eight**, on
    an `<html>` carrying namespace declarations, so that cap has never been reached by
    a real book.
  - **THE CAP DID NOT MOVE, AND RAISING IT WAS THE WRONG FIX** — it is what produced
    512 ("3x the observed worst case") and that was already wrong twice. Measured over
    226 real EPUBs: every tag over 400 bytes of attributes is a Calibre
    `user_metadata` meta, and the fattest tag that is **not** one is that 379-byte
    `<html>`. The failure mode was the bug; the number was fine.
  - **THE CHECK IS THE 224 BOOKS THAT DID NOT MOVE.** The corpus goes 224/225 opened
    to **225/225**, and every other book is byte-identical in title, author, spine
    length, block count and text bytes — the same standard the entity fix was held to,
    where "the unchanged thirteen are the check that matters". `Xml::attrsDropped()`
    is what keeps a drop from being silent.

**AND A BLOCK OVER `kMaxBlockBytes` IS CUT IN TWO, WHICH IS THE THIRD MEMBER OF THE
SAME FAMILY (#37).** 64 KB set `error_`, which stops `BlockReader`, which ends the
chapter — and `next()` returning false is **also** how a chapter ends normally, so
nothing reported it. It was found by the CORPUS and not by the audit, because the audit
was read off the refusal sites and this is a truncation site. **Split rather than
truncate-and-record**, the ticket's other candidate: what the cap protects is the size
of ONE block and both halves are under it, so splitting keeps the bound exactly and
loses no text, where truncation's magnitude is unbounded — a chapter that is one giant
`<div>` with no `<p>` is one block. `BlockReader::blocksSplit()` counts the cuts, in
`Xml::attrsDropped()`'s shape and for its reason.

- **THE TICKET SAID "2 CHAPTERS" AND THE DAMAGE WAS 84–92% OF TWO WHOLE BOOKS**, which
  is the corpus baseline's own stated blind spot arriving: *"`truncated` counts
  chapters, not bytes"*. Measured before and after over all 225: `The 32nd Mersenne
  Prime` 19,411 → **251,869** text bytes and `The Number "e"` 19,494 → **121,991**, so
  the over-long paragraph was the second block of the FIRST chapter and everything after
  it went. Chapter rate 99.97% → **100.00%**, +334,955 bytes, and **223 of 225 books
  byte-identical in every field**.
- **WHAT IT COSTS, stated rather than discovered:** `indentedAfter(Paragraph,
  Paragraph)` is true, so a continuation takes the 1.5em paragraph indent — one spurious
  paragraph break per cap's worth of unbroken text, against text that is simply absent.
  **Raising the cap was refused for #35's reason twice over: the failure mode was the
  bug and the number is fine** — the first half of which is still right and the second
  half of which was wrong in the direction nobody checked. See #90 below.

**AND THE NUMBER WAS NOT FINE: `kMaxBlockBytes` WAS 64 KB AGAINST A 42,152-BYTE FLOOR,
SO THE CAP PROTECTED NOTHING AND THE HEAP GAVE OUT FIRST (#90).** It is **8 KB** now,
and the growth is a **reserve** rather than a `push_back` ladder. #37 is not what
introduced this — the string grew to 64 KB before it too, and only *then* set `error_`,
so the peak was identical — but #37 put recoverable text behind the limit, which is
what made the limit worth calibrating.

- **IT WAS WORSE THAN THE 1.5× THE TICKET STATED, BECAUSE A CAP OF N DOES NOT COST N.**
  `push_back` grows geometrically and libstdc++ — which is what the ESP32 toolchain
  ships — climbs `15·2^k`, so a 64 KB block ended at a capacity of **122,880** and its
  last reallocation held 61,440 and 122,880 **at once**: 184,320 bytes transient, and up
  to 307,200 with the piece already handed to the caller. Under `-fno-exceptions` the
  failing request is `abort()` with no diagnostic — **a reboot onto Home, which this
  file already records as having been misreported twice as "opening a book goes back to
  Home"**.
- **AND BLOCKS FAR BELOW THE CAP WERE ALREADY IMPOSSIBLE, WHICH IS THE FINDING THE
  TICKET DID NOT HAVE.** A natural **16,384**-byte block needs 76,800 bytes — 182% of
  the floor — so the device's real ceiling was a paragraph of about 10 KB, **a sixth of
  the cap**, and **7 of the 225 corpus books (3.1%) sat above it**, not the two #37
  found: `Paradise Lost` (one 50,983-byte block), `The Online World`, `Poetry`, and the
  two Gutenberg mathematics texts. **The cap named none of them**, which is what makes
  this a bound that was fiction rather than a bound that was generous.
- **THE 8 KB IS THREE BOUNDS THAT AGREE**, and `document.h` carries the table. (1) It is
  the corpus's **99.99th percentile**: 69 of the **537,474** blocks 225 real books write
  exceed it, and **208 of the 225 have no block over it at all**. (2) Reserved, the peak
  is two buffers plus one 1,920-byte seam transient — **18,308 B, 43.4% of the floor**,
  largest single request 8,194 B — where 16 KB would be 82% and *"the largest free
  BLOCK decides, not the free total"* is not a rule you satisfy at 82% of a fragmented
  heap. (3) It **is not a regression in the common case**: a caller's `out` keeps its
  capacity between blocks and never shrinks, so a book already pays `2 × ladder(its
  largest paragraph)` — the **median** corpus book pays 15,360 B today and the p90 book
  30,720 B, so this is **+1,028 B on the median book and −475,132 B on the worst**. The
  band was `(Xml::kTextBytes, ~8 KB]`: below 1,024 the one-cut-per-text-node invariant
  `document.cpp` asserts breaks.
- **THE RESERVE IS THE HALF THAT MAKES THE BOUND A BOUND**, not the cap. A cap that
  holds only if the allocator's growth factor is 2 is an argument about a standard
  library this project does not ship — libc++ lands the same block at 12,287 and
  libstdc++ at 15,360. Reserved once, the capacity **is** the cap on both. It is
  **nothrow-PROBED**, because there is no `std::nothrow` spelling of
  `std::string::reserve`: the probe asks the heap the same question, then the reserve
  takes the block it just released, which is `imagefit.cpp`'s shape. **The refusal needs
  no new words** — it is the message `BlockReader`'s own `State` allocation already
  answers with, so no new `BookErrorReason` and no new copy shape on
  `BookError.dc.html`.
- **`take()` SWAPS INSTEAD OF MOVING, so the reserved buffer comes back** and the
  reserve is paid once per reader rather than once per paragraph — 537,474 times over
  the corpus. That is also what `restart()`'s *"reusing the buffers"* has claimed since
  it was written and did not do: `st.cur = Block{}` threw the buffer away every block.
  Nothing can hold a view into the swapped-out value — `LaidLine::text` is OWNED
  precisely so a Page can outlive its blocks — and the caller has by definition already
  consumed it.
- **THE FIRST VERSION LOST TEXT, AND TWO OF #37's OWN TESTS CAUGHT IT.** `roomFor` runs
  once per text NODE and a block spans many, so it swapped a *fresh* reserved buffer
  into a string that was not empty and threw away everything accumulated since the last
  reserve — a **hole in the middle of a rejoined digit run**. `reserve` copies the
  content across by definition, which is the whole reason to use it rather than a swap.
- **MEASURED, IN #37's OWN IDIOM: 208 of 225 books are byte-identical in every field
  INCLUDING the block count**, 225/225 still open, chapter rate still 100.00%. The 17
  that moved take **190 cuts** and **+186 blocks**, and the corpus's text goes
  126,614,534 → **126,614,498** — **36 bytes over 190 cuts, every one of them the single
  space the cut landed on**, which `take()`'s trailing-space trim removes. That is right
  (the pieces render as two paragraphs, so the paragraph break *is* the word boundary)
  and it is now the ONLY thing a cut may lose: `test_document.cpp` pins it with a
  fixture that puts the space **on** the cap, which a run of digits — every other case
  in that file — cannot reach.
- **THE VISIBLE COST: one spurious indent per 8 KB of unbroken text, about once per 15
  pages OF IT**, against #37's once per 121 at 64 KB. **On 208 of the 225 books the rate
  is zero**, because they write no paragraph that long; what is above 8 KB is Gutenberg
  plain-text conversions, where a whole book of the poem is one block and an extra
  indent is the least of it.
- **`ChapterReader::blocksSplit()` EXISTS NOW, and it had to for the cut to be
  observable at all.** `BlockReader::blocksSplit()` shipped with #37 as the
  `Xml::attrsDropped()` counting idiom and **nothing outside `document.h` could reach
  it** — a producer with no reader, the mirror of `ListRow::trackingEm1000`. The
  pass-through is an observation point in `held()`'s sense, and it is **per WALK, not
  per chapter**: a rewind calls `BlockReader::restart()`, which zeroes the counter.
  **Nothing in the FIRMWARE reads it yet** — the corpus probe is its only caller — so a
  cut is still invisible in a serial log, and `[chapter]`'s line is where it would go.
**THE SIBLING BOUNDS STILL HAVE #37's SHAPE — and, after #90, the OTHER shape too: not
one of them is calibrated against a device figure either.** They are cards rather than
paragraphs.

- `kMaxEmphasisPerBlock` (256) is the likeliest of them to meet a real converted book
  and the cheapest to fix — an emphasis run past the cap could be DROPPED, which costs
  one phrase its italics, where today it costs the rest of the chapter. `kMaxBlocks`,
  `kMaxNestDepth` and `kMaxTocEntries` end their stream the same way; 0 corpus hits
  each, which is "no evidence yet" and not "does not happen".
- **AND THE HEAP QUESTION IS OPEN FOR ALL FOUR.** `kMaxEmphasisPerBlock` is 256 `Span`s
  — 2,048 bytes, and it is a `std::vector`, so it climbs the same doubling ladder to
  4,096 with both buffers live at the last step; `kMaxBlocks` (4,096) bounds a
  `Document`, which the reader does not build but `buildDocument` does. Neither is
  anywhere near the block string's old 122,880, which is why #90 stopped at
  `kMaxBlockBytes` — but "small enough not to matter" is the argument that was wrong
  once already, and none of the four has a measurement behind it.

### A grayscale screen is painted twice: fast, then four levels

`renderTop` paints a `Fidelity::Grayscale` screen with ONE waveform and `loop()`
upgrades it to four levels once the buttons have been quiet for `kRefineQuietMs`
(600 ms). The reference firmware does this and it is the right shape for a reader:
the page wants to be there NOW and the grey edges can arrive a moment later.

From this device's own logs: the grayscale sequence is three waveforms and
**~1056 ms**; one waveform is **~520 ms**. So a page turn shows text in half the
time and the refinement costs what the full sequence would have cost anyway.
Flipping through pages costs 520 ms a turn instead of 1056.

Three things that make it work, and one that does not:

- **IT SHOULD NOT COST A SECOND FLASH.** `Uc8279Driver::displayGrayscaleBase` takes
  its visible "clean base" path only when
  `!_oldPlaneValid || _lsbValid || _forceFullSyncNext || _initialFullsRemaining > 0`.
  After an ordinary one-waveform paint the old plane IS valid and no grayscale
  planes have been written, so the base pass is the cheap settle. That is the whole
  reason this beats simply painting twice.
- **The fast pass is `Dithered`, not `Mono`.** The reader declares Grayscale
  precisely because hard-thresholding a serif face at 32px was judged worse, so the
  transient frame keeps what anti-aliasing one waveform can carry. Same cost, closer
  to the final image, smaller visible upgrade. Its known artifact is the em dash
  combing against the 4×4 grid at body size; `paintMono(mode)` is the one-line
  alternative.
- **It refines from `loop()`, never from a dispatch.** A paint cannot be
  interrupted, so refining between two page turns would put its full cost in front
  of the second one.
- **THE QUIET WINDOW HAS TO MEAN "STOPPED", NOT "BETWEEN TURNS".** 600 ms did not,
  and it made rapid page turning *worse* than no refinement at all. A paint blocks
  the loop for ~520 ms, so the earliest a second press can be dispatched is ~520 ms
  after the first — steady turning therefore produces gaps clustered just above
  that, and a 600 ms window fired ~80 ms after each paint finished, exactly where
  the next press lands. It then blocked that press for its own ~550 ms.
  `kRefineQuietMs` is **5000 ms**, and the number comes from the asymmetry rather
  than from taste. Measured across twelve consecutive page turns on the device: the
  gap between the panel going free and the next press being painted was a median of
  **72 ms**, with two of the twelve at **898 ms and 1360 ms** — pauses taken while
  still turning. A refinement measured **1408 ms** and cannot be interrupted. So
  firing early costs 1408 ms of dead buttons; firing late costs a page that stays
  dithered a little longer. 5000 ms is ~3.5× both the longest observed pause and the
  cost of being wrong, and still a fifth of the ~23 s a reader spends on twelve
  lines.
- **It also refuses to start with input already queued** (`rawSamplesPending()`).
  The clock alone cannot see a press that arrived during the paint, and starting
  something the panel cannot interrupt in front of one is the defect the window
  exists to avoid.

`[paint] … refine-owed` and a separate `[refine] done total=…` keep the two costs
distinguishable in the log — a page turn is the fast paint, and the refinement is
what the page settles into.

**THE REFINEMENT COSTS MORE THAN THE SINGLE GRAYSCALE PAINT IT REPLACED**, and that
is the honest accounting: 1408 ms (1041 panel + 367 render, four render passes)
against ~1056 ms. It is still the right trade because what the reader waits for is
TEXT, and text arrives at ~570 ms instead of ~1056 — but the extra ~900 ms of panel
work is real, and it is battery and panel wear rather than latency. A reader turning
pages steadily never pays it at all.

Its base pass does take the cheap settle path as intended — 366 ms of `gray_DRF`
with no visible flash — which is what makes the upgrade look like a refinement
rather than a second paint.

### Nothing may leave the column

Body text wraps with **`WordBreak::Anywhere`**, and it is the one place that is right
— for a reason the boards never had: a board's copy is text the design chose, so
`Normal`'s "a segment wider than the column sits on its own line and overhangs" is
fine there and is not fine for a book.

Measured over `Le Fléau`: **8 lines of 96,823 ran past the column**, the worst by
683px on a 492px column — off the panel entirely. Two fixes, in order of how much
they were worth:

- **A break after a hyphen** (UAX #14 allows one; Chrome does it), which took 8 to 2
  and moved no golden. Six of the eight were chanted hyphen chains like
  `Jeff-Marty-Helen-Harriett-…`. The hyphen stays at the end of the line, which is
  what makes the break read as typography rather than damage.
- **`Anywhere` as the last resort**, for the two survivors. Both separate their words
  with **U+00A0** — non-breaking by definition, so a browser would overflow rather
  than break, which a panel cannot do. `Anywhere` engages only when a segment cannot
  fit a line at all, which is CSS's `overflow-wrap: break-word`.

**0 of 96,658 lines overhang now.**

### Reading progress lives on the card

`/.reader/state/<hash>.json` per book, plus `/.reader/last.json` naming the book last
open. **Card-side, not NVS**, and the reasoning is worth keeping because the session
record went the other way: a wake must work with no card, so *which screen* has to
survive an empty slot — but a reading position does not, because with no card there is
no book to open. What card-side then buys is a correct card swap **by construction**,
where NVS keyed by path would restore page 400 into a different hundred-page novel.
Spec 4.0 had already named `/.reader/state/` when it said deleting a book "never
erases reading progress".

**THE RECORD DEGRADES INSTEAD OF BEING DISCARDED** (`reading_position.h`), three
numbers of decreasing durability:

| field | survives | because |
|---|---|---|
| `spine` | nearly everything | it indexes the OPF's spine, the book's own structure |
| `block` | a re-layout | blocks are `document.h`'s and owe nothing to a column or a ppem |
| `line` | neither | it is a line *within* a block at one ppem and one column width |

So `fitOf` grades a record `Exact` / `Relaid` / `Rebound` / `Unusable`, **weakest
wins**, and `restoreFrom` zeroes what the grade cannot support. The top of the right
paragraph beats the front of the book, which beats nothing; landing on line 9 of a
block that now has four lines is a wrong page that looks like a bug.

`bookBytes` is the identity check — the cheapest one a `FileSystem` with no timestamps
and no hashes can offer, and `DirEntry` already carries it. Not a checksum and it does
not pretend to be.

**THE SIDECAR'S NAME IS A HASH** (FNV-1a, 8 hex) because a book path is not a
filename: it holds `/` by construction, FAT forbids more, and real cards carry
accented 90-character titles. Collisions are handled rather than assumed away — the
path is stored *in* the file and a mismatch reads `Unusable`, so a collision costs one
book its position and can never misapply another's. Refused in two independent places
(`loadPosition` and `fitOf`).

**A SAVE HAS THREE ANSWERS AND THE MIDDLE ONE MATTERS.** `Unchanged` means the card
already says this, so nothing was written — the common case when a save fires on
leaving a book the reader did not move in, and it rests on `serialise()` sorting its
keys. **`Failed` MUST NOT BE TREATED AS FATAL**, and that is the one hazard in the
feature: a card can be readable and refuse writes (a physical write-protect tab), and
`writeAll` calls `noteCardGone()` when a write it had already opened goes wrong, which
`pollCardPresence` turns into an App rooted at `SdMissingScreen`. So acting on a
failed save would throw the reader out of a book they can still read. The shell logs
it and carries on.

**THREE EDGES PLUS A QUIET WINDOW**: leaving the book with Back, crossing a
chapter, and sleeping — each fired unconditionally, because each is a moment the reader
would notice losing. There were FOUR: the reader menu's `Close book` popped the Reader
from underneath an overlay, which the `leaving` save — fired on Back with the Reader ON
TOP — could not see, so it carried its own `closing` edge. **That row was cut
(2026-08-24) and its edge with it**: Back from the page is the one way out of a book
again. **Leaving is saved BEFORE the
dispatch** — Back pops the Reader and once popped there is no screen left to ask where
the reader was. Back is the only way out (`Gesture::Back` → `Action::pop()`), so this
is one save on the way out rather than a save per event.

**IT WAS THREE EDGES ONLY, AND THAT COST A CHAPTER OF READING TO A FLAT BATTERY.** The
reason recorded here was "a turn is ~570 ms of panel and a card write on each one would
be felt", which was right about the cost and wrong about where to put the work: it
bounded a power cut's damage at one chapter, which on a real novel is an hour. The write
is not made cheaper — it is made to happen when the loop is already idle, which is the
answer the page count, the refinement, the ring warm and the card log all reached
before it. (**The card log is no longer purely quiet-gated** — #83 gave it one forced
case, a reserve restored per loop iteration; the save's own claim is unaffected, and the
reasoning is under **AND THERE IS A LOG ON THE CARD**.) `kSaveQuietMs` is **2000 ms**, sized from the device's own twelve-turn
measurement (median 72 ms between turns, longest 898 and 1360) so that **steady page
turning never pays for it at all** and an ordinary reader, who spends ~23 s on a page,
saves about two seconds after every turn.

**IT IS NOT HIDDEN UNDER THE WAVEFORM, and that idea does not work here.** The
`triggerDisplay`/`completeDisplay` seam really does leave ~389 ms of idle CPU, and
`EpdBus` really does balance CS per operation — so the bus is electrically free in the
gap, which is **not** what CLAUDE.md used to say ("the driver keeps the display's CS
asserted across them" is true of the BUSY waits inside a call, not of this seam). It is
still forbidden: the SDK states the contract in three places, `PanelDriver.h`'s "the
caller does non-SPI CPU work in the gap and issues no other bus op until
`displayFinish()`" being the sharpest, and `Uc8279Driver::displayStart` leaves a
`PARTIAL_IN` window open for `displayFinish` to close, so the controller is mid-sequence
throughout. **The quiet window costs the reader the same nothing and breaks no
contract**, so there was never anything to buy by taking the risk.

**`ProgressSaveGate` (`core/include/reader/progress_save_gate.h`) IS WHAT MAKES THE
FOURTH CALLER SAFE**, and it is in `core/` because both of its jobs are exactly the kind
`shell/` has no harness to check:

- **Has the reader moved.** The quiet window is reached on every loop iteration once
  the buttons go quiet, and `savePosition` reaches its `Unchanged` answer by **reading
  both sidecars back off the card first** (`writeIfChanged`) — so an ungated save would
  be two file reads per iteration, forever, on the panel's own SPI bus. Three int
  comparisons replace all of it, and the card is never touched.
- **Has the card earned another attempt.** This is the hazard the feature turns on. A
  card can be readable and refuse writes, and `writeAll` calls `noteCardGone()` on a
  write that fails after opening, which `pollCardPresence` turns into an App rooted at
  `SdMissingScreen` — so a failing save can throw the reader out of a book they can
  still read. Saving ~100× more often would make that ~100× more likely. The gate backs
  off 30 s after a failure and **gives up for the session after three**, at which point
  the behaviour is exactly the three edges that shipped. `forget()` clears the stored
  point on a book change but deliberately **not** the failure count: giving up is a fact
  about the card, not the book, and re-arming per book would hand a read-only card three
  fresh attempts every time one is opened.

**RESTORING COSTS A WALK TO THE READER'S PAGE, NOT A COUNT OF THE CHAPTER.**
`ReaderScreen::openAtCursor` walks page boundaries to the page holding the cursor and
stops — which is what lets the footer say *which* page this is, since a cursor carries
no page number and the number is a count of the boundaries before it. The total then
arrives in the quiet window like any other chapter's, so a restore shows `7 / —` with
a right numerator and an honest denominator. `Cursor{}` is both "no target" and "the
top of the chapter" and takes the cheap path for both — which is also what a `Rebound`
restore asks for.

Tested by the restore equivalent of the strongest paging property here: **a cursor
saved on a page reproduces THAT page, checked for every page of a chapter** — a walk
that stops a boundary early is right at page 1 and wrong everywhere after it.

**AND WITHIN THE OPEN CHAPTER IT IS BYTES TOO, WHICH IT WAS NOT AT FIRST.** It
interpolated on `page`/`pageTotal`, and `pageTotal` is **0 until the deferred count
lands** — so for a long chapter the number did not advance at all for the first
seconds, and did not advance while the reader kept pressing at all. Reported off the
device as "I've advanced but I'm still at 40%", against a Kobo's 42% at the same
place. Two consequences, and the second is the sharper one:

- **A SAVE TAKEN IN THAT WINDOW PERSISTED THE UN-ADVANCED FIGURE**, over a better one
  written by an earlier save that did have the count. That is how the percentage went
  BACKWARDS — 42% on the sleep screen, 40% on Home, then 40% everywhere.
- **Widening `kCountQuietMs` from 1200 ms to 5000 made it worse**, because the count
  lands later. A latency fix and a correctness bug meeting in one constant is worth
  noticing: the constant was right and the thing depending on it was wrong.

`ChapterReader::bytesRead()` is the SAME quantity the rest of the sum is made of and
needs no count and no walk.
`ReaderScreen` records it at the end of the page on screen — the end rather than the
start, because the page in front of you has been read by the time you leave it — and
**the ring carries it per slot**, since a page served from the ring was decoded long
ago and the stream has moved since.

**AND THIS LINE SAID `produced()` FOR TWO PHASES, WHICH IS THE DECODER'S POSITION AND
NOT THE READER'S (#148).** Reported off an article: *"one big chapter surrounded by
chapters that have only one page; I'm at 50% in the chapter and it says 83%."*
`Inflater::next()` produces up to `kChunkBytes` — **HALF THE WINDOW, 16,384 bytes** —
and the tokenizer above asks for `Xml::kInputBytes`, **512**, at a time, so the first
ask inflates 16 KB and delivers 512 of it. `produced()` therefore leads the page on
the glass by the whole chunk in hand. It is `InflateSource::consumed()` now — the same
count less what is still unread — and the arithmetic in `reading_store.cpp` never
changed, because it was never what was wrong.

- **FOR A CHAPTER SHORTER THAN ONE CHUNK IT LED BY THE ENTIRE CHAPTER**, and that is
  the shape an ARTICLE is: the whole thing inflates on the first `next()`, so **page 1
  of 54 reported all 18,639 of its bytes read** and the number then stood still from
  the article's first page to its last. Reproduced at **87%** on a wallabag-shaped
  EPUB before a page was turned.
- **A NOVEL HID IT, WHICH IS WHY IT SHIPPED.** A 50-chapter book's chapters are 2% of
  it each, so a whole chapter of lead is a couple of points and the worst
  disagreement over `A Connecticut Yankee` was **3pp** — while `bytes=14037/14037`
  sat on page 1 of every chapter in the trace, visible and unremarkable.
- **MEASURED OVER THE 226-BOOK CORPUS, page by page against pages-read/pages-total:
  median 4pp → 2pp, p90 19pp → 7pp, p99 69pp → 15pp, max 72pp → 23pp**, 194 books
  improved, 30 unchanged and **2 worse by one point of rounding**. Books within 10pp
  went 192/226 → 218/226. On the article shape **83pp → 7pp**. `tools/progress_probe.cpp`
  is that measurement, tracked and re-runnable for `sleep_chapter_probe`'s reason.
- **WHAT REMAINS IS THE MODEL AND NOT THE INPUT.** The residual 23pp is markup that is
  not text — a one-page wrapper carrying a kilobyte of boilerplate weighs a kilobyte —
  and the same book tops an independent Python count of spine-boundary
  byte-versus-text skew at 24.9pp, which is what says the instrument and the model
  agree about where the floor is. Closing it needs a per-chapter TEXT size, which
  costs decoding every chapter (~49 s), so it stays.
- **THE PROBE'S OWN REFERENCE WAS WRONG FIRST, AND IT ACCUSED THE FIX.** A chapter
  with no pages is SKIPPED by `ReaderScreen`'s constructor, so opening at a cover
  lands on the chapter after it and credits the cover with that chapter's page count —
  which read as 27 books getting worse, one of them by 11pp. With the reference
  corrected the same sweep says 2, by a point each. **A disagreement between an
  instrument and a change is a claim about the instrument until the instrument has
  been checked.**
- **PROVED BY MUTATION AT BOTH LEVELS**, and the two bite in different places:
  `bytesRead()` put back on `produced()` fails `A CHAPTER SHORTER THAN ONE INFLATE
  CHUNK DOES NOT ARRIVE FULLY READ` reporting **`first` at 100%** — the report itself —
  and `consumed()` collapsed to `produced()` fails that *and* the source's own
  property. **Both fixtures carry a guard that would otherwise make them vacuous**: a
  STORED entry has no decoder to be ahead of, and a chapter over a chunk long is the
  case that always worked.
- **`inflateActive_` IS THE SECOND HALF, AND IT IS A LATENT BUG RATHER THAN THIS ONE.**
  `inflated_` is KEPT across a re-stream so a rewind does not reallocate the wrapper,
  so `inflated_ != nullptr` is not "the blocks are coming from the decoder" — a stored
  entry or an in-memory chapter following a deflated one was answered from the
  PREVIOUS chapter's count. Set at the one place that chooses a source, so it cannot
  drift from the choice actually made.

**`page`/`pageTotal` REMAIN AS THE FALLBACK, not as a second answer.** They are used
only where bytes are unknowable: a stored archive entry and an in-memory chapter have
no inflater to ask. Where both are offered the bytes win, and there is a test that
says so — the alternative is two spellings of one fact, which this file has a rule
about.

**PROGRESS IS A FRACTION OF THE BOOK'S BYTES, NOT ITS PAGES**, and that is what makes
it affordable at all. A page-based percentage needs every chapter paginated: 6.94 MB
of inflated XHTML for one real novel, **~49 s of decode** at the measured 7.2 ms/KB.
The byte layout is already in `ChapterSpan`, so `progressPercent` is a sum over 92
integers. It interpolates within the open chapter only when that chapter's count is
known, so the number can sharpen when a deferred count lands — which is honest.

**`last.json` CACHES title, author and percent** so Home can name the real book
without opening an EPUB at boot (a central directory plus an OPF parse, ~100 ms and
~32 KB of transient, for a block the user may not be looking at). The cost is that it
can go stale, so **it is checked against the card** with one `exists` call before
anything is drawn — Home confidently offering to continue a book that cannot be opened
is worse than not offering.

**AND THE CHECK NOW COSTS A STRIP RATHER THAN THE COLUMN (#7).** It threw the pointer
away, so a stale one fell back to the nothing-open screen — recorded here as "honest if
less informative", which undersold it: the title, the author, the percentage and the
chapter name are all **still true**, and dropping them told a reader the device had
forgotten a book it could have named. `design/HomeMissing.dc.html` is built now: the
reading column stays, with a bordered strip over it saying *"MIDDLEMARCH" IS GONE FROM
THE SD CARD.* and no CONTINUE slab.

- **IT IS A THIRD STATE, NOT A THIRD FLAG.** `nothingToContinue` still means the centred
  block and nothing else; `bookMissing` keeps the column. What they share is that
  neither draws a CONTINUE block, and **that is `HomeViewModel::offersContinue()`, one
  predicate asked by three things that must agree** — the focus ring (-1 is the block's
  own position), the Back gesture (Home's board binds Back to READ, which is CONTINUE's
  action from a button), and the theme. Two of them spelled `!nothingToContinue`
  independently, which is the shape this project has shipped a dead button from twice.
- **THE BOARD PROMISED `READ` AND THE FIX WAS THE BOARD.** It drew the hint while its
  own paragraph said "one fewer action" — and that slot is the BACK button, bound to
  `Action::open()`, which on this state resolves from the same pointer, fails the same
  `exists` check and **paints nothing**. Exactly the press the slab was removed for,
  reached by the other door. The slot is the 36px spacer now, as HomeEmpty's and
  HomeUnopened's are.
- **AND THE BOARD DREW A 16×14 MARK WHERE IT DECLARED 32×28**, which only a pixel count
  found: an `<svg>` is a flex ITEM in this strip, where `BookError.dc.html` draws the
  identical SVG in a flex COLUMN and never meets the question, so Chrome shrank it to
  exactly half. `flex-shrink: 0` — the spine's own declaration one box over. Measured:
  **2.69%/2.57% → 2.17%/2.03%** against `home`'s 2.01%/1.85% in the same tree.
- **THE NOTE IS THE ONLY GROWABLE RUN IN THAT COLUMN, so it is the only one with a
  budget.** It carries a title off the card, and everything below it is fixed while the
  menu is bottom-anchored — so the budget derives, and `clampProse` elides past it. A
  255-byte name (FAT's maximum) wraps to about fourteen lines unclamped and writes the
  author, the numeral and the chapter over the menu rows. Proved by mutation: removing
  the clamp puts 1,902 differing pixels into the menu band.
  - **AND IT SHIPPED WRAPPING `Normal` WHILE THE BOARD SAID `anywhere` — the board and
    the code on opposite sides of a declaration this change itself added.** The shell's
    own fallback when the OPF gave no title is the **path**: one token, no space and no
    hyphen, so `Normal` emits it as a single line however wide it measures, and
    `clampProse` cannot help because it bounds LINES and returns untouched when the
    count already fits. Measured at `/books/Le_Fleau_Stephen_King_edition_integrale
    .epub`: 750px against a 248px column, inking **x=479 of 480 and x=527 of 528** —
    through the box's border, through the margin, off the panel. `drawSpine` twenty
    pixels to its left already passed `Anywhere`, so one title was wrapped by two rules
    on one screen. The test watches **the box's own 14px padding**, which is paper by
    construction, on the Sleep card's rule that the panel edge is where the damage
    ENDED rather than where it is visible.
  - **AND THE BUDGET WAS SHORT BY THE BOX'S OWN LOWER CHROME**, 10px: it was measured
    from `contentTop` while the height adds the padding and the border again below the
    content. **The pixels cannot see that one**, which is what made it worth writing
    down: at the X3 it is a whole line (9 against 8) and the chapter line then lands
    4px inside the menu — where the first row is **full-bleed inverted** and draws it
    black on black, so a frame comparison passes on a wrong render. The assertion is
    arithmetic (`longBottom + statsH <= menuTop`). At the X4 the floor's 13px remainder
    absorbs it and nothing moves at all.
- **THE COPY IS `missingBookNote(title)` IN `core/`, NOT A LITERAL AT EACH PRODUCER.**
  There are two — the shell's `homeVmForCard` and `screens.cpp`'s board specimen — and a
  sentence spelled twice is one that can name a different book from the spine two inches
  to its left. It is also where the shout happens, so an accented title is `LE FLÉAU` and
  not `LE FLeAU`.
- **`books == 0` STILL WINS**, so a card that lost every book shows `NO BOOKS YET` rather
  than this. That is the older decision and it stands: with nothing on the card at all,
  the sentence about the /books folder is the more useful thing to say.
- **WHAT ONLY THE PANEL CAN ANSWER:** whether the curly quotes read at 21px under `Mono`
  thresholding — a `"` is about 4×5px of ink there, which is the thin-stroke case this
  file already records for `kChevron` — and whether the paper the missing slab leaves
  between the chapter line and the menu reads as deliberate rather than as a screen that
  failed to finish drawing.

**"THE BOOK IS CLOSED" MEANS NO READER IS LEFT ON THE STACK**, not that one is no
longer on TOP — and it asked the wrong question the moment the reader menu existed. The
menu and the contents are pushed ABOVE the Reader, so opening the menu declared the book
closed, cleared `gReading.open`, and with it the gate on the factory priming: pressing
Contents primed nothing, the factory refused (correctly, now that it refuses), and the
device reported "opening Contents does nothing". Scanned rather than tracked, because a
depth count would be a second copy of the stack's own shape.

**A WAKE CANNOT RESTORE THE READER WITHOUT ITS BOOK, and that is why sleeping on a
page woke to the Library.** `App::restore` pushes the record's stack, the Reader's push
goes through the factory, and the factory REFUSES a Reader with no book — deliberately,
since falling through to the demo is how this device once woke into Middlemarch. So the
restore correctly stopped short of a screen that could not be built. Nothing was wrong
with the restore; the book had never been set.

The shell now primes the factory from `last.json` before restoring, when the record
names the Reader anywhere in its stack. **Two records, two jobs:** the session record
says WHICH SCREENS and has never known about a book; the card says where in the book.
`openBookAt(path, bytes, push)` is the one function both a button press and a wake go
through — extracted precisely because they must agree, with `push` false for the wake
because `App::restore` does the pushing.

**EVERY BOOK READ `NEW` IN THE LIBRARY, and `screen_library.h` had already written down
why:** "a real percentage needs `/.reader/state/`, which has nothing to record until the
Reader exists". It does now. Two things had to change together:

- **The sidecar STORES its percentage.** Derived data in a record is usually a smell and
  this is the exception that earns itself: recovering it needs the book's chapter byte
  layout, so the Library would have to OPEN every started book's archive to draw a
  column of numbers. It is exact when written and goes stale only if the book changes —
  which `bookBytes` already detects, and which drops the position anyway.
- **`loadProgressIndex` reads the whole directory once.** One listing plus one read per
  book STARTED — not per book on the card, which is the point: 203 books with three
  started costs a listing and three reads, where asking each row for its own sidecar
  would be 203 opens, most of them misses, on a screen that has to paint. A corrupt
  record is skipped individually, so a save cut by a power loss costs one book its
  percentage and nothing else.

**AND THE LIBRARY IS BUILT ONCE, WHICH IS THE SAME BUG HOME HAD.** The rows are
derived when the screen is pushed, and the Reader is pushed ON TOP of the Library — so
the pop that leaves a book hands back that same instance with the rows it was born
with, and a book just read to 31% still said `NEW`. Reported off the device, exactly as
Home's "after reading a book, going Home still said NOTHING OPEN YET" was. Two screens
draw reading progress and both had to be told it moved: `saveReadingPosition` sets
`gHomeStale` and `gLibraryStale` together, and each is consumed when ITS screen is
reachable — sharing one flag would let Library, Back, Home clear it before Home used it.

- **The Library's is consumed when the Library is ON TOP**, which is what keeps it off
  the reader's critical path: the position also saves on chapter crossings and in the
  2 s quiet window WHILE READING, and the Reader is on top for all of those. The first
  iteration that can consume it is the pop out of the book.
- **It REFRESHES rather than rescans**, and the difference is ~600 ms. Only
  `/.reader/state` changed — a book cannot ARRIVE while the firmware runs, the same
  premise `libraryCountForHome`'s cache key rests on — and the save has just called
  `forgetCardFacts()`, so a `rescan()` would pay a fresh `/books` listing plus one per
  folder for the counts, on the critical path of a Back.
  `LibraryScreen::refreshProgress()` re-derives over the rows already there and moves
  neither the focus nor the window.
- **`applyProgress` is the one spelling of the derivation**, shared by both callers —
  the second copy is the extraction point, and `to_string(percent) + "%"` appears in
  the row's value AND in Book details' Progress row.
- **A failure leaves the rows alone.** An unreadable index means the card did not
  answer; re-deriving from an empty one would turn every started book back into `NEW`.

**BOOK DETAILS' ROWS COME FROM THE SAME INDEX**, and this is why the sidecar carries
derived data at all. Its `Progress` row is the percentage and its `Current story` is the
chapter name, both read from the one listing the Library already does — where deriving
either would mean opening the book's archive, and the chapter name would mean parsing its
NCX as well, per row.

**THE AUTHOR CANNOT COME FROM THE SCAN**, and that is the one field that needed a
different answer. It lives in the OPF, so learning it per row is ~100 ms an archive open
and **~20 s for a 203-book library**, on a screen that has to paint. Book details shows
ONE book, so the shell reads it on the press that opens the screen — one open, and there
is heap for it precisely because no Reader is on the stack, the same 48 KB the table of
contents could not find from under a live one.

**TWO FIELDS WERE REMOVED RATHER THAN LEFT BLANK**, because a row that can never be
filled reads as a device that failed to load something:

- **`Added`** wanted a file timestamp and `DirEntry` is `{name, isDir, size}` — no date
  anywhere in `FileSystem`, so filling it is a change across three implementations and
  the contract's 27 clauses rather than a metadata question. SdFat does expose file
  dates, so it is reachable; it is not this screen's work. **Five field rows now, not
  six.**
- **The subtitle** is not in the data at all. Checked across four real books: **not one**
  carries a `title-type=subtitle` refinement or any subtitle marker. `dc:date` is in all
  four, but a publication year is not a subtitle — deriving one from the other would be a
  different fact wearing its clothes. The board's "Fifteen stories · 1914" was authored
  copy.

Removing the subtitle gave the TITLE its line back, because the title's line budget is
the block's room less the column's FIXED runs and the subtitle was one of them. And the
block above the fields did NOT move — the cover is 180px and the column was shorter than
it, so the block's height is the cover's. That was measured off the board's own render
rather than reasoned about, which is why the rule positions in
`test_screen_book_details.cpp` are still 290/291.

**`Current chapter`, not `Current story`.** Every other slot on the device that names this
thing calls it a chapter — the reading page's header, the contents list, Home's counter —
and one screen calling it a story was the odd one out.

**A NEW SIDECAR FIELD READS BLANK ON AN OLD SIDECAR**, and that is worth expecting rather
than diagnosing: `chapter` was added after positions were already being written, so the
row is empty until the book is saved once more. `percent` had the same first run. The
record loads either way — refusing it would cost the reader their place to gain a label.

**AND THE PROGRESS ROW LOST ITS PAGE COUNT** — the third slot on the third board to do
so, after Home's CONTINUE block and Contents' rows, for the same ~49 s reason every time.

`percentFor` returns **-1 for "not started", not 0**: a book at 0% has been opened and
one that has not reads `NEW`, and the board draws those differently.

**HOME'S TITLE WRAPS, AND IT USED TO ELIDE.** The board said `text-overflow:
ellipsis` and the device showed a truncated book name on the one screen whose whole
job is to name the book being read — where an ellipsis on a *list row* hides only
which of seven rows this is. Both Home boards now say `overflow-wrap: anywhere`, and
the theme reuses Book details' mechanism: `wrapProseLead(..., WordBreak::Anywhere)` →
`clampProse` → `drawProse`. `Anywhere` because a title falling back to a filename is
usually one word with no break opportunity at an underscore or a hyphen.

**The line budget is DERIVED, not pinned**: the canvas less the band and the block's
padding, less the bar and the slab with their gaps, less the bottom-anchored menu and
hint bar, less the column's three fixed runs, over the title's line box. It comes out
4 lines on the X4 and 3 on the X3 — different branches of one arithmetic, which is
why both geometries are tested. `renderHome` built its hints twice; the reading path
now reuses the pair built at the top, because the bar's height is an *input* to the
budget.

**IT SHIPPED A USE-AFTER-FREE FIRST, and the way it hid is the lesson.** `Prose::lines`
are `string_view`s into the text handed to the wrap — components.h says "which must
outlive the Prose" — and the theme passed `upperAscii(vm.title)` inline, a temporary
that died at the end of the expression. A title long enough to wrap drew from freed
memory and rendered as a column of **notdef boxes**; a short one rendered correctly,
because the freed bytes were still there. So every golden passed, and so did the
row-counting test written to prove the wrap works — **a notdef box inks rows exactly
like a letter does**. What caught it was rendering a long title to a PNG and looking
at it. `home_long_title` goldens exist at both geometries now, which is the check that
distinguishes ink that spells something from ink that does not.

**`test_long_title.cpp` SAID "FOUR SCREENS" AND HOME WAS NOT ONE OF THEM** — the four
were Library, Book details, the actions panel and the delete panel. So the most
prominent title on the device was the one uncovered by the file that exists for
titles, and changing Home's from eliding to wrapping broke no test. Its cases are
there now.

**HOME'S VIEW MODEL IS BUILT ONCE, AND THAT WAS A BUG.** Home is the App's ROOT, so
returning to it hands back the same instance with the view model it was CONSTRUCTED
with — built at boot, before any pointer existed. The device reported it directly:
after reading a book, going Home still said `NOTHING OPEN YET`.

The whole App is replaced (`buildHomeApp`) rather than the view model swapped, because
the two Home shapes have different **focus rings** — `WithNone` where a CONTINUE block
exists, `Noneless` where it does not — and `Focus::None` is a construction-time
property with no setter. Only ever at depth 1, where the root is the only screen.

**IT IS GATED ON A FLAG, NOT DONE UNCONDITIONALLY, and the reason is the cost:**
`homeVmForCard()` counts `/books`, and a listing is ~2.7 ms an ENTRY on this card —
~1.1 s on a 203-book library, since macOS writes a `._name` beside every file. An
unconditional refresh would put a second's pause on a Back that is currently instant.
`gHomeStale` is set exactly when a reading position is saved, which is the only thing
on the device that changes what that block says.

**The focus is carried across the rebuild.** Otherwise pressing Back from the Library
would move a selection the user never touched. `setFocus` clamps, which is what makes
it safe across a ring that changed shape. It does mean landing back on `LIBRARY`
rather than on the new CONTINUE block — predictable rather than helpful, and the
opposite choice would be a focus jump nobody asked for.

**HOME'S CONTINUE BLOCK LOST ITS PAGE COUNTER, and the board says why.** It drew
`PAGE 53 / 890` over `CH. 01 — MISS BROOKE` and **neither was obtainable**: the first
needs the ~49 s book-wide count, the second needs a table of contents
(`Contents.dc.html`, not built at the time — which is also why the Reader's own footer
says a bare `CH. 03`). Both lines became one, `CH. 08 OF 92`, at the 0.16em counter
tracking of the line it replaces. `HomeViewModel::currentPage`/`pageCount` are gone
with it. Re-blessing the four Home goldens was verified the strong way: the change is
confined to rows 283–468 with **0 pixels differing** above or below, so the header
band, cover dither, title, author, the 67px numeral, both menu rows and the hint bar
are bit-identical.

**AND THIS LINE CALLED THAT COUNTER "FREE AND TRUE". IT WAS FREE AND IT WAS FALSE.**
Reported off an X3: `47% · CH. 14 OF 36` on the CONTINUE block, and Contents then put
the reader at part 2 of a book with **7 chapters in 2 parts** and five selectable rows
left. **A spine is not a chapter list** — it counts the cover, the title page, the
copyright, the contents, the part dividers, the acknowledgements, the notes, the index
and the about-the-author alongside the chapters — so the pair invited an arithmetic the
numbers do not support, and the reader did it. **It is the chapter's NAME now**, and
the sentence above is corrected rather than deleted because *"free and true"* is
exactly the shape this file records as its most expensive recurring defect: a claim
that was checked for obtainability and never for truth.

- **NO BETTER TOTAL EXISTS, and that is why there is none rather than a fixed one.**
  Measured over the 206 corpus books with a usable NCX: **126 (61.2%)** have a spine
  count exceeding the spine entries their TOC names at all — worst **164 against 26**,
  and `Dune - Tome 3` on the reporting user's own shelf is **73 against 38** — and
  **183 (88.8%)** have a spine count differing from the count of entries their TOC
  names as *selectable* chapters (`ContentsScreen`'s own header rule). Only **23
  (11.2%)** have spine, named and selectable all agreeing. **The reported book is not
  even in the 126**: 36 spine entries and 36 named, of which 26 are chapters — so a
  count that matches the TOC exactly is still not a chapter count, which is what makes
  this a fact about spines rather than about untidy books.
- **NUMBERING THE TOC'S OWN ENTRIES WAS REFUSED**, because it is a third numbering
  system beside the book's own — the mistake `Contents`' right-hand slot was already
  fixed for when `Chapitre 1.        CH. 09` shipped. If a name is shown, the name is
  what is shown.
- **IT IS THE READER'S OWN LABEL, NOT A SECOND DERIVATION OF IT.** `ReaderScreen`'s
  header band, Contents' `NOW` row and this line now name the reader's chapter in the
  same words, which is the rule this file already states for the `NOW` marker: two
  screens naming one fact differently is two spellings of it. **A book with no contents
  fills it with `CH. 08`** — `updateChapterLabel`'s fallback, a position with **no
  total**, which is what the Reader's footer says and the only handle such a book
  offers — so the no-TOC case needed no new answer and cannot disagree with the Reader.
- **`LastRead` CACHES IT, AND THE SIDECAR ROUTE WAS PRICED AND REFUSED.**
  `ReadingPosition::chapter` has carried this string since it shipped, so reading the
  per-book sidecar in `readingPointer()` would need no new key and have no first-run
  gap — and it puts a small-file read on the SPI bus at boot **and on every Home
  rebuild**, which is the critical path of a Back out of a book that
  `libraryCountForHome` and `DirCountCache` were both written to clear. Caching it
  costs one string in a file already read and **no card work at all**: the save site
  already has `rd->vm().chapter` in hand for the sidecar, one field up.
- **THE PRICE IS ONE BLANK LINE, ONCE.** `kLastReadVersion` did **not** move — a bump
  makes `loadLastRead` refuse the whole record, so every device would lose its CONTINUE
  block, title, author and percentage together on the first boot after this firmware —
  so the key is optional and an older pointer reads **empty**. Home draws that line
  blank until the book's next save, which is the first time the reader leaves it,
  sleeps in it or crosses a chapter. **Blank rather than the spine position**, because
  the only substitutes available are the claim this replaced and nothing: an absent
  claim beats a false one, which is the call this file already makes for an unread
  gauge (`-1`, never `0%`) and for a badge promising a wake charging cannot deliver.
- **`spineCount` WENT WITH THE LABEL IT EXISTED FOR**, from `LastRead` and from the
  shell's `ReadingPointer`. It was written, read and spent composing `CH. n OF N`;
  keeping it would have left a field with no reader, which is `ListRow::trackingEm1000`
  and `readerBookTitle_` a third time. Dropping the key rewrites every card's pointer
  once and nothing reads it on the way in, so an older pointer still loads.
- **THE RUN ELIDES AT 0.10em, AND BOTH HALVES CAME OFF THE BOARD.** 0.10em is the
  tracking `Main.dc.html` gave the chapter line it drew *before* the counter displaced
  it (`kMetaEm`'s 0.16em was the counter's, and `kTightMetaEm` already existed) — a
  name is not a counter. It elides because the words come off the **card**: unelided,
  `PREMIÈRE PARTIE : À LIRE AVANT L'ACHAT` reached column **479 of 480** and **527 of
  528**, off the panel at both geometries. **It may not WRAP**, and that is the Sleep
  card's lesson one screen over: the title above it already grows into a budget derived
  from everything below the block, and one budget cannot serve two growable runs
  without saying which yields. So the run naming the BOOK keeps every line and the run
  naming where you are in it takes one — which is also what keeps `columnFixedH`
  honest, since it reserves exactly one `meta.lineHeight()`.
- **AN EMPTY LABEL STILL COSTS ITS LINE**, so nothing below it steps up; that is the
  property `test_long_title.cpp` pins by diffing every case against a **blank** render
  rather than against the demo one, and a `ry` advance skipped for the blank fails it
  at both geometries.
- **RE-BLESSED PER PIXEL: all ten Home goldens differ by exactly 1005 pixels, in
  exactly 15 contiguous rows, in columns 153–320** — one line of text in the stats
  column, nothing else, identical column extent at both geometries. The row band is
  283–297 for every state except `home_long_title`, where the wrapped title pushes it
  to 415–429 (X4) and 371–385 (X3) — the derived budget's two branches, moving as they
  should. `home_long_chapter` and `home_long_chapter_x3` are **new**, and they exist
  for `home_long_title`'s reason: the elide test proves the run stops inside its
  column, and a column of notdef boxes stops inside a column too. They read
  `PREMIÈRE PARTIE : À LIR…` and `… À LIRE AV…`, accented capitals and the real
  ellipsis, cut differently at the two widths.
- **AGAINST THE BOARD: 3.59% → 3.54% (X4) and 3.30% → 3.25% (X3)**, 13,780 → 13,582
  and 13,782 → 13,584 differing pixels — threshold-at-128 over the bare `--export`
  panels, since the sheet still prints `ok` rather than a percentage (#41). Small and
  in the right direction, which is what a change should measure when **both** sides move
  to the same content: the residual is the same whole-pixel-versus-subpixel disagreement
  on one line of text. **The instrument is pinned by its control**: `library` reads
  **3.85% / 3.54%** in both trees, 14,800 and 14,801 pixels either side, which
  reproduces this file's own recorded figure for that screen to the digit. And the
  change is confined per row on both sides — the design panel differs only in rows
  **282–298** and the firmware panel only in **283–297**, columns 153–321 and 153–320,
  with **0 pixels differing** anywhere else on either.
- **WHAT ONLY THE PANEL CAN SAY: whether an unshouted name reads right there.** The
  string is drawn as the book wrote it, which is what the Reader's band, Contents' rows
  and Book details' `Current chapter` all do — so mixed case is the consistent answer —
  but it makes this line quieter than the caps runs around it (`NOW READING`,
  `CONTINUE`, `LIBRARY`), and no golden can judge that on glass.

**CONTINUE AND THE BOARD'S `READ` HINT BOTH ANSWER `Action::open()`** — they used to
answer `none()` behind a "the Reader is Phase 3" comment, and a slab that draws and
does nothing is the dead-button defect this project has shipped twice. Two screens can
now ask to open a book and they mean different ones, so `handleOpen` resolves it: the
Library means its selected row, Home means the pointer's path. `Action::Kind::Open`
carries no path deliberately, since `core/` does no storage. Neither fires on a
no-reading-column variant: CONTINUE is unreachable there by the model (the ring is
built `Noneless`) and `READ` is gated on `offersContinue()`, because those boards draw
an empty first hint slot and a bar that promises nothing must not do something.

### Home has FOUR states, and two of them share one mechanism

`Main.dc.html` has a reading position to show. `HomeEmpty.dc.html` has no books.
`HomeUnopened.dc.html` is the gap between them — **books on the card and none of them
open** — and until it existed the shell filled the CONTINUE block with `demoHomeVm()`
for any card with books on it, so a device that had never opened a book showed a
stranger's Middlemarch at 6%. Same defect class as the section below: content
substituted where the honest answer was "there is nothing here yet".

**The two no-reading-column states are ONE mechanism**, and the flag that drives it
is `HomeViewModel::nothingToContinue` — renamed from `libraryEmpty`, which named only
one of its two causes. It replaces the reading column with a centred block and builds
the focus ring `Noneless` so -1 (the CONTINUE block) is unreachable. The states differ
in **what they say** and in the LIBRARY row's value (`EMPTY` against a count), never
in what they draw; a second flag or a second render branch would be two ways to spell
one layout, and the boards say it is one layout.

`test_screen_home.cpp` asserts the structural fields of the two variants **against
each other** rather than against literals, so a change made to one and not the other
fails — that drift is the thing the shared mechanism is supposed to make impossible.

**`demoHomeUnopenedVm()` is unconditionally correct on the device today**, because
nothing persists a reading position: no card has a book in progress. When progress
persistence lands, the third branch appears in `homeVmForCard` and this becomes the
fallback for "books, but none started".

**AND THE FOURTH IS `HomeMissing.dc.html`, WHICH SHARES NEITHER MECHANISM** (#7). The
pointer names a book the card no longer has, and the reading column **stays** — its
title, author, percentage and chapter are all still true, and only the file is gone.
So it is not `nothingToContinue`, whose whole content is that there is nothing to say.
What it shares with those two is one narrower fact — **no CONTINUE block** — and that
is `offersContinue()`, which the focus ring, the Back gesture and the theme all ask.
See the `last.json` paragraph under **Reading progress lives on the card** for the
board defects it turned up and for what only the panel can answer.

**Rejected: keeping the reading column and offering a book with a START slab.** There
is no non-arbitrary book to pick — nothing has been opened so there is no most-recent,
and `FileSystem` carries no timestamps so there is no newest either — and three of the
block's five fields (the 67px percentage, the page counter, the progress bar) exist
only to describe progress, so they would all blank at once and read as a broken screen
rather than a fresh one.

**THE NO-READING-COLUMN LAYOUT NOW HAS GOLDENS**, at both geometries, and it had none
before: `home_empty` was checked only by `make compare`, which renders both sides fresh
and so cannot see the two drifting together. It is also the layout with the most
arithmetic on screen — a centred 112px mark, a centred title and a wrapped paragraph,
all accumulated in 1/64 px because the prose's height is a fraction (1.55 × 29px =
44.95). Design-vs-firmware mismatch measured **1.34% / 1.23%**, against the ~5.4%/6.4%
this project averages, and the firmware wraps the sentence at the same break as Chrome
— so `max-width: 400px` holds in both engines here, unlike SdMissing's, which needed
420.

### A factory that substitutes content is worse than one that refuses

`ScreenId::Reader` needs a book, and the shell only sets one from a button press — so
a **session restore has nothing set**. The factory used to fall through to the demo
chapter there, and the device woke from sleep showing Middlemarch: fiction from a book
the user was not reading.

The demo now has to be asked for (`setReaderDemo()`, which the simulator and the
goldens call) and a Reader with neither a book nor a demo is **refused**. A refused
push leaves the Library standing — wrong in a way the user can see through, rather
than wrong in a way they cannot.

**AND THE REFUSAL WAS RIGHT WHILE BEING UNREADABLE, WHICH IS #49.** A wake replays a
stack of screen NAMES and the factory rebuilds each from state the shell must have
primed, and **nothing connected "the record names X" to "X's inputs are primed"** —
there was one hand-written scan for `ScreenId::Reader`, and `ReaderMenu`, `Contents`
and `BookEnd` came back only because `openBookAt` primes them **on its way past**. So
each screen that needed its own inputs rediscovered the same failure, always with the
same symptom and always misattributed: the restore pushes, the factory refuses, the
restore stops and keeps what stands, and the reader reports *"it went back to the
book"*. **Three screens shipped that way as a defect and `Peek` ships it as a
DECISION, and from outside those are the same observation.**

**`reader::restorability(ScreenId)` IS THAT QUESTION, AND EVERY SCREEN ANSWERS IT.**
`Ready` (a wake owes it nothing), `NeedsPriming` (only a press could have produced
its inputs), `Never` (it does not come back, and that is a decision). It is in `app.h`
beside `screenUsesRadio` **for `screenUsesRadio`'s reason** — a fact about the screen
catalogue, and `shell/` has no harness.

- **THE TABLE IS AN ARRAY `static_assert`ed AGAINST `ScreenId::Count`, NOT A SWITCH.**
  An exhaustive switch leans on `-Wswitch`, which is a WARNING here — this file records
  a screen appended while three such switches answered it wrongly and the only
  diagnostic was three warnings scrolling past. **Proved by appending a dummy screen**:
  with `kNames` and `kAllScreens` both satisfied, so every pre-existing guard was
  quiet, this one still refused the build.
- **`App::snapshot()` STOPS THE RECORD AT THE FIRST `Never` SCREEN**, so the record
  only ever names screens that come back. Sleeping under a peek stores `…;reader:0`
  and the wake reports a **complete** restore, where it used to store the peek and
  then report stopping short — which reads in a log exactly like a screen nobody
  primed. **Truncated, not filtered**: dropping one from the MIDDLE would hand the
  wake a stack that never existed.
- **`App::restore` REFUSES A `Never` ENTRY BEFORE ASKING THE FACTORY**, and the
  screens that most needed it are the ones the factory **builds**: `Sleep` and
  `BatteryEmpty` were kept out of a record only by nothing ever pushing them, which
  is a property of the shell rather than a rule. Waking into either is *"press power,
  get asleep back"* and a battery-empty prompt over a charged pack.
- **`RestoreReport::stoppedAt` IS WHAT MAKES THE LOG SAY WHICH**, asking
  `restorability()` rather than carrying a second field free to disagree with it:
  `Never` is the mechanism working, anything else is a screen this build says a wake
  may have and nothing primed — a firmware defect, not a card fault.
- **THE SHELL'S SCAN IS NOW A WALK OVER THE RECORD** that asks `core/` which entries
  owe a priming and **names in the log any it does not answer**. The priming stays the
  shell's and may never move — `core/` does not know what a filesystem, a book or
  `last.json` is — but the LIST is `core/`'s, so the shell cannot hold a stale copy of
  it. That is this file's own rule: a caller list is a function not yet written.
  `openBookAt` still primes all four book-built screens in one pass, and the walk now
  **names all four** rather than leaving three to ride the first.

**THE DECLARATION IS CHECKED, NOT MERELY WRITTEN**, and that is the half that earns
it: `test_focus_restore.cpp` builds every screen from a factory configured the way
`setup()` leaves it — panel geometry, settings, the saved Wi-Fi list — and **no
further**, then demands a `Ready` screen build and a `NeedsPriming` screen refuse.
A declaration nothing checks is a second copy of the factory's own switch, free to
disagree with it, and **it disagreed on the first run**: `WifiSettings` was written
`NeedsPriming` from reading its factory case, and `loadWifi()` primes it at boot. The
counts are hand-maintained for `movable`'s reason — **9 `Ready`, 4 `NeedsPriming`,
9 `Never`**.

**WHAT ONLY A WAKE ON THE DEVICE CAN CONFIRM**, and it is the whole feature: that a
sleep in the peek stores the reader's page and wakes onto it reporting a COMPLETE
restore, that `[session] it stopped at …` appears with the right half of its sentence
when one does stop, and that a record from before this firmware — which can name a
`Never` screen — is refused rather than restored. `shell/` has no harness, so 1,594
green test cases say nothing about any of it.

### Paging: forward is free, backward re-decodes

A DEFLATE stream cannot be seeked and checkpointing one costs 32 KB a checkpoint. So:

- **Opening a chapter costs one decode**, which builds the page index: one start
  Cursor per page, ~8 bytes each, 3,072 bytes for the longest chapter in the book.
  That index is what lets the footer say `3 / 12` at all, and what a backward turn
  decodes *to*.
- **A forward turn continues the live stream** — the reading position keeps its
  `ChapterReader` and its `PageBuilder`. Measured 1.16 ms on the desktop for the
  worst page in the book.
- **A backward turn rewinds and decodes forward** to the recorded cursor — **~390 ms
  on the device**, not the 33.9 ms desktop figure this line used to quote against a
  ~520 ms refresh. Buffers are reused, so it allocates nothing — churning 32 KB per
  turn is how a heap with 142 KB free becomes one that cannot serve the next chapter.

**AND THE COST IS PROPORTIONAL TO THE PAGE INDEX, which is what the first fix
missed.** A rewind decodes pages 0..p, so it costs what page you are ON, not what
page you are going to. The device showed it plainly once a real saved position was
restored: at page 38 a backward turn was ~376 ms, and at **page 99 of the same
chapter it was ~1010 ms**. Two consequences, and both are fixed:

- **A RESTORE WALKED THE CHAPTER TWICE.** `openAtCursor` counted boundaries with
  `countOnly()` to find which page holds the cursor, then handed that page index to
  `seekTo()`, which rewound and walked the whole prefix AGAIN to lay one page out.
  On the device that was `post=2269ms` on CONTINUE against ~1010 ms for one walk.
  It is one walk now, with the lines kept: counting mode saves the LINE BUILDING of
  every page it passes, ~15% of a walk, and it was buying that 15% at the price of a
  second whole walk. The builder is left live one page past the target, which is
  `seekTo`'s own postcondition reached once instead of twice.
- **A REWIND NOW KEEPS WHAT IT PASSES.** `seekTo` stops skipping `kPageCacheDepth`
  pages early and caches each one it takes. The inflate, the parse and the wrap for
  those pages are already paid — only the line building was being skipped — so one
  rewind serves a whole ring's worth of backward turns instead of one. Sustained
  backward reading goes from a rewind per page to a rewind per `kPageCacheDepth`.

**A TEST THAT WALKS BACK ONLY `kPageCacheDepth` PAGES CANNOT SEE THAT CHANGE**, and
was written that way first. Reading forward already seeds the ring with the last
`kPageCacheDepth` pages, so such a walk is served entirely by what the forward pass
left behind — the mutation passed. It walks back **twice** the depth now. Same
lesson as the no-op mutation recorded under **Goldens**: check the mutation lands
before believing what it tells you.

**AND A SKIPPING BUILDER MUST NOT CHARGE THE PAGE IT HAS NOT BEGUN — THIS IS THE
DOUBLED PAGE.** Reported off the device as "I turn the page and the same page comes
back with a different number", and reproduced over **8 of the 16 books in one real
library**. `PageBuilder::drain` charges a block's blank rows to `row_` before its
first line, and did so **while `skipping_`** — while the builder is still discarding
everything before the cursor `startAt` gave it. `row_` is how full the page BEING
BUILT is, and there is no page being built there: the skip's own exit zeroes it. So
those rows are charged to a page about to be thrown away, and they ACCUMULATE,
because nothing resets `row_` until the skip ends.

Once `row_` reaches `rows_`, `ready()` — which is exactly `row_ >= rows_` — claims a
page the builder has not begun. `seekTo` loops on that, takes an **empty** page, and
spends an index slot on it: the page that truly began at the target is then cached
under the NEXT page's cursor and handed back as the next page, and `starts_` ends up
holding **two consecutive pages with the same start cursor**. That is the doubled
page, and the index is where it is visible.

- **GUARDING `ready()` IS THE WRONG FIX AND WEDGES THE BUILDER**, which is how it was
  settled which of the two is the cause. The lay loop is gated on `row_ < rows_`, so a
  full `row_` means the skip can never REACH its exit — **the empty take was the only
  thing un-sticking it.** The fix is one word on the increment; the decrement stays
  outside the guard, so exactly what was consumed before is consumed now and no page
  boundary moves.
- **ALL THREE `startAt` CALLERS HAD IT** — `seekTo`, `rewalkToCurrentPage` and
  `layoutPage`, the last of which could return a **blank** page for a start cursor
  deep in a document. One line in the primitive, which is this file's own rule.
- **NO FIXTURE IN THE SUITE COULD REACH IT**, and that is the part worth keeping.
  `blankRowsBefore` charges a row only when a heading, a blockquote or a list
  boundary is crossed, and `readerfix::longChapter` is paragraphs all the way down —
  so `row_` never climbed. **A stream of one block kind is not a chapter**, and every
  paging fixture here was one. The regression test alternates kinds and says so.
- **AND A FORWARD WALK ALONE NEVER SEES IT.** It needs the builder to be null, which
  on a device means an idle walk was INTERRUPTED — the common case while reading, and
  the one a desktop probe turning pages back to back never produces.
  `tools/paging_probe.cpp` is what reproduced it: `--whole` crosses chapters,
  `--idle` runs the three quiet-window walks in the shell's own order, `--interrupt N`
  answers their stop predicate, and `--only` names which of the three is to blame.

**SO THERE IS A RING OF LAID-OUT PAGES, DEPTH 3**, and turning back to the page you
just left now decodes nothing at all: 5,858 → **8.3 µs** desktop for a backward turn,
and a Prev,Prev,Next,Next burst 4,696 → **22.6 µs with zero decodes**.

- **Keyed on the chapter plus the page's start `Cursor`**, NOT its index. An index is
  a position in a list that grows as the chapter is read and is replaced outright by
  a count, so it names a different page before and after one.
- **Pages are COPIES.** `LaidLine::text` is owned exactly so a Page can outlive the
  blocks it was laid from; a ring of views would resurrect the notdef-box lifetime
  bug recorded under **The lifetime rules that changed**.
- **A HIT LEAVES NO LIVE BUILDER, and that is the sharp edge.** `Gesture::Next` tries
  the ring first — the page ahead is exactly the one a reader who came back is
  returning to — and otherwise decodes with `needStream`. Taking a hit where a stream
  was needed is not slow, it is WRONG: `advance()` answers false, and the screen reads
  that as the end of the chapter and turns to the next one from the middle of this
  one. Dropping `needStream` fails seven tests, two of them pre-existing paging
  properties.
- **A live builder still beats the ring** — ~20 ms against ~376 ms to re-establish
  one — so the whole branch sits under a null check.
- **1,471 B a page on the X4 and 1,512 on the X3**, measured; the ring is 4,536 B at
  the default depth of 3, 10.8% of the 42,152-byte floor, with a test asserting the
  ceiling so it cannot drift.

**AND THE REWIND THAT REMAINS HAPPENS WHILE THE USER IS READING.** A deeper ring
alone only postpones the slow turn -- it cannot remove it, because holding a whole
chapter is 315 pages at ~1.5 KB. What removes it is doing the rewind in a quiet
window: `warmPageRing` walks to the page already on screen, caches the depth's worth
of pages ending there, and leaves the builder live exactly where it found it.
**`page_` and `at_` are untouched on every path**, so nothing visible changes -- that
is the property the other three tests rest on and it is asserted first.

- **Gated on headroom, not on a timer.** Reading FORWARD already fills the ring, so a
  warm straight after it correctly finds nothing to do; without that gate it would
  pay a full rewind every quiet window to cache pages it already holds. The first
  version of the test tripped over exactly this and had to spend the headroom first.
- **The depth is the SHELL's**, sized from `ESP.getFreeHeap()` at each warm, because
  the floor moves by 34 KB on nothing but which button opened the book: through the
  Library there are 203 books resident underneath at ~59 KB and **42,152 bytes**
  free; through Home's CONTINUE the same book leaves **76,476**. A constant has to be
  sized for the first and then wastes the second. An eighth of what is free, and the
  default of 3 is the floor, so a heap under pressure keeps the shipped behaviour.
- **Abandoning costs the next FORWARD turn**, because the warm spends the live
  builder and cannot rebuild it -- the identical trade `completeIndex` makes, and why
  **this one still waits for the refinement's window** where the count no longer does.
  The asymmetry is the whole reason there are now three constants and not one: a warm
  is reached with a stream STANDING, so an interrupted warm loses it; a restream is
  reached only with `pb_` already null, so an interrupted restream loses nothing.
  `kRestreamQuietMs` is 1200 ms for exactly that reason and `kRefineQuietMs` stays
  5000.
- **`restreamAtCurrentPage` IS THIS WALK WITH A DIFFERENT GATE**, sharing the private
  `rewalkToCurrentPage` rather than copying it -- the second copy is the extraction
  point, and a builder installed one page off is a reader that skips or repeats a
  page, which two copies would each have to be tested for separately. The shell runs
  the restream FIRST: a landed restream leaves the headroom a warm would have left,
  so the pair costs one rewind rather than two and the warm below correctly finds
  nothing to do. The gates differ in one more place -- a warm refuses page 0 (nothing
  behind it to cache) and a restream accepts it (the cheapest walk there is).
- **It is LOGGED although nothing is visible**, precisely because nothing is: an idle
  optimisation that silently stops working looks exactly like one that is working.
  `[warm] ready|abandoned depth=N headroom A->B` is what tells them apart.
- **WHAT IT STILL DOES NOT FIX**, and the honest limit: skimming backward faster than
  the warm can run -- a turn every ~500 ms against a rewind of one to three seconds --
  still meets a slow turn, and the rewind is proportional to the page index, so it is
  worse deep in a chapter, which is the opposite of what it feels like.

The strongest test of all this is `READING BACKWARD GIVES EXACTLY THE PAGES READING
FORWARD GAVE`: it exercises the rewind, the buffer reuse, `startAt`'s discard path
and the index together, and any of them off by a line shows up as a page that differs
from its forward self.

### The lifetime rules that changed, and how they broke things

Both of these are worth knowing because neither failure looks like a lifetime bug.

**`Xml::name()` is a view into a reused buffer now**, where it used to view the
caller's whole document and outlive the parse. `document.cpp`'s tag stack held those
views, and the result was that `<blockquote><p>x</p></blockquote>` came out a plain
paragraph — the stack's `"blockquote"` had become `"p"` — and `<a><b></a></b>` was
**accepted**, because the mismatch check compared two views into the same buffer and
those are always equal. A dangling view here does not crash; it silently agrees with
itself. The stack holds 24-byte truncated copies plus the full length.

**`LaidLine::text` is OWNED**, not a view. A view meant whichever blocks a page
spanned had to outlive the Page, which is a rule the reader would have to enforce
across a page turn while blocks are being dropped behind it. A page is ~12 lines of
~45 bytes, so copying costs ~1 KB against a ~520 ms refresh — and it means a block is
released the moment its last line is laid, so **nothing needs a block window**. Two
tests had recovered a block boundary by comparing `text.data()` pointers; `LaidLine`
carries `block` and `lastOfBlock` now, which the page index needs anyway.

**AN ENTITY IT CANNOT DECODE IS TEXT, NOT AN ERROR, AND THAT REVERSED A WRITTEN RULE.**
`decodeEntity` knew the five XML built-ins and numeric references, and errored on
everything else — on the stated grounds that a literal `&nbsp;` in a paragraph "reads as
a rendering bug and is really a parsing one". The first half of that was right and is now
answered by a **generated 252-name HTML 4 table** (`tools/entities.py` →
`core/src/entity_table.h`, from Python's own `html.entities`). The second half was
measured and was false, because **erroring never reported anything**: `document.cpp` stops
on `Node::Error`, `ChapterReader::next()` then returns false, and that is
indistinguishable from the chapter ending. `Dark Plagueis` lost **177 of its 183
chapters** that way — 3,214 bytes of a three-megabyte novel — and read as a book that
opens and is empty.

- **Every exit that is not a decoded character is now text.** Three ways to fail — the
  reference never terminates (`Tom & Jerry`), the name is in no table, the numeric form
  does not parse (`&#zz;`) — and all three emit the bytes the document held. The only
  remaining `false` is "the output buffer cannot hold them", which both callers make
  unreachable by reserving `kMaxEntityBytes + 2` where they reserved 4.
- **A visible wrong beats an invisible one**, which is the call `css.h` already makes for
  over-matched italics. A stray `&unknown;` on the page is a typographic error a reader
  can see and report; a discarded chapter is not.
- **Seven names carry it**: `rsquo` `nbsp` `mdash` `ndash` `ldquo` `rdquo` `lsquo`, every
  distinct named entity across sixteen real books, 50,245 occurrences. The table is all
  252 because the set costs 1,416 bytes of names and typing a subset invites a second
  pass.
- **Two tests in other files pinned the old rule** and had to change with it —
  `test_document.cpp`'s only malformed-markup case *was* `&nbsp;`, and `test_xml.cpp`
  listed `&#xZZ;` among inputs that must error. A rule stated in one place is enforced in
  three.
- **CONFIRMED ON GLASS (2026-08-28)**: `Dark Plagueis` opens and reads on the X3. Both
  this and the identifier fix were desktop-measured first, and desktop evidence has been
  wrong about this panel three times — so the distinction is worth keeping: everything
  above is now a fact about the device, not about the simulator.

**`xml.h` DOES NOT VALIDATE NESTING, ON PURPOSE.** `<p>unclosed` tokenizes without
complaint. The document builder keeps a stack to know which block it is in, so it
notices an unclosed tag at `Eof` for free; a second stack in the parser would be a
second depth cap and a second allocation for a check the layer above cannot skip.

**A REAL BOOK'S ITALICS ARE IN ITS STYLESHEET, NOT IN ITS TAGS.** `document.cpp`
reads `<em>`, `<i>` and `<cite>`, and one chapter of the user's own `Le Fléau`
carries **609 classed inline tags and not one of the three** — its italics are
`<span class="...">` against a publisher sheet, which is what a converted EPUB
usually emits. So without `css.h` most books on a real card rendered no italics at
all, silently, and it read as a regression when nothing had regressed.

- **`collectItalicClasses` IS NOT A CSS ENGINE AND MUST NOT BECOME ONE.** It answers
  one question — which class names carry `font-style: italic` — in a forward scan
  with no tree, no cascade and no specificity. The OUTPUT is a handful of short
  names; that bound is what makes it affordable.
- **IT OVER-MATCHES ON PURPOSE, three ways**: a later rule turning italic back off is
  not modelled, `@media` bodies are scanned like any other, and only classes are
  collected (an element selector would italicise a whole chapter). Over-matching sets
  a run in italic that should be roman, which is a typographic wrong you can see;
  under-matching is invisible, and invisible is what this was.
- **`font-family: "Italic Garamond"` MENTIONS BOTH WORDS AND ASKS FOR NEITHER**, so
  the value has to follow the property through its colon rather than being two
  independent searches. That has its own test.
- **THE CLOSE IS MATCHED BY THE ELEMENT, NOT BY ITS NAME.** A class-italic run ends
  at a `</span>` indistinguishable from every other, so `TagName` carries an
  `openedEmphasis` flag set at the start tag. The tag-based path rides the same flag
  rather than re-testing the name, so the two cannot disagree — and it made that path
  stricter, since an `</em>` whose `<em>` was inside a suppressed element no longer
  decrements a depth it never incremented.
- **IT RIDES `loadToc`'s ARCHIVE OPEN.** Both are "what the book says about itself",
  both are wanted at the same moment, and a second open is ~100 ms and a second
  central-directory parse. The read is capped at 32 KB a sheet — sized against the
  ~133 KB free at book open, not against what CSS can be — and a sheet that will not
  read is skipped rather than fatal.
- **`[css] N italic class(es)` and `[markup] em=N classed=N sample='…'`** are what
  told these apart from the device. A word that should be italic and is not has three
  explanations that look identical on glass: the parse found nothing it reads, the
  wrap lost it, or no italic face is installed. `[page] … emph=N ital=N` separates the
  last two.

**EVERYTHING THE XHTML SAYS THAT `document.h` DOES NOT MODEL IS DROPPED, NOT
APPROXIMATED.** A `<table>` becomes its cells in reading order or nothing, never a
guess at a layout. `<style>` and `<script>` text never reaches a page. **Inline
emphasis is not modelled** — `<em>` contributes its text and no marker — because
there is no italic face to render it with, and a field layout must ignore is worse
than an honest gap.

**LAYOUT ASKS `advance()`, NEVER `glyph()`.** That is design decision 3 of 3A and
`layout.h` is the layer it was made for: a scalable face rasterises inside `glyph()`
at ~3,794 µs a glyph, and a page holds ~600 of them, so a measuring pass that
rasterised would cost seconds to decide where to break a line it has not drawn.
Asserted on the cache's own counter, not on discipline — **0 rasterisations across
6,800 pages** of real EPUBs.

**LINE BREAKING IS NOT IN `layout.h`.** It is `wrapProseLead` in `components.h`,
which already breaks greedily on ASCII spaces against these exact metrics; body text
got `firstIndentF26` added to it rather than a second wrap that would inherit none of
its fixes. Same for drawing: `drawText` and `drawTextJustified` are ONE pen loop
differing by one argument.

**JUSTIFICATION REFUSES ON HOW FULL THE LINE IS, NOT ON HOW FAR A GAP STRETCHES.**
A per-gap cap cannot tell a corridor from prose, because the gap count is what turns
slack into stretch: on `Reader.dc.html`'s own copy a three-space-width cap refused
"necklace, and the two of" — 367px of text in a 444px column — because its 77px of
slack fell across four gaps, and set it ragged directly under a line it had
justified. `kMinJustifyFillPercent` asks the question that is actually visible.
Ragged lines went 18% → 3.4% and what remains is almost all paragraph-final.

**A PAGE BOUNDARY MAY LAND INSIDE A PARAGRAPH, AND MUST.** A 12-line page and a
7-line paragraph means most pages end mid-paragraph. `layoutPage` walks FORWARD only;
`ReaderScreen` paginates the chapter once into a list of page-start cursors, which is
what makes the page counter and the previous page possible at all.

**PARAGRAPHS ARE SEPARATED BY AN INDENT, NOT A GAP** (`Reader.dc.html`), and a
paragraph is indented only if the one before it was also a paragraph — a heading or a
quote is itself the break the indent would announce. A gap would cost a line box of a
12-line page.

### The glyph cache

Sized against the UNION across pages, not against a page. A page is small — the worst
of 5,000 real pages used 36 distinct glyphs and 3,272 bytes — but the arena is a RING,
so a page that introduces a capital the last one did not advances the write pointer,
and on wrap it overwrites whatever is oldest, `e` included. Printable ASCII plus the
32 accents and marks every `fontc.py` subset carries, on the shipped face:

| ppem | 16 | 24 | 29 | 32 | 36 | 41 | 45 | 48 | 64 |
|---|---|---|---|---|---|---|---|---|---|
| bytes | 3,728 | 7,292 | 10,378 | **12,292** | 15,359 | 19,284 | 23,046 | 25,854 | 44,866 |

Bytes go as ppem², so the old 8 KB held the set at **no** reading size, and 16 KB
holds ppem 32 with 25% spare and **ppem 41 not at all**.

**THE BUDGET IS NOW STATED AT ppem 32 AND DERIVED EVERYWHERE ELSE**, which is what
unblocks the Typography `Size` row. This section used to end "a body-size setting must
revisit this — the budget is a constructor argument precisely so the caller can size it
from the chosen ppem", and that was a deferral rather than a mechanism: the two callers
are `static` globals in `shell/src/main.cpp` built **before `setup()` runs**, so the
chosen ppem does not exist at the moment the constructor is called and no caller could
have obeyed it. `init()` is where the size arrives, so `init()` is where the arena is
sized — `ScalableFont::cacheBytesFor(ppem, budget)`.

- **The curve is `u(p) = 39p² + 300p`, and the linear term is not noise.** A glyph's
  row stride rounds up to a whole byte, which is a cost per GLYPH-ROW rather than per
  pixel, so it scales with the height and not the area. Fitted to the table above it is
  good to 1.5% everywhere; ppem² alone is 9% out at 48, in the expensive direction.
- **It scales the caller's budget rather than replacing it**, so the *margin* is the
  caller's decision, stated once. At ppem 32 `cacheBytesFor` returns exactly 16,384 —
  the shipped number to the byte, which is what makes this change invisible to every
  golden and every board measurement.
- **The ceiling is RELATIVE (150%), because the device has TWO of these faces.** An
  absolute cap cannot keep the roman's 16 KB and the italic's measured-cold 10 KB in
  proportion. Roman → 24,576 B, italic → 15,360 B, so the pair's worst case is 39,936
  against today's 26,624: **+13,312 B, and only at the top of the ramp**. Against the
  42,152-byte floor (a book opened through the Library) that leaves ~28.8 KB, and
  against 45,840 (through Home's CONTINUE) ~32.5 KB. Below ppem 32 it gives memory
  *back* — the pair is 22,303 B at ppem 29.
- **Thrash-free to ppem ~46**, which is 22pt at 150 DPI. Past it the arena stops
  holding the union and the cache does what it is built to do: wrap and re-rasterise.
- **`wraps` is the instrument, not a timing.** At ppem 41 with the old flat 16 KB, a
  second pass over the alphabet took **2 cache hits out of 127** and re-rasterised the
  other 125, at ~3,794 µs a glyph on the panel. `test_scalablefont.cpp` asserts the
  second pass rasterises *nothing*, at every step of the ramp.
- **A grow that cannot be allocated keeps the arena it had.** The new block is taken
  before the old one is released, so a failed `new` is slower and never dead — and the
  only thing that re-inits at a new size is a Settings screen with no book open, where
  the heap is ~133 KB rather than the reading floor.

**AND THE PAIR KERN CACHE IS THE OTHER HALF, WHICH TURNED OUT TO BE THE BIGGER ONE.**
`stbtt_GetGlyphKernAdvance` bisects the face's 6,064-pair legacy `kern` table, and
`wrapProseLead` grows every line greedily and re-measures each candidate — so the same
pairs are bisected over and over. Measured on a 56-page pagination walk (desktop, -O3,
best of 20, three runs):

| | ms | µs/page |
|---|--:|--:|
| before | 31.7–32.0 | 566–571 |
| **512-slot pair cache** | **13.8–14.0** | **247–249** |
| `kerning()` removed entirely (the ceiling) | 8.6–8.7 | 153–156 |

So the bisection was **73% of a pagination walk** and the cache recovers **77%** of
what removing kerning altogether would. 512 slots is the knee of a sweep — 128:18.0,
256:14.5, 512:13.5, 1024:13.7 ms — and 1024 is *worse*, because a chapter's pair
alphabet is a few hundred, not a few thousand. 1,536 B a face.

- **It caches its ZEROES, and that is most of the value.** Only **6.5%** of Latin-1
  pairs kern at all once scaled and rounded to whole pixels, and real prose kerns
  **9.7%** of its adjacent pairs — so ~90% of the bisections were finding nothing and
  being repeated.
- **A kern is in PIXELS, so `init()` drops it**, exactly as it drops the advance cache.
  A cached kern outliving its ppem is text that is uniformly, subtly mis-spaced with no
  glyph wrong — invisible to every golden.
- **IT DOES NOTHING FOR THE RENDER**, measured: `reader_sim reader --bench 200` is
  118–128 µs/pass either way. `drawText` walks a string once, so there is nothing to
  amortise. This is a layout win and it should not be quoted as a page-turn win.

**AND THE FIRST RUN OF A FRESHLY BUILT BINARY IS STILL THE SLOWEST BY A WIDE MARGIN** —
the render bench above read 209.6 µs on its first invocation and 118.7 on its third.
This file already records that trap; it reappeared inside the measurement taken to
check the trap had not been fallen into.

### The stack, which is the budget nothing was watching

**stb_image's inflate wants 6,608 bytes in ONE FRAME.** The compiler inlines
`stbi__parse_zlib`, `stbi__compute_huffman_codes` and `stbi__zbuild_huffman` into
`stbi_zlib_decode_noheader_buffer`, so all three `stbi__zhuffman` tables — `fast[512]`
plus `size[288]` plus `value[288]` each — share one frame. Arduino's default
`loopTask` stack is 8,184 usable bytes and the chain above the call spends ~1.5 KB of
it, so **opening any book was a stack-protection fault, every time**, and the reboot
landed back on Home looking like a navigation bug.

`shell/src/main.cpp` therefore carries `SET_LOOP_TASK_STACK_SIZE(16 * 1024)`.

Three things worth keeping:

- **`-DCONFIG_ARDUINO_LOOP_STACK_SIZE` DOES NOTHING.** arduino-esp32 ships
  precompiled, so a `-D` in `build_flags` never reaches its `main.cpp`. The
  weak-symbol override (`SET_LOOP_TASK_STACK_SIZE`, declared in `Arduino.h`) is the
  supported mechanism and the only one that takes effect.
- **Read the frame size off the panic.** `add sp,sp,t0` at the faulting address with
  `T0 = 0xffffe630` is a −6,608-byte allocation; `addr2line` on `MEPC` names the
  function. That is faster and more certain than reasoning about `sizeof`.
- **A stack budget cannot be moved into `core/` to be faked.** The answer to
  "`shell/` has no test harness" has been to move logic where a fake can reach it;
  a stack is not movable, so it is MEASURED instead. `test_inflate.cpp` runs the
  inflate on a pthread with a stack it owns, fills it with a pattern and counts what
  survives — FreeRTOS's own high-water technique. It reports **7,348 bytes** under
  clang and asserts a 10 KB ceiling there, so a vendored-library bump that grows the appetite fails
  on the desktop rather than panicking the device. **THE CEILING IS PER HOST
  COMPILER AND CANNOT BE ONE NUMBER** (`test/unit/stack_ceiling.h`): the same
  chain measures **12,212** under x86-64 gcc, and the streaming decoder 3,072
  against 6,824, so the clang-calibrated ceilings failed the first Linux CI run
  with nothing regressed. Raising one number to cover both was refused — it would
  need clang's appetite to **more than double** before tripping, and clang is
  where nearly all work here happens. **Neither host figure is the device's**:
  the device is gcc-shaped but 32-bit, and its real number is the `[stack]`
  serial line.

The `[stack]` serial line reports `uxTaskGetStackHighWaterMark` after an open — the
worst case since boot, inflate included.

### Memory, which is what a real book runs into

**`new` ABORTS under `-fno-exceptions`, with no message and no stack.** The reboot
looks like a navigation bug — **three times now** it has been reported that way, twice
as "opening a book goes back to Home" and once as a book that **crashed the firmware
on the first press and opened normally on the second**. `MCAUSE 0x2` plus `abort() was
called` plus `addr2line` on the stack words is how you get from that to
`operator new` → `std::bad_alloc` → `__terminate`.

**THIS PARAGRAPH SAID "every sizeable allocation in the EPUB path is
`std::nothrow`-checked and answers with a reason", AND IT WAS TRUE OF EVERY
HAND-ROLLED BUFFER AND FALSE OF EVERY CONTAINER.** There is no nothrow spelling of
`reserve` or `push_back`, and the open path grows five of them from numbers a **FILE**
states — a zip's entry count, a manifest's length, a spine's length, an NCX's entry
count, a stylesheet's size. So the sentence covered the allocations somebody had
written a `Buf` for and silently exempted the ones the standard library makes, which
is the class the third report was. **A comment that overclaims is this project's most
expensive recurring defect** and this is the fourth instance of it recorded here.

**MEASURED, NOT GREPPED**, by replacing global `operator new` and running the real
`openBook` → `loadToc` → chapter walk over all 225 corpus books. Largest **single
contiguous request** per site, which is the number that decides — see "the largest
free BLOCK decides" below:

| bytes | site | who guards it |
|--:|---|---|
| 64,080 | `Epub::open` → the OPF string | `Zip::read`'s probe, since 3A |
| 39,610 | `Zip::open` → the central directory | nothrow `Buf`, since 3A |
| 36,956 | `Inflater::begin` → the window | nothrow, since 3C |
| 32,768 | `loadToc` → `vector<TocEntry>` | `pushOrRefuse` |
| 24,576 | `Epub::open` → the manifest vector | `pushOrRefuse` |
| 17,920 | `Zip::open` → `entries_.reserve(claimed)` | `ensureRoom` |
| 16,640 | `readItalicClasses` → the stylesheet | `appendOrRefuse` |
| 15,408 | `Epub::open` → `chapters_.reserve` | `ensureRoom` |
| 12,288 | `Epub::open` → the spine vector | `pushOrRefuse` |
| 8,194 | `BlockReader::next` → `Block::text` | nothrow probe + `reserve`, **#90** |
| 5,136 | `openBook` → `out.chapters.reserve` | `ensureRoom` |

**AND THE PHASE THAT PEAKS IS `loadToc`, NOT THE CHAPTER WALK**, which is where every
one of those unguarded sites lived. Across the user's own 16 books the toc-and-styles
phase peaks at **47.6–91.8 KB** against a flat **~48–51 KB** for a chapter walk of the
book's longest chapter — so the expensive moment of a book open is the one that reads
what the book says about itself, and the reader's own 36,956-byte window is the
cheaper half. (Desktop figures. The device's cover work measured **17–25 KB above**
its desktop twin for the same allocations, because the allocator is simply different,
so treat these as a floor.)

**`reader/heapguard.h` IS `Zip`'s OWN PROBE, MOVED BEFORE A FOURTH COPY OF IT WAS
WRITTEN.** `canAllocate` had lived in that file's anonymous namespace since 3A with a
comment saying it was "a poor substitute for an interface that could report failure";
`Heap::hasBlock` is the same five lines, plus `ensureRoom` / `pushOrRefuse` /
`appendOrRefuse` over it. Four things about it:

- **It asks for a BLOCK and never a total**, and it asks while the container's OLD
  buffer is still held — which is exactly the state a reallocation is in.
  `getFreeHeap()` answers the wrong question, as this file already says two bullets
  down.
- **It grows GEOMETRICALLY and falls back to the exact size when a doubling is
  refused.** Reserving what was asked for each time would make a 32 KB stylesheet read
  64 reallocations; doubling asks for twice what is needed, so near the limit it would
  refuse a book that fits. **The fallback is the half a mutation catches and nothing
  else does** — the corpus never comes near the ceiling.
- **Failure is INJECTED for the tests**, because the desktop cannot be made to fail an
  8 KB allocation: `Heap::install` swaps the allocator question the way `Profile`
  installs a clock `core/` must not acquire for itself.
- **`test_heapguard.cpp` IS A PROPERTY AND A SITE SET, AND IT NEEDED BOTH.** The
  property refuses every probe the open path makes, from the Nth onward, and demands a
  refusal whose reason maps to `BookErrorReason::OutOfMemory`. That alone **cannot see
  a REMOVED guard** — a deleted guard makes no probe, so the walk has one fewer element
  and every remaining one still passes; deleting the entry-list guard passed all 1,403
  cases. So each site's own words are asserted too. Per-site **words** rather than a
  probe count, because a count is a fact about the standard library's growth ladder and
  libc++ and libstdc++ double from different capacities.

**WHAT IS DELIBERATELY LEFT UNGUARDED, stated rather than implied:**

- **`Block::text` WAS the largest unguarded allocation here at 98,304 bytes, and on
  this tree it is neither.** That figure was measured against a `kMaxBlockBytes` of
  64 KB, which #90 has since derived down to **8 KB** and reserved once through a
  nothrow probe — so the request is **8,194** and it refuses rather than aborting. The
  two changes were written on separate branches and neither touched the other's file,
  which is why the table above needed correcting on the merge rather than either half
  being wrong. **The 98,304 is kept as the before figure**, because it is what #90
  removed and it is the largest single number this path has ever asked for.
- **Everything under ~2 KB**: `cssPaths_` at 8 entries, italic class names at 64,
  `Epub::Chapter`'s two path strings, `Block::emphasis` at 256 `Span`s. They are
  bounded and small, and a guard on each buys a branch rather than a refusal.
- The nothrow sites above are **not** re-guarded. They already refuse.

**TWO FALSE CLAIMS WENT WITH IT, BOTH PRE-EXISTING.** `Epub::open` collapsed "the
entry is absent" and "the entry would not read" into one message, so an out-of-memory
inside the container read arrived as *"this is not an EPUB"* — and would have reached
the panel as `appears damaged`, about a book that is fine. And `Zip::read`'s own
refusals said **"chapter"**, when its one caller is `Epub::readEntry` reading a
container and an OPF, so the noun was wrong at every site it can fire from.

**AND THE `book.cpp` HALF OF THIS SENTENCE HAD BEEN FALSE SINCE 3C.** It read "a
pre-flight probe in `book.cpp` before `buildDocument` (whose `std::string`/`std::vector`
growth cannot fail politely — that one is a bound, not a guarantee)". `openBook` has not
called `buildDocument` since it stopped returning a chapter's blocks and started
returning the spine's geometry; there is no probe in `book.cpp` and there is nothing
there for one to guard. The **claim** it was making survived the move, though, and it
moved down a layer with the work: the growth that cannot fail politely is
`Block::text`'s `push_back`, and *"a bound, not a guarantee"* was exactly right about it
— **a bound of 64 KB against a 42,152-byte floor, which is a bound that cannot be
honoured.** #90 made it a guarantee: the buffer is reserved once through a nothrow
probe, so the growth cannot allocate at all, and the cap is derived from the floor. See
**The lifetime rules that changed**.

**THE EOCD SCAN NO LONGER ALLOCATES.** It used to take the whole 64 KB comment
window in one `std::string`, which was the largest single allocation in the reader
and the first thing a full-length novel broke: 142 KB free and no contiguous block
that size. It reads 2 KB chunks on the stack, backwards, with a 3-byte overlap so a
signature at a chunk edge still reads whole. **Every zip fixture in the repo is
smaller than one chunk**, so the loop is covered by tests that append comments sized
either side of 2048, 4096 and 65535 — without those the rewrite was untested.

Two things about the numbers:

- **A cap is not a memory check.** `kMaxEntryBytes` is 512 KB, which protects against
  a file that lies about its size and does nothing about a file that is honestly too
  big for a 140 KB heap. Those are different failures and need different code.
- **The largest free BLOCK decides, not the free total.** Every reader buffer is one
  contiguous allocation. The `[open]` refusal line reports both.

**The Library is resident while you read.** It sits below the Reader on the stack, so
its entries stay allocated: 203 books cost ~59 KB (heap 201,576 → 142,560 in the
boot log), taken out of the heap exactly when a chapter needs it. **3C made this stop
mattering** — a chapter now peaks at ~70 KB whatever its length — so it is a saving
available if something later needs it, not a blocker.

### What the desktop measures, and what only the panel can answer

Desktop, 12-line page, 444px column, ppem 32: paginate 349 µs/page, lay out one page
168 µs, draw a page 580 µs cold (29 rasterisations) and 363 µs warm. The device is a
160 MHz RISC-V with no FPU and rasterises at ~3,794 µs a glyph, so a cold page is
~110–140 ms there and the pagination walk is the part with no desktop analogue worth
trusting. The `[open]` serial line reports locate, total, pages and the heap cost of an
open for exactly this reason. **It said `parse` and `blocks` here long after the line
stopped printing either** (`af622f1`), which is the cheap half of the same defect #89
found in the line itself: two of its fields were LITERALS, `ch=1` and an `entry=` reading
spine entry **zero** — the cover, a file that is never decoded — so the only size on a
line attributing decode cost described the wrong file. Both were correct while
`openBookAt` could open nothing but entry 0, and both went stale at the same moment, when
the restore learned to open at a saved spine. The field is `spine=` now rather than `ch=`,
and that word IS the convention marker: it is the raw 0-based index every other line in
this log already means by it, where `ch=` is the GLASS's word and the glass counts from
one.
