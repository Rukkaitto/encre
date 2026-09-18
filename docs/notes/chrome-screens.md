# The chrome screens

Extracted from `CLAUDE.md`, which keeps a stub under this heading and is where
the cross-references to it point. Same standing as anything in that file.

V1's chrome is complete as of 2C-3. What each screen is, and the one thing about it
worth knowing before changing it:

| Screen | Board | The thing |
|---|---|---|
| Home | `Main.dc.html` | Focus starts on the CONTINUE block (`-1`), not the menu. Its title WRAPS and its chapter NAME elides — two card-sourced runs in one column, and only the title may grow. **NO COVER: the reading column is the whole content width** (#95). |
| Home / empty | `HomeEmpty.dc.html` | A **variant**, not a screen: same `ScreenId`, same view model, same menu. |
| Home / nothing open | `HomeUnopened.dc.html` | The same variant with different words. What the device actually shows today. |
| Home / missing book | `HomeMissing.dc.html` | A **variant**, and the only one that keeps the reading column: the pointer names a book the card no longer has, so the title, author, percentage and chapter are still true and only the file is gone. A bordered strip says so and CONTINUE is **absent, not inert** — one predicate, `offersContinue()`, takes the slab, the ring's -1 and the bar's READ slot together. The note is the only growable run in that column, so it is the only one with a budget. |
| Library | `Library.dc.html` | The only list that scrolls today, and the only screen with a rail. A book row is the FOLDER row with a different mark (#95) — one expression picking `kBook` or `kFolder`, and the 44×64 slot stays because `bookRowContentH` takes the max with it. |
| Library / scrolled | `LibraryScrolled.dc.html` | Reached by pressing PAST the focused row and back — arriving from above windows it differently. |
| Item actions, Delete confirm | their own boards | Overlays; a focus move repaints the overlay alone. |
| Book details | `BookDetails.dc.html` | Not an overlay, despite covering the Library. Its title **wraps**; everywhere else elides. **NO COVER, and its block's height is the COLUMN's now** — the third and last placeholder to go (#95). The height was `max(column, cover)` and the cover's 180px won for every title the screen can draw, so it was a CONSTANT and the column's runs were free; each run costs its line box now and the 2px rule below moves from 290 to 203. |
| Settings | `Settings.dc.html` | **Twelve items — FOUR sections and eight rows** — and every drawn row responds. The `Wallabag` row joined CONNECTIONS beside `Wi-Fi`. Both figures have moved five times; count `kItems` rather than trusting this line, which was written saying "three sections" and was wrong when counted. |
| Sleep | `Sleep.dc.html` | Painted directly, never pushed — a push would make the wake restore into it. Its title, author and chapter **all wrap**; the chapter's two lines are reserved UNCONDITIONALLY, so the title's budget cannot move when the reader crosses a chapter. The badge is drawn first, because its top is the card's bound. |
| Sleep / nothing open | `SleepIdle.dc.html` | The badge alone. Same screen with its card removed. |
| Sleep / cover | `SleepCover.dc.html` | The cover full-bleed, and **the one screen that drops the badge**. `Grayscale`, decided per paint. |
| Sleep / cover + details | `SleepCoverDetails.dc.html` | The same cover with the reading card and the badge over it. Keeps both. Its golden pinned a **truncated** title for two phases. |
| Reader | `Reader.dc.html` | The only screen whose content is the BOOK's — but no longer the only `Fidelity::Grayscale` one. |
| Book error | `BookError.dc.html` | An overlay whose parent may be Home — **the only one whose parent is not a list**, so it is the only veil no board draws. THREE copy shapes: damaged, unreadable, and a book that is fine and did not fit. |
| Book end | `BookEnd.dc.html` | **The only screen a PAGE TURN opens rather than a press** — off the last page, so it must be reachable with no button bound to it. Its leaving slab's LABEL follows what is under the Reader; its ACTION does not. |
| Typography | `Typography.dc.html` | Two doors, and it needs nothing from the book. `CHANGE` cycles in place; `Font` is drawn and unreachable. |
| Reader / battery low | `LowBattery.dc.html` | A **variant**, not a screen: the same Reader with one 78px band drawn OVER the page. `columnH` is untouched, so no chapter re-paginates, and **any** button dismisses it. |
| Peek | `Peek.dc.html` | The only overlay over a `Grayscale` screen. Its column is NOT the reading column, which is why it shows no page number. |
| Battery empty | `BatteryEmpty.dc.html` | Painted and never pushed, on `Sleep`'s argument. The last thing on the glass before a critical shutdown, and what the resume gate leaves standing when it refuses. |
| SD missing | `SdMissing.dc.html` | RETRY restarts the device when the card was lost after a mount. |
| Wi-Fi | `WifiSettings.dc.html` | V1.1. The hub: saved networks, `AUTO`/`SAVED` toggled in place, and a HOLD for FORGET — the **second hold in the firmware**, the Library's being the first. Reached from Settings' `CONNECTIONS` row. |
| Wi-Fi / nothing saved | `WifiSettingsEmpty.dc.html` | A **variant**, not a screen, and its two movers go quiet: one focusable row means UP and DOWN would promise a press that changes nothing. |
| Join network | `WifiPicker.dc.html` | The **second scrolling list and the second user of the rail**. A scan in flight draws NO list and no count — that state is not boarded (drawStatusBar is specified by LibraryOpening and SleepWaking) and is pinned by a golden. |
| Password | `WifiPassword.dc.html` | The **first text entry in this firmware**, and **the screen is no longer in this file**: the keyboard is `TextEntryScreen`, extracted at the SECOND copy rather than the fifth (#126) — a caret, three layers whose union is all 95 printable ASCII, a 46-cell grid over `GridFocus`, the latching layer key that names where it TAKES you, and a Back that deletes before the caret and LEAVES on an empty field. What stays here is 802.11: the 63-byte bound, the **8-byte floor**, four strings and two Actions. **`minLength` DEFAULTS TO 0 AND THIS IS THE ONLY CALL SITE THAT SETS IT** — a floor blanks the Confirm slot, so carrying it to a caller whose field may legally be empty is a **dead button on a legal state**. A masked mode is NOT foreclosed and is NOT designed: `visibility` is the caller's string, and whether to mask wants a board. |
| Connecting | `WifiConnect.dc.html` | One state. It used to step to `READY`, which is gone: a successful join leaves for the saved list, and the list with the network in it is the confirmation — at one waveform instead of two. |
| Couldn't join | `WifiError.dc.html` | THREE copy shapes, BookError's argument: wrong password, not found, and didn't finish. `EDIT PASSWORD` is **absent** on the latter two rather than inert. Its slab count is the slab LIST, measured, not a second spelling. |
| Articles | `Articles.dc.html` | The wallabag list, and the **third scrolling list**. Its sync row is `-1` — Home's CONTINUE block one screen over — so it inverts and takes the Confirm hint (`SYNC`, against `READ` on an article). Its right-hand stamp is what the device **OWES** the server, `N TO PUSH`, and empty when it owes nothing. |
| Articles / not set up | `ArticlesSetup.dc.html` | A **variant**, not a screen, decided by the CREDENTIALS and never by the row count: an empty list and an unconfigured device are different things and only one of them names a file. |
| Article actions | `ArticleActions.dc.html` | An overlay, `ItemActions`' shape with one slot fewer. Its Facts are re-primed **every loop iteration** the list is on top — the hold pushes it directly, so there is no press for the shell to prime on. |
| Article end | `ArticleEnd.dc.html` | What an article's last page turns into, and the ONLY difference between reading a book and reading an article: `ReaderScreen::setEndScreen`. Every figure on it is a count of FILES on the card, never the server's. |
| Wallabag account | `WallabagAccount.dc.html` | Reached from Settings' `Wallabag` row. Six rows off the card; `Keep offline` cycles and prunes. **The factory holds a COPY of the settings and has to be told** or the row redraws with the old value. |
| Syncing | `WallabagConnecting.dc.html` + `WallabagFetching.dc.html` | ONE screen, two stages, differing by their **mark**: the radio while connecting, an arrow-into-tray while fetching. The two marks share one 60×46 box, because the panel's height derives from `mark.h` and it must not resize mid-sync. |
| Sync failed | `WallabagError.dc.html` | THREE copy shapes, `WifiError`'s argument one flow over: refused credentials, unreachable, no saved network. Only the middle one offers `TRY AGAIN`, being the only one that can fail spuriously. |
| Remove downloads | `ArticlesRemoveConfirm.dc.html` | `DeleteConfirm`'s shape, and `Restore::Never` where that one is Ready: it is built from the ACCOUNT screen's press and carries no row to be rebuilt from, so waking into "remove every article?" would be a destructive question nobody asked. |

**SETTINGS IS NINE ITEMS NOW — THREE SECTIONS AND SIX ROWS — AND NOTHING ON IT IS
INERT BY DEFAULT.** Its five inert TYPOGRAPHY rows became one disclosing
`Typography` row in a `READING` section once a screen existed to edit them (see
**The typography panel**), and a `SLEEP SCREEN` section then replaced the last row
that was drawn with nothing behind it: `Sleep screen` / `BOOK COVER` was **issue
#11**, and it is now `Shows` (COVER / COVER + DETAILS / DETAILS) and `Cover fit`
(FILL / WHOLE), two rows that act.

What survives of the paragraph below: the focus still skips what cannot act, an
inert row is still drawn exactly as an unfocused focusable one, and the theme still
reports a box model rather than a row count. What is gone: the five rows, **every
placeholder** — `SettingsScreen::Item::placeholder` was removed outright rather
than left with no writer, and a test asserts every drawn row either discloses a
screen or states a value from `settings_` — and the claim that no row here pushes a
screen.

**AND THE ONE REMAINING UNREACHABLE ROW IS DERIVED, NOT TABULATED.** `Cover fit`
is unreachable while `Shows` reads DETAILS, because a fit is meaningless with no
cover on the glass — `focusable()` asks `settings_`, which is Typography's own
precedent (`Font` is unreachable while one body face is vendored and becomes
reachable the moment a second lands, with no line to remember). The gate is
consulted per landing, so cycling `Shows` changes the answer with nothing to
invalidate.

**`kSettingsVersion` DID NOT MOVE**, for the third time and for the reason at the
top of `settings.h`: an added field takes its default from an older file, and both
defaults are today's behaviour. `CoverFit` comes from `core/include/reader/cover_fit.h`
— a leaf that includes nothing, written so `settings.h` can reach the enum without
including `imagefit.h`: measured, `settings.h` is **895 preprocessed lines**, the
include costs **9**, and `imagefit.h` would have cost **72,962** (it needs
`<vector>`), which is exactly the coupling `settings.h` already refuses at
`bodyPpem`.

**THE LAST-ROW-OF-A-SECTION RULE IS DEFENDED BY A PIXEL FOR THE FIRST TIME.**
`renderSettings` suppresses a row's `border-bottom` when the next item is a header,
and until this section existed the only section boundary on the screen was
`Typography` → `DEVICE` — where the row is FOCUSED, so `rowRuleFor` had already
suppressed it and the golden could not see the `nextIsHeader` term at all. Deleting
that term now fails both Settings goldens; before, it failed nothing.

**SETTINGS DREW EVERY BOARD ROW AND ONLY THE DEVICE ONES RESPONDED.** TYPOGRAPHY
belongs to Phase 3's reader; its five rows carry the board's own placeholder values
so the screen matches the board before the settings behind them exist. The `Size`
row is the one that will cost something to wire: see the glyph-cache table below,
because at 41px the shipped cache budget thrashes.

- **Focus SKIPS them.** A row that cannot be reached cannot mislead, where a row
  that focuses and then ignores CHANGE is the silent no-op this project has been
  bitten by twice. The cost is real and worth watching on glass: focus starts six
  rows down with five unreachable rows above it, and UP there does nothing.
- **An inert row is drawn EXACTLY as an unfocused focusable one.** No dimming —
  `SettingsRow::focusable` is about input, and a visual difference nobody designed
  is worse than none. `renderSettings` deliberately never reads that flag.
- **The theme reports Settings' BOX MODEL, not its row count.** Library's items are
  one height so a theme can answer "how many fit"; Settings interleaves 54px rows
  with taller section headers, so the answer depends on which items are headers —
  and the item table belongs to the screen. `settingsMetrics` hands over three
  heights and the screen counts, so neither side holds a copy of the other's data.
- **`SettingsSink::commit` applies AND persists**, in that order. The user has
  pressed a button and expects the device to behave differently; a card gone
  read-only must not also cost them the change until the next boot. **A refused
  write still shows the new value** — the change has taken effect in RAM, and
  reverting the display would make a read-only card look like a screen that ignores
  its buttons. The shell logs the failure; `core/` never learns why.
- **The factory holds a COPY of the settings**, because it is what constructs the
  screen. It has to be told when the struct changes, or closing Settings and
  reopening it shows the values from before the change — struct right, policy
  right, screen wrong.

**THREE RULES THE SETTINGS BOARD DRAWS AND A NAIVE RENDERER DOES NOT**, all three
found by diffing pixels rather than by looking:

1. The **first** section header has no rule — the header band's own 2px border is
   already there, and a second doubles it into a 4px slab. Positional, not by
   identity: at the top of the window the band is the separation, whichever section
   is scrolled there.
2. The **last row of a section** has none either; the next section's `border-top`
   is the line between them.
3. The **last drawn row** has none, which is `renderLibrary`'s rule verbatim.

Rule 2 was the expensive one: it also advanced `y`, so every row below the DEVICE
header sat a pixel low. **The symptom reported was a line at the top, and the line
at the top was the smaller of the two defects.**

**A FOCUSED MENU ROW KEEPS ITS `border-top`.** Invisible against the fill, and the
point: an unfocused row is 80px plus a 1px rule, so dropping the rule makes the
focused one 80px — and the menu's total height then depends on whether a row is
focused, stepping the whole block a pixel the moment focus enters it. Black on
black costs nothing and holds the pitch at `kRowH`.

**THE SLEEP SCREEN IS PAINTED NOW.** It was written, boarded, themed, simulated and
tested for two phases without ever reaching glass, because **its content did not
exist**: the board is the reading state, and painting it with no Reader would have
been demo fiction or an empty card. Progress persistence supplied the content and
`sleepNow` now renders it.

**IT IS PAINTED WITHOUT BEING PUSHED**, which was the trap recorded here for whoever
wired it and is now the reason `paintSleepScreen` bypasses `App` entirely. The session
record names the top of the stack, so pushing `SleepScreen` would make the next wake
RESTORE INTO IT — press power, get "asleep, hold power to wake" back. Bypassing `App`
moves two things it normally owns into that function: the **clear**, and
**`gFrameContentsUnknown`**, because `App`'s partial-repaint record now describes a
frame that no longer exists. Nothing reads it before the chip resets, but leaving a lie
there is a trap for the next person to paint something after it.

It costs one **FULL** waveform (~825 ms) on every sleep — full rather than fast because
this is the last thing the panel does for hours and a differential update would leave
the previous screen's residue under it. **WITH A COVER ON IT THAT IS NO LONGER THE
WHOLE BILL**: a cover makes the screen `Grayscale`, which is three waveforms, and a
cover the cache does not already hold costs a decode of seconds between two paints.
See **Covers**, which prices all three shapes; `[power] sleep cost` is the one line
that adds them up.

**ASLEEP WITH NOTHING OPEN IS ITS OWN BOARD** (`SleepIdle.dc.html`): the card *is* the
reading state, and the device sleeps from Home or the Library as often as from a book.
Painting the card with a blank title or a 0% bar would claim a book the user is not
reading; painting nothing at all is worse, because e-ink holds its last image and a
Library left on the glass gives no clue the device is asleep rather than frozen — which
is the entire reason this screen exists. So the **badge is the load-bearing half and it
stays**, drawn by the same tail in both states so the two cannot disagree about where
it sits; the card is the half with something to say only sometimes. **`SleepCover`
OVERRIDES THAT RULE AND IT IS THE ONLY THING THAT MAY** — see **Covers**, which
records why the override cannot be generalised. One screen with and
without its content, not two screens. `SleepViewModel::nothingToContinue` is spelled
exactly as `HomeViewModel`'s, because it is the same fact and one rule should have one
spelling.

**THE TITLE WRAPS NOW, AND IT USED TO ELIDE (#74).** Home's arc, Home's reason and
Home's mechanism — `wrapProseLead(..., WordBreak::Anywhere)` → `clampProse` →
`drawProse` — because an ellipsis on a *list row* hides only which of seven rows this
is, and here it hides the one fact the screen exists to state. What makes it worse
here than on Home: **this screen holds the glass for HOURS**, so a name cut short is
not a truncation the reader presses past, it is the one they live with.
`Sleep.dc.html`, `SleepWaking.dc.html` and `SleepCoverDetails.dc.html` all gained
`overflow-wrap: anywhere`; the other three sleep boards do not draw the card.

- **THE DEFECT WAS BLESSED INTO A GOLDEN, which is how long it had been there.**
  `test/golden/sleep_cover_details.png` read **`GULLIBLE'S T…`** — its fixture's
  title has never fitted the card — so the repo's own baseline pinned the truncation
  and every run was green. It reads `GULLIBLE'S / TRAVELS` now.
- **THE BADGE IS WHAT BOUNDS THE CARD, AND THE RESERVE IS TAKEN TWICE.** The thing a
  growing card collides with is not the edge of the glass, it is the badge — an
  overrunning title runs UNDER an opaque white box and is hidden by it, an ellipsis
  by another name. And the card is CENTRED, so `centreIn` splits the slack evenly and
  reserving the badge *once* still leaves a tall card hanging half a badge into it:
  the same arithmetic `renderDeleteConfirm` and `renderBookError` each shipped wrong.
  It comes from **`drawBadge`'s own returned top**, not from a second copy of its
  private 34px and note-face line box — deriving a shared edge twice is how the
  header band ended up 6px out. **That is why the badge is now drawn BEFORE the
  card**, and the reorder is pixel-neutral: `sleep_idle` and `sleep_cover` are
  byte-identical across it.
- **THE TITLE'S LINE BOX IS THE BOARD'S `1.1`, NOT THE FACE'S 53px.** `Title700`'s
  own `lineHeight()` is 53 at ppem 42 and the board says 46, and the single line this
  screen used to draw took the face's. A wrap has to be *handed* a lead, so there was
  no way to leave the question unanswered — and `BookDetails.dc.html` states the
  identical `--t-title` at `line-height: 1.1` and already resolves it to 46, so
  `kSleepTitleLineH` is a **documented second copy** of `kDetailsTitleLineH` rather
  than a new number.
- **The card's height is still a RESULT**, now a sum whose title term is the wrap's
  own height. The budget derives to **8 lines on both panels** — X4 `800 − 2×79 =
  642` and X3 `792 − 2×79 = 634`, less the 253px of card that is not the title, over
  46 — where 79 is the badge's 34px offset plus its 45px box.

**MEASURED AGAINST ITS BOARD: 3.28% → 2.42% (X4) and 3.01% → 2.22% (X3)**, and the
board's own render is **byte-identical** before and after, so the whole gain is the
firmware moving toward an unchanged board (honouring the 1.1 closed most of the 7px
card-height gap). `sleep_cover_details` went **3.48% → 2.86%** and **3.18% → 2.62%**.
**THIS LINE USED TO SAY `0.27%`, "the closest panel on the sheet", AND THAT FIGURE IS
NOT REPRODUCIBLE** — a threshold-at-128 count over the `--export` panels puts the
pre-change screen at 3.28%/3.01%, and the same instrument reproduces this file's
recorded `sleep_cover_details` pair (3.48%/3.18%) **to the digit**, which is what says
the instrument is the one this file uses elsewhere and the 0.27% is the outlier. The
sheet still prints `ok` rather than a percentage (#41), so any figure here is a count
someone ran by hand: **quote the method with the number.**

**THE AUTHOR WRAPS TOO, TO TWO LINES, AND IT USED TO DO NEITHER — so a long name left
the card entirely.** `drawCentredText` places a run at `centreIn(0, contentW, w)`, and
`centreIn` returns a **NEGATIVE** half when the run is wider than the box: the name began
left of the card's padding, painted over both 2px borders onto the dither field, and was
clipped by the panel edge. "Fyodor Mikhailovich Dostoevsky" is 540px against a 312px
column and rendered as `ODOR MIKHAILOVICH DOSTOEVS` — cut at BOTH ends with no ellipsis
to say so, sitting on the frame. **Every golden passed, because every golden's author was
short.**

- **THE CAP IS TWO AND IT WAS MEASURED, not chosen.** Every `dc:creator` in the 225-book
  corpus, shouted, at `Label400`/0.22em against this column: **68 of 221 (30.8%) overflow
  one line**, so an ellipsis here is the COMMON case and not the edge one — and of those
  68, **58 (85%) fit WHOLE in two**. Only **five DISTINCT names** in the corpus need a
  third and three of the five are corporate, so **216 of 221 render complete**. **None of
  the 16 books on the user's own shelf overflow at all**, the INVERSE of the
  progressive-JPEG split, which is why the figure is quoted with its sources rather than
  as one number.
- **IT IS A LINE COUNT WHERE THE TITLE'S BOUND IS THE CARD'S ROOM, and the two being
  different KINDS is the design.** Both runs can grow now, and **one budget cannot serve
  two growable runs without saying which yields**. The author yields, FIRST and by a fixed
  amount, so the title — the one fact this screen exists to state — keeps every line left
  over. It is expressed as the ORDER of two statements in `renderSleep` rather than as a
  comment, so it cannot drift from what is drawn. A proportional split would let a
  three-line corporate name eat the hero.
- **AT THE BOUND THE TITLE PAYS, AND THE FIRST TEST OF IT ASSERTED THE OPPOSITE AND
  FAILED AT −17px.** That figure is the arithmetic being right: 29px of author bought
  against a 46px title line the budget then gave back. **A card at its bound cannot
  grow**, so measuring growth there measures the floor in the title's division instead —
  the cap is asserted where there IS slack and the ordering where there is not.
- **THE AUTHOR'S LEAD IS THE FACE'S OWN LINE HEIGHT**, not one of this screen's numbers,
  because the board leaves this run at `line-height: normal` — which is what
  `wrapProseLead` is for, and what makes a one-line author **byte-identical** to the
  `drawCentredText` it replaces. **No golden was re-blessed**: the eight existing sleep
  goldens are untouched and the two `sleep_long_author` files are the only ones added.
  Board fidelity did not move either — **2.42%/2.22%, 9301 differing pixels at both
  geometries**, and `sleep_cover_details` 2.86%/2.62%.
- **THE TEST THIS NEEDED WATCHES THE CARD'S PADDING, NOT THE PANEL EDGE.** The card is
  opaque white and only ever drawn in its content column, so the 42px band between each
  border and that column is **paper by construction** and ink there means a run escaped.
  The edge is where the damage ENDED — a test watching only the edge passes for a name
  that merely eats the frame. Restoring `drawCentredText` fails it with 385–435 stray
  pixels per geometry.
- **CHROME AND THE FIRMWARE DISAGREE ABOUT THIS RUN, which is why the corpus figure is
  the firmware's.** Chrome fits `MARY WOLLSTONECRAFT` in the column and the firmware does
  not, so that name is two lines on the board and **three** on the device. The boards'
  committed specimen is one line in both, so nothing on the sheet moves — but a
  long-author *specimen* would land differently in the two engines, the same ~3% the
  `.rfnt` faces measure wider everywhere else.

**THE CARD'S LAST LINE NAMES THE CHAPTER NOW, AND IT WAS THE LAST PLACE ON THE DEVICE
SHOWING A SPINE POSITION.** It read `6% · CH. 01` — a percentage and `last.spine + 1`,
composed in the shell — and this entry twice recorded a reason for that which had
expired: first that a chapter name "would need a table of contents, which is not
built", false since `Contents` shipped, and then that the run needed a bound and a
design decision, which is what this change made. **It was never Home's false claim**:
`CH. 01` quotes no total, so it invited no arithmetic and was merely *less
informative* than a name. So this was an improvement rather than a fix, and the
constraint was that it must not be bought with a regression.

**THE RUN IS TWO RUNS, BECAUSE A CHAPTER NAME DOES NOT FIT AND DOES NOT NEARLY FIT.**
Measured over 225 real books — 205 with a usable NCX, **8,617 chapter labels**, at the
shipped `Role::Label500` against the card's **312px** content column, which is 312 on
*both* panels because `max-width: 400` is under the X4's `480 - 2·kMargin` as well.
`tools/sleep_chapter_probe.cpp` is that measurement, tracked and re-runnable for
`name_probe`'s reason:

| the name gets | elides |
|---|--:|
| the row, beside `100% · ` (213px) | **52.51%** |
| the row, beside `6% · ` (246px) | 47.82% |
| its own line, 312px at 0.14em | 37.60% |
| **its own line, 312px at 0.10em** | **34.54%** |
| its own line, 0.10em, *shouted* | 36.73% |
| *Home's control*, `Meta400`/0.10em at 304 / 352px | 30.70% / 23.69% |

p50 **217px**, p90 536px, max 1887px, and only **33 of the 205** books have every
label fitting. So the median label overflows the combined run, and sharing that row
would show a cut name **more often than a whole one** — where its own line lands in
the band this project already accepted for the identical string.

**IT WRAPS TO TWO LINES, AND IT SHIPPED ELIDING ON ONE — THIS IS AN OWNER'S DECISION
AND NOT A NEW READING OF THE DATA.** The distribution permits either. What decides it
is the screen: the card holds the glass for **hours**, longer than anything else the
device draws, so a cut name is one the reader *lives with* rather than one they press
past. That is the argument that made the title wrap (#74) and then the author (#86),
arriving at the last run on the card.

**TWO LINES, AND THE DATA STOPS THERE.** Of the 8,617 labels, **2,976 (34.54%)**
overflow one line; of those 2,976, **2,274 (76.41%) fit two lines WHOLE** and 702
(23.59%) need three or more. So two lines elide **702 of 8,617 (8.15%)** where one line
elides 2,976 — a **4.2× reduction in cut names**, which is what earns the second line
and is the same shape of knee the author's cap was earned by (85% there). **A third
line was permitted by the owner and is not taken**: a smaller marginal gain bought out
of another of the title's lines. **Elision is moved, not removed** — `clampProse` cuts
the SECOND line for that 8.15%, so this is a change of degree.

**THE TWO LINES ARE RESERVED IN THE TITLE'S BUDGET WHETHER OR NOT THE NAME USES THEM,
AND THE CARD'S HEIGHT IS WHAT THE NAME ACTUALLY TOOK.** Those are **two quantities and
nothing requires them to be one number**, which is the whole of how a growable run is
safe here — and the first shape of this shipped them as one, so the reserve had to be
the height too.

- **`chapterReserveH` IS THE BUDGET'S**, and the objection it answers is real and is not
  answered by the rate: **A CHAPTER CHANGES WHILE THE BOOK IS BEING READ AND AN AUTHOR
  DOES NOT.** The title takes what the card's room leaves, so a budget counting
  this run's second line only when the name *used* it would make the **title's** budget
  depend on where the reader is standing — cross a chapter boundary and the book's name
  reflows, or newly acquires an ellipsis, *because a page was turned*. A visible defect
  with a baffling cause. Reserved either way, the title's budget is a **constant** and
  the card's LAYOUT is a function of the **book alone**, exactly as it was at one line.
  It is `kSleepGap + kSleepChapterMaxLines * lineHeight()` and it never reads the wrap.
- **`chapterH` IS THE CARD'S, and it is the wrap's own height** —
  `kSleepGap + f26ToPx(chapterProse.heightF26())`, `authorH`'s shape one run down. The
  reserve bought **nothing** in the height: this is the card's LAST run, so nothing
  below it steps up, and a one-line name left **29px of empty box** standing at the foot
  of a card that holds the glass for **hours**. Dead space, not spacing — and the common
  case, at 65.46% of corpus labels and every fixture's own specimen. The wrap is
  therefore **hoisted above `cardH`** rather than built at draw time; that placement is
  the mechanism, and building it at the draw is what made the reserve the only number
  the height could have.
- **THE BOUND HOLDS A FORTIORI**, which is worth saying rather than leaving implied:
  `cardH` can only be **shorter** than the room the budget was divided against, never
  taller, so `cardH <= cardRoom` is satisfied with 29px to spare on a one-line name. The
  card is CENTRED, so what a shorter card does is **re-centre** — no type moves relative
  to any other type.
- **DO NOT MAKE THEM ONE EXPRESSION AGAIN, IN EITHER DIRECTION.** They will read as one
  thing spelled twice. Giving the HEIGHT the reserve puts the dead space back; giving the
  BUDGET the actual reintroduces the reflow. Both sites say which quantity they hold and
  name the other, `design/Sleep.dc.html` says the same thing where its `min-height: 58px`
  used to be, and **each swap has its own test** — see the mutations below, which are the
  whole specification of the split.
- **The name is drawn at the TOP of the room `chapterH` gave it**, which for a one-line
  name is exactly one line: there is no slack under it any more. A short name and a
  two-line name still share a first baseline *relative to the run above them*; what a
  chapter crossing moves is the whole **card**, by 29px, re-centred.

**AND THE TITLE IS THEREFORE CONSERVATIVE BY UP TO ONE LINE, which is the stated cost of
the trade and is measured rather than argued.** A one-line chapter buys the title
nothing, so the only title that can notice is one needing exactly `budget + 1` lines —
and `sleep_chapter_probe` says that is **3 of 225 (1.33%)**: `The Fables of Aesop`, `The
Declaration of Independence of the United States of America` and `The Hacker Crackdown`.
**All three have one-line labels and none has only one-line labels** (39 of 90, 2 of 6,
4 of 23), so in those three books the seventh title line is given up in **45 of their 119
chapters** and kept in the other 74. Every other corpus title either fits its six lines
or would elide at seven as well. The probe answers this jointly — a title's line count
*and* that same book's labels — because the tail alone cannot: read it off
`the conservative budget's cost` at the end of a run.

**THE YIELD ORDER IS THREE-STAGE AND IS STILL THE ORDER OF THE STATEMENTS.** The
chapter yields first and absolutely (two lines *of budget*, always), the author second
by its fixed cap of two, and the title takes every line left over. **This entry used to
add that `chapterH` was "one expression spent twice", on `renderBookError`'s
two-copies-free-to-disagree argument, and that was the defect rather than the safeguard**
— the two spends want different numbers, and collapsing them is what put the dead space
in the card. What they really share is `cardFixedH`, which is one expression for that
reason. The claim that "a mutation that reserves it in the height only is caught by the
*pre-existing* badge-bound tests" was also **false in the direction that matters**: the
card gets SHORTER, so nothing overruns the badge and no bound test can see it.

- **WHAT IT COSTS: the title's derived budget goes 8 lines → 6 on both panels** — X4
  642px of card room and X3 634px, less the 325px that is not the title when the author
  takes one line, over the title's own 46px line box. **9 of the 225 corpus titles
  (4.00%) elide at 6**, against **6 at a budget of 7** and **4 at 8** — so the second
  reserved line costs **3** titles and the whole chapter run costs **5**. Read those off
  the tail `sleep_chapter_probe` prints rather than from here; it printed only the
  8-line answer until this change needed the 6-line one, which is the shape of a
  hardcoded figure in an instrument.

**AND ONE ASSERTION ON THIS CARD WAS MEASURING `mod 46` RATHER THAN THE DESIGN, WHICH
IS WORTH KNOWING BEFORE TRUSTING ANY HEIGHT COMPARISON HERE.** *"At the bound the title
pays"* was asserted as *a two-line author's card is no taller than a one-line author's*,
and that is not a property of the yield order at all. The card's height is
`F + A + C + 46·floor((cardRoom − F − A − C)/46)`, which **collapses to
`cardRoom − (budget mod 46)`** — so whether the author's second line costs the title a
line depends *only* on whether the one-line remainder reaches the 29px that line takes:

| | one-line remainder | a two-line author then |
|---|--:|---|
| with a one-line chapter | 24 (X4) / 16 (X3) | forces a title line back, card **−17px** |
| with a two-line chapter | 41 (X4) / 33 (X3) | fits the slack, card **+29px** |

**Both are correct renders** — 12px (X4) and 4px (X3) inside the bound and clear of the
badge — and the second is the *better* one, because the title keeps all six of its lines
instead of dropping to six from seven. **That assertion has now been wrong in both
directions**: its first version asserted the card GREW and failed at −17px, and the
version that replaced it asserted the card SHRANK and failed at +29px. A property that
flips sign when a neighbouring run takes one more line was an artefact both times. It is
replaced by the remainder-independent statement — **the card never leaves a whole title
line box unused** — and the order itself is asserted where it is observable, by the cap
costing exactly one extra line box where there IS slack.

**AND THAT STATEMENT NEEDED THE CHAPTER'S UNSPENT RESERVE ADDED BACK ONCE THE HEIGHT
STOPPED TAKING IT.** With a one-line chapter the card is 29px shorter than the division
it was budgeted by, so the bare form reads that 29px as room the hero was shortchanged
out of and fails at **70px (X4) / 62px (X3)** — measuring the *conservative budget*,
which is a deliberate trade with its own figures above, rather than the division the
assertion is about. `chapterSlackOf` is an **exact term and not a tolerance**: what is
compared is still the budget's own remainder, still required under one whole title line.
**The table's two rows are now the ONE-LINE and TWO-LINE AUTHOR against a reserve that is
always two**, so the reachable remainder is 41px (X4) / 33px (X3) with a one-line author
and 12px / 4px with a two-line one — and that pair is load-bearing for a *test* now: a
budget change is only visible where the remainder is at least `46 − 29 = 17px`, so the
two-line-author fixture cannot see it and the one-line-author one can.
- **`SleepViewModel::progress` IS GONE RATHER THAN RENAMED.** It held the whole
  composed string, so the figure under the bar and the length of the bar were two
  spellings of one fact that the shell could set independently. The theme composes the
  percentage from `progressPercent` — the field `drawProgressBar` already takes —
  exactly as `renderHome` does, and `chapter` carries `last.chapter`, the string the
  Reader's band, Contents' `NOW` row and Home's meta line all draw.
- **AN EMPTY CHAPTER COSTS NO LINE AT ALL** — not two blank ones — unlike Home's,
  which reserves its line because runs sit below it. This is the card's *last* run, so
  nothing below it steps up and an absent chapter simply shortens the card. An absent
  claim beats a false one: a pointer written before `last.json` carried a chapter must
  not fall back to the position this run has just stopped showing.
- **0.10em ON THE NAME AND 0.14em ON THE PERCENTAGE**, which is Home's split and its
  reason (0.14em is a *counter's* tracking and a name is not a counter) and is 3.06
  points narrower on the one run whose width is the whole problem. **Not shouted**,
  although the title and author on this card are: three screens already name this
  string as authored, `toc.h` hands it over "as authored" for that reason, and shouting
  measures **2.19 points wider**.
- **WEIGHT 500, WHERE THE BOARD SAID 700.** The ramp carries `Label400`/`Label500` at
  11pt and **no `Label700`**, so 700 asked for an asset nobody has — `LowBattery`'s
  band label's defect exactly — and the firmware has always drawn `Label500` here.

**AND THE NAME MAY NOT GO THROUGH `drawCentredText`, which is what this run did while
it was a position.** That function places a run at `centreIn(0, contentW, w)` and
`centreIn` returns a **negative** half for a run wider than its box: an unbounded name
begins left of the card's padding, paints over both 2px borders onto the dither field,
and is clipped by the panel edge with no ellipsis to say so — the defect the AUTHOR
line one run above was fixed for, and at 34.54% it would have been the **common case**.

**THE WRAP IS WHAT KEEPS IT IN THE COLUMN NOW, and `clampProse` is the last resort
rather than the mechanism.** `WordBreak::Anywhere` means every line the wrap emits is at
most `contentW` wide, so `centreIn`'s half cannot go negative for any of them — and that
holds for a name with no space in it, which is what `Anywhere` is for. The clamp then
cuts the second line for the 8.15% that need a third.

**THE TEST WATCHES THE CARD'S PADDING, AND ON THIS SCREEN THAT IS THE ONLY PLACE INK IS
EVIDENCE AT ALL.** The 42px band between each border and the content column is paper by
construction. The two obvious alternatives were *tried against the mutation and both
are blind*: the panel EDGE is inked on every row by the dither field, and the card's own
side BORDERS are inked on every row of the card, so neither a full-row scan nor an
in-card extent separates the run's ink from furniture that belongs there.

**IT IS PROVED BY THREE MUTATIONS AND IT BITES ON ALL OF THEM.** Restoring
`drawCentredText` on the raw name — the original defect — fails on **all four** of its
specimens at both geometries, with **508, 440, 723 and 742** stray pixels; dropping
`WordBreak::Anywhere` to `Normal` fails on the two unbreakable-token names with **723
and 742**, which is the case a real label with spaces cannot reach. Its four names are
two real labels either side of the two-line cap plus a 120-byte token and FAT's 255-byte
maximum, so the wrap and the clamp are both covered and nothing depends on where a space
happens to fall.

**AND THE RESERVE/ACTUAL SPLIT IS PROVED BY TWO MORE, ONE PER DIRECTION, EACH FAILING A
TEST THE OTHER DOES NOT.** Those two mutations *are* the specification: if either passes,
the distinction is not enforced.

| swap | fails |
|---|---|
| `cardH` takes `chapterReserveH` | **14** assertions over 6 cases — **4** in `the card's HEIGHT follows the chapter's actual wrap` plus the **10** sleep goldens. Budget test: **0**. |
| `maxTitleLines` takes `chapterH` | **4** assertions over **1** case, all in `the TITLE's budget does NOT follow it`, reporting the bar moved **46px** — one whole title line — between a one-line chapter and a two-line one. Height test: **0**, **and nothing else in the suite moves, goldens included.** |

- **THE BUDGET SWAP IS INVISIBLE TO EVERY GOLDEN**, which is why it needed a test of its
  own rather than a re-render: every golden's title fits inside its budget's slack, so an
  extra line of budget changes nothing that is drawn. **1,325,843 assertions stayed green
  under it** before the test existed.
- **AND THE BUDGET TEST'S FIRST FIXTURE COULD NOT SEE IT EITHER** — the recorded lesson
  again, that *a mutation tells you about your INPUT before it tells you about your
  test*. It copied `THE AUTHOR IS CAPPED AT TWO LINES`', which maximises the **author**
  too, and a two-line author leaves a remainder of 12px / 4px: 29px more budget still
  floors to the same six lines. With the board's one-line author the remainder is
  41px / 33px and the crossing happens. **Both conditions are asserted now** — the title
  fills its budget, and the remainder is at least `46 − 29` — so the test says so instead
  of going quiet.
- **A FIXTURE GUARD HAS TO BE BLIND TO THE DEFECT IT GUARDS.** Those two `REQUIRE`s are
  measured on the **two-line** chapter render, where the reserve is exactly what the name
  takes and both spellings of the budget give the same card. Taken from the one-line
  render the mutation moves them itself, the remainder goes to **−5px**, and the guard
  fires with *"this fixture cannot see the defect"* about a fixture that sees it
  perfectly well — **a guard that accuses the fixture when the code is wrong is worse
  than no guard.**
- **THE HEIGHT TEST HAD TO BE BLIND TO THE BUDGET AND VICE VERSA**, and the two are made
  so differently. The height test uses the board's **one-line title**, so the division
  above it has slack and cannot change what is drawn however the chapter is counted into
  it. The budget test cannot use the card's height at all — the height is *supposed* to
  move there — so it reads the title's line count off the frame as
  **`barTopOf − cardBox().top`**, the bar's distance below the card's own top, which is
  every fixed term plus the author's height plus the title's with the author held still.

**THE MIDDLE DOT'S TRAP OUTLIVED THE LITERAL THAT CARRIED IT, and the duplicated note
was the tell.** `"%d%%\xC2\xB7CH. %02d"` parses `\xB7C` as ONE hex escape, because a C++
hex escape is **unbounded**: clang rejects it outright and the ESP32's GCC **accepted
it** and emitted a byte that is not U+00B7 (this file records the same shape for
`"\xA0b"`, which is 0xA0B). Adjacent literals end the escape. That `snprintf` is gone
with the composition, and **the note describing it stood in `main.cpp` twice, verbatim**
— which is this file's own rule arriving: when a replacement leaves a condition stated
twice, one of them is stale. The trap is real and now lives only where the middot does,
`screens.cpp`'s `kDot` and the badge's own literals, where the bytes are their own
adjacent literal by construction.

**MEASURED AGAINST THE BOARDS: `sleep` 2.32%/2.13% → 2.30%/2.11%, `sleep_waking`
2.16%/1.99% → 2.15%/1.97%, `sleep_cover_details` 2.74%/2.50% → 2.87%/2.63%.** Two of
the three ended up *better* than before the run wrapped and the third is 0.13pp worse.
Threshold-at-128 counts over the bare `--export` panels, since the sheet still prints
`ok` (#41).

**THE WRAP ALONE COST 1,500 PIXELS A BOARD, AND THE CAUSE WAS THE TITLE'S `1.1`.** With
the wrap in and the title still declaring `line-height: 1.1`, `sleep` went to
**2.71%/2.49%** — and the mechanism is worth keeping because it will catch the next
person who changes a run's height on a centred card:

- **Chrome resolves `1.1` on `--t-title` to a FRACTIONAL 46.2px** where the firmware
  derives `round(1.1 * 42) = 46`. That was the only fraction on this card. It made the
  card's own height fractional, and the card is **centred**, so `centreIn` halved the
  fraction and the runs below the title painted at positions the firmware does not
  compute.
- **It cost nothing until the card changed PARITY.** The reserved second line is 29px —
  **odd** — so it flipped the half pixel the centring had been absorbing, and the
  author, the bar, the percentage and the chapter all went from 0–2px out of register
  to 1–3px. Chrome's painted card grew **+30px** where the firmware's grew +29.
- **IT WAS A REAL REGISTER REGRESSION AND NOT THE `Names` CUT'S COUNTING ARTEFACT, and
  this file's own discriminator is what settled it.** A **±1-row-tolerant** count did
  **not** absorb the rise (5,456 → 6,988), and the two card borders sat **beside** each
  other two pixels apart rather than the design's **straddling** the firmware's. Both
  tests point the same way, which is what makes the verdict safe.
- **The fix was to state the number, not to swap instruments**: the three boards say
  `line-height: 46px`, which is what `1.1` was chosen to mean. **No golden moved** —
  `kSleepTitleLineH` was already 46, so only the board's render moved, toward the
  firmware. Same lesson as the chapter run's own `normal`, and `Peek.dc.html`'s panel
  height.
- **The obvious symmetry is WRONG and was measured rather than assumed**: giving the
  AUTHOR run `line-height: 29px` too makes the screen **worse** (2.45%/2.25% against
  2.30%/2.11%), so Chrome's `normal` there is not the same disagreement. Left alone.
- **`sleep_cover_details`'s residual 0.13pp is the stated cost and is inherent.** Its
  card sits over a **four-level dithered cover** rather than the plain field, so the
  1px the card's painted extent still differs by mismatches cover pixels along both
  boundary bands, where the same offset over the field costs far less. Growing a centred
  card by an ODD number of pixels over a dithered photograph is what the design decision
  buys; there is no even-sized line box to grow it by.
- **AND THE PARITY MOVED ONCE MORE, WHEN THE HEIGHT STOPPED TAKING THE RESERVE — the
  same odd 29px, in the other direction, and it did NOT redden the register.** The board
  dropped `min-height: 58px` and Chrome's card shrank by exactly the 29px the firmware's
  did, so the design's card top sits **1px above** the firmware's before *and* after,
  measured. What the strict count does is what this bullet's own lesson predicts:
  `sleep` **2.30%/2.11% → 2.32%/2.13%** (+64 px, +0.02pp) while the **±1-row-tolerant**
  count *falls* 4,326 → 4,152, and `sleep_waking` the same +64/−174. So the rise is the
  half-pixel phase and not drift — **both discriminators point the same way**, which is
  what makes that verdict safe here as it did for the `Names` cut. `sleep_cover_details`
  improves on **both** counts (2.87%/2.63% → **2.74%/2.50%**, −516 strict, −437
  tolerant), which is this bullet above being paid back: the card it centres over that
  dithered cover is 29px smaller. `sleep_cover` stays **0.00%** and `sleep_idle`
  **0.26%/0.24%** to the pixel — neither draws the card.

- **IT READ AS A REGRESSION FIRST, AND IT WAS NOT THE PHASE ARTEFACT IT LOOKED LIKE.**
  With the chapter run's line box left at `line-height: normal` the Sleep board went to
  **3.03%/2.78%** — worse than before the change — because Chrome resolves `normal` to
  **30px** where the face's own line height at this ppem, which the firmware derives, is
  **29**. The card is CENTRED, so one pixel of height is half a pixel at both ends and
  every rule inside it goes out of register.
- **THE DISCRIMINATOR IS THIS FILE'S OWN, AND IT ANSWERED THE OTHER WAY THIS TIME.**
  The `Names` cut records a rise in this same count caused by a half-pixel phase flip,
  where Chrome rasterises a rule across two rows of grey 127 and the count scores both
  as ink; the test given there is whether the design's rule **straddles** the firmware's
  or sits **beside** it. Here both borders were **solid black two pixels apart**, so it
  was a real geometry disagreement — and the fix was to state the number, not to swap
  instruments. The three boards declare `line-height: 29px`, the card's borders go from
  2px out of register to 1px, and the rows differing over half the panel go **8 → 4**.
  This is the Sleep TITLE's own lesson one run lower (its `1.1` exists because the face
  would have given 53 where the board wanted 46) and `Peek.dc.html`'s, where stating the
  panel's own height "closed a disagreement rather than documenting it".

**FOURTEEN GOLDENS TOUCHED — TWELVE RE-BLESSED AND TWO ADDED — WITH THE EVIDENCE PER
PIXEL.** Every differing pixel sits inside the card's own 400px column — x 40..439 on
the X4 and 64..463 on the X3, **not one pixel outside** — so the dither field, the badge
and (on `sleep_cover_details`) the four-level cover are untouched outside it.
`sleep_idle` and `sleep_cover` produced **no candidate at all**, which is what says the
badge-only and cover-only screens were unaffected.

**AND ON THE TEN WHOSE CHAPTER IS THE ONE-LINE SPECIMEN THE PROOF IS EXACT: the card's
content is BIT-IDENTICAL with 29 blank rows of interior inserted above the bottom
border.** Longest common prefix plus suffix covers **342 of 342** card rows on `sleep`
and `sleep_waking`, 434 of 434 on `sleep_long_title`, 371 of 371 on `sleep_long_author`
and 388 of 388 on `sleep_cover_details` — so nothing was redrawn, restyled or moved
*relative to the card*: one band of paper was pushed in at the foot, the card grew by
it, and being centred it then moved up by half of it (−14px, or −15 where the leftover
is odd). That is a stronger statement than "the difference is confined", and it is the
one worth reaching for when a change should be a pure insert.

- **`sleep_long_chapter` IS THE WRAPPING SPECIMEN NOW, WHERE IT WAS THE ELIDING ONE.**
  `PREMIÈRE PARTIE : À LIRE AVANT L'ACHAT` is 526px against a 312px column — 1.7 lines
  — so it stopped exercising the ellipsis the moment the wrap landed, exactly as the
  author's specimen would have if it had ever come in under two. It renders the whole
  name on two centred lines, broken at the space, and it carries the **accented capital
  path through a WRAP**, which is the case this file flags: the grave sits close to cap
  height, and there is clear paper between it and the cap-tops of the line below.
- **`sleep_elided_chapter` IS THE NEW PAIR, AND `chapterTail` IS WHY IT EARNS ONE.**
  That local exists only on the clamping path — `clampProse`'s elided line is a NEW
  string, not a view into the name — and Home shipped precisely that mistake inline
  once and drew a column of **notdef boxes**: right for a short string and wrong for a
  cut one, which is silently right in exactly the case every other fixture covers. Ink
  that spells nothing inks rows exactly like ink that does, so no geometric assertion
  can see it. **Proved by mutation: removing the clamp is caught by this golden and by
  NOTHING else in the suite** — the padding test cannot see it, because the wrap keeps
  every line inside the column and the overflow goes downward.
  Its specimen is a real Verne heading off the probe's own widest-per-book list,
  1887px, in capitals because the book authored it so and stopping mid-word at `SEEI`
  because it is 128 bytes — `toc.h`'s `kMaxTocLabelBytes` doing its job, so the fixture
  is what the device would actually put on the glass.
- **A guard asserts one specimen wraps whole and the other is cut** (`== 1`, `== 2`,
  `>= 3`), because this run now has TWO boundaries and a fixture either side, so there
  are two ways to go quiet — and neither shows up as a failure anywhere else: both
  goldens would simply be re-blessed onto a shorter render. Shortening the eliding
  fixture fails it with `2 >= 3`.

**IT TAKES NO INPUT AND DRAWS NO HINT BAR**, and neither is an
omission: the shell paints it and then calls deep sleep, so there is nobody left to
press anything, and the bar is a contract about four buttons that do nothing.
`onEvent` answers `none()` even for Back. It exists because e-ink holds its last
image with no power — leaving the previous screen there shows a Library or a
half-read page and gives no clue the device is asleep rather than frozen.

**TWO BOARDS ASKED FOR TYPE THAT IS NOT IN THE RAMP** — Sleep's title at 53px and
HomeEmpty's at 39px — and both now use `--t-title` (42px). A role is a pre-rendered
asset per size AND weight, 15–20 KB of flash each, and these were the only boards
that wanted those sizes. Sleep's soft hyphen went with it: MIDDLEMARCH at 53px had
to break, and at 42px it fits. **If either reads too quiet on glass the fix is a new
role plus a wrap that honours a soft hyphen** — real work, and not worth it until
the panel says so.

**`kBookLarge` is a second asset for the same drawing**, at 112px against `kBook`'s
25px, because these are pre-rendered bitmaps and there is no scaling one up. 3,136
bytes. `iconc.py` disambiguates them by `source` board, since both boards carry the
identical path data.

**THE INPUT MONITOR IS GONE**, deleted with `StubScreen` in 2C-3. It was reachable
only from the stub's first row and no board ever listed it. **What was given up: press
classification is now verified only by `test_input.cpp` on the desktop**, and
`shell/` is where four bugs have hidden. If held-scroll or press classification
needs eyes on glass again, it comes back as a board row, not a hidden gesture.
