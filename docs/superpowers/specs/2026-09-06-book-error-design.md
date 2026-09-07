# The corrupt-book dialog

Issue #5. Board: `design/BookError.dc.html`, boarded and unbuilt since 3C+.
Roadmap pointer: `roadmap:790`, which names it in a scope list and nowhere else --
so the board is the only prior art and the behaviour is decided here.

## The defect it closes

`openBookAt` refuses a book with a log line and **nothing on the panel**:

```
logf("[open] %s REFUSED: %s (heap %u free, largest block %u)\n", ...);
return false;
```

So on the device, pressing Confirm on a book that will not open does *nothing
visible*. That is the dead-button shape this project has shipped twice and
records at length -- a control that acts and produces no change on glass reads
as a broken device, and here it is worse than a dead button, because the press
was correct and the file is the problem.

`book.h` already anticipated the fix in as many words: false with `*reason` set is
"NEVER an abort: this is bytes off a user's card, and **the caller has a screen it
can put the reason on**". This is that screen.

## When it is raised

`openBookAt` refuses **and** `push == true`. That is exactly two callers:

| caller | reaches it |
|---|---|
| `handleOpen`, a Library row's Confirm | yes -- the board's own context |
| `handleOpen`, Home's CONTINUE | yes -- and there is no Library under it |
| the wake restore (`push == false`) | **no** |

**The wake is excluded deliberately.** `App::restore` already stops short of a
Reader it cannot build and leaves the Library or Home standing, which is wrong in
a way the reader can see through -- the shape this project chose over
substituting content. Waking into a modal about a book nobody just asked for
replaces a calm landing with an interruption, and the reader has no context for
it seconds after pressing power.

**Two other `openBook` call sites are NOT this screen's**, checked rather than
assumed:

- the sleep-cover decode (`main.cpp:5006`) already answers `CoverResult::ReadFailed`
  and falls back to the reading card with its reason logged;
- Book details' author lookup (`main.cpp:5610`) is best-effort and already logs
  `[details] no metadata for ...`.

Neither is a user asking to read a book, so neither owes the reader a dialog.

## The screen

`ScreenId::BookError`, **appended** -- the session record stores a screen by NAME
(`session_record.h`), so an insertion could not silently become another screen,
and appending also leaves every existing ordinal where it was.

| property | value | why |
|---|---|---|
| `isOverlay()` | `true` | the board veils a list and floats a panel; `App::render` walks down to the topmost non-overlay, so the parent is the Library **or Home** and the screen does not care which |
| `fidelity()` | `Mono` (the default) | chrome, one waveform |
| base | `FocusScreen`, 2 rows | `kOk = 0`, `kDelete = 1`, the board's top-to-bottom order |
| initial focus | `kOk` | a press made before the reader has read anything dismisses -- `DeleteConfirmScreen`'s cancel-first rule, and the board draws OK as the filled slab |
| hints | `{CLOSE, SELECT, UP, DOWN}`, no holds | verbatim from the board's four slots |
| `Gesture::Back` | `Action::pop()` | Back IS close, which is what the first hint slot says |
| `paintFootprint()` | `1` (constant) | the panel's height is fixed once the screen exists, and focus only decides which of two same-box slabs is filled -- identical to `DeleteConfirmScreen`, and pinned by `test_partial_repaint.cpp` rather than asserted in the header |

**`paintFootprint` is constant even though the prose wraps to different heights
between the two copy shapes.** A push is never a partial repaint
(`transition()` is the signal), so two different instances can never be compared
against one frame record; the token only has to hold across focus moves within
one screen's life. `DeleteConfirmScreen` is constant on the same reasoning while
its caption carries a book title of any length.

## Two copy shapes, because one of them would be a lie

`openBook` has four refusal reasons and they are not one event:

| reason | shape |
|---|---|
| `zip.reason()` | Damaged |
| `book.reason()` | Damaged |
| `"the spine names no chapters"` | Damaged |
| `"cannot open the book file"` | **Unreadable** |

The last is `fs.openRead()` returning null -- the file is gone, or the card is.
And `SdFileSystem::openRead` does **not** call `noteCardGone()`; only a handle
read that comes up short does. So a card pulled between the Library's listing and
the press is noticed by `pollCardPresence` at 2 s (fast probe) to 25 s (the FAT-scan
backstop) -- and for that whole window a single-copy dialog would tell the reader
their book "appears to be damaged" when nothing is wrong with it.

**A false claim is worse than an absent one**, which is the call this project
already makes for an unread battery gauge (`-1`, not `0%`), for a book with no
reading position (no demo substitute), and for the charging bolt that now spends a
refresh on the unplug edge rather than staying wrong on glass. This is the same
rule, so it gets the same answer.

**The screen takes a bounded `enum class BookErrorReason { Damaged, Unreadable }`,
never `openBook`'s `why` string.** `core/` composes copy from a closed set; the
`why` string is developer English (`"the spine names no chapters"`), unstyled and
unbounded, and the board has no slot for it. It continues to go to the log
unchanged, where it is actionable.

**The second shape gets its own board**, `design/BookErrorUnreadable.dc.html` --
same `ScreenId`, same layout, different copy. That is the design-first rule (copy
invented in code silently invalidates `make compare`), and it is exactly what
`SleepIdle.dc.html` is to `Sleep.dc.html` and `HomeUnopened.dc.html` is to
`Main.dc.html`: one screen with its content removed or replaced, not two screens.

Copy:

- **Damaged** (the board's own, verbatim):
  `"<file>" appears to be damaged and can't be opened. The file was left untouched on the card.`
- **Unreadable** (the new board):
  `"<file>" could not be read from the SD card. The file was left untouched.`

Both name the file, because a dialog that does not name the thing is one people
learn to dismiss without reading -- `DeleteConfirmScreen`'s own stated reason for
putting the title in its caption.

**The caption stays fixed at `CAN'T OPEN FILE` for both**, which is the board's.
Note it does NOT carry the book's name, unlike DeleteConfirm's -- the name is in
the prose here, and following the board is the rule.

### Both slabs stay focusable on both shapes

Considered and rejected: making `DELETE FILE...` unfocusable on the `Unreadable`
shape, on the argument that deleting a file the device could not even read is
destructive action on bad information.

Rejected because the rule would be invisible. Focusability derived from state is
legitimate here -- Typography's `Font` and Settings' `Cover fit` both do it -- but
in both of those the reader can *see* what makes the row inert (one font is
vendored; `Shows` reads DETAILS). Here the two shapes differ only in a sentence of
prose, so a reader meeting the inert slab has nothing to learn the rule from, and
that is the "works only sometimes" trap CLAUDE.md records for `About this book`.

The copy tells the reader what happened and the reader decides. A file that will
not read is a legitimate thing to want off the card, and if the card really is
gone the removal fails -- which needs no branch, because `FileSystem::remove`
reports the END STATE and the list the reader lands on already says which it was.
That is `DeleteConfirmScreen`'s own documented reasoning for not branching on its
result.

## `DELETE FILE...` -- DeleteConfirm takes facts, not a reference

`DeleteConfirmScreen` today holds a `LibraryScreen&` and acts through
`library_.deleteFocused()`, which resolves the path from the Library's focused row,
removes it through the Library's `fs_`, rescans, and returns
`Action::popTo(ScreenId::Library)`.

**None of that is reachable from Home's CONTINUE**, which has no Library at all --
and CONTINUE is the likeliest real corruption path, because it is a book the reader
was part-way through. Leaving the slab dead there would be a button that works only
sometimes, which is worse than one that never works, because nobody can learn the
rule.

So `DeleteConfirmScreen` takes facts, following `BookDetailsScreen::Facts`, which
solved the identical problem for the identical reason:

```cpp
struct Facts {
  std::string path;         // absolute on the filesystem
  std::string displayName;  // for the caption
  ScreenId returnTo;        // Library from ItemActions; Library or Home from BookError
};
```

**The removal becomes a shell latch**, joining `Open`, `Retry` and `Finish` --
`Action::Kind` already has four of them and `App` already records a request the
shell answers on its next pass. That is the right home because a delete's
consequences are all the shell's and none of them are `core/`'s:
`SdFileSystem::forgetCardFacts()`, the Library's `rescan()`, `gHomeStale`,
`gLibraryStale` and `libraryCountForHome`'s cache key.

`returnTo` is a field rather than a derivation because the screen must not have to
know how it was reached -- the same reason `Facts` replaced the reference.

**`gHomeStale` must be set when `returnTo` is Home**, or Home comes back offering
to continue a book that no longer exists. Home does check `last.json` against the
card with one `exists` call before drawing, so the failure is a fallback to the
nothing-open variant rather than a lie -- but the rebuild is what makes the
LIBRARY count right too, and it is one line beside the removal.

**This is the largest piece of the work**: `DeleteConfirmScreen` is shipped,
tested and reachable from a second parent, so its existing tests and
`test_partial_repaint.cpp`'s every-ordered-pair walk both have to keep passing
unchanged.

## The warning mark

`kWarning`, a new icon: one `ICONS` entry in `tools/iconc.py` naming
`design/BookError.dc.html` as its `source` and matching on the triangle's own path
data (`M9 1 17 15H1z`), then `make icons`.

Matched on the path rather than on anything else for the reason the battery pair
records: `iconc.py`'s matcher must key on something that identifies *this* mark
and not its family, and `source` is the second line of defence -- `BookError`
and `BookErrorUnreadable` will both carry the triangle, so the entry names the
first explicitly.

2 bpp like every other shipped mark, so it takes whatever treatment the plane
implies. Note the triangle is **three diagonals**, and CLAUDE.md records that a
thin diagonal is where `Mono` thresholding reads one notch lighter -- `kChevron`
is the precedent. This is the mark to look at on glass.

## Fixing #42 while appending

Appending `ScreenId::BookError` walks into a documented trap that has now fired
**twice** -- once for `Typography`, once for `BookEnd`:

- `core/src/session_record.cpp` spells three bounds as `ScreenId::BookEnd` (the
  table's `static_assert`, the decode loop, and `sessionWireName`'s switch
  falling through to `return kNames[0]`). Short, and the new screen **serialises
  as `home`** -- a reader idle-sleeping on the dialog wakes on Home, with no
  failing test and no log line.
- `test/unit/test_focus_restore.cpp:59` asserts its catalogue's length against
  `ScreenId::BookEnd + 1` -- a NAMED member, not the last one -- so an append
  satisfies it unchanged and the guard that exists to force a new screen into
  `kAllScreens` says nothing.

Both files carry a comment saying the next append needs the line moved by hand.
This is the third append, and **the instance count is the argument for fixing it
generally rather than one file at a time** -- CLAUDE.md says so in as many words.

The fix: a trailing `Count` sentinel on `ScreenId`, with both `static_assert`s and
`session_record.cpp`'s three bounds pointing at it. Its own commit, and it closes
issue #42 alongside #5.

**`Count` is not a screen**, so `kAllScreens`, `screenName()` and
`sessionWireName()`'s switch must each refuse it rather than acquire a row --
a sentinel that accidentally becomes serialisable is a worse version of the bug
being fixed. The `static_assert` on `kNames` is what proves the table did not grow
a row for it.

## What gets built

| | |
|---|---|
| board | `design/BookErrorUnreadable.dc.html` (new); `BookError.dc.html` unchanged |
| view-model | `BookErrorViewModel` in `viewmodel.h` -- caption, message, two labels, `focusedAction`, hints, holds. No geometry. |
| screen | `core/include/reader/screen_book_error.h` + `core/src/screen_book_error.cpp` |
| theme | `Theme::renderBookError` virtual, `QuietTheme` implements, built from `drawPanelCaption`/`wrapProse`/`drawProse`/`drawActionButton`/`drawIcon`/`drawHintBar` (`components.h`) plus `veilRect` (`dither.h`) |
| icon | `kWarning` via `iconc.py` |
| simulator | `book_error` and `book_error_unreadable` subcommands in `sim/main.cpp` |
| compare | `book_error` exists in `FLOW_SCREENS`; add `book_error_unreadable` |
| goldens | 4 -- two shapes x two geometries (480x800 and 528x792) |
| refactor | `DeleteConfirmScreen` to `Facts`; delete becomes a shell latch |
| #42 | `ScreenId::Count` sentinel; four bounds repointed |

## What only the panel can answer

Named here so the card's move from `On glass` to `Done` has something to be
evidence of, and because `shell/` has no harness -- none of the wiring below is
executed by the desktop suite:

- **the warning triangle's three diagonals under `Mono`.** Thresholding reads a
  thin diagonal one notch lighter; whether it stays a legible warning mark at 32px
  is a glass question.
- **the dialog actually appearing on a real refusal.** The desktop suite cannot
  reach `openBookAt` at all. The reproducible case is a truncated `.epub` copied
  onto the card.
- **`DELETE FILE...` from Home's CONTINUE**, which is the whole reason for the
  `Facts` refactor and the one path with no Library under it.
- **the veil over Home**, which no board draws -- every overlay board in the repo
  veils a list, and Home is the first parent with a 67px numeral and a dither
  block under the stipple.

## Open question for review

The `Unreadable` copy is written above as *"could not be read from the SD card.
The file was left untouched."* It is deliberately short and makes no promise about
retrying, because this board has no RETRY slab -- unlike `SdMissing`, which does.
If the reader should be told to check the card is seated, that sentence belongs on
the board first.
