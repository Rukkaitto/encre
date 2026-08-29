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
