#!/usr/bin/env python3
"""The version the device draws, generated into every file that repeats it.

`core/include/reader/version.h` holds `kVersion`, and that is the ONE copy a
human edits. Everything else that states the version -- the design boards'
version slot, ReaderCore's library manifest -- is generated from it by this
script, the same relationship `iconc.py` has with the icons and
`seed-canvas.mjs` has with the published canvas.

    python3 tools/versionc.py            # rewrite every generated copy
    python3 tools/versionc.py --check    # say whether they are current, and
                                         # name what drifted if not

WHY IT EXISTS (#152). The version used to be three hardcoded copies: the header,
`design/Settings.dc.html` and `test/unit/test_version.cpp`. **`make compare` is
STRUCTURALLY INCAPABLE of catching a stale one**, because the board carries its
own: bump neither and the two agree exactly, so the sheet measures a stale
version against a stale version and reports Settings green. v0.2.0 shipped
drawing `V 0.1.0` that way -- the six-step release gate was run faithfully and
none of its steps mentioned the version. The tag was deleted and re-cut within
the hour because nothing had been downloaded, which is luck rather than a
procedure.

IT WAS FOUR COPIES, NOT THREE. `design/Boot.dc.html` states the version too, and
it had been sitting at `V 0.1.0` since before v0.2.0 -- stale, unnoticed and
named by nothing, because the boot screen is not implemented, so `make compare`
puts a NOT IMPLEMENTED placeholder beside that board and measures nothing at
all. That is the whole argument for generating rather than enumerating: the
fourth copy was missed by the issue, by the release-gate step written to catch
exactly this, and by the comment in the test that listed the copies.

TWO DIRECTIONS OF FAILURE, AND BOTH ARE LOUD. Boards are found by SCANNING
`design/*.dc.html` rather than from a list in here, so a new board carrying a
version slot is generated without anyone remembering to add a line -- and
`REQUIRED_BOARDS` names the two that must carry one, so a slot DELETED from
either is an error rather than a file that quietly stops being checked. A check
that reports on less than it claims is worse than no check, because it is
trusted; this repo has been bitten by that shape at least four times.

Exit codes: 0 current (or rewritten), 1 drifted under --check, 2 could not
answer -- the three `tools/release_blockers.py` uses, for its reason. A gate
that cannot tell a complete answer from a missing one has no business clearing
anything.

Its own tests are `python3 tools/test_versionc.py`, deliberately not wired into
`make test` for `tools/test_compare_design.py`'s reason: the fast loop builds on
a bare checkout with no Python at all.
"""
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# THE ONE HAND-EDITED COPY. It stays a C++ header rather than becoming a plain
# VERSION file with the header generated from it, because this header is
# compiled by BOTH builds on a bare checkout with no generation step: CMake for
# the desktop suite, PlatformIO for the firmware. Making it generated would put
# a code-generation step into the firmware build -- the one build nothing on a
# desktop can exercise -- to relocate a literal that is already in the only
# place both builds can read without help.
SOURCE = pathlib.Path("core/include/reader/version.h")
SOURCE_RE = re.compile(r'\bkVersion\s*=\s*"(?P<version>\d+\.\d+\.\d+)"')

# A board's version slot: the run the header band draws, `V <n>`. Matched a
# little WIDER than the two boards actually author it (`>V 0.2.0<`, the whole
# text node) on purpose -- over-reaching shows up as an unexpected line in a
# diff a human reads, and under-reaching is a copy that silently stops being
# generated. The lookarounds are what keep it off the `192.168.1.1` that four
# of the browser-page boards state.
BOARD_RE = re.compile(r'(?<![\w.])V (?P<version>\d+\.\d+\.\d+)(?![\d.])')

# The boards that MUST carry a slot. Every other board in design/ is generated
# if it has one and left alone if it does not; these two are checked the other
# way round, so losing the slot is an error rather than a silent exemption. It
# is compare-design.py's rule that a board named in its tables and absent from
# disk is a hard error, one directory over.
REQUIRED_BOARDS = {
    "Settings.dc.html": "the version the device draws, in Settings' header band",
    "Boot.dc.html": "the splash's version line",
}

# ReaderCore's PlatformIO manifest. One tag ships the firmware and the library
# inside it, so there is no separate cadence here for a second number to track
# -- and this field had sat at "0.1.0" through every release there has been.
MANIFEST = pathlib.Path("core/library.json")
MANIFEST_RE = re.compile(r'"version"\s*:\s*"(?P<version>\d+\.\d+\.\d+)"')


class Refused(Exception):
    """Could not answer -- exit 2, never a silent pass."""


def read_source(root):
    """The one hand-edited version literal, or Refused."""
    path = root / SOURCE
    if not path.exists():
        raise Refused(f"no version source at {path}")
    found = SOURCE_RE.findall(path.read_text(encoding="utf-8"))
    if not found:
        raise Refused(
            f'{SOURCE} has no `kVersion = "<major>.<minor>.<patch>"` to read, '
            "and that literal is the single source every generated copy comes from")
    if len(found) > 1:
        # Two would make "the single source" a lie in the one file where it has
        # to be true, and picking one is exactly the guess compare-design.py
        # refuses to make for a screen id listed twice.
        raise Refused(f"{SOURCE} states kVersion {len(found)} times; it must state it once")
    return found[0]


def targets(root):
    """Every generated copy: (path, compiled pattern, what it is).

    Boards are SCANNED rather than listed, so a new board carrying a version
    slot is covered by existing. REQUIRED_BOARDS covers the other direction.
    """
    out = []
    design = root / "design"
    if design.is_dir():
        for board in sorted(design.glob("*.dc.html")):
            if BOARD_RE.search(board.read_text(encoding="utf-8")):
                out.append((board, BOARD_RE, "a design board's version slot"))
    manifest = root / MANIFEST
    if manifest.exists() and MANIFEST_RE.search(manifest.read_text(encoding="utf-8")):
        out.append((manifest, MANIFEST_RE, "ReaderCore's library manifest"))
    return out


def required_missing(root):
    """Boards that must carry a slot and do not -- the deletion direction."""
    missing = []
    for name, what in sorted(REQUIRED_BOARDS.items()):
        board = root / "design" / name
        if not board.exists():
            missing.append(f"design/{name} is gone, and it held {what}")
        elif not BOARD_RE.search(board.read_text(encoding="utf-8")):
            missing.append(
                f"design/{name} no longer states a version (`V <n>`), and it holds {what}")
    return missing


def drift(root, version):
    """Every generated copy that does not say `version`, as readable lines."""
    lines = []
    for path, pattern, what in targets(root):
        text = path.read_text(encoding="utf-8")
        for match in pattern.finditer(text):
            found = match.group("version")
            if found == version:
                continue
            line = text.count("\n", 0, match.start()) + 1
            rel = path.relative_to(root)
            lines.append(
                f"  {rel}:{line}  says {found}, {SOURCE} says {version}  ({what})")
    return lines


def _substitute(match, version):
    """The matched text with only its version group replaced."""
    whole = match.group(0)
    start = match.start("version") - match.start()
    end = match.end("version") - match.start()
    return whole[:start] + version + whole[end:]


def rewrite(root, version):
    """Write `version` into every generated copy. Returns the paths changed."""
    changed = []
    for path, pattern, _what in targets(root):
        # READ FULLY, MUTATE IN MEMORY, WRITE ONCE -- CLAUDE.md's rule for
        # scripted edits, earned by three that truncated or deleted what they
        # were editing. Nothing here opens a file for writing in an expression
        # that also reads one, and the substitution replaces only the matched
        # digits rather than rebuilding the line around them.
        text = path.read_text(encoding="utf-8")
        new = pattern.sub(lambda m: _substitute(m, version), text)
        if new != text:
            path.write_text(new, encoding="utf-8")
            changed.append(path.relative_to(root))
    return changed


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__.splitlines()[0],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true",
                    help="write nothing; exit 1 if a generated copy has drifted "
                         "from the source, naming each one. CI runs this through "
                         "`make compare COMPARE_ARGS=--require-version-current`.")
    ap.add_argument("--root", default=str(ROOT),
                    help="repository root (the tests drive throwaway trees through it)")
    args = ap.parse_args(argv)
    root = pathlib.Path(args.root).resolve()

    try:
        version = read_source(root)
        missing = required_missing(root)
        if missing:
            raise Refused(
                "a board that must state the version no longer does:\n"
                + "\n".join(f"  {m}" for m in missing))
    except Refused as exc:
        print(f"versionc: {exc}", file=sys.stderr)
        return 2

    if args.check:
        bad = drift(root, version)
        if bad:
            print("the version is stated in %d place(s) that disagree with %s:\n%s\n"
                  "`make version` rewrites them; commit what moves."
                  % (len(bad), SOURCE, "\n".join(bad)), file=sys.stderr)
            return 1
        print(f"version current: {version}, in {len(targets(root))} generated copies")
        return 0

    changed = rewrite(root, version)
    if changed:
        print("version %s written into %d file(s):\n%s"
              % (version, len(changed), "\n".join(f"  {p}" for p in changed)))
    else:
        print(f"version current: {version}, in {len(targets(root))} generated copies")
    return 0


if __name__ == "__main__":
    sys.exit(main())
