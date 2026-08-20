# Phase 2A-2: Grayscale Text Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make chrome text legible on real e-ink by rendering anti-aliased glyphs through the panel's 4-level grayscale path, instead of hard-thresholding to 1 bit.

**Architecture:** No 2 bpp framebuffer. Glyphs carry 2-bit coverage, and the framebuffer stays 1 bit; the screen is drawn **three times** — once as a black-and-white base frame, then once per bit-plane of the grey level — and the SDK combines the planes into a 4-level paint. This is exactly how CrossPoint drives the same hardware, and it means the memory footprint barely moves.

**Tech Stack:** C++20, CMake + doctest, stb PNG goldens, Python 3 + freetype-py, PlatformIO / freeink-sdk on ESP32-C3.

**Repo:** `/Users/lucasgoudin/dev/encre`, on `main`. Baseline: `make test` passes with 56 doctest cases and 2 ctest tests; `make sim`, `make firmware`, `make compare` all work; Home matches its design board in the simulator on both panel geometries.

---

## Why this phase exists

Phase 2A rendered Home faithfully in the simulator, and on X3 hardware **the chrome was illegible**. Size is not the cause: CrossPoint ships UI chrome at 10–12 px on this exact hardware and it reads fine. The difference is that CrossPoint converts glyphs with `FT_LOAD_RENDER --2bit` — anti-aliased, 4 levels — while Encre uses `FT_LOAD_TARGET_MONO`. At 12 px a stem is 1–1.5 px, so anti-aliasing is what lets the eye reconstruct a letterform; thresholding drops it or leaves a jagged single-pixel stroke.

**Do not change the type ramp in this phase.** With anti-aliasing, 12/13/14 sits inside CrossPoint's proven range. Re-test on device after this lands, and only if it is still too small change the design boards first, per the design-first rule.

## Design decisions this plan locks in

**1. The framebuffer stays 1 bit.** The obvious approach — a 2 bpp buffer — does not fit: at 528×792 that is 104,544 bytes, and a portrait plus a landscape buffer plus the controller's two planes is ~313 KB against 327 KB total with ~74 KB already static. CrossPoint's solution is to render the content once per bit-plane into the same 1-bit buffer. Memory cost becomes one extra full-frame stash rather than a doubling.

**2. Three passes, one render function.** The theme is invoked three times with a `Plane` argument:

| Pass | What it emits | Consumed by |
|---|---|---|
| `Plane::Bw` | ink where coverage ≥ 2 | the base frame, via `displayGrayscaleBase` |
| `Plane::Lsb` | ink where bit 0 of coverage is set | `copyGrayscaleLsbBuffers` |
| `Plane::Msb` | ink where bit 1 of coverage is set | `copyGrayscaleMsbBuffers` |

Structural drawing (rules, fills, dither, icons) is opaque: coverage is 0 or 3, so it appears identically in all three passes. Only glyph edges differ between planes. That property is what makes the approach safe — a bug in plane selection shows up as fringing on text, never as missing furniture.

**3. Plane polarity is an empirical unknown.** Whether a set bit in a plane buffer means "more ink" or "less ink", and which of LSB/MSB is the high bit of the level, are not documented in the SDK header. Task 7 resolves it on hardware with a grey-ramp test pattern before the theme is trusted. The code is written so flipping either convention is a one-line change, and the plan says exactly which line.

**4. `.rfnt` gains a bit-depth field (format v2).** Glyph rows become `ceil(w * bpp / 8)` bytes. The loader accepts v1 (1 bpp, implied) and v2 (explicit `bpp` of 1 or 2), so the Literata body font can stay 1 bpp until Phase 3 revisits body text, while chrome moves to 2 bpp now.

**5. Goldens become 4-level PNGs.** The simulator composes the three passes back into a level per pixel and writes a greyscale PNG, so anti-aliased output is verifiable on the desktop exactly as 1-bit output was. This is the main reason this phase is testable at all without a panel.

---

### Task 1: `.rfnt` v2 — a bit-depth field

**Files:**
- Modify: `core/include/reader/font.h`, `core/src/font.cpp`
- Test: `test/unit/test_font_load.cpp` (append), `test/unit/rfnt_builder.h` (extend)

- [ ] **Step 1: Extend the synthetic-font builder to emit v2**

`test/unit/rfnt_builder.h` already builds `.rfnt` byte buffers for tests. Add a bit-depth parameter. The existing header is `magic "RFNT" | u16 version | u16 glyphCount | i16 ascent | i16 descent | i16 lineGap | u16 kernCount` (16 bytes). For v2, the `lineGap` field is followed by the same `kernCount`, and **bpp is carried in the high byte of `version`**: version `1` means v1/1 bpp, version `0x0102` means v2 with bpp 2 — no struct change, so v1 files stay byte-valid. Write a helper `buildRfnt(..., int bpp = 1)` that sets the version word accordingly and sizes glyph rows as `ceil(w * bpp / 8)`.

- [ ] **Step 2: Write the failing tests**

Append to `test/unit/test_font_load.cpp`:

```cpp
TEST_CASE("Font::load accepts a v2 2bpp font and reports its depth") {
  // One glyph, 4px wide, coverage 0..3 across its single row.
  const uint8_t rows[] = {0b00011011};  // levels 0,1,2,3
  auto blob = rfnt::build({{U'A', /*advance=*/5, /*w=*/4, /*h=*/1, /*xOff=*/0, /*yOff=*/1, rows,
                            sizeof rows}},
                          /*bpp=*/2);
  reader::Font f;
  REQUIRE(f.load(blob.data(), blob.size()));
  CHECK(f.bpp() == 2);
  const reader::Glyph* g = f.glyph(U'A');
  REQUIRE(g != nullptr);
  CHECK(g->rowBytes() == 1);           // 4 px * 2 bpp = 1 byte
  CHECK(f.coverage(*g, 0, 0) == 0);
  CHECK(f.coverage(*g, 1, 0) == 1);
  CHECK(f.coverage(*g, 2, 0) == 2);
  CHECK(f.coverage(*g, 3, 0) == 3);
}

TEST_CASE("a v1 font still loads and reports 1bpp with binary coverage") {
  const uint8_t rows[] = {0b10100000};  // px0 ink, px1 none, px2 ink
  auto blob = rfnt::build({{U'A', 5, 3, 1, 0, 1, rows, sizeof rows}}, /*bpp=*/1);
  reader::Font f;
  REQUIRE(f.load(blob.data(), blob.size()));
  CHECK(f.bpp() == 1);
  const reader::Glyph* g = f.glyph(U'A');
  REQUIRE(g != nullptr);
  CHECK(f.coverage(*g, 0, 0) == 3);    // a set 1bpp bit is full coverage
  CHECK(f.coverage(*g, 1, 0) == 0);
  CHECK(f.coverage(*g, 2, 0) == 3);
}

TEST_CASE("an unknown bit depth is rejected") {
  const uint8_t rows[] = {0xFF};
  auto blob = rfnt::build({{U'A', 5, 2, 1, 0, 1, rows, sizeof rows}}, /*bpp=*/1);
  blob[4] = 0x03;  // version low byte 3: not a version we know
  reader::Font f;
  CHECK_FALSE(f.load(blob.data(), blob.size()));
}
```

- [ ] **Step 3: Run to verify it fails**

```bash
cd /Users/lucasgoudin/dev/encre && cmake -S . -B build && make test
```

Expected: FAIL — `Font` has no `bpp()` or `coverage()`.

- [ ] **Step 4: Implement**

In `core/include/reader/font.h`, add to the public section of `Font`:

```cpp
  // 1 or 2. A 1bpp font reports coverage 0 or 3, so callers never branch on it.
  int bpp() const { return bpp_; }
  // Coverage of one glyph pixel, 0 (none) to 3 (full).
  uint8_t coverage(const Glyph& g, int col, int row) const;
```

and to the private section: `int bpp_ = 1;`

Change `Glyph::rowBytes()` to take the depth into account. Since `Glyph` does not know its font, store the stride on the glyph instead — replace the inline `rowBytes()` with a stored field:

```cpp
struct Glyph {
  int16_t advance, bitmapW, bitmapH, xOff, yOff;
  const uint8_t* bitmap;
  int16_t stride;  // bytes per row, = ceil(bitmapW * bpp / 8)
  int rowBytes() const { return stride; }
};
```

In `core/src/font.cpp`, inside `load()`, after reading the version word:

```cpp
  const uint16_t versionWord = rd<uint16_t>(d + 4);
  const uint8_t version = versionWord & 0xFF;
  const uint8_t depth = (versionWord >> 8) ? (versionWord >> 8) : 1;
  if (version != 1 || (depth != 1 && depth != 2)) return false;
  bpp_ = depth;
```

and where each glyph is populated, set the stride from the depth and validate the extent against it:

```cpp
    gl.stride = static_cast<int16_t>((gl.bitmapW * bpp_ + 7) / 8);
```

(the existing bounds check already multiplies `rowBytes * bitmapH`, so it now validates the correct size). Then add:

```cpp
uint8_t Font::coverage(const Glyph& g, int col, int row) const {
  const uint8_t* r = g.bitmap + row * g.stride;
  if (bpp_ == 1) return ((r[col / 8] >> (7 - col % 8)) & 1) ? 3 : 0;
  // 2bpp, MSB-first: two bits per pixel, four pixels per byte.
  const int shift = 6 - 2 * (col % 4);
  return static_cast<uint8_t>((r[col / 4] >> shift) & 0x3);
}
```

- [ ] **Step 5: Run tests** — `make test`. Expected PASS, with both goldens unchanged (every current asset is v1, and a v1 set bit still reports full coverage).

- [ ] **Step 6: Commit**

```bash
git add core test/unit/rfnt_builder.h test/unit/test_font_load.cpp
git commit -m "feat(core): .rfnt v2 with a bit-depth field and per-pixel coverage"
```

---

### Task 2: `fontc.py` emits 2 bpp

**Files:**
- Modify: `tools/fontc.py`, `Makefile`
- Generated (committed): the six chrome assets, regenerated at 2 bpp

- [ ] **Step 1: Add a `--bpp` option**

In `tools/fontc.py`, add `ap.add_argument("--bpp", type=int, default=1, choices=(1, 2))`. When `bpp == 2`, load glyphs **without** `FT_LOAD_TARGET_MONO` so FreeType returns an 8-bit coverage bitmap, then quantise each pixel to 2 bits and pack four pixels per byte, MSB-first:

```python
    if args.bpp == 2:
        load_flags = freetype.FT_LOAD_RENDER          # 8-bit anti-aliased
        if args.autohint:
            load_flags |= freetype.FT_LOAD_FORCE_AUTOHINT
    else:
        load_flags = freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO
        if args.autohint:
            load_flags |= freetype.FT_LOAD_FORCE_AUTOHINT
```

and in the glyph loop, replace the row-packing with a depth-aware version:

```python
        if args.bpp == 2:
            row_bytes = (bmp.width * 2 + 7) // 8
            packed = bytearray(row_bytes * bmp.rows)
            for row in range(bmp.rows):
                for col in range(bmp.width):
                    # 8-bit coverage -> 2-bit level, rounding to nearest.
                    level = (bmp.buffer[row * bmp.pitch + col] * 3 + 127) // 255
                    if level:
                        shift = 6 - 2 * (col % 4)
                        packed[row * row_bytes + col // 4] |= level << shift
        else:
            row_bytes = (bmp.width + 7) // 8
            packed = bytearray(row_bytes * bmp.rows)
            for row in range(bmp.rows):
                src = bmp.buffer[row * bmp.pitch : row * bmp.pitch + row_bytes]
                packed[row * row_bytes : (row + 1) * row_bytes] = bytes(src)
```

Write the version word as `1 | (args.bpp << 8)` in the header pack, and include the depth in the summary line.

- [ ] **Step 2: Verify the round trip before regenerating anything**

```bash
cd /Users/lucasgoudin/dev/encre
python3 tools/fontc.py assets/fonts/SpaceGrotesk.ttf --size 13 --weight 500 --autohint --bpp 2 --out /tmp/probe_13_2bpp.rfnt
python3 - <<'EOF'
import struct
d = open("/tmp/probe_13_2bpp.rfnt", "rb").read()
magic, ver, count, asc, desc, gap, kerns = struct.unpack("<4sHHhhhH", d[:16])
assert magic == b"RFNT" and (ver & 0xFF) == 1 and (ver >> 8) == 2, (magic, hex(ver))
print("v2 2bpp ok:", count, "glyphs, header version", hex(ver))
# A 2bpp glyph must contain intermediate levels, or anti-aliasing was lost.
blob = d[16 + count * 18 + kerns * 12:]
levels = set()
for byte in blob[:4000]:
    for sh in (6, 4, 2, 0):
        levels.add((byte >> sh) & 3)
print("levels present in blob:", sorted(levels))
assert {1, 2} & levels, "no intermediate coverage - anti-aliasing did not survive"
EOF
```

Expected: `v2 2bpp ok: 200 glyphs` and a level set containing 1 and/or 2. If only 0 and 3 appear, the mono flag is still set and the whole phase is pointless — stop and fix.

- [ ] **Step 3: Regenerate the chrome ramp at 2 bpp**

Add `--bpp 2` to all six Space Grotesk lines in the `Makefile`'s `fonts` target (Meta 12/500, Label 13/500, Value 14/700, Body 17/500, Title 24/700, Display 44/700). **Leave Literata at 1 bpp** — body text is Phase 3's concern and changing it now would move the `text_sample` golden. Then:

```bash
make fonts
```

Expected: the six chrome assets grow (2 bpp roughly doubles the blob) and report `bpp=2`; `assets/built/literata_18.rfnt` unchanged — confirm with `git status assets/built/literata_18.rfnt`.

- [ ] **Step 4: Run tests**

```bash
cmake -S . -B build && make test
```

Expected: the two Home goldens FAIL (chrome glyphs now carry intermediate coverage, and `drawText` still thresholds, so edge pixels change), `text_sample` PASSES. **Do not bless anything yet** — Task 3 changes how coverage is drawn, and Task 5 re-blesses once. Note the failure and continue.

- [ ] **Step 5: Commit**

```bash
git add tools/fontc.py Makefile assets/built shell/src
git commit -m "feat(fonts): emit 2bpp anti-aliased chrome glyphs

Home goldens are knowingly failing until the plane-aware renderer lands."
```

---

### Task 3: Plane-aware text drawing

**Files:**
- Modify: `core/include/reader/text.h`, `core/src/text.cpp`
- Test: `test/unit/test_text.cpp` (append)

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("each plane emits its own bit of a glyph's coverage") {
  // Synthetic 2bpp glyph: one row, coverage 0,1,2,3.
  const uint8_t rows[] = {0b00011011};
  auto blob = rfnt::build({{U'A', 5, 4, 1, 0, 1, rows, sizeof rows}}, /*bpp=*/2);
  reader::Font f;
  REQUIRE(f.load(blob.data(), blob.size()));

  auto inkAt = [&](reader::Plane plane, int px) {
    reader::Framebuffer fb(16, 4);
    reader::drawText(fb, f, 0, 1, "A", reader::Ink::Black, 0, plane);
    return !fb.getPixel(px, 0);   // true when this pixel got ink
  };

  // Bw: ink where coverage >= 2, i.e. pixels 2 and 3.
  CHECK_FALSE(inkAt(reader::Plane::Bw, 0));
  CHECK_FALSE(inkAt(reader::Plane::Bw, 1));
  CHECK(inkAt(reader::Plane::Bw, 2));
  CHECK(inkAt(reader::Plane::Bw, 3));

  // Lsb: bit 0 set, i.e. coverage 1 and 3.
  CHECK_FALSE(inkAt(reader::Plane::Lsb, 0));
  CHECK(inkAt(reader::Plane::Lsb, 1));
  CHECK_FALSE(inkAt(reader::Plane::Lsb, 2));
  CHECK(inkAt(reader::Plane::Lsb, 3));

  // Msb: bit 1 set, i.e. coverage 2 and 3.
  CHECK_FALSE(inkAt(reader::Plane::Msb, 0));
  CHECK_FALSE(inkAt(reader::Plane::Msb, 1));
  CHECK(inkAt(reader::Plane::Msb, 2));
  CHECK(inkAt(reader::Plane::Msb, 3));
}

TEST_CASE("a 1bpp font is identical in every plane") {
  auto bytes = slurpFont("literata_18.rfnt");   // still 1bpp
  reader::Font f;
  REQUIRE(f.load(bytes.data(), bytes.size()));
  auto render = [&](reader::Plane plane) {
    reader::Framebuffer fb(200, 40);
    reader::drawText(fb, f, 4, 30, "Aa", reader::Ink::Black, 0, plane);
    int n = 0;
    for (int y = 0; y < 40; ++y)
      for (int x = 0; x < 200; ++x)
        if (!fb.getPixel(x, y)) ++n;
    return n;
  };
  const int bw = render(reader::Plane::Bw);
  CHECK(bw > 0);
  CHECK(render(reader::Plane::Lsb) == bw);
  CHECK(render(reader::Plane::Msb) == bw);
}
```

- [ ] **Step 2: Run to verify it fails** — expected FAIL, `reader::Plane` does not exist.

- [ ] **Step 3: Implement**

In `core/include/reader/text.h`, add above the `drawText` declaration:

```cpp
// Which bit-plane of a 2-bit grey level this pass emits. The screen is drawn
// three times: Bw produces the base frame the panel paints first, then Lsb and
// Msb produce the two planes the controller combines into 4 levels. Opaque
// drawing (rules, fills, icons) has coverage 0 or 3 and so is identical in all
// three; only glyph edges differ.
enum class Plane { Bw, Lsb, Msb };
```

and extend the signature:

```cpp
int drawText(Framebuffer& fb, const Font& font, int x, int baselineY, std::string_view utf8,
             Ink ink = Ink::Black, int tracking = 0, Plane plane = Plane::Bw);
```

In `core/src/text.cpp`, replace the per-pixel emit inside the glyph loop with a coverage-driven one:

```cpp
    for (int row = 0; row < g->bitmapH; ++row)
      for (int col = 0; col < g->bitmapW; ++col) {
        const uint8_t cov = font.coverage(*g, col, row);
        bool emit = false;
        switch (plane) {
          case Plane::Bw:  emit = cov >= 2;        break;
          case Plane::Lsb: emit = (cov & 1) != 0;  break;
          case Plane::Msb: emit = (cov & 2) != 0;  break;
        }
        if (emit) fb.setPixel(pen + g->xOff + col, baselineY - g->yOff + row, white);
      }
```

**Polarity note for Task 7:** if the panel comes out inverted or with wrong levels, the two candidate fixes are (a) swap `Plane::Lsb` and `Plane::Msb` at the call site in the shell, and (b) invert `emit` for the two plane cases here. Change one at a time.

- [ ] **Step 4: Run tests** — `make test`. The new cases PASS; the two Home goldens still fail (expected until Task 5).

- [ ] **Step 5: Commit**

```bash
git add core/include/reader/text.h core/src/text.cpp test/unit/test_text.cpp
git commit -m "feat(core): plane-aware glyph emission for 4-level grey"
```

---

### Task 4: The theme renders a plane

**Files:**
- Modify: `core/include/reader/theme.h`, `core/include/reader/theme_quiet.h`, `core/src/theme_quiet.cpp`, `core/include/reader/components.h`, `core/src/components.cpp`
- Test: `test/unit/test_components.cpp` (append)

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("structural drawing is identical in every plane") {
  Fixture f;
  auto render = [&](reader::Plane plane) {
    reader::Framebuffer fb(480, 200);
    reader::drawRow(fb, f.fonts, 0, "LIBRARY", "12", /*focused=*/false, nullptr, plane);
    return fb;
  };
  const reader::Framebuffer bw = render(reader::Plane::Bw);
  const reader::Framebuffer lsb = render(reader::Plane::Lsb);

  // The row's hairline is opaque furniture: it must appear in both planes at
  // exactly the same pixels. Only glyph edges may differ.
  for (int x = 0; x < 480; ++x) CHECK(bw.getPixel(x, 0) == lsb.getPixel(x, 0));
}
```

- [ ] **Step 2: Run to verify it fails** — expected FAIL, `drawRow` takes no plane.

- [ ] **Step 3: Implement**

Add `Plane plane = Plane::Bw` as the last parameter of `drawHeaderBand`, `drawRow` and `drawHintBar` in `components.h`/`.cpp`, and pass it through to every `drawText` call inside them. `drawIcon`, `fillRect` and `ditherRect` need no change — they are opaque by construction.

Change the theme interface in `core/include/reader/theme.h`:

```cpp
  virtual void renderHome(Framebuffer& fb, const FontSet& fonts, const HomeViewModel& vm,
                          Plane plane = Plane::Bw) = 0;
```

(add `#include "reader/text.h"` for `Plane`), mirror it in `theme_quiet.h`, and in `theme_quiet.cpp` thread `plane` into every `drawText` and component call. `drawCoverPlaceholder`'s dither and borders stay as they are; only its strip title takes the plane.

- [ ] **Step 4: Run tests** — `make test`. New case PASSES; Home goldens still failing.

- [ ] **Step 5: Commit**

```bash
git add core test/unit/test_components.cpp
git commit -m "feat(theme): thread the bit-plane through components and Home"
```

---

### Task 5: Simulator composes the planes; re-bless goldens

This is where anti-aliased output becomes verifiable on the desktop.

**Files:**
- Modify: `sim/main.cpp`, `core/include/reader/png.h`, `core/src/png.cpp`
- Modify: `test/unit/golden.h`, `test/unit/test_theme_home_golden.cpp`
- Goldens: `test/golden/home_quiet.png`, `test/golden/home_quiet_x3.png` (re-bless)

- [ ] **Step 1: Add a 4-level PNG writer**

In `core/include/reader/png.h`:

```cpp
// Compose three single-plane framebuffers into one 4-level greyscale image and
// write it. Level per pixel is (msb << 1) | lsb, mapped 0..3 -> white..black,
// with `bw` used only as a sanity check that the planes agree on furniture.
bool writeGrayPng(const Framebuffer& lsb, const Framebuffer& msb, const char* path);
```

In `core/src/png.cpp`, inside the `READER_DESKTOP` guard:

```cpp
bool writeGrayPng(const Framebuffer& lsb, const Framebuffer& msb, const char* path) {
  if (lsb.width() != msb.width() || lsb.height() != msb.height()) return false;
  std::vector<unsigned char> px(static_cast<size_t>(lsb.width()) * lsb.height());
  // 0x00 black, 0x55 / 0xAA the two mid levels, 0xFF white.
  static const unsigned char kRamp[4] = {0xFF, 0xAA, 0x55, 0x00};
  for (int y = 0; y < lsb.height(); ++y)
    for (int x = 0; x < lsb.width(); ++x) {
      const int l = lsb.getPixel(x, y) ? 0 : 1;   // ink == level bit set
      const int m = msb.getPixel(x, y) ? 0 : 1;
      px[static_cast<size_t>(y) * lsb.width() + x] = kRamp[(m << 1) | l];
    }
  return stbi_write_png(path, lsb.width(), lsb.height(), 1, px.data(), lsb.width()) != 0;
}
```

- [ ] **Step 2: Render three passes in the simulator**

In `sim/main.cpp`, replace the single render with three, and write the composed image:

```cpp
  reader::Framebuffer bw(w, h), lsb(w, h), msb(w, h);
  reader::QuietTheme theme;
  theme.renderHome(bw, fonts, vm, reader::Plane::Bw);
  theme.renderHome(lsb, fonts, vm, reader::Plane::Lsb);
  theme.renderHome(msb, fonts, vm, reader::Plane::Msb);
  if (!reader::writeGrayPng(lsb, msb, argv[2])) return 1;
```

- [ ] **Step 3: Make the golden helper compare 4-level images**

In `test/unit/golden.h`, add a sibling of `checkGolden` that takes the two plane buffers, composes them with `writeGrayPng` into `build/<name>_candidate.png`, and compares against the golden by reading both PNGs — reuse the existing `diffPng` path by comparing the written candidate to the golden file rather than a framebuffer. Keep the existing 1-bit `checkGolden` for `text_sample`.

Update `test/unit/test_theme_home_golden.cpp` to render all three passes per geometry and call the new helper.

- [ ] **Step 4: Run, inspect, bless**

```bash
cmake -S . -B build && make test
```

Expected: both Home goldens fail with a mismatch, writing candidates. **Open both candidates and check specifically for anti-aliasing:** glyph edges must show intermediate grey, not just black and white; the letterspaced 12–13 px labels should look smoother and heavier than before; furniture (rules, the CONTINUE fill, the cover dither) must be pure black/white with no grey fringe — grey on a rule means a plane bug, not anti-aliasing.

Only then:

```bash
cp build/home_quiet_candidate.png test/golden/home_quiet.png
cp build/home_quiet_x3_candidate.png test/golden/home_quiet_x3.png
make test && make sim
```

Expected: PASS, and `build/home.png` is now a 4-level image.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat(sim): compose the three planes into 4-level output; re-bless goldens"
```

---

### Task 6: The firmware grayscale paint

**Files:**
- Modify: `shell/src/main.cpp`

- [ ] **Step 1: Implement the three-pass paint**

Replace the single render-and-display block in `setup()` with the SDK's grayscale sequence. Buffers: one portrait, one landscape, reused across passes; the BW landscape frame is kept for the cleanup call, which is the one extra full-frame cost of this phase.

```cpp
  const int panelW = display.getDisplayWidth();
  const int panelH = display.getDisplayHeight();
  reader::Framebuffer portrait(panelH, panelW);
  reader::Framebuffer landscape(panelW, panelH);
  reader::Framebuffer bwLandscape(panelW, panelH);  // retained for cleanup

  auto paint = [&](reader::Plane plane, reader::Framebuffer& out) {
    portrait.clear(true);
    theme.renderHome(portrait, fonts, vm, plane);
    reader::rotate90CCW(portrait, out);
  };

  // 1. Base frame: the black-and-white image the panel paints first.
  paint(reader::Plane::Bw, bwLandscape);
  display.setFramebuffer(bwLandscape.data());
  display.displayGrayscaleBase(EInkDisplay::HALF_REFRESH);
  mark("gray-base-displayed");

  // 2. X3 needs a settle pass between the base frame and the planes.
  display.preconditionGrayscale();
  mark("gray-preconditioned");

  // 3. The two bit-planes.
  paint(reader::Plane::Lsb, landscape);
  display.copyGrayscaleLsbBuffers(landscape.data());
  paint(reader::Plane::Msb, landscape);
  display.copyGrayscaleMsbBuffers(landscape.data());
  mark("gray-planes-written");

  // 4. Paint the combined 4-level image, then put the controller back on a
  //    valid B/W baseline so the next ordinary refresh is differentially sane.
  display.displayGrayBuffer();
  mark("gray-displayed");
  display.cleanupGrayscaleBuffers(bwLandscape.data());
  mark("refresh-complete");
```

Log the free heap right after the three buffers are constructed, so the memory claim is measured rather than asserted:

```cpp
  Serial.printf("[info] three frames live: free heap %u, largest block %u\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  Serial.flush();
```

- [ ] **Step 2: Build**

```bash
make firmware
```

Expected: SUCCESS. Record the Flash figure; three framebuffers are heap, so static RAM should barely move.

- [ ] **Step 3: Commit**

```bash
git add shell/src/main.cpp
git commit -m "feat(shell): paint Home through the panel's 4-level grayscale path"
```

---

### Task 7: Hardware bring-up — resolve plane polarity

The two conventions this plan could not derive from the SDK header get settled here, on the panel.

**Files:** possibly one line in `shell/src/main.cpp` or `core/src/text.cpp` (see Task 3's polarity note).

- [ ] **Step 1: Flash and capture the log**

```bash
cd /Users/lucasgoudin/dev/encre
~/.platformio/penv/bin/pio run -e xteink -t upload --upload-port /dev/cu.usbmodem1101
~/.platformio/penv/bin/python tools/serial-log.py --seconds 20 --no-reset
```

Expected stages: `gray-base-displayed`, `gray-preconditioned`, `gray-planes-written`, `gray-displayed`, `refresh-complete`, then `[alive]` heartbeats. A hang at `gray-planes-written` means `displayGrayBuffer` is waiting on a BUSY edge that never comes — report it with the stage rather than guessing.

- [ ] **Step 2: Judge the panel against four possibilities**

| What you see | Meaning | Fix |
|---|---|---|
| Text smoother and clearly legible | Correct | none |
| Text legible but *lighter* than the base frame | plane levels inverted | invert `emit` for `Lsb`/`Msb` in `core/src/text.cpp` |
| Text has a hard halo or doubled edge | LSB/MSB swapped | swap the two `copyGrayscale*` calls in the shell |
| Whole screen inverted | base-frame polarity | `display.setInverted(true)` after `begin()` |

Change **one** thing, reflash, look again. Record each attempt and its result.

- [ ] **Step 3: Answer the question this phase exists for**

Is the chrome now legible at 12–13 px on the panel? Write the answer down in the commit message, plainly, including whether the letterspaced small caps hold together and whether the mid-greys look like tone or like noise.

If it is legible: the type ramp needs no change, and the design boards are untouched — which was the hoped-for outcome.

If it is still too small: **change the design boards first** (`design/*.dc.html`), per the design-first rule, then follow with the firmware ramp and re-bless. Do not adjust only the code.

- [ ] **Step 4: Commit the finding**

```bash
git commit --allow-empty -m "verified: grayscale chrome on X3 hardware

<what you saw, which polarity was correct, and whether 12-13px is now legible>"
```

---

### Task 8: Phase wrap-up

- [ ] **Step 1: Full clean verification**

```bash
cd /Users/lucasgoudin/dev/encre
rm -rf build && make test && make sim && make firmware && make compare
```

Expected: all tests pass, the simulator writes a 4-level PNG, the firmware builds, and the comparison sheet still reports Home implemented at both geometries.

- [ ] **Step 2: Update the roadmap**

Mark 2A-2 done under the Phase 2 heading, and record in the bring-up findings section: the resolved plane polarity, the measured free heap with three frames live, and the answer on 12–13 px legibility.

- [ ] **Step 3: Commit**

```bash
git add docs && git commit -m "docs: Phase 2A-2 complete"
```

---

## Self-review notes (already applied)

- **Coverage:** 2 bpp glyph storage (Tasks 1–2), plane-aware emission (Task 3), plane threading through theme and components (Task 4), desktop verification via composed 4-level goldens (Task 5), the firmware grayscale sequence (Task 6), polarity resolution and the legibility answer on hardware (Task 7).
- **The memory concern that shaped this plan is resolved by the architecture, not by streaming.** Because the framebuffer stays 1 bit, the cost is three 1-bit frames (~157 KB at 528×792) instead of the ~313 KB a 2 bpp double-buffer would have needed. `writeGrayscalePlaneStrip` is therefore not used, and the roadmap's earlier note about strip streaming is superseded — Task 8 should say so.
- **Deliberately unchanged:** the type ramp (Task 7 decides whether it needs to move), Literata and body text (Phase 3), the design boards (nothing to change unless Task 7 says so), and every other screen.
- **Type consistency:** `Plane{Bw,Lsb,Msb}` lives in `reader/text.h` and is the last parameter of `drawText`, `drawHeaderBand`, `drawRow`, `drawHintBar` and `Theme::renderHome`, defaulting to `Plane::Bw` so existing calls compile. `Font::bpp()` / `Font::coverage(glyph, col, row)` and `Glyph::stride` are introduced in Task 1 and used in Tasks 3 and 5.
- **Known risk:** Tasks 2–4 knowingly leave the Home goldens failing across three commits, re-blessed once in Task 5. That is deliberate — blessing twice would mean approving an intermediate rendering nobody wants — but it does mean `main` must not be merged from mid-phase.
