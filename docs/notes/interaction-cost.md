# What an interaction costs

Extracted from `CLAUDE.md`, which keeps a stub under this heading and is where
the cross-references to it point. Same standing as anything in that file.

The budget from the button going down to the panel starting to move, measured or
derived on the X3. **The waveform is not the interesting part** — it is 389 ms
(DU) or 693 ms (GC) and there is no third bank; `RefreshMode` has only
`FULL`/`HALF`/`FAST`, `HALF` maps to GC, and there is no A2. Everything *before*
it is what was worth attacking, and it was ~150–1200 ms depending on the screen.

| stage | cost | where |
|---|---|---|
| press → the input task sees the edge | 0–10 ms poll + ~6 ms debounce | `kPollMs`, the SDK's `DEBOUNCE_DELAY = 5` needing a later sample |
| ~~down → release~~ | ~~80–200 ms~~ **gone** | see the `Short`-on-down rule above |
| queued → the loop looks | ~~0–10 ms~~ **~0** | `waitForRawSample(10)`, which was `delay(10)` |
| pre-dispatch card work | 0 / ~100 ms / **~1100 ms** | details author `openBook`; a `/books` listing |
| dispatch + `saveWhereWeAre` (NVS) | ~2–15 ms | one small `putString` |
| `saveReadingPosition` (2 SD writes) | ~20–100 ms, on Back-from-a-book and chapter crossings | |
| render | chrome ~40 ms, an overlay ~3× that, a reader page 125–165 ms | `[paint] render=` |
| plane write, 52,272 B at 20 MHz | ~21 ms | `Uc8279Driver::displayStart` |
| **the waveform** | **389 ms / 693 ms** | `BW_DU` / `BW_GC` |
| DTM1 sync, a second 52 KB write | ~21 ms, *after* the image is on glass | `displayFinish` |

Desktop render, `reader_sim <screen> out.png --canvas 528x792 --bench 200`, µs a
pass warm: home 252, library 292, settings 208, contents 199, book_details 116,
library_actions 698, reader_menu 764, reader (dithered) 474. **An overlay costs
~3× a bare screen** because it renders the parent, the veil and the panel; that
is what `App::renderTopOnly` exists to avoid on a focus move, and it does not
apply to the push that opens one.

### Reading a run off the device

**One `[i]` line per interaction**, from the button going down to the panel being
finished with it. It exists because the cost used to be spread across log families
that could not be added up — `[input]` said a press happened, `[paint] done` said
what the panel cost, and everything between them had no line at all, which is how
a second of directory listing sat on the critical path of a Back with nobody able
to name it.

```
[i] #12 CONFIRM SHORT from=home to=library ev=1 | wait=7 pre=0 disp=1103 post=2
        render=41 up=24 wave=712 | total=1889ms ser=31 net=1858
```

`tools/latency.py run.log` groups those by where the press landed and reports
medians. **Median, not mean**: one interaction that caught the card doing internal
housekeeping drags a mean somewhere no press ever was.

| field | is |
|---|---|
| `wait` | the event's own timestamp to the loop picking it up — raw queue, loop wake, drain |
| `pre` | work done because of what is on top, *before* the dispatch |
| `disp` | `App::dispatch` — **a push builds a screen, so a Library rescan is here** |
| `post` | what the dispatch made necessary: `handleOpen`, a chapter jump, Home's rebuild, the session record |
| `render` | drawing the frame, all passes |
| `up` | the plane upload to the controller, *before* the waveform starts |
| `wave` | the waveform, plus the ~21 ms baseline sync that follows it |
| `ser` | how much of `total` was this device talking to the USB host |
| `net` | `total − ser`. **The number to compare across runs.** |

**A BURST IS ONE INTERACTION.** Several events can drain before one paint — that
is the coalescing the loop exists to do — so the line reports the FIRST event's
timestamp against the paint that satisfied it, with `ev=N`. Reporting per event
would divide one visible response between N lines and make every one look fast.
`paint=none` marks a press that changed nothing, which is exactly the case worth
having a line for.

**`ser` EXISTS BECAUSE WATCHING THE DEVICE CHANGES IT.** `HWCDC::write` posts what
fits the TX ring and then **blocks** until the host takes the rest; `flush()` spins
`delay(1)` until the ring empties, up to 100 ms. Unplugged, both short-circuit on
`!isCDC_Connected()` and cost microseconds — which is the device's real behaviour,
since it lives on battery, and is why this was never noticed. So **every timing
taken over USB is inflated by the cable**, and this project has already paid once
for a measurement artefact read as a device fact (a `delay(2500)` in `setup()`
recorded as the panel detection's cost, because the first *timestamped* line was
read as time zero). Every print site in `shell/src/main.cpp` goes through `logf()`
/ `logFlush()`, which time themselves into `gLogMs`; `ser` is the delta across the
interaction. Unplugged it reads ~0 and `net == total`, which is the proof that the
numbers either side of it are the device's own.

**`up=` and `wave=` are a split, not a change.** `showOnePass` calls
`triggerDisplay` then `completeDisplay` where it used to call `displayBuffer`, and
for the configuration this firmware builds those are the same two driver calls with
the seam exposed — `Uc8279Driver::display` is literally `displayStart` then
`displayFinish`; single-buffer means both take the `prev == nullptr` branch; and
`_inverted`/`_inversionDirty` are false forever because nothing calls `setInverted`,
which is what kills `triggerDisplay`'s fall-back guard and `displayBuffer`'s
FAST→HALF promotion. What it buys is the one division the log could not make: `up`
is ours and bounded by the 20 MHz SPI clock, `wave` is the panel's.

**`[render] total=… fill=…/N glyph=…/N …`** is the second half of `[paint] done`
and breaks a render down by PRIMITIVE — see "Which primitive spent the render"
below for what it found. `reader::Profile` (`core/include/reader/profile.h`) is
five slots and an injected clock: `core/` has no clock and must not acquire one,
so the shell installs `micros()`, the simulator installs `std::chrono`, and a
build that installs neither pays a load and a branch. Two clock reads per
primitive CALL, never per pixel.

**AND THERE IS A LOG ON THE CARD, because the cable changes the device.**
`logToCard` in `/.reader/settings.json` (off by default, hand-edited — it is a
diagnostic, not a preference, so it has no Settings row and needs no board) tees
everything `logf()` writes to `/encre.log`.

It exists for the one class of fault serial cannot see. `HWCDC::write` and `flush`
short-circuit unplugged and BLOCK when a host is attached, so a timing taken over
USB is not the device's — and attaching after a sleep can reset the chip, turning
the wake being investigated into a cold boot. **A fault that only happens unplugged
is not observable over the wire at all.**

**AND IT HAD NEVER RUN ONCE, THROUGH TWO PHASES OF THIS FILE DESCRIBING IT AS
WORKING INFRASTRUCTURE (#47, #69).** `gLogToCard` was read at four sites in
`shell/src/main.cpp` — the buffer append, the idle flush, the flush before sleep and
the `[alive]` line — and **assigned at none**; `git log --all -S"gLogToCard =" --
shell/` was empty for the whole life of the feature. `core/` parsed the key into
`Settings::logToCard` and the shell never consulted it, so the 4 KB buffer, the idle
flush, the dropped-byte counting and the 256 KB cap were all unreachable. Confirmed
on an X3: `"logToCard": true` applying correctly on the `[boot] settings in force:`
line and no `[log]` line on any `[alive]`, across a full session. **The same shape as
`ListRow::trackingEm1000` and `readerBookTitle_` — a reader with no producer** — and
it was found twice, from two directions, because a diagnostic nobody can turn on
looks exactly like a device with nothing to report.

- **THE FIX IS NOT THE ASSIGNMENT; IT IS WHEN THE QUESTION CAN BE ASKED.** The
  setting is on the CARD, so `loadAndApplySettings()` cannot run before the mount —
  and the lines this feature exists to capture all print before it: `[wake] refused`
  / `[wake] held` (~470 lines earlier), the `[prev]` crumb record, `[boot] reset
  reason=…`, and the storage bring-up itself. A tee armed at the load drops exactly
  the boot it was wanted for. **Nothing can be WRITTEN that early either**, since
  there is no mounted volume, so the only question is whether those lines are still
  in RAM when a flush first becomes legal.
- **SO THE TEE HAS THREE STATES AND STARTS ARMED**
  (`reader::CardLogBuffer`, `core/include/reader/card_log.h`): `Pending` buffers and
  may not write, `Enabled` keeps what `Pending` accumulated, `Disabled` discards it
  and stops. **Buffering by default and discarding is the cheaper of the two
  orderings** — a memcpy per line into a static array that exists either way, against
  a second buffer or a replay mechanism — and it is the only one that can keep a line
  printed before the file was read.
- **AND THE BOOT PREAMBLE IS FLUSHED THE MOMENT THE SETTING IS KNOWN**, in
  `loadAndApplySettings()`, rather than being left to `loop()`'s idle window. Boot
  does not fit in 4 KB: the next legal flush is after the first paint, thousands more
  bytes of stage lines, font timings, library scan and session restore later, so
  without it the file would open with a HOLE precisely where the wake diagnostics
  are. It is safe there for the settings file's own two reasons — the card is mounted
  and nothing has been painted.
- **IT IS `core/`'s LOGIC AND THE SHELL'S ARRAY.** Arming, appending, drop counting
  and the flush threshold are bytes in and bytes out, and `shell/` has no harness —
  five bugs have hidden there. The shell keeps the 4 KB (nothing in `core/`
  allocates) and owns the card write, which is the only part a desktop test cannot
  reach. `applySetting` is idempotent for the same answer, because
  `loadAndApplySettings()` runs a **second** time on the RETRY path and a re-arm that
  discarded would throw away the session so far.
- **THE STATED LOSS IS THE RETRY PATH.** A device that booted with no card decided
  *off* and threw the boot buffer away; if the card that then appears asks for a log,
  the tee arms from that point and the preamble is gone. At the moment the question
  was asked, the default was the only answer available — so the `[log] armed late`
  line says which of the three transitions happened rather than leaving them alike.
- **`logToCard` IS ON THE `[boot] settings in force:` LINE NOW**, and its absence was
  the other half of #69: it was the one field in the struct with no line reporting
  it, so a card asking for a log and a firmware ignoring the request looked
  identical, which is how the request went unimplemented for two phases. **An
  instrument that reports on less than it claims is worse than none** — the card
  probe answered from cache, the `make compare` default that skipped four screens,
  and this.
- **A FLUSH SAYS WHETHER IT LANDED**, because `appendToCard` can fail on a card that
  reads and refuses writes and the bytes are dropped either way: `[log] wrote NB` and
  `[log] COULD NOT WRITE NB` are separate claims, and a line reporting a write that
  did not happen is the false-claim shape this file refuses for the battery gauge and
  the sleep badge.
- **WHAT ONLY THE PANEL CAN ANSWER**, and it is the whole feature: that `/encre.log`
  appears at all, that it opens with the pre-settings lines, and that the ~40 ms boot
  flush does not cost anything visible. `shell/` has no harness, so 1,363 green test
  cases say nothing about any of it.

- **IT MUST NOT MAKE THE DELAY IT IS HUNTING**, which is the whole design. A card
  write costs ~40 ms and takes the DISPLAY'S SPI BUS, so one per line would put tens
  of milliseconds into every interaction and be indistinguishable from the fault. It
  buffers 4 KB in RAM and flushes **when the panel and the buttons are both quiet** —
  the gate `pollCardPresence` already uses — **and in exactly one other case**, which
  is the next bullet.
- **THE HEADROOM ABOVE THE TRIGGER WAS A ONE-SHOT RESERVE, AND THE LOG WAS THEREFORE
  LEAST COMPLETE WHERE A FAULT IS MOST INTERESTING (#83).** `kLogFlushAtBytes` was
  3072 against a 4096-byte buffer and the 1024 between them was described as the room
  a burst still has. It is room the buffer gets **once**: the flush may only run in an
  idle window, so from the trigger onwards the free space only shrinks and nothing
  tops it up. Measured on glass in the first real session the card log ever ran
  (X3/UC8279, 2026-09-07): **two drop events, 407 B and 349 B**, both in reading
  stretches where `quiet` stayed false — so the burst reached **1024 + 407 = 1431 B**
  past the trigger and `append` refused whole lines.
  - **NO TRIGGER CAN BE THE FIX, WHICH IS WHAT MAKES THIS A POLICY QUESTION AND NOT A
    TUNING ONE.** A reader turning pages keeps a paint owed or a press queued
    continuously, so the non-quiet stretch is bounded by **the user** rather than by
    anything the firmware picks. Lowering the trigger makes the hole rarer; it cannot
    make it impossible, and a number fitted to two drop events is fitted to one
    session, which this file's own rule says is not a distribution.
  - **SO THE RESERVE IS RESTORED EVERY LOOP ITERATION INSTEAD OF EVERY IDLE WINDOW.**
    `CardLogBuffer::mustFlush(reserveBytes)` asks whether fewer than that many bytes
    are free, and the loop tail writes the card when it is true **whether or not the
    loop is quiet**. What that buys is a bound the trigger cannot express: every
    iteration begins with `kLogLineReserveBytes` free, so a drop now needs more than
    the reserve **inside one iteration** rather than merely more than the headroom
    across an open-ended stretch.
  - **THE TWO CONSTANTS ARE TWO QUANTITIES**, the sleep card's `chapterReserveH` /
    `chapterH` idiom one feature over, and collapsing them is the defect: `mustFlush`
    takes **free space** where `wantsFlush` takes a **fill level**, the trigger is
    **2048** and the reserve **1024**, and a `static_assert` in `shell/src/main.cpp`
    fails the build if they cross — which the **old 3072 now does**, proved by
    mutation.
  - **1024 IS DERIVED FROM WHAT ONE ITERATION EMITS**, measured off the real format
    strings at values from this file's own recorded runs: a plain page turn is
    **417 B** (`[i]` 137 + `[paint]` 152 + `[render]` 91 + `[page]` 37), a chapter
    crossing **559**, and a crossing whose quiet-window jobs also report **725**. It
    is also 2× `logf`'s `char line[512]`, the hard bound on one append — a reserve
    under 512 could not promise even one whole line.
  - **WHAT IT COSTS, AND THE LAST TERM IS THE DEVICE'S TO SETTLE.** The forced write
    can only fire once per 3072 B logged, which is **one per eight page turns** in the
    worst case where the reader never pauses and **never at all** on a device that
    does. Against `net=` it is a ~15–40 ms write on a 634 ms (RIGHT Reader) or 1055 ms
    (LEFT Reader) turn — **2.8% typical, 7.9% worst**. Lowering the trigger to 2048
    costs write COUNT, not latency: 1.5× as many writes at two thirds the size, total
    bytes unchanged, every one still in an idle window. **RAM is unchanged to the
    byte** — 46,188 either side, measured — which is why growing `kLogBufBytes` was
    rejected: the array is `.bss`, so it is paid by every device at every instant
    including the overwhelming majority whose `logToCard` is off, and 4096 more is
    9.7% of the 42,152-byte reading floor.
  - **AND A FORCED WRITE NAMES ITSELF**, `[log] FORCED wrote NB in Xms`, for the
    reason `ser=` exists: a device whose reading bursts routinely overrun and one that
    never forces a write must not look alike in the log. **It does NOT land in
    `ser=`**, which is the USB cable's term, so it inflates `net=` silently and that
    line beside it is the only thing that says so.
- **IT REPORTS ITS OWN WEIGHT**: `[log] wrote NB in Xms` per flush and
  `buffered/dropped/sdTotal` on `[alive]`. Same reason `ser=` exists — an instrument
  that hides its cost lets you attribute it to the device.
- **A DROPPED LINE IS COUNTED, NEVER SILENT.** An overrun between two idle windows
  leaves a HOLE in the log, and a hole must not read as the device having gone quiet.
  **The count is what made #83 visible at all** — `dropped=756B` on an `[alive]` line
  is the only reason anybody knew. It is still CUMULATIVE and still only on `[alive]`,
  so it says bytes were lost and not **where**: with drops now rare, the next hole is
  further from the line that reports it. Marking the hole in place, in the file, is
  the honest completion of this bullet and is not built.
- **It flushes on the way into sleep**, after `markSleeping()` — the flag is what the
  next boot needs and the log is only what a human needs, so the ordering says which
  one may not be lost.
- **`appendToCard` is a free function in `shell/`, not a `FileSystem` method.**
  `reader::FileSystem` has no append and should not grow one for this: the contract
  is 27 clauses driven by two harnesses, and widening it means widening both for
  something `core/` will never call. It also deliberately does NOT ask
  `SdFileSystem::mounted()` — the reason that object thinks the card has gone is
  exactly the kind of thing worth having in the log.
- The file is capped at 256 KB and restarted past it, so a device left running
  cannot fill the card. Losing the oldest half beats refusing to write, which loses
  the newest.

**`[fs] list … (N.NN ms/entry)`** over 15 ms is the other half. `[i]` can say a
Confirm on Home spent two seconds in `disp=`; only this says the two seconds were
one `list()` over 406 entries. The ~2.7 ms an entry quoted throughout this file is
a figure from one session that has never been re-checked against the card in the
slot, and where a number decides a design this project's rule is to measure the
thing rather than argue about it.

**THE BIGGEST REMAINING NUMBER IS A DIRECTORY LISTING, AND IT IS NOT THE PANEL'S
FAULT.** `SdFileSystem::list` costs ~2.7 ms an ENTRY on the user's card, and a
203-book library is 406 entries because macOS writes a `._name` beside every
file — so **~1.1 s**, paid on the critical path in two places:

- **Home's `LIBRARY` count**, which is `countLibrary`: one listing plus one per
  folder, for one integer. It went on the critical path the moment Home learned to
  rebuild itself (`gHomeStale`), so a Back out of a book cost a second of listing
  with nothing on the panel. **Cached now** — `libraryCountForHome`, keyed on
  `SdFileSystem::removals()` and `gStorageUsable`. The key is sound *because of
  what V1 is*: a book cannot ARRIVE while the firmware runs (transfer is card-only,
  so putting one there means the card is in a computer), which leaves delete as the
  only mutation of `/books`, and every delete goes through `remove`. **V2's Wi-Fi
  transfer is the change that breaks that argument** and it needs a one-line
  invalidation beside whatever writes the file.
- **AND THE "ONE PER FOLDER" HALF OF IT IS ITS OWN DEFECT, WHICH THE CACHED INTEGER
  DOES NOT TOUCH.** `countLibrary` calls `BookList::countBooks` per subfolder, and
  so does `LibraryScreen::rescan()` — for the board's `FOLDER · 6 BOOKS` line — so
  a card with fifty folders paid fifty listings at boot and fifty more on **every**
  Library push. Invisible on a flat card and unbounded on a foldered one; the
  listing cache below does not help, because a six-book folder is under its minimum
  entry count and it has two slots against fifty directories. **FIXED by memoising
  the COUNT, not the listing** (`reader/dir_counts.h`, `DirCountCache`): the rows of
  a subfolder are never wanted, only how many are books, so fifty folders cost
  ~1.5 KB where fifty listings would not fit the 20 KB ceiling. A folder is now
  walked once per card STATE rather than once per caller — the second caller and
  every Library push after it reach the card for nothing.
  - **It hangs off the `FileSystem`, for the same reason the listing cache does**,
    and that is what made it reachable at all: `countLibrary` and `rescan()` share
    no state except the `FileSystem&` they were both handed, so a memo anywhere else
    would have needed a setter plumbed through the factory — a caller list, and this
    file's rule is that a caller list is a function not yet written.
    `FileSystem::dirCounts()` defaults to **null**, which means "I keep nothing" and
    is exactly today's behaviour, so `HostFileSystem` and every test wrapper are
    untouched and the simulator memoises nothing. **The 27-clause contract did not
    widen** — no new behaviour clause, so `test_filesystem.cpp` and
    `sd_selftest.cpp` are unchanged.
  - **`SdFileSystem::forgetCardFacts()` is the invalidation, and it is one function
    on purpose.** `listings_.clear()` already had five call sites; a second thing to
    drop would have made it five chances to half invalidate. Every method that
    CHANGES the card calls it, above its own refusals.
  - **`openRead` deliberately does NOT call it**, and the distinction is worth
    keeping: dropping the listing cache there is **eviction** (10–20 KB against the
    45,840-byte floor a book open measures), not invalidation — opening a file
    changes nothing — and that argument does not reach ~1.5 KB. So Home → open a
    book → Back → Library costs no folder listings at all.
  - **The fake memoises too**, so the desktop suite runs the path the device runs.
    A staleness bug is a failing test rather than a device report — which is the
    whole reason the memo sits on an object `core/` can reach.
  - **A hit is invisible**, because a folder answered from RAM produces no
    `[fs] list` line: `[library] … folders held=N hit=N miss=N` is what tells a memo
    that is working from one that has quietly stopped being called.
- **The Library's own `rescan()`**, on every push — the Library is destroyed by the
  pop that leaves it, so Home → Library → Back → Library lists twice. **FIXED by
  holding the listing, because the walk itself cannot be made cheaper** — and that
  was settled by reading SdFat rather than by arguing, which this file had left as
  an open question between "cache geometry" and "a double LFN walk". It is BOTH:
  - `FatFile::getName8()` is the only API that yields a long name. It opens a
    second `FatFile` over the directory's first cluster and calls
    `cacheDir(m_dirIndex - order)` once per LFN entry with the index **decreasing**,
    and `cacheDir` is `seekSet` + read. `seekSet` takes its "follow the chain from
    the first cluster" branch whenever the target is behind the current position,
    which a decreasing index always is — so **every LFN entry of every name
    re-walks the directory's cluster chain from its start**.
  - `USE_SEPARATE_FAT_CACHE` is gated on `__arm__`, so on the RISC-V C3 the FAT
    sector and the directory sector evict each other in one 512-byte buffer, turn
    for turn.
  - Nothing is left to remove: `getName` is already called once an entry,
    `isDirectory`/`fileSize` are RAM reads off the open handle, and `close()`
    reaches `syncDevice()`, which touches no bus outside a read/write stream. The
    remaining routes are editing a submodule this project does not edit, or
    decoding FAT **and** exFAT directory entries from raw sectors in `shell/`.
  - **exFAT does not have this defect** — `ExFatFile::dirCache` seeks relative to
    the file's own recorded `m_dirPos` with no restart. If a card ever measures
    fast, that is why.

**THE LISTING IS HELD BY `SdFileSystem`, NOT BY THE LIBRARY, AND THAT IS WHAT MAKES
THE INVALIDATION STRUCTURAL.** Every way to change what is on the card is a method
on that one object, so `writeAll`, `mkdirs` and `remove` each drop it and there is
no mutation path that can miss it. That is strictly stronger than the `removals()`
key Home's count uses, whose premise — a book cannot ARRIVE while the firmware runs
— is the one **V2's Wi-Fi transfer breaks**: a transfer writes through `writeAll`
and drops this cache by construction, with no line to remember to add.

- **Two slots, not one**, because `rescan()` lists `/books` and *then*
  `/.reader/state`. With one slot the sidecars would take it on every rescan and the
  Library would hit the cache exactly once, ever — a fix that silently stops working
  on a well-used device.
- **Eviction takes the SMALLEST listing held**, because the cost is per entry. LRU
  would throw `/books` out for the directory read that follows it, and "refuse
  anything smaller than what is held" wedges: one big folder locks `/books` out for
  good.
- **20 KB ceiling, and never resident at the 42–46 KB floor**, because `openRead()`
  drops everything — and `openRead` *is* the EPUB path (`readAll` serves the small
  JSON), so nothing but opening a book reaches it. It adds to the no-book-open peak,
  where there is ~133 KB free. **The folder-count memo does NOT go with it**: that
  clear is eviction rather than invalidation, and 1.5 KB does not need evicting —
  see `forgetCardFacts()`, which is the invalidation and which `openRead` does not
  call.
- **ONE BEHAVIOUR CHANGED: a cache hit never touches the bus**, so `list()` stops
  being a place a dead card is noticed. Affordable because `pollCardPresence()` was
  always the detector — operation feedback never fires in V1, which this file
  already records — and the cost is at most one stale listing inside a window that
  ends at the SD-missing screen.
- **The card probe is NOT answerable from it**, checked rather than assumed:
  `readRootDirectory()` and `readProbeTargetFile()` both go straight to SdFat and
  neither routes through `list()`. That is the recorded defect this would otherwise
  have reintroduced.
- **A HIT PRINTS NOTHING**, which on a device is indistinguishable from the call not
  happening — so `[alive]` carries `listings=N slots/NB hit=N miss=N`. "It got
  faster" and "it stopped being called" must not look the same.

**What was looked at and left alone, with the reason, so it is not re-derived:**

- **`Serial.flush()` in the paint path is free on battery.** `HWCDC::write` and
  `flush` both short-circuit when `isCDC_Connected()` is false. **They are not free
  with a logger attached** — `flush` spins `delay(1)` up to 100 ms — so *every
  timing taken over USB includes serial drain that the device never pays*.
- **The transition FULL refresh (693 ms vs 389 ms) stays**, and the roadmap's own
  measurement is why: the paints that *felt* quicker were the slowest, because the
  GC flash reads as the device acknowledging the press. **On e-ink, feedback and
  speed are separate problems.**
- **Deferring the panel wait** (`triggerDisplay`/`completeDisplay`, which the X3
  driver really does support) buys less than it looks like. The framebuffer must not
  be overwritten between the two calls, so the *next* render cannot overlap; and the
  contract is explicitly "non-SPI work", so no card access can either. What is left
  to overlap is CPU work and NVS — a few milliseconds. The SDK's own headline
  1274→822 ms figure needs `supportsBusyGrayscaleStaging()`, which only
  `PaperMonoDriver` returns true for.
- **The SPI clock is already at the datasheet maximum** (20 MHz, both X3 profiles),
  so the two 52 KB plane writes cannot be shortened without going out of spec.
- **`kPollMs` stays at 10 ms.** Halving it saves ~3 ms of a ~550 ms interaction and
  doubles the ADC duty cycle on a device built to sit idle. The queue peek was the
  free half of that trade; this is the half that costs battery for nothing.
- **`saveWhereWeAre`'s NVS write stays on the critical path.** A ~40-byte
  `putString` into a page with room is ~2–5 ms, and deferring it past the paint
  means `sleepNow()` — which is `[[noreturn]]` and can be reached in the same drain
  loop as the navigation that dirtied the record — has to flush it first. That is a
  correctness hazard bought with 1% of a paint. Measure it first; the
  `[session] stored` line is already there to hang a duration on.

### What a real run measured (2026-08-24, X3/UC8279, 203-book card)

53 interactions off the device, `ser=0%` — the cable was not in the numbers.
`tools/latency.py` medians, milliseconds:

| press | n | net | wait | disp | post | render | up | wave |
|---|--:|--:|--:|--:|--:|--:|--:|--:|
| CONFIRM Library → Reader | 1 | **1753** | 0 | 0 | **1012** | 306 | 25 | 415 |
| UP Reader | 5 | **1355** (max **3401**) | 533 | 0 | 0 | — | — | — |
| CONFIRM Home → Library | 1 | **1214** | 0 | **666** | 4 | 105 | 25 | 415 |
| LEFT Reader (page back) | 7 | **1055** | 97 | **376** | 0 | 155 | 25 | 414 |
| CONFIRM Reader → menu | 2 | 718 | 0 | 0 | 14 | **266** | 25 | 414 |
| RIGHT Reader (page fwd) | 2 | 634 | 38 | 26 | 0 | 131 | 25 | 414 |
| DOWN Library | 16 | 549 | 0 | 0 | 4 | 100 | 25 | 414 |
| CONFIRM Home → Settings | 1 | 515 | 0 | 0 | 4 | 71 | 25 | 414 |
| UP Contents | 2 | 508 | 3 | 0 | 5 | 62 | 25 | 414 |

**THE PANEL IS 439 ms AND IT NEVER VARIES** — `up=25` plus `wave=414` on every
one-pass paint in the run, ±1 ms. `up` is the 52,272-byte plane write at 20 MHz;
`wave` is the SDK's own `8279_DRF (389 ms)` plus the ~25 ms DTM1 baseline write
that follows it. **So an ordinary chrome interaction is 80–87% panel** and chrome
is finished: the only movable part left is a 62–106 ms render.

**Every transition in the run was FAST**, because this card's `settings.json` has
`fullOnTransition=0`. A screen change therefore costs the same 439 ms as a focus
move, and the 693 ms GC appears only on the first paint after boot. The "a
transition costs ~825 ms" arithmetic elsewhere in this file is the *default*
setting's, not this device's.

**`wait=` IS NOT A DEFECT.** It is how much of the *previous* paint the press
landed inside — the loop is blocked for the paint's whole duration, so pressing
faster than 550 ms puts the remainder in front of your press. Library DOWN shows
it directly: median 549 ms, max 937 ms, the difference being entirely `wait`.

**The four things the run actually indicts:**

1. **THE DEFERRED PAGE COUNT BLOCKED THE LOOP FOR 2–3.6 s** (`[index] pages=315 in
   3605ms`), which is *more* than the refinement's 1409 ms, while `kCountQuietMs`
   was 1200 ms — barely two paints. Two presses landed inside a count: one waited
   1304 ms to be noticed and then drew nothing, one took 3401 ms end to end.
   **FIXED, AND WIDENING THE WINDOW WAS NOT THE FIX** — that only made it rarer.
   `completeIndex` takes a stop predicate now; see below.
2. **A BACKWARD PAGE TURN SPENT ~390 ms IN THE DISPATCH**, against 20–33 ms
   forward — so paging back cost 1055 ms against 634 ms, and the rewind was
   comparable to the whole waveform. This file dismissed it as "33.9 ms desktop
   against a ~520 ms panel refresh"; **the desktop→device ratio on the inflate path
   is ~11×, not ~1×**, which is the same "37× is a RENDER ratio" trap recorded
   under the eager page count. **Fixed by the page ring; see below.**
3. **THE OVERLAYS ARE THE MOST EXPENSIVE RENDERS ON THE DEVICE** — the actions
   panel at 256 ms and the reader menu at 260 ms, against Contents' 62 ms. The
   guess recorded here first was the veil, and **the veil was wrong**: it is 10 ms.
   See the breakdown below, which is what settled it.
4. **`/books` LISTS AT 2.90–2.96 ms AN ENTRY**, confirming the figure this file has
   quoted for two phases, and 203 entries is **~600 ms** on every Library push. Not
   the 1.1 s estimated elsewhere here: that assumed 406 entries because macOS writes
   a `._name` beside every file, and **this card has none**. Both are fixed now —
   Home's count by `libraryCountForHome`, the listing itself by holding it, and the
   one-listing-per-FOLDER underneath both by `DirCountCache`; see **Storage**, which
   also names why the walk cannot be made faster. **Note this run's card is FLAT**,
   so it measures none of the folder cost: the defect the memo fixes is invisible on
   exactly the card every device measurement in this file was taken on.

### Which primitive spent the render

`[render]` is the second half of `[paint] done`, and it is per PRIMITIVE rather
than per screen — deliberately, because the primitives are shared: whatever is
expensive here is expensive on every screen that draws one, where a per-screen
breakdown would have to be read once per screen to notice that. Five slots, each
a leaf that touches pixels, so nothing nests and nothing is double-counted;
`outlineRect` is four `fillRect`s and is not a slot, `drawPanelRow` is a fill plus
a run and is not one either. Measured on the device, microseconds:

| screen | total | **fill** | glyph | veil | dither | icon | other |
|---|--:|--:|--:|--:|--:|--:|--:|
| ITEM-ACTIONS | 256 000 | **157 836** (62%) | 57 032 | 9 923 | 5 044 | 8 076 | 18 089 |
| READER-MENU | 260 000 | **138 246** (53%) | 103 621 | 9 965 | — | 5 918 | 2 250 |
| READER, a page | 215 000 | 299 | **212 935** (99%) | — | — | — | 1 766 |
| LIBRARY | 105 000 | 36 054 (34%) | 44 506 | — | 4 939 | 3 719 | 15 782 |
| HOME | 74 000 | 34 225 (46%) | 26 616 | — | 4 974 | 6 048 | 2 137 |

**`Framebuffer::fillRect` WAS 34–62% OF EVERY CHROME RENDER, and it was the same
defect `veilRect` had already been fixed for** — a per-pixel `setPixel` loop, so a
bounds check, a `byteIndex` (a division under rotation), a `bitMask` (a modulo)
and a read-modify-write, per pixel. An overlay panel is ~340×450 and a full-bleed
focused row ~300×72, so one actions-panel frame asked for ~200,000 of them. The
arithmetic that made it unambiguous, and which is the shape to look for next time:

| | pixels | cost | per pixel |
|---|--:|--:|--:|
| `veilRect`, byte-wise, whole frame | 418,176 | 9.9 ms | **24 ns** |
| `fillRect`, per-pixel | ~200,000 | 158 ms | **790 ns** |

Same class of work, **33× apart**, and the difference was entirely `setPixel`.

**IT IS BYTE-WISE NOW**: clip once, resolve the run to a first byte, a last byte
and two edge masks, then per physical row write the masked first byte, `memset`
the middle and write the masked last byte. **A fill has no PHASE** — unlike the
veil and the dither it is keyed on nothing — so its run is identical for every
row and the masks hoist out of the loop, which `veilRect` cannot do. Desktop, µs
a pass: `library_actions` 706 → **351** (fill 441 → 3), `reader_menu` 1004 → **437**
(402 → 4), `library` 277 → **219**, `home` 187 → **137**.

Two things it inherits from the veil and one it does not:

- **The rotation hazard is the same and so is the structure.** Under CCW a logical
  row is a physical column, so the outer loop is the logical **x**. Getting it
  wrong passes every desktop test and every golden and smears on glass.
- **The edge masks need their own tests**, because the panel widths are multiples
  of 8 and *overlay geometry is not* — it is derived by subtraction from a centred
  panel, so a run starting or ending mid-byte, and a negative origin, are real
  cases. `test_framebuffer.cpp` keeps the per-pixel form as its reference and
  asserts byte-identity across 37 rectangles × both rotations × both colours, and
  each of its cases was proved by MUTATION rather than by passing.
- **`ditherRect` WAS the last per-pixel area primitive, and it is byte-wise now
  too — so the family is closed.** It was expected to need the veil's shape,
  because its mask varies per row by tile phase; it needed the FILL's, and the
  difference is one modulus. A byte is eight columns and the tint's tile is
  **four** wide, so `8 % 4 == 0` and every byte of a row carries the identical
  mask — the run hoists out exactly as a fill's does, and only a one-byte table
  lookup varies per row. The veil's tile is **three** wide and `8 % 3 == 2`, which
  is the whole reason its mask has to advance per byte. *Which* primitive a new
  one resembles is decided by that modulus, not by whether it has a phase.
  - **THE SIZE OF IT WAS ESTIMATED FROM THE WRONG CALLER.** This entry named
    Home's cover placeholder, at 15–22 µs desktop — which is right (16–17 µs
    measured) and is not the big one. `renderSleep` tints the **whole panel**
    (`.dither-field`), 418,176 pixels, and measured **329–338 µs of Sleep's
    362.8 µs render — 91%**. Same mistake as the veil, whose cost was also assumed
    small until it was measured: a primitive's bill is set by its widest caller,
    and a full-frame call does not look different from a thumbnail at the call
    site.
  - **IT VECTORISES, WHICH IS WHY IT BEAT THE VEIL BY AN ORDER OF MAGNITUDE.** The
    veil managed 13.6×; this is ~14× on the whole Sleep render and **well over
    100× on the primitive**. The middle of a run is `row[b] |= tile` with `tile`
    constant, so clang emits `orr.16b`/`and.16b` over `q` registers — 16 bytes a
    go — where the veil's per-byte phase advance and a per-pixel loop can emit
    nothing of the kind. Release/-O3, `--bench 200`, three runs each: `sleep`
    362.8 → **25.9**, `sleep_idle` 343.5 → **8.1**, `home` 59.9 → **43.6**,
    `library` 91.5 → **73.2**, `book_details` 54.0 → **35.4**.
  - **THE TILE'S RANKS ARE NOT TRANSPOSE-SYMMETRIC BUT EVERY LEVEL'S SET IS**, and
    that distinction is what lets the two rotations share one mask table the way
    the veil's outright symmetry lets it swap axes. `kClustered[0][1]` is 6 where
    `kClustered[1][0]` is 4, so the matrix is asymmetric; but a threshold only ever
    asks "is this cell below the level", and each of those four sets IS its own
    transpose. It is a `static_assert` in `dither.cpp`, not a comment, because a
    change to `kClustered` that preserved density and broke it would draw the tint
    transposed **under rotation only** — right on every golden, wrong on glass.
  - `test_dither.cpp` keeps the per-pixel form as its reference across 30
    rectangles × both rotations × all four levels × both inks, and every case was
    proved by MUTATION: forcing the unrotated branch for both rotations fails 275
    assertions, dropping the rotation's mirror term 143, keying the rotated branch
    on the wrong axis 153, an off-by-one row phase 320, and each of the four
    edge-mask failures 30–370. **The two rotation mutations are the ones nothing
    else can catch** — all 40 simulator PNGs are byte-identical across the change,
    and every one of them is `Rotation::None`.

**A READER PAGE WAS 99% GLYPH BLIT** — 213 ms of a 215 ms render, the same
per-pixel shape in `drawRunF26`, and the biggest single render cost in the
product because the reader is the screen a user spends their time on. `fill` on
that screen is 299 µs: a page draws almost no furniture.

**IT IS BYTE-WISE TOO NOW.** Clip the glyph box once; decide the plane **per
physical row** as a four-entry "which coverages ink" table, so `BwDithered`'s
Bayer phase stays keyed on absolute panel coordinates; hoist the glyph row
pointer; accumulate a byte of bits and skip bytes that ink nothing. Under CCW the
outer loop walks the glyph's **columns**, because a logical column is a physical
row — **`text.cpp` is the second routine in `core/` that has to know `Rotation`
exists**, for the identical reason as the veil and with the identical trap: it
passes every golden and smears on glass.

Cumulative, µs a pass at 528×792 (`-O3`), over both rewrites:

| screen | was | after `fill` | after `glyph` |
|---|--:|--:|--:|
| reader, one dithered pass | 474 | 474 | **144** |
| reader_menu | 1004 | 437 | **209** |
| library_actions | 706 | 351 | **192** |
| library | 277 | 219 | **113** |
| home | 187 | 137 | **74** |

**THE PROOF THAT MATTERS WAS NOT THE SUITE.** Every simulator screen was rendered
at both geometries against a binary built from the old blit — **36 of 36
byte-identical PNGs** — and the rotated screens verified as `rotate90CCW` of the
unrotated ones, 24 of 24, across all four planes. The reference-implementation
test was then proved by MUTATION: a rotated-row bug fails 860 assertions, the
dither phase 616, partial-byte alignment 1584. That pass also caught a bug in the
TEST, which had been filling the field with the ink's own colour so every
comparison was trivially true — a golden-shaped failure inside the thing checking
the goldens.

**WHAT IS LEFT ON THAT SCREEN IS THE PEN AND THE GLYPH CACHE**, not the blit:
`glyph()` and `measure()` are the remainder. If a page turn has to get faster
again, that is the target, and the note under **Opening a chapter** saying "the
blit is the target" has been spent.

**AND A µs FIGURE MUST SAY WHICH TREE PRODUCED IT.** `cmake -S . -B build` sets no
`CMAKE_BUILD_TYPE`, so a fresh tree builds `core/` at **-O0** and every number
above would be ~5× larger. The figures in this file are `-O3`/`-Os`; they differ
from each other by a few percent and from a default tree by a factor. A bench
quoted without its build type is not a measurement.

**`other` IS A REAL SLOT, NOT A ROUNDING ERROR.** It is the remainder — layout
arithmetic, measuring, wrapping, eliding — and on the Library it is 11–16 ms and
VARIES between renders of the same screen, which is elision measuring real
filenames rather than the boards' short demo titles. Not chased yet.

**And the desktop agrees, which is what makes `--bench` worth trusting for this.**
`reader_sim <screen> --bench N` installs the same profiler through a
`std::chrono` clock and reports the same slots: it called the actions panel 62%
fill against the device's 62%, and the reader page 97% glyph against 99%. So a
change to a drawing primitive can be judged before it is flashed. It does NOT
transfer to anything touching the card or the inflater — see the ratio warnings
above.

**And one dead button the timing exposed rather than the logic**: `UP` on the
Reader is `Gesture::AltPrev`, the way back, and answers `none()` with no way back
to promise — three presses in the run reported `paint=none`, two of them after waiting
out a count. It is correct behaviour and it reads as a broken device, which is a design
question (the hint bar advertises nothing there) rather than a performance one.
