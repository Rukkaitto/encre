#!/usr/bin/env python3
"""Turn corpus_probe's JSONL into the two numbers and the histogram.

TWO METRICS, because one of them lies. A book that opens with three of its
ninety-two chapters scores as a success on the open rate alone, and reporting only
that is the shape of the card probe answered from cache: a check that reports on less
than it claims is worse than no check, because it is trusted.

Usage:
    python3 tools/corpus_report.py run.jsonl
    python3 tools/corpus_report.py before.jsonl after.jsonl   # a delta
"""

import collections
import json
import sys


def load(path):
    with open(path) as fh:
        return [json.loads(line) for line in fh if line.strip()]


def summarise(rows):
    opened = [r for r in rows if r["opened"]]
    spine = sum(r["spine"] for r in opened)
    unreadable = sum(r["unreadable"] for r in opened)
    truncated = sum(r["truncated"] for r in opened)
    return {
        "books": len(rows),
        "opened": len(opened),
        "openRate": 100.0 * len(opened) / max(1, len(rows)),
        "clean": sum(1 for r in opened if not r["unreadable"] and not r["truncated"]),
        "spine": spine,
        "chapterRate": 100.0 * (spine - unreadable - truncated) / max(1, spine),
        "textBytes": sum(r["textBytes"] for r in opened),
        "blocks": sum(r["blocks"] for r in opened),
        # #90's numbers. `splits` and `maxBlock` are absent from a run made before the
        # probe reported them, so both default rather than raising -- a delta against
        # an older baseline is the commonest use of this script.
        "splits": sum(r.get("splits", 0) for r in opened),
        "cutBooks": sum(1 for r in opened if r.get("splits", 0)),
        "maxBlock": max([r.get("maxBlock", 0) for r in opened] or [0]),
    }


def source_of(row):
    """The cache lays books out as <cache>/<source>/<digest>.epub."""
    parts = row["path"].split("/")
    return parts[-2] if len(parts) >= 2 else "?"


def report(path):
    rows = load(path)
    s = summarise(rows)
    print("%s: %d books" % (path, s["books"]))
    print("  open rate    %6.2f%%  (%d of %d)" % (s["openRate"], s["opened"], s["books"]))
    print("  whole books  %6.2f%%  (%d opened with nothing missing)"
          % (100.0 * s["clean"] / max(1, s["books"]), s["clean"]))
    print("  chapter rate %6.2f%%  (%d spine entries)" % (s["chapterRate"], s["spine"]))
    print("  text         %d bytes in %d blocks" % (s["textBytes"], s["blocks"]))
    # WHAT THE BLOCK CAP COST, and the two numbers are separate claims: a cut loses no
    # text, so what it moves is the BLOCK count and one paragraph indent per cut. The
    # largest emitted block is the bound itself, observed rather than asserted -- if it
    # ever reads above `kMaxBlockBytes + 2` the cut is not firing where it says.
    print("  cuts         %d across %d books (largest emitted block %d bytes)"
          % (s["splits"], s["cutBooks"], s["maxBlock"]))

    refusals = collections.Counter(r["reason"] for r in rows if not r["opened"])
    if refusals:
        print("\n  why books were refused")
        for reason, n in refusals.most_common():
            print("    %4d  %s" % (n, reason))

    errors = collections.Counter(r["firstError"] for r in rows
                                 if r["opened"] and r.get("firstError"))
    if errors:
        print("\n  why chapters were lost")
        for err, n in errors.most_common():
            print("    %4d  %s" % (n, err))

    damaged = [r for r in rows if r["opened"] and (r["unreadable"] or r["truncated"])]
    if damaged:
        print("\n  the worst-damaged books that still opened")
        damaged.sort(key=lambda r: -(r["unreadable"] + r["truncated"]) / max(1, r["spine"]))
        for r in damaged[:10]:
            print("    %3d/%3d chapters lost  %s"
                  % (r["unreadable"] + r["truncated"], r["spine"], r["title"][:52]))

    # PER SOURCE, because a corpus of 250 books from three toolchains is three
    # samples and not 250, and a rate that hides that flatters itself.
    per = collections.defaultdict(list)
    for r in rows:
        per[source_of(r)].append(r)
    print("\n  by source")
    for src in sorted(per):
        s2 = summarise(per[src])
        print("    %-16s %6.2f%% open, %6.2f%% of chapters, %d books"
              % (src, s2["openRate"], s2["chapterRate"], s2["books"]))


def main():
    if len(sys.argv) == 2:
        report(sys.argv[1])
        return 0
    if len(sys.argv) == 3:
        before, after = summarise(load(sys.argv[1])), summarise(load(sys.argv[2]))
        print("                 before     after     delta")
        for k in ("openRate", "chapterRate"):
            print("  %-12s %8.2f%% %8.2f%% %8.2f%%" % (k, before[k], after[k], after[k] - before[k]))
        for k in ("textBytes", "blocks", "splits", "cutBooks", "maxBlock"):
            print("  %-12s %9d %9d %+9d" % (k, before[k], after[k], after[k] - before[k]))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
