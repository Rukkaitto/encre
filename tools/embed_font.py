#!/usr/bin/env python3
"""Emit a C++ header embedding a font file as a byte array.

The firmware has no filesystem to read fonts from at boot -- a card may not even
be in the slot -- so every face it needs is compiled in. Run via `make fonts` so
a header never drifts from the asset it was generated from.

Either kind of asset: an .rfnt bitmap set for chrome (tools/fontc.py) or a
prepared TTF for body text (tools/ttfprep.py), which the runtime rasteriser
reads straight out of memory-mapped flash. Both are just bytes here.

Usage: python3 tools/embed_font.py IN --out OUT.h --symbol kUiFont
"""
import argparse
import os


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("blob")
    ap.add_argument("--out", required=True)
    ap.add_argument("--symbol", default="kUiFont")
    args = ap.parse_args()

    with open(args.blob, "rb") as f:
        data = f.read()

    src = os.path.relpath(args.blob).replace(os.sep, "/")
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
