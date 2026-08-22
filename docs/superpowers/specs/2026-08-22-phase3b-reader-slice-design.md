# Phase 3B — a thin end-to-end reader slice

**One book, one chapter, one page, on glass.** Zip bytes in at one end, a Reader
screen out at the other, and every layer only as wide as that path needs. 3C
broadens each layer; this proves the path exists.

## Why a slice and not a layer

Every phase of this project has had a device finding invalidate a desktop
assumption, and each one was cheap to fix early and expensive late:

- a `delay(2500)` in `setup()` blamed on panel detection for two commits
- per-book memory arithmetic that was 4x out, corrected only by a boot line
- a "wake" that was a power-on reset, invisible until an unplugged test
- a rasteriser cost quoted 2x high from a single cold sample

A content-pipeline-only 3B would put nothing in front of the panel for the whole
phase. The slice puts real books, real pagination memory and the real refresh
behaviour in front of us while the design can still move.

## What the board demands, which is more than "show text"

`design/Reader.dc.html`, and reading it changed this plan:

| | The board | Have we got it |
|---|---|---|
| Body face | Literata, **32px**, `line-height: 1.7` | `ScalableFont` does any size — 3A |
| Alignment | **`text-align: justify`** | No. `drawText` has a fractional pen and `Tracking`; justification is space distribution on top |
| Drop cap | ~~102px floated~~ | **DROPPED from V1** — see below and the board |
| Header | `MIDDLEMARCH` / `CH. 01`, Space Grotesk meta | Yes |
| Footer | `6%`, a 210x5 bar, `53 / 890` | The bar is `outlineRect` plus a fill |

### Justification is IN

The board justifies, so ragged-right would be a screen that does not match its
board — and this project's pixel diff is worth more than the week it would save.
`drawText` already accumulates the pen in 1/64 px and rounds once, so the work is
distributing a line's slack across its word gaps rather than a new text path.

### The drop cap is DROPPED from V1, not deferred

The board no longer draws one, so `make compare` has no gap to explain. The
reasoning is kept because whoever brings it back will need it:

- **A drop cap is a BOUNDED set.** It is the first letter of a chapter, so at most
  26 glyphs plus a handful of quote marks — unlike body text, whose size set is
  unbounded and is exactly why 3A built a runtime rasteriser. So the right answer
  is a **pre-rendered asset**, the same as the chrome ramp, not the rasteriser.
- **At 102px it does not fit the glyph cache.** 2bpp is `ceil(102/4) * 102` =
  2,652 bytes for ONE glyph, against an 8 KB arena that printable ASCII at ppem 29
  already fills to 7,785. Rasterising it would evict a third of the page's glyphs
  and re-rasterise them at 3,794 us each.
- **`float: left` is a layout feature nothing else needs.** The first N lines are
  indented by the cap's width; N depends on the cap's height and the line height.
  One screen is a thin reason to put a float in the layout engine, and 3C can add
  it once with the other block features.

Justification stays, because that is not ornament -- it is what the paragraph
looks like.

## The layers, and what each one may not do

Boundaries first, because a slice's whole risk is a layer reaching past its own job.

### 1. Inflate — `stbi_zlib_decode_noheader_buffer`, already vendored

`third_party/stb_image.h` carries a complete zlib decoder and can be compiled with
no image decoders at all: `STBI_NO_PNG` plus `STBI_SUPPORT_ZLIB` (see its lines
582-583). Same provenance as the `stb_truetype` 3A ships, so no new dependency and
no second vendoring note.

- **It decodes into a buffer the caller sized.** A zip entry's header states the
  uncompressed length, so the size is known before the read — which is what makes
  this safe under `-fno-exceptions`, where a bad `resize` is an `abort()` with no
  diagnostic.
- **stb range-checks nothing on hostile input**, exactly as 3A recorded for fonts.
  The size from the header is a claim by the file, so it is bounded against a cap
  before it is believed.

### 2. `reader/zip.h` — the archive

Over `FileHandle`, which 3A made seekable precisely for this: a zip's central
directory is at the END of the file.

- Read the end-of-central-directory record, then the directory, then entries by
  name. **Not** a general zip library: stored and deflated entries, no encryption,
  no zip64, no spanning. Anything else is a refusal with a reason.
- **Read the directory ONCE into RAM.** SdFat has a single 512-byte sector cache
  on this part, so a reader ping-ponging between the directory and an entry makes
  every alternating read a real card read. 3A recorded this; it lands here.
- **The entry count is capped**, like the library's rows: an archive claiming
  100,000 entries must not be believed into an allocation.

### 3. `reader/xml.h` — a pull parser for the subset

EPUB content is well-formed XML by spec, so no tag-soup recovery. `next()` yields
`StartTag | Text | EndTag | Eof`; attributes are read from the current start tag.

- Bounded and non-allocating per token: the parser holds a `string_view` into a
  buffer the caller owns, so a document is one allocation, not one per node.
- The five predefined entities and numeric character references. **Namespaces are
  handled by ignoring the prefix**, which is right for EPUB — `opf:`, `dc:` and
  `xhtml:` are prefixes on a known vocabulary, not a general namespace problem.
- Malformed is a clean `false` with a position, never an abort. The JSON reader's
  rule, and for the same reason: this parses a file a user can hand-edit.

### 4. `reader/epub.h` — container, OPF, spine

`META-INF/container.xml` names the OPF; the OPF gives the manifest, the spine
order, and the metadata Home and Library already display.

- **The `unique-identifier` must resolve**, which `tools/mkepub.py` already warns
  about: it is what per-book state will key on, and a mismatch parses fine and
  then breaks progress silently.
- 3B reads the spine and opens **one** item. Chapter navigation is 3C's.

### 5. `reader/document.h` — the model

A block/inline tree, deliberately small: paragraphs, headings, emphasis, and the
block boundaries that change layout. Everything the XHTML says that this does not
model is DROPPED, not approximated.

### 6. `reader/layout.h` — lines, then a page

Shaping a paragraph into lines at a given column and face, then filling one page.

- Justification distributes a line's slack across its word gaps, in 1/64 px, with
  the last line of a paragraph left ragged as every typesetter does it.
- **It asks `advance()`, never `glyph()`.** 3A made that structural and it matters
  most here: measuring a chapter must not rasterise it. `rast=0` on the boot probe
  is the check.
- A page is a list of positioned runs. Not a bitmap and not a `Framebuffer` call:
  the theme draws, as it does for every other screen.

### 7. `ReaderScreen` + `renderReader`

The screen owns the page and the position; the theme draws the board. Same split as
every other screen, so `implement-screen`'s checks apply unchanged.

## What 3B does NOT do, so 3C inherits a list rather than a surprise

- No pagination cache, and **no whole-book pagination**: 3A measured 17.3 us/char,
  so a 1.8M-character novel is ~31 s before a first page. Per-chapter, lazily.
- No chapter navigation, no TXT, no images, no typography settings.
- **The glyph cache must be sized for ppem 32, not left at 8 KB.** 3A measured
  printable ASCII filling 7,785 of 8,192 at ppem 29 with zero evictions and no
  margin; bytes go as ppem^2, so 32px needs ~40% more for the same set before a
  single accent. Sizing it is 3B's, because a thrashing cache turns a 571 us page
  into seconds and would read as "the reader is slow" rather than as a budget.

## Verification

- Desktop tests per layer, and the fixtures already exist: `tools/mkepub.py`
  writes EPUBs with an em dash, accents, a blockquote, a list, and one
  deliberately over-long paragraph so a page break has to land inside one.
- A `reader` simulator subcommand and a `make compare` pair, so the screen is held
  to its board at both geometries like every other.
- On device: the `[body]` probe's numbers under a real page, and the heap
  high-water mark across an open — the two things only the panel can answer.
