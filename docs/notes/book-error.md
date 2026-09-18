# The corrupt-book dialog

Extracted from `CLAUDE.md`, which keeps a stub under this heading and is where
the cross-references to it point. Same standing as anything in that file.

`BookError.dc.html`, `BookErrorUnreadable.dc.html` for the refusal that is not
damage, and `BookErrorMemory.dc.html` for the refusal that is not about the file at
all — **the one shape with no `DELETE FILE…` slab.** Issue #5.

**WHAT IT CLOSES IS A PRESS THAT DID NOTHING.** `openBookAt` refused a book with a
log line and **nothing on the panel**, so Confirm on a damaged book produced no
visible change — worse than a dead button, because the press was correct and the
file is the problem. `book.h` had already anticipated the screen in as many words:
a refusal is "NEVER an abort ... the caller has a screen it can put the reason on".

**RAISED ONLY WHEN `push` IS TRUE**, which is a user press — a Library row or Home's
CONTINUE. The wake restore is excluded deliberately: `App::restore` already stops
short of a Reader it cannot build and leaves Home or the Library standing, which is
wrong in a way the reader can see through, and waking into a modal about a book
nobody just asked for replaces a calm landing with an interruption. The two other
`openBook` call sites are not this screen's and were checked rather than assumed —
the sleep-cover decode answers `CoverResult::ReadFailed` and falls back to the
reading card, and Book details' author lookup is best-effort; neither is a reader
asking to read a book.

**THREE COPY SHAPES, BECAUSE ONE SENTENCE WOULD BE A LIE.** `openBook`'s refusals are
not one event. Most are parse failures; `"cannot open the book file"` is `openRead`
returning null, a file that is gone or a card that is; and the `"not enough memory
to …"` family is a book that is fine on a device that is momentarily short. (This
said "four reasons … the fourth" while there were four; the count moved when the heap
guards added a class, which is why the shapes are named here and the reasons are
not counted.)

**`SdFileSystem::openRead` does not call `noteCardGone()`**; only a handle read that
comes up short does. So a card pulled between the Library's listing and the press is
noticed by `pollCardPresence` between 2 s (the fast probe) and 25 s (the FAT-scan
backstop), and for that whole window a single sentence would tell the reader a
perfectly healthy book "appears to be damaged". **A false claim is worse than an
absent one** — the same call this file already records for the unread battery gauge
(`-1`, not `0%`) and for the charging bolt that spends a refresh on the unplug edge.

**AND THE THIRD IS `OutOfMemory`, WHICH IS THE SAME ARGUMENT ARRIVING ONE REFUSAL
LATER** — `design/BookErrorMemory.dc.html`, which is `BookError.dc.html` with one
sentence changed exactly as `BookErrorUnreadable.dc.html` is. `openBook` can run out
of memory (see **Memory, which is what a real book runs into**), and **neither
existing shape may carry it**: `Damaged` says the bytes are not a book and they are,
`Unreadable` says the card would not answer and it did. The book is fine and the
device was momentarily short, which is `CoverResult::OutOfMemory`'s distinction one
screen over and the reason that enum has six values rather than a bool.

- **The copy says WHAT and not WHAT TO DO**, deliberately: *"…needs more memory than
  is free right now. The file was left untouched on the card."* The reader has no way
  to free memory on purpose — there is no second book to close and no restart control
  — and the one thing that reliably helps, a power cycle, is a promise about the
  resume path this screen is in no position to make. **`right now` is load-bearing the
  way `appears` is**: what the firmware knows is that the heap was short at one
  instant, not that this book is too big for the device. Its second sentence is
  `Damaged`'s character for character.
- **It clears the wrap boundary by 30px** against `test_book_error_copy.cpp`'s 12px
  floor (`Damaged` 19, `Unreadable` 39), so #76's rule was satisfied at authoring time
  rather than measured after the fact — which is what having made that rule mechanical
  for one screen buys.

**AND THIS SHAPE HAS NO `DELETE FILE…` SLAB, WHICH REVERSES THE DECISION THAT SHIPPED
WITH IT.** The slab was drawn and live on all three shapes, and both the header and
this file recorded that as **owed an owner's opinion rather than settled**. The owner
has settled it: **the file is fine.** Offering to delete a good book to fix a
transient shortage is a nudge in the wrong direction, and a reader might take it.
`Damaged` and `Unreadable` keep theirs exactly as they were — on those two, wanting
the file gone is reasonable.

- **THE PRECEDENT IS EXACT AND ALREADY IN THIS FILE: `HomeEmpty` HAS NO ACTION SLAB**,
  because its `SEND BOOKS OVER WI-FI` could not work once Wi-Fi was cut — *"a primary
  action that cannot work is worse than none"*. Same shape one screen over.
- **ABSENT, NOT INERT, AND THAT DISTINCTION IS THE WHOLE LICENCE.** A slab that
  **draws and does nothing** is the `works only sometimes` trap this project has
  shipped twice, and it is the recorded reason the slab is live on `Unreadable` — two
  shapes differing only by a sentence, so a reader meeting a dead slab has nothing to
  learn the rule from. A slab that **is not there** teaches nothing because there is
  nothing to press: the panel simply has one action, as `SdMissing` does. **Do not
  make it inert.**
- **TWO OF THE THREE OBJECTIONS RECORDED AGAINST THIS WERE ALREADY FALSE WHEN WRITTEN.**
  "A fourth board" — the third board exists and is the one edited; nothing was added.
  "A panel whose height depends on which refusal it is reporting" — it already did,
  and `paintFootprint`'s own comment says so: the shapes wrap to different heights, so
  `book_error_unreadable` is a 490px panel against `book_error`'s 531. Height varying
  by shape was the status quo, not a cost of this change.
- **THE ROW COUNT IS THE ONLY GATE.** `rowsFor()` gives this shape **one** row, so the
  focus cannot reach `kDelete` and `onGesture` is deliberately **not** also gated on
  `offersDelete` — a second condition is free to drift from the first, which is the
  class of bug `Focus` was extracted to delete. The view-model flag is what the
  RENDERER asks, and it is an explicit `bool` rather than `deleteLabel.empty()`:
  `ListRow::discloses` is the recorded precedent for why deriving this from an empty
  value is wrong, and a slab is a bigger claim than a chevron.
- **THE PANEL IS 451px, WAS 531px, AND THE 80 IS `kActionH` PLUS THE GAP THAT
  SEPARATED THE TWO SLABS.** Derived, never pinned: `4 + 73 + (18 + 28 + 12 + 210 + 18)
  + actionsH`, where `actionsH` is `2·68 + 12 + 20 = 168` with the slab and
  `68 + 20 = 88` without it. **The gap goes with the slab it separated** — the board's
  actions block is a flex column and a `gap` is BETWEEN items, so one slab has nothing
  for it to separate; keeping it would leave 12px of dead air and put the centred panel
  6px high. `actionsH` is now computed **once** and spent on both the paragraph's
  clamp budget and the panel's height, which had shipped as two copies of one
  expression — the shape that lets a budget and a height disagree.
- **THE BAR FOLLOWS THE PANEL: `CLOSE · OK` and two dead slots.** `SELECT` promises a
  choice and there is nothing to choose between; Up and Down have no second row. The
  Confirm slot is named after the slab it activates, which is `SdMissingScreen`'s own
  rule (`{"", "RETRY", "", ""}`). An empty slot is **36px, not zero**
  (`kHintEmptySlotW`), and the board authors both as the spacer eight other boards use
  — measuring one as nothing draws the two live slots in the wrong places. The bar's
  height did not move: its top rule is row 736 at both geometries, before and after.
- **MEASURED: 3.37% / 3.41%**, from 3.45% / 3.54%. The controls in the same tree are
  `book_error` **3.44% / 3.53%** and `book_error_unreadable` **2.99% / 3.17%**, which
  reproduce this file's recorded figures **to the digit** — that is what says the before
  and after are one instrument rather than two. The sheet still prints `ok` and not a
  percentage (#41), so these are threshold-at-128 counts over the bare `--export`
  panels. It improved because what left the panel is a tracked-caps label, which is
  where Chrome's subpixel advances and the firmware's whole-pixel ones disagree most per
  pixel of ink.
- **THE TWO RE-BLESSED GOLDENS MOVED A LOT AND IN EXACTLY TWO BANDS.** 49,629 px (X4)
  and 49,404 (X3): the panel band (old ∪ new, rows 135–665 / 131–661) and the hint
  bar's label rows (761–779 / 753–771, 1,414 px at both geometries). **Zero differing
  pixels anywhere else** — the veiled Library above and below the panel and the bar's
  own top rule are byte-identical. And `book_error` and `book_error_unreadable` did not
  move by a pixel at either geometry, which is what says the change is in the one shape
  and not in the shared path.

The screen takes a bounded `BookErrorReason`, **never the `why` string**, which is
developer English (`"the spine names no chapters"`), unstyled, unbounded and with no
slot on any board. It still goes to the log, where it is actionable.

**WHICH SHAPE IS `core/`'s NOW, AND WHERE A DELETE RETURNS TO IS STILL THE SHELL'S.**
That split used to read "both are decided in the SHELL", and the mapping half was a
bare `strcmp` against a literal the shell spelled and `book.cpp` spelled again — in
the one directory with no test harness, where five of this project's bugs have hidden.
A third shape would have made it two comparisons; a fourth that nobody remembered to
add reads as "damaged" on a healthy file. `bookErrorReasonFor` is the whole mapping
from developer English to the only vocabulary the panel has, and it lives beside the
enum. The `returnTo` stays the shell's, because only the shell knows which screen
asked.

**THE CLASS OF REFUSAL IS A PREFIX, NOT A CODE.** Every layer on the open path says
`"not enough memory to …"` and then what it was doing, so the class is readable off
`kOpenOutOfMemory` while the log keeps the site. That is a convention rather than a
type, and `test_heapguard.cpp` is what stops it being a convention nobody kept — it
drives real refusals out of every guarded site with an injected allocator and asserts
each one lands on this board. The alternative was a reason code out through
`openBook`'s signature and its five callers, which is worth it if a fourth class ever
appears.

**`DELETE FILE…` IS WHY `DeleteConfirmScreen` TOOK FACTS.** It held a
`LibraryScreen&` and acted through `deleteFocused()`, so it was reachable only from
the Library — and **Home's CONTINUE has none**, which is the likeliest real
corruption path because it is a book the reader was part-way through. A button that
works only sometimes is worse than one that never works, because nobody can learn
the rule. `BookDetailsScreen::Facts` had solved the identical problem for the
identical reason. The removal became a shell latch beside `Open`/`Retry`/`Finish`,
since a delete's consequences — `forgetCardFacts`, the rescan, `gHomeStale`,
`gLibraryStale` — are all the shell's.

- **PRIMING THE FACTS IS PART OF RAISING THE DIALOG, and leaving it out left the
  slab dead in exactly the case the refactor existed for.** `BookError` returns a
  bare `Action::push(ScreenId::DeleteConfirm)`; the factory checks `deleteFactsSet_`
  above its Library fallback, so from the Library it worked *by luck* — the focused
  row happened to be the failing book — and from Home it was refused outright.
  `openBookAt` primes both facts together now.
- **AND THE CLEAR IS NOT OPTIONAL.** Nothing cleared `deleteFacts_`, so: fail to
  open a book, dismiss, then `Delete…` a *different* book from the actions panel,
  and the confirmation named and removed the corrupt one. `clearDeleteFacts()` sits
  beside the `clearDetailsFacts()` that exists for the identical reason on the
  identical press.

**`DELETE FILE…` REPLACES THIS DIALOG RATHER THAN STACKING ON IT, AND A PUSH IS WHAT
SHIPPED FIRST.** `App::render` draws **every** overlay above the topmost non-overlay,
so pushing one overlay from another leaves the asking screen's panel standing under
the new one's veil. That is invisible between `ItemActions` and `DeleteConfirm` — the
confirmation is 380 wide against 340 and taller on both geometries, so it covers the
actions panel completely, which is why no board draws that panel behind it. **This
screen breaks the coincidence in the one direction that shows**: its paragraph makes
its panel TALLER than the confirmation's, so the error dialog stood out above and
below the thing meant to replace it. Reported off the device, and nothing on the
desktop had a reason to look — both goldens pin a single overlay.

- **`Action::replace` is the primitive**, not a special case in the screen. Two
  Actions cannot express it for `Action::popTo`'s own reason: a screen returns ONE
  Action, and one that followed a `Pop` with a `Push` would be reaching into the
  stack.
- **IT PUSHES BEFORE IT REMOVES**, so a factory that refuses leaves the stack exactly
  as it was — popping first would lose the screen that asked and put the reader back
  on the list with nothing to show for the press. From the root it degrades to a
  push, because erasing the root leaves nothing to render and nothing to receive the
  next event.
- The **depth** is the property the tests pin, and mutation says so: a push leaves
  three where a replace leaves two.

**A CENTRED PANEL MUST RESERVE THE HINT BAR TWICE, AND `renderDeleteConfirm` HAD THE
SAME DEFECT.** The clamp budget was `fb.height() - panelFixedH`, the whole canvas.
With a 255-character name — FAT's LFN maximum, so a name a real card can hold — the
panel ran to the canvas bottom, `DELETE FILE…` was sliced by the hint bar and the
panel's bottom border went off-glass. `centreIn` splits the slack evenly, so
reserving the bar **once** still leaves the panel hanging half a bar into it: the
budget is `height - 2 * hintBarHeight(...)`, derived and never pinned.

- **THE TEST THAT EXISTED TO CATCH THIS PASSED VACUOUSLY.** It found the panel by
  the first and last row carrying a `>= panelW` run, and the caption's **rule** spans
  the content width between the two side borders — so all three are contiguous and
  that row measures `panelW` too. With the bottom border off-canvas entirely, the
  "last such row" resolved to the caption's rule and the bound read `82 < 735`. Both
  cases locate the panel by its **side border columns** now, which nothing else on the
  frame inks. Reverting the budget fails 8 assertions; under the old form it failed
  none.

**THE BOARD DECLARES THE WRAP AND THE BOUND**, and for a while the code had both and
the board neither — `overflow-wrap: anywhere` on the paragraph and `max-height: 100%;
overflow: hidden` on the panel. Note this board needs the bound **one run further
down** than `DeleteConfirm.dc.html` does: there the caption carries the filename and
is the part that grows, here the caption is the fixed `CAN'T OPEN FILE` and the
PARAGRAPH carries the name. Getting that backwards — clamping the caption, wrapping
the prose `Normal` — put **391 pixels of a single realistic 67-character filename
outside the panel** on the X3.

**BOTH COPY SHAPES SAT ON THE WRAP BOUNDARY AND BOTH ARE OFF IT NOW (#76).** The
damaged sentence broke after `appears to be` because `damaged` needed **337px
against a 336px column** — one pixel. Chrome fits it, since the firmware's
whole-pixel advances measure ~3% wider, so the firmware wrapped to six lines where
the board wrapped to five, the centred panel was 41px taller, every rule landed
~20px out, and the sheet read **11.12% / 11.70%** against 3.58% for the screen that
differs from it only by a sentence. `appears damaged` clears it by 19px:
**3.44% / 3.53%**.

**THE UNREADABLE SHAPE WAS ON THE SAME EDGE AT 3px** and agreed with Chrome by luck
rather than by clearance — it would have flipped on any change to the face or the
ramp. Dropping `SD` takes it to 39px and to **2.99% / 3.17%**, and makes it agree
with the other shape, which already said "on the card".

**THE SLACK IS THE WRONG METRIC AND CHECKING IT WOULD NOT HAVE CAUGHT THIS.** A line
with 15px of slack is safe when the next word is 130px wide and on a knife edge when
the next word is 14px; what decides a break is by how much the NEXT WORD overflowed.
`test_book_error_copy.cpp` asserts that is at least 12px — ~3% of the column plus a
little — so a copy edit cannot put a line back on the boundary. It is proved by
mutation: the shipped sentences fail it reporting exactly 2 and 3.

**That makes this file's own rule mechanical for one screen** — *a specimen board
must not put a line on the wrap boundary* had no enforcement anywhere, and
`ReaderList`'s "Space is measured in rows." had already been moved by hand for it.
Every other board is still on the honour system.
