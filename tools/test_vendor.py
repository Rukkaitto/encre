#!/usr/bin/env python3
""""Vendored, unmodified" is a claim. This checks it.

    python3 tools/test_vendor.py

WHY THIS EXISTS AT ALL. `third_party/stb_truetype.h` carries the sentence "NOT
ONE BYTE BELOW THIS BLOCK IS CHANGED" and a sha256, and CLAUDE.md records that
the claim in that file's header became false -- the edge count was patched for a
real reason and the header went on saying otherwise. A provenance comment is
worth exactly as much as whatever verifies it, and nothing did.

`web/vendor/esptool-js/bundle.js` is the file that writes firmware to somebody's
reader, so it is the worst place in this repository for an unverified claim
about what the bytes are. This asks the only two questions that matter: are the
bytes below the block the ones the upstream package shipped, and do the two
places that record their digest agree.

It CANNOT tell you the recorded digest is upstream's -- that is a fact about
npm, not about this tree, and the header records the tarball's own shasum and
the URL so a person can re-derive it. What it catches is the thing that actually
happens: somebody edits the vendored file, or replaces it without updating the
digest beside it.

Not wired into `make test`, for tools/test_compare_design.py's reason. It needs
no wiring: `make test-tools` globs tools/test_*.py.
"""
import hashlib
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
VENDOR = ROOT / "web" / "vendor" / "esptool-js"
BUNDLE = VENDOR / "bundle.js"
RECORD = VENDOR / "UPSTREAM.sha256"
LICENSE = VENDOR / "LICENSE"

# The last line of the vendoring block. Everything after it is upstream's.
MARKER = b" * -------------------------------------------------------------------------- */\n"

failures = []


def check(ok, what):
    print("  %-4s %s" % ("ok" if ok else "FAIL", what))
    if not ok:
        failures.append(what)


def split(text):
    """(block, upstream bytes), or (None, None) if the marker is not there."""
    i = text.find(MARKER)
    if i < 0:
        return None, None
    end = i + len(MARKER)
    return text[:end], text[end:]


print("the vendored bundle")
check(BUNDLE.exists(), "web/vendor/esptool-js/bundle.js is present")
check(LICENSE.exists(), "...and its LICENSE is beside it")
check(RECORD.exists(), "...and the digest is recorded in UPSTREAM.sha256")

if not (BUNDLE.exists() and RECORD.exists()):
    print("\nnothing further can be checked")
    sys.exit(1)

raw = BUNDLE.read_bytes()
block, upstream = split(raw)
check(block is not None, "the vendoring block is present and terminated")

if upstream is None:
    print("\nnothing further can be checked")
    sys.exit(1)

recorded = RECORD.read_text().split()[0]
actual = hashlib.sha256(upstream).hexdigest()
check(actual == recorded,
      "the bytes below the block hash to the recorded digest\n"
      "       recorded %s\n       actual   %s" % (recorded, actual)
      if actual != recorded else
      "the bytes below the block hash to the recorded digest")

# The header states the digest too, so the two copies must agree. They are two
# copies on purpose: the header is what a person reads at the site, and the
# .sha256 file is what a machine reads. Two copies that can disagree is exactly
# what this repository keeps getting bitten by, so they are checked against each
# other rather than left to drift.
check(recorded.encode() in block,
      "the digest in the file's own header matches UPSTREAM.sha256")

# Details a reader needs to re-derive the claim from npm.
for needle, what in (
    (b"esptool-js v0.7.0", "the header names the version"),
    (b"Apache-2.0", "...the licence"),
    (b"registry.npmjs.org", "...where it came from"),
    (b"9b891bfa886a0b3373b63f1eefe5538fea56cbe8", "...and the tarball's npm shasum"),
):
    check(needle in block, what)

# The bundle has to still BE an ES module with the names the page imports. A
# truncated or half-replaced file would pass a digest check only if the digest
# were updated with it, and this is what notices that.
for name in (b"ESPLoader", b"Transport", b"UsbJtagSerialReset",
             b"ESPRESSIF_VID", b"USB_JTAG_SERIAL_PID"):
    check(b"export{" in upstream and name in upstream,
          "the bundle still exports %s" % name.decode())

print()
if failures:
    print("%d failure(s):" % len(failures))
    for f in failures:
        print("  - %s" % f)
    print("\nIf the bundle was deliberately replaced, update UPSTREAM.sha256 AND the\n"
          "digest in the file's own header, and say in the commit which version it is.")
    sys.exit(1)
print("all checks passed")
