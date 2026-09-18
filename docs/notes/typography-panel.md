# The typography panel

Extracted from `CLAUDE.md`, which keeps a stub under this heading and is where
the cross-references to it point. Same standing as anything in that file.

`Typography.dc.html`. Five rows over four `Settings` fields — `bodyPpem`,
`margins`, `lineSpacing`, `justify` — a live specimen, and a re-pagination of the
open chapter on the way out. **`kSettingsVersion` did NOT move**: an added field
takes its default from an older file, and every default here is the pre-feature
behaviour to the pixel, which is what keeps every reader golden where it is.

**ONE MODE, AND `CHANGE` CYCLES IN PLACE.** Up/Down move the focus, Confirm cycles
the focused value forward and wraps, Back pops. A TWO-MODE design was built first
and rejected off the rendered board: it read `DONE / EDIT / UP / DOWN` browsing and
`DONE / OK / UP / DOWN` editing, and **`DONE` and `OK` are synonyms** — two words
for "finished", nothing to say that one finished the ROW and the other left the
SCREEN. Cycling in place is Settings' own mechanism and its argument transfers
unchanged; what it costs is one direction, at most four presses over a five-value
list. Deleting the mode also deleted a second board, two view-model fields, and a
`‹ ›` marker whose axis contradicted the vertical buttons that stepped it.

**FOCUSABILITY IS DERIVED, NOT TABULATED**: a row is focusable iff its field has
more than one value. So `Font` is unreachable while one body face is vendored and
becomes reachable the moment a second lands, with no line to remember — which is
what replaced the chevron affordance that used to make a one-value row honest.

**THE VALUES AND THE FOCUS BOTH WRAP, and this screen is where that is free.** The
recorded hazard was never the wrap; it is AUTO-REPEAT — "a wrap belongs to a press
and a hold rests at the end". This screen declares no repeat, and must not: every
size step re-rasterises the body face.

**TWO ENTRY POINTS, and the panel needs nothing from the book.** The reader menu's
`Typography` row, and Settings' `READING` section — one disclosing row where five
inert readout rows used to be. Settings could become a door only because the panel
stopped needing an open book: its band names no book (the settings are device-wide,
so naming one contradicted the footnote) and its specimen is fixed.

**THE BAND'S RIGHT SLOT IS EMPTY AND STILL RESERVED ON THE BOARD.** Removing the
div outright shrank Chrome's band by 2px, because Chrome sizes a flex row by its
children while `bandContentH` takes `max(Label500, Value700)` unconditionally — and
a 2px band pushes every row below it out of alignment. **A band's height must not
vary by screen**, for the same reason the hint bar is always one line, so the board
holds the line box with an `&nbsp;`.

**SETTINGS' CONFIRM HINT FOLLOWS THE FOCUSED ROW** — `OPEN` on the `Typography`
row, `CHANGE` on the four `DEVICE` rows. It is **the first hint bar in this
firmware whose text varies within a screen**, and it has to: `screen_settings.cpp`
stated the premise outright ("CHANGE, not OPEN: nothing here pushes a screen") and
the new row makes it false. One slot changes as the focus moves; the alternative is
a Confirm labelled `CHANGE` that opens a screen.

**THE APPLY IS KEYED ON A READER BEING ANYWHERE ON THE STACK, NOT ON TOP**, and
that is load-bearing twice. From Settings there is no Reader and nothing should be
re-paginated. From the reader menu the pop lands on the MENU, which is an overlay —
`App::render` walks down to the topmost non-overlay, paints the Reader, then paints
the overlay over it — so **the Reader's stale page IS drawn on the very next
frame**. "On top" would never fire there. `Back` is a plain `pop()` for the same
reason `popTo(Reader)` was wrong: Settings' stack has no Reader and `popTo` stops at
the root.

**`ReaderScreen::relayout` LANDS AT THE TOP OF THE BLOCK**, dropping the cursor's
line, in one place. `setMetrics` cannot serve — it re-opens the chapter at PAGE ONE,
which is not what a reader who changed their type size asked for. And `fitOf` had
already graded this before the feature existed: it keys on `(ppem, columnW)`, so a
size or margin change reads `Relaid` and zeroes the same field, while line spacing
and alignment change neither and `line` legitimately survives them.

**THE PAGE RING IS GIVEN BACK ON THE WAY IN**, before any face re-init, because
`ScalableFont::init` takes the new arena BEFORE releasing the old: at ppem 46 the
roman alone is 24,576 bytes transient on top of the 16,384 it holds, against a
42,152-byte reading floor.

**A PERSISTED `Size` NEEDS A SECOND APPLY AT BOOT.** The body face is inited ~230
lines before `loadAndApplySettings()` runs, with the constant `kBodyPpem`, because
it must exist before anything can measure with it. So the setting reached the
SETTINGS and never the FACE: margins, lead and justify survived a reboot (
`readerMetrics` is computed after the load) and Size did not — set 22 PT, reboot,
and the page came back at 15 while both screens said 22. `setup()` now re-inits when
the face DISAGREES with the setting, guarded that way so a card holding the default
costs no cache flush.

**THE PREVIEW SHOWS ALL FOUR EDITABLE ROWS, AND IT SHIPPED SHOWING THREE.** The
spec said the box "cannot preview the margins" because the box is chrome geometry —
396px of measure on the X4 where the reading column is 444, so it can never BE the
reading measure — and that framing was wrong. **THE BOX IS THE PAGE AND ITS SIDE
PADDING IS THE MARGIN**, so the padding tracks the setting and the base measure
being narrower than the column is beside the point. Reported off the device as
"changing the margins doesn't update the live preview", which is the argument that
put justification in the box arriving on the one row that had been excluded from it.

- **The delta is EXACT, not scaled**, because the box and the panel are the same
  device pixels: one px of margin narrows the reading column by 2px and this
  padding by 1px each side. `kTypoPreviewPadXBase` is 16 at the tightest step, so
  `margins = 18` renders the board's 24px and WIDE reads 36.
- **ONLY THE MEASURE MOVES, AND THAT IS THE HALF A TEST HAS TO CHECK.** The border
  is placed from `kMargin` and `boxH`, neither of which reads the setting, so no row
  below the box shifts. A fix that inset the whole box instead would keep the box's
  HEIGHT and step every row below it on every press of one row —
  `test_theme_typography.cpp` compares the border's inked COLUMNS between the two
  end steps for exactly that, and it fails 166 assertions when the outline moves.
- **Justified text RIVERS MORE in the preview than on the page** at the default and
  wide settings, because the measure is still narrower than the column. The roadmap
  records rivers at this size as inherent; the preview exaggerates them.
- **The box's height is DERIVED and fixed with respect to the settings**, so the
  five rows never move and there is visible slack at large sizes. A pinned height
  was tried, was wrong by ~42px, and `flex-shrink` hid it — CLAUDE.md's first
  invariant, broken in this feature's first commit.

**THE TWO TIGHTEST LEADS ARE TIGHTER THAN THE FACE'S OWN INK, AND 1.0 CAN TOUCH.**
`kLineSpacingSteps` is seven values now — 1.0 and 1.2 were added below the shipped
floor of 1.4 — and the measurement is in `settings.h` beside the table so nobody
re-derives it: the body face at ppem 32 is `ascent=38 descent=-10 lineHeight=48`, so
its nominal extent is 48px against a 32px line box at 1.0 and 38px at 1.2. The
nominal figure is the face's worst case rather than any real pair of lines — with
real glyph heights a collision needs a box under ~40px — so **1.4 is clear despite
overflowing nominally, 1.2 can touch by ~2px and 1.0 by ~8px**. Offered anyway: it
is a reading-comfort call and this glass is the only place to settle it.

- **`settings.cpp`'s `kLineSpacingSteps[2] == kBodyLeadEm` assert exists to fail
  here**, and did: the default's index moved 2 → 4. A step added below the default
  silently re-indexes it, and the build stopping is what forces the number re-read.
- **IT TURNED A DOCUMENTED NON-PROPERTY INTO A REAL ONE.** `previewLinesThatFit`
  measures INK rather than line boxes, and its comment said plainly that
  `floor(boxH / lead)` agreed with it everywhere reachable and that a mutation to
  floor failed nothing. With these two steps the rules differ in **13 of 210**
  reachable cases, and at **(ppem 42, lead 1000) on the X4 floor draws a fifth line
  whose ink leaves the box** — the slice itself. **No hand-picked sample had that
  pair**: the case list held 25, 32 and 46 at that lead and all three agreed with
  floor, so the test walks the whole space (5 sizes × 7 leads × 3 margins × 2
  panels, 0.44 s) instead. The floor mutation now fails 14 assertions. **Writing
  down that a mutation does not bite is what made it noticeable when it started
  to.**

**`ProseAlign::Justify` EXISTS BECAUSE THE BOX SAYS LIVE PREVIEW.** Without it,
`CHANGE` on the `Alignment` row spends a ~520 ms repaint moving four characters of a
row value while the box does not move — a live preview visibly ignoring one of its
four rows. `stretchFor` and `kMinJustifyFillPercent` moved from `layout` down to the
TEXT layer with it, where `drawTextJustified` already lived; `stretchFor` takes no
`PageMetrics`, no `Block` and no cursor, so it was never pagination's.

**ppem 38 IS `18 PT`, AND `roadmap:1269` IS STILL OPEN.** That entry asks whether the
reader is under-sized by its own spec, having spotted that the board stated `18 PT`
while rendering book text at 32px. Implementing the screen forced the contradiction —
the firmware cannot render both — and it was resolved toward the preview's pixels and
the shipped default, because at ppem 38 the specimen is cut off mid-sentence with
~85px of empty box beneath it. **The board now says `15 PT` and the question is
unchanged**: it is about the DEFAULT, it is answerable only on the panel, and 18 PT
is one press away on the device.

**THE GLYPH CACHE LEVER IS SPENT.** `ScalableFont::cacheBytesFor` was written so a
caller could size the arena from the chosen ppem, and until now had none. The Size
row is that caller, and the pair's worst case is 39,936 bytes at ppem 46 against
26,624 today.

**WHAT THE DESKTOP CANNOT SEE HERE**, and it is most of the shell: that the faces
re-init without OOM at ppem 46 against the reading floor, that the ring shrink buys
the headroom `init` needs, that the apply fires exactly once per panel visit, that
the frame after the pop shows the re-paginated page under the veil, and what
`relayout` costs on a card-backed book — the desktop does no SD reads and no real
inflate, and this file's ~135× ratio warning applies to that walk.
