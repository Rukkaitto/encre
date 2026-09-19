# NAMES — answering "who's this character again?" on a device with 45 KB free

**Status:** design approved 2026-08-24. **Rewritten 2026-09-19, and the rewrite changed
the answer rather than the wording.** Release **v0.3.0** — #156 (index), #157 (list
screen), #158 (alias rows), #159 (the `ReaderMenu` row #73 cut while the family was V2),
plus work none of those four describe (below). Depends on 3D (peek and return), which
shipped, and on `ReaderMenu`, which shipped without the row. Roadmap entry: 3E.

## What the rewrite changed

The 2026-08-24 design's central claim was that selecting a name opens a peek directly,
which "deletes a screen, its board, and the stored-sentence problem in one move". A
second screen was asked for instead: a name opens a list of its **mentions**, each a
short extract of the sentence it appeared in, and any one of them opens the peek. That
is the opposite trade, and it has to be paid for.

| | 2026-08-24 | now |
|---|---|---|
| selecting a name | opens the peek | opens the **Mentions** list |
| per name on the card | one `(spine, block)` cursor | up to **8** cursors **plus a 64-byte extract each** |
| sentence text | never stored | stored, truncated, centred on the name |
| index size, `Le Fléau` | 14.8 KB | **19.6 KB** + 205.6 KB of extracts |
| card layout | one file | an index plus **write-once chunked per-chapter files** |
| the scan | rides the page count | page count **plus a second walk** per chapter |
| coverage badge | `TO CH. 07` in the band | **removed** |
| chapters behind the reader | never scanned | **backfilled**, forward, in the quiet window |

**Three of the original design's load-bearing facts turned out to be false about this
tree**, found by reading it rather than by building:

- **The chapter-crossing window does not exist.** The old plan put the merge where
  "`openChapterAt` releases the old chapter before opening the new one, so the
  36,956-byte inflate scratch is unallocated exactly then". `openChapterAt`
  (`core/src/screen_reader.cpp:197`) frees only the page index; `ChapterReader::begin`
  deliberately **reuses** the window across a crossing, with a comment saying why
  (`core/src/chapter.cpp:136-140`). The only production caller of `release()`
  (`core/src/chapter.cpp:78`) is the peek-open path, `shell/src/main.cpp:8543`.
- **The position save does not share that window either.** The crossing save is
  `saveReadingPosition("chapter")` at `shell/src/main.cpp:8472`, in the shell *after*
  dispatch returns, with the new chapter already open and its scratch already allocated.
- **The scratch is 37,056 bytes, not 36,956.** `Inflater::kHeapBytes` is
  `kWindowBytes + kInputBytes + 3 * 640 + 320` = 32768 + 2048 + 1920 + 320
  (`core/include/reader/inflate_stream.h:82`). Five places in the repo's prose say
  36,956 — `cover.h:146-147`, `chapter.h:72,82,100` — and the arithmetic says otherwise.
  A hundred bytes changes no decision here; it is recorded because every figure in this
  file is meant to be re-runnable, and that one was copied rather than computed.

## The question, and why it took a probe first

A reader 300 pages into a novel meets a name and cannot place it. Kindle answers with
X-Ray, which is precomputed on Amazon's servers and reached through a touchscreen; Encre
has neither — there are six buttons and no pointer.

So the riskiest question was not the UI. It was whether a heuristic a 160 MHz part could
run yields a list worth reading at all, and it was cheap to answer before designing
anything: `tools/name_probe.cpp`, run over real novels.

**It does.** `Le Fléau`'s top 26 is 26 of 26 real named entities — Stu, Larry, Harold,
Nick, Frannie, Ralph, Glen, Nadine, Flagg, La Poubelle, Kojak the dog, and `Dieu`, which
is a proper noun and so correct on a screen that says NAMES. **No false positives.**
`Neuromancien` returns Case, Molly, Armitage, Maelcum, Riviera, Wintermute, Chiba,
Tessier-Ashpool. `Darkly Dreaming Dexter` returns Deborah, Deb, Dexter, LaGuerta, Harry,
Rita, Dark Passenger, Captain Matthews, Sergeant Doakes. Nothing in the rules is
vocabulary, which is why it is not French-specific.

The probe's own header carries the eleven rules and the failure each one fixed. Two are
worth restating because they are the ones a reimplementation would get wrong:
**sentence-initial suppression is load-bearing** (ranking on total mentions floods the
list with `Il`/`Je`/`Et` — a French pronoun starts thousands of sentences), and **the
introducing sentence is the earliest of the best kind, not the best-scoring** (a running
best that rewards length picks the most verbose sentence in the novel).

**Every figure in this file was re-measured on 2026-09-19 with one instrument**, a
scratch copy of the probe carrying the per-chapter admission simulation and the extract
cap. The probe reproduces the 2026-08-24 figures to the digit — 4,093 runs / 85,001
bytes whole book, 412 / 7,797 worst chapter, 738 admitted / 14,812 bytes — so the tree is
at the state those numbers were taken at.

**ONE OF THOSE FOUR NO LONGER DESCRIBES WHAT SHIPS, AND THE REASON IS WORTH THE
PARAGRAPH.** Porting the scan into `core/` surfaced that the probe uses **two
definitions of mid-sentence**: it RANKS on mentions that are neither sentence-initial
nor speech-initial — the opener rule, which is what stops French pronouns scoring —
and its ADMISSION counter checks position only. The firmware uses the strict count for
both, because a rule that differs from its own name by an invisible clause is the
drift this file is mostly a record of. **So admission is 722 runs and 14,547 bytes
where the probe says 738 and 14,812**, and every figure below that depends on
admission is re-measured under the one rule. The 16 runs cost nothing a reader could
see: the display threshold is five of the strict count, which none of them reaches.
Relaxing `core/names.cpp` to the probe's rule reproduces 738/14,812 exactly, which is
how the two were told apart.

## The constraint that forces the design

**The probe holds every candidate for the whole book because a desktop can.** The device
cannot, and it is not close:

| | runs | bytes of keys + counters |
|---|---|---|
| `Le Fléau`, whole book | 4,093 | **85,001** |
| `Le Fléau`, worst single chapter | 412 | **7,797** |
| `Neuromancien`, whole book | 1,116 | 22,142 |
| `Neuromancien`, worst chapter | 228 | 4,219 |
| **device heap floor, measured** | | **45,840** |

Those figures store no sentence text at all. A whole-book table is 1.9x the entire margin
before the feature has anything to show. One chapter fits with room.

**A second constraint, confirmed in `filesystem.h` rather than assumed: there is no
streaming write.** `writeAll` takes a whole buffer and truncates
(`core/include/reader/filesystem.h:156`); `FileHandle` is read-only and seekable
(`:71-93`). `readAll` is capped at 64 KB, and the cap is `SdFileSystem`'s rather than the
interface's (`shell/src/sd_fs.h:273`), so a desktop test will happily read a file the
device refuses.

**A third, new to this revision: two live inflate scratches do not fit**
(`core/include/reader/chapter.h:69-93`). Anything that needs to read a chapter other than
the one the reader has open must release first, which is the peek's move and nobody
else's.

Together they determine most of what follows:

- **the scan is per chapter**, and the accumulator is the card
- **extract text is bounded and written once**, never rewritten
- **the merge streams**, because two copies of the index do not fit
- **reading another chapter costs a release and a reacquire**

## Three pieces and two screens

| | Piece | Lives in | Knows nothing about |
|---|---|---|---|
| **Scan** | a `Block` stream → this chapter's candidate runs | `core/` | files, cards, screens |
| **Capture** | a second walk → this chapter's extracts | `core/` | pixels |
| **Store** | index + per-chapter files | `core/` (format) + shell (I/O) | pixels |
| **Names** | alphabetical list of groups; a row opens Mentions | `core/` + theme | storage |
| **Mentions** | one name's sightings; a row opens the peek | `core/` + theme | storage |

`Mentions` is the word in the code and on the glass — `ScreenId::Mentions`,
`screen_mentions.{h,cpp}`, `Mentions.dc.html`, compare id `mentions`. There is no
internal/external split, and no screen is called `Extracts`.

**One term has to be kept apart from itself.** A *mention* in the probe is a mid-sentence
occurrence counted for ranking — Stu has 941. A `Mention` in the store is one of the
**eight** sightings kept with an extract. The count never reaches the glass, so a reader
sees no contradiction; code and docs say `mentionCount` for the ranking figure and
`Mention` for a stored sighting.

## The scan

**It rides `countPages`** (`core/src/screen_reader.cpp:348`), the pagination walk that
already decodes every block of the chapter, called by `buildIndex` for chapters under
`kEagerCountBytes` (8 KB, `core/include/reader/screen_reader.h:261`) and by
`completeIndex` in the quiet window for larger ones. So a chapter's names become known
exactly when its page count does, and the scan costs no I/O of its own and no extra
decode.

**The hook is inside `countPages` and the chapter table is scoped to one walk, committed
only on `CountOutcome::Counted`.** That is not a detail. A `completeIndex` walk is
interruptible every block and a walk that is abandoned is discarded whole and retried
from scratch, so a scan that accumulated across an abandoned walk would double-count
within a single chapter open. Three other functions also walk every block —
`openAtCursor`, `rewalkToCurrentPage`, and `seekTo`'s rewind — and none of them enters
`countPages`, so they are excluded by construction rather than by a guard someone has to
remember. Hooking at `chapter_.next()` would catch all four and need a caller list, which
is the shape this project has twice had to convert into a function.

Its whole memory is one chapter's candidate table: **7,797 bytes worst case**.

**Grouping runs at DISPLAY time, not scan time.** The probe's grouping — containment,
prefix edges, the plural guard, the 3:1 dominance tie-break, union-find — is a batch
operation over the whole candidate set, and it is validated as such. Doing it
incrementally per chapter would freeze decisions that later chapters should be able to
change: `Fran` is unambiguous until `Frank` appears, which may be thirty chapters later.
So the card holds **raw runs** and the screen groups them when it opens.

## What earns a slot on the card

**There are two thresholds and they answer different questions.** This one is about
*storage* — what the card keeps, so that a name's count can go on growing. The display
threshold further down is about *navigability*. They are deliberately different numbers:
the index has to admit a name before anyone knows whether it will end up frequent.

A run is admitted only when **one chapter alone saw it at least twice mid-sentence**:

| | admitted runs | bytes | share of the naive table | most admitted by one chapter |
|---|---|---|---|---|
| `Le Fléau` | 722 | **14,547** | 17.1% | 63 |
| `Neuromancien` | 167 | 3,300 | 14.9% | 33 |

Admission at the door rather than eviction after the fact is the point. Eviction by count
would thrash: a name at two mentions is dropped, reappears with two more, and is stored as
two again — losing history exactly for the mid-frequency names the feature exists to
serve.

**What this gives up:** a name that appears once per chapter across many chapters is never
admitted. That is a real gap, it is accepted, and it is the price of a bounded index.

## Mentions

**Eight per name, the first eight in reading order, one row per sighting.** Measured over
`Le Fléau` at a 64-byte extract, one instrument:

| cap | extracts | card | worst chapter file | index | chapters per name |
|---|---|---|---|---|---|
| 4 | 2,398 | 137.7 KB | 11.9 KB | 17.7 KB | 1.40 |
| **8** | **3,594** | **205.6 KB** | **17.8 KB** | **19.6 KB** | **1.92** |
| 12 | 4,338 | 247.5 KB | 20.5 KB | 20.7 KB | 2.23 |
| 20 | 5,191 | 295.8 KB | 22.8 KB | 21.9 KB | 2.59 |

Eight is about one screenful, so a name's whole list fits without a rail, and the
chapters-per-name figure is what the read costs: **1.92 file opens on average, 7 at
worst**. Keeping the *first* eight rather than a spread keeps the introduction — the
sighting the probe's rule 4 exists to find — and keeps a name's list stable, where a
spread reshuffles as the reader goes on.

**The extract is 64 bytes centred on the name, not the sentence start.** The mean sentence
is 124 bytes, so 64 bytes taken from the front frequently stops before the name appears
and shows a row that does not contain what it is about. Centring costs nothing and is the
only version that is always correct.

**Full sentences cannot ship**, and the numbers say so rather than a preference:

| extract | card | worst chapter, one `writeAll` | one-pass capture peak |
|---|---|---|---|
| full sentence | 437.1 KB | **52.8 KB** | **82.4 KB** |
| 96 B | 277.0 KB | 24.8 KB | **39.4 KB** |
| **64 B** | **205.6 KB** | **17.8 KB** | 28.3 KB |
| 48 B | 160.7 KB | 13.7 KB | 21.7 KB |

Against a 45,840-byte floor, the full sentence cannot be written at all.

### The capture is a SECOND walk, and skipping it is not an option

Admission is decided at the **end** of a chapter, so a capture that runs during the count
walk would have to hold candidate extracts for every run in the chapter — 28.3 KB at 64
bytes, beside the 37,056-byte scratch that walk is using. Capturing only for runs already
admitted *before* the chapter avoids that and is far worse:

| book | names left with **no mentions at all** |
|---|---|
| `Le Fléau` | **338 of 722** (47%) |
| `Walden` | **86 of 133** (65%) |
| `Darkly Dreaming Dexter` | **36 of 75** (48%) |

A name that is admitted in chapter 12 and never seen again would keep nothing, and a row
that opens onto an empty screen is worse than a feature that is not there. So the chapter
is walked twice: pass one counts pages and decides admission, pass two captures extracts
for admitted runs. The cost is one extra inflate and block decode per chapter, deferred.

## The card format

**`/.reader/names/<hash>/`**, reusing the reading position's FNV-1a 8-hex naming
(`core/include/reader/reading_position.h:205`), and for the same reason: a book path holds
`/` by construction and real cards carry accented 90-character titles.

**`index`** — line-oriented, **sorted by run name**, which makes the per-chapter merge a
linear two-way merge rather than a sort. Per line: name, non-initial mentions,
chapter-opening mentions (the furniture cut), and up to eight `(chapter, count)` pairs
naming where this run's extracts live. **19.6 KB** for `Le Fléau`.

**Not JSON.** `core/include/reader/json.h` is one flat object with no nesting and no
arrays, bounded at 64 pairs, and this is a list of 722 things.

A header carries five things:

- **a version**, so a format change discards rather than misreads
- **the book path and `bookBytes`**, the identity check the reading position already uses
  (`reading_position.h:67`); a mismatch starts over, because an index for a different book
  is worse than none
- **a bitmap of scanned spine entries** — 23 hex characters for a 92-chapter book. Without
  it, re-reading a chapter **scans it twice and double-counts**, inflating every mention
  and admitting runs that had been correctly rejected. Re-reading is normal, so this is
  not an edge case, and it is the field whose absence would be the worst bug in the
  feature.
- **the admission threshold in force**, so changing it invalidates the index rather than
  mixing two populations into one set of counts.
- **the extract cap in force**, for the same reason.

**That sort order is for the merge, not for the screen.** The file is ordered by RUN name;
the list is ordered by GROUP display name, which only exists after grouping, and grouping
happens when the screen opens.

**`NN-0`, `NN-1`, …** — one chapter's extracts, **written once and never rewritten**. Each
entry is a run name, a block index and a 64-byte extract, in block order. They are chunked
at an **8 KB buffer** because the capture walk holds the 37,056-byte scratch throughout,
and the worst chapter's file is 17.8 KB — 55 KB together, against a 45,840-byte floor.
Repeated `writeAll` to numbered paths is the only streaming write this filesystem has.

Chunking costs nothing on read: entries come out in block order rather than name order, so
a lookup scans a chapter's parts either way.

### The merge streams, because two copies do not fit

Holding the old index, the new index and the chapter's table together is 20 + 20 + 7.8 =
**48 KB against a 45,840-byte floor**. The index file is sorted and `FileHandle` is
seekable (`core/include/reader/filesystem.h:152`), so the old copy never goes resident:
read a line, merge, append to the output. Peak is about **28 KB**, and no window has to be
created, which matters because the window the 2026-08-24 design named does not exist.

### Merge and capture are ONE deferred job, and the index is written LAST

Per chapter the quiet window runs, behind the page count: merge the index, then capture
the extracts, then write the index and set its scanned-spine bit. Writing the index last
means an interruption — card pulled, battery flat, a button — leaves orphan extract parts
and an index that never knew about them, so the chapter is simply rescanned. The
alternative, writing the index first and tolerating a store that promises more than it
holds, is the reports-on-less-than-it-claims shape this repo keeps a list of.

### Eviction leaves orphan bytes, and that is accepted

A name's eight slots are filled in reading order, so a chapter arriving out of order —
a Contents jump followed later by the chapters in between — evicts. The extract files are
write-once, so the evicted bytes stay where they are with nothing referencing them. That
is dead weight in a file a lookup reads selectively, not corruption. Reclaiming it means
rewriting a chapter file, which is the streaming-write problem that forced chunking.

**Nothing evicts the store itself.** It is deleted when its book is deleted, as
`article_store` already does for article state. A book removed from the card by hand
leaves an orphan directory that a hash name cannot explain; that is the known cost of
having no general bound.

## Backfill

**The index covers chapters that have been OPENED, and that is not the same as chapters
read.** Three ordinary situations produce a reader at chapter 40 with an index covering
nothing: updating from v0.2.0 halfway through a book, jumping into the middle with
Contents, and arriving from another device. Each of them loses the introductions, which
are the most valuable extract the feature has.

**The spoiler argument survives, because it was always about scanning AHEAD.** Scanning
chapters behind the reader's furthest point spoils nothing — they chose to be past them.

- **Bounded by the saved position's spine** (`core/include/reader/reading_position.h:55`),
  the one durable number the card already holds. A reader at chapter 40 backfills 1–39. A
  reader who jumped to 40 and then went back to 1 backfills nothing, and loses nothing
  they had.
- **Forward from the lowest unscanned chapter.** Introductions land first, and within
  backfill nothing ever evicts, because sightings arrive in the order the cap wants them.
- **One chapter per quiet window, LAST**, behind the position save, the page count, the
  refinement, the restream and the ring warm — everything the reader can see wins. One
  chapter per window means a button is never more than one chapter away, and the count
  walk's existing per-block stop function is the interruption mechanism.
- **It releases and reacquires**, because two live scratches do not fit: release the
  Reader's chapter, open the backfill chapter headless, scan, capture, **release the
  backfill chapter, then reacquire the Reader's**. Releasing first means the request is
  for a block of exactly the size just returned, which is the benign case for
  fragmentation. If it fails anyway, backfill stops for the session and says so in the
  log; the Reader is restored by the path the peek already uses
  (`shell/src/main.cpp:8546-8552`).
- **The idle timer is the bound.** Backfill runs in quiet windows until `IdleTimer` sleeps
  the device and resumes on wake from the scanned-spine bitmap. No new setting, and a
  reader who walks away loses no work.
- **Nothing on the glass says it is happening.** The list grows between visits. This was
  weighed against bringing back a coverage badge and the badge lost: the steady state is
  the screen as designed, and a growing list is a smaller confusion than a permanent
  label nobody can act on.

## The screens

`Reader → Names → Mentions`. **Names replaces the reader menu** rather than stacking on
it, and the peek is pushed after Mentions pops, which is what Contents already does
(`shell/src/main.cpp:8535-8544`). Nothing goes more than three deep.

**`releaseChapter()` before pushing Names.** Grouping 722 raw runs needs roughly 20 KB —
names, counters and union-find — and with the Reader below still holding its 37,056-byte
scratch that is 57 KB against the floor. The peek's move applies unchanged, and
`reacquireChapter()` brings the page back.

### Names

**Alphabetical.** The reader always arrives knowing the string, because they just read it
on the page, and alphabetical is the only order where knowing the name tells you where to
look. Most-mentioned-first is ordered by the inverse of need: **the name you cannot place
is rare**, so it would sit near the bottom of a list hundreds long, and its position would
move as you read on.

**Display threshold: 5 mid-sentence mentions**, unchanged by the second screen:

| threshold | groups, `Le Fléau` | screens at ~8 rows |
|---|---|---|
| ≥2 | 713 | 89 |
| ≥3 | 479 | 60 |
| **≥5** | **300** | **38** |
| ≥8 | 211 | 26 |
| ≥12 | 143 | 18 |

`Neuromancien` gives 83 at ≥5, so `Le Fléau` is the worst case on the shelf. A higher
threshold cuts precisely the rare names the feature exists for, and a lower one is
unnavigable.

**How long the list is depends on the EDITION, and how big the store is does not.** Two
EPUBs of `Darkly Dreaming Dexter` measure **35 and 42 groups** at ≥5 — the 2026-08-24
spec quotes 42, which is the Random House edition — while their stores agree within one
percent: 78 against 77 admitted runs, 405 against 409 extracts, a 2.1 KB index either
way. The obvious explanation is wrong: the two split the book into 34 and 58 spine
entries, and admission is per chapter, so a finer split ought to admit more runs. It
admits one fewer. The difference is in the text the two editions carry, not in how it is
chopped up. **Quote an edition with any display-threshold figure**, and expect no such
care to be needed for a storage one.

**Rows** are the name in Body500 over its fullest form in tracked uppercase Meta — `Stu`
over `STUART REDMAN`. **Two heights, 90px with a fullest form and 60px without.** A place
or an already-full name has nothing for a second line, and that is 30–40% of rows on a
real book, so a blank second line would read as broken on a third of the screen.

**No chevron.** Every list on this device opens something from a row without one; a
chevron is for a settings row that discloses a panel. The Confirm hint carries it.

**Header band: `NAMES` alone.** `TO CH. 07` is gone. Its argument — that coverage is the
one thing a reader might otherwise wonder about — was real, and backfill is most of the
answer to it: the gap it explained now closes by itself.

**Hint bar: `BACK · MENTIONS · UP · DOWN`.** The board says `PEEK` today and that is wrong
in this design whatever else changes, because Confirm no longer opens a peek.

**Names reopens on the last name focused**, for as long as the book is open. A reader
looking up a second name in the same scene is looking up a neighbour far more often than
not, and the alternative is scrolling the alphabet again after every peek. No card state.

**Thin and empty are a variant, not a screen** — `HomeEmpty`'s pattern: same `ScreenId`,
same view model, copy in place of rows.

### Mentions

**Header band: `MENTIONS` left, the fullest form right** — `MENTIONS` / `WILL
LADISLAW`. The label names the screen, which is what every band here does; the right
slot is a fact about what you are looking at, which is Contents' and Library's rule,
and the fullest form is often most of the answer before an extract is read. **Where a
name has no fuller form** — a place, or a name the book always writes in full, which is
the one-line row on Names — **the slot carries the name itself**, because with the label
naming the screen it is the only thing on the band identifying whose mentions these are.

That makes the yielding question the ordinary one rather than Contents': the label is a
literal that always fits and the value takes the remainder, which is five of the seven
band call sites in this firmware. `min-width: 0` still goes on the value, because the
value is content and a long fullest form must elide rather than wrap (#82, #134).

**The band said the NAME in the first draft of the board**, with the fullest form beside
it, and the yielding rule was then Contents' — two content runs, the left naming the
screen and never allowed to elide. It was changed because a band that does not say which
screen you are on would have been the only one in the firmware, and #82 is on record that
a band unable to name its screen is worse than a value cut short.

**Rows** are the extract, with the chapter shown only when it changes — a section header
above its runs, which is the shape Contents and Settings already draw. A name's eight
sightings span 1.92 chapters on average, so most rows would otherwise repeat their
neighbour's label.

**Hint bar: `BACK · PEEK · UP · DOWN`.** With the band naming the screen, `MENTIONS`
reaches the glass in two places — Names' Confirm hint and this band — and they are a
promise and its arrival, so neither may be reworded alone.

**Selecting a row opens the peek at `(spine, block)`**, and the peek takes a spine and
nothing else today (`core/include/reader/screen_peek.h:103`,
`core/include/reader/screens.h:427`). The landing mechanism exists —
`ReaderScreen::restoreAt(Cursor)` is public (`core/include/reader/screen_reader.h:426`)
and must be called before `setMetrics`, which `core/src/screens.cpp:772-773` already does
in the right order — so this is a `PeekScreen` constructor argument and a `setPeek`
overload, not a new mechanism. `GO HERE` then reaches `goToPosition`
(`core/src/screen_reader.cpp:856`) unchanged.

**The stored cursor is `(spine, block)` — deliberately one field shorter than a reading
position.** `fitOf` already grades `line` as the field that survives neither a re-layout
nor a re-bind, and a landing only needs the paragraph. Dropping it means the store
survives a type-size change where a reading position would degrade. `PeekScreen` already
zeroes the line for the same reason (`core/src/screen_peek.cpp:126`).

### Rows are one new primitive, shared

Neither existing row type fits. `ListRow` (`core/include/reader/viewmodel.h:570`) is
label-left and value-right, where both screens stack their second line underneath.
`drawBookRow` stacks, but its height is **content-independent by design** — "an empty
author line must not make a row shorter than its neighbours"
(`core/include/reader/components.h:540-542`) — which is exactly the rule Names has to
break. So a stacked-row primitive is added, drawing a lead line over an optional secondary
line with the height following whether the second line is present, and both screens use
it. The second copy is the extraction point, and here both copies arrive together.

### Restore

Both screens are `Restore::NeedsPriming` (`core/include/reader/app.h:249`). A wake on
Names primes the index from the card; a wake on Mentions needs the selected name, which
the session record does not carry, so **the record gains it as an optional key with no
version bump**, which is `core/src/reading_store.cpp:18-28`'s documented precedent for
adding an optional field. #49 records that every new screen rediscovers this; two screens
means two answers, stated here so neither is rediscovered.

## What this does not do

**It does not tell a person from a place.** The screen says NAMES, and Boulder, Las Vegas,
New York and Miami all rank, correctly. Separating them needs a per-language word list, a
table `core/` has twice refused to carry.

**It does not display a mention count.** The ranking figure is computed to threshold and
then thrown away. A number beside a name reads as a database statistic.

**It does not scan ahead**, which is what makes it spoiler-proof by construction rather
than by a rule someone has to remember. Backfill only reaches chapters behind the reader.

**It does not survive a book changing.** `bookBytes` disagreeing discards the store whole,
exactly as it discards a reading position — and backfill then rebuilds it.

**It does not run on articles.** A wallabag article reads through `openBook` like any
other EPUB, so the scan would fire on it; the reader menu already knows which it is, and
the Names row is hidden and the scan skipped. Measured on a four-entry essay: 2 admitted
runs, 4 extracts, a 59-byte index. The store would be mostly header, one directory per
synced article.

**It does not fix the pronoun residue.** `Il` survives at rank 57 in `Le Fléau` with 27
mentions, below anything a reader scrolls to.

## Risks

**The scan's cost on device is unmeasured, and the capture walk doubles it.** The whole
book is ~0.5 s on the desktop, so ~5 ms a chapter — but this project's ~37x ratio is a
**render** ratio and its ~135x figure is for work that touches the card or the inflater. A
name scan is neither. **Backfill triples it** for chapters behind the reader, since each
one pays a release, an open, two walks and a reacquire. **Measure before tuning, and read
the `mark()` trail rather than reasoning from code shape** — that instruction is in
`CLAUDE.md` because guessing here has been wrong twice.

**Peak memory has three separate sites and all three are arithmetic, not measurement:**
the 8 KB chunk buffer beside the 37,056-byte scratch during capture, the ~28 KB streamed
merge, and grouping's ~20 KB at screen-open. Each needs a `heapguard.h` assertion on the
desktop and a `mark()` reading on glass.

**Backfill's release-and-reacquire is the one that can strand a reader.** A failed
reacquire leaves the Reader on a page it cannot repaint. Releasing the backfill chapter
first makes the request same-size and therefore benign, and a failure stops backfill for
the session — but this is the path to watch on hardware.

**`Le Fléau` is the worst case on a five-EPUB sample**, and by a wide margin:

| book | spine | admitted runs | extracts | extract bytes | worst chapter file | index |
|---|---|---|---|---|---|---|
| `Le Fléau` | 92 | 722 | 3,594 | 205.6 KB | 17.8 KB | 19.6 KB |
| `Walden` | 44 | 133 | 534 | 31.5 KB | 7.7 KB | 3.4 KB |
| `Darkly Dreaming Dexter` (ePubLibre) | 34 | 75 | 391 | 20.3 KB | 2.5 KB | 2.0 KB |
| `Darkly Dreaming Dexter` (Random House) | 58 | 76 | 406 | 20.6 KB | 2.2 KB | 2.1 KB |
| a four-entry essay | 4 | 2 | 4 | 0.2 KB | 0.2 KB | 59 B |

**The 64-byte extract's rendered width is assumed, not checked.** It is ~55 characters of
French once accents are counted, which is about one line of a 480px row at Meta — and a
board is rasterised by Chrome from the webfont while the device uses the prepped TTF, so a
string within half a percent of the measure lands on opposite sides of the break. The
board must not put a line on the wrap boundary.

## Verification

- **The grouping rules keep their own unit tests**, so the eleven the probe earned cannot
  drift: sentence-initial suppression, the abbreviation guard, maximal runs,
  earliest-of-best-tier, the lowercase-after-comma test, apostrophe trimming, the
  containment link with no ratio guard, the fuller-name tie-break, prefix edges with the
  plural and dominance guards, the furniture cut, and NBSP-as-whitespace.
- **Scanning a chapter twice yields the same store as scanning it once.** That is the
  scanned-spine bitmap's entire job, and the property that fails loudly if it is dropped.
- **An abandoned count walk changes nothing.** The commit-on-`Counted` rule, proved by
  driving `countPages` with a stop function that fires mid-chapter.
- **A merge is order-independent**: chapters 1,2,3 and 3,2,1 give the same index, extract
  lists included, which is the eviction rule under test. Re-reading does not happen in
  spine order and neither does backfill after a jump.
- **The extract is centred on the name**, asserted per extract rather than eyeballed: the
  name's bytes appear in every stored extract.
- **Golden name lists over committed EPUB fixtures**, so a scan change is visible as a
  diff rather than as a vibe. The real novels are not committable; `mkepub.py` fixtures
  with planted names are.
- **Peak-memory assertions on the merge, the capture buffer and the grouping**, in the
  shape `test_inflate.cpp:159-203` already uses for the stack: measure on the desktop and
  fail there rather than aborting on the device.
- **Goldens for both screens at both geometries**, including the two-height mix, the
  chapter-header-when-it-changes rule, and the thin/empty Names variant.
- **`make compare`** on all three boards, at both geometries, plus the three new
  `compare-design.py` entries and the duplicate-id check they are subject to.

## Boards and bookkeeping

Per the design-first rule, before any code: **`Names.dc.html`** and
**`NamesEmpty.dc.html`** lose their `TO CH.` slot and Names' Confirm hint becomes
`MENTIONS`; **`Mentions.dc.html`** is new; **`ReaderMenu.dc.html`** gains the `Names` row
#73 cut, and loses the comment that still calls the family V2. Then `make canvas`, with a
`canvas.json` artboard entry for `Mentions` — a board with no entry is loaded and never
shown, which is how the canvas went six boards stale.

**All new copy goes through `/humanizer` on the board**, which is a standing rule for every
string a reader sees.

**The four open issues describe the 2026-08-24 design and need rewriting.** #156 (index),
#157 (list screen), #158 (alias rows — answered by the fullest-form row, and a candidate
to close), #159 (the menu row). Nothing currently covers: the Mentions screen, the extract
capture and its second walk, the chunked per-chapter store, backfill, the shared
stacked-row primitive, the peek's cursor plumbing, store deletion with the book, or the
session record's new field.
