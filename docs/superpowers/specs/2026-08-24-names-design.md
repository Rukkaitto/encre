# NAMES — answering "who's this character again?" on a device with 45 KB free

**Status:** design approved 2026-08-24. Not implemented. Depends on 3D (peek and
return) and on `ReaderMenu`. Roadmap entry: 3E.

## The question, and why it took a probe first

A reader 300 pages into a novel meets a name and cannot place it. Kindle answers
with X-Ray, which is precomputed on Amazon's servers and reached through a
touchscreen; Encre has neither — Wi-Fi is cut from V1, and there are six buttons
and no pointer.

So the riskiest question was not the UI. It was whether a heuristic a 160 MHz
part could run yields a list worth reading at all, and it was cheap to answer
before designing anything: `tools/name_probe.cpp`, run over three real novels.

**It does.** `Le Fléau`'s top 26 is 26 of 26 real named entities — Stu, Larry,
Harold, Nick, Frannie, Ralph, Glen, Nadine, Flagg, La Poubelle, Kojak the dog,
and `Dieu`, which is a proper noun and so correct on a screen that says NAMES.
**No false positives.** `Neuromancien` returns Case, Molly, Armitage, Maelcum,
Riviera, Wintermute, Chiba, Tessier-Ashpool. `Darkly Dreaming Dexter` (English)
returns Deborah, Deb, Dexter, LaGuerta, Harry, Rita, Dark Passenger, Captain
Matthews, Sergeant Doakes. Nothing in the rules is vocabulary, which is why it is
not French-specific.

The probe's own header carries the eleven rules and the failure each one fixed.
Two are worth restating because they are the ones a reimplementation would get
wrong: **sentence-initial suppression is load-bearing** (ranking on total
mentions floods the list with `Il`/`Je`/`Et` — a French pronoun starts thousands
of sentences), and **the introducing sentence is the earliest of the best kind,
not the best-scoring** (a running best that rewards length picks the most verbose
sentence in the novel).

## The constraint that forces the design

**The probe holds every candidate for the whole book because a desktop can.** The
device cannot, and it is not close:

| | runs | bytes of keys + counters |
|---|---|---|
| `Le Fléau`, whole book | 4,093 | **85,001** |
| `Le Fléau`, worst single chapter | 412 | **7,797** |
| `Neuromancien`, whole book | 1,116 | 22,142 |
| `Neuromancien`, worst chapter | 228 | 4,219 |
| **device heap floor, measured** | | **45,840** |

Those figures store **no sentence text at all**. A whole-book table is 1.9× the
entire margin before the feature has anything to show. One chapter fits with room.

**A second constraint, confirmed in `filesystem.h` rather than assumed: there is no
streaming write.** `writeAll` takes a whole buffer and truncates; `readAll` caps at
64 KB. So the persisted index must be small enough to build in RAM as one string —
which independently rules out storing an introducing sentence per name (~220 bytes
an entry against ~20 for a cursor).

Both point the same way, and the design is essentially determined by them:

- **the scan is per chapter**, and the accumulator is the card
- **no sentence text is stored** — a cursor is, and the peek renders the sentence
  from the book itself

## Three pieces, and no entry screen

| | Piece | Lives in | Knows nothing about |
|---|---|---|---|
| **Scan** | a `Block` stream → this chapter's candidate runs | `core/` | files, cards, screens |
| **Index** | merge a chapter's runs into the card's sidecar | `core/` (format) + shell (I/O) | pixels |
| **Screen** | `NamesScreen` — alphabetical, rail'd, a row opens a peek | `core/` + theme | storage |

**Selecting a name opens the peek at the introducing sentence.** There is no name
detail screen, and that is the single largest simplification in this design: it
deletes a screen, its board, and the stored-sentence problem in one move, and it
makes 3D the payoff rather than only a prerequisite. The reader gets the sentence
*in its context*, with `CLOSE` and `GO HERE` already on the bar.

The cost is honest and small: the sentence is not singled out. The peek opens at the
block containing it, so it is at or near the top of eight lines.

**The stored cursor is `(spine, block)` — deliberately one field shorter than a
reading position.** `fitOf` already grades `line` as the field that survives neither
a re-layout nor a re-bind, and a name's landing only needs the paragraph. Dropping it
means the index survives a type-size change where a reading position would degrade.

## The scan

**It rides `completeIndex`**, the pagination pass that already decodes every block of
the chapter — eagerly for chapters under `kEagerCountBytes` (8 KB), in the quiet
window for larger ones. So a chapter's names become known exactly when its page count
does, and the scan costs no I/O of its own and no extra decode.

Its whole memory is one chapter's candidate table: **7,797 bytes worst case over a
real book**.

**Grouping runs at DISPLAY time, not scan time**, and that is deliberate. The probe's
grouping — containment, prefix edges, the plural guard, the 3:1 dominance tie-break,
union-find — is a batch operation over the whole candidate set, and it is validated as
such. Doing it incrementally per chapter would freeze decisions that later chapters
should be able to change: `Fran` is unambiguous until `Frank` appears, which may be
thirty chapters later. So the card holds **raw runs** and the screen groups them when
it opens.

## What earns a slot on the card

**There are two thresholds and they answer different questions.** This one is about
*storage* — what the card keeps, so that a name's count can go on growing. The
display threshold further down is about *navigability* — how many rows a reader
scrolls. They are deliberately different numbers: the index has to admit a name
before anyone knows whether it will end up frequent.

A run is admitted only when **one chapter alone saw it at least twice
mid-sentence**. Measured, that is the difference between a feature and an
impossibility:

| | admitted runs | bytes | share of the naive table | most admitted by one chapter |
|---|---|---|---|---|
| `Le Fléau` | 738 | **14,812** | 17.4% | 63 |
| `Neuromancien` | 167 | 3,300 | 14.9% | 33 |

Admission at the door rather than eviction after the fact is the point. Eviction by
count would thrash: a name at two mentions is dropped, reappears with two more, and is
stored as two again — losing history exactly for the mid-frequency names the feature
exists to serve. A run that clears the bar keeps accumulating for the rest of the book.

**What this gives up:** a name that appears once per chapter across many chapters is
never admitted. That is a real gap, it is accepted, and it is the price of a bounded
index. The alternative is 85 KB.

## The card format

**`/.reader/names/<hash>.idx`**, reusing the reading position's FNV-1a 8-hex naming,
and for the same reason: a book path holds `/` by construction and real cards carry
accented 90-character titles.

**Not JSON.** `core/include/reader/json.h` is one flat object with no nesting and no
arrays, and this is a list of 738 things. Line-oriented instead, **sorted by run
name**, which makes the per-chapter merge a linear two-way merge rather than a sort.
Per line: name, non-initial mentions, chapter-opening mentions (the furniture cut),
spine, block — five fields, ~20 bytes.

**That sort order is for the merge, not for the screen.** The file is ordered by RUN
name; the list is ordered by GROUP display name, which only exists after grouping, and
grouping happens when the screen opens. So the screen sorts its groups itself and must
not assume the file's order is the reading order.

A header carries four things:

- **a version**, so a format change discards rather than misreads
- **the book path and `bookBytes`**, the identity check the reading position already
  uses; a mismatch starts over, because an index for a different book is worse than
  none
- **a bitmap of scanned spine entries** — 23 hex characters for a 92-chapter book.
  Without it, re-reading a chapter **scans it twice and double-counts**, inflating
  every mention and admitting runs that had been correctly rejected. Re-reading is
  normal, so this is not an edge case, and it is the field whose absence would be the
  worst bug in the feature.
- **the admission threshold in force**, so changing it invalidates the index rather
  than mixing two populations into one set of counts.

### Peak memory is the top risk, and the answer is a window

The merge wants the index (14.8 KB), the chapter's table (7.8 KB) and the output
(~15 KB) live together — **~37 KB against a 45,840-byte floor**. Too close to ship.

**So the merge happens in the chapter-crossing window**, which is the one moment the
reader's largest allocation is already free: `openChapterAt` releases the old chapter
before opening the new one, so the **36,956-byte inflate scratch is unallocated
exactly then**. It is also the window the position save already uses, so this adds no
new write edge.

## The screen

**Rows are `ListRow`** (`viewmodel.h:251`), the shared row view model Settings and the
reader menu already use — `SettingsRow` is a typedef of it. It carries `isHeader`,
`discloses`, `trackingEm1000` and `live`, so the Names list should extend it rather
than introduce a second row type. Note that its taller-row axis is `isHeader`, which
is *not* the axis this screen needs: here the two heights are "has a fullest form" and
"does not", so that is a field to add rather than a flag to reuse.

`NamesScreen` derives from `FocusScreen`, which gives it `focus()`/`setFocus()` as a
`final` pair — so the focus-restore contract is structural rather than remembered,
which is the rule this project shipped one-way on three screens before making it
impossible — plus a `ScrollWindow`, wrapping, and auto-repeat.

**Alphabetical.** The reader always arrives knowing the string, because they just read
it on the page, and alphabetical is the only order where knowing the name tells you
where to look. Most-mentioned-first is ordered by the inverse of need: **the name you
cannot place is rare**, so it would sit near the bottom of a list hundreds long, and
its position would move as you read on.

**Navigation is the Library's, unchanged** — `ScrollWindow`, auto-repeat, and
`drawScrollRail` in the 14px gutter, with the gutter present only when the rail is
(already `renderLibrary`'s rule). No new mechanism. The rail is more useful here than
on the Library: with alphabetical ordering a thumb 60% down the track is roughly
"around M", so it can be aimed.

**Rows** are the Library's title-over-author shape minus the cover: the name in
Body500 over its fullest form in tracked uppercase Meta — `Stu` over `STUART REDMAN`.

**Two row heights, measured off the board: 90px with a fullest form, 60px without.**
A place or an already-full name has nothing for a second line, and that is 30–40% of
rows on a real book, so a blank second line would read as broken on a third of the
screen. The theme therefore reports the **box model** — both heights — and the screen
counts how many fit, exactly as `settingsMetrics` already does, because the item table
belongs to the screen. The list box is 671px at 480×800 and 663px at 528×792, so a
typical mix is **eight rows**.

**The slack below the last row is the check, and it has an invariant**: a
`ScrollWindow` draws whole rows only, so with two row heights some empty space at the
bottom is inevitable and varies with which rows are on screen. **Slack must be less
than the shortest row height**, because 60px or more means another row would have
fitted. The board's first draft had 132px of it and understated the screen by a whole
row; it is 42px at 480×800 and 34px at 528×792 now.
The fullest form is often most of the answer before a peek is even opened. Aliases
still *merge*; they are not all displayed. The probe's raw label,
`Stu (Stuart/Stu Redman/Redman/Stuart Redman)`, does not fit a 480px row and reads as
a database dump rather than a cast list.

**Header band: `NAMES` left, `TO CH. 07` right.** The band's right slot is a fact
about the list, and the honest fact here is its **coverage** — the index knows only
the chapters that have been read. A count would restate the label; this makes the
spoiler boundary visible instead of leaving a reader to wonder why someone is missing.

**Display threshold: 5 mid-sentence mentions.** Chosen on the numbers, not taste:

| threshold | groups, `Le Fléau` | screens at ~8 rows |
|---|---|---|
| ≥2 | 713 | 89 |
| ≥3 | 479 | 60 |
| **≥5** | **300** | **38** |
| ≥8 | 211 | 26 |
| ≥12 | 143 | 18 |

`Neuromancien` gives 83 at ≥5 and Dexter 42, so `Le Fléau` — a 1,400-page novel with
an enormous cast — is the worst case on the shelf. The tension is real and worth
stating rather than hiding: a higher threshold cuts precisely the rare names the
feature exists for, and a lower one is unnavigable. 5 keeps the walk-ons and holds the
worst real book to 300 rows.

**Thin and empty are a variant, not a screen** — `HomeEmpty`'s pattern: same
`ScreenId`, same view model, copy in place of rows. *"Names appear as you read."* A
book opened at chapter 1 legitimately has almost nothing to show, and that is a state
to state rather than an empty list to explain.

## Boards

Per the design-first rule, before any code: **`Names.dc.html`**,
**`NamesEmpty.dc.html`**, and a row added to **`ReaderMenu.dc.html`** — whose sheet is
boarded but unbuilt, so 3E depends on it being built.

## What this does not do

**It does not tell a person from a place.** The screen says NAMES, and Boulder, Las
Vegas, New York and Miami all rank, correctly. Separating them needs a per-language
word list, a table `core/` has twice refused to carry.

**It does not store or display a mention count.** It is computed to rank and threshold
and then thrown away. A number beside a name reads as a database statistic and invites
comparing figures that mean nothing to a reader.

**It does not scan ahead.** The index covers chapters read, which is what makes it
spoiler-proof by construction rather than by a rule someone has to remember.

**It does not survive a book changing.** `bookBytes` disagreeing discards the index
whole, exactly as it discards a reading position.

**It does not fix the pronoun residue.** `Il` survives at rank 57 in `Le Fléau` with
27 mentions, below anything a reader scrolls to. The two causes that were fixed are in
the probe's rule 11; chasing the rest is not worth a rule.

## Risks

**The scan's cost on device is unmeasured.** The whole book is ~0.5 s on the desktop,
so ~5 ms a chapter — but this project's ~37× ratio is a **render** ratio, and its
~135× figure is for work that touches the card or the inflater. A name scan is neither:
it is byte and allocation work over text already in RAM. The suspect is the candidate
table's string-keyed map, ~412 inserts a chapter. **Measure before tuning, and read the
`mark()` trail rather than reasoning from code shape** — that instruction is in
`CLAUDE.md` because guessing here has been wrong twice.

**Peak memory at the merge**, addressed by the chapter-crossing window above, is the
thing most likely to need a second pass on hardware.

**`ReaderMenu` and `Contents` have both LANDED, so nothing here is blocked any more.**
`screen_reader_menu.{h,cpp}` and `screen_contents.{h,cpp}` exist, which means the
`Names` row this design adds to the sheet has a real item table to go into rather than
only a board. Neither is this spec's work, but both were listed as blockers and are
not.

## Verification

- **The grouping rules keep their own unit tests**, so the eleven the probe earned
  cannot drift: sentence-initial suppression, the abbreviation guard, maximal runs,
  earliest-of-best-tier, the lowercase-after-comma test, apostrophe trimming, the
  containment link with no ratio guard, the fuller-name tie-break, prefix edges with
  the plural and dominance guards, the furniture cut, and NBSP-as-whitespace.
- **Scanning a chapter twice yields the same index as scanning it once.** That is the
  scanned-spine bitmap's entire job, and the property that fails loudly if it is
  dropped.
- **A merge is order-independent**: scanning chapters 1,2,3 and 3,2,1 gives the same
  index. Re-reading does not happen in spine order.
- **Golden name lists over committed EPUB fixtures**, so a scan change is visible as a
  diff rather than as a vibe. The three real novels are not committable; `mkepub.py`
  fixtures with planted names are.
- **A peak-memory assertion on the merge**, in the shape `test_inflate.cpp` already
  uses for the stack: measure it on the desktop and fail there rather than aborting on
  the device.
- **`make compare`** on all three boards, at both geometries.
