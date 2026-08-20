# Phase 2A: Design Fidelity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Home screen render at the design's actual fidelity — real type ramp, cover art, icons, dithering — on both panel geometries, so the remaining screens have a component vocabulary to be built from.

**Architecture:** All of it lands in the portable core (`core/`, namespace `reader::`), so every change is desktop-testable with unit tests and golden PNGs before it ever reaches hardware. Text gains three capabilities it lacks (ink colour, letterspacing, a visible missing-glyph box); the theme gains a role-based type ramp supplied by the caller (`FontSet`) so device knowledge stays in the shell; layout stops assuming a 480-wide canvas.

**Tech Stack:** C++20, CMake + doctest, stb PNG goldens, Python 3 + freetype-py for the font ramp, PlatformIO for the ESP32-C3 firmware.

**Repo:** `/Users/lucasgoudin/dev/encre`, on `main`. Baseline: `make test` passes with 38 doctest cases and 2 ctest tests, `make sim` renders `build/home.png`, `make firmware` builds, and `make compare` reports 1 of 7 V1 screens implemented.

---

## Design decisions this plan locks in

Read these before starting. They are the reasoning behind the tasks, and the first three overturn assumptions baked into Phase 1.

**1. Type sizes are physical, layout is flexible.** The X4 is 800×480 at 4.26" (219 PPI) and the X3's 792×528 sits in the same ~220–235 PPI band; both SDK board profiles leave `uiScale` at 1.0. So the two devices want *identical* type sizes — scaling type with canvas width would make text physically bigger on the X3 for no reason. What must adapt is the *layout*: no constant may assume a 480-wide canvas. Margins stay fixed in pixels; everything else derives from `fb.width()` / `fb.height()`.

**2. The theme does not choose its fonts.** `core/` cannot include `BoardConfig.h` (an Arduino library), so the caller — shell or simulator — builds a `FontSet` and hands it in. That keeps device and asset knowledge out of the portable core and is also what makes `uiScale` support a caller concern rather than a core one.

**3. Inverted text needs no scratch framebuffer.** Phase 1's `textInverted` allocated a full 48 KB framebuffer per call and scanned every pixel. Giving `drawText` an ink colour deletes the whole mechanism: white-on-black is just `drawText(..., Ink::White)` over an already-filled black rect.

**4. Letterspacing is a rendering parameter, not a font property.** The design uses tracking heavily (0.1em–0.26em on every label). One `tracking` argument on `drawText`/`measure` covers it; baking it into font assets would need a separate asset per tracking value.

**5. The type ramp is a fixed set of five roles.** Pre-rendered bitmaps mean every distinct pixel size is its own asset, so the ramp is enumerated rather than arbitrary:

| Role | Size | Weight | Used for |
|---|---|---|---|
| `Meta` | 12 px | 500 | row metadata, hint labels |
| `Label` | 13 px | 500 | band labels, section headers |
| `Value` | 14 px | 700 | right-aligned values, counts |
| `Body` | 17 px | 500 | list item titles, prose chrome |
| `Title` | 24 px | 700 | screen and book titles |

Five `.rfnt` assets, roughly 6 KB each. A caller wanting a non-1.0 `uiScale` picks the nearest available size rather than scaling glyphs.

---

### Task 1: Text ink colour

Removes the scratch-framebuffer inversion. `drawText` currently always draws black.

**Files:**
- Modify: `core/include/reader/text.h`, `core/src/text.cpp`
- Test: `test/unit/test_text.cpp` (create)

- [ ] **Step 1: Write the failing test**

`test/unit/test_text.cpp`:

```cpp
#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/font.h"
#include "reader/framebuffer.h"
#include "reader/text.h"

static std::vector<uint8_t> slurpFont(const char* name) {
  std::ifstream f(std::string(ASSETS_DIR) + "/built/" + name, std::ios::binary);
  REQUIRE(f.good());
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

TEST_CASE("drawText can draw white ink on a black field") {
  auto bytes = slurpFont("spacegrotesk_500_16.rfnt");
  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));

  reader::Framebuffer black(64, 32);
  black.fillRect(0, 0, 64, 32, false);          // all black
  const int advance = reader::drawText(black, font, 4, 22, "A", reader::Ink::White);
  CHECK(advance > 0);

  // Some pixel inside the glyph must now be white, and the field still black.
  bool anyWhite = false;
  for (int y = 0; y < 32 && !anyWhite; ++y)
    for (int x = 0; x < 64 && !anyWhite; ++x)
      if (black.getPixel(x, y)) anyWhite = true;
  CHECK(anyWhite);
  CHECK_FALSE(black.getPixel(63, 31));          // untouched corner stays black
}

TEST_CASE("black ink is still the default") {
  auto bytes = slurpFont("spacegrotesk_500_16.rfnt");
  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));
  reader::Framebuffer white(64, 32);
  reader::drawText(white, font, 4, 22, "A");
  bool anyBlack = false;
  for (int y = 0; y < 32 && !anyBlack; ++y)
    for (int x = 0; x < 64 && !anyBlack; ++x)
      if (!white.getPixel(x, y)) anyBlack = true;
  CHECK(anyBlack);
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cd /Users/lucasgoudin/dev/encre && cmake -S . -B build && make test
```

Expected: FAIL — `reader::Ink` does not exist. (CMake globs sources, so the configure step is required after adding a file.)

- [ ] **Step 3: Implement**

`core/include/reader/text.h` — replace the whole file:

```cpp
#pragma once
#include <string_view>

namespace reader {
class Framebuffer;
class Font;

// Which colour glyph coverage paints. White exists so inverted text (a focused
// row, a filled action block) needs no scratch buffer: fill the rect black,
// then draw over it with Ink::White.
enum class Ink { Black, White };

// Draws UTF-8 text with kerning; (x, baselineY) is the pen origin.
// `tracking` adds that many pixels after every glyph, for the letterspaced
// labels the design uses. Returns the advance width consumed.
int drawText(Framebuffer& fb, const Font& font, int x, int baselineY, std::string_view utf8,
             Ink ink = Ink::Black, int tracking = 0);
}  // namespace reader
```

`core/src/text.cpp` — replace the whole file:

```cpp
#include "reader/text.h"

#include "reader/font.h"
#include "reader/framebuffer.h"

namespace reader {

int drawText(Framebuffer& fb, const Font& font, int x, int baselineY, std::string_view utf8,
             Ink ink, int tracking) {
  const bool white = (ink == Ink::White);
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
          fb.setPixel(pen + g->xOff + col, baselineY - g->yOff + row, white);
    }
    pen += g->advance + tracking;
    prev = cp;
  }
  return pen - x;
}

}  // namespace reader
```

- [ ] **Step 4: Run tests**

```bash
make test
```

Expected: PASS. The existing `text render matches golden` and `QuietTheme renders Home to golden` tests must still pass — the default argument keeps black ink and zero tracking, so no rendering changes.

- [ ] **Step 5: Commit**

```bash
git add core/include/reader/text.h core/src/text.cpp test/unit/test_text.cpp
git commit -m "feat(core): ink colour and letterspacing on drawText"
```

---

### Task 2: Letterspacing in measurement

`Font::measure` must agree with `drawText`'s advance or right-aligned text drifts. Tracking has to reach both.

**Files:**
- Modify: `core/include/reader/font.h`, `core/src/font.cpp`
- Test: `test/unit/test_text.cpp` (append)

- [ ] **Step 1: Write the failing test**

Append to `test/unit/test_text.cpp`:

```cpp
TEST_CASE("measure accounts for tracking and agrees with drawText") {
  auto bytes = slurpFont("spacegrotesk_500_16.rfnt");
  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));

  const int plain = font.measure("LIBRARY");
  const int tracked = font.measure("LIBRARY", 2);
  CHECK(tracked == plain + 2 * 7);  // 7 glyphs, 2px each

  // The advance drawText reports must equal what measure predicts, or
  // right-aligned chrome drifts.
  reader::Framebuffer fb(400, 40);
  CHECK(reader::drawText(fb, font, 0, 30, "LIBRARY", reader::Ink::Black, 2) == tracked);
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
make test
```

Expected: FAIL — `measure` takes one argument.

- [ ] **Step 3: Implement**

In `core/include/reader/font.h`, change the `measure` declaration to:

```cpp
  // `tracking` adds that many pixels after every glyph, matching drawText.
  int measure(std::string_view utf8, int tracking = 0) const;
```

In `core/src/font.cpp`, replace the `measure` definition with:

```cpp
int Font::measure(std::string_view utf8, int tracking) const {
  int w = 0;
  char32_t prev = 0;
  for (size_t i = 0; i < utf8.size();) {
    const char32_t cp = utf8Next(utf8, i);
    const Glyph* g = glyph(cp);
    if (!g) continue;
    if (prev) w += kerning(prev, cp);
    w += g->advance + tracking;
    prev = cp;
  }
  return w;
}
```

- [ ] **Step 4: Run tests** — `make test`, expected PASS with both goldens unchanged.

- [ ] **Step 5: Commit**

```bash
git add core/include/reader/font.h core/src/font.cpp test/unit/test_text.cpp
git commit -m "feat(core): tracking-aware text measurement"
```

---

### Task 3: Visible missing-glyph box

Today a codepoint absent from a font renders as nothing, so malformed or out-of-subset text is silently invisible. Neither bundled TTF has U+FFFD, so the fallback must be drawn, not looked up.

**Files:**
- Modify: `core/src/text.cpp`
- Test: `test/unit/test_text.cpp` (append)

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("a missing glyph draws a visible box rather than nothing") {
  auto bytes = slurpFont("spacegrotesk_500_16.rfnt");
  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));
  REQUIRE(font.glyph(0x4E2D) == nullptr);   // CJK, definitely not in the subset

  reader::Framebuffer fb(64, 32);
  const int advance = reader::drawText(fb, font, 4, 24, "\xE4\xB8\xAD");  // U+4E2D
  CHECK(advance > 0);                        // it occupies space

  int inked = 0;
  for (int y = 0; y < 32; ++y)
    for (int x = 0; x < 64; ++x)
      if (!fb.getPixel(x, y)) ++inked;
  CHECK(inked > 0);                          // and it is visible

  // A hollow box: its interior is untouched, so it reads as a placeholder
  // rather than a solid blob.
  CHECK(inked < 4 * font.ascent());
}
```

- [ ] **Step 2: Run to verify it fails** — `make test`, expected FAIL (`advance` is 0, nothing inked).

- [ ] **Step 3: Implement**

In `core/src/text.cpp`, replace `if (!g) continue;` with:

```cpp
    if (!g) {
      // No glyph for this codepoint: draw a hollow box so malformed or
      // out-of-subset text is visibly wrong instead of silently invisible.
      const int h = font.ascent() * 2 / 3;
      const int w = h / 2 + 1;
      const int top = baselineY - h;
      for (int col = 0; col < w; ++col) {
        fb.setPixel(pen + col, top, white);
        fb.setPixel(pen + col, baselineY - 1, white);
      }
      for (int row = 0; row < h; ++row) {
        fb.setPixel(pen, top + row, white);
        fb.setPixel(pen + w - 1, top + row, white);
      }
      pen += w + 2 + tracking;
      prev = 0;
      continue;
    }
```

- [ ] **Step 4: Run tests** — `make test`, PASS. Both goldens must be unchanged: they contain no missing glyphs.

- [ ] **Step 5: Commit**

```bash
git add core/src/text.cpp test/unit/test_text.cpp
git commit -m "feat(core): draw a placeholder box for missing glyphs"
```

---

### Task 4: Generate the five-role type ramp

**Files:**
- Modify: `Makefile`
- Generated (committed): `assets/built/spacegrotesk_500_12.rfnt`, `_500_13.rfnt`, `_700_14.rfnt`, `_500_17.rfnt`, `_700_24.rfnt`
- Removed: `assets/built/spacegrotesk_500_16.rfnt`, `assets/built/spacegrotesk_700_16.rfnt` (superseded)

- [ ] **Step 1: Generate the ramp**

`tools/fontc.py` already takes `--weight` and `--autohint`. Run:

```bash
cd /Users/lucasgoudin/dev/encre
python3 tools/fontc.py assets/fonts/SpaceGrotesk.ttf --size 12 --weight 500 --autohint --out assets/built/spacegrotesk_500_12.rfnt
python3 tools/fontc.py assets/fonts/SpaceGrotesk.ttf --size 13 --weight 500 --autohint --out assets/built/spacegrotesk_500_13.rfnt
python3 tools/fontc.py assets/fonts/SpaceGrotesk.ttf --size 14 --weight 700 --autohint --out assets/built/spacegrotesk_700_14.rfnt
python3 tools/fontc.py assets/fonts/SpaceGrotesk.ttf --size 17 --weight 500 --autohint --out assets/built/spacegrotesk_500_17.rfnt
python3 tools/fontc.py assets/fonts/SpaceGrotesk.ttf --size 24 --weight 700 --autohint --out assets/built/spacegrotesk_700_24.rfnt
```

Expected: five lines each reporting `200 glyphs` and the resolved axis, e.g.
`assets/built/spacegrotesk_700_24.rfnt: 200 glyphs, 0 kern pairs, blob NNNN bytes, 24px, wght=700, autohint`

- [ ] **Step 2: Replace the `fonts` target in `Makefile`**

```make
# Rebuilds every generated font asset from the TTFs in assets/fonts. Needs
# freetype-py: pip install -r tools/requirements.txt
# The chrome ramp is a fixed set of roles (see the Phase 2A plan): bitmaps are
# pre-rendered, so each size is its own asset.
fonts:
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --size 12 --weight 500 --autohint --out assets/built/spacegrotesk_500_12.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --size 13 --weight 500 --autohint --out assets/built/spacegrotesk_500_13.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --size 14 --weight 700 --autohint --out assets/built/spacegrotesk_700_14.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --size 17 --weight 500 --autohint --out assets/built/spacegrotesk_500_17.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --size 24 --weight 700 --autohint --out assets/built/spacegrotesk_700_24.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/Literata.ttf --size 18 --weight 400 --opsz 12 --out assets/built/literata_18.rfnt
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_500_12.rfnt --out shell/src/font_meta.h --symbol kFontMeta
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_500_13.rfnt --out shell/src/font_label.h --symbol kFontLabel
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_700_14.rfnt --out shell/src/font_value.h --symbol kFontValue
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_500_17.rfnt --out shell/src/font_body.h --symbol kFontBody
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_700_24.rfnt --out shell/src/font_title.h --symbol kFontTitle
```

- [ ] **Step 3: Run it and confirm reproducibility**

```bash
make fonts && ls -la assets/built/ shell/src/font_*.h
```

Expected: five `.rfnt` chrome assets plus `literata_18.rfnt`, and five `shell/src/font_*.h` headers. `literata_18.rfnt` must be byte-identical to the committed copy — check with `git status assets/built/literata_18.rfnt`, which should show no change.

- [ ] **Step 4: Remove the superseded 16px assets**

```bash
git rm assets/built/spacegrotesk_500_16.rfnt assets/built/spacegrotesk_700_16.rfnt \
       shell/src/font_spacegrotesk_500_16.h shell/src/font_spacegrotesk_700_16.h
```

Do not build yet — Tasks 5 and 6 replace the callers that reference them.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat(fonts): five-role chrome type ramp replacing the single 16px face"
```

---

### Task 5: FontSet — roles instead of two weights

**Files:**
- Create: `core/include/reader/fontset.h`
- Test: `test/unit/test_fontset.cpp`

- [ ] **Step 1: Write the failing test**

`test/unit/test_fontset.cpp`:

```cpp
#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/fontset.h"

static std::vector<uint8_t> slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  REQUIRE(f.good());
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

TEST_CASE("FontSet exposes one loaded face per role and reports readiness") {
  const std::string dir = std::string(ASSETS_DIR) + "/built/";
  auto meta  = slurp(dir + "spacegrotesk_500_12.rfnt");
  auto label = slurp(dir + "spacegrotesk_500_13.rfnt");
  auto value = slurp(dir + "spacegrotesk_700_14.rfnt");
  auto body  = slurp(dir + "spacegrotesk_500_17.rfnt");
  auto title = slurp(dir + "spacegrotesk_700_24.rfnt");

  reader::FontSet fonts;
  CHECK_FALSE(fonts.ready());
  REQUIRE(fonts.load(reader::Role::Meta,  meta.data(),  meta.size()));
  REQUIRE(fonts.load(reader::Role::Label, label.data(), label.size()));
  REQUIRE(fonts.load(reader::Role::Value, value.data(), value.size()));
  REQUIRE(fonts.load(reader::Role::Body,  body.data(),  body.size()));
  REQUIRE(fonts.load(reader::Role::Title, title.data(), title.size()));
  CHECK(fonts.ready());

  // The ramp must actually be a ramp, or the design's hierarchy is lost.
  CHECK(fonts[reader::Role::Title].ascent() > fonts[reader::Role::Body].ascent());
  CHECK(fonts[reader::Role::Body].ascent()  > fonts[reader::Role::Value].ascent());
  CHECK(fonts[reader::Role::Label].ascent() >= fonts[reader::Role::Meta].ascent());
}

TEST_CASE("a rejected blob leaves the set not ready") {
  reader::FontSet fonts;
  const uint8_t junk[16] = {0};
  CHECK_FALSE(fonts.load(reader::Role::Body, junk, sizeof junk));
  CHECK_FALSE(fonts.ready());
}
```

- [ ] **Step 2: Run to verify it fails** — `cmake -S . -B build && make test`, expected FAIL (`reader/fontset.h` not found).

- [ ] **Step 3: Implement**

`core/include/reader/fontset.h`:

```cpp
#pragma once
#include <cstddef>
#include <cstdint>

#include "reader/font.h"

namespace reader {

// The chrome type ramp, by role rather than by pixel size. Bitmap glyphs are
// pre-rendered so each size is its own asset; enumerating the roles keeps the
// asset list finite and lets a theme ask for meaning ("this is a value")
// instead of numbers.
enum class Role : uint8_t { Meta, Label, Value, Body, Title, Count_ };

// Owns nothing: each Font is a zero-copy view, so every blob passed to load()
// must outlive the FontSet. The caller (shell or simulator) picks which asset
// backs each role, which is where device knowledge such as BoardConfig's
// uiScale belongs — core/ never sees a board profile.
class FontSet {
 public:
  bool load(Role r, const uint8_t* data, size_t size);
  const Font& operator[](Role r) const { return faces_[static_cast<size_t>(r)]; }
  bool ready() const;

 private:
  static constexpr size_t kCount = static_cast<size_t>(Role::Count_);
  Font faces_[kCount];
  bool loaded_[kCount] = {};
};

inline bool FontSet::load(Role r, const uint8_t* data, size_t size) {
  const size_t i = static_cast<size_t>(r);
  if (i >= kCount) return false;
  loaded_[i] = faces_[i].load(data, size);
  return loaded_[i];
}

inline bool FontSet::ready() const {
  for (size_t i = 0; i < kCount; ++i)
    if (!loaded_[i]) return false;
  return true;
}

}  // namespace reader
```

- [ ] **Step 4: Run tests** — `make test`, expected PASS.

- [ ] **Step 5: Commit**

```bash
git add core/include/reader/fontset.h test/unit/test_fontset.cpp
git commit -m "feat(core): FontSet — the chrome type ramp addressed by role"
```

---

### Task 6: Icons

The design's hint bar, menu rows and folder entries all carry small glyphs. They are drawn from 1-bit bitmaps rather than the font, because they are UI marks, not text.

**Files:**
- Create: `core/include/reader/icons.h`, `core/src/icons.cpp`
- Test: `test/unit/test_icons.cpp`

- [ ] **Step 1: Write the failing test**

`test/unit/test_icons.cpp`:

```cpp
#include "doctest.h"
#include "reader/framebuffer.h"
#include "reader/icons.h"

TEST_CASE("every icon draws something inside its own box and nothing outside") {
  const reader::Icon* all[] = {&reader::icons::kBack,   &reader::icons::kDot,
                              &reader::icons::kUp,      &reader::icons::kDown,
                              &reader::icons::kChevron, &reader::icons::kBook,
                              &reader::icons::kFolder};
  for (const reader::Icon* ic : all) {
    reader::Framebuffer fb(48, 48);
    reader::drawIcon(fb, *ic, 8, 8, reader::Ink::Black);
    int inked = 0, outside = 0;
    for (int y = 0; y < 48; ++y)
      for (int x = 0; x < 48; ++x)
        if (!fb.getPixel(x, y)) {
          ++inked;
          const bool inBox = x >= 8 && y >= 8 && x < 8 + ic->w && y < 8 + ic->h;
          if (!inBox) ++outside;
        }
    CHECK(inked > 0);
    CHECK(outside == 0);
  }
}

TEST_CASE("icons can draw in white for inverted rows") {
  reader::Framebuffer fb(48, 48);
  fb.fillRect(0, 0, 48, 48, false);
  reader::drawIcon(fb, reader::icons::kDot, 8, 8, reader::Ink::White);
  bool anyWhite = false;
  for (int y = 0; y < 48 && !anyWhite; ++y)
    for (int x = 0; x < 48 && !anyWhite; ++x)
      if (fb.getPixel(x, y)) anyWhite = true;
  CHECK(anyWhite);
}
```

- [ ] **Step 2: Run to verify it fails** — expected FAIL, `reader/icons.h` not found.

- [ ] **Step 3: Implement**

`core/include/reader/icons.h`:

```cpp
#pragma once
#include <cstdint>

#include "reader/text.h"  // Ink

namespace reader {
class Framebuffer;

// A UI mark, not a character: 1bpp rows, MSB-first, a set bit meaning ink.
// Hand-authored at the sizes the design uses so they stay crisp on a 1-bit
// panel — scaled bitmaps would not.
struct Icon {
  int w, h;
  const uint8_t* rows;  // ((w + 7) / 8) * h bytes
};

void drawIcon(Framebuffer& fb, const Icon& icon, int x, int y, Ink ink = Ink::Black);

namespace icons {
extern const Icon kBack;     // arrow curving left
extern const Icon kDot;      // filled circle: the Confirm button
extern const Icon kUp;       // chevron up in a circle-less form
extern const Icon kDown;     // chevron down
extern const Icon kChevron;  // right-pointing disclosure
extern const Icon kBook;     // open book: the Read action
extern const Icon kFolder;   // folder: a Library directory row
}  // namespace icons

}  // namespace reader
```

`core/src/icons.cpp`:

```cpp
#include "reader/icons.h"

#include "reader/framebuffer.h"

namespace reader {

void drawIcon(Framebuffer& fb, const Icon& icon, int x, int y, Ink ink) {
  const bool white = (ink == Ink::White);
  const int rowBytes = (icon.w + 7) / 8;
  for (int row = 0; row < icon.h; ++row) {
    const uint8_t* src = icon.rows + row * rowBytes;
    for (int col = 0; col < icon.w; ++col)
      if ((src[col / 8] >> (7 - col % 8)) & 1) fb.setPixel(x + col, y + row, white);
  }
}

namespace icons {
namespace {
// 13x13 marks, one byte per row (13 bits rounded to 2 bytes). Authored as
// binary literals so the shape is readable and editable in place.
#define R2(a, b) 0b##a, 0b##b

const uint8_t kBackBits[] = {
    R2(00000000, 00000000), R2(00000100, 00000000), R2(00001100, 00000000),
    R2(00011100, 00000000), R2(00111111, 11100000), R2(01111111, 11110000),
    R2(11100000, 00111000), R2(01110000, 00111000), R2(00111000, 00111000),
    R2(00011100, 00111000), R2(00000000, 00111000), R2(00000000, 00000000),
    R2(00000000, 00000000),
};
const uint8_t kDotBits[] = {
    R2(00000000, 00000000), R2(00000000, 00000000), R2(00001110, 00000000),
    R2(00111111, 10000000), R2(01111111, 11000000), R2(01111111, 11000000),
    R2(11111111, 11100000), R2(01111111, 11000000), R2(01111111, 11000000),
    R2(00111111, 10000000), R2(00001110, 00000000), R2(00000000, 00000000),
    R2(00000000, 00000000),
};
const uint8_t kUpBits[] = {
    R2(00000000, 00000000), R2(00000100, 00000000), R2(00001110, 00000000),
    R2(00011111, 00000000), R2(00111011, 10000000), R2(01110001, 11000000),
    R2(11100000, 11100000), R2(00000000, 00000000), R2(00001110, 00000000),
    R2(00001110, 00000000), R2(00001110, 00000000), R2(00000000, 00000000),
    R2(00000000, 00000000),
};
const uint8_t kDownBits[] = {
    R2(00000000, 00000000), R2(00001110, 00000000), R2(00001110, 00000000),
    R2(00001110, 00000000), R2(00000000, 00000000), R2(11100000, 11100000),
    R2(01110001, 11000000), R2(00111011, 10000000), R2(00011111, 00000000),
    R2(00001110, 00000000), R2(00000100, 00000000), R2(00000000, 00000000),
    R2(00000000, 00000000),
};
const uint8_t kChevronBits[] = {
    R2(00000000, 00000000), R2(00110000, 00000000), R2(00111000, 00000000),
    R2(00011100, 00000000), R2(00001110, 00000000), R2(00000111, 00000000),
    R2(00000011, 10000000), R2(00000111, 00000000), R2(00001110, 00000000),
    R2(00011100, 00000000), R2(00111000, 00000000), R2(00110000, 00000000),
    R2(00000000, 00000000),
};
const uint8_t kBookBits[] = {
    R2(00000000, 00000000), R2(01111100, 11111000), R2(11111110, 11111100),
    R2(11000110, 11000110), R2(11000110, 11000110), R2(11000110, 11000110),
    R2(11000110, 11000110), R2(11000110, 11000110), R2(11000110, 11000110),
    R2(11111110, 11111100), R2(01111100, 11111000), R2(00000000, 00000000),
    R2(00000000, 00000000),
};
const uint8_t kFolderBits[] = {
    R2(00000000, 00000000), R2(11111000, 00000000), R2(11111100, 00000000),
    R2(11111111, 11111000), R2(10000000, 00001000), R2(10000000, 00001000),
    R2(10000000, 00001000), R2(10000000, 00001000), R2(10000000, 00001000),
    R2(11111111, 11111000), R2(00000000, 00000000), R2(00000000, 00000000),
    R2(00000000, 00000000),
};
#undef R2
}  // namespace

const Icon kBack{13, 13, kBackBits};
const Icon kDot{13, 13, kDotBits};
const Icon kUp{13, 13, kUpBits};
const Icon kDown{13, 13, kDownBits};
const Icon kChevron{13, 13, kChevronBits};
const Icon kBook{13, 13, kBookBits};
const Icon kFolder{13, 13, kFolderBits};
}  // namespace icons

}  // namespace reader
```

- [ ] **Step 4: Run tests** — `cmake -S . -B build && make test`, expected PASS.

- [ ] **Step 5: Render them for a human look**

The bitmaps above are hand-authored and may read badly at size. Add a temporary check by rendering an icon strip via the simulator once Task 9 lands; for now confirm the tests pass and note in the commit that shapes are unreviewed.

- [ ] **Step 6: Commit**

```bash
git add core/include/reader/icons.h core/src/icons.cpp test/unit/test_icons.cpp
git commit -m "feat(core): 1-bit icon set for chrome marks

Shapes are hand-authored and not yet reviewed at size on a panel."
```

---

### Task 7: Dither fills

The design fakes grey with dot patterns — cover placeholders, the sleep-screen field, the veil behind overlays. One ordered-dither fill covers all of them.

**Files:**
- Create: `core/include/reader/dither.h`, `core/src/dither.cpp`
- Test: `test/unit/test_dither.cpp`

- [ ] **Step 1: Write the failing test**

`test/unit/test_dither.cpp`:

```cpp
#include "doctest.h"
#include "reader/dither.h"
#include "reader/framebuffer.h"

static int inkCount(const reader::Framebuffer& fb, int x, int y, int w, int h) {
  int n = 0;
  for (int yy = y; yy < y + h; ++yy)
    for (int xx = x; xx < x + w; ++xx)
      if (!fb.getPixel(xx, yy)) ++n;
  return n;
}

TEST_CASE("dither density rises monotonically with the requested level") {
  int last = -1;
  for (int level = 0; level <= 4; ++level) {
    reader::Framebuffer fb(64, 64);
    reader::ditherRect(fb, 0, 0, 64, 64, level);
    const int n = inkCount(fb, 0, 0, 64, 64);
    CHECK(n > last);
    last = n;
  }
}

TEST_CASE("level 0 leaves the area white and level 4 fills it solid") {
  reader::Framebuffer fb(32, 32);
  reader::ditherRect(fb, 0, 0, 32, 32, 0);
  CHECK(inkCount(fb, 0, 0, 32, 32) == 0);
  reader::ditherRect(fb, 0, 0, 32, 32, 4);
  CHECK(inkCount(fb, 0, 0, 32, 32) == 32 * 32);
}

TEST_CASE("dither stays inside its rect") {
  reader::Framebuffer fb(32, 32);
  reader::ditherRect(fb, 8, 8, 16, 16, 3);
  CHECK(inkCount(fb, 0, 0, 32, 8) == 0);
  CHECK(inkCount(fb, 0, 0, 8, 32) == 0);
  CHECK(inkCount(fb, 24, 0, 8, 32) == 0);
}
```

- [ ] **Step 2: Run to verify it fails** — expected FAIL, `reader/dither.h` not found.

- [ ] **Step 3: Implement**

`core/include/reader/dither.h`:

```cpp
#pragma once
namespace reader {
class Framebuffer;

// Ordered (Bayer 4x4) dither fill. `level` is 0 (white) to 4 (solid black);
// the panel has no real greys, so apparent tone comes from a stipple the eye
// integrates at reading distance. Used for cover placeholders, sleep-screen
// fields and the veil behind overlays.
void ditherRect(Framebuffer& fb, int x, int y, int w, int h, int level);
}  // namespace reader
```

`core/src/dither.cpp`:

```cpp
#include "reader/dither.h"

#include "reader/framebuffer.h"

namespace reader {

namespace {
// Classic Bayer 4x4 threshold matrix, values 0..15.
constexpr int kBayer[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};
}  // namespace

void ditherRect(Framebuffer& fb, int x, int y, int w, int h, int level) {
  if (level <= 0) return;
  if (level > 4) level = 4;
  // level 1..4 -> threshold 4, 8, 12, 16 out of 16 cells inked.
  const int threshold = level * 4;
  for (int yy = y; yy < y + h; ++yy)
    for (int xx = x; xx < x + w; ++xx)
      if (kBayer[yy & 3][xx & 3] < threshold) fb.setPixel(xx, yy, false);
}

}  // namespace reader
```

- [ ] **Step 4: Run tests** — `cmake -S . -B build && make test`, expected PASS.

- [ ] **Step 5: Commit**

```bash
git add core/include/reader/dither.h core/src/dither.cpp test/unit/test_dither.cpp
git commit -m "feat(core): Bayer dither fill for cover and overlay tone"
```

---

### Task 8: Canvas-agnostic component primitives

This is the task that removes the 480-wide assumption. Every primitive takes the framebuffer and derives its geometry from it.

**Files:**
- Create: `core/include/reader/components.h`, `core/src/components.cpp`
- Test: `test/unit/test_components.cpp`

- [ ] **Step 1: Write the failing test**

`test/unit/test_components.cpp`:

```cpp
#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/components.h"
#include "reader/fontset.h"
#include "reader/framebuffer.h"

static std::vector<uint8_t> slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  REQUIRE(f.good());
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

struct Fixture {
  std::vector<uint8_t> a, b, c, d, e;
  reader::FontSet fonts;
  Fixture() {
    const std::string dir = std::string(ASSETS_DIR) + "/built/";
    a = slurp(dir + "spacegrotesk_500_12.rfnt");
    b = slurp(dir + "spacegrotesk_500_13.rfnt");
    c = slurp(dir + "spacegrotesk_700_14.rfnt");
    d = slurp(dir + "spacegrotesk_500_17.rfnt");
    e = slurp(dir + "spacegrotesk_700_24.rfnt");
    fonts.load(reader::Role::Meta, a.data(), a.size());
    fonts.load(reader::Role::Label, b.data(), b.size());
    fonts.load(reader::Role::Value, c.data(), c.size());
    fonts.load(reader::Role::Body, d.data(), d.size());
    fonts.load(reader::Role::Title, e.data(), e.size());
    REQUIRE(fonts.ready());
  }
};

TEST_CASE("the header band right-aligns its value on any canvas width") {
  Fixture f;
  for (int width : {480, 528}) {
    reader::Framebuffer fb(width, 200);
    const int h = reader::drawHeaderBand(fb, f.fonts, "NOW READING", "87%");
    CHECK(h > 0);
    // Ink must reach close to the right margin, and never past it.
    int rightmost = -1;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < width; ++x)
        if (!fb.getPixel(x, y) && x > rightmost) rightmost = x;
    CHECK(rightmost <= width - reader::kMargin);
    CHECK(rightmost > width - reader::kMargin - 60);
  }
}

TEST_CASE("a focused row inverts: black field, white text") {
  Fixture f;
  reader::Framebuffer fb(480, 120);
  reader::drawRow(fb, f.fonts, 0, "LIBRARY", "12", /*focused=*/true);
  // The row's field is black...
  CHECK_FALSE(fb.getPixel(2, 10));
  // ...and contains white glyph pixels.
  bool anyWhite = false;
  for (int y = 0; y < 56 && !anyWhite; ++y)
    for (int x = 0; x < 480 && !anyWhite; ++x)
      if (fb.getPixel(x, y)) anyWhite = true;
  CHECK(anyWhite);
}

TEST_CASE("hint slots distribute across the canvas and never overlap") {
  Fixture f;
  for (int width : {480, 528}) {
    reader::Framebuffer fb(width, 120);
    const reader::Hint hints[4] = {{&reader::icons::kBack, "BACK", ""},
                                   {&reader::icons::kDot, "OPEN", "HOLD"},
                                   {&reader::icons::kUp, "UP", ""},
                                   {&reader::icons::kDown, "DOWN", ""}};
    int slotX[4] = {};
    reader::drawHintBar(fb, f.fonts, hints, slotX);
    for (int i = 1; i < 4; ++i) CHECK(slotX[i] > slotX[i - 1]);
    CHECK(slotX[0] >= reader::kMargin);
    CHECK(slotX[3] < width - reader::kMargin);
  }
}
```

- [ ] **Step 2: Run to verify it fails** — expected FAIL, `reader/components.h` not found.

- [ ] **Step 3: Implement**

`core/include/reader/components.h`:

```cpp
#pragma once
#include <string>
#include <string_view>

#include "reader/fontset.h"
#include "reader/icons.h"

namespace reader {
class Framebuffer;

// Fixed in pixels, not proportional: the X3 and X4 are within ~2% of the same
// PPI, so a margin should be the same physical size on both. What must adapt is
// the canvas width, which every primitive reads from the framebuffer.
inline constexpr int kMargin = 24;
inline constexpr int kBandH = 52;
inline constexpr int kRowH = 56;
inline constexpr int kHintBarH = 46;
inline constexpr int kLabelTracking = 2;

// One hint-bar slot. `hold` is the second line a long-press variant gets, drawn
// inside the owning button's slot rather than as a fifth hint.
struct Hint {
  const Icon* icon;
  std::string_view label;
  std::string_view hold;
};

// Each returns the height it consumed, so callers stack without recomputing.
int drawHeaderBand(Framebuffer& fb, const FontSet& fonts, std::string_view label,
                   std::string_view value);
int drawRow(Framebuffer& fb, const FontSet& fonts, int y, std::string_view label,
            std::string_view value, bool focused);
// Draws at the bottom of fb. Reports each slot's x in slotXOut[4] so tests and
// callers can assert the distribution.
int drawHintBar(Framebuffer& fb, const FontSet& fonts, const Hint hints[4], int slotXOut[4]);

}  // namespace reader
```

`core/src/components.cpp`:

```cpp
#include "reader/components.h"

#include "reader/framebuffer.h"
#include "reader/text.h"

namespace reader {

int drawHeaderBand(Framebuffer& fb, const FontSet& fonts, std::string_view label,
                   std::string_view value) {
  const Font& lf = fonts[Role::Label];
  const Font& vf = fonts[Role::Value];
  const int baseline = kBandH / 2 + lf.ascent() / 2;
  drawText(fb, lf, kMargin, baseline, label, Ink::Black, kLabelTracking);
  const int vw = vf.measure(value);
  drawText(fb, vf, fb.width() - kMargin - vw, baseline, value);
  fb.fillRect(0, kBandH - 2, fb.width(), 2, false);
  return kBandH;
}

int drawRow(Framebuffer& fb, const FontSet& fonts, int y, std::string_view label,
            std::string_view value, bool focused) {
  const Font& lf = fonts[Role::Body];
  const Font& vf = fonts[Role::Value];
  const Ink ink = focused ? Ink::White : Ink::Black;
  if (focused)
    fb.fillRect(0, y, fb.width(), kRowH, false);
  else
    fb.fillRect(0, y, fb.width(), 1, false);  // hairline above
  const int baseline = y + kRowH / 2 + lf.ascent() / 2;
  drawText(fb, lf, kMargin, baseline, label, ink);
  if (!value.empty()) {
    const int vw = vf.measure(value);
    drawText(fb, vf, fb.width() - kMargin - vw, baseline, value, ink);
  }
  return kRowH;
}

int drawHintBar(Framebuffer& fb, const FontSet& fonts, const Hint hints[4], int slotXOut[4]) {
  const Font& mf = fonts[Role::Meta];
  const int top = fb.height() - kHintBarH;
  fb.fillRect(0, top, fb.width(), 1, false);

  // Measure every slot, then distribute the leftover space evenly. This is what
  // makes the bar correct on both 480 and 528 wide canvases: nothing is pinned.
  int widths[4] = {};
  int total = 0;
  for (int i = 0; i < 4; ++i) {
    const int iconW = hints[i].icon ? hints[i].icon->w + 6 : 0;
    const int textW = mf.measure(hints[i].label, kLabelTracking);
    const int holdW = hints[i].hold.empty() ? 0 : mf.measure(hints[i].hold, kLabelTracking);
    widths[i] = iconW + (textW > holdW ? textW : holdW);
    total += widths[i];
  }
  const int usable = fb.width() - 2 * kMargin;
  const int gap = (usable > total && total > 0) ? (usable - total) / 3 : 0;

  int x = kMargin;
  const int baseline = top + kHintBarH / 2 + mf.ascent() / 2 -
                       (hints[0].hold.empty() ? 0 : mf.lineHeight() / 2);
  for (int i = 0; i < 4; ++i) {
    slotXOut[i] = x;
    int textX = x;
    if (hints[i].icon) {
      drawIcon(fb, *hints[i].icon, x, baseline - hints[i].icon->h + 2);
      textX += hints[i].icon->w + 6;
    }
    drawText(fb, mf, textX, baseline, hints[i].label, Ink::Black, kLabelTracking);
    if (!hints[i].hold.empty())
      drawText(fb, mf, textX, baseline + mf.lineHeight(), hints[i].hold, Ink::Black,
               kLabelTracking);
    x += widths[i] + gap;
  }
  return kHintBarH;
}

}  // namespace reader
```

- [ ] **Step 4: Run tests** — `cmake -S . -B build && make test`, expected PASS.

- [ ] **Step 5: Commit**

```bash
git add core/include/reader/components.h core/src/components.cpp test/unit/test_components.cpp
git commit -m "feat(core): canvas-agnostic header/row/hint-bar primitives"
```

---

### Task 9: Home at design fidelity

Rebuilds `QuietTheme::renderHome` on the new primitives: two-column composition with a dithered cover, the real type ramp, icons in the hint bar. This is the task that makes `make compare` show a match.

**Files:**
- Modify: `core/include/reader/theme_quiet.h`, `core/src/theme_quiet.cpp`, `core/include/reader/theme.h`, `core/include/reader/viewmodel.h`
- Modify: `sim/main.cpp`, `shell/src/main.cpp`
- Test: `test/unit/test_theme_home_golden.cpp` (rewrite)
- Golden: `test/golden/home_quiet.png` (re-bless), `test/golden/home_quiet_x3.png` (new)

- [ ] **Step 1: Extend the view-model with what the design shows**

`core/include/reader/viewmodel.h` — add to `HomeViewModel`, after `batteryPercent`:

```cpp
  // Cover art is not decoded yet (Phase 3 owns EPUB images), so the theme draws
  // a dithered placeholder carrying the title. This flag says whether a real
  // cover exists, so the placeholder can be replaced without a view-model change.
  bool hasCover = false;
```

- [ ] **Step 2: Change the theme interface to take a FontSet**

`core/include/reader/theme.h` — replace the whole file:

```cpp
#pragma once
namespace reader {
class Framebuffer;
class FontSet;
struct HomeViewModel;

// Themes own the entire presentation, layout structure included (spec 3.3).
// The FontSet is supplied by the caller so device knowledge — which asset backs
// which role, any board uiScale — stays out of core/.
class Theme {
 public:
  virtual ~Theme() = default;
  virtual void renderHome(Framebuffer& fb, const FontSet& fonts, const HomeViewModel& vm) = 0;
};
}  // namespace reader
```

`core/include/reader/theme_quiet.h` — replace the whole file:

```cpp
#pragma once
#include "reader/fontset.h"
#include "reader/theme.h"

namespace reader {

// B2 "Quiet": Space Grotesk chrome, a 2px-ruled header band, hairline rows,
// black fill for focus. Layout follows the design canvas and derives every
// horizontal position from the framebuffer, so the same code composes correctly
// on the X4's 480x800 and the X3's 528x792.
class QuietTheme : public Theme {
 public:
  void renderHome(Framebuffer& fb, const FontSet& fonts, const HomeViewModel& vm) override;
};

}  // namespace reader
```

- [ ] **Step 3: Write the failing golden test**

`test/unit/test_theme_home_golden.cpp` — replace the whole file:

```cpp
#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "golden.h"
#include "reader/fontset.h"
#include "reader/framebuffer.h"
#include "reader/theme_quiet.h"
#include "reader/viewmodel.h"

static std::vector<uint8_t> slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  REQUIRE(f.good());
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

static reader::HomeViewModel sampleHome() {
  reader::HomeViewModel vm;
  vm.title = "Middlemarch";
  vm.author = "George Eliot";
  vm.chapterLabel = "CH. 01 — MISS BROOKE";
  vm.percent = 6;
  vm.currentPage = 53;
  vm.pageCount = 890;
  vm.batteryPercent = 87;
  vm.hasCover = false;
  vm.menu = {{"LIBRARY", "12"}, {"SETTINGS", ""}};
  vm.focusedMenuIndex = -1;
  vm.hints = {"READ", "SELECT", "UP", "DOWN"};
  return vm;
}

TEST_CASE("QuietTheme renders Home to golden on both panel geometries") {
  const std::string dir = std::string(ASSETS_DIR) + "/built/";
  auto a = slurp(dir + "spacegrotesk_500_12.rfnt");
  auto b = slurp(dir + "spacegrotesk_500_13.rfnt");
  auto c = slurp(dir + "spacegrotesk_700_14.rfnt");
  auto d = slurp(dir + "spacegrotesk_500_17.rfnt");
  auto e = slurp(dir + "spacegrotesk_700_24.rfnt");
  reader::FontSet fonts;
  fonts.load(reader::Role::Meta, a.data(), a.size());
  fonts.load(reader::Role::Label, b.data(), b.size());
  fonts.load(reader::Role::Value, c.data(), c.size());
  fonts.load(reader::Role::Body, d.data(), d.size());
  fonts.load(reader::Role::Title, e.data(), e.size());
  REQUIRE(fonts.ready());

  reader::QuietTheme theme;

  SUBCASE("X4 480x800") {
    reader::Framebuffer fb(480, 800);
    theme.renderHome(fb, fonts, sampleHome());
    checkGolden(fb, "home_quiet");
  }
  SUBCASE("X3 528x792") {
    reader::Framebuffer fb(528, 792);
    theme.renderHome(fb, fonts, sampleHome());
    checkGolden(fb, "home_quiet_x3");
  }
}
```

- [ ] **Step 4: Run to verify it fails** — `cmake -S . -B build && make test`. Expected FAIL: `renderHome` has the old signature.

- [ ] **Step 5: Implement the theme**

`core/src/theme_quiet.cpp` — replace the whole file:

```cpp
#include "reader/theme_quiet.h"

#include <string>

#include "reader/components.h"
#include "reader/dither.h"
#include "reader/framebuffer.h"
#include "reader/text.h"
#include "reader/viewmodel.h"

namespace reader {

namespace {
constexpr int kCoverW = 140;
constexpr int kCoverH = 210;
constexpr int kGutter = 22;
constexpr int kBlockH = 52;

// A dithered stand-in until Phase 3 decodes real cover images: a bordered
// panel with the title reversed out of a filled strip along its bottom.
void drawCoverPlaceholder(Framebuffer& fb, const FontSet& fonts, int x, int y,
                          std::string_view title) {
  ditherRect(fb, x, y, kCoverW, kCoverH, 2);
  fb.fillRect(x, y, kCoverW, 2, false);
  fb.fillRect(x, y + kCoverH - 2, kCoverW, 2, false);
  fb.fillRect(x, y, 2, kCoverH, false);
  fb.fillRect(x + kCoverW - 2, y, 2, kCoverH, false);
  const Font& bf = fonts[Role::Body];
  const int stripH = bf.lineHeight() + 10;
  const int stripY = y + kCoverH - stripH - 2;
  fb.fillRect(x + 2, stripY, kCoverW - 4, stripH, true);
  fb.fillRect(x + 2, stripY, kCoverW - 4, 2, false);
  drawText(fb, bf, x + 10, stripY + stripH - 8, title);
}
}  // namespace

void QuietTheme::renderHome(Framebuffer& fb, const FontSet& fonts, const HomeViewModel& vm) {
  fb.clear(true);
  int y = drawHeaderBand(fb, fonts, "NOW READING", std::to_string(vm.batteryPercent) + "%");

  // Two columns: cover on the left, the reading state stacked on the right.
  y += 28;
  drawCoverPlaceholder(fb, fonts, kMargin, y, vm.title);

  const int rightX = kMargin + kCoverW + kGutter;
  const Font& title = fonts[Role::Title];
  const Font& body = fonts[Role::Body];
  const Font& meta = fonts[Role::Meta];
  int ry = y + title.ascent();
  drawText(fb, title, rightX, ry, vm.title);
  ry += body.lineHeight() + 6;
  drawText(fb, body, rightX, ry, vm.author);

  // The percentage is the one display-scale number on the screen.
  ry = y + kCoverH - meta.lineHeight() * 2 - 8;
  drawText(fb, title, rightX, ry, std::to_string(vm.percent) + "%");
  ry += meta.lineHeight() + 4;
  drawText(fb, meta, rightX, ry,
           "PAGE " + std::to_string(vm.currentPage) + " / " + std::to_string(vm.pageCount),
           Ink::Black, kLabelTracking);
  ry += meta.lineHeight() + 2;
  drawText(fb, meta, rightX, ry, vm.chapterLabel, Ink::Black, kLabelTracking);

  y += kCoverH + 22;

  // Progress bar spans the usable width.
  const int barW = fb.width() - 2 * kMargin;
  fb.fillRect(kMargin, y, barW, 8, false);
  fb.fillRect(kMargin + 1, y + 1, barW - 2, 6, true);
  const int clamped = vm.percent < 0 ? 0 : (vm.percent > 100 ? 100 : vm.percent);
  fb.fillRect(kMargin + 1, y + 1, (barW - 2) * clamped / 100, 6, false);
  y += 8 + 22;

  // Continue block: focused when no menu row is.
  const bool continueFocused = (vm.focusedMenuIndex < 0);
  const Font& label = fonts[Role::Label];
  if (continueFocused) {
    fb.fillRect(kMargin, y, barW, kBlockH, false);
  } else {
    fb.fillRect(kMargin, y, barW, 2, false);
    fb.fillRect(kMargin, y + kBlockH - 2, barW, 2, false);
    fb.fillRect(kMargin, y, 2, kBlockH, false);
    fb.fillRect(kMargin + barW - 2, y, 2, kBlockH, false);
  }
  const Ink cink = continueFocused ? Ink::White : Ink::Black;
  drawText(fb, label, kMargin + 20, y + kBlockH / 2 + label.ascent() / 2, "CONTINUE", cink,
           kLabelTracking);
  drawIcon(fb, icons::kChevron, kMargin + barW - 20 - icons::kChevron.w,
           y + kBlockH / 2 - icons::kChevron.h / 2, cink);

  // Menu rows sit above the hint bar.
  const int menuTop = fb.height() - kHintBarH - static_cast<int>(vm.menu.size()) * kRowH;
  for (size_t i = 0; i < vm.menu.size(); ++i)
    drawRow(fb, fonts, menuTop + static_cast<int>(i) * kRowH, vm.menu[i].label, vm.menu[i].value,
            static_cast<int>(i) == vm.focusedMenuIndex);

  const Hint hints[4] = {{&icons::kBook, vm.hints[0], ""},
                         {&icons::kDot, vm.hints[1], ""},
                         {&icons::kUp, vm.hints[2], ""},
                         {&icons::kDown, vm.hints[3], ""}};
  int slots[4] = {};
  drawHintBar(fb, fonts, hints, slots);
}

}  // namespace reader
```

- [ ] **Step 6: Update the simulator**

`sim/main.cpp` — replace the font loading and render block so it builds a `FontSet` and accepts a canvas size. Replace the whole file:

```cpp
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "reader/fontset.h"
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
  if (argc < 3) {
    std::fprintf(stderr, "usage: reader_sim SCREEN OUT.png [--canvas WxH]\n");
    return 2;
  }
  int w = 480, h = 800;
  for (int i = 3; i + 1 < argc; ++i)
    if (std::strcmp(argv[i], "--canvas") == 0) std::sscanf(argv[i + 1], "%dx%d", &w, &h);

  if (std::strcmp(argv[1], "home") != 0) {
    std::fprintf(stderr, "unknown screen '%s' (only 'home' so far)\n", argv[1]);
    return 3;
  }

  const std::string dir = std::string(ASSETS_DIR) + "/built/";
  auto a = slurp(dir + "spacegrotesk_500_12.rfnt");
  auto b = slurp(dir + "spacegrotesk_500_13.rfnt");
  auto c = slurp(dir + "spacegrotesk_700_14.rfnt");
  auto d = slurp(dir + "spacegrotesk_500_17.rfnt");
  auto e = slurp(dir + "spacegrotesk_700_24.rfnt");
  reader::FontSet fonts;
  fonts.load(reader::Role::Meta, a.data(), a.size());
  fonts.load(reader::Role::Label, b.data(), b.size());
  fonts.load(reader::Role::Value, c.data(), c.size());
  fonts.load(reader::Role::Body, d.data(), d.size());
  fonts.load(reader::Role::Title, e.data(), e.size());
  if (!fonts.ready()) {
    std::fprintf(stderr, "font ramp failed to load from %s\n", dir.c_str());
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
  vm.hasCover = false;
  vm.menu = {{"LIBRARY", "12"}, {"SETTINGS", ""}};
  vm.focusedMenuIndex = -1;
  vm.hints = {"READ", "SELECT", "UP", "DOWN"};

  reader::Framebuffer fb(w, h);
  reader::QuietTheme theme;
  theme.renderHome(fb, fonts, vm);
  if (!reader::writePng(fb, argv[2])) return 1;
  std::printf("wrote %s (%dx%d)\n", argv[2], w, h);
  return 0;
}
```

- [ ] **Step 7: Update the firmware**

In `shell/src/main.cpp`, replace the two font includes with the five ramp headers:

```cpp
#include "font_body.h"
#include "font_label.h"
#include "font_meta.h"
#include "font_title.h"
#include "font_value.h"
```

Replace the `QuietTheme theme; if (!theme.loadFonts(...))` block with:

```cpp
  reader::FontSet fonts;
  const bool fontsOk = fonts.load(reader::Role::Meta, kFontMeta, kFontMetaSize) &&
                       fonts.load(reader::Role::Label, kFontLabel, kFontLabelSize) &&
                       fonts.load(reader::Role::Value, kFontValue, kFontValueSize) &&
                       fonts.load(reader::Role::Body, kFontBody, kFontBodySize) &&
                       fonts.load(reader::Role::Title, kFontTitle, kFontTitleSize);
  if (!fontsOk || !fonts.ready()) {
    mark("font-load-FAILED");
    return;
  }
  reader::QuietTheme theme;
  mark("fonts-ok");
```

Add `#include "reader/fontset.h"` to the includes, set `vm.hasCover = false;` alongside the other view-model fields, and change the render call to `theme.renderHome(portrait, fonts, vm);`.

- [ ] **Step 8: Run, inspect, bless both goldens**

```bash
cmake -S . -B build && make test
```

Expected: FAIL twice with `golden mismatch` / `golden missing`, writing `build/home_quiet_candidate.png` and `build/home_quiet_x3_candidate.png`.

Open **both** candidates with an image viewer and compare against the design board. The X4 candidate must show: header band with `NOW READING` and `87%`; a dithered cover panel on the left with `Middlemarch` reversed out of its bottom strip; to its right the title at display size, the author beneath, then `6%` large with `PAGE 53 / 890` and `CH. 01 — MISS BROOKE` in letterspaced small caps; a progress bar; a black `CONTINUE` block with white text and a chevron; `LIBRARY 12` and `SETTINGS` rows; and a hint bar with four icon+label slots. The X3 candidate must show the same composition using its extra 48 px of width — the hint slots spread wider, nothing pinned left, nothing clipped.

Only when both look right:

```bash
cp build/home_quiet_candidate.png test/golden/home_quiet.png
cp build/home_quiet_x3_candidate.png test/golden/home_quiet_x3.png
make test
```

Expected: PASS.

- [ ] **Step 9: Verify the whole toolchain**

```bash
rm -rf build && make test && make sim && make firmware && make compare COMPARE_ARGS="--only home"
```

Expected: tests pass, `build/home.png` written, firmware builds, and the comparison sheet reports `1/1 screens implemented` for Home.

- [ ] **Step 10: Commit**

```bash
git add -A
git commit -m "feat(theme): Home at design fidelity on both panel geometries

Two-column composition with a dithered cover placeholder, the five-role type
ramp, icons in the hint bar, and every horizontal position derived from the
framebuffer so 480x800 and 528x792 both compose correctly."
```

---

### Task 10: Verify on hardware

The simulator cannot answer whether the ramp reads well on e-ink; only the panel can.

**Files:** none — this is a bring-up check.

- [ ] **Step 1: Flash**

```bash
cd /Users/lucasgoudin/dev/encre
~/.platformio/penv/bin/pio run -e xteink -t upload --upload-port /dev/cu.usbmodem1101
```

- [ ] **Step 2: Confirm it reached the end of setup**

```bash
~/.platformio/penv/bin/python tools/serial-log.py --seconds 14 --no-reset
```

Expected: the staged log through `refresh-complete`, then `[alive]` heartbeats. `[detect]` must report the I2C verdict and the active controller.

- [ ] **Step 3: Judge the panel, and record it**

Look at the device and answer three questions in the commit message: does the title now dominate as the design intends; do the letterspaced small-caps labels hold together at 12–13 px on e-ink, or do they need to move up a step; and does the dithered cover read as a tone or as visible dots at reading distance.

If the small sizes prove illegible, that is a real finding — record it in the roadmap rather than silently changing the ramp, because the ramp is now the design's contract.

- [ ] **Step 4: Commit the finding**

```bash
git commit --allow-empty -m "verified: Home type ramp on X3 hardware

<what you actually saw, including anything that needs to change>"
```

---

### Task 11: Phase wrap-up

- [ ] **Step 1: Full clean verification**

```bash
rm -rf build && make test && make sim && make firmware && make compare
```

Expected: all tests pass, sim renders, firmware builds, and the comparison sheet reports Home implemented with 6 screens still to go.

- [ ] **Step 2: Update the roadmap's Phase 2A entry to done, and record anything the hardware check found**

Edit `docs/superpowers/plans/2026-08-20-v1-roadmap.md`: mark 2A complete under the Phase 2 heading, and add any new finding to the bring-up findings section.

- [ ] **Step 3: Commit**

```bash
git add docs && git commit -m "docs: Phase 2A complete"
```

---

## Self-review notes (already applied)

- **Coverage against the roadmap's 2A scope:** text ink (Task 1), tracking (Tasks 1–2), missing-glyph box (Task 3), type ramp (Tasks 4–5), icons (Task 6), dither (Task 7), canvas-agnostic layout (Task 8), Home fidelity plus both goldens (Task 9), hardware verification (Task 10). The `uiScale` hook is satisfied structurally rather than by code: `FontSet` is built by the caller, so a board wanting a different scale selects different assets — no core change. That is deliberate, since both Xteink profiles are 1.0 and inventing a scaling path now would be untested speculation.
- **Deliberately deferred to 2B/2C:** input dispatch, screen stack, focus navigation, refresh policy, power manager, the remaining screens, and rotating directly into the SDK framebuffer to cut peak heap.
- **Type consistency:** `drawText(fb, font, x, baseline, utf8, Ink, tracking)` and `Font::measure(utf8, tracking)` are used with those exact signatures in Tasks 8 and 9. `Theme::renderHome(fb, fonts, vm)` is the signature in `theme.h`, `theme_quiet.h`, the golden test, the simulator and the firmware. `FontSet::load(Role, data, size)` / `ready()` / `operator[]` match everywhere. `drawHeaderBand` / `drawRow` / `drawHintBar` all return consumed height.
- **Known risk:** the icon bitmaps in Task 6 are hand-authored and unreviewed at size; Task 9's golden inspection and Task 10's hardware check are where they get judged. Redrawing them is expected, not a failure.
