#!/usr/bin/env python3
"""Tests for versionc.py, the generator that makes the version ONE copy.

    python3 tools/test_versionc.py

DELIBERATELY NOT WIRED INTO `make test`, for tools/test_compare_design.py's
reason: the fast loop builds on a bare checkout with no Python at all, and
making it depend on an interpreter is a worse trade than these tests are worth.
They are the tests for a tool, run when the tool changes.

WHAT THEY COVER. The defect this tool exists for is a check that looks fine and
reports on less than it claims -- v0.2.0 shipped drawing `V 0.1.0` with a green
comparison sheet beside it. So every case here drives the real CLI against a
THROWAWAY TREE built in a temp directory, the way test_seed_canvas.mjs drives
its layout guards, because a guard that has stopped firing looks exactly like a
repository with nothing wrong. Nothing here renders a pixel or reads the real
design/ -- except the last case, which asserts the committed tree is current,
since that is the one claim a developer actually relies on.

Both DIRECTIONS are covered, because a generated copy can fail two ways: the
source moves and a copy does not (drift), and a copy is deleted so there is
nothing left to generate (a silent exemption). The second is the one that ate
`design/Boot.dc.html`'s slot for a whole release -- not by deletion, but by
nobody ever having listed it.
"""
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
TOOL = ROOT / "tools" / "versionc.py"

FAILURES = []


def check(ok, what, detail=""):
    print(("  ok   " if ok else "  FAIL ") + what + (f"   [{detail}]" if detail and not ok else ""))
    if not ok:
        FAILURES.append(what)


def run(root, *argv):
    """The real CLI, against `root`. Returns (exit code, stdout+stderr)."""
    proc = subprocess.run(
        [sys.executable, str(TOOL), "--root", str(root), *argv],
        capture_output=True, text=True)
    return proc.returncode, proc.stdout + proc.stderr


def board(version_line):
    """A minimal board, shaped like the real ones: the slot is a whole text node."""
    return ('<x-dc><div style="display: flex;">\n'
            f'    <div style="font-weight: 700;">{version_line}</div>\n'
            "</div></x-dc>\n")


def tree(tmp, version="0.2.0", settings="0.2.0", boot="0.2.0",
         manifest="0.2.0", extra=None):
    """A throwaway repository with the four files versionc.py knows about."""
    root = pathlib.Path(tmp)
    (root / "core" / "include" / "reader").mkdir(parents=True, exist_ok=True)
    (root / "design").mkdir(parents=True, exist_ok=True)
    (root / "core" / "include" / "reader" / "version.h").write_text(
        "#pragma once\nnamespace reader {\n"
        f'inline constexpr const char* kVersion = "{version}";\n}}\n')
    if settings is not None:
        (root / "design" / "Settings.dc.html").write_text(board(f"V {settings}"))
    if boot is not None:
        (root / "design" / "Boot.dc.html").write_text(board(f"V {boot}"))
    if manifest is not None:
        (root / "core" / "library.json").write_text(
            json.dumps({"name": "ReaderCore", "version": manifest}, indent=2) + "\n")
    for name, body in (extra or {}).items():
        (root / "design" / name).write_text(body)
    return root


def read(root, rel):
    return (root / rel).read_text()


def main():
    print("a tree already in agreement is reported current and left alone")
    with tempfile.TemporaryDirectory() as tmp:
        root = tree(tmp)
        before = {p: read(root, p) for p in
                  ("design/Settings.dc.html", "design/Boot.dc.html", "core/library.json")}
        code, out = run(root, "--check")
        check(code == 0 and "0.2.0" in out, "--check exits 0 on a current tree",
              f"exit={code} out={out!r}")
        code, _ = run(root)
        check(code == 0 and all(read(root, p) == t for p, t in before.items()),
              "a rewrite with nothing to do writes nothing")

    print("\n--check FAILS on a drifted copy and names it")
    # The whole point. Each copy is drifted on its own, because a check that
    # only notices when everything is wrong is the one this tool replaces.
    for rel, kw in (("design/Settings.dc.html", {"settings": "0.1.0"}),
                    ("design/Boot.dc.html", {"boot": "0.1.0"}),
                    ("core/library.json", {"manifest": "0.1.0"})):
        with tempfile.TemporaryDirectory() as tmp:
            root = tree(tmp, **kw)
            code, out = run(root, "--check")
            check(code == 1 and rel in out and "0.1.0" in out and "0.2.0" in out,
                  f"--check names {rel} as drifted, with both versions",
                  f"exit={code} out={out!r}")

    print("\nthe generator rewrites every drifted copy, and only the version")
    with tempfile.TemporaryDirectory() as tmp:
        root = tree(tmp, version="0.3.0", settings="0.2.0", boot="0.1.0", manifest="0.1.0")
        code, out = run(root)
        check(code == 0, "the rewrite succeeds", f"exit={code} out={out!r}")
        check("V 0.3.0" in read(root, "design/Settings.dc.html")
              and "V 0.3.0" in read(root, "design/Boot.dc.html"),
              "both boards now state the source's version")
        check(json.loads(read(root, "core/library.json"))["version"] == "0.3.0",
              "the manifest does too")
        # The board is HTML and the substitution replaces the digits, not the
        # line: an edit that rebuilt the markup around the slot would pass a
        # version check and silently restyle the screen.
        check('style="font-weight: 700;"' in read(root, "design/Settings.dc.html"),
              "the markup around the slot is untouched")
        code, _ = run(root, "--check")
        check(code == 0, "the tree is current afterwards")

    print("\na REQUIRED board that has lost its slot is an error, not an exemption")
    # The direction that cannot be caught by comparing strings: with the slot
    # gone there is nothing to disagree with, so a scan alone would report the
    # tree current while the screen silently stopped stating a version.
    for name, kw in (("Settings.dc.html", {"settings": None}),
                     ("Boot.dc.html", {"boot": None})):
        with tempfile.TemporaryDirectory() as tmp:
            root = tree(tmp, **kw)
            code, out = run(root, "--check")
            check(code == 2 and name in out,
                  f"a missing design/{name} refuses with exit 2", f"exit={code} out={out!r}")
        with tempfile.TemporaryDirectory() as tmp:
            # ...and present but with the slot edited out, which is the likelier
            # way it happens: a board redesigned without noticing what it held.
            root = tree(tmp, **kw)
            (root / "design" / name).write_text(board("SETTINGS"))
            code, out = run(root, "--check")
            check(code == 2 and name in out,
                  f"design/{name} present with no version slot refuses too",
                  f"exit={code} out={out!r}")

    print("\na NEW board carrying a slot is generated without being listed")
    # REQUIRED_BOARDS is the floor, not the set. A board added later states the
    # version because it is in design/ and says `V <n>`, which is the half of
    # this that no list could have got right -- Boot.dc.html was a fourth copy
    # nobody knew about, including the issue that asked for this tool.
    with tempfile.TemporaryDirectory() as tmp:
        root = tree(tmp, extra={"About.dc.html": board("V 0.0.9")})
        code, out = run(root, "--check")
        check(code == 1 and "About.dc.html" in out,
              "an unlisted board's stale slot is reported", f"exit={code} out={out!r}")
        run(root)
        check("V 0.2.0" in read(root, "design/About.dc.html"),
              "and it is rewritten by the generator")

    print("\nwhat the pattern must NOT match")
    # Four boards state `192.168.1.1`, which is three dot-separated numbers and
    # is not a version. A generator that rewrote one would be a design change
    # nobody asked for, arriving through a release.
    with tempfile.TemporaryDirectory() as tmp:
        addresses = board("http://192.168.1.1/") + "<p>Firmware V2 is not a version</p>\n"
        root = tree(tmp, extra={"Transfer.dc.html": addresses})
        code, _ = run(root, "--check")
        check(code == 0, "an IP address and a bare `V2` are not version slots")
        run(root)
        check(read(root, "design/Transfer.dc.html") == addresses,
              "and the generator leaves that board byte-identical")

    print("\nthe source itself has to be answerable, or the tool refuses")
    with tempfile.TemporaryDirectory() as tmp:
        root = tree(tmp)
        (root / "core" / "include" / "reader" / "version.h").write_text("#pragma once\n")
        code, out = run(root, "--check")
        check(code == 2 and "version.h" in out,
              "no kVersion refuses with exit 2", f"exit={code} out={out!r}")
    with tempfile.TemporaryDirectory() as tmp:
        root = tree(tmp)
        (root / "core" / "include" / "reader" / "version.h").write_text(
            'const char* kVersion = "0.2.0";\n'
            '#ifdef ENCRE_NEXT\nconst char* kVersion = "9.9.9";\n#endif\n')
        code, out = run(root, "--check")
        check(code == 2 and "once" in out,
              "two kVersion literals refuse rather than picking one",
              f"exit={code} out={out!r}")
    with tempfile.TemporaryDirectory() as tmp:
        root = tree(tmp)
        (root / "core" / "include" / "reader" / "version.h").unlink()
        code, out = run(root, "--check")
        check(code == 2, "a missing source refuses with exit 2", f"exit={code} out={out!r}")

    print("\nthe committed tree is current")
    # The one case that reads the real repository, because "the boards agree
    # with the header" is the claim every other file in this repo now relies on.
    code, out = run(ROOT, "--check")
    check(code == 0, "python3 tools/versionc.py --check passes here",
          f"exit={code} out={out!r}")

    print()
    if FAILURES:
        print(f"{len(FAILURES)} FAILED:")
        for f in FAILURES:
            print(f"  - {f}")
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
