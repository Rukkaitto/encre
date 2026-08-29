#!/usr/bin/env python3
"""Re-encode progressive-JPEG covers to baseline, into COPIES of the books.

WHY THIS EXISTS: the firmware's cover decoder reads baseline JPEG and PNG, and
refuses progressive JPEG. That refusal is architectural rather than a gap --
progressive requires every DCT coefficient resident, because later scans refine
earlier ones, and a 1440x2200 cover at 4:2:0 is 4,757,760 coefficients, 9.5 MB,
against ~158 KB of free heap at sleep. Sixty times over. The DC-only trick does
not rescue it either: the first scan is a 1/8-scale image, 180x275 against a
528x792 panel, and the fitter never upscales.

So the fix cannot live on the device, and this is the desktop half of it.

THE INCIDENCE IS WHY IT IS WORTH A TOOL. Across the 225-book corpus progressive
covers are 2 books -- a tail case. Across the user's own library they are about
one book in ten, because Anna's Archive and Calibre-processed files often
re-encode covers progressively where Standard Ebooks and Gutenberg do not. A
limit that reads as negligible in the corpus is visible on a real card.

IT NEVER WRITES TO THE INPUT. Every output is a new file under --out, and a book
that needs no change is not copied at all (it is listed as `skip`). Losing
somebody's library to a tool that "fixed" it is not a risk worth any convenience.

AND IT PROVES ITS OWN WORK WITH THE FIRMWARE'S DECODER, not with the re-encoder
that just produced the bytes. `reader_sim cover` runs the real decodeCover over
the rewritten book; anything it will not read is reported as a failure and the
output is deleted rather than left looking finished. Pillow saying it wrote a
baseline JPEG is not evidence that this firmware can read it.

Usage:
    python3 tools/rebake_covers.py --in ~/Books --out ~/Books-baked
    python3 tools/rebake_covers.py --in ~/Books            # dry run, reports only
    python3 tools/rebake_covers.py --in ~/Books --out DIR --quality 92
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import zipfile
from urllib.parse import unquote

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_SIM = os.path.join(REPO, "build", "reader_sim")


def opf_path(z):
    """The OPF, by the container, falling back to a scan.

    The fallback is not defensive padding: one book in the 225-book corpus has a
    container.xml the strict read does not resolve, and it is a book with a
    perfectly good cover.
    """
    try:
        m = re.search(r'full-path="([^"]+)"',
                      z.read("META-INF/container.xml").decode("utf-8", "replace"))
        if m:
            return m.group(1)
    except Exception:
        pass
    return next((n for n in z.namelist() if n.endswith(".opf")), None)


def cover_entry(z):
    """The archive path of the OPF-declared cover, or None.

    BOTH ROUTES, because real files use one or the other and neither is required
    by any spec: `<meta name="cover" content="id">` is the EPUB 2 convention and
    what the corpus overwhelmingly uses; `properties="cover-image"` is EPUB 3's.
    This mirrors what core/src/epub.cpp does, deliberately -- a tool that
    disagreed with the firmware about which entry is the cover would rewrite the
    wrong image and report success.
    """
    opf = opf_path(z)
    if not opf:
        return None
    x = z.read(opf).decode("utf-8", "replace")
    base = os.path.dirname(opf)
    items = {}
    for mi in re.finditer(r"<item\b[^>]*>", x):
        t = mi.group(0)
        i = re.search(r'id="([^"]*)"', t)
        h = re.search(r'href="([^"]*)"', t)
        pr = re.search(r'properties="([^"]*)"', t)
        if i and h:
            items[i.group(1)] = (h.group(1), pr.group(1) if pr else "")

    href = None
    # ATTRIBUTE ORDER IS NOT PART OF XML, and a regex that demands one is a bug
    # waiting for a file that writes them the other way round. Calibre writes
    # `<meta content="cover-image" name="cover"/>` -- content FIRST -- and an
    # earlier version of this function required name first, so it missed that
    # book's meta route entirely and was rescued only by properties=. A book with
    # reversed attributes AND no properties would have been reported as having no
    # cover at all. core/src/epub.cpp is immune because it asks x.attr("name")
    # rather than matching a pattern; this is the tool catching up with it.
    for mm in re.finditer(r"<meta\b[^>]*?/?>", x):
        tag = mm.group(0)
        n = re.search(r'\bname="([^"]*)"', tag)
        c = re.search(r'\bcontent="([^"]*)"', tag)
        if n and c and n.group(1) == "cover" and c.group(1) in items:
            href = items[c.group(1)][0]
            break
    if href is None:
        for _, (h, pr) in items.items():
            # A space-separated token list, so a substring test would accept
            # `not-cover-image`. Same rule as epub.cpp's hasToken.
            if "cover-image" in pr.split():
                href = h
                break
    if href is None:
        return None

    path = os.path.normpath(os.path.join(base, unquote(href))).replace("\\", "/")
    return path if path in z.namelist() else None


def jpeg_kind(data):
    """('progressive'|'baseline'|None, width, height) from the SOFn marker.

    Read off the bytes rather than asked of a library: the whole question is what
    the FILE is, and a decoder that silently normalises would answer about itself.
    """
    if data[:2] != b"\xff\xd8":
        return None, 0, 0
    i = 2
    while i < len(data) - 9:
        if data[i] != 0xFF:
            i += 1
            continue
        mk = data[i + 1]
        if mk in (0xD8, 0x01) or 0xD0 <= mk <= 0xD7:
            i += 2
            continue
        ln = struct.unpack(">H", data[i + 2:i + 4])[0]
        if 0xC0 <= mk <= 0xCF and mk not in (0xC4, 0xC8, 0xCC):
            h, w = struct.unpack(">HH", data[i + 5:i + 9])
            return ("progressive" if mk == 0xC2 else "baseline"), w, h
        i += 2 + ln
    return None, 0, 0


def rebake(data, quality):
    """Progressive JPEG bytes -> baseline JPEG bytes, same pixels, same size."""
    from PIL import Image
    import io
    im = Image.open(io.BytesIO(data))
    im.load()
    if im.mode not in ("RGB", "L"):
        im = im.convert("RGB")
    out = io.BytesIO()
    # COLOUR IS KEPT even though the panel is grey. This rewrites somebody's
    # book, and it should stay a faithful copy for every other reader they own;
    # the greyscale conversion belongs on the device, where it already happens.
    im.save(out, "JPEG", quality=quality, progressive=False, optimize=True,
            subsampling="keep")
    return out.getvalue()


# WHAT A COVER COSTS THE DEVICE, and it is NOT what you would guess.
#
# The cost tracks the cover's COMPRESSED SIZE -- how much entropy-coded data the
# Huffman decoder has to chew -- and barely tracks its pixel count. TJpgDec's
# scale divisor reduces the IDCT only; every MCU is still entropy-decoded at full
# resolution, so asking for a smaller output buys nothing here.
#
# Measured on an X3, three real covers:
#
#     156 KB, 1.23 MP, scale 1/1  ->  3,443 ms
#     159 KB, 2.94 MP, scale 1/2  ->  3,074 ms
#   1,325 KB, 3.41 MP, scale 1/2  -> 12,733 ms
#
# The first two are the same size in BYTES and 2.4x apart in PIXELS, and they cost
# the same. The third is 8x the bytes and costs 4x. So: bytes, not pixels.
#
# THREE POINTS IS NOT A CURVE. This is a rule of thumb for flagging outliers, not
# a model to quote -- and the corpus median cover is 246 KB with p90 at 436 KB, so
# anything past about half a megabyte is already unusual.
DECODE_BASE_MS = 1800.0
DECODE_MS_PER_KB = 8.3
SLOW_MS = 6000


def estimate_ms(nbytes):
    return DECODE_BASE_MS + DECODE_MS_PER_KB * (nbytes / 1024.0)


def shrink(data, max_px, max_kb, quality):
    """Re-encode a cover down to a cap. Returns bytes, or None if already fine."""
    from PIL import Image
    import io
    im = Image.open(io.BytesIO(data))
    im.load()
    if im.mode not in ("RGB", "L"):
        im = im.convert("RGB")

    too_big_px = max_px and max(im.size) > max_px
    too_big_kb = max_kb and len(data) > max_kb * 1024
    if not (too_big_px or too_big_kb):
        return None

    if too_big_px:
        im.thumbnail((max_px, max_px), Image.LANCZOS)

    # Step the quality down until it fits, rather than picking one and hoping.
    # A cover that will not come under the cap at q60 is left at q60 rather than
    # ground into mush -- the point is a faster decode, not a smaller number.
    q = quality
    while True:
        out = io.BytesIO()
        im.save(out, "JPEG", quality=q, progressive=False, optimize=True)
        b = out.getvalue()
        if not max_kb or len(b) <= max_kb * 1024 or q <= 60:
            return b
        q -= 5


def rewrite(src, dst, cover_path, new_bytes):
    """Copy the book, replacing one entry, preserving everything else.

    ENTRY ORDER AND PER-ENTRY COMPRESSION ARE PRESERVED, and that is not
    fastidiousness: an EPUB's `mimetype` must be the FIRST entry and must be
    STORED, so a rebuild that reorders or deflates it produces a file some
    readers reject. Copying infolist() in order with each entry's own
    compress_type keeps that true without special-casing it.
    """
    with zipfile.ZipFile(src) as zin, zipfile.ZipFile(dst, "w") as zout:
        for item in zin.infolist():
            data = zin.read(item.filename)
            if item.filename == cover_path:
                data = new_bytes
            zi = zipfile.ZipInfo(item.filename, date_time=item.date_time)
            zi.compress_type = item.compress_type
            zi.external_attr = item.external_attr
            zi.internal_attr = item.internal_attr
            zi.create_system = item.create_system
            zout.writestr(zi, data)


def verify(sim, path):
    """Does the FIRMWARE read this cover now? Returns (ok, detail)."""
    if not sim or not os.path.exists(sim):
        return None, "no reader_sim; not verified"
    try:
        r = subprocess.run([sim, "cover", path, os.devnull, "--canvas", "528x792"],
                           capture_output=True, text=True, timeout=120)
    except Exception as e:
        return False, "reader_sim failed to run: %s" % e
    line = (r.stdout or r.stderr).strip().splitlines()
    line = line[-1] if line else "(no output)"
    return ("result=Ok" in line), line


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--in", dest="src", required=True, help="directory to scan (recursive)")
    ap.add_argument("--out", help="where copies go; omit for a dry run")
    ap.add_argument("--quality", type=int, default=95, help="JPEG quality, default 95")
    ap.add_argument("--sim", default=DEFAULT_SIM,
                    help="reader_sim to verify with; '' to skip verification")
    ap.add_argument("--max-px", type=int, default=0, metavar="N",
                    help="also shrink covers whose long edge exceeds N (try 1600). "
                         "Off by default: it re-encodes covers that are not broken")
    ap.add_argument("--max-kb", type=int, default=0, metavar="N",
                    help="also shrink covers larger than N KB (try 400)")
    a = ap.parse_args()

    books = []
    for root, _, files in os.walk(os.path.expanduser(a.src)):
        for f in files:
            if f.lower().endswith(".epub"):
                books.append(os.path.join(root, f))
    books.sort()
    if not books:
        print("no .epub under %s" % a.src)
        return 1

    if a.out:
        os.makedirs(os.path.expanduser(a.out), exist_ok=True)

    counts = {"skip": 0, "rebaked": 0, "failed": 0, "no cover": 0, "unreadable": 0,
              "slow": 0}
    failures = []

    for p in books:
        name = os.path.basename(p)
        try:
            with zipfile.ZipFile(p) as z:
                entry = cover_entry(z)
                data = z.read(entry) if entry else None
        except Exception as e:
            counts["unreadable"] += 1
            print("  unreadable  %s  (%s)" % (name[:60], e))
            continue

        if not data:
            counts["no cover"] += 1
            continue

        kind, w, h = jpeg_kind(data)
        est = estimate_ms(len(data))
        oversized = bool(a.max_px and max(w, h) > a.max_px) or \
                    bool(a.max_kb and len(data) > a.max_kb * 1024)

        if kind != "progressive" and not oversized:
            counts["skip"] += 1
            # SAY SO EVEN WHEN NOT ACTING. A cover this device will take ten
            # seconds over is worth knowing about whether or not the caller asked
            # for shrinking, because the symptom -- the reading card sitting there
            # for an age before the picture arrives -- looks like a hang, not like
            # a big file.
            if est >= SLOW_MS and kind is not None:
                counts["slow"] += 1
                print("  SLOW     %-46s %dx%d %5d KB  ~%.1fs on device%s"
                      % (name[:46], w, h, len(data) // 1024, est / 1000.0,
                         "" if (a.max_px or a.max_kb) else "   (--max-px 1600 fixes it)"))
            continue

        why = "progressive" if kind == "progressive" else "oversized"
        if not a.out:
            counts["rebaked"] += 1
            print("  would fix  %-42s %dx%d %5d KB  %s  ~%.1fs"
                  % (name[:42], w, h, len(data) // 1024, why, est / 1000.0))
            continue

        dst = os.path.join(os.path.expanduser(a.out), name)
        try:
            new_bytes = shrink(data, a.max_px, a.max_kb, a.quality) if oversized \
                        else rebake(data, a.quality)
            rewrite(p, dst, entry, new_bytes)
        except Exception as e:
            counts["failed"] += 1
            failures.append((name, "rewrite failed: %s" % e))
            if os.path.exists(dst):
                os.remove(dst)
            continue

        ok, detail = verify(a.sim, dst)
        if ok is False:
            # DELETED RATHER THAN LEFT LOOKING FINISHED. A book in the output
            # directory is a promise that the device reads it.
            os.remove(dst)
            counts["failed"] += 1
            failures.append((name, detail))
            continue

        counts["rebaked"] += 1
        print("  fixed  %-40s %s  %5d -> %-5d KB  ~%.1fs -> ~%.1fs%s"
              % (name[:40], why, len(data) // 1024, len(new_bytes) // 1024,
                 est / 1000.0, estimate_ms(len(new_bytes)) / 1000.0,
                 "" if ok else "  (UNVERIFIED)"))

    print()
    print("%d book(s): %d fixed, %d already fine, %d without a cover, %d failed, %d unreadable"
          % (len(books), counts["rebaked"], counts["skip"], counts["no cover"],
             counts["failed"], counts["unreadable"]))
    if counts["slow"]:
        print("%d cover(s) will take over %.0fs on the device and were left alone."
              % (counts["slow"], SLOW_MS / 1000.0))
    if not a.out:
        print("DRY RUN -- nothing was written. Pass --out DIR to produce copies.")
    else:
        print("Originals untouched. Copies are in %s" % a.out)
    for n, why in failures:
        print("  FAILED  %s\n          %s" % (n[:70], why))
    return 1 if counts["failed"] else 0


if __name__ == "__main__":
    sys.exit(main())
