# Sleep screens from book covers

**Status:** design approved 2026-08-29. Not implemented. Issue
[#11](https://github.com/Rukkaitto/encre/issues/11), Phase 5, `Kind = Screen`,
card in `Needs a board`.

**The card's `Source` reads `roadmap:1090` and that line is about character-name
detection.** The sleep-screen mention is `roadmap:1175`, in the Phase 5 list. The
pointer has drifted with edits to the roadmap; the card should be corrected to
`roadmap:1175` when this is picked up.

**Scope is covers only.** The card's title reads "Sleep screens from covers **or
user images**" and the user-image half is **cut from this spec**, not deferred
silently: it is a different feature — an import path, a place on the card for the
files, and a picker — sharing only the decoder. If it is wanted later it gets its
own card and its own board.

## What a cover actually is, measured before anything was designed

225 real EPUBs from `~/.cache/encre-corpus`, parsed for the cover their OPF
declares (`<meta name="cover">` first, `properties="cover-image"` second). Not
estimates — every number below is from that corpus, and the script that produced
them is worth rerunning before trusting any of it again.

| | |
|---|---|
| books declaring a cover | **225 / 225** |
| median cover | **1400 × 2100 = 2.94 MP** |
| largest | 3133 × 5000 = 15.66 MP |
| median compressed bytes | 246 KB (max 1.36 MB) |

**A cover is universal, and it cannot be held in memory.** 2.94 MP decoded to
8-bit grey is **2.9 MB**, against a free heap of 133 KB with no book open, 76,476 B
reading through Home's CONTINUE, and **42,152 B reading through the Library** — and
the ESP32-C3 has no PSRAM. The decoded image is 22× larger than everything the
device has, so this is not a tuning problem and no amount of care with a one-shot
decoder reaches it.

**Therefore every layer streams and downscales**, exactly as the reader's own six
layers do: JPEG one MCU row at a time, PNG one scanline at a time, box-filtered
straight down into a panel-sized destination, never holding the whole picture.

### What the encodings actually are

Encodings, over the 224 whose cover a stricter parser resolved — one book's
`container.xml` defeats it and needs the `*.opf` fallback the first pass used, which
is why the counts below total 224 and the table above says 225:

| encoding | of 224 | the user's own 16 |
|---|---|---|
| baseline JPEG | 183 (81%) | 12 |
| PNG, colour type 2, 8-bit, non-interlaced | 39 (17%) | 1 |
| **progressive JPEG** | 2 (1%) | **2** |

**Progressive JPEG matters more than 1% suggests** — it is 12.5% of the user's own
library, and no small streaming decoder handles it. It is a stated refusal, not a
bug to fix later.

Two more facts that set the memory budget, and neither was guessable:

- **59% of JPEG covers are DEFLATED inside the zip** (109 of 185), so the JPEG path
  usually needs a zip inflater — 36,956 B — on top of the image decoder.
- **38 of 39 PNGs are STORED** (method 0). So the common PNG case needs exactly
  **one** 32 KB window, its own, and not two. The single deflated PNG needs two and
  may refuse; see **Stated limits**.

JPEG subsampling is 177 × 4:2:0 and 8 × 4:4:4, no 4:2:2. Every PNG is colour type 2,
bit depth 8, non-interlaced, ≤ 1600 wide.

## What the reader gets

The sleep screen can show the cover of the book being read. Three modes, chosen in
Settings, and the mode also decides whether the badge is drawn.

| mode | panel | badge |
|---|---|---|
| `COVER` | cover full-bleed, nothing else | **hidden** |
| `COVER + DETAILS` | cover full-bleed, the NOW READING block over it | shown |
| `DETAILS` | today's screen exactly — dither field and the block | shown |

Default is `COVER + DETAILS`.

**With no cached cover every mode paints byte-identically to today.** That is the
property that keeps this change small: `sleep` and `sleep_idle` goldens do not move,
and flipping the default changes nothing on a device until a cover has actually been
decoded.

### The badge rule, and the written rule it overrides

CLAUDE.md says the badge **"is the load-bearing half and it stays"**, because e-ink
holds its last image and a screen left on the glass "gives no clue the device is
asleep rather than frozen". `COVER` mode hides it anyway, and the reason does not
generalise: **a full-bleed book cover is not a screen the device can otherwise be
in**, so it is unambiguous by itself, where a Library or a half-read page is not.

That argument holds only while a cover is genuinely on the glass, so the condition is
asked once and in one place:

> The badge is hidden **iff** a cover was actually painted **and** the mode is
> `COVER`.

Every fallback restores it. One predicate, not two conditions free to drift — this
project has shipped a dead button twice from exactly that shape.

### Fallbacks

In order, and none of them substitutes anything:

1. **No book open** → `SleepIdle`, badge alone. Unchanged.
2. **Book open, no usable cover** — never decoded yet, progressive JPEG, unsupported
   PNG variant, decode failed, card unreadable → **`DETAILS`**, badge shown.

The log names which reason. A factory that substitutes content is worse than one
that refuses, and this project has the woken-into-Middlemarch scar to prove it.

## The two decisions that were taken on pictures, not on argument

Both were made by rendering a real cover — *Gullible's Travels*, Standard Ebooks,
1400 × 2100 — at true panel size and looking.

**The cover sits BEHIND the card, it does not replace it.** A full-bleed cover with
no card was the recommended option and was rejected in favour of keeping both, with
the mode switch offering the cover-alone variant to anyone who wants it.

**Four grey levels, not one bit.** Hard thresholding — what `Plane::Bw` does today —
is **not an option at all**: the sky of a real cover collapses to solid black and the
picture is destroyed. That rules out shipping a cover through the current Sleep paint
path whatever else is decided. Of the three survivors, Bayer 4×4 is coarse,
Floyd–Steinberg at 1 bit is good, and four levels is better.

`Fidelity::Grayscale` is chosen, and this is the arrival of a question CLAUDE.md
parked deliberately: *"it is kept, not deprecated, because it is the only way to put
continuous tone on this glass — Phase 3's question about book covers and images."*
A cover is that question. **Sleep is also the one screen where the cost is free** —
three waveforms and a rebase, ~1363 ms against ~825 ms, on a panel about to sit idle
for hours with nobody waiting on the next press.

**BOTH HALVES OF WHAT THIS PARAGRAPH ORIGINALLY CLAIMED WERE FALSE WHEN WRITTEN**, and
the correction is more interesting than the claim. It said the sleep screen would be the
first to declare `Grayscale`, and that `golden::checkGoldenGray` "has never had a caller".
Neither is true: **`ReaderScreen` and `PeekScreen` both declare `Grayscale`**, and the
Reader golden has called `checkGoldenGray` since the Reader landed, the Peek golden since
the peek did. Both errors came from CLAUDE.md's own stale lines — "No screen declares it
today" and "nothing uses it today" — copied into this spec, then into the implementation
plan, then into a task prompt, and very nearly into a test comment asserting first-caller
status. **An inherited note is a claim with an expiry date**, and these had expired twice
over before anyone repeated them. Both CLAUDE.md lines are corrected now.

What *is* true is narrower and is the part that matters: **`SleepScreen::fidelity()` is the
first NON-CONSTANT one in the firmware.** Every other override returns a literal; this one
answers `Grayscale` only when a cover is actually painted and `Mono` otherwise, which is
what keeps `DETAILS` on today's single waveform and today's exact pixels.

## Fitting a cover to a panel that is not its shape

Measured over 224 covers with usable dimensions:

| panel | median crop loss at `FILL` | covers needing no crop |
|---|---|---|
| **X3** 528 × 792 (2:3 exactly) | **0.0 %** | **162 / 224** |
| **X4** 480 × 800 (3:5) | **10.0 %** | 3 / 224 |

**Which axis is cropped, stated because leaving it implicit cost real work.** A 2:3
cover is 0.667 and the X4 is 0.600, so the cover is *relatively wider* than the
panel: `FILL` crops **width** and keeps the full height, and `WHOLE` fills the width
and leaves bands **above and below**. The implementation plan asserted both the
other way round for a while and nothing disagreed with it, because the loss is 10.0%
whichever axis you measure — 0.600/0.667 is 0.9 either way. The number was right and
the axis was unstated.

163 of 224 covers are 2:3, so **on the dev device this is a no-op for 73% of books**
and it is very largely an X4 concern. The tail is what justifies a control: the
squarest cover in the corpus is 877 × 973 and `FILL` cuts its title off at both
edges — a 33.4% loss.

`WHOLE` letterboxes instead, and **the bands carry the level-1 clustered tint
`Sleep.dc.html` already uses as its field**, not white. White bands read as
unpainted glass — the "did it fail to draw?" reading — where the tint reads as the
sleep screen's own background, and it is a pattern already on the board rather than
a new one.

## Settings

The screen gains a section, not just rows:

```
READING          Typography
SLEEP SCREEN     Shows       COVER + DETAILS      <- new
                 Cover fit   FILL                 <- new
DEVICE           Sleep after / Full refresh / Refresh on screen change
```

- **The row is `Shows`, not `Sleep screen`.** The board already states the rule:
  `READING` was chosen over `TYPOGRAPHY` so a section "does not repeat the row's own
  word directly above it". Under a `SLEEP SCREEN` header it reads as a sentence —
  *sleep screen · shows · cover + details*.
- **`Sleep after` does not move.** It governs the timer, not the screen, and the two
  refresh rows stay adjacent for the reason CLAUDE.md records — a reader hunting for
  "why does my screen flash" should find both answers together.
- **Nine items; eleven fit**, so no rail appears. `renderSettings` reads
  `totalRows > rows`, so nothing has to change when it eventually overflows.
- **`Cover fit`'s focusability is DERIVED, not tabulated**: focusable iff the current
  mode shows a cover. That is `Typography`'s own precedent — `Font` is unreachable
  while one body face is vendored and becomes reachable the moment a second lands. In
  `DETAILS` mode the focus skips it, which is this screen's standing rule that a row
  which cannot act is never selectable.
- **Values are `FILL` / `WHOLE`, deliberately not `FILL` / `FIT`** — one letter apart
  is bad at 25 px on this glass, where `WHOLE` is unmistakable at a glance.
- **`kSettingsVersion` does NOT move.** An added field takes its default from an
  older file, and both defaults are chosen so an existing card behaves as it does
  today until a cover exists. Typography set this precedent.

**This closes the last inert row on the screen.** `{"Sleep screen", Field::None,
false, false, "BOOK COVER"}` — drawn, focus-skipped, carrying a board placeholder —
is what CLAUDE.md ties to this issue by number. After this, `Item::placeholder` has
no producer left; a test should assert that, so the field's redundancy is a stated
fact rather than an assumption, exactly as `ListRow::trackingEm1000` is handled.

## Architecture

### Layers, each ignorant of the next

| layer | holds | does NOT know |
|---|---|---|
| `jpegd.h` | baseline JPEG → 8-bit grey rows, bounded scratch | scaling, panels, EPUB |
| `pngd.h` | PNG ct 0/2/4/6, bd 8, non-interlaced → grey scanlines, over `inflate_stream.h` | scaling, panels, EPUB |
| `imagefit.h` | box-filter downscale + Floyd–Steinberg to 4 levels → two 1-bit plane rows | file formats |
| `cover.h` | locate the cover in an `OpenedBook`, drive the above, emit planes to a sink | screens, sleep, filesystems |

All of it in `core/`, so all of it is desktop-testable against real EPUB bytes
through `fake_fs.h` — this project's standing answer to "`shell/` has no harness",
and the reason `book.h` lives in `core/` already.

### Vendor TJpgDec for JPEG; write the PNG ourselves

The asymmetry is principled rather than convenient. **The reasons this project wrote
its own DEFLATE do not apply to TJpgDec**: stb's zlib was rejected for a one-shot API
and a 6,608-byte single stack frame, and TJpgDec already has the memory model wanted
here — ~3.5 KB workspace, an MCU output callback, and free ½/¼/⅛ scaling out of the
IDCT. **Meanwhile PNG is DEFLATE plus per-row unfiltering and we already own the hard
half**, so vendoring a second decoder to get ~200 lines of unfiltering would be the
worse trade.

JPEG's edge cases are numerous and a wrong upsample is a *subtly wrong picture*
rather than a crash — the hardest kind of defect for this project's tests to catch,
which is the strongest argument for battle-tested code on that side.

**Decode at the largest TJpgDec scale that still exceeds the panel, then box-filter.**
A 1400 × 2100 cover decoded at ½ is 700 × 1050, roughly a quarter of the IDCT work,
and still comfortably above 480 × 800.

**The repo has no LICENSE file.** TJpgDec carries a retain-the-notice condition;
vendoring it means keeping its header intact, as `stb_truetype.h` is kept.

### Memory

Peak, worst realistic case (a deflated JPEG):

| | bytes |
|---|--:|
| zip entry inflater (`Inflater::Scratch`) | 36,956 |
| TJpgDec workspace | ~3,500 |
| **TJpgDec MCU band buffer** | **8,400–16,900** |
| destination row accumulator (528 × 4) | 2,112 |
| Floyd–Steinberg error row | 2,112 |
| two 1-bit plane row buffers | 132 |
| **total** | **~54–62 KB** |

**MEASURED 2026-08-29, once `decodeCover` existed to measure** — live bytes across one
call, nothing held afterwards:

| case | peak | largest block |
|---|--:|--:|
| stored JPEG, 480×800 | 26,596 | 11,840 (the MCU band) |
| **deflated JPEG, 480×800** | **63,560** | 36,956 (the zip window) |
| stored PNG, 528×792 | 58,730 | 36,956 |
| **deflated PNG, 528×792** | **95,686** | 36,956 × 2 |

So the estimate above was right in shape and ~1.6 KB low. The deflated JPEG — the
**majority** case, since 59% of JPEG covers are deflated — is 63.6 KB against ~87 KB
free, about three quarters of the headroom. **The deflated PNG does not fit on the
device**, which confirms the stated limit rather than contradicting it: it is 1 of 225
corpus books, it answers `OutOfMemory`, and it falls back to the card.

**That refusal is DEVICE-ONLY, and the desktop cannot show it.** A 64-bit host serves
both 36,956-byte windows without complaint, so `tools/covers.py` will count that book
`Ok`. The corpus report is therefore a measure of *format support*, not of what fits —
the only instrument for the second is the device. Do not read a clean corpus run as
evidence that every book's cover will appear.

Reporting a shortfall **as** a shortfall took a change rather than a reword: both
decoders now carry `outOfMemory()` beside `aborted()`. Before it, a failed inflate
window arrived as `ReadFailed` — a card fault that had not happened — because
`PngDecoder` asks the sink before taking its window, so the sink was already declared
by the time the allocation failed. The shell branches on that word.

**CORRECTED 2026-08-29, from reading the vendored source rather than its summary.**
This table first omitted the band buffer and read ~45 KB. `jd_decomp` emits **MCU
rectangles**, typically 16×16 at 4:2:0 (177 of the 185 corpus JPEG covers), so a row
is not complete until its whole band has arrived and one band — `mcuHeight ×
outputWidth` — must be held. That also settles the decoder's shape: `jd_decomp`
decodes the whole image in one call, so a pull interface would need control
inversion and **both decoders push rows into a sink instead**. The margin against
~87 KB is now ~25 KB rather than ~42 KB, which is still comfortable but is no longer
something to spend without checking.

**Painting costs no extra RAM at all**, which is what makes four levels affordable at
the 42,152-byte reading floor: each pass reads one plane straight into
`Framebuffer::data()` and the chrome draws over it.

### The cache

One file, `/.reader/sleep.cover`: a header — magic, version, panel geometry,
rotation, book path, `bookBytes` — then two 1-bit planes as **logical raster rows**.

**CORRECTED 2026-08-29: this said "physical store layout", and that was wrong on the
device and right on the desktop** — the worst way to be wrong. The shell binds
`Rotation::Ccw`, under which `byteIndex` maps logical *(x, y)* to physical
*(physX = y, physY = width − 1 − x)*, so **one logical row is one physical column**.
A streaming row-major downscale can only emit logical rows, so a file of physical
rows cannot be produced at all. Getting a row onto the frame is therefore a strided
scatter, not a `memcpy` — `Framebuffer::writePackedRow` — and it lives in `core/`
because `shell/` has no harness and because **the simulator and every golden are
`Rotation::None`, where the two branches agree**. A version that always memcpy'd
would pass the whole desktop suite and smear on glass, exactly as CLAUDE.md records
for the veil, `fillRect`, the glyph blit and `ditherRect`.

**Two planes serve three passes.** `Plane::Bw` inks where coverage ≥ 2, which is
exactly "MSB set", so the `Bw` base pass and the `Msb` pass read the **same** plane.
104,608 B on the X3.

**One file rather than one per book**, scoped exactly like `last.json`, because the
sleep screen only ever shows the last-read book. That makes cache eviction — its own
Phase 5 card — a non-problem by construction rather than a thing to remember. The
cost is stated: alternating between two books re-decodes on each sleep.

`bookPath` and `bookBytes` are the identity check, the same pair and the same
reasoning as the reading sidecar: the cheapest check a `FileSystem` with no
timestamps and no hashes can offer, and `DirEntry` already carries the size.

### Reading and writing use different mechanisms, on purpose

**Writing is a shell free function using SdFat directly.** 104 KB cannot go through
`writeAll`, which takes a whole buffer, and the contract has no write handle.
CLAUDE.md's precedent is explicit — `appendToCard` is a free function in `shell/`
rather than a `FileSystem` method precisely so a 27-clause contract driven by two
harnesses does not widen for one caller.

**Atomicity without a `rename` the contract does not have:** write the header with
`complete = 0`, stream the planes, then seek back and write `complete = 1`. A
half-written file — an abandoned decode, a power loss — is never mistaken for a good
one, and is simply re-attempted at the next sleep.

**Reading goes through `FileSystem::openRead`**, which the contract already has and
which is already random-access with refused-past-the-end seek semantics.

### `SleepScreen` gets a `CoverSource`

An abstract `bool loadPlane(Plane, Framebuffer&)` — the same shape as `SettingsSink`,
for the same reason. The shell implements it over the card, the simulator over
`HostFileSystem`, the tests over a fake. `core/` never learns what a filesystem is,
and the screen keeps ownership of composition order and of the clear-versus-cover
decision: **a cover replaces the clear**, and only a failed load falls back to
`clear(true)`.

## The sleep path

The decode happens **on the sleep path, after the first paint**. Three approaches
were weighed and the reasoning is worth keeping, because the two rejected ones look
attractive.

```
sleepNow():
  saveReadingPosition("sleep")
  paintSleepScreen()                  // cache if valid, else DETAILS
  if (mode wants a cover && cache invalid for this book):
      reader->releaseChapter()        // EXISTS -- the peek built it
      ok = decodeCoverToCache()       // interruptible on rawSamplesPending()
      if (ok) paintSleepScreen()      // second FULL waveform, now with the cover
  markSleeping(); logFlush(); deepSleep()
```

**Sleep is the only moment in this firmware where freeing everything is free.** Deep
sleep is a chip reset, so nothing needs to survive — which is what makes ~87 KB
available at the one moment a decode wants it, and what makes releasing the live
chapter safe rather than a state bug.

**Nothing new is needed to free the heap: `ReaderScreen::releaseChapter()` already
exists and already does exactly this**, built for the peek, which needs the same
36,956 bytes for the same reason. Reuse it rather than writing a second release —
and the reason that matters is a trap CLAUDE.md records verbatim. **`inflater_` is a
VALUE member**, not a `unique_ptr`, and the window lives behind its private
`Scratch* s_`, so an "obvious" release that drops the `BlockReader`, the
`InflateSource` wrapper, the buffer view and the file handle frees **none of the
bytes this path exists to get**. `Inflater::release()` is the load-bearing call, and
**`ChapterReader::inflateWindowHeld()` is the only observation point that can see the
difference** — `held()` and `bytesRead()` both go false either way.

`reacquireChapter()` is deliberately **not** called afterwards. There is nothing to
come back to: the next statement is `deepSleep()`, which is a chip reset.

**The sleep screen refines, exactly as a reader page does.** The card is painted
first because it is correct and honest immediately; the cover arrives on a second
waveform a few seconds later. The user has pressed power and walked away, so the
first sleep of a new book still *ends* with the cover on the glass. Any button press
abandons the decode and sleeps at once.

### Rejected: the idle window while reading

The obvious home, and it fails on a property the other idle jobs do not have.
`completeIndex` can be abandoned cheaply because it builds into a scratch vector and
commits on completion. **A cover decode cannot be resumed** — that would mean
persisting the decoder state, the box-filter accumulator and the diffusion row, and
baseline JPEG offers no restart points to hang them on. So every attempt starts from
byte zero, and against the device's own measured reading pattern — median 72 ms
between page turns, longest pauses 898 ms and 1360 ms — an actively reading user
would trigger it rarely and **abandon it every time**, burning SD reads and CPU on
each attempt and quite possibly never finishing.

It would also occupy exactly the quiet the grayscale refinement waits for
(`kRefineQuietMs` 5000 ms, 1408 ms and uninterruptible), leaving the page dithered
longer — a regression on the screen the user actually spends time on. And PNG could
never run there at all: a second 32 KB window against the 42 KB floor is the ToC
allocation failure verbatim.

### Rejected: synchronously at book open, behind `OPENING…`

Genuinely viable — `kStatusOpening`, `drawStatusBar` and `design/LibraryOpening.dc.html`
all already exist, so there is no new affordance to design, and heap is at its most
plentiful before the `ReaderScreen` is built. Rejected because **it charges every
book you open**, including one opened, glanced at and backed out of, turning a
1753 ms Library → Reader into 4–7 s. The sleep path charges only books actually slept
on, and charges them where no one is waiting.

### Rejected: decoding before the first paint

Same work, but the user presses power and nothing happens for several seconds, which
reads as a hang — and this project already records that "a frozen screen does not
mean the firmware ran".

## Boards

`Sleep.dc.html` and `SleepIdle.dc.html` are **unchanged** — they are `DETAILS` mode.
New: `SleepCover.dc.html` and `SleepCoverDetails.dc.html`. `Settings.dc.html` gains
the `SLEEP SCREEN` section and its two rows.

**`make compare` cannot compare a dithered photograph, and pretending otherwise
would make the number meaningless.** Chrome cannot Floyd–Steinberg, so a
hand-authored board cover would mismatch the firmware across the whole image and the
percentage would measure the rasteriser rather than the design. Instead a `tools/`
generator produces the dithered cover as a **committed asset** from a fixture EPUB,
and both the board and the golden use that one file. The comparison then measures the
chrome around the cover — the part that can actually drift — which is the honest
thing for it to measure.

This is the same principle as `iconc.py` reading each icon's SVG from its named
board: **generated from one source, never transcribed twice.**

## Testing

- **The decoders are validated against `stb_image`**, already vendored and already
  desktop-only, over the corpus's real covers. This is the move that validated
  `inflate_stream` against the one-shot decoder — 216 entry-passes, zero
  disagreements — and it is available here for the same reason.
- **The streaming downscale-and-diffuse keeps a naive whole-image form as its
  reference** and asserts byte-identity, which is how `test_dither.cpp` and
  `test_framebuffer.cpp` are built. Both rotations, both panel geometries, and runs
  that start and end mid-byte.
- **A corpus run over all 225 covers reporting refusals** — a repeatable measurement
  in the style of the committed manifest, so "which books get a cover" stays a number
  rather than an anecdote.
- **Goldens:** `sleep_cover` and `sleep_cover_details` at both geometries, through
  `golden::checkGoldenGray`. `sleep` and `sleep_idle` must be **unmoved** — that is
  the check that proves the default changes nothing without a cover. `settings`
  re-blessed for the new section.
- `test_focus_restore.cpp`'s hand-maintained `movable` and `wrapping` counts change
  with the Settings rows; #42 is the related `static_assert` weakness.
- **Every new test proved by mutation**, and `git commit` **before** mutating —
  never `git checkout` to undo one, which reverts the change under test as well as
  the mutation.

## What the corpus actually yields, measured with the built pipeline

`tools/covers.py`, over the 225-book manifest, at both geometries — identical results on
each:

| | count |
|---|--:|
| `Ok` | **222** |
| `Unsupported` — both progressive JPEGs | 2 |
| the book never reaches a decoder | 1 |
| `NoCover` / `ReadFailed` / `OutOfMemory` / `Abandoned` | 0 |

**224 books open, and 222 of them give a cover.** The 225th is refused by `openBook`
before any of this runs — a Calibre `user_metadata` `<meta content="…">` of **849 bytes**
against `Xml::kMaxAttrBytes`'s 512, which CLAUDE.md already records as a known refusal
under `Epub::open`. It belongs to the EPUB refusal rate, not to cover support, and
**"222 of 225" would double-count it.**

Desktop decode: median 76.6 ms, slowest 200.4 ms. **Not predictive of the device** —
this project has been wrong by 8× that way once already.

## Stated limits

| limit | incidence |
|---|---|
| progressive JPEG refused | 2 / 225 — but 2 of the user's own 16 |
| deflated PNG may refuse on heap | 1 / 225 |
| interlaced or palette PNG refused | 0 / 225 observed |
| one cached cover; alternating books re-decode | by design |
| X4 crops 10% of a 2:3 cover at `FILL` | default, reversible in Settings |
| first sleep of a new book shows the card for a few seconds | by design |

Every one falls back to `DETAILS` with the badge shown, and logs the reason.

## What a cover costs on the device — MEASURED 2026-08-29, X3

This section replaces "the number this design turns on, and does not yet have". Four
corpus books spanning the cases, decoded at boot by `ENCRE_COVER_PROBE`, panel 528×792:

| cover | zip | source | scale | decode | peak heap |
|---|---|---|--:|--:|--:|
| JPEG | deflated | 1400×2100 | 1/2 | **3,074 ms** | 81,088 |
| JPEG | stored | 1424×2048 | 1/2 | **2,987 ms** | 81,560 |
| PNG | stored | 1600×2400 | 1/1 | **6,919 ms** | 81,796 |
| PNG | deflated | 601×918 | 1/1 | **6,648 ms** | 120,248 |

**The decision: PROCEED.** JPEG is ~3.0 s and is 81% of covers; PNG is 6.6–6.9 s, inside
the "proceed but the cover lands late" band; nothing approaches the 15 s that would have
sent this back to the book-open path.

**PNG is 2.2× slower than JPEG on a SMALLER image**, which is the asymmetry the gate was
split to see: JPEG gets TJpgDec's free IDCT halving, PNG has no scaled inflate and walks
every source pixel. A single number would have been a JPEG number.

### The heap is what the gate actually caught

**Every case peaked 17–25 KB ABOVE its desktop measurement** — 81,088 against 63,560 for
the deflated JPEG. That is this project's ratio trap in a third disguise: not "the desktop
is faster", not "the desktop has an instruction the device lacks", but simply a different
allocator. **A desktop heap figure is not a device heap figure, and this one under-predicted
in the dangerous direction.**

Against that, releasing only the chapter at sleep — which is what the implementation plan
originally said — leaves:

| the book was opened via | free at sleep | margin over an 81 KB peak |
|---|--:|--:|
| Home → CONTINUE | ~121 KB | ~40 KB |
| **the Library, 203 books** | **~87 KB** | **~6 KB** |

Six kilobytes, on the commonest way to open a book. And the failure would have been
**silent**: `decodeCover` answers `OutOfMemory`, the sleep screen falls back to the reading
card, and nothing looks broken — it would have read as "covers don't work for some books"
and never been reported as a defect.

**So the sleep path releases the whole `App`, not just the chapter.** This section's own
sentence already said it — *sleep is the only moment in this firmware where freeing
everything is free* — and the plan under-implemented it. After the first sleep paint
nothing needs the `App`: the session record was written at navigation time, not here;
`paintSleepScreen` bypasses `App` by design; and the next statement is a chip reset.
Dropping it returns the Library's ~59 KB and takes the margin to **~65 KB**.

It may also make the deflated PNG work rather than refuse — 120 KB against ~146 KB —
which would narrow the stated limit rather than widen it. Not claimed until measured.

**One incidental correction:** the device reported **173 KB free at boot**, where CLAUDE.md
documents ~133 KB.


## Board card

#11 goes to `Building` before the first commit and stops at **`On glass`**. No
`Closes #11`. The panel can be wrong about every visual decision here — this repo has
shipped an overlay painted on white, a veil that smeared diagonally under rotation
and a function that could only recurse, all with a green desktop suite — and this
change also introduces the first `Grayscale` screen, so desktop evidence must not
close it.

**What only the glass can answer:** whether four levels on a dithered cover reads as
a photograph or as noise; whether `COVER` mode without a badge reads as *asleep*;
what the decode actually costs; whether the two-paint refinement reads as deliberate
or as a glitch; and whether `FILL`'s 10% crop on the X4 is acceptable as a default.
