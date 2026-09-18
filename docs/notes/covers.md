# Covers

Extracted from `CLAUDE.md`, which keeps a stub under this heading and is where
the cross-references to it point. Same standing as anything in that file.

The sleep screen can hold the open book's cover (#11). `SleepCover.dc.html` is the
cover alone, `SleepCoverDetails.dc.html` is the cover with the reading card and the
badge over it, and `Sleep.dc.html` is the card on paper that shipped and is still the
default. Settings' `SLEEP SCREEN` section picks between them — `Shows`
(COVER / COVER + DETAILS / DETAILS) and `Cover fit` (FILL / WHOLE).

**IT IS ON GLASS, AND THE QUESTION IT TURNED ON IS ANSWERED: A FOUR-LEVEL COVER READS
AS A PHOTOGRAPH, NOT AS NOISE.** Confirmed on the X3, 2026-08-29, with no ghosting of
the card's text under the picture. That was the decision deferred to the panel when
`Grayscale` was chosen over 1-bit Floyd–Steinberg, and it went the way the design
assumed — which is worth recording precisely because this project has been wrong about
this panel from desktop evidence three times.

**What the glass also corrected, and it is the sharper half:** the heap. A deflated
JPEG peaks at **81,088 bytes on the device against the desktop's 63,560** for the same
work — every case measured 17–25 KB above its desktop figure, because the allocator is
simply different. That is the ratio trap in a third disguise, after time-on-the-card and
`__divdi3`. See **Sleep releases the whole `App`** below for what it cost.

**Two things remain untested on glass** and are honest gaps rather than oversights: the
**one-bit cover the WAKE paints** (the Msb plane is a threshold *through* an already
dithered picture, and hard thresholding a photograph is exactly what this file warns
about), and whether the badge slicing a cover's own title band is tolerable.

### A cover is universal, and it can never be held

225 real EPUBs from `~/.cache/encre-corpus`, parsed for the cover their OPF declares
(`<meta name="cover">` first, `properties="cover-image"` second):

| | |
|---|---|
| books declaring a cover | **225 / 225** |
| median cover | **1400 × 2100 = 2.94 MP** |
| largest | 3133 × 5000 = 15.66 MP |
| median compressed bytes | 246 KB (max 1.36 MB) |

2.94 MP decoded to 8-bit grey is **2.9 MB**, against 133 KB free with no book open,
76,476 B reading through Home's CONTINUE, and **42,152 B reading through the
Library** — on a part with **no PSRAM**. The decoded image is **22× larger than
everything the device has**, so this was never a tuning problem and no amount of care
with a one-shot decoder reaches it.

**So every layer streams and downscales, exactly as the reader's six do.** JPEG one
MCU row at a time (`jpegd.h`, over vendored TJpgDec); PNG one scanline at a time over
our own `inflate_stream.h` (`pngd.h`); box-filtered straight down into a panel-sized
destination (`imagefit.h`); `cover.h` drives the chain. **The output is PLANES, not an
image** — the picture never exists anywhere in one piece.

**TJpgDec is vendored and the PNG decoder is ours, and the asymmetry is deliberate.**
JPEG's edge cases are numerous and a wrong upsample is a *subtly wrong picture* rather
than a failure, which is the worst kind of bug to own; PNG is DEFLATE plus per-row
unfiltering and **we already own the hard half**, so vendoring a second decoder to get
~200 lines of unfiltering would buy nothing.

### Two planes serve three passes

`Plane::Bw` inks where coverage ≥ 2, which is **exactly "MSB set"** — so the grayscale
base pass and the `Msb` pass ask for the *same* plane. That identity is what makes
four levels affordable at the 42,152-byte reading floor: the cache is two planes'
worth of bytes rather than three, and **each pass is one blit straight onto the frame,
so painting a cover costs no extra RAM at all**. Holding a 2 bpp image of a 480×800
panel would be 96 KB, which does not exist.

`CoverSource` (`screen_sleep.h`) is an interface rather than a buffer for that reason,
the same shape as `SettingsSink` and for the same reason — `core/` never learns what a
filesystem is. An implementation that answered `Bw` with anything other than its `Msb`
plane would give a base pass that disagrees with the refinement painted over it.

**A `false` FROM `loadPlane` DOES NOT PROMISE THE FRAME IS UNTOUCHED**, and pretending
otherwise would be a contract no streaming implementation can keep — it finds out the
card is gone half way down the picture. What makes that safe is the *caller*:
`renderSleep` clears and draws the dither field whenever the load refuses, so a
partial write is overwritten rather than shown.

### The cache holds LOGICAL rows, and a `memcpy` would pass the whole desktop suite

**IT IS NOT A `memcpy` INTO `data()`, AND THAT IS THE HALF THAT IS ONLY WRONG ON THE
DEVICE.** The cache holds **logical raster rows**, because a streaming row-major
downscale can emit nothing else — `imagefit.h` produces destination row *n* and then
destination row *n+1*, and it has no picture left to transpose. The shell binds
`Rotation::Ccw`, under which **one logical row is a physical COLUMN**. So the blit
goes row by row through `Framebuffer::writePackedRow`, which is **the one function in
this feature that knows `Rotation` exists**.

**Under `Rotation::None` that reduces to the `memcpy` and the difference is
invisible** — and `Rotation::None` is the entire simulator and every golden. So the
wrong version passes `make test`, passes every golden at both geometries, passes
`make compare`, and smears diagonally on glass. **This is the fifth time this project
has met that hazard**: `veilRect`, `Framebuffer::fillRect`, the glyph blit in
`text.cpp` and `ditherRect` each took the same structure for the same reason and each
carries the same warning. Nothing on the desktop stands between that mistake and the
panel except a test written to run under both rotations.

### A software 64-bit division, found by reading the assembly

The obvious spelling of `CoverFitter::addRow`'s column map (`imagefit.cpp`) is
`(int)((long long)j * dstW / srcW)`, one per source pixel. On x86-64 that is a
hardware `idiv` and **benchmarks at 3.19 ms a cover against 3.19 for the form that
shipped — no difference at all**. **RV32IMC HAS NO 64-BIT DIVIDER.** Compiled with the
project's own toolchain (`riscv32-esp-elf-g++ -Os`) that line emitted `mulh`/`mul` and
a **`call __divdi3` inside the per-pixel loop body** — a libgcc shift-subtract routine,
~100–200 cycles, run once for every source pixel. A median cover cropped to the X4 is
~2.65 M source pixels: **1.7–3.3 s at 160 MHz, in one line**.

It is stepped instead, carrying the remainder, and **bit-identical rather than
approximate**: `dstW <= srcW` means the quotient advances by 0 or 1 per pixel, so the
carry reproduces the floor exactly. Verified across **113,388 assertions — not one
output bit moved**, which is the standard a "faster and equivalent" claim has to meet
here. The *row* map keeps its divide, because it runs once per source ROW; a CFG cycle
analysis put **0 of the surviving `__divdi3` calls in a loop body**, which is the check
worth repeating rather than the count.

**THE HABIT IS THE POINT, NOT THE INCIDENT.** This is the desktop-to-device ratio trap
in its sharpest form yet: the ratio here is not 37× or 135×, **it is infinite, because
the desktop cost is zero**. A desktop benchmark cannot see an instruction the target
does not have. **When a hot loop is about to run millions of times on the device,
cross-compile it and read the assembly** — `riscv32-esp-elf-g++ -Os -S`, then grep for
`call`. A `call` in a leaf arithmetic loop is a compiler-emitted software routine and
is always worth a look; `__divdi3`, `__udivdi3`, `__moddi3` and the soft-float family
are what to expect on a part with no FPU and a 32-bit divider.

### A small cover is enlarged — to a measured ×2, then to an overridden ×2.5 (#64)

**`fitCover` USED TO NEVER UPSCALE, AND `imagefit.h` STATED THAT AS A PROPERTY RATHER
THAN A TASTE.** Every word of the argument was true — a box filter's support is the
destination pixel's footprint, which when enlarging is *smaller* than a source pixel, so
area-averaging an enlargement is nearest-neighbour however it is spelled; and
one-source-row-to-one-destination-row streaming cannot complete two rows from one push.
**What was written down is what the reader saw as a defect**: a 260×346 cover sat on the
X3's 528×792 as a small picture covering **22% of the glass**, for hours, and
`design/SleepCover.dc.html` says full-bleed. That state matched no board at all — it drew
neither the picture nor the reading card.

**THE CAP WAS MEASURED TWICE AT ×2, AND THE SHIPPED CAP IS ×2.5 — AN OWNER OVERRIDE OF
THAT DERIVATION AND NOT A CORRECTION OF IT.** Both halves have to be read together, and
they are kept apart on purpose: the measurements below bound **200**, they are unchanged
and nothing has falsified either, and `kMaxCoverUpscalePercent` is **250**. Restating the
derivation under the larger number — letting the argument for 200 read as though it had
produced 250 — is the comment-drifted-from-code defect this file records over and over,
and the figures are kept whole so the constant can be moved **back** with evidence.
Both measurements are against the smallest structure this glass carries. Nearest-neighbour
replication at scale *k* introduces structure of period *k* pixels, so the question is
where that stops being absorbed:

- **THE PIPELINE'S OWN GRAIN, off the shipped `CoverFitter`.** A flat field at each of the
  three level midpoints — grey 42/43, 127/128, 212/213, the tones four levels carry worst
  and therefore the patterns with the most contrast — comes out of `emitRow` as a run
  length of **exactly one**, which is a period-**two** alternation. Over all 234 greys that
  need a pattern at all the mean run is **3.20 px**, so 2 px is the floor of that
  distribution and its highest-contrast end. **That picture is confirmed on the X3 to read
  as a photograph** (2026-08-29, at the head of this section), so 2 px is structure this
  panel is *known* to accept.
- **THE PROJECT'S OWN LEGIBILITY FLOOR, already in the repo.** "Below ~10pt is not legible
  on this glass, measured"; 10 pt at 150 DPI is a 21 px ppem whose stem is ~2 px, and the
  whole `Mono` argument is about "a 2px stem fully inked". 2 px is the smallest structure
  this project has measured as carrying meaning here.

Two independent measurements landing on the same number is what makes **200** a derivation
rather than a pick. At *k* ≤ 2 the introduced structure is no coarser than the dither grain
beside it and is absorbed into the diffusion; **at *k* = 2.5 it is not** — a source pixel
becomes a run of **2 or 3** destination pixels, mean 2.5 — so past 200 the sufficiency of
nearest-neighbour is **assumed rather than measured**. That is the stated cost of the
override. Anything smoother needs a reconstruction filter wider than the destination
pixel — real interpolation, a new hot loop, ~520 bytes of held source rows — and that is
the named next step if the glass says the replication reads blocky, which is now a
sharper question than it was at ×2.

**WHY IT WAS RAISED, WHICH IS A DECISION AND NOT A FINDING.** The reporting book —
`Walden ou la vie dans les bois`, 260×346 — asks **×2.29** on the X3 and **×2.31** on the
X4, so 200 refused it and the sleep screen showed the reading card. Both states are
boarded, and the owner's call is that a **soft full-bleed cover beats a card**:
`SleepCover.dc.html` draws a picture, and the card is what the screen falls back to when
there is *none*. The refusal was not a wrong answer, it was the *derived* answer, and it
was overruled on a judgement no measurement in this repo can make. All four of that
shape's combinations are now served, verified through the real `decodeCover` at both
panels: `Ok`, `dst=528×792+0+0` and `480×800+0+0` at `FILL`, `528×703+0+44` and
`480×639+0+80` at `WHOLE`, against the pre-#64 binary's `231×346+148+223`.

**WHAT THE CORPUS SAYS AND WHERE IT IS THE WRONG INSTRUMENT.** Run through the real
`decodeCover` at both panels: 223 of 225 covers have dimensions that parse, and the worst
enlargement any of them asks for is **×1.32** (400×662 on the X3). So **the cap admits
every corpus cover** — `tools/covers.py` reports 223 `Ok` and **0 `TooSmall`** at both
geometries — and the change fills the panel for the **3 (X4) / 4 (X3)** covers that used
to sit centred.

**AND THAT IS WHY THE RAISE TO ×2.5 MOVES NO CORPUS COVER AT ALL.** 200 already admitted
every one of them, so the corpus reports the identical 223 `Ok` / 0 `TooSmall` at both
caps and **all 892 cover renders are byte-identical across the change** — measured, not
argued, by keeping both runs' PNGs. Against the pre-#64 binary the count is unchanged at
either cap: **10 of 892 renders differ** (7 `FILL`, 3 `WHOLE`, four distinct books), which
is exactly the 3/4-per-panel set #64 itself moved. So the corpus has nothing to say for or
against the override, **which is the point rather than a gap**: the case it is for is the
one the corpus does not contain. 260×346 is far smaller than anything in it, the corpus
under-counts this the way it under-counted #35, and **both of the books #35 made openable
are small-cover cases**, so that fix raised this one's incidence.

**A REFUSAL IS `CoverResult::TooSmall`, WHICH IS A SIXTH VALUE AND NOT THE NEAREST
EXISTING ONE.** `Unsupported` is "not an image we read" and this is an image we read
perfectly; `OutOfMemory` is the false `CoverFitter::begin` used to answer, and nothing is
wrong with the memory. Both would be a log line asserting something untrue about a book —
the shape this file already refuses for an unread gauge (`-1`, never `0%`) and for a badge
promising a wake charging cannot deliver. `CoverReport` carries the **1:1 box the cover
would have occupied**, because the reason is a fixed sentence and the geometry is the half
that says by how much it missed.

**NO BOARD CHANGED, AND THAT IS THE ARGUMENT FOR THIS SHAPE.** Both outcomes are already
boarded — full-bleed (`SleepCover.dc.html`) or the reading card (`Sleep.dc.html`) — so the
firmware moved *toward* a board it had been failing rather than a board moving toward it.
The small centred picture was the only unboarded state and it is gone.

**THE INTERFACE HAD TO WIDEN, AND IT WIDENED HONESTLY.** `addRow(src, bool& emitted)` is
now `addRow(src)` plus `nextRow()`, drained in a loop. A bool can say "zero or one"; an
enlargement completes several rows from one push, and a contract promising "at most two"
would have been true only while the cap happened to be 200% — **which it stopped being one
ticket later**, so the honest shape earned itself faster than expected. The one misuse it
introduces —
pushing with rows still pending, which would blend two source rows into one accumulator —
is **refused rather than silent**, and no downscale can reach it, which is why every
shipped caller changed by exactly one `if` becoming a `while`.

**THE ACCUMULATE PATH IS A SECOND PATH AND NOT A SECOND COPY.** A downscale is a forward
**scatter** (walk the source, add each pixel to the cell it lands in) and an enlargement is
an inverse **gather** (walk the destination, read the pixel it sits on): different
operations over one accumulator, with the mean, the diffusion, the packing and every guard
shared. The forward form is kept **verbatim** rather than generalised, because a gather
with the same boundaries rounds its cell edges the other way and would move a byte of
every cover the device has ever drawn.

**AND THE ROW MAP HAD TO CHANGE WITH IT — THE DEFECT THIS NEARLY SHIPPED.** The columns
gather with `floor(c·srcW/dstW)`, so the rows must say the same thing: source row *i* owes
the destination rows *r* with `floor(r·srcH/dstH) == i`, which is `r < CEIL((i+1)·dstH/srcH)`.
Reusing the downscale's **floor** there handed destination row 1 of a 33-to-64 enlargement
to source row 1 while its *columns* were reading source row 0 — **a picture sheared by one
source pixel down its whole height**, still a picture, so nothing but a reference
comparison could see it. It was caught by the reference disagreeing, which is what that
file is for.

**A REPLICATED ROW IS NOT A DUPLICATED ROW**, and this is worth knowing before predicting
what an enlargement looks like. The accumulator is held across the rows one source row
completes, but `err_` advances **per emitted row** — so the copies are the same tone in
*different* dither patterns. Asserted as **zero** identical adjacent destination rows over
a flat midtone at ×1.94 (33 source rows into 64), where 31 of those rows are second
copies. Clearing the accumulator on every emit instead — the obvious spelling — makes the
second copy **paper**, and fails that case with the tone as well as the pattern.

**WHAT IT COSTS: NOTHING NEW IN MEMORY, AND IT IS THE CHEAP DIRECTION IN TIME.** `acc_`,
`count_` and `err_` are sized by `dstW`, which for `FILL` is the panel width — the same
bound a full-panel downscale already pays, so at most **+1,280 bytes** against what a
small cover used to take and **nothing** against the worst case that already ships. The
device's 81,088-byte deflated-JPEG peak is untouched. The gather runs `dstW` times per
source row, so **349,536 iterations** for 400×662 → 528×792 against the **2.65 M** a
median cover's downscale walks. Desktop, three runs each: that cover's decode goes
**4.4 → 5.9 ms** against a median cover's 21 ms.

**WHAT ONLY THE PANEL CAN ANSWER, AND THE OVERRIDE MADE IT THE WHOLE TICKET.** The two
measurements bound the *introduced structure* at the grain the glass has accepted; they do
not say a ×2 enlargement of a photograph reads well, and at ×2.5 they do not reach it at
all. **This panel has corrected desktop reasoning three times**, and nothing on the desktop
can arbitrate here by construction — the simulator and the goldens run this same
arithmetic, so they agree with it whatever it says. So:

1. ~~Whether ×2.29 replication of a 260 px cover across 528 px reads as a photograph or as
   BLOCKS.~~ **ANSWERED ON GLASS (2026-09-07): CONFIRMED on an X3**, on the real book the
   raise was made for — `Walden ou la vie dans les bois`, 260×346, a **deflated PNG**, the
   one corpus format this file says *may* refuse on heap. The verdict was that it *"looks
   good enough"*, and **that wording is the finding rather than a rough note**: it is an
   ACCEPTANCE, not a measurement, so it does not extend the two measurements to ×2.5 —
   they still bound 200. `kMaxCoverUpscalePercent` remains a **one-constant** change in
   either direction and **200 is still the number the measurements support**, so the way
   back stays open and cheap.
2. Whether the `FILL` crop of an *enlarged* cover cuts type the reader wanted — the crop is
   unchanged arithmetic, but it now bites on covers that used to be shown whole, and the
   raise widened the set it bites on.

**Question (2) of the original three is closed, by decision and not by evidence**: it asked
whether refusing at ×2.29 beats a soft full-bleed picture, and the owner answered *no*,
which is what this constant now records. `[cover] TooSmall … dst=…` is still the line that
makes a refusal readable off a device — there is simply less that reaches it.

### Sleep releases the whole `App`, not just the chapter

`ReaderScreen::releaseChapter()` already existed, built for the peek, and it frees the
right **36,956 bytes** — and it is **not enough**. Measured on the X3, a deflated JPEG
peaks at **81,088 bytes**, which is **17.5 KB ABOVE the desktop's 63,560 for the same
work**. Releasing only the chapter leaves:

| the book was opened via | free at sleep | margin over an 81 KB peak |
|---|--:|--:|
| Home → CONTINUE | ~121 KB | ~40 KB |
| **the Library, 203 books** | **~87 KB** | **~6 KB** |

**Six kilobytes, on the commonest way to open a book** — the Library's 203 entries sit
resident under the Reader at ~59 KB. **And the failure would have been SILENT**:
`decodeCover` answers `OutOfMemory`, the screen falls back to the reading card, nothing
looks broken, and it reads as *"covers don't work for some books"* rather than as a
defect anybody reports. Releasing the `App` returns that ~59 KB and takes the margin to
**~65 KB**.

**NOTHING NEEDS THE `App` AFTER THE FIRST SLEEP PAINT**, and each half was checked
rather than assumed: `saveWhereWeAre` wrote the session record at *navigation* time and
not here; `saveReadingPosition` ran while the stack was still standing;
`paintSleepScreen` bypasses `App` by design, because pushing `SleepScreen` would make
the next wake restore *into* it; and the next statement is a chip reset. So **sleep is
the only moment in this firmware where freeing everything is free** — a sentence the
spec carried from the start and the plan under-implemented.

**AND THE HEAP IS WHAT THE GATE ACTUALLY CAUGHT.** Every measured case peaked **17–25 KB
above its desktop figure**, and the cause is neither speed nor a missing instruction —
it is simply **a different allocator**. **A desktop heap figure is not a device heap
figure**, and this one under-predicted in the dangerous direction. That is the ratio
trap in a third disguise, after time-on-the-card and `__divdi3`.

**`inflater_` IS A VALUE MEMBER, AND THIS FILE ALREADY RECORDED THAT TRAP UNDER THE
PEEK.** An "obvious" release that drops the `BlockReader`, the `InflateSource` wrapper,
the buffer view and the file handle frees **none** of the 36,956 bytes, because the
window lives behind `Inflater`'s private `Scratch*` and is freed only by `~Inflater`.
`inflateWindowHeld()` is the observation point; `held()` and `bytesRead()` both go
false either way and can see nothing.

**One incidental correction the probe produced and this file has not absorbed:** the
device reported **173 KB free at boot**, where this file documents ~133 KB at four
older sites (the listing cache's ceiling argument, the CSS sheet cap, the Typography
re-init and the ToC's archive re-open) — and at the head of this section, which
inherited the same figure. All five are *arguments from headroom* and a larger number
only makes them safer, so none was rewritten on the strength of one reading — but
**the next person to size something against ~133 KB should re-measure rather than
inherit it**, and a second reading is enough to correct all five at once.

### The badge rule, and the rule it overrides

**ONE PREDICATE, ASKED ONCE, DRIVES BOTH THE CARD AND THE BADGE** —
`covered && vm.shows == SleepShows::Cover`. `SleepCover.dc.html` is
`SleepCoverDetails` with the card and the badge taken away, so they go together or not
at all. Two conditions spelled separately would drift, and **this project has shipped a
dead button twice from exactly that shape**.

**THE BADGE HALF OVERRIDES A RULE THIS FILE STATES OUTRIGHT** — the badge "is the
load-bearing half and it stays", because e-ink holds its last image and a screen left
on the glass gives no clue the device is asleep rather than frozen. It may go **here**
because a full-bleed book cover is **not a screen the device can otherwise be in**, so
it is unambiguous by itself.

**THAT REASON IS FALSE THE MOMENT NO COVER IS ON THE GLASS, WHICH IS WHY `covered` IS
IN THE EXPRESSION AND NOT JUST `vm.shows`.** Every fallback — no source, a source that
refused, a mode that never asked — puts the badge back. **The override does not
generalise and must not be copied**: it is licensed by one property of one screen, and
a future screen that wants to drop the badge has to earn the same property rather than
cite this precedent.

**AND THE BADGE COLLIDES IN `COVER + DETAILS`, WHICH IS AN OPEN ON-GLASS QUESTION.** It
sits at `bottom: 34px` and a Standard Ebooks cover carries its title band exactly
there, so the cover's author line is sliced by an opaque white box. Most covers set
type low, so this is the common case rather than an unlucky one — and **it is the
strongest argument the boards make for why `COVER` drops the badge at all**. **Do not
move the badge to fix it**: its position is `Sleep.dc.html`'s, and a badge that moved
by mode would be two spellings of one thing.

### `make compare` cannot compare a dithered photograph

**Chrome cannot Floyd–Steinberg.** A board showing a cover the browser dithered its own
way would measure the two rasterisers against each other and say nothing about the
firmware. So **the board's cover is generated by our own pipeline and committed** —
`design/assets/sleep-cover-480x800.png` and `sleep-cover-528x792.png` — and both the
board and the golden read that one file. The simulator *decodes* it and deliberately
does **not** re-fit it: running the source through `CoverFitter` again would dither a
second time and could only differ from the file the board shows. `sleep_cover`
therefore measures **0.00% / 0.00%**, which is the correct answer and not a suspicious
one: the two columns hold the same bytes. `sleep_cover_details` is **3.48% / 3.18%**,
against `sleep`'s 3.29% / 3.02% for the same card on paper.

**`render_board` HAD TO LEARN TO SERVE `design/assets`, AND THE DEMONSTRATION IS THE
POINT.** It wrote only `index.html` into a temp dir and served that, so a relative
`src="assets/..."` 404s. **Measured with the serving removed, `sleep_cover` goes from
0.00% to 82.97% mismatched while the sheet prints `ok` at every step** — the exact
drift this file records under the reader's menu: **the percentage is the check, the
word is not.** A symlink rather than a copy (263 KB × 60 renders would be ~16 MB of
temporary files), and **its absence is a hard error rather than a skip**, for the same
reason a named board missing from disk is.

**Inlining the PNG as a data URI was rejected**, and it is the obvious fix: it would
make the markup a **second copy** of a file whose entire purpose is that the board and
the firmware read the same bytes.

### What a cover costs on the device

Four corpus books spanning the cases, decoded at boot by the (now removed)
`ENCRE_COVER_PROBE`, panel 528×792:

| cover | zip | source | scale | decode | heap SPENT | `heapMin` |
|---|---|---|--:|--:|--:|--:|
| JPEG | deflated | 1400×2100 | 1/2 | **3,074 ms** | 81,088 | 91,660 |
| JPEG | stored | 1424×2048 | 1/2 | **2,987 ms** | 81,560 | 91,660 |
| PNG | stored | 1600×2400 | 1/1 | **6,919 ms** | 81,796 | 91,660 |
| PNG | deflated | 601×918 | 1/1 | **6,648 ms** | 120,248 | 52,744 |

**THE SPENT COLUMN IS `heapBefore − heapMin`, NOT `heapMin`, and they are trivially
swappable** — the `[cover]` line reports both and the spec's table reports the
difference. Quoting 91,660 as "the peak" would say the decode cost nothing.

**PNG IS 2.2× SLOWER THAN JPEG ON A SMALLER IMAGE**, which is the asymmetry the gate
was deliberately split to see: `decodeCover` passes the panel size to TJpgDec as
`atLeast`, so **a JPEG gets free IDCT halving before the box filter ever sees it** — up
to 64× fewer pixels through the filter and the diffusion — and **PNG has no scaled
inflate**, so it walks every source pixel. **A single number would have been a JPEG
number**, and JPEG is 81% of covers, so the 17% that is PNG is the half nobody would
have looked at.

**THE DEFLATED PNG DECODED, AND THE SPEC'S DESKTOP TEXT SAYS IT CANNOT.** Written from
a desktop measurement, the spec states outright that it "does not fit on the device"
and answers `OutOfMemory`; the probe decoded it `Ok` at 120,248 bytes. **Both can be
true, because this probe ran at BOOT** — ~173 KB free — **and a cover is decoded at
SLEEP**, out of whatever a session left behind. The stated limit therefore still says
**"may refuse"**, and that word is deliberate: this is the case where the headroom is
larger than modelled and therefore wrong in the *safe* direction, which is a thing to
say out loud rather than quietly enjoy. **Do not upgrade "may" to either "does" or
"works" until a real sleep has been measured.**

The three shapes a sleep can take, and `[power] sleep cost` is the line that adds them
up: `DETAILS` or nothing open is one MONO paint, ~825 ms; a cover with the cache warm
is one GRAY paint, three waveforms and four render passes; a cover with the cache cold
is a MONO paint, then the probe, then the decode, then a second GRAY paint — **the
expensive one, and the first sleep of a new book is always it.**

### Stated limits

| limit | incidence |
|---|---|
| progressive JPEG refused | 2 / 225 — **but 2 of the user's own 16** |
| deflated PNG may refuse on heap | 1 / 225 |
| interlaced or palette PNG refused | 0 / 225 observed |
| one cached cover; alternating books re-decode | by design |
| X4 crops ~10% of a 2:3 cover's **width** at `FILL` | default, reversible in Settings |
| first sleep of a new book shows the card for a few seconds | by design |
| a cover needing more than **×2.5** to fill the panel is refused `TooSmall` | 0 / 225 |

**WHAT THE CORPUS ACTUALLY YIELDS, run through the built pipeline: `Ok` for 223**,
`Unsupported` for 2 (both progressive JPEGs), and `NoCover` / `ReadFailed` /
`OutOfMemory` / `Abandoned` all **zero**. **The denominator is 225 now, and it read
224 here for as long as one book could not be opened at all** — `openBook` refused it
over a Calibre `user_metadata` `<meta content="…">` against `Xml::kMaxAttrBytes`, so
it never reached a decoder and counting it as a cover failure would have
double-counted it against the EPUB refusal rate. That refusal is gone (an attribute
too long to hold reads as absent — see **The lifetime rules that changed**), the book
opens, and **its cover decodes**, which is why the numerator moved with the
denominator. **The distinction it was drawn for still holds** and is the thing to keep:
a book that cannot be opened is not a book whose cover failed.

**Every one falls back to `DETAILS` with the badge shown, and logs the reason.** That
is the whole reason `CoverResult` distinguishes `NoCover` / `Unsupported` /
`ReadFailed` / `OutOfMemory` / `Abandoned` / `TooSmall` rather than answering a bool: a
refusal that cannot say which of the six it was is indistinguishable from a decoder
that does not work, and this file has paid for that shape more than once. **`TooSmall`
is the sixth and it was added rather than borrowed** — see **A small cover is enlarged**
below, which is exactly this rule applied one refusal later.

**PROGRESSIVE JPEG MATTERS MORE THAN 2/225 SUGGESTS.** It is **12.5% of the user's own
library**, and no small streaming decoder handles it. It is a **stated refusal**, not
an oversight — and the corpus is the wrong instrument for how often a real reader meets
it.

**THE CROP IS AN AXIS QUESTION AND THE PLAN HAD IT TRANSPOSED.** The X4 is 3:5 = 0.600
and a 2:3 cover is 0.667, so a cover is **relatively wider** than the panel: `FILL`
crops its **width** and `WHOLE` leaves bands **above and below**. 160 of 225 covers are
2:3 to within half a percent, so **on the X3 (528×792, 2:3 exactly) `FILL` loses
nothing for 71% of books and the setting is a no-op there**. What earns the setting is
the tail: the squarest corpus cover is 877×973, and `FILL` cuts its title off at both
edges, keeping 584 of 877 columns — a **33.4% loss**.
