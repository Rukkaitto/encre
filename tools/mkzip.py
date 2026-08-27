#!/usr/bin/env python3
"""Generate zip fixtures for the reader's archive layer, as a C++ header.

Why a generator and not checked-in .zip files: the same reason mkepub.py gives.
A binary fixture is opaque -- when the parser disagrees with it you cannot see
which of the two is wrong -- and half of these fixtures are deliberately
MALFORMED, so there is no third-party tool that could have produced them and no
way to read their intent off the bytes.

Why a header rather than files on disk: the tests that consume these run against
a FakeFileSystem, so they need bytes rather than paths, and a self-contained test
needs no fixture directory to exist at run time. Same relationship iconc.py has
with icons_data.h -- the script is the source of truth, the header is generated
and committed.

The zip format, as much of it as this uses:

    local header    0x04034b50  sig, ver, flags, method, time, date, crc32,
                                csize, usize, namelen, extralen, name, extra
    central entry   0x02014b50  the same plus attributes and the local offset
    end of central  0x06054b50  disk numbers, entry counts, cd size, cd offset,
                                comment length

The reader only ever trusts the CENTRAL directory, so several fixtures below make
the local header disagree with it on purpose.

Usage:
    python3 tools/mkzip.py --out core/src/zip_fixtures.h
"""

import argparse
import struct
import zlib

LOCAL_SIG = 0x04034B50
CENTRAL_SIG = 0x02014B50
EOCD_SIG = 0x06054B50

STORED = 0
DEFLATED = 8


def raw_deflate(data):
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    return c.compress(data) + c.flush()


class Entry:
    def __init__(self, name, data, method=DEFLATED, flags=0, csize=None,
                 usize=None, crc=None):
        self.name = name.encode()
        self.data = data
        self.method = method
        self.flags = flags
        # Only DEFLATED is actually compressed. Any other method stores the
        # bytes as-is: a fixture for an unimplemented method needs the METHOD
        # NUMBER to be wrong, not the payload, because the reader must refuse
        # before it ever looks at the data.
        self.raw = raw_deflate(data) if method == DEFLATED else data
        # Overridable so a fixture can LIE about its own sizes -- which is the
        # thing the reader has to survive, since these numbers are what it uses
        # to size a buffer.
        self.csize = len(self.raw) if csize is None else csize
        self.usize = len(data) if usize is None else usize
        self.crc = (zlib.crc32(data) & 0xFFFFFFFF) if crc is None else crc


def build(entries, *, eocd_entries=None, cd_offset=None, drop_eocd=False,
          eocd_comment=b"", extra_before=b""):
    """Assemble a zip. Every deviation from a correct file is an argument."""
    out = bytearray(extra_before)
    locals_at = []
    for e in entries:
        locals_at.append(len(out))
        out += struct.pack("<IHHHHHIIIHH", LOCAL_SIG, 20, e.flags, e.method, 0, 0,
                           e.crc, e.csize, e.usize, len(e.name), 0)
        out += e.name
        out += e.raw

    cd_start = len(out)
    for e, at in zip(entries, locals_at):
        out += struct.pack("<IHHHHHHIIIHHHHHII", CENTRAL_SIG, 20, 20, e.flags,
                           e.method, 0, 0, e.crc, e.csize, e.usize,
                           len(e.name), 0, 0, 0, 0, 0, at)
        out += e.name
    cd_size = len(out) - cd_start

    if not drop_eocd:
        n = len(entries) if eocd_entries is None else eocd_entries
        off = cd_start if cd_offset is None else cd_offset
        out += struct.pack("<IHHHHIIH", EOCD_SIG, 0, 0, n, n, cd_size, off,
                           len(eocd_comment))
        out += eocd_comment
    return bytes(out)


# Each fixture is one clause the reader has to answer, and the comment is what
# the test asserts about it.
def fixtures():
    chapter = (b"<html><body><p>Miss Brooke had that kind of beauty which seems "
               b"to be thrown into relief by poor dress.</p></body></html>")
    mimetype = b"application/epub+zip"

    f = {}

    # The happy path, and it carries BOTH storage methods because an EPUB does:
    # mimetype must be stored, everything else is normally deflated.
    f["kGood"] = ("a stored entry and a deflated one, as an EPUB has",
                  build([Entry("mimetype", mimetype, STORED),
                         Entry("OEBPS/ch1.xhtml", chapter, DEFLATED)]))

    # No end-of-central-directory record at all. A file that is not a zip, or one
    # whose tail was lost.
    f["kNoEocd"] = ("no end-of-central-directory record",
                    build([Entry("a.txt", b"hello", STORED)], drop_eocd=True))

    # The EOCD claims more entries than the directory holds. Believing it walks
    # off the end of the buffer.
    f["kEntryCountLies"] = ("an EOCD claiming 9999 entries",
                            build([Entry("a.txt", b"hello", STORED)],
                                  eocd_entries=9999))

    # The EOCD points its central directory somewhere that is not one.
    f["kBadCdOffset"] = ("an EOCD whose central directory offset is nonsense",
                         build([Entry("a.txt", b"hello", STORED)],
                               cd_offset=0xFFFF))

    # A compressed size that runs past the end of the file. The number is a claim
    # and this is the claim being false.
    f["kCsizeOverruns"] = ("an entry whose compressed data runs past the file",
                           build([Entry("a.txt", b"hello", STORED, csize=100000)]))

    # An uncompressed size far larger than anything a chapter is. The reader caps
    # this rather than allocating it.
    f["kUsizeAbsurd"] = ("an entry claiming a 200 MB uncompressed size",
                         build([Entry("a.txt", b"hello", STORED,
                                      usize=200 * 1024 * 1024)]))

    # A method that is neither stored nor deflated -- 12 is bzip2, which the
    # format permits and this reader does not implement.
    #
    # Set STRUCTURALLY, not by patching bytes. The first version of this replaced
    # the first `\x00\x00` in the file, which in a local header is the FLAGS
    # field and not the method -- so it produced flags=12 (data descriptor set!)
    # over a stored entry, and Python's zipfile read it happily. A fixture that
    # does not test what it is named after is worse than no fixture.
    f["kBzip2Method"] = ("an entry whose method is bzip2 (12), which we do not implement",
                         build([Entry("a.txt", b"hello", method=12)]))

    # General purpose bit 0: the entry is encrypted. Its bytes are not readable
    # and pretending otherwise would hand the parser ciphertext.
    f["kEncrypted"] = ("an entry with the encryption flag set",
                       build([Entry("a.txt", b"hello", STORED, flags=0x0001)]))

    # A comment on the EOCD, which is legal and shifts the record away from the
    # very end of the file -- so a reader that only looks at the last 22 bytes
    # misses it.
    f["kEocdComment"] = ("a legal EOCD comment, so the record is not at the end",
                         build([Entry("a.txt", b"hello", STORED)],
                               eocd_comment=b"written by mkzip.py"))

    # Bytes before the first local header, which is legal (self-extracting
    # archives do it) and means offsets are not relative to zero.
    f["kPrefixed"] = ("junk before the first local header, as an SFX archive has",
                      build([Entry("a.txt", b"hello", STORED)],
                            extra_before=b"#!/bin/sh\nexit 1\n"))

    # Empty: a valid zip with no entries at all.
    f["kEmpty"] = ("a valid zip holding nothing", build([]))

    return f


# --- EPUB-shaped fixtures ----------------------------------------------------
#
# The archive fixtures above test the ZIP layer; these test the layer above it,
# which reads container.xml, the OPF and the spine. Same generator because they
# are the same kind of artifact -- a zip with known contents, several of them
# deliberately wrong in one specific way each.
#
# Deliberately NOT mkepub.py's output. That writes realistic books for the device
# and for pagination; these are the smallest thing that exercises one clause, so a
# failure names the clause rather than sending you reading a whole book.

CONTAINER = b"""<?xml version="1.0"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles><rootfile full-path="%s" media-type="application/oebps-package+xml"/></rootfiles>
</container>"""


def opf(*, uid_ref="bookid", uid_id="bookid", spine=("ch1", "ch2"),
        manifest=(("ch1", "ch1.xhtml"), ("ch2", "ch2.xhtml")),
        ncx=None, spine_toc=None):
    """`ncx` adds a manifest item for a table of contents, by the media type that
    makes an NCX an NCX. `spine_toc` sets the spine's `toc` attribute, which is the
    formal route to the same file and is OPTIONAL in real books -- three of the four
    measured carry it, so both paths need a fixture.

    `uid_ref=None` omits the package's `unique-identifier` attribute and `uid_id=None`
    leaves the dc:identifier with no id at all. Both are shapes real books ship -- 4
    of 16 EPUBs in one measured library have an identifier that does not resolve, one
    of each shape -- and both are READABLE: see epub.cpp on why an identifier is
    metadata rather than a reading order."""
    items = b"".join(
        b'    <item id="%s" href="%s" media-type="application/xhtml+xml"/>\n'
        % (i.encode(), h.encode()) for i, h in manifest)
    if ncx is not None:
        items += (b'    <item id="ncx" href="%s" media-type="application/x-dtbncx+xml"/>\n'
                  % ncx.encode())
    refs = b"".join(b'    <itemref idref="%s"/>\n' % r.encode() for r in spine)
    spine_open = b"<spine>" if spine_toc is None else b'<spine toc="%s">' % spine_toc.encode()
    uid_attr = b"" if uid_ref is None else b' unique-identifier="%s"' % uid_ref.encode()
    id_attr = b"" if uid_id is None else b' id="%s"' % uid_id.encode()
    return b"""<?xml version="1.0"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0"%s>
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier%s>urn:uuid:0000-1111</dc:identifier>
    <dc:title>Middlemarch</dc:title>
    <dc:creator>George Eliot</dc:creator>
    <dc:language>en</dc:language>
  </metadata>
  <manifest>
%s  </manifest>
  %s
%s  </spine>
</package>""" % (uid_attr, id_attr, items, spine_open, refs)


CH1 = (b"<?xml version='1.0'?><html><body><h1>One</h1>"
       b"<p>Miss Brooke had that kind of beauty which seems to be thrown into "
       b"relief by poor dress. Her hand and wrist were so finely formed &#8212; "
       b"caf&#233;.</p></body></html>")
CH2 = b"<?xml version='1.0'?><html><body><h1>Two</h1><p>Weights and measures.</p></body></html>"


def ncx(points=((("Miss Brooke",), "ch1.xhtml"), (("Weights",), "ch2.xhtml")),
        nested=False):
    """An EPUB 2 toc.ncx. `points` is (labels, href) -- several labels on one href is
    what a book does when one file holds several sections, and it is why the reader
    keeps distinct labels for one spine entry and drops exact repeats.

    An accented label is in here on purpose: NCX labels are UTF-8 and the theme shouts
    them, so a caps mapping that only handled ASCII would show up as `MISS BROOKé`."""
    body = b""
    for labels, href in points:
        for label in labels:
            point = (b'<navPoint><navLabel><text>%s</text></navLabel>'
                     b'<content src="%s"/>' % (label.encode(), href.encode()))
            # A nested point sits INSIDE its parent, which the reader flattens: no
            # measured book nests, so this exists to prove the flattening rather than
            # to describe a book anyone has.
            if nested:
                point += (b'<navPoint><navLabel><text>%s (inner)</text></navLabel>'
                          b'<content src="%s"/></navPoint>' % (label.encode(), href.encode()))
            body += point + b'</navPoint>'
    return b"""<?xml version="1.0"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
  <head><meta name="dtb:uid" content="urn:uuid:0000-1111"/></head>
  <docTitle><text>Middlemarch</text></docTitle>
  <navMap>%s</navMap>
</ncx>""" % body


def epub(*, container_path=b"OEBPS/content.opf", opf_bytes=None, files=None,
         drop_container=False, drop_opf=False):
    """An EPUB as a zip. mimetype first and STORED, which the format requires."""
    entries = [Entry("mimetype", b"application/epub+zip", STORED)]
    if not drop_container:
        entries.append(Entry("META-INF/container.xml", CONTAINER % container_path))
    if not drop_opf:
        entries.append(Entry("OEBPS/content.opf",
                             opf() if opf_bytes is None else opf_bytes))
    for name, data in (files if files is not None else
                       [("OEBPS/ch1.xhtml", CH1), ("OEBPS/ch2.xhtml", CH2)]):
        entries.append(Entry(name, data))
    return build(entries)


def epub_fixtures():
    f = {}
    f["kEpubGood"] = ("a minimal two-chapter EPUB", epub())
    f["kEpubNoContainer"] = ("no META-INF/container.xml",
                             epub(drop_container=True))
    f["kEpubNoOpf"] = ("container.xml names an OPF that is not in the archive",
                       epub(drop_opf=True))
    f["kEpubContainerPointsNowhere"] = (
        "container.xml names a path nothing is at",
        epub(container_path=b"OEBPS/nope.opf"))
    # AN IDENTIFIER THAT DOES NOT RESOLVE IS A READABLE BOOK, and these two are the
    # shapes that produced that finding: 4 of 16 EPUBs in one real library refuse to
    # resolve their own unique-identifier, and every one of them reads. The first
    # names an id some other element carries; the second declares no id anywhere,
    # which is the case that makes the empty-matches-empty trap in epub.cpp reachable.
    f["kEpubIdMismatch"] = (
        "unique-identifier names an id no dc:identifier has -- readable",
        epub(opf_bytes=opf(uid_ref="bookid", uid_id="somethingelse")))
    f["kEpubNoUniqueId"] = (
        "no unique-identifier attribute and no id on the identifier -- readable",
        epub(opf_bytes=opf(uid_ref=None, uid_id=None)))
    f["kEpubEmptySpine"] = ("a spine with no itemrefs -- a book with no chapters",
                            epub(opf_bytes=opf(spine=())))
    f["kEpubSpineRefMissing"] = (
        "an itemref whose idref is in no manifest item",
        epub(opf_bytes=opf(spine=("ch1", "ghost"))))
    # --- Tables of contents ------------------------------------------------
    #
    # Measured over four real books before any of this was written: every one carries
    # an EPUB 2 `toc.ncx` and NOT ONE has an EPUB 3 nav document, and none nests. So
    # the NCX is what has fixtures, and the nested one is a proof of flattening rather
    # than a shape anyone ships.
    f["kEpubToc"] = (
        "an NCX reached by the spine's `toc` attribute",
        epub(opf_bytes=opf(ncx="toc.ncx", spine_toc="ncx"),
             files=[("OEBPS/ch1.xhtml", CH1), ("OEBPS/ch2.xhtml", CH2),
                    ("OEBPS/toc.ncx", ncx())]))
    f["kEpubTocNoSpineAttr"] = (
        "an NCX found only by its media type -- the spine has no `toc`",
        epub(opf_bytes=opf(ncx="toc.ncx"),
             files=[("OEBPS/ch1.xhtml", CH1), ("OEBPS/ch2.xhtml", CH2),
                    ("OEBPS/toc.ncx", ncx())]))
    f["kEpubTocRepeats"] = (
        "an NCX naming one file twice identically and another twice with two labels",
        epub(opf_bytes=opf(ncx="toc.ncx", spine_toc="ncx"),
             files=[("OEBPS/ch1.xhtml", CH1), ("OEBPS/ch2.xhtml", CH2),
                    ("OEBPS/toc.ncx",
                     ncx(points=((("Pour Tabby", "Pour Tabby"), "ch1.xhtml"),
                                 (("PREMI\u00c8RE PARTIE", "DEUXI\u00c8ME PARTIE"),
                                  "ch2.xhtml"))))]))
    f["kEpubTocNested"] = (
        "an NCX with nested navPoints, which the reader flattens",
        epub(opf_bytes=opf(ncx="toc.ncx", spine_toc="ncx"),
             files=[("OEBPS/ch1.xhtml", CH1), ("OEBPS/ch2.xhtml", CH2),
                    ("OEBPS/toc.ncx", ncx(nested=True))]))
    f["kEpubTocOffSpine"] = (
        "an NCX naming a file the spine does not read, plus a fragment target",
        epub(opf_bytes=opf(ncx="toc.ncx", spine_toc="ncx"),
             files=[("OEBPS/ch1.xhtml", CH1), ("OEBPS/ch2.xhtml", CH2),
                    ("OEBPS/notes.xhtml", CH2),
                    ("OEBPS/toc.ncx",
                     ncx(points=((("Notes",), "notes.xhtml"),
                                 (("Two, part two",), "ch2.xhtml#part2"))))]))
    f["kEpubHrefMissing"] = (
        "a manifest item whose href is not in the archive",
        epub(opf_bytes=opf(manifest=(("ch1", "ch1.xhtml"), ("ch2", "gone.xhtml")))))
    return f


def emit(f, out_path, ns="zipfix"):
    lines = [
        "#pragma once",
        "// GENERATED by tools/mkzip.py -- do not edit.",
        "//",
        "// Fixtures from tools/mkzip.py. Most are deliberately malformed, so there",
        "// is no tool that could have produced them and no way to read their intent",
        "// off the bytes -- mkzip.py states what each one is.",
        "",
        "#include <cstddef>",
        "",
        "namespace %s {" % ns,
        "",
    ]
    for name, (what, data) in f.items():
        lines.append("// %s" % what)
        lines.append("inline constexpr unsigned char %s[] = {" % name)
        for i in range(0, len(data), 12):
            lines.append("    " + " ".join("0x%02x," % b for b in data[i:i + 12]))
        lines.append("};")
        lines.append("inline constexpr size_t %sLen = sizeof(%s);" % (name, name))
        lines.append("")
    lines.append("}  // namespace %s" % ns)
    lines.append("")
    with open(out_path, "w") as fh:
        fh.write("\n".join(lines))
    total = sum(len(d) for _, d in f.values())
    print("%s: %d fixtures, %d bytes of zip" % (out_path, len(f), total))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, help="the zip fixtures header")
    ap.add_argument("--epub-out", help="the EPUB fixtures header")
    a = ap.parse_args()
    emit(fixtures(), a.out)
    if a.epub_out:
        emit(epub_fixtures(), a.epub_out, ns="epubfix")


if __name__ == "__main__":
    main()
