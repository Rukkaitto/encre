# Image fixtures

Real covers extracted from the EPUB corpus (`tools/corpus.py`,
`~/.cache/encre-corpus`), for the JPEG and PNG decoder tests (Tasks 2/3) to run
against `image_fixtures.h`'s stb_image oracle.

| file | source | rights |
|---|---|---|
| `baseline.jpg` | Project Gutenberg #86, *A Connecticut Yankee in King Arthur's Court*, Mark Twain | `Public domain in the USA.` -- the EPUB's own `dc:rights` |
| `progressive.jpg` | Re-encoded from `baseline.jpg` (Pillow, `progressive=True`, quality 80) -- same source, same rights | as above |
| `truecolour.png` | Project Gutenberg #111, *Freckles*, Gene Stratton-Porter | `Public domain in the USA.` -- the EPUB's own `dc:rights` |
| `tiny_444.jpg` | Synthetic -- generated here, see below | None to state: no third party's bytes are in it |
| `tiny_422.jpg` | Synthetic -- generated here, see below | as above |
| `tiny_420.jpg` | Synthetic -- generated here, see below | as above |
| `grey8.png` | Derived from `truecolour.png` -- same book, same rights | as above |
| `greyalpha8.png` | as above | as above |
| `rgba8.png` | as above | as above |
| `split_idat.png` | `grey8.png`, re-chunked -- identical compressed bytes | as above |

**The three `tiny_*.jpg` are the MCU geometries a real cover cannot reach.**
`baseline.jpg` is 4:2:0, so `msx == msy == 2` and its MCU band is always 16 rows
-- and the band is the whole of `core/src/jpegd.cpp`. Every other sampling factor
gives an 8-row band, a different spacing between consecutive band tops and a
different trigger point for the flush, and none of it was covered. These are a
few hundred bytes each and cover the rest of the matrix:

| file | size | sampling | what it is for |
|---|---|---|---|
| `tiny_444.jpg` | 33x9 | 4:4:4, `msx=1 msy=1` | 8-row band; 5 MCU columns of which the last is 1px; final band of 1 row |
| `tiny_422.jpg` | 9x17 | 4:2:2, `msx=2 msy=1` | the mixed factors, and NARROWER than one 16px MCU, so every rectangle is clipped on both axes |
| `tiny_420.jpg` | 17x17 | 4:2:0, `msx=2 msy=2` | the cover's own factors at a size that is one full band plus a band of one row |

**A synthetic JPEG is legitimate here where a synthetic cover was not**, and the
distinction is worth stating because the paragraph below argues the opposite for
`baseline.jpg`. What that argument rules out is hand-written BYTES standing in
for an encoder's output. These are a real encoder's output -- Pillow's libjpeg --
so they carry genuine DCT blocks, quantisation tables and Huffman coding; what is
synthetic is only the PICTURE, and a picture has no rights to state. Committing
them rather than generating them at test time is the same call the covers get:
a decoder test wants identical bytes on every run, which a re-encode on a
different libjpeg would not give.

The content is a smooth two-axis ramp on purpose. A flat field would make a
row-order or band-phase defect invisible -- every row would equal its neighbours
-- and noise would push the two decoders' IDCT rounding outside the tolerance the
real cover is held to. Reproduce with Pillow 10.0.0:

```python
# subsampling: 0 = 4:4:4, 1 = 4:2:2, 2 = 4:2:0
im.save(name, "JPEG", quality=95, subsampling=sub, optimize=False)
```

...and CHECK THE RESULT rather than trusting the parameter: the sampling factors
above were read back out of each file's own SOF0 marker, because `subsampling=`
is a request to libjpeg and not a guarantee.

`progressive.jpg` exists so Task 2 has a file it must REJECT rather than
mis-decode: the vendored decoder and this project's own target baseline JPEG
(SOF0) only, and this fixture is confirmed SOF2 by its own marker bytes.

**Both source books were checked, not assumed.** The extraction script picks the
first cover of each kind under a size cap, and its first pass over `truecolour.png`
landed on a different Gutenberg etext -- *Big Dummy's Guide to the Internet* --
whose own `dc:rights` reads `Copyrighted. Read the copyright notice inside this
book for details.`, the opposite of what a fixture note is supposed to certify.
Re-picked for a cover whose own metadata states `Public domain in the USA.`
instead of asserting the folder name (`gutenberg/`) was enough on its own -- it
is not, since Gutenberg hosts both.

**Why these bytes are committed where the corpus itself is not.**
`tools/corpus.py`'s own docstring gives the reason its 225 EPUBs live in
`~/.cache/encre-corpus` and never in the repo: they are hundreds of megabytes,
and a committed manifest can re-fetch the same books on demand. That trade
doesn't hold here -- a decoder test wants the identical bytes on every run, the
same requirement a golden has, not a fresh fetch -- and three small covers are
~500 KB total, cheap enough to commit outright. `tools/mkepub.py` sidesteps the
same redistribution question for its own fixtures by writing every word of prose
itself; a real JPEG/PNG decoder needs a real encoder's actual output (DCT block
boundaries, filter bytes, chroma subsampling) in a way synthetic bytes cannot
reliably stand in for, so this fixture set answers the question directly instead:
confirmed public domain, from each cover's own embedded rights metadata, checked
against the source book rather than inferred from which corpus folder it sat in.

**There is deliberately NO grayscale (1-component) JPEG here, and that is a
measurement rather than an oversight.** A 1-component JPEG takes a distinct
branch in TJpgDec's `mcu_load`, so it is a fair thing to ask for. Across the
225-book corpus, **185 of 185 JPEG covers are 3-component and none is
grayscale** -- so the branch has no caller in any real book this project has
seen, and adding a fixture for it would be building ahead of one.

What that leaves untested is narrower than it first looks: a 1-component JPEG is
`msx = msy = 1`, and that band geometry -- an 8-row band, the case where
`bandRows` differs from the 4:2:0 default -- is already covered by
`tiny_444.jpg`. So `jpegd.cpp`'s own logic is exercised; only vendored code
downstream of it differs. Re-measure before adding one: the command is in the
commit that added this paragraph.

## The four PNG fixtures beside `truecolour.png`

**`pngd.h` accepts colour types 0, 2, 4 and 6, and the corpus has only type 2.**
39 of 39 PNG covers across the 225 books are colour type 2, bit depth 8,
non-interlaced. The other three are accepted on the argument that they "come
free with the same unfilter" -- which is true of the code and says nothing about
whether the code is right. **Accepting a type that has never once been decoded
is a claim, not a tested behaviour**, so the three of them exist here and
`test_pngd.cpp` decodes each byte for byte against the stb_image oracle. This is
the same call `tiny_444.jpg` and its two siblings got for JPEG's MCU geometries,
and the opposite of the call the grayscale JPEG got -- the difference is that a
grayscale JPEG needs a branch in VENDORED code, where a colour type here is a
branch in ours.

| file | size | what it is for |
|---|---|---|
| `grey8.png` | 200x300, type 0 | one channel; and the only fixture that uses **all five row filters** |
| `greyalpha8.png` | 200x300, type 4 | two channels, so the grey byte is not at a pixel-sized stride and alpha must be dropped rather than composited |
| `rgba8.png` | 200x300, type 6 | four channels; the weighting reads bytes 0..2 and must ignore byte 3 |
| `split_idat.png` | 200x300, type 0 | **five IDAT chunks**, with `pHYs` and `tEXt` around them |

**The filter coverage was its own hole, found while filling this one.**
`truecolour.png`'s 2400 rows use filters 1 (Sub, 26), 2 (Up, 2291) and 4 (Paeth,
83) -- and **not one None row and not one Average row**. So the byte-exactness
assertion, the strongest one in the file, never reached two of the five unfilter
branches. `grey8.png` uses all five (None 3, Sub 15, Up 197, Average 4, Paeth
81), which is what makes the Average branch testable at all. Read the
distribution back out of a candidate fixture rather than assuming an encoder
will produce a spread; both of these were checked by decompressing the IDAT and
counting the leading byte of each scanline.

**`split_idat.png` is `grey8.png`'s own compressed bytes, re-framed.** A real
PNG splits its IDAT -- libpng emits 8192-byte chunks by default -- and
`truecolour.png` has exactly one, so nothing exercised the concatenation the
chunk walk exists to do, nor the ancillary-chunk skip. The IDAT payload is cut
into five and wrapped with a `pHYs` and two `tEXt` chunks; the deflate stream is
byte-for-byte the one Pillow wrote, so the fixture tests the FRAMING and nothing
else. Verified to decode to the same pixels as `grey8.png` through Pillow before
it was committed, and `test_pngd.cpp` asserts the same thing against the oracle.

**No derivative carries a rights question the source did not.** All four come
from `truecolour.png`, whose own `dc:rights` reads `Public domain in the USA.`;
the pictures are a resize and a channel conversion of it, plus an alpha ramp
generated here.

Reproduce with Pillow 10.0.0 -- and note the alpha is a real gradient, not a
constant, so a decoder that read the wrong channel of a two- or four-channel
pixel differs visibly rather than by luck:

```python
im = Image.open("truecolour.png").convert("RGB").resize((200, 300), Image.LANCZOS)
a = Image.linear_gradient("L").resize((200, 300))
im.convert("L").save("grey8.png", "PNG", optimize=True)
la = im.convert("L").convert("LA"); la.putalpha(a)
la.save("greyalpha8.png", "PNG", optimize=True)
rgba = im.convert("RGBA"); rgba.putalpha(a)
rgba.save("rgba8.png", "PNG", optimize=True)
```

`split_idat.png` is then `grey8.png` with its IDAT payload cut into five equal
pieces and re-chunked with fresh CRCs, ancillary chunks added before the first
IDAT and after the last. (The decoder verifies no CRCs -- see `pngd.h` for why --
but stb_image is the oracle here and the file should be valid for anything else
that ever reads it.)

**Bit depth 16, palette, and interlace have NO fixture, deliberately**, and the
tests say so at the site: they are made by changing one byte of
`truecolour.png`'s IHDR and leaving the CRC wrong. That is stronger than a
fixture would be, because `pngd.cpp` verifies no CRCs -- so the only thing that
can be declining those files is the IHDR field itself, which is exactly what
those tests claim.

**One thing no PNG fixture can cover, measured rather than assumed: the first
row's filter.** All five PNG files here have a **Sub**-filtered first row, and
Sub never reads the row above -- so the spec's virtual row of zeroes above the
image was reached by nothing at all. An encoder will not fix that: filtering row
0 against a known-zero row is wasteful, so every one of them picks Sub or None.
`test_pngd.cpp` builds a PNG in the test instead (DEFLATE's stored-block mode
needs no compressor, the same trick `test_reader_restream.cpp` uses for a real
method-8 zip entry) and sets that first byte to Up, Average and Paeth in turn.
