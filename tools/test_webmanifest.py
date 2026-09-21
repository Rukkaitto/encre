#!/usr/bin/env python3
"""Tests for tools/webmanifest.py. Plain python3, no dependencies.

    python3 tools/test_webmanifest.py

NOT WIRED INTO `make test`, for `tools/test_compare_design.py`'s reason: the
fast loop builds on a bare checkout with no Python at all, and keeping it
interpreter-free is worth a test somebody has to remember. `make test-tools`
runs it with the others, and CI has a job.

EVERY GUARD IS DRIVEN AGAINST A THROWAWAY TREE rather than asserted about the
real one, because a guard that has stopped firing looks exactly like a
repository with nothing wrong -- which is the argument
`tools/design-canvas/test_seed_canvas.mjs` makes for the same shape one
directory over. The committed tree is then checked once, at the end, on the
question only it can answer: that what is committed is what partitions.csv
says.
"""
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "tools" / "webmanifest.py"

STOCK_CSV = """\
# Name,     Type, SubType,  Offset,   Size,      Flags
nvs,        data, nvs,      0x9000,   0x5000,
otadata,    data, ota,      0xe000,   0x2000,
app0,       app,  ota_0,    0x10000,  0x640000,
app1,       app,  ota_1,    0x650000, 0x640000,
spiffs,     data, spiffs,   0xc90000, 0x360000,
coredump,   data, coredump, 0xFF0000, 0x10000,
"""

VERSION_H = 'inline constexpr const char* kVersion = "9.9.9";\n'

failures = []


def check(ok, what):
    print("  %-4s %s" % ("ok" if ok else "FAIL", what))
    if not ok:
        failures.append(what)


def tree(csv=STOCK_CSV, version=VERSION_H):
    """A throwaway repo with just the two files the generator reads."""
    d = pathlib.Path(tempfile.mkdtemp(prefix="webmanifest-test-"))
    (d / "tools").mkdir()
    shutil.copy(SCRIPT, d / "tools" / "webmanifest.py")
    (d / "partitions.csv").write_text(csv)
    (d / "core" / "include" / "reader").mkdir(parents=True)
    (d / "core" / "include" / "reader" / "version.h").write_text(version)
    return d


def run(d, *args):
    p = subprocess.run([sys.executable, str(d / "tools" / "webmanifest.py"), *args],
                       capture_output=True, text=True, cwd=d)
    return p.returncode, p.stdout + p.stderr


def manifest_of(d):
    return json.loads((d / "web" / "manifest.json").read_text())


print("the layout comes from partitions.csv")
d = tree()
rc, out = run(d)
check(rc == 0, "a generate run succeeds")
m = manifest_of(d)
by_name = {p["name"]: p for p in m["layout"]["partitions"]}
check([p["name"] for p in m["layout"]["partitions"]]
      == ["nvs", "otadata", "app0", "app1", "spiffs", "coredump"],
      "every row is carried, in file order")
check(by_name["app1"]["offset"] == "0x650000" and by_name["app1"]["size"] == "0x640000",
      "offsets and sizes survive as hex strings")
check(by_name["coredump"]["offset"] == "0xff0000",
      "an uppercase offset in the CSV is normalised, not passed through")
check(m["layout"]["install"] == {"partition": "app1", "offset": "0x650000"},
      "install names app1 and takes its offset from the table")
check(m["layout"]["keeps"] == ["app0"],
      "the other app partition is what the install keeps")
check(m["expects"] == "9.9.9", "the version comes from version.h")
check(m["firmware"] is None, "a plain run attaches no firmware")

print("\nsizes in K/M mean what they mean")
d = tree(csv=STOCK_CSV.replace("0x640000,", "6400K,", 1))
rc, _ = run(d)
check(rc == 0, "a K-suffixed size is accepted")
check(manifest_of(d)["layout"]["partitions"][2]["size"] == "0x640000",
      "...and is the same number as the hex spelling")

print("\n--check refuses drift")
d = tree()
run(d)
rc, _ = run(d, "--check")
check(rc == 0, "--check passes on a manifest it just wrote")
(d / "partitions.csv").write_text(STOCK_CSV.replace("0x650000", "0x660000"))
rc, out = run(d, "--check")
check(rc == 1, "--check fails once partitions.csv moves a partition")
check("make webmanifest" in out, "...and names the command that fixes it")
d = tree()
rc, out = run(d, "--check")
check(rc == 1, "--check fails when the manifest does not exist at all")

print("\nrefusals are exit 2, never a quiet default")
# Proved by mutation: with app1 renamed the generator must NOT pick another
# slot. Installing to the wrong one overwrites the firmware the reader came
# with, which is the whole thing the pinned slot exists to prevent.
d = tree(csv=STOCK_CSV.replace("app1,       app,  ota_1", "appX,       app,  ota_1"))
rc, out = run(d)
check(rc == 2, "losing app1 is an error rather than an invented install target")
check("app1" in out and "taught" in out.lower(),
      "...and the message names app1 and says the script must be taught the new table")
d = tree(version="no version literal here\n")
rc, out = run(d)
check(rc == 2, "a version.h the regex cannot read is an error")
d = tree(csv="# only a comment\n")
rc, out = run(d)
check(rc == 2, "a partition table with no rows is an error")
d = tree(csv=STOCK_CSV + "broken,row\n")
rc, out = run(d)
check(rc == 2, "a malformed row is an error")

print("\nthe firmware half needs all of its arguments")
d = tree()
rc, out = run(d, "--app", "x.bin")
check(rc == 2, "--app without --tag is refused")
rc, out = run(d, "--tag", "v1.2.3")
check(rc == 2, "--tag without the binaries is refused")
rc, out = run(d, "--check", "--tag", "v1.2.3")
check(rc == 2, "--check with --tag is refused: the committed copy has no firmware")

print("\na release run records what it was given")
d = tree()
(d / "app.bin").write_bytes(b"APP" * 100)
(d / "full.bin").write_bytes(b"FULL" * 100)
rc, out = run(d, "--tag", "v1.2.3", "--app", "app.bin", "--full", "full.bin",
              "--released", "2026-09-18T10:23:33Z")
check(rc == 0, "a release run succeeds")
fw = manifest_of(d)["firmware"]
import hashlib
check(fw["tag"] == "v1.2.3" and fw["version"] == "1.2.3",
      "the tag and its bare version are both recorded")
check(fw["released"] == "2026-09-18T10:23:33Z", "the release timestamp is carried")
check(fw["app"]["bytes"] == 300 and fw["full"]["bytes"] == 400,
      "each image's size is measured from the file")
check(fw["app"]["sha256"] == hashlib.sha256(b"APP" * 100).hexdigest(),
      "...and so is its digest")
check(fw["app"]["path"] == "firmware/app.bin",
      "the published path is under firmware/, whatever the local path was")
d = tree()
rc, out = run(d, "--tag", "v1.2.3", "--app", "missing.bin", "--full", "full.bin")
check(rc == 2, "a binary that is not there is an error, not an empty digest")

print("\nthe committed tree")
p = subprocess.run([sys.executable, str(SCRIPT), "--check"],
                   capture_output=True, text=True, cwd=ROOT)
check(p.returncode == 0,
      "web/manifest.json is what partitions.csv and version.h say")
committed = json.loads((ROOT / "web" / "manifest.json").read_text())
check(committed["firmware"] is None,
      "the committed manifest names no release, so it cannot go stale against one")
check(committed["layout"]["install"]["partition"] == "app1",
      "the committed manifest installs to app1")

print()
if failures:
    print("%d failure(s):" % len(failures))
    for f in failures:
        print("  - %s" % f)
    sys.exit(1)
print("all checks passed")
