# Image fixtures

Real covers extracted from the EPUB corpus (`tools/corpus.py`,
`~/.cache/encre-corpus`), for the JPEG and PNG decoder tests (Tasks 2/3) to run
against `image_fixtures.h`'s stb_image oracle.

| file | source | rights |
|---|---|---|
| `baseline.jpg` | Project Gutenberg #86, *A Connecticut Yankee in King Arthur's Court*, Mark Twain | `Public domain in the USA.` -- the EPUB's own `dc:rights` |
| `progressive.jpg` | Re-encoded from `baseline.jpg` (Pillow, `progressive=True`, quality 80) -- same source, same rights | as above |
| `truecolour.png` | Project Gutenberg #111, *Freckles*, Gene Stratton-Porter | `Public domain in the USA.` -- the EPUB's own `dc:rights` |

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
