#!/usr/bin/env python3
"""What the flasher page needs to know, generated from what already states it.

    python3 tools/webmanifest.py                 # rewrite web/manifest.json
    python3 tools/webmanifest.py --check         # say whether it is current
    python3 tools/webmanifest.py --tag v0.2.0 \
        --app dist/encre-v0.2.0-xteink.bin \
        --full dist/encre-v0.2.0-xteink-full.bin \
        --released 2026-09-18T10:23:33Z          # ...with a build attached

THE PAGE MUST NOT HOLD A SECOND COPY OF THE PARTITION TABLE. It reads the
reader's own table at 0x8000 and compares it against the one Encre expects, so
that table is a fact the page needs -- and `partitions.csv` is where this
project states it. A hand-written copy in JavaScript would be the drift this
repo has been bitten by at least four times: `iconc.py` reads the SVG off the
board rather than holding a copy, `versionc.py` reads `kVersion` rather than
repeating it, and `compare-design.py` delegates the canvas comparison to the
generator rather than reimplementing it. This is the same bargain one directory
over.

TWO HALVES WITH TWO LIFETIMES, which is the whole shape of the file:

  `layout` comes from partitions.csv and changes when that file changes, so it
  is GENERATED AND COMMITTED, and `--check` refuses drift the way
  `make version-check` does. A partitions.csv edit that never reaches the page
  is a page that fingerprints against a table nothing ships.

  `firmware` names an actual release's binaries and cannot be known until one
  exists, so the committed copy is `null` and the Pages workflow regenerates it
  at deploy time with `--tag`. A committed copy would be stale the moment a
  release was cut, and stale here means offering a download that is not the
  latest -- quietly, because nothing would say so.

`install` IS THE PINNED SLOT, AND IT IS HERE RATHER THAN IN THE PAGE for the
reason the table is. Encre always installs to `app1`, so `app0` keeps whatever
firmware the reader shipped with and "boot the other slot" always means "boot
that". Writing `0x650000` into JavaScript would put the one number that decides
whether a reader keeps its original firmware somewhere partitions.csv cannot
reach.

OFFSETS ARE HEX STRINGS rather than JSON numbers, and this is the one place
this file prefers eyeballing to arithmetic: the manifest's job is to be held up
against partitions.csv by somebody working out why a reader was called a
mismatch, and `"0x650000"` is what that file says. JavaScript's `Number()`
parses them directly, so nothing has to agree about a base.

Exit codes: 0 current (or rewritten), 1 drifted under --check, 2 could not
answer -- `tools/release_blockers.py`'s three, for its reason. A gate that
cannot tell a complete answer from a missing one has no business clearing
anything.

Its own tests are `python3 tools/test_webmanifest.py`, deliberately not wired
into `make test` for `tools/test_compare_design.py`'s reason: the fast loop
builds on a bare checkout with no Python at all.
"""
import argparse
import datetime
import hashlib
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
PARTITIONS = pathlib.Path("partitions.csv")
VERSION_H = pathlib.Path("core/include/reader/version.h")
OUT = pathlib.Path("web/manifest.json")

VERSION_RE = re.compile(r'\bkVersion\s*=\s*"(?P<version>\d+\.\d+\.\d+)"')

# The chip every Xteink X3 and X4 carries. One binary drives both models, and
# the chip id cannot tell them apart -- so this is what the page may check
# before writing, and the model is not something it gets to assert.
CHIP_FAMILY = "ESP32-C3"

# Where the ESP-IDF partition table lives, and how much of it there is. Both are
# platform constants rather than this project's, which is why they are here and
# not read from partitions.csv -- the same distinction release.yml draws when it
# reads app0's offset from that file but spells the bootloader's 0x0 inline.
TABLE_OFFSET = 0x8000
TABLE_SIZE = 0xC00

# Encre installs here, always. See the module docstring.
INSTALL_PARTITION = "app1"


class Refused(Exception):
    """Something this script cannot answer. Exit 2, never a silent default."""


def size_to_int(text):
    """partitions.csv writes sizes as 0x..., or as a decimal with K/M."""
    t = text.strip()
    if not t:
        raise Refused("a partition row has an empty size")
    if t.lower().startswith("0x"):
        return int(t, 16)
    if t[-1] in "kK":
        return int(t[:-1]) * 1024
    if t[-1] in "mM":
        return int(t[:-1]) * 1024 * 1024
    return int(t)


def read_partitions(path):
    """partitions.csv as a list of dicts, in file order."""
    try:
        text = path.read_text()
    except OSError as exc:
        raise Refused("cannot read %s: %s" % (path, exc))
    rows = []
    for line in text.splitlines():
        line = line.split("#")[0].strip()
        if not line:
            continue
        fields = [f.strip() for f in line.split(",")]
        if len(fields) < 5:
            raise Refused("malformed row in %s: %r" % (path, line))
        name, ptype, subtype, offset, size = fields[:5]
        rows.append({
            "name": name,
            "type": ptype,
            "subType": subtype,
            "offset": "0x%x" % int(offset, 0),
            "size": "0x%x" % size_to_int(size),
        })
    if not rows:
        raise Refused("no partition rows found in %s" % path)
    return rows


def read_version(path):
    try:
        text = path.read_text()
    except OSError as exc:
        raise Refused("cannot read %s: %s" % (path, exc))
    m = VERSION_RE.search(text)
    if not m:
        # The same refusal versionc.py makes, for the same reason: a version
        # this cannot parse must not become a manifest that quietly omits one.
        raise Refused("no kVersion literal in %s" % path)
    return m.group("version")


def image(path, published_dir="firmware"):
    """One binary's published path, size and digest."""
    p = pathlib.Path(path)
    try:
        data = p.read_bytes()
    except OSError as exc:
        raise Refused("cannot read %s: %s" % (p, exc))
    if not data:
        raise Refused("%s is empty" % p)
    return {
        "path": "%s/%s" % (published_dir, p.name),
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }


def build(args):
    partitions = read_partitions(ROOT / PARTITIONS)
    names = [p["name"] for p in partitions]
    if INSTALL_PARTITION not in names:
        # Losing app1 would not break anything visibly: the manifest would just
        # stop naming an install target, and the page would have to invent one.
        raise Refused(
            "%s names no %r partition, and that is where Encre installs. "
            "If the table really has changed, this script has to be taught the "
            "new one rather than left to pick a slot."
            % (PARTITIONS, INSTALL_PARTITION))
    install = next(p for p in partitions if p["name"] == INSTALL_PARTITION)
    keep = [p["name"] for p in partitions
            if p["type"] == "app" and p["name"] != INSTALL_PARTITION]

    firmware = None
    if args.tag:
        if not (args.app and args.full):
            raise Refused("--tag needs both --app and --full")
        firmware = {
            "tag": args.tag,
            "version": args.tag.lstrip("v"),
            "released": args.released or datetime.datetime.now(
                datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "app": image(args.app),
            "full": image(args.full),
        }
    elif args.app or args.full:
        raise Refused("--app/--full need --tag, which names the release they came from")

    return {
        "$comment": "Generated by tools/webmanifest.py. Do not hand-edit; "
                    "run `make webmanifest`.",
        "expects": read_version(ROOT / VERSION_H),
        "chipFamily": CHIP_FAMILY,
        "firmware": firmware,
        "layout": {
            "tableOffset": "0x%x" % TABLE_OFFSET,
            "tableSize": "0x%x" % TABLE_SIZE,
            "install": {"partition": install["name"], "offset": install["offset"]},
            "keeps": keep,
            "partitions": partitions,
        },
    }


def render(manifest):
    return json.dumps(manifest, indent=2) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true",
                    help="say whether the committed manifest is current")
    ap.add_argument("--out", default=str(OUT))
    ap.add_argument("--tag", help="the release these binaries come from, e.g. v0.2.0")
    ap.add_argument("--app", help="the app-only image (written to the install slot)")
    ap.add_argument("--full", help="the merged image (written at 0x0)")
    ap.add_argument("--released", help="ISO 8601 timestamp for the release")
    args = ap.parse_args(argv)

    try:
        manifest = build(args)
    except Refused as exc:
        print("webmanifest: %s" % exc, file=sys.stderr)
        return 2

    out = ROOT / args.out
    text = render(manifest)

    if args.check:
        if args.tag:
            # A --check against a release would compare the committed manifest
            # with one carrying firmware, which it is never supposed to have.
            print("webmanifest: --check takes no --tag; the committed manifest "
                  "records the layout only", file=sys.stderr)
            return 2
        try:
            current = out.read_text()
        except OSError:
            print("webmanifest: %s does not exist. Run `make webmanifest`." % args.out)
            return 1
        if current != text:
            print("webmanifest: %s is not what partitions.csv and version.h say. "
                  "Run `make webmanifest`." % args.out)
            return 1
        print("webmanifest current: %s, %d partitions"
              % (args.out, len(manifest["layout"]["partitions"])))
        return 0

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(text)
    where = "with %s" % manifest["firmware"]["tag"] if manifest["firmware"] else "layout only"
    print("wrote %s: %d partitions, install to %s at %s, %s"
          % (args.out, len(manifest["layout"]["partitions"]),
             manifest["layout"]["install"]["partition"],
             manifest["layout"]["install"]["offset"], where))
    return 0


if __name__ == "__main__":
    sys.exit(main())
