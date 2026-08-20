#!/usr/bin/env python3
"""Emit a C++ header embedding an .rfnt blob as a byte array.

The firmware has no filesystem to read fonts from at boot, so the UI font is
compiled in. Run via `make fonts` so the header never drifts from the .rfnt.

Usage: python3 tools/embed_font.py IN.rfnt --out OUT.h --symbol kUiFont
"""
import argparse
import os


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("rfnt")
    ap.add_argument("--out", required=True)
    ap.add_argument("--symbol", default="kUiFont")
    args = ap.parse_args()

    with open(args.rfnt, "rb") as f:
        data = f.read()

    src = os.path.relpath(args.rfnt).replace(os.sep, "/")
    with open(args.out, "w") as f:
        f.write(f"// Generated from {src} by tools/embed_font.py - do not edit.\n")
        f.write("// Regenerate with: make fonts\n")
        f.write("#pragma once\n#include <cstdint>\n#include <cstddef>\n")
        f.write(f"inline const uint8_t {args.symbol}[] = {{")
        f.write(",".join(str(b) for b in data))
        f.write("};\n")
        # Derived from the array so the two can never disagree.
        f.write(f"inline constexpr size_t {args.symbol}Size = sizeof({args.symbol});\n")
    print(f"{args.out}: {len(data)} bytes from {src}")


if __name__ == "__main__":
    main()
