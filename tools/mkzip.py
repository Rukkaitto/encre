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


def emit(f, out_path):
    lines = [
        "#pragma once",
        "// GENERATED by tools/mkzip.py -- do not edit.",
        "//",
        "// Zip fixtures for test_zip.cpp. Half of them are deliberately malformed,",
        "// so there is no tool that could have produced them and no way to read",
        "// their intent off the bytes -- mkzip.py states what each one is.",
        "",
        "#include <cstddef>",
        "",
        "namespace zipfix {",
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
    lines.append("}  // namespace zipfix")
    lines.append("")
    with open(out_path, "w") as fh:
        fh.write("\n".join(lines))
    total = sum(len(d) for _, d in f.values())
    print("%s: %d fixtures, %d bytes of zip" % (out_path, len(f), total))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    emit(fixtures(), ap.parse_args().out)


if __name__ == "__main__":
    main()
