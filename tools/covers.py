#!/usr/bin/env python3
"""How many real books give up a cover, as a number you can reproduce.

WHY THIS EXISTS: `reader/cover_fit.h` carries a census of the corpus's cover
aspects, and says of it that the walk behind those figures was a throwaway
script -- "a measurement that cannot be repeated is an anecdote", which is the
standard `tools/corpus.py` sets. This is the tool that lands it, and it differs
from that throwaway in the way that matters: it runs the REAL `decodeCover`
through `reader_sim cover`, not a parallel parser that could be wrong in a way
the firmware is not.

WHAT IT MEASURES IS FORMAT SUPPORT, NOT WHAT FITS ON THE DEVICE, and the two are
not the same question. The decoders run here on a 64-bit host with gigabytes
free, so every allocation succeeds. On the ESP32-C3 a deflated PNG cover needs
two 32 KB inflate windows at once -- the zip's and the picture's -- against a
measured ~45 KB floor, and it refuses with `OutOfMemory`. That refusal CANNOT
appear in this table by construction. So a clean run here means "we can read
these formats", never "every book's cover will appear on the panel"; the only
instrument for the second question is the device.

That is this project's own "a check that reports on less than it claims is worse
than no check, because it is trusted" -- so the caveat is printed with the
numbers rather than left in this docstring.

Usage:
    make sim && python3 tools/covers.py
    python3 tools/covers.py --cache /some/dir --fit whole
    python3 tools/covers.py --keep build/covers   # keep the PNGs
"""

import argparse
import os
import statistics
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import corpus  # noqa: E402  -- the manifest reader and the cache layout, once.

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_SIM = os.path.join(REPO, "build", "reader_sim")

# The two panels, named. Both, always: the fit box is derived from the panel's
# aspect, so a decoder bug that only bites when height is the binding axis is
# invisible at one geometry. The X4 is 3:5 and the X3 is 2:3, which is the same
# ratio as the median cover -- so on the X3 a Fill is usually a no-op and on the
# X4 it usually crops. They are not interchangeable measurements.
GEOMETRIES = [("480x800", "X4"), ("528x792", "X3")]

# Every CoverResult, in cover.h's own order, plus the two answers that are this
# TOOL's and not the enum's. Listed rather than discovered so a result that stops
# occurring still prints as 0 -- a column that vanishes reads as a category that
# was never checked.
RESULTS = ["Ok", "NoCover", "Unsupported", "ReadFailed", "OutOfMemory", "Abandoned",
           # The book did not open at all, so nothing was ever asked about its
           # cover. Kept out of `NoCover`, which is a book that opened and
           # declares none.
           "BookRefused",
           # reader_sim itself failed or printed something this cannot parse.
           # Never silent: an unreadable line must not be counted as a pass.
           "SimFailed"]


def books(cache, manifest):
    """The manifest's books that are actually in the cache, plus the count that is not.

    FROM THE MANIFEST, NOT FROM A DIRECTORY WALK, for the reason corpus.py commits
    one at all: a run today and a run in three months have to be over the same
    books. A stray EPUB dropped into the cache would otherwise join the corpus and
    move the number with nothing to say it had."""
    have, absent = [], 0
    for e in corpus.read_manifest(manifest):
        p = corpus.cache_path(cache, e)
        if os.path.exists(p):
            have.append(p)
        else:
            absent += 1
    return sorted(have), absent


def parse(line):
    """`key=value ... file=PATH [reason=SENTENCE]` -- the one line reader_sim prints.

    THE LAST TWO FIELDS ARE NOT SPLIT ON WHITESPACE, and that is the whole reason
    the simulator puts them last: a reason is a sentence and a path may hold a
    space, so each is taken as a tail. Splitting the reason on whitespace like
    the rest turned "the OPF is malformed" into a table cell reading "the"."""
    out = {}
    head, sep, reason = line.partition(" reason=")
    out["reason"] = reason.strip() if sep else ""
    head, _, path = head.partition(" file=")
    out["file"] = path.strip()
    for tok in head.split():
        k, _, v = tok.partition("=")
        out[k] = v
    return out


def run(sim, book, canvas, fit, out_png):
    proc = subprocess.run([sim, "cover", book, out_png, "--canvas", canvas, "--fit", fit],
                          capture_output=True, text=True)
    line = ""
    for candidate in proc.stdout.splitlines():
        if candidate.startswith("cover result="):
            line = candidate
    if not line:
        return {"result": "SimFailed",
                "reason": (proc.stderr.strip().splitlines() or ["no output"])[-1],
                "decode_ms": "0", "src": "0x0", "scale": "1/1"}
    return parse(line)


def report(name, canvas, rows, absent, fit):
    counts = {r: 0 for r in RESULTS}
    unknown = {}
    for r in rows:
        key = r.get("result", "SimFailed")
        if key in counts:
            counts[key] += 1
        else:
            unknown[key] = unknown.get(key, 0) + 1
    total = len(rows)
    print("%s %s, fit=%s -- %d book(s)%s" %
          (name, canvas, fit, total, "" if not absent else ", %d not fetched" % absent))
    for r in RESULTS:
        if counts[r] == 0 and r != "Ok":
            continue
        print("  %-12s %4d  %5.1f%%" % (r, counts[r], 100.0 * counts[r] / total if total else 0.0))
    for r, n in sorted(unknown.items()):
        print("  %-12s %4d  (NOT a CoverResult this tool knows -- cover.h has moved)" % (r, n))

    # DESKTOP MILLISECONDS. Labelled at every print site, because this project has
    # twice been burned by a desktop figure read as a device one: the decode is SD
    # reads plus an inflate on a part with no FPU, which is the class of work the
    # desktop does least like the device.
    times = [(float(r.get("decode_ms", 0)), r.get("file", "?")) for r in rows
             if r.get("result") == "Ok"]
    if times:
        times.sort()
        print("  decode, DESKTOP ms: median %.1f, slowest %.1f (%s)" %
              (statistics.median(t for t, _ in times), times[-1][0],
               os.path.basename(times[-1][1])))

    # THE SCALE DIVISOR IS COUNTED BECAUSE NOTHING ELSE CAN SEE IT. cover.h says
    # so at the field: a cover decoded at 1/1 and at 1/2 fills the SAME box, so a
    # JPEG scale search that quietly stopped choosing anything but 1/1 would move
    # no pixel geometry and no test that looked only at the planes. It is a PNG's
    # 1/1 too -- that format has no such lever -- so the row is only meaningful
    # read next to the 39 PNGs in the corpus.
    scales = {}
    for r in rows:
        if r.get("result") == "Ok":
            s = r.get("scale", "?")
            scales[s] = scales.get(s, 0) + 1
    if scales:
        print("  JPEG scale chosen: " +
              ", ".join("%s x%d" % (s, n) for s, n in sorted(scales.items())))
    for r in [r for r in rows if r.get("result") != "Ok"]:
        print("  %-12s %-24s %s" % (r.get("result"), os.path.basename(r.get("file", "?")),
                                    r.get("reason", "")))
    if counts["BookRefused"]:
        # A DIFFERENT MEASUREMENT WEARING THIS ONE'S CLOTHES. These books never
        # reached a decoder: the EPUB layer refused them, so they belong to the
        # corpus's EPUB refusal rate and say nothing about cover support. Named
        # here rather than folded into the total, because a reader counting
        # 222/225 as a cover figure would be counting one book twice over.
        print("  (%d of those belong to the EPUB refusal rate, not to cover support: the"
              % counts["BookRefused"])
        print("   book never opened, so nothing was ever asked about its cover.)")
    print()
    return counts


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cache", default=corpus.DEFAULT_CACHE)
    ap.add_argument("--manifest", default=corpus.MANIFEST)
    ap.add_argument("--sim", default=DEFAULT_SIM)
    ap.add_argument("--fit", default="fill", choices=["fill", "whole"])
    ap.add_argument("--keep", default=None,
                    help="write each cover's PNG into this directory instead of a temp file")
    a = ap.parse_args()

    if not os.path.exists(a.sim):
        print("no reader_sim at %s -- run `make sim`" % a.sim)
        return 1
    have, absent = books(a.cache, a.manifest)
    if not have:
        print("no corpus books under %s -- run `python3 tools/corpus.py fetch`" % a.cache)
        return 1
    if a.keep:
        os.makedirs(a.keep, exist_ok=True)

    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        for canvas, name in GEOMETRIES:
            rows = []
            for i, book in enumerate(have):
                if a.keep:
                    stem = os.path.splitext(os.path.basename(book))[0]
                    out_png = os.path.join(a.keep, "%s_%s.png" % (stem, canvas))
                else:
                    out_png = os.path.join(tmp, "cover.png")
                rows.append(run(a.sim, book, canvas, a.fit, out_png))
                if (i + 1) % 25 == 0:
                    print("  ...%d/%d at %s" % (i + 1, len(have), canvas), file=sys.stderr)
            counts = report(name, canvas, rows, absent, a.fit)
            failures += counts["SimFailed"]

    print("WHAT THIS IS AND IS NOT: these counts are FORMAT SUPPORT on a 64-bit host,")
    print("not what fits on the device. Every allocation succeeds here, so OutOfMemory")
    print("cannot appear -- the deflated-PNG cover that needs two 32 KB inflate windows")
    print("against the ESP32's ~45 KB floor counts as Ok above and refuses on glass.")
    print("A clean run means we read these formats, NOT that every cover will appear.")
    print("The decode times are the DESKTOP's; the device is a separate measurement.")
    # A run that could not even drive the simulator is not a measurement, and must
    # not exit 0 with a table that looks like one.
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
