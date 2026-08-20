# Phase 1: Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A repo where the hardware-free core builds and tests on macOS, the simulator renders the QuietTheme Home screen to a PNG, and the same core code puts that Home screen on a real Xteink X4.

**Architecture:** Three trees — `core/` (portable C++20, no Arduino includes, built by CMake for tests/sim and by PlatformIO as a library for firmware), `sim/` (native CLI writing PNGs), `shell/` (Arduino `main.cpp` binding core to freeink-sdk drivers). Fonts are pre-rendered offline by `tools/fontc.py` into a compact `.rfnt` bitmap format.

**Tech Stack:** C++20, CMake + doctest (desktop), stb_image_write (PNG), Python 3 + freetype-py (font converter), PlatformIO + pioarduino ESP32 platform + freeink-sdk (git submodule).

**Repo:** `~/dev/ereader-firmware`. All commands run from the repo root unless stated. The design spec is `docs/superpowers/specs/2026-08-20-ereader-firmware-v1-design.md`; the visual contract is the "E-Reader V1 UI" design canvas (V1 Screens page).

**Conventions locked in this phase (do not deviate):**
- Framebuffer is 1 bit per pixel, row-major, MSB-first within each byte
  (bit 7 = leftmost pixel), `1 = white`, `0 = black` — matching freeink-sdk's
  `clearScreen(0xFF)` = white convention. Width must be a multiple of 8.
- Core namespace is `reader::`. Headers live in `core/include/reader/`,
  sources in `core/src/`.
- Font format magic is `RFNT`, version 1, little-endian, as defined in Task 6.

---

### Task 1: Repo scaffold

**Files:**
- Create: `.gitignore`, `README.md`, `core/include/reader/.keep`, `core/src/.keep`, `sim/.keep`, `shell/src/.keep`, `tools/.keep`, `test/unit/.keep`, `test/golden/.keep`, `test/vendor/.keep`, `assets/fonts/.keep`, `assets/built/.keep`

- [ ] **Step 1: Create the directory tree and .gitignore**

```bash
mkdir -p core/include/reader core/src sim shell/src tools test/unit test/golden test/vendor assets/fonts assets/built
touch core/include/reader/.keep core/src/.keep sim/.keep shell/src/.keep tools/.keep test/unit/.keep test/golden/.keep test/vendor/.keep assets/fonts/.keep assets/built/.keep
```

Write `.gitignore`:

```gitignore
build/
.pio/
.cache/
__pycache__/
*.pyc
.DS_Store
platformio.local.ini
```

- [ ] **Step 2: Write README.md**

```markdown
# Encre

From-scratch firmware for the Xteink X4/X3 e-readers (ESP32-C3, 1-bit e-ink).

- `core/`  — portable, hardware-free C++20 (layout, fonts, view-models, themes). Builds on macOS.
- `sim/`   — desktop simulator: renders any screen to PNG at exact panel size.
- `shell/` — Arduino/PlatformIO layer binding core to freeink-sdk drivers.
- `tools/` — offline tools (font converter).
- Spec: `docs/superpowers/specs/2026-08-20-ereader-firmware-v1-design.md`

## Quickstart (desktop)
    make test        # build core + run unit/golden tests
    make sim         # render the Home screen to build/home.png

## Firmware
    pio run -e xteink            # build
    pio run -e xteink -t upload  # flash over USB-C
```

- [ ] **Step 3: Commit**

```bash
git add -A && git commit -m "chore: repo scaffold for core/sim/shell/tools split"
```

---

### Task 2: freeink-sdk submodule

**Files:**
- Create: `.gitmodules`, `freeink-sdk/` (submodule)

- [ ] **Step 1: Add the submodule**

```bash
git submodule add https://github.com/Free-Ink/freeink-sdk.git freeink-sdk
```

- [ ] **Step 2: Verify the pieces the firmware needs exist**

```bash
ls freeink-sdk/libs/display/FreeInkDisplay freeink-sdk/libs/hardware/BoardConfig freeink-sdk/platformio.sample.ini
```

Expected: all three paths listed without error.

- [ ] **Step 3: Commit**

```bash
git add .gitmodules freeink-sdk && git commit -m "chore: add freeink-sdk as submodule (MIT drivers for display/input/SD/battery)"
```

---

### Task 3: Desktop build + test harness (CMake + doctest)

**Files:**
- Create: `test/vendor/doctest.h`, `CMakeLists.txt`, `core/include/reader/version.h`, `test/unit/test_version.cpp`, `Makefile`

- [ ] **Step 1: Vendor doctest (single header, v2.4.11)**

```bash
curl -fsSL https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h -o test/vendor/doctest.h
```

- [ ] **Step 2: Write the failing test**

`test/unit/test_version.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "reader/version.h"

TEST_CASE("core reports its version") {
  CHECK(std::string(reader::kVersion) == "0.1.0");
}
```

- [ ] **Step 3: Write CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.20)
project(ereader LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB READER_CORE_SOURCES ${CMAKE_SOURCE_DIR}/core/src/*.cpp)
add_library(reader_core STATIC ${READER_CORE_SOURCES})
target_include_directories(reader_core PUBLIC ${CMAKE_SOURCE_DIR}/core/include)
# A library needs at least one TU before core sources exist:
if(NOT READER_CORE_SOURCES)
  file(WRITE ${CMAKE_BINARY_DIR}/_empty.cpp "namespace { int _unused; }\n")
  target_sources(reader_core PRIVATE ${CMAKE_BINARY_DIR}/_empty.cpp)
endif()

enable_testing()
file(GLOB TEST_SOURCES ${CMAKE_SOURCE_DIR}/test/unit/*.cpp)
add_executable(unit_tests ${TEST_SOURCES})
target_include_directories(unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/test/vendor)
target_link_libraries(unit_tests PRIVATE reader_core)
target_compile_definitions(unit_tests PRIVATE
  ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
  GOLDEN_DIR="${CMAKE_SOURCE_DIR}/test/golden"
  BUILD_DIR="${CMAKE_BINARY_DIR}")
add_test(NAME unit COMMAND unit_tests)
```

- [ ] **Step 4: Run to verify it fails**

```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: FAIL — `reader/version.h` not found.

- [ ] **Step 5: Write the minimal implementation**

`core/include/reader/version.h`:

```cpp
#pragma once
namespace reader {
inline constexpr const char* kVersion = "0.1.0";
}
```

- [ ] **Step 6: Run tests to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: `100% tests passed`.

- [ ] **Step 7: Write the Makefile**

```make
.PHONY: test sim firmware
test:
	cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
sim:
	cmake -S . -B build && cmake --build build -j --target reader_sim && ./build/reader_sim home build/home.png
firmware:
	pio run -e xteink
```

(`sim` fails until Task 10 adds the target — that's expected for now.)

- [ ] **Step 8: Commit**

```bash
git add -A && git commit -m "build: CMake + doctest desktop harness with first passing test"
```

---

### Task 4: 1-bit Framebuffer

**Files:**
- Create: `core/include/reader/framebuffer.h`, `core/src/framebuffer.cpp`
- Test: `test/unit/test_framebuffer.cpp`

- [ ] **Step 1: Write the failing tests**

`test/unit/test_framebuffer.cpp`:

```cpp
#include "doctest.h"
#include "reader/framebuffer.h"

using reader::Framebuffer;

TEST_CASE("framebuffer starts white and stores pixels MSB-first") {
  Framebuffer fb(16, 4);            // 16 px wide -> 2 bytes per row
  CHECK(fb.width() == 16);
  CHECK(fb.height() == 4);
  CHECK(fb.rowBytes() == 2);
  CHECK(fb.data()[0] == 0xFF);      // all white

  fb.setPixel(0, 0, false);         // black at leftmost pixel
  CHECK(fb.data()[0] == 0x7F);      // bit 7 cleared -> MSB is leftmost
  CHECK_FALSE(fb.getPixel(0, 0));
  CHECK(fb.getPixel(1, 0));

  fb.setPixel(0, 0, true);
  CHECK(fb.data()[0] == 0xFF);
}

TEST_CASE("fillRect clips to bounds") {
  Framebuffer fb(16, 4);
  fb.fillRect(12, 2, 100, 100, false);  // overflows right and bottom
  CHECK_FALSE(fb.getPixel(12, 2));
  CHECK_FALSE(fb.getPixel(15, 3));
  CHECK(fb.getPixel(11, 2));
  CHECK(fb.getPixel(12, 1));
}

TEST_CASE("clear repaints everything") {
  Framebuffer fb(8, 1);
  fb.fillRect(0, 0, 8, 1, false);
  fb.clear(true);
  CHECK(fb.data()[0] == 0xFF);
}
```

- [ ] **Step 2: Run to verify failure**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: FAIL — `reader/framebuffer.h` not found.

- [ ] **Step 3: Implement**

`core/include/reader/framebuffer.h`:

```cpp
#pragma once
#include <cstdint>
#include <vector>

namespace reader {

// 1bpp, row-major, MSB-first (bit 7 = leftmost pixel of the byte).
// true/1 = white, 0 = black (freeink-sdk convention). Width % 8 == 0.
class Framebuffer {
 public:
  Framebuffer(int width, int height);

  int width() const { return width_; }
  int height() const { return height_; }
  int rowBytes() const { return width_ / 8; }
  const uint8_t* data() const { return bytes_.data(); }
  uint8_t* data() { return bytes_.data(); }
  int sizeBytes() const { return static_cast<int>(bytes_.size()); }

  void clear(bool white = true);
  void setPixel(int x, int y, bool white);
  bool getPixel(int x, int y) const;
  void fillRect(int x, int y, int w, int h, bool white);

 private:
  int width_;
  int height_;
  std::vector<uint8_t> bytes_;
};

}  // namespace reader
```

`core/src/framebuffer.cpp`:

```cpp
#include "reader/framebuffer.h"

#include <cstring>

namespace reader {

Framebuffer::Framebuffer(int width, int height)
    : width_(width), height_(height), bytes_(static_cast<size_t>(width / 8) * height, 0xFF) {}

void Framebuffer::clear(bool white) {
  std::memset(bytes_.data(), white ? 0xFF : 0x00, bytes_.size());
}

void Framebuffer::setPixel(int x, int y, bool white) {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
  uint8_t& b = bytes_[static_cast<size_t>(y) * rowBytes() + x / 8];
  const uint8_t mask = static_cast<uint8_t>(0x80u >> (x % 8));
  if (white) b |= mask; else b &= static_cast<uint8_t>(~mask);
}

bool Framebuffer::getPixel(int x, int y) const {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) return true;
  const uint8_t b = bytes_[static_cast<size_t>(y) * rowBytes() + x / 8];
  return (b >> (7 - x % 8)) & 1;
}

void Framebuffer::fillRect(int x, int y, int w, int h, bool white) {
  for (int yy = y; yy < y + h; ++yy)
    for (int xx = x; xx < x + w; ++xx)
      setPixel(xx, yy, white);
}

}  // namespace reader
```

(Per-pixel fillRect is fine at this stage — YAGNI; optimize when a profile says so.)

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add core test/unit/test_framebuffer.cpp && git commit -m "feat(core): 1-bit MSB-first framebuffer with clipped drawing"
```

---

### Task 5: PNG writer for the simulator and golden tests

**Files:**
- Create: `test/vendor/stb_image_write.h`, `test/vendor/stb_image.h`, `core/include/reader/png.h`, `core/src/png.cpp` *(desktop-only helpers live in core for simplicity; the shell never calls them)*
- Test: `test/unit/test_png.cpp`

- [ ] **Step 1: Vendor stb (pinned commit)**

```bash
curl -fsSL https://raw.githubusercontent.com/nothings/stb/5c205738c191bcb0abc65c4febfa9bd25ff35234/stb_image_write.h -o test/vendor/stb_image_write.h
curl -fsSL https://raw.githubusercontent.com/nothings/stb/5c205738c191bcb0abc65c4febfa9bd25ff35234/stb_image.h -o test/vendor/stb_image.h
```

Add the vendor dir to the core target in `CMakeLists.txt` (after `target_include_directories(reader_core PUBLIC ...)`):

```cmake
target_include_directories(reader_core PRIVATE ${CMAKE_SOURCE_DIR}/test/vendor)
target_compile_definitions(reader_core PRIVATE READER_DESKTOP=1)
```

- [ ] **Step 2: Write the failing test**

`test/unit/test_png.cpp`:

```cpp
#include <cstdio>
#include <string>

#include "doctest.h"
#include "reader/framebuffer.h"
#include "reader/png.h"

TEST_CASE("writePng emits a decodable 1-bit image") {
  reader::Framebuffer fb(16, 8);
  fb.fillRect(0, 0, 8, 8, false);  // left half black
  const std::string path = std::string(BUILD_DIR) + "/test_png_out.png";
  REQUIRE(reader::writePng(fb, path.c_str()));

  int w = 0, h = 0;
  REQUIRE(reader::comparePng(fb, path.c_str(), &w, &h));
  CHECK(w == 16);
  CHECK(h == 8);
  std::remove(path.c_str());
}
```

- [ ] **Step 3: Implement**

`core/include/reader/png.h`:

```cpp
#pragma once
namespace reader {
class Framebuffer;
// Desktop-only (guarded by READER_DESKTOP): write fb as an 8-bit grayscale
// PNG (0x00 black / 0xFF white); compare fb against a PNG on disk.
bool writePng(const Framebuffer& fb, const char* path);
bool comparePng(const Framebuffer& fb, const char* path, int* wOut = nullptr, int* hOut = nullptr);
}  // namespace reader
```

`core/src/png.cpp`:

```cpp
#include "reader/png.h"

#ifdef READER_DESKTOP
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <vector>

#include "reader/framebuffer.h"

namespace reader {

static std::vector<unsigned char> toGray(const Framebuffer& fb) {
  std::vector<unsigned char> px(static_cast<size_t>(fb.width()) * fb.height());
  for (int y = 0; y < fb.height(); ++y)
    for (int x = 0; x < fb.width(); ++x)
      px[static_cast<size_t>(y) * fb.width() + x] = fb.getPixel(x, y) ? 0xFF : 0x00;
  return px;
}

bool writePng(const Framebuffer& fb, const char* path) {
  auto px = toGray(fb);
  return stbi_write_png(path, fb.width(), fb.height(), 1, px.data(), fb.width()) != 0;
}

bool comparePng(const Framebuffer& fb, const char* path, int* wOut, int* hOut) {
  int w = 0, h = 0, n = 0;
  unsigned char* img = stbi_load(path, &w, &h, &n, 1);
  if (!img) return false;
  if (wOut) *wOut = w;
  if (hOut) *hOut = h;
  bool same = (w == fb.width() && h == fb.height());
  if (same) {
    auto px = toGray(fb);
    for (size_t i = 0; i < px.size() && same; ++i) same = (px[i] == img[i]);
  }
  stbi_image_free(img);
  return same;
}

}  // namespace reader
#endif  // READER_DESKTOP
```

- [ ] **Step 4: Run tests**

```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat(core): desktop PNG write/compare for simulator and golden tests"
```

---

### Task 6: Fonts — assets and the offline converter

**Files:**
- Create: `assets/fonts/Literata.ttf`, `assets/fonts/SpaceGrotesk.ttf`, `assets/fonts/OFL-NOTICE.md`, `tools/fontc.py`, `tools/requirements.txt`
- Generated (committed): `assets/built/spacegrotesk_16.rfnt`, `assets/built/literata_18.rfnt`

**`.rfnt` v1 format (little-endian), the contract for Tasks 6–7:**

```
Header (16 bytes): magic "RFNT" | u16 version=1 | u16 glyphCount |
                   i16 ascent | i16 descent | i16 lineGap | u16 kernCount
Glyph records (glyphCount x 18 bytes):
  u32 codepoint | i16 advance | i16 bitmapW | i16 bitmapH |
  i16 xOff | i16 yOff (from baseline to bitmap top, positive up) | u32 bitmapOffset
Kerning records (kernCount x 12 bytes): u32 left | u32 right | i32 adjust
Bitmap blob: per glyph, rows packed 1bpp MSB-first, rowBytes = ceil(bitmapW/8),
  bit 1 = ink (black). bitmapOffset is relative to blob start.
```

- [ ] **Step 1: Download the two fonts (OFL-licensed) and note the license**

```bash
curl -fsSL "https://github.com/google/fonts/raw/main/ofl/literata/Literata%5Bopsz%2Cwght%5D.ttf" -o assets/fonts/Literata.ttf
curl -fsSL "https://github.com/google/fonts/raw/main/ofl/spacegrotesk/SpaceGrotesk%5Bwght%5D.ttf" -o assets/fonts/SpaceGrotesk.ttf
```

`assets/fonts/OFL-NOTICE.md`:

```markdown
Literata (c) The Literata Project Authors, and Space Grotesk (c) Florian
Karsten — both licensed under the SIL Open Font License 1.1
(https://openfontlicense.org). Embedded here as converted bitmap fonts.
```

- [ ] **Step 2: Write the converter**

`tools/requirements.txt`:

```
freetype-py==2.5.1
```

`tools/fontc.py`:

```python
#!/usr/bin/env python3
"""fontc: TTF/OTF -> .rfnt v1 bitmap font (1bpp, mono-rendered, with kerning).

Usage: python3 tools/fontc.py FONT.ttf --size 18 --out out.rfnt
"""
import argparse
import struct

import freetype

# ASCII + Latin-1 + typographic set used by the UI and books.
CODEPOINTS = (
    list(range(0x20, 0x7F))
    + list(range(0xA0, 0x100))
    + [0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026, 0x2039, 0x203A]
)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("font")
    ap.add_argument("--size", type=int, required=True, help="pixel size")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    face = freetype.Face(args.font)
    face.set_pixel_sizes(0, args.size)

    glyphs, blob = [], bytearray()
    for cp in CODEPOINTS:
        if face.get_char_index(cp) == 0:
            continue
        face.load_char(chr(cp), freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
        g = face.glyph
        bmp = g.bitmap
        row_bytes = (bmp.width + 7) // 8
        packed = bytearray(row_bytes * bmp.rows)
        for row in range(bmp.rows):
            src = bmp.buffer[row * bmp.pitch : row * bmp.pitch + row_bytes]
            packed[row * row_bytes : (row + 1) * row_bytes] = bytes(src)
        glyphs.append(
            (cp, g.advance.x // 64, bmp.width, bmp.rows, g.bitmap_left, g.bitmap_top, len(blob))
        )
        blob += packed

    kerns = []
    if face.has_kerning:
        present = [g[0] for g in glyphs]
        for left in present:
            for right in present:
                k = face.get_kerning(chr(left), chr(right)).x // 64
                if k:
                    kerns.append((left, right, k))

    ascent = face.size.ascender // 64
    descent = face.size.descender // 64  # negative
    line_gap = (face.size.height // 64) - ascent + descent

    with open(args.out, "wb") as f:
        f.write(struct.pack("<4sHHhhhH", b"RFNT", 1, len(glyphs), ascent, descent, line_gap, min(len(kerns), 0xFFFF)))
        for cp, adv, w, h, xo, yo, off in glyphs:
            f.write(struct.pack("<IhhhhhI", cp, adv, w, h, xo, yo, off))
        for left, right, adj in kerns[:0xFFFF]:
            f.write(struct.pack("<IIi", left, right, adj))
        f.write(bytes(blob))
    print(f"{args.out}: {len(glyphs)} glyphs, {len(kerns)} kern pairs, blob {len(blob)} bytes")


if __name__ == "__main__":
    main()
```

- [ ] **Step 3: Generate the two phase-1 fonts and sanity-check**

```bash
python3 -m pip install -r tools/requirements.txt
python3 tools/fontc.py assets/fonts/SpaceGrotesk.ttf --size 16 --out assets/built/spacegrotesk_16.rfnt
python3 tools/fontc.py assets/fonts/Literata.ttf --size 18 --out assets/built/literata_18.rfnt
python3 - <<'EOF'
import struct
for p in ("assets/built/spacegrotesk_16.rfnt", "assets/built/literata_18.rfnt"):
    hdr = open(p, "rb").read(16)
    magic, ver, count, asc, desc, gap, kerns = struct.unpack("<4sHHhhhH", hdr)
    assert magic == b"RFNT" and ver == 1 and count > 180 and asc > 0 and desc < 0, p
    print(p, "ok:", count, "glyphs")
EOF
```

Expected: both files print `ok:` with >180 glyphs.

- [ ] **Step 4: Commit**

```bash
git add assets tools && git commit -m "feat(fonts): OFL font assets + fontc TTF->rfnt converter with kerning"
```

---

### Task 7: Font loader and text measurement (core)

**Files:**
- Create: `core/include/reader/font.h`, `core/src/font.cpp`
- Test: `test/unit/test_font.cpp`

- [ ] **Step 1: Write the failing tests**

`test/unit/test_font.cpp`:

```cpp
#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/font.h"

static std::vector<uint8_t> slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  REQUIRE(f.good());
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

TEST_CASE("font loads and measures text") {
  auto bytes = slurp(std::string(ASSETS_DIR) + "/built/spacegrotesk_16.rfnt");
  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));

  CHECK(font.ascent() > 8);
  CHECK(font.descent() < 0);
  CHECK(font.glyph(U'A') != nullptr);
  CHECK(font.glyph(U'é') != nullptr);   // é (Latin-1)
  CHECK(font.glyph(U'—') != nullptr);   // em dash
  CHECK(font.glyph(0x1F600) == nullptr);     // no emoji

  const int wa = font.measure("A");
  const int wab = font.measure("AB");
  CHECK(wa > 0);
  CHECK(wab > wa);
  CHECK(font.measure("") == 0);
}

TEST_CASE("kerning changes measure when pairs exist") {
  auto bytes = slurp(std::string(ASSETS_DIR) + "/built/literata_18.rfnt");
  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));
  // "AV" should not be wider than advance sum (kerning tightens or is zero).
  const auto* a = font.glyph(U'A');
  const auto* v = font.glyph(U'V');
  REQUIRE(a); REQUIRE(v);
  CHECK(font.measure("AV") <= a->advance + v->advance);
}
```

- [ ] **Step 2: Run to verify failure** — `cmake --build build && ctest --test-dir build --output-on-failure` → FAIL (`reader/font.h` not found).

- [ ] **Step 3: Implement**

`core/include/reader/font.h`:

```cpp
#pragma once
#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace reader {

struct Glyph {
  int16_t advance, bitmapW, bitmapH, xOff, yOff;  // yOff: baseline to bitmap top, +up
  const uint8_t* bitmap;                          // 1bpp MSB-first, 1 = ink
  int rowBytes() const { return (bitmapW + 7) / 8; }
};

// Zero-copy view over an .rfnt blob; the blob must outlive the Font.
class Font {
 public:
  bool load(const uint8_t* data, size_t size);
  const Glyph* glyph(char32_t cp) const;
  int kerning(char32_t left, char32_t right) const;
  int ascent() const { return ascent_; }
  int descent() const { return descent_; }
  int lineHeight() const { return ascent_ - descent_ + lineGap_; }
  int measure(std::string_view utf8) const;

 private:
  std::unordered_map<char32_t, Glyph> glyphs_;
  std::unordered_map<uint64_t, int32_t> kerns_;
  int ascent_ = 0, descent_ = 0, lineGap_ = 0;
};

// Decodes one UTF-8 code point starting at s[i]; advances i. Invalid -> U+FFFD.
char32_t utf8Next(std::string_view s, size_t& i);

}  // namespace reader
```

`core/src/font.cpp`:

```cpp
#include "reader/font.h"

#include <cstring>

namespace reader {

namespace {
template <typename T>
T rd(const uint8_t* p) {  // little-endian read
  T v;
  std::memcpy(&v, p, sizeof(T));
  return v;
}
}  // namespace

bool Font::load(const uint8_t* d, size_t size) {
  if (size < 16 || std::memcmp(d, "RFNT", 4) != 0 || rd<uint16_t>(d + 4) != 1) return false;
  const uint16_t glyphCount = rd<uint16_t>(d + 6);
  ascent_ = rd<int16_t>(d + 8);
  descent_ = rd<int16_t>(d + 10);
  lineGap_ = rd<int16_t>(d + 12);
  const uint16_t kernCount = rd<uint16_t>(d + 14);

  const size_t glyphsAt = 16;
  const size_t kernsAt = glyphsAt + glyphCount * 18u;
  const size_t blobAt = kernsAt + kernCount * 12u;
  if (blobAt > size) return false;

  for (uint16_t i = 0; i < glyphCount; ++i) {
    const uint8_t* g = d + glyphsAt + i * 18u;
    Glyph gl;
    const uint32_t cp = rd<uint32_t>(g);
    gl.advance = rd<int16_t>(g + 4);
    gl.bitmapW = rd<int16_t>(g + 6);
    gl.bitmapH = rd<int16_t>(g + 8);
    gl.xOff = rd<int16_t>(g + 10);
    gl.yOff = rd<int16_t>(g + 12);
    gl.bitmap = d + blobAt + rd<uint32_t>(g + 14);
    glyphs_.emplace(static_cast<char32_t>(cp), gl);
  }
  for (uint16_t i = 0; i < kernCount; ++i) {
    const uint8_t* k = d + kernsAt + i * 12u;
    const uint64_t key = (static_cast<uint64_t>(rd<uint32_t>(k)) << 32) | rd<uint32_t>(k + 4);
    kerns_.emplace(key, rd<int32_t>(k + 8));
  }
  return true;
}

const Glyph* Font::glyph(char32_t cp) const {
  auto it = glyphs_.find(cp);
  return it == glyphs_.end() ? nullptr : &it->second;
}

int Font::kerning(char32_t l, char32_t r) const {
  auto it = kerns_.find((static_cast<uint64_t>(l) << 32) | r);
  return it == kerns_.end() ? 0 : it->second;
}

int Font::measure(std::string_view utf8) const {
  int w = 0;
  char32_t prev = 0;
  for (size_t i = 0; i < utf8.size();) {
    const char32_t cp = utf8Next(utf8, i);
    const Glyph* g = glyph(cp);
    if (!g) continue;
    if (prev) w += kerning(prev, cp);
    w += g->advance;
    prev = cp;
  }
  return w;
}

char32_t utf8Next(std::string_view s, size_t& i) {
  const auto b0 = static_cast<uint8_t>(s[i]);
  auto cont = [&](size_t n) -> char32_t {
    char32_t cp = b0 & (0x7F >> (n + 1));
    for (size_t k = 1; k <= n; ++k) {
      if (i + k >= s.size()) { i = s.size(); return 0xFFFD; }
      cp = (cp << 6) | (static_cast<uint8_t>(s[i + k]) & 0x3F);
    }
    i += n + 1;
    return cp;
  };
  if (b0 < 0x80) { ++i; return b0; }
  if ((b0 >> 5) == 0x6) return cont(1);
  if ((b0 >> 4) == 0xE) return cont(2);
  if ((b0 >> 3) == 0x1E) return cont(3);
  ++i;
  return 0xFFFD;
}

}  // namespace reader
```

- [ ] **Step 4: Run tests to verify they pass** — `cmake --build build && ctest --test-dir build --output-on-failure` → PASS.

- [ ] **Step 5: Commit**

```bash
git add core test/unit/test_font.cpp && git commit -m "feat(core): rfnt font loader with kerning, UTF-8 decode, text measure"
```

---

### Task 8: Text rendering + first golden test

**Files:**
- Create: `core/include/reader/text.h`, `core/src/text.cpp`
- Test: `test/unit/test_text_golden.cpp`, `test/golden/text_sample.png` (generated then committed)

- [ ] **Step 1: Write the failing test**

`test/unit/test_text_golden.cpp`:

```cpp
#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/font.h"
#include "reader/framebuffer.h"
#include "reader/png.h"
#include "reader/text.h"

static std::vector<uint8_t> slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  REQUIRE(f.good());
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

TEST_CASE("text render matches golden") {
  auto bytes = slurp(std::string(ASSETS_DIR) + "/built/literata_18.rfnt");
  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));

  reader::Framebuffer fb(480, 64);
  reader::drawText(fb, font, 12, 40, "Middlemarch — 6% · page 53");

  const std::string golden = std::string(GOLDEN_DIR) + "/text_sample.png";
  if (!std::ifstream(golden).good()) {
    // First run: write the candidate for human review, then fail loudly.
    reader::writePng(fb, (std::string(BUILD_DIR) + "/text_sample_candidate.png").c_str());
    FAIL("golden missing - inspect build/text_sample_candidate.png, then copy to test/golden/text_sample.png");
  }
  CHECK(reader::comparePng(fb, golden.c_str()));
}
```

- [ ] **Step 2: Implement**

`core/include/reader/text.h`:

```cpp
#pragma once
#include <string_view>

namespace reader {
class Framebuffer;
class Font;

// Draws UTF-8 text with kerning; (x, baselineY) is the pen origin.
// Returns the advance width consumed. Ink is black on the framebuffer.
int drawText(Framebuffer& fb, const Font& font, int x, int baselineY, std::string_view utf8);
}  // namespace reader
```

`core/src/text.cpp`:

```cpp
#include "reader/text.h"

#include "reader/font.h"
#include "reader/framebuffer.h"

namespace reader {

int drawText(Framebuffer& fb, const Font& font, int x, int baselineY, std::string_view utf8) {
  int pen = x;
  char32_t prev = 0;
  for (size_t i = 0; i < utf8.size();) {
    const char32_t cp = utf8Next(utf8, i);
    const Glyph* g = font.glyph(cp);
    if (!g) continue;
    if (prev) pen += font.kerning(prev, cp);
    for (int row = 0; row < g->bitmapH; ++row) {
      const uint8_t* src = g->bitmap + row * g->rowBytes();
      for (int col = 0; col < g->bitmapW; ++col)
        if ((src[col / 8] >> (7 - col % 8)) & 1)
          fb.setPixel(pen + g->xOff + col, baselineY - g->yOff + row, false);
    }
    pen += g->advance;
    prev = cp;
  }
  return pen - x;
}

}  // namespace reader
```

- [ ] **Step 3: Run, inspect, bless the golden**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
open build/text_sample_candidate.png   # verify: crisp serif text, correct dash/middle dot
cp build/text_sample_candidate.png test/golden/text_sample.png
ctest --test-dir build --output-on-failure
```

Expected: first run FAILs with the "golden missing" message; after blessing, PASS.

- [ ] **Step 4: Commit**

```bash
git add core test/unit/test_text_golden.cpp test/golden/text_sample.png
git commit -m "feat(core): kerned glyph rendering with golden-PNG regression test"
```

---

### Task 9: View-model + theme interface + QuietTheme Home (phase-1 subset)

**Files:**
- Create: `core/include/reader/viewmodel.h`, `core/include/reader/theme.h`, `core/include/reader/theme_quiet.h`, `core/src/theme_quiet.cpp`
- Test: `test/unit/test_theme_home_golden.cpp`, `test/golden/home_quiet.png` (blessed like Task 8)

Phase-1 scope: text, rules, filled blocks, progress bar, and the 4-slot hint
bar as **text-only labels** (icons, cover art, and dithering arrive in
Phase 2). Layout numbers come from the design canvas (B2 Quiet Home board).

- [ ] **Step 1: Write the failing golden test**

`test/unit/test_theme_home_golden.cpp`:

```cpp
#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/framebuffer.h"
#include "reader/png.h"
#include "reader/theme_quiet.h"
#include "reader/viewmodel.h"

static std::vector<uint8_t> slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  REQUIRE(f.good());
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

TEST_CASE("QuietTheme renders Home to golden") {
  auto ui = slurp(std::string(ASSETS_DIR) + "/built/spacegrotesk_16.rfnt");
  reader::QuietTheme theme;
  REQUIRE(theme.loadFonts(ui.data(), ui.size()));

  reader::HomeViewModel vm;
  vm.title = "Middlemarch";
  vm.author = "George Eliot";
  vm.chapterLabel = "CH. 01 — MISS BROOKE";
  vm.percent = 6;
  vm.currentPage = 53;
  vm.pageCount = 890;
  vm.batteryPercent = 87;
  vm.menu = {{"LIBRARY", "12"}, {"ARTICLES", "3 UNREAD"}, {"SETTINGS", ""}};
  vm.focusedMenuIndex = -1;  // focus on Continue
  vm.hints = {"READ", "SELECT", "UP", "DOWN"};

  reader::Framebuffer fb(480, 800);
  theme.renderHome(fb, vm);

  const std::string golden = std::string(GOLDEN_DIR) + "/home_quiet.png";
  if (!std::ifstream(golden).good()) {
    reader::writePng(fb, (std::string(BUILD_DIR) + "/home_quiet_candidate.png").c_str());
    FAIL("golden missing - inspect build/home_quiet_candidate.png, then copy to test/golden/home_quiet.png");
  }
  CHECK(reader::comparePng(fb, golden.c_str()));
}
```

- [ ] **Step 2: Implement the view-model and theme interface**

`core/include/reader/viewmodel.h`:

```cpp
#pragma once
#include <array>
#include <string>
#include <vector>

namespace reader {

struct MenuEntry {
  std::string label;
  std::string value;
};

// Semantic content + interaction state only. No geometry, no style.
struct HomeViewModel {
  std::string title;
  std::string author;
  std::string chapterLabel;
  int percent = 0;
  int currentPage = 0;
  int pageCount = 0;
  int batteryPercent = 0;
  std::vector<MenuEntry> menu;
  int focusedMenuIndex = -1;                 // -1 = Continue block focused
  std::array<std::string, 4> hints{};        // Back, Confirm, Up, Down slots
};

}  // namespace reader
```

`core/include/reader/theme.h`:

```cpp
#pragma once
namespace reader {
class Framebuffer;
struct HomeViewModel;

// Themes own the entire presentation (spec 3.3). Phase 1: Home only;
// later phases extend this interface one screen at a time.
class Theme {
 public:
  virtual ~Theme() = default;
  virtual void renderHome(Framebuffer& fb, const HomeViewModel& vm) = 0;
};
}  // namespace reader
```

`core/include/reader/theme_quiet.h`:

```cpp
#pragma once
#include "reader/font.h"
#include "reader/theme.h"

namespace reader {

// B2 "Quiet": Space Grotesk chrome, 2px header rule, hairline rows,
// black fill = focus. Layout constants follow the design canvas.
class QuietTheme : public Theme {
 public:
  bool loadFonts(const uint8_t* uiFontData, size_t uiFontSize);
  void renderHome(Framebuffer& fb, const HomeViewModel& vm) override;

 private:
  Font ui_;
  void headerBand(Framebuffer& fb, const char* label, const std::string& right);
  void hintBar(Framebuffer& fb, const std::array<std::string, 4>& hints);
  void textInverted(Framebuffer& fb, int x, int baseline, std::string_view s);
};

}  // namespace reader
```

`core/src/theme_quiet.cpp`:

```cpp
#include "reader/theme_quiet.h"

#include "reader/framebuffer.h"
#include "reader/text.h"
#include "reader/viewmodel.h"

namespace reader {

namespace {
constexpr int kMargin = 24;
constexpr int kHeaderH = 52;
constexpr int kRowH = 56;
constexpr int kHintH = 46;
}  // namespace

bool QuietTheme::loadFonts(const uint8_t* d, size_t n) { return ui_.load(d, n); }

void QuietTheme::textInverted(Framebuffer& fb, int x, int baseline, std::string_view s) {
  // Draw white-on-black: render into place by flipping pixels the glyphs touch.
  // Simple approach: draw black text into a scratch fb, then invert-copy.
  Framebuffer scratch(fb.width(), fb.height());
  drawText(scratch, ui_, x, baseline, s);
  for (int y = 0; y < fb.height(); ++y)
    for (int xx = 0; xx < fb.width(); ++xx)
      if (!scratch.getPixel(xx, y)) fb.setPixel(xx, y, true);
}

void QuietTheme::headerBand(Framebuffer& fb, const char* label, const std::string& right) {
  const int baseline = 33;
  drawText(fb, ui_, kMargin, baseline, label);
  const int rw = ui_.measure(right);
  drawText(fb, ui_, fb.width() - kMargin - rw, baseline, right);
  fb.fillRect(0, kHeaderH - 2, fb.width(), 2, false);
}

void QuietTheme::hintBar(Framebuffer& fb, const std::array<std::string, 4>& hints) {
  const int top = fb.height() - kHintH;
  fb.fillRect(0, top, fb.width(), 1, false);
  const int baseline = top + 30;
  // 4 fixed slots: Back left, Confirm center-left, Up, Down right.
  const int slotX[4] = {kMargin, 170, 330, 408};
  for (int i = 0; i < 4; ++i)
    if (!hints[i].empty()) drawText(fb, ui_, slotX[i], baseline, hints[i]);
}

void QuietTheme::renderHome(Framebuffer& fb, const HomeViewModel& vm) {
  fb.clear(true);
  headerBand(fb, "NOW READING", std::to_string(vm.batteryPercent) + "%");

  int y = kHeaderH + 30;
  drawText(fb, ui_, kMargin, y + ui_.ascent(), vm.title);
  y += ui_.lineHeight() + 4;
  drawText(fb, ui_, kMargin, y + ui_.ascent(), vm.author);
  y += ui_.lineHeight() + 18;

  const std::string pct = std::to_string(vm.percent) + "%";
  drawText(fb, ui_, kMargin, y + ui_.ascent(), pct);
  const std::string page =
      "PAGE " + std::to_string(vm.currentPage) + " / " + std::to_string(vm.pageCount);
  drawText(fb, ui_, kMargin + 90, y + ui_.ascent(), page);
  y += ui_.lineHeight() + 6;
  drawText(fb, ui_, kMargin, y + ui_.ascent(), vm.chapterLabel);
  y += ui_.lineHeight() + 22;

  // Progress bar: 1px border, filled portion black.
  const int barW = fb.width() - 2 * kMargin;
  fb.fillRect(kMargin, y, barW, 8, false);
  fb.fillRect(kMargin + 1, y + 1, barW - 2, 6, true);
  fb.fillRect(kMargin + 1, y + 1, (barW - 2) * vm.percent / 100, 6, false);
  y += 8 + 22;

  // Continue block (focused when focusedMenuIndex == -1): black fill + inverted text.
  const int blockH = 52;
  if (vm.focusedMenuIndex == -1) {
    fb.fillRect(kMargin, y, barW, blockH, false);
    textInverted(fb, kMargin + 20, y + 33, "CONTINUE");
  } else {
    fb.fillRect(kMargin, y, barW, 2, false);
    fb.fillRect(kMargin, y + blockH - 2, barW, 2, false);
    fb.fillRect(kMargin, y, 2, blockH, false);
    fb.fillRect(kMargin + barW - 2, y, 2, blockH, false);
    drawText(fb, ui_, kMargin + 20, y + 33, "CONTINUE");
  }

  // Menu rows anchored above the hint bar.
  const int menuTop = fb.height() - kHintH - static_cast<int>(vm.menu.size()) * kRowH;
  for (size_t i = 0; i < vm.menu.size(); ++i) {
    const int ry = menuTop + static_cast<int>(i) * kRowH;
    const bool focused = (static_cast<int>(i) == vm.focusedMenuIndex);
    if (focused) {
      fb.fillRect(0, ry, fb.width(), kRowH, false);
      textInverted(fb, kMargin, ry + 35, vm.menu[i].label);
      const int rw = ui_.measure(vm.menu[i].value);
      textInverted(fb, fb.width() - kMargin - rw, ry + 35, vm.menu[i].value);
    } else {
      fb.fillRect(0, ry, fb.width(), 1, false);
      drawText(fb, ui_, kMargin, ry + 35, vm.menu[i].label);
      const int rw = ui_.measure(vm.menu[i].value);
      drawText(fb, ui_, fb.width() - kMargin - rw, ry + 35, vm.menu[i].value);
    }
  }

  hintBar(fb, vm.hints);
}

}  // namespace reader
```

- [ ] **Step 3: Run, inspect against the design canvas, bless the golden**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
open build/home_quiet_candidate.png   # compare with the B2 Quiet Home board
cp build/home_quiet_candidate.png test/golden/home_quiet.png
ctest --test-dir build --output-on-failure
```

Expected: PASS after blessing.

- [ ] **Step 4: Commit**

```bash
git add core test/unit/test_theme_home_golden.cpp test/golden/home_quiet.png
git commit -m "feat(core): view-model + theme interface, QuietTheme Home (phase-1 subset)"
```

---

### Task 10: Simulator CLI

**Files:**
- Create: `sim/main.cpp`
- Modify: `CMakeLists.txt` (add target)

- [ ] **Step 1: Write sim/main.cpp**

```cpp
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "reader/framebuffer.h"
#include "reader/png.h"
#include "reader/theme_quiet.h"
#include "reader/viewmodel.h"

static std::vector<uint8_t> slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f.good()) return {};
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

int main(int argc, char** argv) {
  if (argc < 3 || std::string(argv[1]) != "home") {
    std::fprintf(stderr, "usage: reader_sim home OUT.png\n");
    return 2;
  }
  auto ui = slurp(std::string(ASSETS_DIR) + "/built/spacegrotesk_16.rfnt");
  reader::QuietTheme theme;
  if (ui.empty() || !theme.loadFonts(ui.data(), ui.size())) {
    std::fprintf(stderr, "failed to load ui font\n");
    return 1;
  }
  reader::HomeViewModel vm;
  vm.title = "Middlemarch";
  vm.author = "George Eliot";
  vm.chapterLabel = "CH. 01 — MISS BROOKE";
  vm.percent = 6;
  vm.currentPage = 53;
  vm.pageCount = 890;
  vm.batteryPercent = 87;
  vm.menu = {{"LIBRARY", "12"}, {"ARTICLES", "3 UNREAD"}, {"SETTINGS", ""}};
  vm.focusedMenuIndex = -1;
  vm.hints = {"READ", "SELECT", "UP", "DOWN"};

  reader::Framebuffer fb(480, 800);
  theme.renderHome(fb, vm);
  if (!reader::writePng(fb, argv[2])) return 1;
  std::printf("wrote %s\n", argv[2]);
  return 0;
}
```

- [ ] **Step 2: Add the CMake target**

Append to `CMakeLists.txt`:

```cmake
add_executable(reader_sim ${CMAKE_SOURCE_DIR}/sim/main.cpp)
target_link_libraries(reader_sim PRIVATE reader_core)
target_compile_definitions(reader_sim PRIVATE ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME sim_home COMMAND reader_sim home ${CMAKE_BINARY_DIR}/sim_home.png)
```

- [ ] **Step 3: Run**

```bash
make sim && open build/home.png
ctest --test-dir build --output-on-failure
```

Expected: PNG opens showing the Home screen; all tests PASS.

- [ ] **Step 4: Commit**

```bash
git add sim CMakeLists.txt && git commit -m "feat(sim): CLI renders QuietTheme Home to PNG"
```

---

### Task 11: Firmware scaffold — Home screen on the device

**Files:**
- Create: `platformio.ini`, `core/library.json`, `shell/src/main.cpp`, `core/src/rotate.cpp`, `core/include/reader/rotate.h`
- Test: `test/unit/test_rotate.cpp`

- [ ] **Step 1: Write the failing rotation test** (the panel is 800×480 landscape; the UI renders 480×800 portrait)

`test/unit/test_rotate.cpp`:

```cpp
#include "doctest.h"
#include "reader/framebuffer.h"
#include "reader/rotate.h"

TEST_CASE("rotate90CW maps portrait to landscape") {
  reader::Framebuffer portrait(8, 16);   // w=8, h=16
  reader::Framebuffer landscape(16, 8);  // w=16, h=8
  portrait.setPixel(0, 0, false);        // top-left of portrait
  reader::rotate90CW(portrait, landscape);
  // Portrait top-left lands at landscape top-right.
  CHECK_FALSE(landscape.getPixel(15, 0));
  CHECK(landscape.getPixel(0, 0));
}
```

- [ ] **Step 2: Run to verify failure** — FAIL (`reader/rotate.h` not found).

- [ ] **Step 3: Implement**

`core/include/reader/rotate.h`:

```cpp
#pragma once
namespace reader {
class Framebuffer;
// dst(w=src.h, h=src.w): dst[x][y] = src rotated 90 deg clockwise.
// If the panel image appears upside down on hardware, switch main.cpp to
// rotate90CCW (both mappings provided so bring-up is a one-line change).
void rotate90CW(const Framebuffer& src, Framebuffer& dst);
void rotate90CCW(const Framebuffer& src, Framebuffer& dst);
}  // namespace reader
```

`core/src/rotate.cpp`:

```cpp
#include "reader/rotate.h"

#include "reader/framebuffer.h"

namespace reader {

void rotate90CW(const Framebuffer& src, Framebuffer& dst) {
  for (int y = 0; y < src.height(); ++y)
    for (int x = 0; x < src.width(); ++x)
      dst.setPixel(src.height() - 1 - y, x, src.getPixel(x, y));
}

void rotate90CCW(const Framebuffer& src, Framebuffer& dst) {
  for (int y = 0; y < src.height(); ++y)
    for (int x = 0; x < src.width(); ++x)
      dst.setPixel(y, src.width() - 1 - x, src.getPixel(x, y));
}

}  // namespace reader
```

- [ ] **Step 4: Run tests** — PASS. Commit:

```bash
git add core test/unit/test_rotate.cpp && git commit -m "feat(core): 1bpp 90-degree rotation for portrait UI on landscape panel"
```

- [ ] **Step 5: Make core consumable by PlatformIO**

`core/library.json`:

```json
{
  "name": "ReaderCore",
  "version": "0.1.0",
  "build": {
    "srcDir": "src",
    "includeDir": "include",
    "flags": ["-std=gnu++2a"],
    "srcFilter": ["+<*>", "-<png.cpp>"]
  }
}
```

(`png.cpp` is desktop-only; excluding it keeps stb out of the firmware.)

- [ ] **Step 6: Write platformio.ini** (adapted from `freeink-sdk/platformio.sample.ini`, `[env:xteink]`)

```ini
[platformio]
default_envs = xteink
src_dir = shell/src

[base]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.37/platform-espressif32.zip
framework = arduino
monitor_speed = 115200
upload_speed = 921600
build_flags =
  -std=gnu++2a
  -DARDUINO_USB_MODE=1
  -DARDUINO_USB_CDC_ON_BOOT=1
  -DEINK_DISPLAY_SINGLE_BUFFER_MODE=1
build_unflags =
  -std=gnu++11
board_build.flash_mode = dio
board_build.flash_size = 16MB
lib_deps =
  ReaderCore=symlink://core
  BoardConfig=symlink://freeink-sdk/libs/hardware/BoardConfig
  EInkDisplay=symlink://freeink-sdk/libs/display/FreeInkDisplay
  InputManager=symlink://freeink-sdk/libs/hardware/InputManager
  BatteryMonitor=symlink://freeink-sdk/libs/hardware/BatteryMonitor
  SDCardManager=symlink://freeink-sdk/libs/hardware/SDCardManager

[env:xteink]
extends = base
board = esp32-c3-devkitm-1
build_flags =
  ${base.build_flags}
  -DFREEINK_DEVICE_X3=1
  -DFREEINK_DEVICE_X4=1
```

- [ ] **Step 7: Embed the UI font and write main.cpp**

Generate the embeddable font header:

```bash
python3 - <<'EOF'
data = open("assets/built/spacegrotesk_16.rfnt", "rb").read()
with open("shell/src/font_spacegrotesk_16.h", "w") as f:
    f.write("// Generated from assets/built/spacegrotesk_16.rfnt - do not edit.\n")
    f.write("#pragma once\n#include <cstdint>\n#include <cstddef>\n")
    f.write(f"inline constexpr size_t kUiFontSize = {len(data)};\n")
    f.write("inline const uint8_t kUiFont[] = {")
    f.write(",".join(str(b) for b in data))
    f.write("};\n")
print("wrote shell/src/font_spacegrotesk_16.h")
EOF
```

`shell/src/main.cpp`:

```cpp
#include <Arduino.h>
#include <EInkDisplay.h>

#include "font_spacegrotesk_16.h"
#include "reader/framebuffer.h"
#include "reader/rotate.h"
#include "reader/theme_quiet.h"
#include "reader/viewmodel.h"

// Xteink display SPI pins (same wiring as CrossPoint's HalGPIO).
constexpr int8_t EPD_SCLK = 8, EPD_MOSI = 10, EPD_CS = 21, EPD_DC = 4, EPD_RST = 5, EPD_BUSY = 6;

EInkDisplay display(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);

void setup() {
  Serial.begin(115200);
  // X4 geometry by default; X3 runtime detection lands in Phase 2
  // (call display.setDisplayX3() before begin() for an X3 unit).
  display.begin();

  reader::QuietTheme theme;
  if (!theme.loadFonts(kUiFont, kUiFontSize)) {
    Serial.println("font load failed");
    return;
  }

  reader::HomeViewModel vm;
  vm.title = "Middlemarch";
  vm.author = "George Eliot";
  vm.chapterLabel = "CH. 01 \xE2\x80\x94 MISS BROOKE";
  vm.percent = 6;
  vm.currentPage = 53;
  vm.pageCount = 890;
  vm.batteryPercent = 87;
  vm.menu = {{"LIBRARY", "12"}, {"ARTICLES", "3 UNREAD"}, {"SETTINGS", ""}};
  vm.focusedMenuIndex = -1;
  vm.hints = {"READ", "SELECT", "UP", "DOWN"};

  const int panelW = display.getDisplayWidth();    // 800 on X4
  const int panelH = display.getDisplayHeight();   // 480 on X4
  reader::Framebuffer portrait(panelH, panelW);    // 480 x 800
  reader::Framebuffer landscape(panelW, panelH);   // 800 x 480
  theme.renderHome(portrait, vm);
  reader::rotate90CW(portrait, landscape);

  display.setFramebuffer(landscape.data());
  display.displayBuffer(EInkDisplay::FULL_REFRESH);
  Serial.println("home rendered");
}

void loop() { delay(1000); }
```

- [ ] **Step 8: Build the firmware**

```bash
git submodule update --init
pio run -e xteink
```

Expected: `SUCCESS` with RAM/Flash usage printed. If `EInkDisplay.h` is not
found, check that the symlink lib_deps paths match the submodule layout
(`ls freeink-sdk/libs/display/FreeInkDisplay`).

- [ ] **Step 9: Flash and verify on hardware (manual)**

```bash
pio run -e xteink -t upload
```

Expected on the X4: full refresh, then the Home screen (title, progress bar,
CONTINUE block, menu rows, four hints). If the image is rotated the wrong
way, swap `rotate90CW` for `rotate90CCW` in `main.cpp` and re-flash. If text
is inverted (white page/black background), the panel polarity differs from
the assumed convention — call `display.setInverted(true)` after `begin()`
and file the finding for Phase 2.

- [ ] **Step 10: Commit**

```bash
git add platformio.ini core/library.json shell && git commit -m "feat(shell): PlatformIO scaffold; QuietTheme Home renders on X4 hardware"
```

---

### Task 12: Phase wrap-up

- [ ] **Step 1: Full clean verification**

```bash
rm -rf build && make test && make sim && pio run -e xteink
```

Expected: all tests pass, sim PNG renders, firmware builds.

- [ ] **Step 2: Update README status and commit**

Append to `README.md`:

```markdown
## Status
Phase 1 (foundation) complete: core builds and tests on macOS, simulator
renders Home, firmware shows Home on the X4. Next: Phase 2 (UI system) —
see docs/superpowers/plans/2026-08-20-v1-roadmap.md.
```

```bash
git add README.md && git commit -m "docs: phase 1 complete"
```

---

## Self-review notes (already applied)

- **Glyph record size:** 18 bytes (`<IhhhhhI`), bitmapOffset at byte 14;
  Task 6 writer and Task 7 loader agree.
- **Spec coverage:** this phase intentionally covers only spec §3.2 (fonts,
  partial), §3.3 (theme skeleton), §3.4 (simulator), and first-boot §3.1
  usage. Everything else is scheduled in the roadmap phases.
- **X3:** compiled in (`FREEINK_DEVICE_X3=1`) but runtime panel selection is
  deferred to Phase 2 — stated in `main.cpp` comments, not silent.
