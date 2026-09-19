# The reader's menu and the chapter list

Extracted from `CLAUDE.md`, which keeps a stub under this heading and is where
the cross-references to it point. Same standing as anything in that file.

`ReaderMenu.dc.html` opens on the page's Activate, and its Contents row opens
`Contents.dc.html`. Between them they are the "go to chapter" the roadmap lists as
`contents`.

**THE MENU IS ASSEMBLY, NOT NEW GEOMETRY.** `components.h` already listed ReaderMenu
among the eight boards sharing the overlay panel box, `kActionsPanelW` is the same 340,
and `drawPanelRow` was already "72 tall, inset on a panel's own 20px padding, discloses
with a chevron". The only thing the menu added to the primitives is a row that states a
VALUE — its `Bookmarks` count — which is the other half of Home's "a row states a
quantity or discloses a screen, never both". **THAT ROW IS CUT AND THE PARAMETER IS
NOT** — see the trackingEm1000 paragraph below, which is where this went next. With
`Names` cut too (#73) the menu now adds **nothing at all** to the shared primitives.

**IT DECLARES `Mono` WHERE THE READER DECLARES `Grayscale`.** Fidelity comes from the
top screen, so the menu paints in one waveform instead of three and its focus moves are
eligible for the overlay-only partial repaint (grayscale never is). The page under the
veil is hard-thresholded for those frames — the trade, and acceptable because the menu
is chrome and the page is the one thing here that wanted four levels. Its
`paintFootprint` is a constant, unlike the actions panel's: every row is one height, so
the panel cannot change height when the focus moves and every move takes the fast path.
(That sentence counted the rows twice and the count was wrong twice; the property is
"one height", and the number belongs in the test.) **AND THE CLAIM IS FALSE — #68 IS THE
OPEN CARD.** The rows really are one height, but `renderReaderMenu` sizes the panel
through `panelRowHeight(rowRuleFor(i, rows, focused))`, and `rowRuleFor` suppresses the
rule for the focused row **and** for the last row — so focusing the LAST row is the one
state where two suppressions coincide and the centred panel moves a pixel. Measured on
the X3: panel top 213 on Contents and Typography, **212** on About this book. That is
the actions panel's own defect, which `ItemActions::paintFootprint` counts borderless
rows for and this does not. **Cutting `Names` did not touch it**: that row was never
focusable and never last, so it always drew its rule — the cut takes 73px off the panel
in every state and leaves the focusable set, and therefore every per-state delta,
exactly as it was.

**`discloses` CANNOT BE DERIVED FROM AN EMPTY VALUE**, and deriving it drew a chevron on
`Close book` promising a screen that does not exist — that row had neither a value nor a
mark, because it acted in place. So `ListRow` carries the flag explicitly, as
`ItemActionEntry` already did. `Bookmarks` WAS the surviving instance of the same rule
from the other side — a value where its siblings have marks — and with that row cut
(#55) **this sheet states no quantity at all and every row on it discloses**. Both fixes
took the menu from 3.24% to **3.02%** against its board.

**`ListRow::trackingEm1000` NOW HAS NO PRODUCER.** `Close book` was `0.06em` where its
siblings were untracked — 1.5px a gap at Value500, ~15px across that label — and it was
the only letter-spaced row on any panel in this firmware. With the row cut (2026-08-24)
the field, `drawPanelRow`'s `labelTrackingEm1000` and the `trackingEm` call it guards are
**untested capability rather than working behaviour**. Kept because it is a generic
component parameter a board can ask for again; a test asserts every row is `0` so this
stays a stated fact rather than an assumption.

**AND `drawPanelRow`'s VALUE PATH JOINED IT, WITH THE OPPOSITE ANSWER.** Cutting
`Bookmarks` (#55) left the value argument in exactly the shape tracking is already in —
a parameter whose last producer walked out — and the two were settled differently on
purpose. **Tracking is a board's typographic request and its absence is invisible; a
value is a MARK, and a wrong one is a number in the wrong place on the glass.** So the
pixels are pinned at the PRIMITIVE instead of at a screen — `test_components.cpp`, "a
panel row states a quantity or discloses a screen, never both" — which needs no caller
to reach them: the value wins over the chevron, it is right-aligned by MEASURING itself
rather than by a fixed offset, and it inverts with the focus. **Proved by mutation, and
the first version of the alignment case did not bite**: it asserted a wider value's LEFT
edge moved, which passes against a hardcoded offset because `1` and `2` have different
side bearings — it was reading the FACE. Two values ending at the same column is the
assertion that is actually about the placement.

**The parameter stays for the same reason tracking's does** — `Bookmarks.dc.html` is a
board that asks for it again in V1.1 — and `ListRow::value` is untouched either way,
since Settings, Contents and Typography all still state values through their own row
primitives. It is `drawPanelRow`'s argument alone that lost its caller.

**A MERGE CHANGED THIS BOARD UNDER THE SCREEN, and `make compare` said "firmware ok"
the whole time.** Another branch (`claude/book-character-identification`) added a `Names`
row — its own boarded character index — so the board had SEVEN rows against this screen's
six. The comparison sheet reported it as fine because "ok" means the simulator produced a
frame, not that the frame matches: measured per pixel it was **13.02%** against 3.02%
before the merge. **The percentage is the check; the word is not.**

Two board inconsistencies surfaced with it, both about the page UNDER the veil, which IS
the reader's page — so `ReaderMenu.dc.html` and `Reader.dc.html` have to agree about it.
The menu board still drew a **drop cap** that `Reader.dc.html` drops from V1 with a long
mechanism note, and still said `CH. 01` where the Reader's slot had become a chapter
name. Both fixed on the board; the menu is back to 3.06% / 3.60%.

**`About this book` WAS INERT FOR A REASON THAT WAS FIXABLE.** Book details was built from
the LIBRARY's focused row, which is fine from the Library and wrong from a Reader: a
reader who arrived through Home's CONTINUE has no Library on the stack, so the factory
refused the push. Making the row focusable anyway would have been **a button that works
only sometimes** — worse than one that never does, because nobody can learn the rule.

So the screen takes **facts** (`BookDetailsScreen::Facts`) instead of a Library
reference. The Library answers them from a row and the Reader answers them from the book
it has open, and neither has to know how the other is shaped. Two details worth keeping:

- **The progress and chapter come from the READER, not the sidecar**, when the screen is
  opened from inside a book: the reader has moved since the last save, and a details
  screen opened mid-book should say where they *are*.
- **The Library path CLEARS the facts.** Without that, opening details from the Library
  after opening them from a book would show the book — a stale answer that looks like the
  right screen.

**EVERY ROW ON THE MENU RESPONDS NOW, AND SETTINGS' RULE HAS NO INSTANCE LEFT HERE.**
The board was edited to match the rule before the screen was written — it had focused
Typography, which was not built then, so implementing it faithfully would have drawn a
selection on a dead row — and the last drawn-and-skipped row was `Names`, which is cut
(#73). `Contents`, `Typography` and `About this book` are all that is left and all three
act. `ReaderMenuScreen::focusable()` and `ListRow::focusable` stay, because the rule is
the screen's and the next unbuilt **V1** row gets it by setting one word.

**AND THAT RULE'S LIMIT HAS NOW BEEN REACHED TWICE — `Bookmarks` (#55/#3) AND `Names`
(#73) — AND THIS PARAGRAPH GOT THE SECOND ONE WRONG WHILE STATING THE TEST FOR IT.**
Skipping the focus stops an unbuilt row misleading a reader who PRESSES it; it does
nothing about the row itself promising a feature the release does not have. The
distinction that decides it is **which release the row is waiting on** — and this file
wrote that sentence down and then applied it to `Names` from memory rather than from the
board: it said "`Names` waits on its own screen inside V1, so it is drawn and skipped".
**The Names family is V2**, three `Boarded` cards (the per-chapter index, the list
screen, and the alias-row overflow), so it was `Bookmarks`' case from the moment those
cards were filed and the row should have gone with it. **A rule and its worked example
drifted apart inside one paragraph**, which is the same shape as the guards that named a
member instead of `ScreenId::Count`: the rule was right, the instance was stale, and
nothing but the board could tell them apart. `Names.dc.html` and `NamesEmpty.dc.html`
stay; the row returns with the screen.

**AND IT IS RETURNING: THE NAMES FAMILY IS v0.3.0 AS OF 2026-09-19**, three
issues now rather than three drafts (#156 the index, #157 the list screen, #158
the alias-row overflow), with #159 for the row itself. **The rule did not move
and the answer did** — a row waits on the release its screen lands in, and that
release is this one, so `Names` goes back on `ReaderMenu.dc.html` and back in
the enum. Until #157 lands it is `Typography`'s old case rather than
`Bookmarks`': drawn and focus-skipped, because a row whose screen arrives in
the same release cannot mislead a reader about what the release has. The board
comment above the three rows still sends the family to V2 and `Bookmarks` to
V1.1, and #159 owns both of those lines.

**FOUR ROWS HAVE BEEN CUT ENTIRELY, AND NOT ONE OF THEM FOR ROOM** — two of them on
2026-08-24, then `Bookmarks` and `Names` above. `Go to page…` because
**nobody navigates an EPUB by page number**: a reflowable book has no stable page to go
to and the number a picker offers moves with the type size, so the honest jump is the
chapter name `Contents` already gives. (Its board and its roadmap entry went too; the
`Peek` spec listed it as one of three callers and now has two.) `Close book` because
**Back from the page already closes the book** — it was a second door to a room with
one, and it cost a fourth save edge to stay correct. Removing it deleted that edge, the
`popTo(Library)` it was the only user of on this screen, and the only producer of row
tracking in the firmware. The enum shrank with it: **a row index is not a stable
numbering** here, because the one thing that persists one is `FocusScreen`'s restore,
and that refuses an index it cannot land on — exactly what a shrunk table produces. It
has now shrunk three times on that argument with nothing to migrate any of them.

The menu measured **3.10% / 3.60%** against the board after that cut, against 3.06% /
3.60% before: the panel shrank consistently on both sides, so the residual was the same
rasteriser difference rather than new drift. **That the number barely moved is the
check** — a structural mismatch would have shown as a jump.

**AFTER THE `Bookmarks` CUT IT IS 2.46% / 3.02%**, and that one moved the number rather
than holding it — which is the expected direction and worth saying why: the row that
went was the only one carrying a **right-aligned bold numeral**, and a numeral is where
Chrome's subpixel advances and the firmware's whole-pixel ones disagree most per pixel of
ink. **The sheet still prints `ok` and not a percentage (#41)**, so both figures are a
threshold-at-128 count over the bare panel PNGs `--export` writes. Measured in the same
tree, the untouched `reader` reads 5.24% / 6.29% against the 5.34% / 6.38% recorded
elsewhere here — the same ~0.1pp offset this file already notes for the peek, which is
what makes the before and after comparable rather than two instruments.

**AFTER THE `Names` CUT IT IS 3.00% / 3.56%, AND THAT NUMBER WENT THE WRONG WAY FOR A
REASON THAT IS NOT DRIFT.** The panel lost a 72px row and its 1px rule, so it got
*smaller* and *closer* to nothing — and the strict figure ROSE by 0.54pp on both
geometries. **This is the first time on this project that a threshold-at-128 count has
been read as a regression and been an artefact of the count itself**, and the mechanism
is worth having written down because it will happen to the next row anybody cuts from a
centred panel:

- **The board's panel is a HALF PIXEL out of phase now.** Chrome derives the panel's
  height as `2 * border + caption + rows` and its caption block's content height is
  FRACTIONAL, so the sum is fractional. Removing one 73px row flipped the *parity* of a
  centred panel's top edge: the board's panel used to land on an integer y and now lands
  on a half-integer. Chrome then rasterises every 1px rule inside it across **two rows
  of grey 127**, and 127 is under the threshold, so the count scores **both** as ink
  where the firmware inks exactly one. ~340 spurious mismatches per full-width edge,
  five edges, and the +2,062 (X4) / +2,276 (X3) is accounted for.
- **NOTHING MOVED, and that was checked per band rather than argued.** Every full-width
  edge of the design's panel BRACKETS the firmware's: top border design 252(127) /
  253(0) / 254(127) against firmware 253 / 254; the rule design 472(127) / 473(127)
  against firmware 472; bottom border design 545(128) / 546(0) / 547(127) against
  firmware 545 / 546; and the focused row's black block spans the same 74px, offset by
  half of one. Identical story at 528×792. A rule that had really moved would sit
  *beside* the firmware's, not straddle it.
- **A ±1-ROW-TOLERANT COUNT IS WHAT THE FIGURE WOULD BE WITHOUT THE PHASE**, and it
  moves the way a smaller panel should: **1.66% → 1.76% (X4) and 2.11% → 2.20% (X3)**,
  +0.10pp, consistent across both geometries. That is the same order as every other row
  cut here. It is quoted as a second reading and **not** as a replacement — the
  threshold-at-128 number is this project's instrument and swapping instruments to make
  a figure look better is how a real regression gets hidden.
- **THE FIX IS NOT TO PIN THE PANEL'S HEIGHT ON THE BOARD.** That is what
  `Peek.dc.html` did, and it was right *there* because the peek's box is a fixed
  constant by design; this panel's height is the sum of its rows, which is exactly the
  box model CLAUDE.md's first invariant says to derive from and never pin. The
  fractional part lives in the shared overlay caption that **eight boards** draw, so it
  is a `components.h`-level question and not this screen's — and it is worth a card
  rather than a paragraph.

**THE ROW'S RIGHT SLOT HAS HELD TWO WRONG THINGS.** It was `P. 21`, a page number for a
place in the book, which needs every chapter paginated (~49 s). That became `CH. 01`, the
spine position — free, true, and WORSE on a real book: chapter names carry their own
numbering, so a row read `Chapitre 1.        CH. 09`, two numbering systems side by side
with neither explaining the other. It is `NOW` on the row being read and empty elsewhere:
the NAME is the content of a table of contents, and the full width belongs to it. **On
exactly ONE row** — it marked every entry naming the open spine entry, which is a group
in the majority of real books; see the `NOW` bullets under **The table of contents**.

**HOME'S COUNTER WAS THE SURVIVING INSTANCE OF THE SAME DEFECT AND IT IS CLOSED.** Its
CONTINUE block drew `CH. 14 OF 36` — the identical spine position, with a spine COUNT
beside it, which is the worse form because two numbers invite an arithmetic one does
not. It draws the chapter's name now, from the same `toc.h` label this row's `NOW`
follows; see **Home's CONTINUE block** for the corpus figures. **What made it survive
is worth keeping: this paragraph called the position "free, true, and WORSE on a real
book" and Home's own paragraph called the same quantity "free and true"** — the same
fact, judged in two places, and the sentence that got it right was not the one next to
the code.

**THE LABEL ELIDES, AND `drawDetailRow` DID NOT.** It drew the label at full length from
the left margin, so a long one ran under the value and off the panel. Book details'
labels are field names and never overflowed, which is why it only surfaced when real
chapter names went through the same primitive. Fixed IN the primitive — a row that
overflows its own box is wrong on every screen that draws one.

The test for it first reported the FOCUSED row as an overflow: that row is full-bleed
inverted, so its fill legitimately inks both margins. `x=0` is the discriminator — a
full-bleed fill inks it and an overrunning label never reaches it, since every label
starts at `kMargin`.

**HELD UP OR DOWN SCROLLS**, `declareRepeat` on the two front movers as the Library does.
The SIDE buttons are movers now too, and a held one on a list still resolves as `Long`
and is dropped — worth deciding deliberately for both screens rather than changing one.

**CONTENTS IS SETTINGS' SHAPE**: a header band, a list interleaving section headers with
64px rows, a rail when it overflows, a hint bar. `drawDetailRow`'s own comment was
written anticipating it — "`focused` inverts it, which BookDetails never does and
Contents does on the chapter you are in". The section header's **BOX** is byte identical
on both boards (`--t-meta`, 0.2em/500, `padding: 18px 24px 6px 24px`), so it is
`drawSectionHeader` now rather than a second copy — and it returns the height it ACTUALLY
drew, because a header without its rule is 2px shorter and a caller advancing by the
nominal height puts every row 2px low. Settings shipped that exact bug once.

**ITS RULE IS NOT SHARED, AND THIS PARAGRAPH SAID IT WAS — "a 2px `border-top` except
the first", which is SETTINGS' rule and not this board's (#81).** `Contents.dc.html`
gives **neither** header a `border-top` and neither section-final row a `border-bottom`;
`Settings.dc.html` gives every non-first header one. So `renderContents` drew a row's 1px
rule straight into a 2px header rule — a **3px** full-width line where the board draws
none, **1,440 of 13,274 differing pixels at X4 and 1,584 of 13,514 at X3**, ~11% of the
screen's whole mismatch and the largest contiguous band on it. The same false claim stood
in `drawSectionHeader`'s own doc comment, which is what licensed it: **a comment about a
neighbouring file is not evidence about it**, the shape this file already records for the
`book.cpp` comment describing `Epub::open`.

- **THE BOARD WAS RIGHT AND THE TWO ABSENCES ARE ONE DECISION.** A Settings section is a
  change of SUBJECT and a divider says so; a part of a book is a soft hierarchy over one
  continuous reading sequence, carried by the label's own 18/6 padding and tracked caps.
  **And Contents SCROLLS where Settings does not**, so a header that ruled at all would
  make Settings' positional first-versus-later question something this screen has to
  answer correctly at every scroll offset, for a line it wants nowhere. **WHETHER a
  screen's headers rule at all stays at the call site** — Settings passes `i != 0`,
  Contents passes `false` — and `Contents.dc.html` now carries the reasoning, because
  nothing on it said the absences were deliberate and that is why the render copied
  Settings' shape.
- **THE ROW'S HALF IS `rowRuleFor`'s THIRD TERM NOW, AND IT ARRIVED ONE COPY LATE.**
  `renderSettings` carried it hand-written as `rowRuleFor(...) && !nextIsHeader` and this
  file's own primitive carried the rest of it in **PROSE** ("screens with extra reasons to
  drop a rule … AND this together"). **A rule half in a constexpr and half in a sentence
  is a primitive not yet finished, and the sentence is the half that does not get copied
  to the next caller.** `nextIsHeader` defaults to false, which is the answer *by
  construction* for every headerless list rather than merely convenient; the one-line
  lookahead that answers it stays per-screen, because the two sectioned screens hold
  different row types. Proved by mutation: dropping the term reddens both Contents
  goldens, both mixed-depth goldens **and both Settings goldens**, which is what says the
  extraction moved behaviour rather than leaving a dead parameter.
- **MEASURED, and the gain is bigger than the band** because the 3px also put everything
  below the header out of register with the board: **13,274 → 8,814 (3.46% → 2.30%) at X4
  and 13,514 → 8,814 (3.23% → 2.11%) at X3**, no full-width differing row left on either
  panel. Threshold-at-128 over the bare `--export` panels — the sheet still prints `ok`
  rather than a percentage (#41). **Confined to the band and below, checked rather than
  claimed: above y=505 the count is 6,733 before AND after, at both geometries.**

**A DEPTH-1 ENTRY IS A HEADER ONLY IN A BOOK THAT HAS DEEPER ONES.** Two of the four
measured books are flat, and treating depth 1 as a header unconditionally would render
one as nothing but headers — no focusable row, nothing to select. `sectioned()` decides
once, from the list. **A sectioned book therefore always has a focusable row by
construction**, since `sectioned()` requires a depth-2 entry and every such entry is a
row; the only nothing-to-select case is an empty contents. That invariant replaced a
test case written for a state that cannot exist.

**A SECTION HEADER IS ALSO A TARGET AND IS STILL NOT FOCUSABLE.** An NCX header carries
its own `content src`, so jumping to it would work — but the board draws it as a tracked
caps label with its own rule and no value, which is not a row a selection sits on. The
cost is one unreachable target per section, and its first child usually names the same
spine entry anyway.

**GO POPS TO THE READER; THE SHELL MOVES IT.** Contents cannot push a Reader — one is
already under the menu it was opened from, and a second would leave the first below with
its own position. So it answers `popTo(Reader)` and names the chapter, the shell reads
`chosenSpine()` **while Contents is still on top** (the dispatch pops it, and after that
there is no screen left to ask), and calls `ReaderScreen::goToChapter` once the Reader is
back. That lands on page ONE of the target rather than a saved position: a reader who
picked a chapter from a list asked for its beginning.

**THE TOC IS READ WHEN THE BOOK OPENS, AND IT HAD TO BE.** It was read on demand — one
archive re-open when Contents opened, to avoid a resident cost — and on the device that
could not allocate: `loadToc` needs a second `Inflater` (**36,956 bytes** of window and
tables) plus the zip's 121-entry directory and the epub's 92 chapters, about **48 KB**,
against a heap floor with a page on glass of **45,840**. It failed every time, returned
empty, and the factory substituted its demo — so Le Fléau showed Middlemarch's chapters.

**THIS FILE ALREADY HAD THE ANSWER**, under the eager page count: "counting on a second
`ChapterReader` would buy one pass for another 32 KB window against a 45,840-byte
floor". Same window, same floor, one screen later.

At OPEN there is room — `openBook` has released its archive and the Reader's own
inflater does not exist yet, so the heap is ~133 KB — and it is cheap to keep: **1,161
bytes of labels for a 96-entry book**, ~12 a row. So the shell reads it in `openBookAt`
and hands over a copy when Contents opens, with no card work on that press at all.

**AND THE FACTORY MUST NOT SUBSTITUTE.** `contentsToc_.empty() ? demoContents() : …` is
what turned a diagnosable allocation failure into a puzzle. The demo is asked for now
(`setContentsDemo()`, as `setReaderDemo()` is) and an unprimed Contents or reader menu
is **refused** — a refused push leaves the menu standing, which is wrong in a way the
reader can see through, and the log says why. `contentsPrimed_` is its own flag rather
than "the list is non-empty", because a real book with no NCX primes an EMPTY list and
must still build: it reads fine and simply cannot name its chapters.

**`readerBookTitle_` IS NEVER ASSIGNED** — a factory member read by two cases with no
setter anywhere, so Contents' band would have drawn an empty book name. The title comes
from `readerBook_.title`, which is the OPF's own and arrives with the spine.

`App::at(index)` exists because the menu is an overlay and the chapter it marks `NOW`
belongs to the Reader underneath: reached through the stack rather than remembered, since
a chapter crossing while the menu is closed would make a remembered one stale.

**AND TWO STALE DEAD BUTTONS WENT WITH THIS.** The Reader's Activate answered `none()`
behind "ReaderMenu is not built", which was true when written. The actions overlay's
`Open` row answered `none()` behind "the Reader is Phase 3, exactly as Confirm on a
Library row is" — and Confirm on a Library row opens a book, so that row had become a
dead button on a shipped screen while its test kept pinning the placeholder. Both are
live, and both tests now assert the action.
