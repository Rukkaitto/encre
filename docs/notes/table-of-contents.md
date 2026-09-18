# The table of contents

Extracted from `CLAUDE.md`, which keeps a stub under this heading and is where
the cross-references to it point. Same standing as anything in that file.

The seventh reader layer (`toc.h`), and the last one that reads the archive rather than
the text. The spine gives an ORDER and no names, which is why the Reader's footer says
`CH. 03`, Book details' "Current story" is blank and there is no chapter list to jump
from.

**IT IS THE NCX, NOT THE EPUB 3 NAV DOCUMENT.** Measured over four real books before
writing anything: every one carries an EPUB 2 `toc.ncx` and **not one** has a nav
document. Building the modern form first would have parsed something no book on this
card contains. The nav document is a later job and a small one — `Epub::tocPath()`
already answers "which part is the contents" by media type, so it is the only thing
that would need widening.

**AND THE ONE THING IT WOULD OBVIOUSLY BUY, IT DOES NOT BUY: A NAV DOCUMENT NAMES 0 OF
THE 133 SPINE ENTRIES THE NCX SKIPS.** Measured over the whole corpus when a reader
reported the conclusion of `Digital Minimalism` as unreachable — both tables are emitted
by one generator from one source, so a publisher's contents that misses an entry misses
it in **both**. This does not retire the later job (a book with a nav and no NCX is a
real shape), but it removes the reason somebody would reach for it first. Also measured
against the two other places a name could come from: the chapter's own `<h1>`–`<h6>`
names **35 of 133 (26.3%)**, and `<title>` is present for **96.2%** and is not a chapter
name — the reporting book's reads `Continued, Digital Minimalism`. **The name is not
recoverable**, so `fillTocGaps` gives the row a POSITION rather than chasing one.

**A SPINE ENTRY NO ENTRY NAMES IS READABLE BY PAGING AND REACHABLE BY NOTHING ELSE, AND
`fillTocGaps` CLOSES THAT.** The list is what the NCX names; what a reader can be IN is a
spine entry, and where those differ Contents cannot offer the section at all — so the
only way back to it is to remember which chapter it follows and page through. Reported
off `Digital Minimalism`, whose publisher styled the Conclusion's title as a `<p>` where
every real chapter uses an `<h2>`: their generator walks headings, the chapter lost its
navPoint, and the NCX runs `… spine 14, spine 16 …`. The reader sat in the conclusion of
the book with no row marked `NOW`, **the cursor thrown to `Cover`**, and no way back.

- **THE BOUND IS POSITIONAL AND THAT WAS A MEASUREMENT, NOT A TASTE.** A gap is a spine
  entry no row names lying strictly between the first and last the book DID name. That
  is what separates a missing chapter from front and back matter — this book's spine
  carries 15 footnote files and a `next-reads.xhtml` after its last named entry, so an
  unbounded fill adds **16 rows of noise to reach the one chapter that matters**.
  **A SIZE FLOOR WAS MEASURED AND REFUSED**: text length separates cleanly (junk tops out
  at 1,976 characters, real chapters start at 4,510) and is unknowable without decoding
  every gap at book-open; the archive's UNCOMPRESSED SIZE is free and does **not**
  separate — junk reaches 5,210 bytes where a real chapter starts at 6,187 — so any free
  floor either keeps junk or drops a chapter. The stated cost of having none is a couple
  of front-matter rows on a minority of books.
- **ONLY THE LOWER HALF OF THE BOUND IS WRITTEN DOWN.** The upper half is structural: the
  walk emits a gap only in front of an entry that already exists, so it cannot reach past
  the last one. A `next < hi` term read as load-bearing and was implied by the loop's own
  `next < e.spine` — **caught by a mutation that removed it and failed nothing**, which is
  this file's own rule about a branch no test exercises.
- **A SYNTHESISED ROW TAKES THE FOLLOWING ENTRY'S DEPTH, WHICH MAKES IT STRUCTURALLY
  INCAPABLE OF BECOMING A HEADER.** `isHeaderAt` is "the next entry sits deeper than this
  one", and equal depths are not — so the row can never be drawn as a tracked-caps label
  the focus skips, which would be this defect reintroduced by its own fix. **The first
  test of it could not tell the two candidate rules apart**: its fixture gave the gap
  neighbours at equal depths, so taking the PRECEDING entry's depth passed all 1,359,370
  assertions. A part divider followed by its first chapter is the shape that separates
  them. *A mutation tells you about your INPUT before it tells you about your test*, for
  the fourth time in this file.
- **THE LABEL IS `chapterPositionLabel`, ONE FUNCTION AND TWO CALLERS.**
  `ReaderScreen::updateChapterLabel` has composed `CH. %02d` since the header band stopped
  being a spine position; the row now carries the same string, so the list, the band, the
  sleep card and Home say one thing about a chapter none of them can name. Two copies of a
  format string is how those four surfaces drift. The position fallback survives for a
  book with **no** contents at all, which is the only case left that can reach it.
- **WHAT IT COSTS ON REAL BOOKS**, through the built pipeline over `~/.cache/encre-corpus`:
  of 206 books with a usable NCX, **29 gain a row, 134 rows in all**, a median of 2 per
  affected book. The largest is the point rather than the price — **`Dune - Tome 3` gains
  35 rows, 33 of them whole chapters of 7,000–20,000 characters**, a novel navigable today
  only by paging. On the corpus's own `local/` shelf **5 of 16** books gain something.
- **A BOOK WITH NO CONTENTS AT ALL IS LEFT ALONE**: there is no named range to bound the
  fill by, so the only available rule would be "every spine entry", which is a different
  feature with a different argument.
- **IT RUNS IN THE SHELL, NOT INSIDE `loadToc`**, because that function's job is to report
  what the book AUTHORED and this adds rows the book did not write — and it needs the
  spine's length, which `openBookAt` has in hand and the archive read does not. Its second
  row list is guarded with `pushOrRefuse` like `loadToc`'s own, and **a refusal leaves the
  contents exactly as the book wrote them**: a partial fill would make which chapters got
  a row depend on where the heap ran out.

**`Epub` NOTES THE NCX DURING THE OPF WALK**, which already resolves every manifest
href — finding it later would mean re-parsing the OPF, and scanning the archive for
`*.ncx` would be a guess where the manifest is a statement. Two routes, both needed:
the spine's `toc` attribute is the formal one and is OPTIONAL (real files omit it), and
the `application/x-dtbncx+xml` media type is what makes an NCX an NCX. The spine's
answer wins where both exist.

**A MEASUREMENT WAS WRONG AND IT CHANGED THE DESIGN.** This section first said real
files are flat, and that `Contents.dc.html`'s two-level grouping "does not exist in
real files". The check was a regex looking for a `navPoint` inside a `navPoint` that
allowed only tags between them — real files put text there, so it reported every book
as flat. Parsed properly:

| book | entries | by depth |
|---|---|---|
| Le Fléau | 96 | **`{1: 10, 2: 84, 3: 2}`** |
| Darkly Dreaming Dexter | 28 | `{1: 28}` |
| …another edition | 31 | `{1: 31}` |

So one book is three levels deep — ten section headers over eighty-four chapters — and
the board was right. `TocEntry::depth` carries it. **The list stays LINEAR**, not a
tree: a tree needs allocation per node and a traversal to draw, where a screen wants
"the Nth visible row", and a depth is all the board's grouping needs. Every entry is a
real target either way, because a section header in an NCX carries its own
`content src`. **The linear form keeps the parent/child relation recoverable and that
is now load-bearing**: children immediately follow their parent, so "does this entry
group others" is `entries[i + 1].depth > entries[i].depth` — which is what #75's fix
asks, and what a flattened list could not have answered.

**A LOOSE REGEX IS NOT A MEASUREMENT.** This project's habit of measuring before
designing is what caught the nav-document question; the same habit applied carelessly
got the nesting question backwards and wrote the wrong claim into a header. Where the
answer decides a design, parse the thing.

**AND FOUR BOOKS IS NOT A DISTRIBUTION, WHICH IS THE SECOND HALF OF THAT LESSON AND
COST 1,635 ROWS (#75).** The table above is right and it is a SAMPLE, and the design
built on it read a `depth` as a level in a hierarchy: `ContentsScreen` made every
depth-1 entry of a sectioned book a section header. Re-measured by parsing all 225 NCXs
in `~/.cache/encre-corpus` — 19 have no usable NCX, **103 are flat and 103 are
sectioned**, an even split, and **98 of the 103 sectioned ones mix entries that GROUP
others with top-level entries that group nothing**. That second shape is what Standard
Ebooks emits for every book with parts (`Titlepage`, `Imprint`, `Colophon`,
`Uncopyright` sitting at depth 1 beside a real `Part I`), it is **9 of the 9 sectioned
books on the user's own shelf**, and the worst case in the corpus loses **362 rows of
384**. So the childless top-level entry is not a tail case; it is the common case, and
the four-book sample happened to contain none of it. **The fix is in
`screen_contents.h`** — a header is an entry that groups others, one lookahead in a list
already walked in document order — and the reachability rule now lives there rather than
being inferred from a depth here.

**A DEPTH IS A NESTING LEVEL, NOT A ROLE.** `toc.h` reports what the NCX authored;
what a level MEANS on a screen is the screen's decision, and the two were conflated for
two phases. This layer is deliberately unchanged by that fix: `TocEntry::depth` is still
the navPoint nesting depth, and nothing here needs to know which entries a screen will
draw as headers.

**COMMITTING AN ENTRY HAPPENS AT TWO MOMENTS**, and only handling one lost every
parent: a `navPoint` is complete when it closes AND when a CHILD opens, because the
child's start clears the label the parent had already read. A test caught it. State is
cleared after each commit, so a parent's close adds nothing — verified by deleting the
duplicate rule and confirming the nested case still passes, since it used to be correct
only by accident of that rule.

**AN IDENTICAL ROW TWICE IS NOISE; A DIFFERENT NAME FOR ONE TARGET IS CONTENT.** Real
books produce both, and only the PREVIOUS entry is compared — an NCX is authored in
reading order (0 out-of-order entries across all four measured), so a repeat is adjacent
and a full scan would be quadratic for a case that cannot happen far apart. **That
premise is also what makes #75's lookahead sound**: an entry's children are the entries
immediately after it, so a document-order list carries the hierarchy without a tree.

**THE LIMITATION WORTH KNOWING:** an NCX target is a file plus an optional fragment
(`ch3.xhtml#part2`) and the reader positions by spine entry only, so several entries
pointing into one file all land at that file's start. They are kept rather than
merged — their labels are real content — but selecting one is approximate. That is why
Le Fléau has 96 entries for 92 spine entries.

**AND THAT LIMITATION IS THE MAJORITY CASE, WHICH IS HOW IT MADE `NOW` A FALSE CLAIM.**
Reported off an X3 on `Discourse on the Method`: **two rows** read `NOW` —
`DISCOURSE ON THE METHOD OF RI…` and `Contents`, whose targets are
`…59-h-0.htm.xhtml#pgepubid00000` and `#pgepubid00001`, both resolving to spine entry
1. `renderContents`' source asked `!row.isHeader && e.spine == spine_` **per row**, so
every entry naming the open spine entry got the marker. `NOW` is a claim about where
the reader is, so more than one of them is the false-claim shape this file refuses for
an unread gauge (`-1`, never `0%`) and for a badge promising a wake charging cannot
deliver.

- **MEASURED OVER `~/.cache/encre-corpus`, and the limitation above under-sells its own
  incidence: 109 of the 206 books with a usable NCX (52.9%) have at least one spine
  entry named twice or more** — **605** such groups, **4,526** rows that would have read
  `NOW` at once. The worst is `standardebooks/f822606a92670aa1.epub`, whose spine entry
  2 is named by **378** navPoints, **373** of them non-headers. **One of the affected
  books is on the user's own shelf** (`local/6eff4fa621681282.epub`, 44 on one spine
  entry), so two rows is the mild version.
- **THE ROW CHOSEN IS THE FIRST OF THE GROUP, AND `tocIndexForSpine` ANSWERED THE LAST
  FOR TWO PHASES.** Its argument — "the later ones are further into the file, so the
  last is the closest thing to where you are" — is true in its premise and needs the
  reader to be at the **END** of the file. The fragment is **stripped** before the
  match, so every member of a group resolves to that file's **start** and nothing on
  this path knows any offset within it: the first entry is the only one that can be
  *proved* not to be **ahead** of the reader, and `reading_position.h` grades the same
  trade the same way, degrading backwards. It was also wrong at the one moment it is
  asked — `updateChapterLabel` runs when a chapter **opens**, which is its first page on
  a jump and on a forward crossing. So the Reader's header band moved with the marker:
  **one rule, because two screens naming the reader's chapter differently is two
  spellings of one fact.** What it costs is stated rather than hidden — a reader deep
  inside a 378-fragment file is named by that file's first fragment, which is stale
  rather than false, and closing that needs a fragment-to-block map `document.h` cannot
  supply.
- **A HEADER MAY NOT TAKE IT, and that gate is the screen's rather than `toc.h`'s** — a
  depth is a nesting level and not a role (#75), so `ContentsScreen::rowForSpine` is
  `tocIndexForSpine`'s rule plus one lookahead, **pinned to it by an equivalence over
  header-free lists**, which is `test_focus.cpp`'s device for `Focus`'s gated walk.
  **A spine entry named ONLY by headers answers −1 and marks nothing** — a `Part I` with
  a file of its own, **108 spine entries across 62 corpus books** — which is the
  pre-existing behaviour and the honest one.
- **THE TRAP IS THAT `syncVm` WALKS THE VISIBLE SLICE (`s.first + i`), NOT THE LIST.**
  A "first match" computed inside that loop is the first match **on screen**: the marker
  would hop between members of the group as the list scrolled and would appear on a row
  that is not the reader's once the real one scrolled out of the window — strictly worse
  than the defect, and **invisible to any single-screenful test**. It is decided **once,
  in the constructor, over `entries_`**, and compared as an absolute index; `entries_`
  and `spine_` have no setters, so there is nothing to invalidate. Proved by mutation: a
  slice-local rule fails only the scrolling case, 11 assertions, while the
  reported-book case stays green.
- **NO FIXTURE COULD REACH IT, INCLUDING THE BOARD'S OWN.** `sectioned()`,
  `mixedDepths()` and `demoContents()` **do** share spine indices, and in every one of
  those pairs one member is a HEADER, which `!row.isHeader` already suppressed;
  `flat()` gives every row a spine of its own. So the two Contents goldens, the
  comparison sheet and a test literally named *the row being read is the only one marked
  `NOW`* all agreed with a rule that marks every match. Same shape as "a stream of one
  block kind is not a chapter". **What those goldens DO defend is the header gate** —
  dropping it moves the marker onto `BOOK I · MISS BROOKE` and reddens both.
- **`design/Contents.dc.html` NEEDED NO CHANGE**: it draws exactly one `NOW` and its own
  copy says the marker is on "the row being read", singular. So the board was already
  right and the firmware moved toward it. Nothing moved on the sheet either —
  **2.30% / 2.11%, 8,814 differing pixels at both geometries**, which reproduces #81's
  recorded figure to the digit.

**It re-opens the archive**, deliberately: `OpenedBook` holds twelve bytes a spine entry
and no hrefs, and matching an NCX target to a spine index needs the real paths on both
sides. One central-directory parse and one OPF inflate (~32 KB transient) when Contents
opens, not when a book does. Measured 0.2–0.6 ms on the desktop for 28–96 entries, and
labels total **1,161 bytes for 96 entries** (mean 12.1), so the resident cost is small.
