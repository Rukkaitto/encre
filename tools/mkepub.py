#!/usr/bin/env python3
"""Generate small, valid EPUB 3 files for testing on the device.

Why this is a generator and not a checked-in .epub: the same reason fonts and
icons are generated here. A binary fixture is opaque -- when Phase 3's parser
disagrees with it, you cannot see which of the two is wrong. A script says
exactly what is in the file, and lets the next person add the case they need.

The books are deliberately not real ones. Every word here is written for this
purpose, so there is no question about what may be redistributed in a test
fixture, and the prose is chosen to exercise the layout engine rather than to be
read: long unbroken words for hyphenation, an em dash and accented characters for
the font's coverage, a blockquote and a list for block handling, and one
deliberately over-long paragraph so pagination has to break inside it.

What makes an EPUB valid, and the two things that are easy to get wrong:

  * `mimetype` MUST be the first entry in the zip, STORED (uncompressed), with
    no extra field. Readers identify the format by reading it at a fixed offset,
    so a deflated or reordered mimetype is the classic "my EPUB opens nowhere"
    bug. zipfile will happily do the wrong thing here.
  * The OPF's `unique-identifier` must name a `dc:identifier` that actually
    exists in the metadata. A mismatch parses fine and then breaks anything that
    keys per-book state on the identifier -- which is exactly what this firmware
    will do for reading progress.

Usage:
    python3 tools/mkepub.py --out ~/Desktop/epubs           # the default set
    python3 tools/mkepub.py --out DIR --one "Title|Author"  # just one
"""

import argparse
import pathlib
import zipfile

MIMETYPE = "application/epub+zip"

CONTAINER = """<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>
"""

# A cover as SVG rather than a raster: it is text, so a diff shows what changed,
# and it scales to whatever the reader asks for. Bands of flat tone on purpose --
# a 4-level panel has to dither them, so this doubles as a test of that path.
COVER_SVG = """<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="600" height="900" viewBox="0 0 600 900">
  <rect width="600" height="900" fill="#ffffff"/>
  <rect x="0" y="0" width="600" height="120" fill="#000000"/>
  <rect x="40" y="200" width="520" height="8" fill="#000000"/>
  <rect x="40" y="240" width="360" height="8" fill="#808080"/>
  <rect x="40" y="280" width="440" height="8" fill="#404040"/>
  <rect x="40" y="700" width="520" height="160" fill="#c0c0c0"/>
  <text x="40" y="420" font-family="serif" font-size="54" fill="#000000">{title}</text>
  <text x="40" y="480" font-family="serif" font-size="28" fill="#404040">{author}</text>
</svg>
"""

OPF = """<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="bookid">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="bookid">urn:uuid:{uuid}</dc:identifier>
    <dc:title>{title}</dc:title>
    <dc:creator id="creator">{author}</dc:creator>
    <meta refines="#creator" property="role" scheme="marc:relators">aut</meta>
    <dc:language>en</dc:language>
    <dc:publisher>Encre test fixtures</dc:publisher>
    <dc:date>2026-08-21</dc:date>
    <dc:description>{blurb}</dc:description>
    <dc:subject>Testing</dc:subject>
    <meta property="dcterms:modified">2026-08-21T00:00:00Z</meta>
  </metadata>
  <manifest>
    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
    <item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
    <item id="cover-image" href="cover.svg" media-type="image/svg+xml" properties="cover-image"/>
    <item id="css" href="style.css" media-type="text/css"/>
    <item id="ch1" href="ch1.xhtml" media-type="application/xhtml+xml"/>
    <item id="ch2" href="ch2.xhtml" media-type="application/xhtml+xml"/>
    <item id="ch3" href="ch3.xhtml" media-type="application/xhtml+xml"/>
  </manifest>
  <spine toc="ncx">
    <itemref idref="ch1"/>
    <itemref idref="ch2"/>
    <itemref idref="ch3"/>
  </spine>
</package>
"""

# An NCX as well as the EPUB 3 nav document. Redundant for a v3 reader, but it is
# what a v2 reader looks for, and a parser being written from scratch may well
# reach for one before the other.
NCX = """<?xml version="1.0" encoding="UTF-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
  <head><meta name="dtb:uid" content="urn:uuid:{uuid}"/></head>
  <docTitle><text>{title}</text></docTitle>
  <navMap>
    <navPoint id="np1" playOrder="1"><navLabel><text>One: The Rope Ferry</text></navLabel><content src="ch1.xhtml"/></navPoint>
    <navPoint id="np2" playOrder="2"><navLabel><text>Two: Weights and Measures</text></navLabel><content src="ch2.xhtml"/></navPoint>
    <navPoint id="np3" playOrder="3"><navLabel><text>Three: A Short Account of the Weather</text></navLabel><content src="ch3.xhtml"/></navPoint>
  </navMap>
</ncx>
"""

NAV = """<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<head><title>Contents</title><meta charset="utf-8"/></head>
<body>
  <nav epub:type="toc" id="toc">
    <h1>Contents</h1>
    <ol>
      <li><a href="ch1.xhtml">One: The Rope Ferry</a></li>
      <li><a href="ch2.xhtml">Two: Weights and Measures</a></li>
      <li><a href="ch3.xhtml">Three: A Short Account of the Weather</a></li>
    </ol>
  </nav>
</body>
</html>
"""

CSS = """body { margin: 0 1em; }
h1 { font-size: 1.4em; margin: 1.2em 0 0.6em; }
p { text-indent: 1.2em; margin: 0; }
p.first { text-indent: 0; }
blockquote { margin: 1em 2em; font-style: italic; }
"""

CHAPTER = """<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml">
<head><title>{heading}</title><meta charset="utf-8"/>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>
  <h1>{heading}</h1>
{body}
</body>
</html>
"""

# Each of these exists to make the layout engine do something specific. Keep the
# annotations if you edit them, or the next person deletes the awkward bits for
# being awkward.
CH1 = """  <p class="first">The ferry was a rope and a flat boat, and the man who worked
  it had a way of looking at the water as though it owed him an answer. He took
  the coin without counting it. &#8220;Mind the step,&#8221; he said, and there
  was no step.</p>
  <p>Upstream the light came off the surface in flat sheets &#8212; an em dash
  there, and here a few letters the font had better have: caf&#233;, na&#239;ve,
  Br&#246;nt&#235;, cliché. If any of those arrive as blanks, the subset is wrong
  rather than the book.</p>
  <blockquote>A blockquote, indented and italic, so the engine has to change two
  things at once and put them back afterwards.</blockquote>
  <p>Now a word with no convenient break in it, for the hyphenation patterns to
  argue with: antidisestablishmentarianism. And another, longer, which no line
  will hold: pneumonoultramicroscopicsilicovolcanoconiosis.</p>
  <ul>
    <li>A list item.</li>
    <li>A second, to prove the first was not an accident.</li>
  </ul>
"""

CH2 = """  <p class="first">There were scales in the back room, and a set of brass
  weights in a felt-lined case, and the felt had gone the colour of weak tea.
  Every weight was stamped with a number it no longer deserved.</p>
""" + "".join(
    """  <p>Paragraph %d. This one exists to make the chapter long enough that
  pagination has to break inside it rather than tidily between chapters, which is
  the case that finds off-by-one errors in a page cache. It also gives progress
  something to be a percentage of.</p>
""" % n for n in range(2, 26))

CH3 = """  <p class="first">It rained. Then it did not rain, and everyone said so, at
  length, as though the not-raining were an achievement they had each had some
  hand in.</p>
  <p>The last paragraph of the last chapter, which is where an end-of-book screen
  has to decide it has arrived.</p>
"""

BOOKS = [
    ("The Rope Ferry", "Aldous Prine",
     "Three short chapters written to exercise a layout engine."),
    ("Weights and Measures", "Marta Oyelaran",
     "A second fixture, so a library list has more than one row."),
    ("A Short Account of the Weather", "H. J. Vance",
     "A third, with a long title that will want truncating in a list."),
]


def build(out_dir, title, author, blurb, index):
    """Write one .epub. Returns its path."""
    # A stable, obviously-fake UUID: reproducible runs matter more than real
    # uniqueness for a fixture, and a random one would make every rebuild a diff.
    uuid = "00000000-0000-4000-8000-%012d" % index
    safe = "".join(c if c.isalnum() or c in " -_" else "" for c in title).strip()
    path = pathlib.Path(out_dir) / ("%s.epub" % safe.replace(" ", "_"))

    with zipfile.ZipFile(path, "w") as z:
        # FIRST, and STORED. See the module docstring -- this is the one ordering
        # requirement in the format, and getting it wrong produces a file that
        # looks fine in a zip tool and opens in nothing.
        z.writestr(zipfile.ZipInfo("mimetype"), MIMETYPE, zipfile.ZIP_STORED)
        w = lambda name, text: z.writestr(name, text, zipfile.ZIP_DEFLATED)
        w("META-INF/container.xml", CONTAINER)
        w("OEBPS/content.opf", OPF.format(uuid=uuid, title=title, author=author,
                                          blurb=blurb))
        w("OEBPS/toc.ncx", NCX.format(uuid=uuid, title=title))
        w("OEBPS/nav.xhtml", NAV)
        w("OEBPS/style.css", CSS)
        w("OEBPS/cover.svg", COVER_SVG.format(title=title, author=author))
        w("OEBPS/ch1.xhtml", CHAPTER.format(heading="One: The Rope Ferry", body=CH1))
        w("OEBPS/ch2.xhtml", CHAPTER.format(heading="Two: Weights and Measures", body=CH2))
        w("OEBPS/ch3.xhtml", CHAPTER.format(
            heading="Three: A Short Account of the Weather", body=CH3))
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, help="directory to write the .epub files into")
    ap.add_argument("--one", help='a single book as "Title|Author"')
    args = ap.parse_args()

    out = pathlib.Path(args.out).expanduser()
    out.mkdir(parents=True, exist_ok=True)

    books = BOOKS
    if args.one:
        title, _, author = args.one.partition("|")
        books = [(title.strip(), (author or "Unknown").strip(), "A single fixture.")]

    for i, (title, author, blurb) in enumerate(books, start=1):
        p = build(out, title, author, blurb, i)
        print("%s  (%d bytes)  %s / %s" % (p, p.stat().st_size, title, author))


if __name__ == "__main__":
    main()
