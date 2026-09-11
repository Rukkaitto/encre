# Design board assets

Pictures a board cannot author, because the firmware's own output is the only
thing worth comparing it against.

| file | bytes | sha256 | drawn by |
|---|--:|---|---|
| `sleep-cover-480x800.png` | 129,716 | `759d0cfe9b5a068fb07cb381b8ed13602a3245a0e031ed12482d56bad4f13471` | `reader_sim cover`, X4 geometry |
| `sleep-cover-528x792.png` | 142,796 | `7cf0087100b84446efcae0402923e555543fb9e351f847aea81d3d2213207476` | `reader_sim cover`, X3 geometry |

Used by **three** boards: `design/SleepCover.dc.html` and
`design/SleepCoverDetails.dc.html`, the two cover modes of the sleep screen, and
`design/SleepCoverWaking.dc.html`, the wake over a cover. `Sleep.dc.html` and
`SleepIdle.dc.html` are the `DETAILS` mode and need no asset.

**The third one shows the same file at one bit, and there is no fourth file for
it.** A wake paints one waveform, so the firmware renders `Plane::Bw` -- which
inks where coverage >= 2, which is exactly "the Msb plane set". These files hold
only the four ramp levels `{0xFF, 0xAA, 0x55, 0x00}` that `writeGrayPng` writes,
indexed `(msb << 1) | lsb`, so **thresholding one at 128 recovers its Msb plane
bit for bit**: 0xAA (170) and 0x55 (85) fall either side of the threshold, and
that boundary IS the Msb boundary. `SleepCoverWaking.dc.html` therefore shows
this same PNG under a `contrast(100000%)` filter -- a hard threshold at 0.5 --
rather than a committed one-bit twin.

**Committing a one-bit asset was the obvious alternative and it is the wrong
one**, for a reason specific to how the comparison works: `sim/main.cpp`'s
`BoardCover` -- the *firmware* column of `make compare` -- decodes the
four-level PNG and unpacks its two planes. A one-bit file would be a second copy
of a picture the firmware column already reads from the first, kept in step by
nothing. One file feeds both columns of all three boards.

**Measured, not argued.** CSS filters could plausibly threshold in *linear*
light, where 170 and 85 are 0.40 and 0.09 and both fall below 0.5 -- the cover
would come back solid black. Rendered through `compare-design.py`'s own Chrome
invocation, the filtered board matches the threshold-at-128 of the source file
on every one of 384,000 pixels. It also fails loudly if that ever changes: a
linearising Chrome blacks out the ~17% of the frame that is paper.

## Why the board's cover is generated and not drawn

**`make compare` renders the board in Chrome and diffs it against the firmware
per pixel, and Chrome cannot Floyd–Steinberg.** A hand-authored or
CSS-generated cover would mismatch across the whole image, and the percentage
would be measuring the two rasterisers rather than the design. With the
firmware's own output committed as the board's image, the comparison measures
the chrome *around* the cover -- the card, the badge, the type -- which is the
part that can actually drift. On `SleepCover.dc.html` the cover is the entire
screen, so without this there would be nothing on that board worth comparing at
all.

This is `iconc.py`'s rule -- each icon's SVG read from its named board at
generation time, generated from one source and never transcribed twice --
applied to a picture instead of a mark.

**Two files, because a fit is not a scale.** The X4 is 480x800 (3:5) and the X3
is 528x792 (2:3), so `--fit fill` crops a 1400x2100 cover differently on each,
and the dither grid is keyed on absolute panel coordinates either way. Letting
Chrome resample one file up to the other geometry would put the browser's
resampler into the comparison, which is the exact thing this asset exists to
keep out. The boards pick between them with `<source media="(width: 528px)">`:
`compare-design.py` drives Chrome at `--window-size=WxH`, so the viewport is the
panel.

## The book, and the rights check

**Middlemarch, George Eliot** -- Standard Ebooks, from the corpus
(`tools/corpus.manifest` line 212, `standardebooks`, epub sha256
`cc2f7d8113f526335de81455b5bea9d3750a935f4b751cbdc6a191b3138ffcc8`, cached as
`~/.cache/encre-corpus/standardebooks/cc2f7d8113f52633.epub`). The cover is
`epub/images/cover.jpg`, 1400x2100, sha256
`bfe7b0532e5899ad4fc9ad1dec9eaab1f9034f40ad19c2fc30cdca3c9d495687`.

**It is Middlemarch because every board's card says Middlemarch** (#120). The
specimen book is one book across 33 boards, and `SleepCoverDetails.dc.html` is
`Sleep.dc.html` with its background replaced and nothing else -- so a cover of a
DIFFERENT book put two claims on one screen, and the card called the picture a
liar. The asset was Romola for as long as the cover was chosen for its picture
rather than for its title.

**Checked from the book's own `dc:rights`, not from which folder it sits in**,
which is the standard `test/unit/fixtures/images/README.md` sets and the episode
it records: an earlier pick from `gutenberg/` turned out to be a *copyrighted*
etext, because Gutenberg hosts both. This book's `content.opf` says:

> The source text **and artwork** in this ebook are believed to be in the United
> States public domain; that is, they are believed to be free of copyright
> restrictions in the United States. […] The creators of, and contributors to,
> this ebook dedicate their contributions to the worldwide public domain via the
> terms in the CC0 1.0 Universal Public Domain Dedication.

The artwork clause is the one that matters here, since what is committed is the
cover and not a word of the text. The colophon names the painting: *The Grove,
Hampstead*, John Constable, completed between 1821 and 1822 -- painter died
1837, so the source artwork is public domain on its own terms as well as by
Standard Ebooks' policy.

**1400x2100 is the typical case, not a flattering one.** All 95 Standard Ebooks
in the corpus are exactly that size, and `cover_fit.h`'s own census says 160 of
225 covers are 2:3 to within half a percent with a median aspect of 0.667 --
1400x2100 is the very shape its worked example uses ("the median 2:3 cover keeps
1260 of its 1400 columns, a 10.0% loss"). So the X4 asset is a 10% side crop and
the X3 asset loses nothing, which is exactly the pair of behaviours a board
showing this screen should be showing.

## Reproducing them

```bash
make sim
B=~/.cache/encre-corpus/standardebooks/cc2f7d8113f52633.epub
./build/reader_sim cover "$B" design/assets/sleep-cover-480x800.png --canvas 480x800 --fit fill
./build/reader_sim cover "$B" design/assets/sleep-cover-528x792.png --canvas 528x792 --fit fill
```

What it printed when these were made:

```
cover result=Ok src=1400x2100 scale=1/2 dst=480x800+0+0 panel=480x800 fit=fill open_ms=0.4 decode_ms=27.6
cover result=Ok src=1400x2100 scale=1/2 dst=528x792+0+0 panel=528x792 fit=fill open_ms=0.2 decode_ms=26.7
```

`dst` covering the whole panel at `+0+0` is what makes these full-bleed: `fill`
crops rather than letterboxes, so there is no paper edge for the board to have
to match.

**Committed rather than fetched**, for the reason a golden is committed:
`tools/corpus.py` keeps its 225 EPUBs out of the repo because they are hundreds
of megabytes and a manifest can re-fetch them, but a per-pixel comparison wants
the identical bytes on every run, on a machine that may have no corpus cached at
all. 263 KB for the pair.
