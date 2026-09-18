# Storage

Extracted from `CLAUDE.md`, which keeps a stub under this heading and is where
the cross-references to it point. Same standing as anything in that file.

**`core/` sees one interface — `reader::FileSystem`** (`filesystem.h`) — and never
learns what backs it: `exists`, `list`, `readAll`, `writeAll`, `mkdirs`, `remove`,
`openRead`, plus `mounted()`. Three implementations, and the contract is what
keeps them one thing:

| Implementation | Lives in | In which build |
|---|---|---|
| `FakeFileSystem` | `test/unit/fake_fs.h` | Unit tests. In-memory, injectable failures. |
| `HostFileSystem` | `core/src/host_fs.cpp` | **Desktop only** — the simulator and desktop tests. |
| `SdFileSystem` | `shell/src/sd_fs.cpp` | The device, over `SDCardManager` (SdFat). |

- **`host_fs.cpp` is excluded from the firmware exactly as `png.cpp` is**, via
  `core/library.json`'s `srcFilter`, because `<filesystem>` is a host-OS
  dependency. Belt and braces: the body is also guarded by `READER_DESKTOP`, so a
  filter that silently stopped matching yields an empty object file rather than a
  `<filesystem>` include reaching the ESP32 toolchain.
- **The contract is written once** (`core/include/reader/fs_contract.h`) and
  reported through a callback, so `test/unit/test_filesystem.cpp` drives it with
  doctest on the desktop and `shell/src/sd_selftest.cpp` drives the same clauses
  against a real card over serial. `shell/` has no test harness, so without that
  seam `SdFileSystem` would be the one implementation nothing checks. Build it in
  with `PLATFORMIO_BUILD_FLAGS="-DENCRE_FS_SELFTEST=1" make firmware` (that
  variable **appends** to `platformio.ini`'s flags; `--project-option` would
  *replace* them and silently build for the wrong board). It costs ~29.7 KB and is
  a stub returning **-1** — not 0 — otherwise, so a build without it cannot be
  mistaken for a build that passed. **27 clauses**, ten of them `openRead`'s.
- **The path normaliser exists in three copies**, one per implementation, and
  nothing but the contract's "a redundant or trailing separator addresses the same
  thing" clause holds them together. They live in three build worlds (Arduino,
  desktop-only TU, test header), so the duplication is deliberate — but a fourth
  implementation should extract it rather than copy it again.
- **`readAll` is not the EPUB path.** It is for the small JSON files V1 stores and
  `SdFileSystem` caps it at 64 KB, because `-fno-exceptions` makes a `resize` that
  cannot allocate an `abort()` with no diagnostic. `openRead` is the EPUB path
  (3A-3), and the difference is who owns the buffer: a handle is one fixed ~100-byte
  allocation whatever the file's size, so it has no cap and needs none.
- **The handle is RANDOM ACCESS, not just streaming**, and that is the requirement
  rather than a nicety: a zip's central directory is at the **end** of the file, so
  a forward-only stream cannot read an EPUB at all. `openRead(path)` returns
  `std::unique_ptr<FileHandle>` — null on failure, never an `abort()`, and every
  implementation allocates with `new (std::nothrow)` so even OOM is a null.
  `read(dst, n)` / `seek(offset)` / `size()` / `position()`, and **`seek` past the
  end is REFUSED with `position()` unchanged, not clamped** — that is SdFat's own
  `seekSet` semantics, so the device is the primitive rather than an emulation, and
  it keeps "that offset does not exist" distinct from "I am at the end", which is a
  distinction the end-of-central-directory scan needs.
- **The handle's `SpiBusGuard` is per OPERATION, never per handle lifetime.** A
  reader holds a book open for minutes. Holding the guard across that would not
  deadlock today — the mutex is recursive and both users are on the loop task — but
  the day a handle is held off that task, `renderTop()` would block behind it for
  as long as the book is open: a panel that never repaints, which reads as a
  display fault. Per-operation is sufficient because SdFat holds no bus state
  between calls; an open `FsFile` is a cluster number and an offset in RAM.
  `size()` and `position()` are answered from cached members and touch neither the
  bus nor the guard.
- **`DESTRUCTOR_CLOSES_FILE` is 0 in this SdFat build**, so `~FsFile` does *not*
  close and a leaked handle eventually stops the firmware opening anything at all —
  arriving as a failure on an unrelated screen, long after the leak. So the handle
  owns exactly one `FsFile`, closes it in its own destructor, and is reached only
  through a `unique_ptr`; `FileHandle` deletes copy and move so there can never be
  two owners.

**The settings file is CREATED at boot when the card has none** — defaults
written to `/.reader/settings.json`, logged. Three things want that: the user gets
a hand-editable file rather than an invisible one, 2C-3's Settings screen updates
a file instead of creating one, and the card-presence probe gets a guaranteed
target to read (see below). Only when **absent** — a corrupt or wrong-version file
is left exactly as the user typed it, because `loadAndApplySettings` already
logged `DEFAULTED` and overwriting it would destroy the only copy of their edit.

**Settings are flat JSON at `/.reader/settings.json`** (spec §5), read by a
minimal one-object parser in `core/` (`json.h` — no nesting, no arrays; Phase 3's
bookmark array will outgrow it). Nothing is vendored and there is no network to
fetch a parser with.

- **A bad file is replaced, not trusted**: missing, unreadable, unparseable or an
  unknown `version` all mean defaults. An out-of-range *value* is different — that
  field is **clamped** and the rest of the file still loads, because refusing to
  boot over one bad number is worse. `loadSettings` returns false either way.
- **The boot log distinguishes those two**, `DEFAULTED` from `CORRECTED`, and names
  the reason. That line is the only way a user ever learns their hand-edited file
  was rejected rather than applied.
- **The `Settings` struct's defaults are the constants the shell used to compile
  in** (`kSleepAfterMs`, `kFullRefreshEvery`, `kFullOnTransition` through 2B), so a
  device with no card behaves exactly as it did. `shell/src/main.cpp` holds the
  *reasoning* for each number; `settings.h` holds the number.

**The wake pointer is in NVS, not on the card**, because a wake must work with no
card in the slot — which is the entire state the SD-missing screen exists for.
`Preferences` namespace `encre_sess`, keys `ver` / `stack` / `slept` (NVS caps a
key at 15 chars). **The payload is ONE key**, and that is what makes the version
key a real commit record: with `scr` and `focus` as two keys, a cut between them
left a valid-looking mixed record — a review found that the "written last" claim
held only for a namespace's FIRST write, since an update's previous version key is
already valid. One payload plus a version written after it has no such gap.

The wire format is `core/include/reader/session_record.h` —
`home:-1;library:7:/books;item-actions:1`, root first — and it lives in `core/`
because `shell/` has no test harness and this is the only pure logic on the
resume path. An entry is `name:focus` or `name:focus:place`. Record version is
**5**.

**AN ENTRY'S THIRD FIELD IS WHAT ITS FOCUS IS AN INDEX INTO, AND WITHOUT IT THE
RECORD PRODUCED A WRONG ROW THAT LOOKED RIGHT (#14).** The Library can be listing
a **subfolder** of `/books` and the record could not say which, so sleeping in
`/books/Classics` on row 3 woke on `/books` row 3 — which is worse than losing
the position, because nothing on the glass says the restore went wrong and the row
opens a book the reader never chose. A focus is only meaningful relative to the
list it indexes, so the list is stored beside the index into it: `Screen::place()`
/ `setPlace()`, mirrored into `StackEntry::place`.

- **`core/` NEVER LEARNS WHAT A PLACE IS.** The Library's is a directory path;
  `session_record.cpp` knows only that it is bytes and what may not appear in them
  raw. A `path` field would put a filesystem into `App` and name one screen in a
  format that names none — the ladder-of-screen-names shape `App::restore` exists
  to have deleted. **One screen has a place today**, and a count in
  `test_focus_restore.cpp` says so, so the loop cannot quietly test nothing.
- **PERCENT-ESCAPED, BECAUSE A LEGAL FILENAME MUST NOT BREAK THE FORMAT.** `%`,
  `;`, `:` and every control byte become `%XX` — **`;` and `%` are both legal in a
  FAT long name**, so a format that trusted them is not a fix. Everything else
  passes through, **UTF-8 included**, because `nvs_get encre_sess stack str`
  printing `library:7:/books/Le Fléau` is the same property that made the screen a
  NAME rather than an ordinal: a record a person can read off a device is one they
  can diagnose. (`:` cannot occur in a FAT or exFAT name at all, so the only
  escapes a real card produces are `%` and `;`.)
- **A MALFORMED ESCAPE REFUSES THE WHOLE RECORD**, on the unknown-name rule; a
  place TOO LONG is a different question with a different answer. `kPlaceMaxBytes`
  is **128 escaped bytes** — enough for the `/books/<author>/<title>` a real card
  carries, and not enough for what FAT permits, which is stated as a limit rather
  than hidden — and a place over it is **dropped whole, never truncated**, because
  a cut path addresses a *different* directory rather than none. That is
  `Xml::kMaxAttrBytes`'s rule reached from the other side. **The entry's focus goes
  with it, written as `-1`**: a row index without the folder it indexes is the whole
  of #14, and -1 is not a marker but the real "nothing selected" every focused
  screen already accepts.
- **THE BOUND IS ENFORCED ON THE WAY OUT ONLY**, which is what lets
  `sessionStackMaxBytes()` stay derived (1,209 bytes at `kMaxDepth` 8, inside NVS's
  4,000-byte cap for a string, and the read buffer in `shell/src/session.cpp` comes
  from it). Anything that fit that buffer is by definition within the bound on the
  way in, and the SCREEN is the thing entitled to refuse a place — which it does.
- **THE FOCUS IS APPLIED ONLY WHEN THE PLACE WAS HONOURED**, in `App::restore`,
  and that one branch is what makes every failure degrade instead of mislead: a
  folder deleted while the device slept, a card that is not the card the record was
  written on, a `..` component, or **a screen that reports a place and never learned
  to accept one back** all land the user at the top of the list the screen did
  build. `reading_position.h`'s `fitOf` grading is the same rule over
  a different quantity — a book's block rather than a screen's list.
  There is deliberately no `FocusScreen`-style `final` pair here: one screen has a
  place, and a shared base for a single caller is a header edge bought for nothing
  (the Typography formatters' extraction was undone for that reason), so the
  **default `setPlace` returns false** and the half-taken pair costs a row rather
  than putting one in the wrong folder. The second screen to want a place is the
  extraction point.
- **`setPlace`'s BOOL IS NOT `setFocus`'s BOOL**, and the difference is stated at
  the one site: a place is not a coordinate you can be part of the way to, so it
  answers "you are there now", where `setFocus` answers "something moved" because
  `moveFocus` needs to know whether a 520 ms refresh is owed. Asking for the place
  already listed is honoured and **touches no card** — the ordinary case, since the
  constructor lists the root.
- **THE VERSION BUMP IS A DECISION, NOT A NECESSITY.** A version-4 record still
  parses under the new decoder — two fields is the no-place form — and it is
  discarded anyway, because its `library:7` means "row 7 of some directory" and
  honouring that is exactly the wrong row the field exists to stop claiming. The
  cost is the documented one: one wake per device, the first after this firmware
  lands, which starts at Home.
- **A REFUSED PLACE IS VISIBLE IN THE LOG WITHOUT A NEW LINE**, because the restore
  already prints where it LANDED and compares it against what the record named
  (`encodeSessionStack(gApp->snapshot())` against the record's own string) — a
  dropped folder makes those differ, and the parenthetical now names it as one of
  the three reasons they can.
- **WHAT ONLY A CARD CAN EXERCISE, and therefore where this was untestable
  before:** the sample Library the goldens and the comparison sheet use has ONE
  directory and cannot descend, so nothing on the desktop could reach the defect
  until `test_session_restore.cpp` grew a `FakeFileSystem`-backed App. **The
  listing cost is measured there rather than argued**: a wake into a subfolder
  spends **4** listings against **3** for one at the root — `/books`, one
  `countBooks` per folder for the board's `FOLDER · 6 BOOKS` line, and then the
  subfolder — so the place costs exactly one more listing than a Library push
  always has. That is the honest price of the screen being built before it is told
  where it was, and it is why `setPlace` refuses to re-list a directory it is
  already showing. (This line first said "two", from reading the code rather than
  running it, and the folder counts are what it missed.)

**The stored focus is real**, and this paragraph twice said otherwise: it claimed
"always 0" after 2C-2 made that false, and the roadmap said the same. An
inherited-work note is a claim with an expiry date.

**`Focus` (`core/include/reader/focus.h`) IS WHERE MOVING A SELECTION LIVES**, and
until it existed there were five copies of it: `HomeScreen`, `StubScreen` (which
2C-3 has since deleted — it was Settings until the real screen landed), both
overlay panels and `ScrollWindow` each carried the same eight lines — add a
delta, clamp to a range, report whether anything moved — with the range spelled
slightly differently in each. That is why "clamp, do not wrap" had to be written
into four separate comments to stay one rule. A screen now declares its **range**
(`Focus::WithNone` when -1 is a position below the first item, as Home's CONTINUE
block is) and mirrors the focus into its view-model; it holds no clamp at
all. `ScrollWindow` owns a `Focus` plus the window around it, so the two concerns
are separable.

**The mirror around Focus is now `FocusScreen`'s**
(`core/include/reader/focus_screen.h`): the base class owns a `ScrollWindow` (a
window with `visibleRows == count` never scrolls and behaves as a bare `Focus`),
and a screen supplies `syncVm()` — mirror the focus and, for a windowed list, the
visible slice into the view-model — plus `focusable(int)` where some rows refuse a
landing. The triad every screen used to hand-write (`syncFocus`/`setFocus`/
`moveFocus`, three byte-identical copies plus two `ScrollWindow` variants) is one
mechanism now. **Landing rules live in `Focus` too**: `Focus::Gate` is consulted
per landing by the gated `move`/`set` overloads, which is where Settings'
skip-past-headers stepping went — the hand-rolled walk had silently stopped
wrapping while every other list rolled over. The gated walk is pinned to the
ungated arithmetic by an equivalence property in `test_focus.cpp`, so it cannot
drift into a second copy of wrap/clamp/none.

- **`set()` CLAMPS and `move()` WRAPS**, deliberately: `set` is the restore path,
  where a record naming row 400 of a three-row list means "as far down as you can
  go", and wrapping that to row 1 would land the user somewhere unrelated to where
  they were.
- **EVERY LIST WRAPS, and that reversed a decision this project had written down
  four times** — "clamp, do not wrap: a list that jumps silently from the last
  item to the first is indistinguishable from a stuck button". Half that argument
  still stands and it is worth knowing which half. A wrap is now the only thing a
  press at the end can do, so it can never read as a *dead* button — the screen
  always changes. What it costs is the opposite reading, a Down that appears to
  jump a long way, which is unambiguous on a four-row overlay and is the case to
  watch on a several-hundred-book Library. `setWrapping(false)` is the opt-out, on
  `Focus` and passed through by `ScrollWindow`; nothing uses it.
- **AUTO-REPEAT WAS WHERE WRAPPING WAS SHARPEST, AND IT IS NOW SOLVED.**
  `Focus::move(delta, held)` clamps when the flag is set, so a wrap belongs to a
  press and a hold rests at the end. It needed the gesture layer above to exist
  first: before that, nothing on the path knew whether a movement had come from
  holding, which is why the defect belonged to neither half that created it.

**THE RULE IS: A SCREEN THAT REPORTS A FOCUS ACCEPTS ONE BACK.** It was
implemented one screen at a time instead, and each screen that had not been done
yet failed the same silent way — `Screen::focus()` overridden, `setFocus()` left
on the base class's no-op, so the wake stored a real number, handed it back, and
the screen dropped it. No log line, no failing test, just the user waking on the
first row. Library got it in 2C-2, Home after "why does the Library come back
where I left it and Home does not", Settings after the same question again, and
each of the three headers carried a paragraph arguing that *its* screen was the
exception (Home cannot be pushed; the Stub is about to be deleted; no wake can
reach an overlay). Every one of those premises was true and every conclusion was
wrong: the reachability of a screen is a fact about the shell's restore ladder,
and encoding it in a `core/` header is how a change over there leaves a screen
silently one-way. `test/unit/test_focus_restore.cpp` walks **every** `ScreenId`
and asserts the round trip, `static_assert`s its own catalogue against the enum
so an added screen cannot slip past, and **counts** the screens whose focus can
move (**nine** — this line said five, then seven, then eight, and each figure was
right when written) so it cannot quietly end up testing nothing. **There
are TWO such counts**, `movable` and `wrapping`, and this line only ever mentioned
one. **And the `static_assert` beside them let a screen through**: it compared
against `ScreenId::SdMissing + 1`, a NAMED member rather than the last one, so
appending `Typography` satisfied it unchanged and the guard that exists to force a
new screen into `kAllScreens` said nothing. Caught only because the hand-maintained
counts failed for an unrelated reason; #42 is the fix.

**IT RECURRED WHEN `BookEnd` LANDED, AND THE SECOND INSTANCE WAS THE EXPENSIVE
ONE.** The assert had been advanced to `ScreenId::Peek + 1` — still a named member,
so appending `BookEnd` satisfied it unchanged and said nothing, exactly as
`Typography` had. That is the same defect twice in the same line, which is what
makes it a pattern rather than an oversight. **And the pattern is not confined to
the test**: `core/src/session_record.cpp` had *three* bounds spelled
`<= ScreenId::Peek`, so `sessionWireName` fell through to `return kNames[0]` and
**serialised the new screen as `home`** — a reader idle-sleeping on the end-of-book
screen would have woken on Home, with no failing test and no log line, because the
round-trip test could not see a screen the table was too short to name.
`session_record.cpp`'s table is `static_assert`ed against the enum's END now, so it
cannot be short.

**#42 IS CLOSED, AND THE THIRD APPEND IS WHAT CLOSED IT.** `ScreenId` ends in a
`Count` sentinel, and **all six** bounds name that instead of a member —
`kNames`' assert, its decode loop, both catalogue asserts, and
`test_session_record.cpp`'s two every-id walks. **This line said "all four" and the
two it missed were the two that were still broken**, which is the undercount the
paragraph below this one was written about: the fix reached four bounds, the count
of bounds was four, and the two walks nobody had enumerated kept naming a member.
**The defect was reproduced
before it was fixed**: `BookError` was appended alone and BOTH guards stayed silent,
the only diagnostic being a `-Wswitch` warning, which this project does not build
with `-Werror`. Then the same append was made against the sentinel and failed the
build twice, in sequence — `kNames` first, `kAllScreens` second — until each table
grew. That is the guard doing its job at the moment it was written for, rather than
one append later.

**`Count` IS NOT A SCREEN AND NOTHING MAY MAKE IT ONE.** `sessionWireName` breaks
out, `screenName` answers `"?"` and `DemoScreenFactory::create` returns nullptr —
each **explicitly** rather than by fall-through, so `-Wswitch` keeps working as the
reminder that a new screen needs a case. A sentinel that quietly became
serialisable would be a worse version of the bug it closes.

**IT RECURRED A THIRD TIME WITH `BatteryEmpty`, AND THE SENTENCE ABOVE IS WHY IT
WAS ALLOWED TO.** "`session_record.cpp`'s table is `static_assert`ed against the
enum's END" was believed of that file and was **not true of it**: the assert read
`static_cast<size_t>(ScreenId::BookEnd) + 1` — a NAMED member, exactly the shape
`test_focus_restore.cpp` had — so appending `BatteryEmpty` put `BookEnd + 1` on both
sides and it said nothing. **Its own comment claimed "TIED TO THE ENUM, NOT TO A
NAMED MEMBER"**, which is the worst version of this defect: a guard that documents
itself as the fixed one. The only thing that pointed at the tables was **`-Wswitch`,
three warnings and not errors**, so a build with warnings scrolling past would have
shipped the new screen serialising as `home`. That was the state of the
`BatteryEmpty` branch before it merged `main`'s sentinel.

**THERE WERE SIX HAND-MAINTAINED BOUNDS ACROSS THREE FILES, MAIN'S FIX REACHED FOUR,
AND THE MERGE FOUND THE OTHER TWO.** `session_record.cpp`'s assert **and** its
`decodeName` loop, `test_focus_restore.cpp`'s catalogue **and** its assert, and
`test_session_record.cpp`'s two every-id walks. The sentinel landed on `main`
against the first four; the last two still read `<= ScreenId::BookEnd`, so **`main`
itself carried two walks that did not cover `BookError`** — a screen was appended,
the two guards that name `Count` fired, and these two silently walked fourteen of
fifteen ids. They name `Count` now, so **all six are structural and none can be left
behind by an append.** `grep -n "ScreenId::Count" core/src/session_record.cpp
test/unit/test_focus_restore.cpp test/unit/test_session_record.cpp` is the check to
run before believing any figure here — which is how the two were found, the figure
in this paragraph having been wrong at every previous revision of it.

**AND THE SENTINEL'S FIRST REAL TEST WAS A MERGE, WHICH IS THE CASE #42 EXISTS FOR.**
`BookError` and `BatteryEmpty` were appended on two branches at once and merged, so
both tables and both asserts had to grow in one change. Every guard naming `Count`
failed the build until it did; every guard naming a member would have passed
unchanged, and the loser of the merge would have serialised as `home`. **`-Wswitch`
stayed silent throughout** — the switches were exhaustive because the asserts had
already forced the tables — which is the first time that warning was not the only
thing standing between an append and a wrong wake.

**The rule is structural now**: `focus()` and `setFocus()` are `final` on
`FocusScreen`, so a derived screen cannot take one half without the other — the
test checks a property the
type system also enforces, and a sixth focused screen gets the whole contract by
choosing its base class.

**THE RECORD IS THE WHOLE STACK, AND `App` PUTS IT BACK** — `snapshot()` /
`restore()`, root first. It held one screen id through version 3, so
Home > Library > actions came back as **Home**: the restore pushed the overlay
onto a fresh app, the factory refused it (correctly — an overlay reads the focused
row of the Library under it, and there was none), and the user lost both. Three
things about the fix are worth keeping:

- **Order is load-bearing.** Each entry's focus is set BEFORE the next push,
  because an overlay reads its parent's focused row *at construction*. That is
  also what makes an overlay restorable at all. **The place now goes in front of
  the focus for the same reason one layer down** — the row is an index into the
  directory, so a Library told which folder only afterwards would caption the
  overlay above it with a book from the wrong one.
- **It deleted the special cases.** The shell's restore was a ladder naming Home
  ("already the root, nothing to push") and SD-missing ("the card mounted, so the
  message is no longer true"), and every screen not in the ladder was handled by
  accident — three of them wrongly. Both branches are now one question,
  *does the record's root match this app's root*, and `App::restore` names no
  screen at all. **A restore that stops early keeps what already stands**: a
  record from a newer firmware should not cost the user the Library they were in.
- **The wire format is in `core/`** (`reader/session_record.h`), as
  `home:-1;library:7:/books;item-actions:1`, because `shell/` has no test harness and
  that is the only part of the resume path that is pure logic. One payload key
  also makes the version key a real commit record — with `scr` and `focus` as two
  keys, a cut between them left a valid-looking mixed record.

**`focus` is SIGNED (`int16`), and that is what let Home restore its focus too.**
Restoring onto Home is not a push — Home is already the root — so the ladder
skipped it entirely and every wake from Home landed on CONTINUE whatever row the
user had left selected. Two things had to change together, and the second is the
one worth remembering: the ladder's Home branch sets the focus on the **root**
instead of on a screen it just pushed, and the record had to be able to hold
**-1**. On Library, -1 ("nothing selected", an empty `/books`) survived being
flattened to 0 because 0 clamps straight back to -1 there; on Home, -1 is the
CONTINUE block and 0 is the first menu row, so flattening woke the user somewhere
they were never sitting. **A field that cannot hold the value is not a place to
store it**, and a round trip that is stable on one screen for an accidental reason
is not a round trip. The record went version 2 → 3 with the type, so the first
wake after this firmware lands reads as "no session" and starts at Home.

**Two limitations to know before trusting the card:**

- **A card pulled after a successful mount cannot be re-mounted without a
  reboot.** `SDCardManager::begin()` opens with `if (initialized) return true;` and
  the SPI path exposes no `end()`/`unmount()`, so it reports success without
  touching the hardware. So the shell never accepts `mount()` alone: it requires
  `probe()` to agree. A RETRY that reports success and
  then fails is worse than one that stays put — so **RETRY has two branches**
  (`handleRetry`), told apart by `gSdBeganOnce`: never mounted this boot means the
  in-place attempt is real and is kept, while mounted-then-lost **restarts the
  device** (`esp_restart`), because boot is the only code path that re-runs the
  mount. The restart is forced by the SDK, not a workaround for our own bug.
- **`mounted()` is "the card was there and nothing has since told us otherwise".**
  There is no card-detect GPIO in the board profiles and `SdCard::status()` (the
  one cheap CMD13) is private to `SDCardManager`, so liveness is maintained from
  operation feedback plus the two probes below. Neither is a card-detect: a pull is
  noticed by a read *failing*, not by the slot reporting empty.
- **A pull is detected proactively**, because operation feedback alone never fires:
  V1 does almost no filesystem work after boot, so a card pulled on Home stayed
  invisible. `pollCardPresence()` runs from `loop()` **after** the paint block and
  gated on `!gApp->dirty()`, under the same `SpiBusGuard` — SPI traffic on the
  panel's bus, and battery on a device built to sit idle. A usable → unusable edge
  rebuilds the `App` rooted at `SdMissingScreen` (a fresh `App` is dirty and in
  transition, so it paints as a screen change) and leaves the session record alone.
- **What the probe READS is the load-bearing part, and getting it wrong shipped
  once.** The first version of the poll called `probe()`, which opened `"/"`. The
  root directory's sector is the one sector guaranteed to be in SdFat's cache after
  boot, so the poll was answered out of RAM and kept succeeding with the card in
  the user's hand: no log line, and the SD-missing screen was never reached.
  Hardware confirmed it. Two layers replace it:
  - **The fast probe, 2 s.** `probe()` reads a **byte out of `/.reader/settings.json`**,
    which walks the root directory, then `/.reader`, then a data sector. SdFat here
    has exactly **one 512-byte cache slot** — `FsCache` holds a single
    `m_buffer[512]`, and `USE_SEPARATE_FAT_CACHE` is gated on `__arm__` so it is
    **off** on the RISC-V C3 — so three sectors cannot all be served from RAM, and
    the slot ends up holding the *last* of the three, so the next probe misses on
    its first access. `useFileProbeTarget()` adopts the path only if it opens and
    yields a byte *now*; otherwise the target stays the root-directory read and the
    boot log says **DEGRADED**, because a probe that quietly falls back is the same
    defect again.
  - **The backstop, 25 s.** The above is an *argument* about cache geometry, and an
    argument is what was wrong last time. `deepProbe()` calls
    `SDCardManager::sdUsedBytes()` → `freeClusterCount()`, a whole-FAT scan of
    thousands of sectors that no 512-byte cache can serve. **25 s is a floor, not a
    taste**: the SDK caches that value for 20 s, so anything sooner returns the
    cached number without touching the card. It bounds worst-case detection at
    ~25 s and costs a real FAT scan (hundreds of ms to over a second on a big
    card), so it is rare by design and `armCardProbes` logs the measured scan time.
    `sdUsedBytes()` reports its own failure as **0**, so it is armed only if the
    baseline scan returns non-zero — otherwise it says it is not armed.
  - At most **one** of the two runs per call, and **the backstop wins when both are
    due**: its entire value is not depending on the fast probe being right.
  - The card-lost line **names which mechanism noticed**. If the backstop is doing
    the detecting, the fast probe is being served from cache and this is back.

**The shared SPI bus is handled in exactly two places.** Every public method of
`SdFileSystem` takes a recursive `SpiBusGuard` — and so does each *operation* on a
`FileHandle` it handed out, open and close included, which is the same rule and
not a third place; `renderTop()` in
`shell/src/main.cpp` takes the same guard around the **whole** paint, BUSY waits
included, because the driver keeps the display's CS asserted across them. Today
both run on the Arduino loop task, so this is free insurance — it is there to be
structural rather than a rule someone remembers when a background library scan
moves off that task.

**`SPI.begin()` ends up called twice on one bus** — `detectAndSelectBoard()` with
the display's pins, then `SDCardManager::begin()` with the card's, which is the
reverse of the order the SDK's comments assume. It should be benign (the CS-high
mitigation still applies, and SdFat sets per-transaction `SPISettings`), and the
card is mounted **after** the display is up. **The first paint after a mount is the
thing to watch on hardware**: a bad refresh with a card in and a clean one without
is this call order and nothing else.
