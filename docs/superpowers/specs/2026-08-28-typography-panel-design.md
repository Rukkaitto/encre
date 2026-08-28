# Typography panel with live re-pagination

Issue [#4](https://github.com/Rukkaitto/encre/issues/4). `roadmap:788`.
Board: `design/Typography.dc.html` (exists), plus one new state board.

## What this is

The reader's Typography panel: five rows that change how a book is set, a live
preview of the body face at the chosen size, and a reader that re-paginates from
the page you were on when you leave.

It also settles a decision the roadmap has been holding open since 2A-2
(`roadmap:1269`): *"Reader body text may be under-sized by its own spec. The
Typography board states Size 18 PT, which at 150 DPI is 37.5px; the boards
currently render book text at 32px... Phase 3 owns body text -- decide there, on
the panel, not on a monitor."* **This panel is the mechanism that lets it be
decided on the panel**, because 18 PT is one of the steps. Nothing here picks a
different default; it makes the question answerable by pressing a button on the
device.

## What this is NOT

- **Not the pagination/SD cache keyed by a settings hash**, which is issue #19.
  Changing a setting re-paginates the open chapter and nothing else; the rest of
  the book is re-paginated when it is next opened, exactly as today.
- **Not hyphenation, widows or orphans** (#18). `RAGGED` is a new alignment
  value, not a new line breaker.
- **Not a second body face.** One is vendored, so the `Font` row has one value.
- **Not a Settings entry point.** Settings' TYPOGRAPHY rows stay unfocusable.

## Where the state lives

Four new fields on `reader::Settings`, in `/.reader/settings.json` with
everything else:

| field | type | default | what it drives |
|---|---|---|---|
| `bodyPpem` | `int` | 32 | `ScalableFont::init`'s size, and `PageMetrics` through the face |
| `margins` | `int` (px of side padding) | 18 | `kReadPadX` -> `PageMetrics::columnLeft/columnW` |
| `lineSpacing` | `int` (em x 1000) | 1700 | `PageMetrics::leadEm1000` |
| `justify` | `bool` | true | new `PageMetrics::justify` |

**`kSettingsVersion` stays 1.** An added field takes its default from an older
file -- `settings.h` states that rule and this is the case it was written for. A
card carrying today's file loads and behaves identically.

**Every default is today's behaviour, to the pixel.** That is the property that
keeps every reader, sleep and book-details golden where it is: no golden is
re-blessed by this feature, and any golden that moves is a bug in it.

**No `font` field.** With one face there is nothing to store, so the row reads a
constant. A field whose only value is its default is a second spelling of a
constant, and this file has a rule about second spellings.

`validate()` clamps each of the four the way it already clamps `sleepAfterMs`: a
value outside the offered set is clamped to the nearest offered one and the rest
of the file still loads. New public range constants beside `kSleepAfterMsMin` and
friends, for the reason those are public -- the screen has to know what it is
allowed to offer and a second copy of the numbers would drift.

### Rejected

- **A separate `Typography` struct in its own file.** Two files, two versions,
  two load failures to distinguish in the boot log, for a struct with four ints.
- **Applying to the reader on every step.** A chapter re-index (~545 ms on the
  device) per press, for a reader that is not on screen -- the panel is a full
  screen, not an overlay, so nothing of the page is visible while it is open.

## The screen

`TypographyScreen` in `core/`, `ScreenId::Typography` appended to the enum
(appended, not inserted: the session record stores a screen by NAME so an
insertion could not silently become another screen, but appending also leaves
every existing ordinal where it was). `Fidelity::Mono`, the default. A full
screen, not an overlay: it clears and draws its own hint bar.

It holds a `Settings` copy and a `SettingsSink*`, the same pair `SettingsScreen`
holds, with the same contract -- `commit` applies AND persists, and a refused
write still shows the new value, because the change has taken effect in RAM and
reverting the display would make a read-only card look like a screen that ignores
its buttons.

It holds a `const GlyphSource* body_` for the preview, as `ReaderScreen` does,
and passes it to `Theme::renderTypography`.

Two catalogues have to name it, and neither has a `-Wswitch` to lean on:
`screenName()` for the logs, and `session_record.cpp`'s name array as
`"typography"` -- the record stores a NAME rather than an ordinal, and that array
is a storage format rather than a log label, so the two strings are separate
facts that happen to agree.

`FocusScreen` is the base, so `focus()`/`setFocus()` come in as a pair and cannot
be adopted by halves. Five rows, **four of them focusable** (`Font` is not -- see
below), no `ScrollWindow` behaviour needed (five rows fit) -- but `FocusScreen`
gives it a window with `visibleRows == count`, which behaves as a bare `Focus`,
and `Focus::Gate` is what skips the unreachable row, exactly as it does for
Settings' section headers.

### One mode, one word per button

Up/Down move the focus, wrapping like every other list. Confirm (`CHANGE`)
**cycles the focused row's value forward in place**, wrapping. Back (`BACK`)
leaves for the Reader.

Hints are `BACK / CHANGE / UP / DOWN`, and they never change.

**THIS REPLACED A TWO-MODE DESIGN, AND THE REASON IS WORTH KEEPING.** The first
version had a browse mode and an edit mode: Confirm entered edit on a row, Up/Down
then stepped its value between `‹ ›` chevrons, and Confirm left edit again. Its
hint bar read `DONE / EDIT / UP / DOWN` browsing and `DONE / OK / UP / DOWN`
editing, and **that was reported as confusing off the rendered board** — correctly.
`DONE` and `OK` are synonyms in English, so the bar offered two words for
"finished" and nothing said that one finished the ROW and the other left the
SCREEN. Rewording it to `BACK / DONE` would have narrowed the ambiguity without
removing it; dropping the mode means it cannot exist.

Three things fell out of the change, and none of them was the point of it:

- **The chevrons went**, and with them a mismatch nobody had named: `‹ ›` is a
  HORIZONTAL marker and the buttons that stepped it were the vertical Up/Down
  pair. On this device a horizontal marker points at the two SIDE buttons.
- **`design/TypographyEditing.dc.html` was deleted.** There is no second state
  left to board, so there is no second state to keep in step.
- **It is the mechanism Settings already uses.** That screen cycles five sleep
  values and three refresh cadences on `CHANGE`, and the argument written into
  `screen_settings.cpp` transfers unchanged: "this is one button, so there is no
  way back except round. Five sleep steps and three cadences keeps a full cycle
  short enough to be usable on a panel that costs ~520 ms a repaint." Two screens
  editing a value list two different ways would have been two mechanisms for one
  job.

**WHAT IT COSTS is one direction.** The longest list here is five, so any value is
at most four presses from any other — the same worst case Settings accepted. The
alternative was a second mode whose two exits could not be told apart.

### The Font row is drawn and unreachable

`Font` has one value while one body face is vendored, so `CHANGE` on it would do
nothing visible.

**It was kept live in the two-mode design**, on the grounds that a row entering
edit mode with NO chevrons was an honest picture of a setting with nowhere to
step. That justification died with the chevrons: a `CHANGE` that produces an
identical frame is the silent no-op this project has been bitten by twice.

So **the focus skips it**, which is Settings' own rule for a row with nothing
behind it, and the focus starts on `Size`. It is drawn **exactly** as any
unfocused row — no dimming, because `focusable` is about input and a visual
difference nobody designed is worse than none. The board moves its inversion to
`Size` to match.

The row stays rather than being cut, because it states a true fact the reader
wants: the book is set in Literata. It becomes focusable in the commit that
vendors a second face, and that is one line.

### The values wrap

Every list in this firmware wraps, and the recorded hazard is not the wrap —
CLAUDE.md settles that a wrap is the only thing a press at the end can do, so it
can never read as a dead button. The hazard is **auto-repeat**: "a wrap belongs to
a press and a hold rests at the end", which is why `Focus::move(delta, held)`
clamps for a held button.

**This screen declares no repeat**, as Settings does not, and it must not — every
size step re-inits the body face, so a held Up would race through the sizes
re-rasterising the alphabet on each one. One step per press, and the sharp edge on
wrapping never arises.

So `setWrapping(false)` stays unused, and both the focus and the value cycle are
one more list rather than an exception with its own rule.


### The values

| Row | Steps | Default | Label form |
|---|---|---|---|
| Font | Literata (unreachable) | -- | `LITERATA` |
| Size | ppem 27, 32, **38**, 42, 46 | 32 | `12 PT` .. `22 PT` |
| Margins | 10, 18, 30 px | 18 | `TIGHT`, `COMFORTABLE`, `WIDE` |
| Line spacing | 1400, 1550, 1700, 1850, 2000 | 1700 | `1.4` .. `2.0` |
| Alignment | justify true / false | true | `JUSTIFIED`, `RAGGED` |

**The PT label truncates**, as `sleepLabel` truncates minutes: `pt = ppem * 72 /
150`, so 27 -> 12, 32 -> 15, 38 -> 18, 42 -> 20, 46 -> 22. All five are clean.

**ppem 38 is on the list because 18 PT is the board's own stated value**, which
is what makes the roadmap question above answerable rather than merely raised.

**46 is the top because the glyph cache is thrash-free to ppem ~46** (CLAUDE.md,
The glyph cache). At 46 the roman arena is 24,576 B and the italic 15,360 --
39,936 for the pair against today's 26,624, so +13,312 against a 42,152-byte
reading floor. Past 46 the cache does what it is built to do, wrap and
re-rasterise, at ~3,794 us a glyph; that is not a size to offer.

**A value from a hand-edited file that is not in a list** is clamped by
`validate()` to the nearest offered value before the screen ever sees it. Same
hazard `cycleFocused` handles by landing on index 0's successor, solved one layer
earlier: a file is where a stray value comes from, so the file is where it should
be corrected -- and then the stepper only ever indexes a list it is on.

### Alignment is one line in layout.cpp

`PageMetrics` gains `bool justify = true`, and `PageBuilder` reads
`justifiable(kind_) && m_.justify` where it reads `justifiable(kind_)`.
`kMinJustifyFillPercent` is untouched: it answers "is this line full enough to
stretch", which is a different question from "does this reader want stretch at
all".

## The preview box

A fixed specimen string in `core/`, next to the screen. The band still names the
book, as `ReaderMenu.dc.html`'s band does -- it says which book you return to.

**THE BOX'S HEIGHT IS FIXED, NOT CONTENT-SIZED.** It takes the panel less its
fixed runs -- band, `LIVE PREVIEW` label, five rows, footnote, hint bar -- so as
many specimen lines fit as fit at the current size, and **the five rows below
never move**. Derived, not pinned, which is Home's title-budget rule; and it is
the scroll-rail gutter decision again, where a reflow the user sees on every
press lost to a fixed cost they see once.

The theme wraps the specimen with `wrapProseLead` against the body face, so the
preview is measured by the code that lays out the book rather than by a second
wrap that would inherit none of its fixes.

`Theme::renderTypography(fb, fonts, body, vm, plane)` -- the body face as an
argument, as `renderReader` takes it, because the preview is set in a
`ScalableFont` rasterised at a runtime size and not in one of `FontSet`'s twelve
fixed roles.

**THE PREVIEW CANNOT PREVIEW THE MARGINS, AND THAT IS WORTH WRITING DOWN BEFORE
SOMEBODY "FIXES" IT.** The box is CHROME geometry -- the board's 24px page
margins less its own border and padding, 396px of measure on the X4 -- where the
reading column is `panelW - 2 * margins`, 444px at the default. They are
different numbers and always will be, so the box shows `Font`, `Size`,
`Line spacing` and `Alignment` faithfully and cannot show `Margins` at all.

Making the box track the margin setting would be worse than not trying: it would
move the border on every step of one row, and it would still not be the reading
measure, so it would look like a preview and be wrong by 48px. A preview that is
honest about four of five settings beats one that appears to cover all five.

## The apply path

`DONE` answers `Action::popTo(ScreenId::Reader)`.

**The Contents pattern verbatim**, and for the same reason: the panel cannot push
a Reader, because one is already underneath the menu it was opened from, and a
second would leave the first below it with its own position. So the shell reads
what it needs while Typography is still on top -- the dispatch pops it, and after
that there is no screen left to ask -- and acts once the Reader is back.

The shell, in order:

1. **On the way IN** (the press that pushes Typography): shrink the Reader's page
   ring to 1 (`setPageCacheDepth`). It is invalid across a size or margin change
   anyway, and it buys up to ~10 KB of the headroom the arena grow needs. `init`
   takes the new arena before releasing the old, so the transient peak is both.
2. **On each step**: `commit` writes `settings.json` and re-inits `gBody` and
   `gItalic` at the new ppem, so the preview is the same face object the reader
   will use -- a genuine live preview rather than a second approximation of one.
   `init` is `nothrow` and keeps the arena it had on failure, so an OOM device
   gets a `false` and a log line, not an `abort()`.
3. **On DONE**: read the settings while Typography is on top, dispatch the
   `popTo`, then recompute `Theme::readerMetrics` from the new margins, lead and
   justify, and hand it to `ReaderScreen::setMetrics` targeting the **current
   block** cursor.

Ordering is load-bearing: dispatch, then apply, then paint, in one loop
iteration. Between the pop and the apply the Reader's `page_` and `metrics_`
describe a layout that no longer exists, and a paint in that window would draw
old line positions in a new face.

### Three things that fall out for free

- **`fitOf` already grades this.** `ReadingPosition` stores `ppem` and `columnW`
  and grades a change to either as `Relaid`: spine and block usable, `line` not.
  So a size or margin change lands the reader at the top of the right block,
  which is the honest answer and needs no new code. Line spacing and alignment
  change neither, because the wrap depends on the face and the column and not on
  the lead, so `line` legitimately survives them.
- **The page total goes back to an em dash and re-arrives** in the existing
  deferred count, in `kCountQuietMs`. That is literally what the board's footnote
  promises: *"the book re-paginates in the background"*.
- **A wake needs no special case.** The record names Typography on the stack, the
  factory builds it from the settings it already holds, and the Reader beneath is
  primed from `last.json` at the saved ppem -- which `fitOf` grades against the
  new one and lands at the block.

## Settings' TYPOGRAPHY rows

**They read the real values and stay unfocusable.** The rows are drawn, focus
skips them exactly as it does today, and their values come from `settings_`
instead of `Item::placeholder`.

A placeholder is right only while nothing exists behind the row. Once a setting
genuinely exists, a placeholder is a screen displaying a stale number -- the
defect class this repo keeps recording, and one the user can now create in three
button presses. Four of the five placeholders already equal the defaults; only
`18 PT` is wrong, and it is wrong by 3 PT.

The `Font` row keeps a constant, because there is no field to read.

## Board work, first

`make compare` is what keeps design and firmware honest, and a UI change goes
into the design HTML first -- including when the board is what is wrong.

**DONE, and reviewed on the rendered boards (2026-08-28).**

1. **`design/Typography.dc.html`** -- the preview box **derives** its height
   (`flex: 1`, never a pinned number: a pinned one was wrong by ~42px and
   `flex-shrink` hid it); the hint bar is `BACK / CHANGE / UP / DOWN`; the
   footnote is `APPLIES TO EVERY BOOK. YOUR PLACE IS KEPT.`; the specimen is
   Middlemarch's full opening sentence; the focus sits on `Size`, not `Font`.
   Measured: the box is 250px on the X4 and 241px on the X3, so 222px and 213px
   of text area, holding four whole lines of specimen at the default setting.
2. **`design/Settings.dc.html`** -- the `Size` row's value `18 PT` -> `15 PT`,
   which is ppem 32 truncated. The other four already state the defaults.
3. **NO SECOND BOARD.** `TypographyEditing.dc.html` was written and then deleted
   with the edit mode; `tools/compare-design.py` gained its entry and lost it
   again. There is one state, so there is one board.
4. **Republish the design canvas** (`design/canvas.json`) -- one changed board.
   Issue #34 already says the canvas is behind; this adds to it rather than
   fixing it. **Not done here.**

## Testing

**Goldens at both geometries** -- `typography` at 480x800 and 528x792. Two, not
four: there is one state now. Every built screen has a golden and this is a built
screen.

**Proved by mutation, not by passing.** Each golden and each new test case is
checked by breaking the code it defends and confirming the failure count -- and
the mutation is confirmed to have LANDED before its result is believed
(`touch` the file; this project has been fooled by a same-second rebuild and by a
mutation on a line the input never reaches).

| file | what it pins |
|---|---|
| `test_screen_typography.cpp` (new) | the five rows in the board's order, the values in the board's forms, `CHANGE` cycling each multi-value row through its whole table and back, the focus wrapping and SKIPPING `Font`, the focus starting on `Size`, the one hint set, a commit carrying the value the row shows, a refused write still showing it, and that no gesture carries more than one step |
| `test_settings.cpp` | the four fields round-trip, each clamp, and a file with none of them loading with defaults under version 1 |
| `test_layout.cpp` | `justify=false` leaves `extraPerGapF26` at 0 on every line; a lead change and a margin change each move page boundaries |
| `test_focus_restore.cpp` | its two counts go 7 -> 8, and `kAllScreens` gains a row (its `static_assert` fails until it does) |
| `test_screen_settings.cpp` | the five TYPOGRAPHY rows show `settings_`, not placeholders, and are still unfocusable |
| a re-pagination case | apply new metrics at a cursor and land on the page holding that block, checked for every page of a chapter -- the walk-stops-a-boundary-early bug is right at page 1 and wrong after it |

**`test_focus_restore.cpp`'s counts are the check that matters most here**,
because they are the mechanism that stops this feature adding a screen that
reports a focus and drops it. They fail until Typography is added, which is the
test doing its job rather than an obstacle.

**`make compare` on the board**, with the percentage read rather than the word
`ok`: `ok` means the simulator produced a frame, not that the frame matches. A
merge changed `ReaderMenu.dc.html` under its screen and the sheet said `ok` at
13.02%.

## What is out of scope, and stays out

- **`tools/compare-design.py:109` lists `GoToPage.dc.html`, which does not
  exist.** The board was deleted when the `Go to page...` row was cut. `make
  compare` names a board that is not there. Found on the way; its own card.
- Issues #19 (pagination cache), #18 (hyphenation), #15 (cache budget at 41),
  #34 (canvas republish).

## Device verification, which an agent cannot do

Flashing must be run by the user, so this work reaches `On glass` on the board
and stops there. What needs eyes on the panel:

- **Is 18 PT the right default?** The roadmap says decide on the panel. This
  ships 15 PT (unchanged) and makes 18 PT reachable; the answer is a separate
  card.
- **`RAGGED` against `JUSTIFIED` at 32px in a ~400px measure.** The roadmap
  records rivers as inherent to this measure; ragged is the first thing that has
  ever been able to test that claim.
- **ppem 46 with a book open.** The arena grow's transient peak against the
  reading floor, and `[body]`/`[italic]` are the lines that report it.
- **The re-paginate on DONE**, timed: `[i]` will carry it as `post=`, and it is a
  chapter re-index, so it should read like a chapter crossing and not like a page
  turn.
