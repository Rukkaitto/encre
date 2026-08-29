# Sleep screen covers — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show the cover of the book you are reading on the sleep screen, at four grey levels, decoded on the sleep path and cached on the card.

**Architecture:** Four new `core/` layers — a vendored baseline-JPEG decoder, our own PNG decoder over the existing `inflate_stream.h`, a streaming box-filter-and-error-diffuse fitter, and a `cover.h` that drives them — write a two-plane cache file. `SleepScreen` reads one plane per grayscale pass straight into `Framebuffer::data()`, so painting costs no extra RAM. The decode runs *after* the first sleep paint, reusing the peek's `ReaderScreen::releaseChapter()` for the 36 KB inflate window.

**Tech Stack:** C++20 (`core/`, no Arduino/ESP/host-OS), doctest, TJpgDec (vendored), `stb_image` (desktop-only, already vendored, used as the test oracle), Python for the corpus probe and the board asset.

**Spec:** [`docs/superpowers/specs/2026-08-29-sleep-screen-covers-design.md`](../specs/2026-08-29-sleep-screen-covers-design.md). Issue [#11](https://github.com/Rukkaitto/encre/issues/11).

---

## Two stages, and a gate between them

**Stage 1 (Tasks 1–8) is independently shippable and is the risky half.** It ends with a desktop tool that turns any EPUB into a pair of panel-sized planes, validated against `stb_image` over all 225 corpus books — and with the one number this design turns on measured *on the device*. Nothing user-visible changes.

**Task 9 is a GATE, not a task to complete and forget.** If the measured decode is far above the 2–5 s estimate, stop and revisit the approach before starting Stage 2; the spec names the fallback (the rejected book-open path).

**Stage 2 (Tasks 10–19)** puts it on the screen: boards first, then Settings, then the screen, then the shell.

## Rules that apply to every task in this plan

These are CLAUDE.md's, and this feature touches every one of them:

- **`cmake -S . -B build` after adding or removing ANY source file.** CMake uses `file(GLOB)`; a new file is silently ignored otherwise. Every task that creates a file includes this step.
- **A UI change goes into the design HTML first**, then the implementation. Tasks 10 and 12 are boards; the screens that follow them implement what the board says.
- **Never re-bless a golden to make a test pass.** Inspect the candidate, say what you see, then bless.
- **Prove every new test by mutation** — break the code it defends and confirm the count of failures. **`git commit` BEFORE mutating**, and restore with `cp` from a backup, **never `git checkout`** (that reverts the change under test as well as the mutation).
- **Check `git diff --stat` before every commit.** A large accidental deletion is one line in a diff stat and invisible in a script's success message.
- **A green desktop suite is not evidence for `shell/`.** Tasks 15, 17 and 18 touch `shell/src/main.cpp`, which nothing on the desktop compiles.
- **CROSS-COMPILE AND READ THE ASSEMBLY FOR ANY PER-PIXEL LOOP IN THIS PATH.** Task 4 shipped a review round with a **software 64-bit division per source pixel** — the obvious `(long long)j * dstW / srcW` — which x86-64 answers with a hardware `idiv` at no measurable cost and **RV32IMC answers with a libgcc `__divdi3` call**. At ~2.65 M pixels for a median cover that is 1.7–3.3 s at 160 MHz *in one line*, and no desktop benchmark can see it: the two forms measured 3.19 vs 3.19 ms. This is CLAUDE.md's ratio trap in a new disguise — not "the desktop is N× faster" but "the desktop has an instruction the device does not."

  The check is cheap, so run it on every task that adds a loop over pixels:

  ```bash
  ~/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-g++ \
    -std=gnu++2a -Os -fno-exceptions -Icore/include -Ithird_party \
    -S -o /tmp/x.s core/src/<file>.cpp
  grep -n "call.*__divdi3\|call.*__udivdi3\|call.*__moddi3" /tmp/x.s
  ```

  A hit is not automatically wrong — once per row or once per image is fine. **A hit inside the loop body is the defect**, so find the label the backward branch targets and check whether the call is inside it. As of Task 4's fix, `jpegd.cpp`, `pngd.cpp` and vendored `tjpgd.c` are all at zero.

## File structure

**Created in `core/` (portable, no Arduino/ESP/host-OS):**

| file | responsibility |
|---|---|
| `core/include/reader/jpegd.h`, `core/src/jpegd.cpp` | baseline JPEG → 8-bit grey rows, over a `ByteSource`. Wraps TJpgDec. Knows nothing of scaling or panels. |
| `core/include/reader/pngd.h`, `core/src/pngd.cpp` | PNG ct 0/2/4/6 bd 8 non-interlaced → 8-bit grey scanlines, over `inflate_stream.h`. |
| `core/include/reader/imagefit.h`, `core/src/imagefit.cpp` | box-filter downscale + Floyd–Steinberg to 4 levels → two 1-bit plane rows. Knows no file format. |
| `core/include/reader/cover.h`, `core/src/cover.cpp` | find the cover in an `OpenedBook`, drive the above, emit planes to a sink. Knows no filesystem. |
| `core/include/reader/sleep_cover.h`, `core/src/sleep_cover.cpp` | the cache file's header: layout, serialise, parse, validity against a book and a panel. |

**Vendored:** `third_party/tjpgd.h`, `third_party/tjpgd.c` (+ `third_party/tjpgdcnf.h`).

**Modified in `core/`:** `settings.h`/`settings.cpp` (two fields), `screen_settings.h`/`screen_settings.cpp` (section + rows + derived focusability), `screen_sleep.h`/`screen_sleep.cpp` (`CoverSource`, dynamic fidelity), `viewmodel.h` (`SleepViewModel` mode fields), `theme.h`/`theme_quiet.cpp` (`renderSleep` cover path), `book.h`/`book.cpp` (`OpenedBook::cover`), `library.json` (exclude nothing new — `jpegd.cpp` and `pngd.cpp` ship on device).

**Modified in `shell/`:** `shell/src/main.cpp` (the sleep path, the plane writer, the `CoverSource`).

**Design:** `design/Settings.dc.html` (section), `design/SleepCover.dc.html` and `design/SleepCoverDetails.dc.html` (new), `design/assets/sleep-cover-480x800.png` and `-528x792.png` (generated).

**Tools:** `tools/covers.py` (corpus probe), `sim/main.cpp` (three subcommands + a `cover` conversion mode), `tools/compare-design.py` (register two boards).

**Tests:** `test/unit/test_jpegd.cpp`, `test_pngd.cpp`, `test_imagefit.cpp`, `test_cover.cpp`, `test_sleep_cover.cpp`, `test_theme_sleep_cover_golden.cpp`; modified `test_settings.cpp`, `test_theme_sleep_golden.cpp`, `test_focus_restore.cpp`.

---

# Stage 1 — the imaging pipeline

## Task 1: `ByteSource` fixtures and the test oracle

`stb_image` is already vendored and already desktop-only (`core/library.json` excludes `png.cpp` from the firmware). Tasks 2 and 3 validate our decoders against it, so this task builds the shared harness once rather than twice.

**Files:**
- Create: `test/unit/image_fixtures.h`
- Test: (header only; first used by Task 2)

- [ ] **Step 1: Write the fixture header**

```cpp
#pragma once
// A ByteSource over a std::string, and the stb_image oracle the decoder tests
// compare against.
//
// THE ORACLE IS stb_image, ALREADY VENDORED AND ALREADY DESKTOP-ONLY. This is the
// move that validated inflate_stream against the one-shot decoder -- 216 entry
// passes, zero disagreements -- and it is available here for the same reason: a
// decoder is only trustworthy against a decoder nobody in this repo wrote.
#include <cstdint>
#include <string>
#include <vector>

#include "reader/inflate_stream.h"

namespace imgfix {

// Serves `bytes` in chunks of at most `grain`. GRAIN 1 IS THE LOAD-BEARING CASE:
// a source that satisfies every read hides every resumption bug there is, which
// is exactly what test_inflate_stream.cpp found.
class StringSource : public reader::ByteSource {
 public:
  StringSource(std::string bytes, size_t grain = 4096)
      : bytes_(std::move(bytes)), grain_(grain == 0 ? 1 : grain) {}

  size_t read(void* dst, size_t want) override {
    if (want > grain_) want = grain_;
    const size_t left = bytes_.size() - at_;
    const size_t n = want < left ? want : left;
    if (n != 0) __builtin_memcpy(dst, bytes_.data() + at_, n);
    at_ += n;
    return n;
  }

  void rewind() { at_ = 0; }

 private:
  std::string bytes_;
  size_t grain_;
  size_t at_ = 0;
};

// Decoded by stb_image to 8-bit grey. Empty `pixels` means stb refused it.
struct Oracle {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> pixels;  // width*height, row-major, 0 = black
};

Oracle decodeWithStb(const std::string& bytes);

// Reads a file from test/unit/fixtures/images/. Empty on failure.
std::string loadFixture(const char* name);

}  // namespace imgfix
```

- [ ] **Step 2: Write the fixture implementation**

Create `test/unit/image_fixtures.cpp`:

```cpp
#include "image_fixtures.h"

#include <fstream>
#include <sstream>

// png.cpp already defines STB_IMAGE_IMPLEMENTATION for the desktop build, so this
// translation unit takes the header only -- two implementations would be a
// duplicate-symbol link error.
#include "stb_image.h"

namespace imgfix {

Oracle decodeWithStb(const std::string& bytes) {
  Oracle out;
  int w = 0, h = 0, comp = 0;
  unsigned char* px = stbi_load_from_memory(
      reinterpret_cast<const unsigned char*>(bytes.data()),
      static_cast<int>(bytes.size()), &w, &h, &comp, 1);
  if (px == nullptr) return out;
  out.width = w;
  out.height = h;
  out.pixels.assign(px, px + static_cast<size_t>(w) * h);
  stbi_image_free(px);
  return out;
}

std::string loadFixture(const char* name) {
  std::string path = std::string(TEST_FIXTURE_DIR) + "/images/" + name;
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace imgfix
```

- [ ] **Step 3: Check how `TEST_FIXTURE_DIR` is already provided**

Run: `grep -rn "TEST_FIXTURE_DIR\|GOLDEN_DIR\|add_definitions\|target_compile_definitions" CMakeLists.txt test/unit/golden.h | head`

If `golden.h` gets its directory from a compile definition, follow the identical mechanism for `TEST_FIXTURE_DIR` in `CMakeLists.txt`. If it derives the path some other way, use that way instead — **do not invent a second mechanism**; one table spelled in two build worlds is the manifest defect CLAUDE.md records.

- [ ] **Step 4: Add three fixture images**

```bash
mkdir -p test/unit/fixtures/images
python3 - <<'PY'
import zipfile, re, os, glob
from urllib.parse import unquote
def cover(p):
    z = zipfile.ZipFile(p); names = z.namelist()
    m = re.search(r'full-path="([^"]+)"', z.read('META-INF/container.xml').decode('utf-8','replace'))
    opf = m.group(1); x = z.read(opf).decode('utf-8','replace'); base = os.path.dirname(opf)
    items = {}
    for mi in re.finditer(r'<item\b[^>]*>', x):
        t = mi.group(0)
        i = re.search(r'id="([^"]*)"', t); h = re.search(r'href="([^"]*)"', t)
        pr = re.search(r'properties="([^"]*)"', t)
        if i and h: items[i.group(1)] = (h.group(1), pr.group(1) if pr else '')
    href = None
    mm = re.search(r'<meta\b[^>]*name="cover"[^>]*content="([^"]+)"', x)
    if mm and mm.group(1) in items: href = items[mm.group(1)][0]
    if not href:
        for i,(h,pr) in items.items():
            if 'cover-image' in pr: href = h; break
    return z.read(os.path.normpath(os.path.join(base, unquote(href))).replace('\\','/'))

out = 'test/unit/fixtures/images'
picked = {}
for p in sorted(glob.glob(os.path.expanduser('~/.cache/encre-corpus/*/*.epub'))):
    try: d = cover(p)
    except Exception: continue
    if d[:2] == b'\xff\xd8' and 'baseline.jpg' not in picked and len(d) < 400_000:
        picked['baseline.jpg'] = d
    if d[:8] == b'\x89PNG\r\n\x1a\n' and 'truecolour.png' not in picked and len(d) < 400_000:
        picked['truecolour.png'] = d
    if len(picked) == 2: break
for name, d in picked.items():
    open(os.path.join(out, name), 'wb').write(d)
    print(name, len(d))
PY
```

Then add a deliberately-refused third fixture — a progressive JPEG, which Task 2 must reject rather than mis-decode:

```bash
python3 - <<'PY'
from PIL import Image
im = Image.open('test/unit/fixtures/images/baseline.jpg')
im.save('test/unit/fixtures/images/progressive.jpg', 'JPEG', progressive=True, quality=80)
print('progressive.jpg written')
PY
```

- [ ] **Step 5: Re-run CMake and build**

```bash
cmake -S . -B build && make test
```

Expected: the suite builds and passes exactly as before — nothing consumes the fixtures yet.

- [ ] **Step 6: Commit**

```bash
git add test/unit/image_fixtures.h test/unit/image_fixtures.cpp test/unit/fixtures/images CMakeLists.txt
git diff --cached --stat
git commit -m "test: a ByteSource fixture and the stb_image oracle for the cover decoders"
```

---

## Task 2: `jpegd.h` — baseline JPEG, pushed a row at a time

**Files:**
- Create: `third_party/tjpgd.h`, `third_party/tjpgd.c`, `third_party/tjpgdcnf.h`
- Create: `core/include/reader/jpegd.h`, `core/src/jpegd.cpp`
- Test: `test/unit/test_jpegd.cpp`

**Why vendored rather than written:** the reasons this project wrote its own DEFLATE — stb's one-shot API and 6,608-byte single stack frame — do not apply to TJpgDec, which already has the memory model wanted here (~3.5 KB workspace, an MCU output callback, free ½/¼/⅛ IDCT scaling). JPEG's edge cases are numerous and a wrong upsample is a *subtly wrong picture* rather than a crash, which is the hardest defect for this project's tests to catch.

### THE INTERFACE IS PUSH, NOT PULL, AND THIS IS FORCED BY THE LIBRARY

**Read this before writing any code.** An earlier draft of this plan specified a `nextRow(&row)` pull interface. That cannot be built over TJpgDec without inverting control, and the reason is in its API:

```c
JRESULT jd_prepare(JDEC*, size_t (*infunc)(JDEC*,uint8_t*,size_t), void* pool, size_t sz_pool, void* dev);
JRESULT jd_decomp (JDEC*, int (*outfunc)(JDEC*,void*,JRECT*), uint8_t scale);
```

`jd_decomp` **decodes the whole image in one call** and pushes results through `outfunc`. It does not return until the image is finished or aborted, so there is no point at which a caller can ask for "the next row".

Two consequences, both load-bearing:

1. **The decoder takes a sink.** `decode(src, sink)` calls back per output row. `cover.cpp` supplies a sink that feeds `CoverFitter`, so the whole pipeline is push from the archive to the plane rows.
2. **`outfunc` receives RECTANGLES, not rows.** TJpgDec emits MCU by MCU — typically 16×16 for 4:2:0, which is 177 of the 185 corpus JPEG covers — left to right across a band, then the next band. So a row is not complete until its whole band has arrived, and **`jpegd` must buffer one MCU band**: `mcuHeight * outputWidth` bytes. At the scales this actually uses (output ≥ panel and under 2× panel) that is **8.4–16.9 KB**, which is real and must be counted in the budget. It is why `JpegDecoder::workspaceBytes()` exists.

**The stop predicate comes free:** `outfunc` returning 0 aborts `jd_decomp` with `JDR_INTR`. That is the abort path, not an exception and not a flag checked later.

- [ ] **Step 1: Vendor TJpgDec**

```bash
cd /tmp && curl -sSL -o tjpgd3.zip https://elm-chan.org/fsw/tjpgd/arc/tjpgd3.zip
shasum -a 256 tjpgd3.zip
# expect: 052fe3efbc9a8be29f31597ad009c5b51a4f6905878eb28569e0ab3d46d0c013
mkdir -p tj && unzip -q -o tjpgd3.zip -d tj
cd - && cp /tmp/tj/src/tjpgd.c /tmp/tj/src/tjpgd.h /tmp/tj/src/tjpgdcnf.h third_party/
```

This is **TJpgDec R0.03 (C)ChaN, 2021**. Its licence is: *"No restriction on use... Redistributions of source code must retain the above copyright notice."* **Keep the copyright header intact**, exactly as `third_party/stb_truetype.h` is kept. Add a short note in this repo's style at the top of `tjpgd.c` recording the release, the URL, the sha256 above, and that it is unmodified apart from that note.

Configure `third_party/tjpgdcnf.h` — the stock values are wrong for us in two places:

```c
#define JD_SZBUF      512  /* input buffer; the ByteSource refills it. Stock value. */
#define JD_FORMAT     2    /* CHANGED from 0. 2 = 8-bit grayscale straight out of the
                              decoder, so we never allocate an RGB intermediate --
                              the panel is grey and the fitter wants grey. */
#define JD_USE_SCALE  1    /* Stock. The free 1/2, 1/4, 1/8 out of the IDCT, which is
                              what makes a 2.94 MP cover affordable at all. */
#define JD_TBLCLIP    1    /* Stock. ~1 KB of flash for faster saturation. */
#define JD_FASTDECODE 1    /* CHANGED from 0. 1 uses the 32-bit barrel shifter, which
                              the RISC-V C3 has. 2 wants 6 << HUFF_BIT bytes of extra
                              RAM and this path has none to spare. */
```

- [ ] **Step 2: Write the failing test**

Create `test/unit/test_jpegd.cpp`:

```cpp
// The JPEG decoder, against stb_image over a real cover.
//
// TWO GRAINS, and grain 1 is the load-bearing one: TJpgDec pulls through our
// ByteSource, so a source that satisfies every read hides every refill bug there
// is. test_inflate_stream.cpp found exactly that class of defect this way.
#include <string>
#include <vector>

#include "doctest.h"
#include "image_fixtures.h"
#include "reader/jpegd.h"

namespace {

// Accumulates every row. A TEST may hold the whole image; the FIRMWARE may not,
// which is exactly why the decoder pushes rows instead of returning a buffer.
struct CollectingSink : reader::ImageRowSink {
  int width = 0, height = 0, rows = 0;
  int stopAfter = -1;  // -1 never stops
  std::vector<uint8_t> px;

  bool begin(int w, int h) override { width = w; height = h; return true; }
  bool row(const uint8_t* p) override {
    px.insert(px.end(), p, p + width);
    ++rows;
    return stopAfter < 0 || rows < stopAfter;
  }
};

}  // namespace

TEST_CASE("JpegDecoder matches stb_image on a real baseline cover") {
  const std::string bytes = imgfix::loadFixture("baseline.jpg");
  REQUIRE(!bytes.empty());

  const imgfix::Oracle want = imgfix::decodeWithStb(bytes);
  REQUIRE(!want.pixels.empty());

  imgfix::StringSource src(bytes, 4096);
  CollectingSink sink;
  reader::JpegDecoder dec;
  REQUIRE(dec.decode(src, sink));           // full scale: no atLeast given
  REQUIRE(sink.width == want.width);
  REQUIRE(sink.height == want.height);
  REQUIRE(sink.rows == want.height);
  REQUIRE(sink.px.size() == want.pixels.size());

  // NOT byte-identical, and it must not be asserted as such: TJpgDec and stb use
  // different IDCT rounding and different YCbCr->grey coefficients. What is
  // asserted is that no pixel is far off, which catches a wrong upsample, a
  // transposed block or an off-by-one row -- the defects that actually happen --
  // while tolerating arithmetic that is legitimately not bit-equal.
  long worst = 0, sum = 0;
  for (size_t i = 0; i < sink.px.size(); ++i) {
    const long d = std::abs(static_cast<long>(sink.px[i]) -
                            static_cast<long>(want.pixels[i]));
    if (d > worst) worst = d;
    sum += d;
  }
  CHECK(worst <= 24);
  CHECK(static_cast<double>(sum) / static_cast<double>(sink.px.size()) <= 2.0);
}

TEST_CASE("JpegDecoder is unaffected by how the source chunks its bytes") {
  const std::string bytes = imgfix::loadFixture("baseline.jpg");
  REQUIRE(!bytes.empty());

  imgfix::StringSource big(bytes, 4096);
  imgfix::StringSource one(bytes, 1);
  CollectingSink a, b;
  reader::JpegDecoder d1, d2;
  REQUIRE(d1.decode(big, a));
  REQUIRE(d2.decode(one, b));
  CHECK(a.width == b.width);
  CHECK(a.rows == b.rows);
  CHECK(a.px == b.px);
}

TEST_CASE("JpegDecoder scales down but never below what the caller asked for") {
  // The free IDCT scaling is what makes a 2.94 MP cover affordable. A cover is
  // ~1400x2100 and a panel ~480x800, so 1/2 is chosen: 700x1050, a quarter of the
  // work and still above the panel. Scaling to 1/4 would be 350x525 -- BELOW the
  // panel, and upscaling a cover is not something this pipeline does.
  const std::string bytes = imgfix::loadFixture("baseline.jpg");
  REQUIRE(!bytes.empty());

  imgfix::StringSource src(bytes);
  CollectingSink sink;
  reader::JpegDecoder dec;
  REQUIRE(dec.decode(src, sink, 480, 800));
  CHECK(sink.width >= 480);
  CHECK(sink.height >= 800);
  // And it really did scale, rather than ignoring the hint.
  const imgfix::Oracle full = imgfix::decodeWithStb(bytes);
  CHECK(sink.width < full.width);
}

TEST_CASE("a sink that says stop aborts the decode") {
  // This is the interruption path the shell uses to get out of the way of a button
  // press, and it is TJpgDec's own: outfunc returning 0 aborts with JDR_INTR.
  const std::string bytes = imgfix::loadFixture("baseline.jpg");
  REQUIRE(!bytes.empty());

  imgfix::StringSource src(bytes);
  CollectingSink sink;
  sink.stopAfter = 40;
  reader::JpegDecoder dec;
  CHECK_FALSE(dec.decode(src, sink));
  CHECK(dec.aborted());
  CHECK(sink.rows < sink.height);
}

TEST_CASE("JpegDecoder refuses a progressive JPEG rather than mis-decoding it") {
  const std::string bytes = imgfix::loadFixture("progressive.jpg");
  REQUIRE(!bytes.empty());
  imgfix::StringSource src(bytes);
  CollectingSink sink;
  reader::JpegDecoder dec;
  CHECK_FALSE(dec.decode(src, sink));
  CHECK_FALSE(dec.aborted());   // refused, not interrupted -- a different outcome
  CHECK(sink.rows == 0);
  // A refusal must SAY something -- every refusal on this path ends up in a log
  // line the user's card can be diagnosed from.
  CHECK(dec.reason() != nullptr);
  CHECK(std::string(dec.reason()).size() > 0);
}
```

- [ ] **Step 3: Run the test to verify it fails**

```bash
cmake -S . -B build && make test 2>&1 | tail -20
```

Expected: a compile error — `reader/jpegd.h` does not exist.

- [ ] **Step 4: Write `core/include/reader/jpegd.h`**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

#include "reader/inflate_stream.h"  // ByteSource

namespace reader {

// WHERE DECODED IMAGE ROWS GO.
//
// A sink, because the decoder cannot hand rows back on request: TJpgDec's
// jd_decomp() decodes the whole image in ONE call and pushes MCU rectangles
// through a callback, so there is no point at which a caller could ask for the
// next row. Everything downstream (CoverFitter, the plane sink) is push for the
// same reason, and the whole pipeline is one direction from the archive to the
// card.
class ImageRowSink {
 public:
  virtual ~ImageRowSink() = default;
  // Once, before any row, with the OUTPUT dimensions after any scaling.
  virtual bool begin(int width, int height) = 0;
  // One row of `width` bytes of grey, 0 = black, in top-to-bottom order. Return
  // false to stop the decode -- this is the interruption path, and for JPEG it is
  // TJpgDec's own (outfunc returning 0 aborts with JDR_INTR).
  virtual bool row(const uint8_t* px) = 0;
};

// BASELINE JPEG, PUSHED ONE ROW AT A TIME.
//
// A cover is 2.94 MP at the corpus median, which is 2.9 MB decoded to 8-bit grey
// against a 42 KB reading floor and no PSRAM -- so the whole picture is never in
// memory at any instant.
//
// WRAPS TJpgDec R0.03, VENDORED. The reasons this project wrote its own DEFLATE --
// stb's one-shot API and its 6,608-byte single stack frame -- do not apply:
// TJpgDec already has the memory model wanted here. What this class adds is a
// ByteSource front end, rows instead of MCU rectangles, and a refusal that carries
// a reason.
//
// PROGRESSIVE JPEG IS REFUSED, not approximated. Measured: 2 of 225 corpus books,
// but 2 of the user's own 16. A refusal falls back to the reading card and logs
// why; a mis-decode would put garbage on the glass for hours.
class JpegDecoder {
 public:
  JpegDecoder();
  ~JpegDecoder();
  JpegDecoder(const JpegDecoder&) = delete;
  JpegDecoder& operator=(const JpegDecoder&) = delete;

  // Decode `src` into `sink`. False for a refusal (reason() says why) OR for an
  // abort the sink asked for -- ask aborted() which, because they mean different
  // things to the caller: a refusal is permanent for this book, an abort is not.
  //
  // `atLeastW`/`atLeastH` are the smallest output the caller can use; TJpgDec
  // halves out of the IDCT for free, so a 1400x2100 cover asked for 480x800 is
  // decoded at 1/2 -- a quarter of the work. Never scales BELOW the request. 0 for
  // both means full scale.
  bool decode(ByteSource& src, ImageRowSink& sink, int atLeastW = 0, int atLeastH = 0);

  // What the file said, before scaling. Valid once decode() has read the headers,
  // including on a refusal that happened after them.
  int sourceWidth() const;
  int sourceHeight() const;
  // 1, 2, 4 or 8 -- the divisor decode() chose.
  int scaleDivisor() const;

  // Whether the last decode() stopped because the SINK said so, as opposed to
  // failing. Never both.
  bool aborted() const;
  // Null until something fails. A sentence, for a log line.
  const char* reason() const;

  // Heap held during a decode: TJpgDec's pool plus the MCU band buffer.
  //
  // REPORTED RATHER THAN DOCUMENTED, so the figure in the spec's budget cannot
  // drift from the object. The band is the part that surprises: TJpgDec emits
  // rectangles, typically 16x16 at 4:2:0, so a row is not complete until its whole
  // band has arrived and one band must be held -- mcuHeight * outputWidth, which
  // is 8.4-16.9 KB at the scales this actually uses. Valid after decode() has read
  // the headers; 0 before.
  size_t workspaceBytes() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace reader
```

- [ ] **Step 5: Write `core/src/jpegd.cpp`**

`Impl` holds the `JDEC`, the TJpgDec work pool, the MCU band buffer, the `ByteSource*`, the `ImageRowSink*`, and the abort/reason state. Traps, each of which is real:

- **The input callback is `size_t infunc(JDEC*, uint8_t* buff, size_t nbyte)`, and `buff == nullptr` means SKIP, not read.** Consume `nbyte` from the source and discard; returning 0 there makes TJpgDec think the stream ended.
- **`jd_prepare` reports progressive and arithmetic streams as `JDR_FMT3`** ("not supported"). Map every `JRESULT` to a distinct `reason()` sentence rather than one generic string — the log line is how a user's card gets diagnosed.
- **Allocate the pool and the band with `new (std::nothrow)`** and answer `false`. `-fno-exceptions` makes a throwing `new` an `abort()` with no diagnostic; CLAUDE.md records that reaching the device twice as "opening a book goes back to Home".
- **The band buffer cannot be sized before `jd_prepare`**, because it needs `jd->msx`/`msy` (the sampling factors) and the chosen scale. Allocate it between `jd_prepare` and `jd_decomp`.
- **`outfunc` writes its rect into the band at `(rect->left, rect->top % bandHeight)`**, and when the band's last rectangle has arrived, emits each of its rows to the sink. **The bottom band is short** — TJpgDec clips rectangles at the right and bottom edges — so emit `min(bandHeight, height - bandTop)` rows.
- **Choose the scale in `decode`**, after `jd_prepare` has given you `jd->width`/`jd->height`: the largest divisor in {1,2,4,8} such that `width/n >= atLeastW && height/n >= atLeastH`. With both 0, use 1.
- **`JD_FORMAT 2` gives grey directly** — do not add an RGB path.

- [ ] **Step 6: Run the tests to verify they pass**

```bash
cmake -S . -B build && make test 2>&1 | tail -20
```

Expected: PASS, all five `JpegDecoder` cases.

- [ ] **Step 7: Prove the tests bite (mutation)**

```bash
git add -A && git commit -m "wip: jpegd before mutation" && cp core/src/jpegd.cpp /tmp/jpegd.bak
```

Mutate one at a time, rebuild, record the failure count, then restore with `cp /tmp/jpegd.bak core/src/jpegd.cpp && touch core/src/jpegd.cpp`:

1. Drop the progressive refusal → the refusal test must fail.
2. Emit band rows bottom-to-top within the band → the oracle comparison must fail.
3. Return 0 from the input callback on a skip request → the grain-1 test must fail.
4. Ignore the sink's false return in `outfunc` → the abort test must fail.
5. Always use divisor 1 → the scaling test must fail.

**If a mutation fails nothing, it is telling you about your INPUT before it tells you about your test.** Fix the fixture, not the assertion.

- [ ] **Step 8: Commit**

```bash
git add third_party/tjpgd.h third_party/tjpgd.c third_party/tjpgdcnf.h \
        core/include/reader/jpegd.h core/src/jpegd.cpp test/unit/test_jpegd.cpp
git diff --cached --stat
git commit -m "core: baseline JPEG pushed a row at a time, over a ByteSource"
```

---


## Task 3: `pngd.h` — PNG over our own `inflate_stream.h`

**Files:**
- Create: `core/include/reader/pngd.h`, `core/src/pngd.cpp`
- Test: `test/unit/test_pngd.cpp`

**Why ours rather than a second vendored decoder:** a PNG is DEFLATE plus per-row unfiltering, and we already own the hard half. Measured: every one of the 39 corpus PNGs is colour type 2, bit depth 8, non-interlaced.

**Same push interface as `JpegDecoder`, and deliberately so.** PNG *could* be pull — scanlines come out in order — but `cover.cpp` must drive both formats through one code path, and two shapes would mean two drivers and two chances to get the fitter's feeding wrong.

**FIRST STEP OF THIS TASK: move `ImageRowSink` out of `jpegd.h` into its own `core/include/reader/image_sink.h`, and have `jpegd.h` include that.** Task 2 correctly left it in `jpegd.h` — it had one consumer, and a shared home for a single caller is a header edge bought for nothing, which this project has a rule about. This task creates the second consumer, and **the second copy is the extraction point, not the fifth**. Leaving it where it is would mean `pngd.h` includes `jpegd.h` for a base class, so a PNG-only build drags in the JPEG decoder's declaration and the two decoders are coupled through nothing but an accident of which one was written first.

That move is a pure rename-and-reinclude: `jpegd.h` keeps compiling for its existing includers because it includes the new header, and `test_jpegd.cpp` needs no change. Do it first, confirm `make test` is still green, and commit it separately from the PNG work so the extraction is legible in the history.

- [ ] **Step 1: Write the failing test**

Create `test/unit/test_pngd.cpp`:

```cpp
// PNG, against stb_image over a real cover.
//
// Unlike JPEG this one IS byte-exact: PNG is lossless and the only arithmetic is
// the grey weighting, which is fixed to stb's own coefficients precisely so this
// assertion can be an equality rather than a tolerance.
#include <string>
#include <vector>

#include "doctest.h"
#include "image_fixtures.h"
#include "reader/pngd.h"

namespace {

struct CollectingSink : reader::ImageRowSink {
  int width = 0, height = 0, rows = 0;
  int stopAfter = -1;
  std::vector<uint8_t> px;

  bool begin(int w, int h) override { width = w; height = h; return true; }
  bool row(const uint8_t* p) override {
    px.insert(px.end(), p, p + width);
    ++rows;
    return stopAfter < 0 || rows < stopAfter;
  }
};

}  // namespace

TEST_CASE("PngDecoder matches stb_image byte for byte on a real cover") {
  const std::string bytes = imgfix::loadFixture("truecolour.png");
  REQUIRE(!bytes.empty());

  const imgfix::Oracle want = imgfix::decodeWithStb(bytes);
  REQUIRE(!want.pixels.empty());

  imgfix::StringSource src(bytes, 4096);
  CollectingSink sink;
  reader::PngDecoder dec;
  REQUIRE(dec.decode(src, sink));
  CHECK(sink.width == want.width);
  CHECK(sink.height == want.height);
  CHECK(sink.rows == want.height);
  CHECK(sink.px == want.pixels);
}

TEST_CASE("PngDecoder is unaffected by how the source chunks its bytes") {
  const std::string bytes = imgfix::loadFixture("truecolour.png");
  REQUIRE(!bytes.empty());

  imgfix::StringSource big(bytes, 4096);
  imgfix::StringSource one(bytes, 1);
  CollectingSink a, b;
  reader::PngDecoder d1, d2;
  REQUIRE(d1.decode(big, a));
  REQUIRE(d2.decode(one, b));
  CHECK(a.px == b.px);
  CHECK(a.rows == b.rows);
}

TEST_CASE("a sink that says stop aborts the decode") {
  const std::string bytes = imgfix::loadFixture("truecolour.png");
  REQUIRE(!bytes.empty());
  imgfix::StringSource src(bytes);
  CollectingSink sink;
  sink.stopAfter = 20;
  reader::PngDecoder dec;
  CHECK_FALSE(dec.decode(src, sink));
  CHECK(dec.aborted());
  CHECK(sink.rows < sink.height);
}

TEST_CASE("PngDecoder refuses what it does not implement, with a reason") {
  // An interlaced PNG: 0 of 225 corpus covers use it, and a decoder that silently
  // produced a seventh of the picture would be worse than one that declines.
  std::string bytes = imgfix::loadFixture("truecolour.png");
  REQUIRE(bytes.size() > 32);
  bytes[28] = 1;  // IHDR interlace method -- now Adam7

  imgfix::StringSource src(bytes);
  CollectingSink sink;
  reader::PngDecoder dec;
  CHECK_FALSE(dec.decode(src, sink));
  CHECK_FALSE(dec.aborted());
  CHECK(sink.rows == 0);
  CHECK(dec.reason() != nullptr);
}

TEST_CASE("a palette PNG is refused, not rendered as noise") {
  std::string bytes = imgfix::loadFixture("truecolour.png");
  REQUIRE(bytes.size() > 32);
  bytes[25] = 3;  // IHDR colour type -- palette, which we do not implement

  imgfix::StringSource src(bytes);
  CollectingSink sink;
  reader::PngDecoder dec;
  CHECK_FALSE(dec.decode(src, sink));
  CHECK(dec.reason() != nullptr);
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake -S . -B build && make test 2>&1 | tail -20
```

Expected: compile error, `reader/pngd.h` missing.

- [ ] **Step 3: Write `core/include/reader/pngd.h`**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

#include "reader/inflate_stream.h"
#include "reader/image_sink.h"  // ImageRowSink -- one sink shape for both formats

namespace reader {

// PNG, PUSHED ONE SCANLINE AT A TIME, over this project's own Inflater.
//
// WHY OURS AND NOT A SECOND VENDORED DECODER: a PNG is DEFLATE plus per-row
// unfiltering, and inflate_stream.h is already the hard half -- bounded memory, a
// 32 KB window, no giant stack frame. Vendoring a second decoder to get ~200 lines
// of unfiltering would be the worse trade, which is the mirror of jpegd.h's
// argument for vendoring THERE.
//
// THE PUSH INTERFACE IS NOT FORCED HERE THE WAY IT IS FOR JPEG -- scanlines come
// out in order and a pull form would be natural. It is push anyway so cover.cpp
// drives both formats through ONE path: two shapes would be two drivers feeding
// the fitter, and two chances to get that feeding wrong.
//
// WHAT IT SUPPORTS, AND THE MEASUREMENT BEHIND IT: colour types 0/2/4/6 at bit
// depth 8, non-interlaced. All 39 PNG covers across 225 corpus books are colour
// type 2, bit depth 8, non-interlaced -- 0 interlaced, 0 palette. Types 0/4/6 come
// free with the same unfilter and are accepted; PALETTE (3), bit depths other than
// 8, and INTERLACED are REFUSED with a reason rather than approximated.
//
// A NOTE ON HEAP, because it decides where this may run: an Inflater is 36,956
// bytes. 38 of the 39 corpus PNGs are STORED inside the zip (method 0), so the
// common case needs exactly this one window. The single deflated PNG needs the
// zip's window too and may refuse at the reading floor -- a stated limit.
class PngDecoder {
 public:
  PngDecoder();
  ~PngDecoder();
  PngDecoder(const PngDecoder&) = delete;
  PngDecoder& operator=(const PngDecoder&) = delete;

  // False for a refusal (reason() says why) OR for an abort the sink asked for --
  // ask aborted() which. Same contract as JpegDecoder::decode, deliberately.
  bool decode(ByteSource& src, ImageRowSink& sink);

  bool aborted() const;
  const char* reason() const;

  // The inflate window plus the two row buffers. Reported, not documented, so the
  // spec's budget cannot drift from the object.
  size_t workspaceBytes() const;

  // Grey from RGB with stb_image's own coefficients, so test_pngd.cpp can assert
  // EQUALITY against the oracle rather than a tolerance. Stated here because the
  // choice is load-bearing for the test, not because the number is interesting.
  static uint8_t greyOf(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
  }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace reader
```

- [ ] **Step 4: Write `core/src/pngd.cpp`**

Implementation notes, each earned:

- **Chunk walk:** signature, IHDR, then stream the IDAT payloads into an `Inflater` through a `ByteSource` adapter that concatenates consecutive IDATs and stops at IEND. Skip ancillary chunks by length.
- **Two row buffers**, current and previous, each `width * channels` bytes — the unfilter needs the row above. Worst corpus PNG is 1600 wide × 4 channels = 6,400 B a row, 12,800 B for the pair.
- **Filters 0–4** (None/Sub/Up/Average/Paeth) per the PNG spec, applied to the raw channel bytes *before* grey conversion.
- **`new (std::nothrow)`** for both row buffers and the inflater, answering `false`. Never `abort()`.
- **Do not verify CRCs.** Stated deliberately: a CRC failure on a cover should cost the cover, not the book, and structural nonsense is already refused. The two mutation tests above change IHDR bytes *without* fixing the CRC and must still be refused **by the interlace and colour-type checks**, which is what proves those refusals are structural rather than incidental.

- [ ] **Step 5: Run to verify it passes**

```bash
cmake -S . -B build && make test 2>&1 | tail -20
```

Expected: PASS.

- [ ] **Step 6: Prove the tests bite (mutation)**

```bash
git add -A && git commit -m "wip: pngd before mutation" && cp core/src/pngd.cpp /tmp/pngd.bak
```

1. Replace the Paeth predictor with `a` (left) → byte-exactness must fail. **If it does not, your fixture has no Paeth rows** — pick a different corpus PNG rather than weakening the assertion.
2. Drop the interlace refusal → that refusal test must fail.
3. Reuse one row buffer for current and previous → the Up/Average/Paeth rows must fail.
4. Ignore the sink's false return → the abort test must fail.

Restore with `cp /tmp/pngd.bak core/src/pngd.cpp && touch core/src/pngd.cpp` each time.

- [ ] **Step 7: Commit**

```bash
git add core/include/reader/pngd.h core/src/pngd.cpp test/unit/test_pngd.cpp
git diff --cached --stat
git commit -m "core: PNG scanlines over our own inflate_stream, refusing what it does not implement"
```

---


## Task 4: `imagefit.h` — streaming downscale and diffuse to four levels

**Files:**
- Create: `core/include/reader/imagefit.h`, `core/src/imagefit.cpp`
- Test: `test/unit/test_imagefit.cpp`

This is where `FILL` vs `WHOLE` lives, and where two 1-bit planes come out.

- [ ] **Step 1: Write the header**

```cpp
#pragma once
#include <cstdint>
#include <vector>

namespace reader {

// HOW A COVER IS FITTED TO A PANEL.
//
// Fill crops to the panel; Whole letterboxes and the caller tints the bands. The
// measurement behind offering both: 163 of 224 corpus covers are 2:3, so on the X3
// (528x792, 2:3 exactly) Fill loses NOTHING for 73% of books and this is a no-op.
// On the X4 (480x800) the median 2:3 cover loses 10.0% of its height. The tail is
// what earns the setting -- the squarest corpus cover is 877x973 and Fill cuts its
// title off at both edges, a 33.4% loss.
enum class CoverFit { Fill, Whole };

// The rectangle the cover occupies inside the panel, and which source rectangle
// maps onto it. Whole leaves bands; Fill leaves none and crops the source.
struct FitBox {
  int dstX = 0, dstY = 0, dstW = 0, dstH = 0;
  int srcX = 0, srcY = 0, srcW = 0, srcH = 0;
};

// Pure arithmetic, so it is testable without an image. Vertical centring for Fill
// is at 0.4 rather than 0.5 FOR THE COVERS WHERE FILL CROPS HEIGHT: a cover's title
// band sits low, and a centred crop takes from it. Measured on real covers, not
// chosen for symmetry -- and measured again as to how often it fires, which is 2 of
// 225 books on the X4 and 18 on the X3, because most covers are RELATIVELY WIDER
// than the panel and lose width instead.
FitBox fitCover(int srcW, int srcH, int panelW, int panelH, CoverFit fit);

// TURNS SOURCE ROWS INTO TWO 1-BIT PLANE ROWS, IN ORDER, HOLDING NEITHER IMAGE.
//
// Box-filter down (accumulate whole source rows into the destination row they land
// in) then Floyd-Steinberg to four levels, diffusing into a single carried error
// row. That is the whole reason this streams: FS needs the NEXT row's error and
// nothing more, so one row of state serves an image of any height.
//
// TWO PLANES, NOT A 2 BPP IMAGE, and the reason is what makes four levels
// affordable at all: Plane::Bw inks where coverage >= 2, which is exactly "MSB
// set", so the Bw base pass and the Msb pass read the SAME plane. Two planes serve
// three passes, and each pass is one file read into Framebuffer::data().
//
// LEVELS ARE COVERAGE 0..3, mapped to (msb, lsb) as the framebuffer's planes are:
// level 0 is paper, level 3 is full ink.
class CoverFitter {
 public:
  // `panelW` must be the PHYSICAL row width in pixels; plane rows come out
  // (panelW + 7) / 8 bytes. False if the geometry is non-positive or a buffer
  // could not be allocated -- never an abort.
  bool begin(int srcW, int srcH, int panelW, int panelH, CoverFit fit);

  const FitBox& box() const { return box_; }

  // Feed source rows IN ORDER, every row of the source, exactly once. Rows outside
  // box().srcY..srcY+srcH are consumed and discarded, which is what lets the
  // caller stream the whole JPEG without knowing about the crop.
  //
  // When a destination row completes, `msb` and `lsb` are filled and true is
  // returned in `emitted`. A source row can complete at most one destination row,
  // because the destination is never taller than the source.
  bool addRow(const uint8_t* src, bool& emitted);

  // Valid after addRow set `emitted`. (panelW + 7) / 8 bytes each.
  const uint8_t* msbRow() const;
  const uint8_t* lsbRow() const;
  // Which destination row was just emitted, counting from box().dstY.
  int emittedRow() const;

  // Destination rows emitted so far. Equal to box().dstH when the source is spent.
  int rowsEmitted() const;

 private:
  FitBox box_;
  int panelW_ = 0, panelH_ = 0, planeBytes_ = 0;
  int srcRow_ = 0, dstRow_ = 0;
  std::vector<uint32_t> acc_;    // per destination column: summed grey
  std::vector<uint16_t> count_;  // per destination column: source pixels summed
  std::vector<int16_t> err_;     // the carried Floyd-Steinberg error row
  std::vector<uint8_t> msb_, lsb_;
};

}  // namespace reader
```

- [ ] **Step 2: Write the failing test**

Create `test/unit/test_imagefit.cpp`. **The strong test is a reference implementation**, exactly as `test_dither.cpp` and `test_framebuffer.cpp` are built:

```cpp
// The streaming fitter against a NAIVE WHOLE-IMAGE reference.
//
// This is the shape test_dither.cpp and test_framebuffer.cpp use, and it is the
// only one that can catch the defect that matters here: a streaming downscale and
// diffusion is easy to get subtly wrong -- a dropped error term, a row phase off
// by one -- in a way that still produces a plausible picture. A reference that
// holds the whole image is trivial to write and obviously correct, and byte
// identity against it is a real assertion.
#include <vector>

#include "doctest.h"
#include "reader/imagefit.h"

namespace {

// A deterministic pseudo-image with real gradients, so the diffusion has something
// to diffuse. A flat field would make every mutation below invisible.
std::vector<uint8_t> ramp(int w, int h) {
  std::vector<uint8_t> px(static_cast<size_t>(w) * h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      px[static_cast<size_t>(y) * w + x] =
          static_cast<uint8_t>((x * 255 / (w - 1) + y * 255 / (h - 1)) / 2);
  return px;
}

struct Planes {
  std::vector<uint8_t> msb, lsb;
  int rows = 0, bytes = 0;
};

// The reference: whole image in memory, box filter then Floyd-Steinberg, no
// streaming. Deliberately written the obvious way.
Planes reference(const std::vector<uint8_t>& src, int sw, int sh, int pw, int ph,
                 reader::CoverFit fit);

Planes streamed(const std::vector<uint8_t>& src, int sw, int sh, int pw, int ph,
                reader::CoverFit fit) {
  reader::CoverFitter f;
  REQUIRE(f.begin(sw, sh, pw, ph, fit));
  Planes out;
  out.bytes = (pw + 7) / 8;
  for (int y = 0; y < sh; ++y) {
    bool emitted = false;
    REQUIRE(f.addRow(src.data() + static_cast<size_t>(y) * sw, emitted));
    if (!emitted) continue;
    out.msb.insert(out.msb.end(), f.msbRow(), f.msbRow() + out.bytes);
    out.lsb.insert(out.lsb.end(), f.lsbRow(), f.lsbRow() + out.bytes);
    ++out.rows;
  }
  return out;
}

}  // namespace

TEST_CASE("CoverFitter streams to exactly what the whole-image reference produces") {
  // Both panels, both fits, and source shapes that are NOT integer multiples of the
  // destination -- an exact multiple would hide every rounding bug in the box filter.
  const int panels[2][2] = {{480, 800}, {528, 792}};
  const int sources[4][2] = {{1400, 2100}, {877, 973}, {601, 1000}, {1600, 2400}};

  for (const auto& p : panels) {
    for (const auto& s : sources) {
      for (const reader::CoverFit fit : {reader::CoverFit::Fill, reader::CoverFit::Whole}) {
        CAPTURE(p[0]); CAPTURE(p[1]); CAPTURE(s[0]); CAPTURE(s[1]);
        const std::vector<uint8_t> px = ramp(s[0], s[1]);
        const Planes want = reference(px, s[0], s[1], p[0], p[1], fit);
        const Planes got = streamed(px, s[0], s[1], p[0], p[1], fit);
        REQUIRE(got.rows == want.rows);
        CHECK(got.msb == want.msb);
        CHECK(got.lsb == want.lsb);
      }
    }
  }
}

TEST_CASE("fitCover crops for Fill and letterboxes for Whole") {
  // X3 is 2:3 exactly, so a 2:3 cover is untouched by either fit. This is the
  // measurement that says the setting is a no-op for 73% of books on the dev device.
  const reader::FitBox x3 = reader::fitCover(1400, 2100, 528, 792, reader::CoverFit::Fill);
  CHECK(x3.dstW == 528);
  CHECK(x3.dstH == 792);
  CHECK(x3.srcW == 1400);
  CHECK(x3.srcH == 2100);

  // X4 is 3:5 = 0.600 and the cover is 0.667, so the cover is RELATIVELY WIDER than
  // the panel. Fill therefore crops WIDTH and keeps the full height; Whole fills the
  // width and leaves bands ABOVE AND BELOW.
  //
  // THIS PLAN HAD BOTH AXES BACKWARDS UNTIL 2026-08-29, and why it was not obvious is
  // worth keeping: the loss is 10.0% either way, because 0.600/0.667 is 0.9 whichever
  // ratio you divide by. The NUMBER in the spec's table was right and the AXIS in its
  // prose was wrong, so nothing in the documents disagreed with anything.
  const reader::FitBox fill = reader::fitCover(1400, 2100, 480, 800, reader::CoverFit::Fill);
  CHECK(fill.dstW == 480);
  CHECK(fill.dstH == 800);
  CHECK(fill.srcW < 1400);   // width is the cropped axis
  CHECK(fill.srcH == 2100);  // height is used whole

  const reader::FitBox whole = reader::fitCover(1400, 2100, 480, 800, reader::CoverFit::Whole);
  CHECK(whole.srcW == 1400);
  CHECK(whole.srcH == 2100);
  CHECK(whole.dstW == 480);
  CHECK(whole.dstH < 800);
  CHECK(whole.dstY > 0);  // centred, so there is a band above and below
}

TEST_CASE("every emitted level is 0..3 and the two planes agree on it") {
  const std::vector<uint8_t> px = ramp(1400, 2100);
  reader::CoverFitter f;
  REQUIRE(f.begin(1400, 2100, 480, 800, reader::CoverFit::Fill));
  int rows = 0;
  for (int y = 0; y < 2100; ++y) {
    bool emitted = false;
    REQUIRE(f.addRow(px.data() + static_cast<size_t>(y) * 1400, emitted));
    if (emitted) ++rows;
  }
  CHECK(rows == 800);
  CHECK(f.rowsEmitted() == 800);
}
```

Write `reference()` in the same file, holding the whole image: box-filter into a `dstW * dstH` grey buffer, then Floyd–Steinberg to 4 levels across the whole buffer, then pack to planes.

- [ ] **Step 3: Run to verify it fails**

```bash
cmake -S . -B build && make test 2>&1 | tail -20
```

Expected: compile error, `reader/imagefit.h` missing.

- [ ] **Step 4: Write `core/src/imagefit.cpp`**

Two things that will be got wrong if they are not stated:

- **The plane bit order must match `Framebuffer`'s physical store.** Read `core/src/framebuffer.cpp`'s `bitMask()` and pack the same way. Getting it wrong is a horizontally mirrored cover in 8-pixel groups — visible instantly on glass and invisible to the arithmetic tests above, which compare our packing to our own reference. **Task 13's golden is what actually pins this**, so do not treat this task's green suite as proof.
- **The error row is carried across destination rows only.** A source row that does not complete a destination row contributes to the accumulator and nothing else.

- [ ] **Step 5: Run to verify it passes**

```bash
cmake -S . -B build && make test 2>&1 | tail -20
```

Expected: PASS.

- [ ] **Step 6: Prove the test bites (mutation)**

```bash
git add -A && git commit -m "wip: imagefit before mutation" && cp core/src/imagefit.cpp /tmp/imagefit.bak
```

1. Drop the `1/16` diagonal error term → byte identity must fail.
2. Reset the error row between destination rows → must fail.
3. Round the box filter down instead of to nearest → must fail.
4. Use `0.5` vertical centring for `Fill` → the `fitCover` case's `srcY` changes; add an explicit `srcY` assertion if nothing catches it.

Restore with `cp` and `touch` between each.

- [ ] **Step 7: Commit**

```bash
git add core/include/reader/imagefit.h core/src/imagefit.cpp test/unit/test_imagefit.cpp
git diff --cached --stat
git commit -m "core: stream a cover down to two 1-bit planes at four grey levels"
```

---

## Task 5: `OpenedBook` learns where its cover is

**Files:**
- Modify: `core/include/reader/book.h`, `core/src/book.cpp`
- Modify: `core/src/epub.cpp` (note the cover during the OPF walk)
- Test: `test/unit/test_book.cpp` (extend)

**Why during the OPF walk:** `Epub` already resolves every manifest href, so finding the cover later would mean re-parsing the OPF — ~100 ms and ~32 KB of transient. This is the identical argument `Epub` already makes for noting the NCX.

- [ ] **Step 1: Write the failing test**

Add to `test/unit/test_book.cpp`:

```cpp
TEST_CASE("openBook records where the cover is, by both OPF routes") {
  // Route 1: <meta name="cover" content="id">, the EPUB 2 convention -- and the
  // one the corpus overwhelmingly uses.
  // Route 2: a manifest item with properties="cover-image", EPUB 3's.
  // Both are needed: real files use one or the other and neither is required.
  FakeFileSystem fs;
  fs.write("/books/withcover.epub", epubfix::withCoverMetaTag());
  reader::OpenedBook book;
  std::string reason;
  REQUIRE(reader::openBook(fs, "/books/withcover.epub", book, &reason));
  CHECK(book.cover.readable());
  CHECK(book.cover.compressedSize > 0);

  FakeFileSystem fs3;
  fs3.write("/books/epub3.epub", epubfix::withCoverProperties());
  reader::OpenedBook book3;
  REQUIRE(reader::openBook(fs3, "/books/epub3.epub", book3, &reason));
  CHECK(book3.cover.readable());
}

TEST_CASE("a book with no cover opens normally and says it has none") {
  // 225 of 225 corpus books declare one, but the firmware must not require it:
  // a book that opens and reads is worth more than a cover.
  FakeFileSystem fs;
  fs.write("/books/plain.epub", epubfix::minimalEpub());
  reader::OpenedBook book;
  std::string reason;
  REQUIRE(reader::openBook(fs, "/books/plain.epub", book, &reason));
  CHECK_FALSE(book.cover.readable());
}
```

Add `withCoverMetaTag()`, `withCoverProperties()` to `test/unit/epub_fixtures.h`, following the existing fixture style there. Each needs a manifest item whose media type is `image/jpeg` and a matching stored archive entry.

- [ ] **Step 2: Run to verify it fails**

```bash
make test 2>&1 | grep -A3 "records where the cover" | head -10
```

Expected: compile error — `OpenedBook` has no `cover`.

- [ ] **Step 3: Add the field**

In `core/include/reader/book.h`, inside `OpenedBook`:

```cpp
  // WHERE THE COVER IS, or an unreadable span if the book declares none.
  //
  // A ChapterSpan rather than a new type, because it is the same four facts -- a
  // local header offset, two sizes and whether it is deflated -- and a second type
  // spelling one shape is what this project's own rule warns about.
  //
  // NOTED DURING THE OPF WALK, not looked up later: Epub already resolves every
  // manifest href, so finding it afterwards would re-parse the OPF for ~100 ms and
  // ~32 KB of transient. Exactly the argument Epub already makes for the NCX.
  //
  // TWO ROUTES, BOTH NEEDED. `<meta name="cover" content="id">` is the EPUB 2
  // convention and what the corpus overwhelmingly uses; `properties="cover-image"`
  // is EPUB 3's. Neither is required by any spec, so a book may have neither.
  ChapterSpan cover;

  // Where the cover is, for a decoder. Mirrors locate() and refuses the same way.
  ChapterLocation locateCover() const {
    ChapterLocation out;
    if (!cover.readable()) return out;
    out.bookPath = path;
    out.localHeaderOffset = cover.localHeaderOffset;
    out.uncompressedSize = cover.uncompressedSize;
    out.compressedSize = cover.compressedSize;
    out.deflated = cover.deflated;
    return out;
  }
```

- [ ] **Step 4: Note the cover in `epub.cpp` and fill the span in `book.cpp`**

In `epub.cpp`, during the manifest walk, record the cover's href by both routes (spine-independent). Expose it as `Epub::coverPath()`, beside the existing `tocPath()`. In `book.cpp`, resolve that path against the zip's central directory into `book.cover`, exactly as a chapter span is resolved.

**A book with no cover is not an error.** Leave `cover` unreadable and open the book.

- [ ] **Step 5: Run to verify it passes**

```bash
make test 2>&1 | tail -5
```

Expected: PASS, 3 new cases.

- [ ] **Step 6: Commit**

```bash
git add core/include/reader/book.h core/src/book.cpp core/src/epub.cpp \
        core/include/reader/epub.h test/unit/test_book.cpp test/unit/epub_fixtures.h
git diff --cached --stat
git commit -m "core: OpenedBook records where its cover is, by both OPF routes"
```

---

## Task 6: `cover.h` — drive the pipeline, emit planes to a sink

**Files:**
- Create: `core/include/reader/cover.h`, `core/src/cover.cpp`
- Test: `test/unit/test_cover.cpp`

- [ ] **Step 1: Write the header**

```cpp
#pragma once
#include <cstdint>
#include <string>

#include "reader/book.h"
#include "reader/filesystem.h"
#include "reader/imagefit.h"

namespace reader {

// WHERE A DECODED COVER GOES.
//
// A sink rather than a buffer, because the whole point is that the planes are
// never all in memory: two rows at a time come out of CoverFitter and go straight
// to the card. The shell implements this over SdFat, the tests over a vector.
//
// Same shape as SettingsSink, for the same reason -- core/ does not learn what a
// filesystem is, and the desktop can drive the real code with a fake.
class CoverPlaneSink {
 public:
  virtual ~CoverPlaneSink() = default;
  // Called once, before any row, with the plane geometry.
  virtual bool begin(int panelW, int panelH, int planeRowBytes, int rows) = 0;
  // One destination row, both planes. `rowBytes` each.
  virtual bool row(const uint8_t* msb, const uint8_t* lsb) = 0;
  // Called once at the end. `ok` is false if decoding was abandoned or failed, and
  // an implementation MUST NOT leave a file that a reader would accept.
  virtual bool finish(bool ok) = 0;
};

// Answered every few source rows. True abandons the decode -- the shell answers it
// from rawSamplesPending(), so a button press gets the device out of the way.
//
// A FUNCTION POINTER, NOT std::function: this is -fno-exceptions embedded code and
// completeIndex's stop predicate already set the precedent.
using CoverStopFn = bool (*)(void*);

enum class CoverResult {
  Ok,
  NoCover,        // the book declares none
  Unsupported,    // progressive JPEG, interlaced/palette PNG, not an image we read
  ReadFailed,     // the card, the zip, or a truncated entry
  OutOfMemory,    // a window or a buffer could not be allocated
  Abandoned,      // the stop predicate said so
};

const char* coverResultName(CoverResult r);

// DECODE `book`'s COVER INTO `sink`, STREAMING, HOLDING NEITHER IMAGE NOR PLANE.
//
// Peak heap, worst realistic case (a deflated JPEG -- 59% of corpus JPEG covers
// are deflated inside the zip): the zip inflater's 36,956 bytes, TJpgDec's ~3,500,
// ITS MCU BAND BUFFER at 8.4-16.9 KB, a destination accumulator and an error row
// at ~2,112 each, and two plane rows. About 54-62 KB, against ~87 KB free at sleep
// once the reader's chapter is released.
//
// THE BAND IS THE PART THAT SURPRISES, and it is why JpegDecoder reports its own
// workspace rather than this comment asserting a number: TJpgDec emits MCU
// RECTANGLES, so a row is not complete until its whole band has arrived and one
// band must be held. Ask JpegDecoder::workspaceBytes() rather than trusting this.
//
// A DEFLATED PNG NEEDS TWO WINDOWS and may answer OutOfMemory. That is 1 of 225
// corpus books, and the caller falls back to the reading card.
CoverResult decodeCover(FileSystem& fs, const OpenedBook& book, int panelW, int panelH,
                        CoverFit fit, CoverPlaneSink& sink,
                        CoverStopFn stop = nullptr, void* stopCtx = nullptr);

}  // namespace reader
```

- [ ] **Step 2: Write the failing test**

Create `test/unit/test_cover.cpp`:

```cpp
#include <vector>

#include "doctest.h"
#include "epub_fixtures.h"
#include "fake_fs.h"
#include "image_fixtures.h"
#include "reader/cover.h"

namespace {

struct VectorSink : reader::CoverPlaneSink {
  int w = 0, h = 0, bytes = 0, rows = 0, declaredRows = 0;
  bool finished = false, finishedOk = false;
  std::vector<uint8_t> msb, lsb;

  bool begin(int panelW, int panelH, int planeRowBytes, int r) override {
    w = panelW; h = panelH; bytes = planeRowBytes; declaredRows = r;
    return true;
  }
  bool row(const uint8_t* m, const uint8_t* l) override {
    msb.insert(msb.end(), m, m + bytes);
    lsb.insert(lsb.end(), l, l + bytes);
    ++rows;
    return true;
  }
  bool finish(bool ok) override { finished = true; finishedOk = ok; return true; }
};

}  // namespace

TEST_CASE("decodeCover turns a real JPEG cover into a full set of plane rows") {
  FakeFileSystem fs;
  fs.write("/books/c.epub", epubfix::withCoverImage(imgfix::loadFixture("baseline.jpg")));
  reader::OpenedBook book;
  std::string reason;
  REQUIRE(reader::openBook(fs, "/books/c.epub", book, &reason));

  VectorSink sink;
  const reader::CoverResult r =
      reader::decodeCover(fs, book, 480, 800, reader::CoverFit::Fill, sink);
  CHECK(r == reader::CoverResult::Ok);
  CHECK(sink.w == 480);
  CHECK(sink.bytes == 60);
  CHECK(sink.rows == 800);
  CHECK(sink.declaredRows == 800);
  CHECK(sink.finishedOk);
  // Not a blank plane: a cover that decoded to nothing would satisfy every count
  // above and put white on the glass.
  //
  // MIND THE POLARITY -- Framebuffer's convention is 1 = WHITE, so an inked pixel
  // is a CLEARED bit and a plane row starts 0xFF. "Some ink" is therefore "some
  // byte is not 0xFF", not "some byte is non-zero"; the latter reads as ink only
  // by accident and answers FALSE for an all-black cover.
  bool anyInk = false;
  for (uint8_t b : sink.msb) if (b != 0xFF) { anyInk = true; break; }
  CHECK(anyInk);
}

TEST_CASE("a book with no cover is NoCover, and the sink is never begun") {
  FakeFileSystem fs;
  fs.write("/books/plain.epub", epubfix::minimalEpub());
  reader::OpenedBook book;
  std::string reason;
  REQUIRE(reader::openBook(fs, "/books/plain.epub", book, &reason));

  VectorSink sink;
  CHECK(reader::decodeCover(fs, book, 480, 800, reader::CoverFit::Fill, sink) ==
        reader::CoverResult::NoCover);
  CHECK(sink.rows == 0);
  CHECK(sink.w == 0);
}

TEST_CASE("a progressive JPEG is Unsupported, not garbage") {
  FakeFileSystem fs;
  fs.write("/books/p.epub", epubfix::withCoverImage(imgfix::loadFixture("progressive.jpg")));
  reader::OpenedBook book;
  std::string reason;
  REQUIRE(reader::openBook(fs, "/books/p.epub", book, &reason));

  VectorSink sink;
  CHECK(reader::decodeCover(fs, book, 480, 800, reader::CoverFit::Fill, sink) ==
        reader::CoverResult::Unsupported);
  CHECK(sink.finished);
  CHECK_FALSE(sink.finishedOk);  // the sink must be told, so it can refuse to leave a file
}

TEST_CASE("the stop predicate abandons the decode and the sink is told") {
  FakeFileSystem fs;
  fs.write("/books/c.epub", epubfix::withCoverImage(imgfix::loadFixture("baseline.jpg")));
  reader::OpenedBook book;
  std::string reason;
  REQUIRE(reader::openBook(fs, "/books/c.epub", book, &reason));

  int calls = 0;
  VectorSink sink;
  const reader::CoverResult r = reader::decodeCover(
      fs, book, 480, 800, reader::CoverFit::Fill, sink,
      [](void* ctx) { return ++*static_cast<int*>(ctx) > 5; }, &calls);
  CHECK(r == reader::CoverResult::Abandoned);
  CHECK(sink.rows < 800);
  CHECK(sink.finished);
  CHECK_FALSE(sink.finishedOk);
}
```

Add `epubfix::withCoverImage(std::string bytes)` to `epub_fixtures.h` — a minimal EPUB with the image **stored** (method 0) and declared by `<meta name="cover">`.

- [ ] **Step 3: Run to verify it fails**

```bash
cmake -S . -B build && make test 2>&1 | tail -20
```

Expected: compile error, `reader/cover.h` missing.

- [ ] **Step 4: Write `core/src/cover.cpp`**

Sequence: `locateCover()` → open the file through `fs.openRead` → `Zip::locateData` → an `EntrySource` over the compressed bytes → wrap in `InflateSource` **only if `deflated`** → sniff the first bytes for the JPEG SOI or the PNG signature → drive `JpegDecoder` or `PngDecoder` with an internal `ImageRowSink` → that sink feeds `CoverFitter` → each emitted plane row pair goes to `CoverPlaneSink`.

**The private `ImageRowSink` is where this task's work actually is.** It is the adapter between the two push interfaces: `begin(w, h)` sizes the `CoverFitter`, and each `row()` calls `CoverFitter::addRow` and forwards any emitted plane row pair to the `CoverPlaneSink`.

- **Pass `box().dstW`/`dstH` as `JpegDecoder::decode`'s `atLeast` pair**, so the free IDCT scaling is used. Note the ordering problem this creates and solve it in this order: the fit box needs the SOURCE dimensions, which are not known until the decoder has read the headers — so the `ImageRowSink::begin` callback is where `fitCover` is called and `CoverFitter::begin` happens, **not** before the decode starts. The `atLeast` pair passed to `decode` is just the panel size, which is known up front.
- **The stop predicate rides the sink's return value.** `ImageRowSink::row` answers `false` when `stop(stopCtx)` says so, which for JPEG aborts `jd_decomp` through TJpgDec's own `JDR_INTR` path and for PNG stops the scanline loop. There is no second interruption mechanism. Call `stop` **every 8 source rows**, not every row: `completeIndex` checks per block and this is the same trade at a comparable granularity.
- **Distinguish an abort from a failure** using the decoder's `aborted()`, and map to `CoverResult::Abandoned` vs `Unsupported`/`ReadFailed`. Reporting an abandoned decode as a failure would make the shell log a card fault that did not happen.
- **THE SPAN IS NOT GUARANTEED TO BE AN IMAGE, and that is deliberate.** Task 5 chose not to filter the cover on its manifest `media-type`, on the same reasoning that made `Epub::open` stop refusing books over `unique-identifier`: a wrong pointer costs a decoder that sniffs the bytes and falls back to the card, where a `media-type` check costs a *real* cover on a book that spells its type oddly — the more expensive mistake. So **sniff the bytes and refuse non-image data gracefully**; do not assume the span is a JPEG or a PNG because the OPF said so. Neither signature matching means `CoverResult::Unsupported`, not a crash and not a guess.
- `sink.finish(false)` on **every** non-`Ok` path, including `Abandoned`.

- [ ] **Step 5: Run to verify it passes**

```bash
cmake -S . -B build && make test 2>&1 | tail -20
```

Expected: PASS, 4 new cases.

- [ ] **Step 6: Commit**

```bash
git add core/include/reader/cover.h core/src/cover.cpp test/unit/test_cover.cpp \
        test/unit/epub_fixtures.h
git diff --cached --stat
git commit -m "core: decode a book's cover into plane rows, streaming and interruptible"
```

---

## Task 7: The corpus probe — a repeatable refusal rate

**Files:**
- Modify: `sim/main.cpp` (add a `cover` subcommand)
- Create: `tools/covers.py`

**Why:** "which books get a cover" must be a number, not an anecdote — the same reasoning as the committed corpus manifest and the EPUB refusal rate.

- [ ] **Step 1: Add a `cover` subcommand to the simulator**

`reader_sim cover <book.epub> <out.png> [--canvas WxH] [--fit fill|whole]` — runs `decodeCover` through `HostFileSystem`, composes the two planes into a 4-level PNG with `writeGrayPng`, and prints one line: the result name, the source dimensions, the chosen scale, and the elapsed milliseconds. On a non-`Ok` result it prints the reason and exits non-zero.

This is also **the generator for the board asset in Task 12**, which is why it writes a PNG rather than a plane file.

- [ ] **Step 2: Write `tools/covers.py`**

Walks `~/.cache/encre-corpus`, runs `reader_sim cover` per book at both geometries, and prints a table: total, `Ok`, and a count per `CoverResult`, plus the slowest and median desktop times. `--cache DIR` to match `corpus.py`.

- [ ] **Step 3: Run it and record the numbers**

```bash
make sim && python3 tools/covers.py | tail -20
```

Expected: **~223 `Ok` and 2 `Unsupported`** (the two progressive JPEGs). A materially
different result means a decoder bug — investigate before continuing, do not adjust the
expectation.

**`OutOfMemory` MUST NOT APPEAR, and its absence is not good news.** The one deflated
PNG in the corpus needs two 32 KB inflate windows and refuses *on the device*; a 64-bit
host serves both without complaint, so it counts `Ok` here. **This report measures
format support, not what fits** — the only instrument for the second is the device, at
Task 9. Say so in the tool's own output rather than letting a clean run read as "every
book's cover will appear."

- [ ] **Step 4: Commit**

```bash
git add sim/main.cpp tools/covers.py
git diff --cached --stat
git commit -m "tools: convert a book's cover on the desktop, and a corpus refusal rate"
```

---

## Task 8: `sleep_cover.h` — the cache file's header

**Files:**
- Create: `core/include/reader/sleep_cover.h`, `core/src/sleep_cover.cpp`
- Test: `test/unit/test_sleep_cover.cpp`

- [ ] **Step 1: Write the header**

```cpp
#pragma once
#include <cstdint>
#include <string>

namespace reader {

inline constexpr const char* kSleepCoverPath = "/.reader/sleep.cover";
inline constexpr uint32_t kSleepCoverMagic = 0x56435245;  // "ERCV"
inline constexpr int kSleepCoverVersion = 1;

// THE CACHED COVER'S HEADER, then plane 0 (MSB) then plane 1 (LSB), each
// `planeBytes` of LOGICAL raster rows -- panelW pixels a row, (panelW + 7) / 8
// bytes, top to bottom, 1 = white as Framebuffer means it.
//
// LOGICAL, NOT PHYSICAL, AND AN EARLIER DRAFT OF THIS PLAN SAID PHYSICAL. That was
// wrong on the device and right on the desktop, which is the worst way to be wrong:
// the shell binds Rotation::Ccw (shell/src/main.cpp), and under Ccw
// `byteIndex` maps logical (x, y) to physical (physX = y, physY = width - 1 - x) --
// so ONE LOGICAL ROW IS ONE PHYSICAL COLUMN. A streaming row-major downscale can
// only ever emit logical rows (a physical row would need the whole image), so a
// file of physical rows cannot be produced by CoverFitter at all.
//
// The simulator and every golden are Rotation::None, where logical IS physical --
// so a reader that just memcpy'd would pass the entire desktop suite and smear on
// glass. That is the exact hazard CLAUDE.md records for the veil, fillRect, the
// glyph blit and ditherRect, four times over.
//
// TWO PLANES SERVE THREE PASSES: Plane::Bw inks where coverage >= 2, which is
// exactly "MSB set", so the Bw base pass and the Msb pass read the same plane.
//
// ONE FILE, NOT ONE PER BOOK, scoped exactly like last.json because the sleep
// screen only ever shows the last-read book. That makes cache eviction -- its own
// Phase 5 card -- a non-problem by construction. The cost is stated: alternating
// between two books re-decodes on each sleep.
//
// `complete` IS WRITTEN LAST, by seeking back. FileSystem has no rename (and must
// not grow one for this -- appendToCard is a shell free function for exactly that
// reason), so atomicity is a flag the writer sets only when every plane row is
// down. A half-written file -- an abandoned decode, a power loss -- is never
// mistaken for a good one.
struct SleepCoverHeader {
  uint32_t magic = kSleepCoverMagic;
  int32_t version = kSleepCoverVersion;
  int32_t panelW = 0;
  int32_t panelH = 0;
  int32_t rotation = 0;    // the Rotation the planes were packed for
  int32_t planeBytes = 0;  // Framebuffer::sizeBytes() -- ONE plane
  uint32_t bookBytes = 0;  // the EPUB's size, the identity check
  int32_t complete = 0;    // written last; 0 means do not trust what follows
  char bookPath[128] = {}; // NUL-padded; a longer path stores empty and never matches
};

// Fixed-size little-endian encoding, so the file does not depend on the compiler's
// padding. 160 bytes.
inline constexpr size_t kSleepCoverHeaderBytes = 160;

void encodeSleepCoverHeader(const SleepCoverHeader& h, uint8_t* out);
bool decodeSleepCoverHeader(const uint8_t* in, size_t bytes, SleepCoverHeader& out);

// Whether this header describes a cover that may be painted NOW: complete, the
// right magic and version, the right panel and rotation, and the same book.
//
// ONE PREDICATE, asked in one place. Two spellings of "is the cache good" would be
// two chances to disagree, and this project has shipped a dead button twice from
// exactly that shape.
bool sleepCoverUsable(const SleepCoverHeader& h, const std::string& bookPath,
                      uint32_t bookBytes, int panelW, int panelH, int rotation,
                      int planeBytes);

}  // namespace reader
```

- [ ] **Step 2: Write the failing test**

Create `test/unit/test_sleep_cover.cpp`:

```cpp
#include <cstring>
#include <vector>

#include "doctest.h"
#include "reader/sleep_cover.h"

namespace {

reader::SleepCoverHeader sample() {
  reader::SleepCoverHeader h;
  h.panelW = 528; h.panelH = 792; h.rotation = 1;
  h.planeBytes = 52272; h.bookBytes = 1234567; h.complete = 1;
  std::strcpy(h.bookPath, "/books/Le Fleau.epub");
  return h;
}

}  // namespace

TEST_CASE("the header round-trips through its fixed-size encoding") {
  std::vector<uint8_t> buf(reader::kSleepCoverHeaderBytes);
  const reader::SleepCoverHeader in = sample();
  reader::encodeSleepCoverHeader(in, buf.data());

  reader::SleepCoverHeader out;
  REQUIRE(reader::decodeSleepCoverHeader(buf.data(), buf.size(), out));
  CHECK(out.magic == in.magic);
  CHECK(out.version == in.version);
  CHECK(out.panelW == 528);
  CHECK(out.planeBytes == 52272);
  CHECK(out.bookBytes == 1234567u);
  CHECK(out.complete == 1);
  CHECK(std::string(out.bookPath) == "/books/Le Fleau.epub");
}

TEST_CASE("a short buffer is refused rather than read past") {
  std::vector<uint8_t> buf(reader::kSleepCoverHeaderBytes);
  reader::encodeSleepCoverHeader(sample(), buf.data());
  reader::SleepCoverHeader out;
  CHECK_FALSE(reader::decodeSleepCoverHeader(buf.data(), 12, out));
}

TEST_CASE("sleepCoverUsable refuses every way the cache can be stale") {
  const reader::SleepCoverHeader good = sample();
  const std::string path = "/books/Le Fleau.epub";

  CHECK(reader::sleepCoverUsable(good, path, 1234567, 528, 792, 1, 52272));

  // Each of these has actually happened to a cache somewhere, and each must cost
  // the cover rather than putting the WRONG book's cover on the glass for hours.
  SUBCASE("incomplete") {
    reader::SleepCoverHeader h = good; h.complete = 0;
    CHECK_FALSE(reader::sleepCoverUsable(h, path, 1234567, 528, 792, 1, 52272));
  }
  SUBCASE("a different book at the same path") {
    CHECK_FALSE(reader::sleepCoverUsable(good, path, 999, 528, 792, 1, 52272));
  }
  SUBCASE("a different book") {
    CHECK_FALSE(reader::sleepCoverUsable(good, "/books/Other.epub", 1234567, 528, 792, 1, 52272));
  }
  SUBCASE("the other panel") {
    CHECK_FALSE(reader::sleepCoverUsable(good, path, 1234567, 480, 800, 1, 60000));
  }
  SUBCASE("the other rotation") {
    CHECK_FALSE(reader::sleepCoverUsable(good, path, 1234567, 528, 792, 0, 52272));
  }
  SUBCASE("a plane size that disagrees with the geometry") {
    CHECK_FALSE(reader::sleepCoverUsable(good, path, 1234567, 528, 792, 1, 52273));
  }
  SUBCASE("a version from another firmware") {
    reader::SleepCoverHeader h = good; h.version = 99;
    CHECK_FALSE(reader::sleepCoverUsable(h, path, 1234567, 528, 792, 1, 52272));
  }
  SUBCASE("not our file at all") {
    reader::SleepCoverHeader h = good; h.magic = 0;
    CHECK_FALSE(reader::sleepCoverUsable(h, path, 1234567, 528, 792, 1, 52272));
  }
}

TEST_CASE("a path too long for the field stores empty and therefore never matches") {
  // Truncating would be worse than refusing: two books whose paths share their
  // first 127 bytes would each accept the other's cover.
  reader::SleepCoverHeader h = sample();
  const std::string longPath(200, 'x');
  std::vector<uint8_t> buf(reader::kSleepCoverHeaderBytes);
  reader::SleepCoverHeader made;
  made.panelW = 528; made.panelH = 792; made.rotation = 1;
  made.planeBytes = 52272; made.bookBytes = 1; made.complete = 1;
  // The writer is responsible for this; assert the predicate cannot match empty.
  CHECK_FALSE(reader::sleepCoverUsable(made, longPath, 1, 528, 792, 1, 52272));
}
```

- [ ] **Step 3: Run to verify it fails**

```bash
cmake -S . -B build && make test 2>&1 | tail -20
```

Expected: compile error.

- [ ] **Step 4: Write `core/src/sleep_cover.cpp`**

Little-endian fixed-width encode/decode. `sleepCoverUsable` checks every field listed in the test. A `bookPath` longer than 127 bytes stores empty and therefore matches nothing.

- [ ] **Step 5: Run to verify it passes**

```bash
cmake -S . -B build && make test 2>&1 | tail -5
```

Expected: PASS.

- [ ] **Step 6: Add `Framebuffer::writePackedRow`, WHICH IS WHERE THE ROTATION LIVES**

The cache holds **logical** raster rows, and on the device the frame is
`Rotation::Ccw`, so getting a row onto the frame is not a memcpy. This is the one
function that knows that, and it goes in `core/` **because `shell/` has no
harness** — a scatter written in `shell/src/main.cpp` could not be tested at all,
and the desktop cannot catch it anyway (see below).

```cpp
  // ONE LOGICAL ROW OF PACKED BITS ONTO THE FRAME, 1 = white, MSB = leftmost --
  // the packing `bitMask()` already implies and `imagefit.h` already emits.
  //
  // NOT A memcpy, AND THAT IS THE WHOLE POINT. Under Rotation::Ccw, byteIndex maps
  // logical (x, y) to physical (physX = y, physY = width - 1 - x), so one logical
  // ROW is one physical COLUMN: `row`'s bits land in `width` different bytes at one
  // fixed bit position, strided by physRowBytes(). Under Rotation::None it really is
  // a memcpy into `data() + y * physRowBytes()`.
  //
  // THE DESKTOP CANNOT CATCH A WRONG ONE. The simulator and every golden are
  // Rotation::None, where the two branches agree -- so a version that always
  // memcpy'd would pass the entire suite and smear diagonally on glass, which is
  // precisely what CLAUDE.md records happening to the veil, fillRect, the glyph blit
  // and ditherRect. The test below therefore asserts the ROTATED case against
  // getPixel, and that assertion is the only thing standing between this and the
  // panel.
  void writePackedRow(int y, const uint8_t* row);
```

Test it in `test/unit/test_framebuffer.cpp`, beside the existing byte-wise cases,
and make it the shape those already use: build a frame at **both** rotations, write
a known pattern through `writePackedRow`, and read every pixel back with
`getPixel`, asserting it equals what a per-pixel `setPixel` of the same pattern
would have produced. Include a `panelW` that is **not** a multiple of 8 so the
partial last byte is exercised — the real panels are 480 and 528, both multiples of
8, so nothing on the device reaches that edge and only a test can.

**Prove it by mutation**: force the unrotated branch for both rotations. Under
`Rotation::None` nothing should fail; under `Ccw` it must fail loudly. If it fails
nothing, your test is not building a `Ccw` frame.

- [ ] **Step 7: Commit**

```bash
git add core/include/reader/sleep_cover.h core/src/sleep_cover.cpp \
        core/include/reader/framebuffer.h core/src/framebuffer.cpp \
        test/unit/test_sleep_cover.cpp test/unit/test_framebuffer.cpp
git diff --cached --stat
git commit -m "core: the cached cover's header, and the one row-blit that knows about rotation"
```

---

## Task 9: GATE — measure the decode on the device

**This is not a task to tick and move past. It is the decision point the spec names.**

**Files:**
- Modify: `shell/src/main.cpp` (a temporary probe, removed in Task 18)

- [ ] **Step 1: Add a boot-time probe behind a build flag**

Guarded by `ENCRE_COVER_PROBE`, so it costs nothing in a normal build — the same shape as `ENCRE_FS_SELFTEST`. After the card mounts, open the first book in `/books`, run `decodeCover` into a sink that counts rows and discards them, and log:

```
[cover] <result> src=WxH scale=1/N rows=R decode=Xms read=Yms heapMin=Z
```

- [ ] **Step 2: Build and hand the flash command to the user**

```bash
PLATFORMIO_BUILD_FLAGS="-DENCRE_COVER_PROBE=1" make firmware
```

**Flashing must be run by the user** — the permission classifier blocks it from an agent. Give them the upload command and ask for the `[cover]` lines. If `freeink-sdk/` is empty in this worktree, `git submodule update --init` first.

- [ ] **Step 2b: MEASURE A JPEG COVER AND A PNG COVER SEPARATELY — they are not comparable**

`decodeCover` passes the panel size to `JpegDecoder::decode` as `atLeast`, so TJpgDec's
free IDCT scaling shrinks a JPEG **before `CoverFitter` ever sees it** — up to 64× fewer
pixels through the box filter and the diffusion. **PNG has no equivalent**: there is no
scaled inflate, so a PNG cover walks every source pixel of a 1600×2400 image.

So a single number is not an answer to this gate. Put a **median JPEG** and a **PNG** cover
on the card and report both. If only one is measured it will be the JPEG — 81% of covers —
and the PNG path, which is 17% of a real library, will be the one nobody looked at.

**Four books from the corpus that span the cases**, picked nearest the 1400×2100 median:

| case | book | cover | file |
|---|---|---|---|
| JPEG deflated — *the majority*, 59% of JPEG covers | *The Rescue* | 1400×2100 | `030bdda8969df031.epub` |
| JPEG stored | *The Book of Mormon* | 1424×2048 | `ff6389c9f9a4c4c8.epub` |
| PNG stored — 38 of 39 PNGs | *Big Dummy's Guide* | 1600×2400 | `1929dda32a16ff6b.epub` |
| PNG deflated — **expected to refuse** on device | *Neuromancien* | 601×918 | `b76b4d9597650aa1.epub` |

They are in `~/.cache/encre-corpus/*/`. The fourth is the interesting one: it is the 1-of-225
that needs two 32 KB windows, and it should answer `OutOfMemory` and fall back to the card.
**If it decodes instead, the heap headroom is larger than measured and the budget is wrong in
the safe direction — say so rather than quietly enjoying it.**

- [ ] **Step 3: Record the numbers in the spec**

Add a short section to `docs/superpowers/specs/2026-08-29-sleep-screen-covers-design.md` replacing "The number this design turns on, and does not yet have" with what was measured, for at least a median-sized JPEG cover and one PNG.

- [ ] **Step 4: Decide, and say so**

- **Under ~6 s:** proceed to Stage 2 unchanged.
- **6–15 s:** proceed, but note in the spec that the second paint lands late and that the user may see the card for a noticeable while.
- **Over ~15 s:** **stop.** Re-open the approach with the user. The spec names the fallback — the rejected book-open path, which pays the same cost where the user is already waiting behind `OPENING…`.

- [ ] **Step 5: Commit the measurement**

```bash
git add docs/superpowers/specs/2026-08-29-sleep-screen-covers-design.md shell/src/main.cpp
git diff --cached --stat
git commit -m "measure: what a cover decode costs on the X3"
```

---

# Stage 2 — putting it on the screen

## Task 10: The Settings board gains a SLEEP SCREEN section

**A UI change goes into the design HTML first.** This task changes only the board.

**Files:**
- Modify: `design/Settings.dc.html`

- [ ] **Step 1: Add the section and its two rows**

Between the `Typography` row and the `DEVICE` header, insert a section header and two rows, copying the exact markup of the existing rows (54px, `padding: 0 24px`, `--t-value` 500 label, `--t-value` 700 value, `border-bottom: 1px solid #000000`):

```html
    <div style="font-size: var(--t-meta); letter-spacing: 0.2em; font-weight: 500; padding: 18px 24px 6px 24px; border-top: 2px solid #000000;">SLEEP SCREEN</div>
    <!-- `Shows`, NOT `Sleep screen`. The board already states the rule that names
         this row: READING was chosen over TYPOGRAPHY so a section does not repeat
         the row's own word directly above it. Under this header it reads as a
         sentence -- sleep screen, shows, cover + details.

         The value cycles COVER -> COVER + DETAILS -> DETAILS and wraps. COVER draws
         the cover full-bleed and NO BADGE: a full-bleed cover is not a screen the
         device can otherwise be in, so it is unambiguous by itself, where a Library
         or a half-read page is not. Every fallback puts the badge back. -->
    <div style="display: flex; justify-content: space-between; align-items: center; height: 54px; padding: 0 24px; border-bottom: 1px solid #000000;">
      <div style="font-size: var(--t-value); font-weight: 500;">Shows</div>
      <div style="font-size: var(--t-value); font-weight: 700;">COVER + DETAILS</div>
    </div>
    <!-- FILL crops to the panel, WHOLE letterboxes and tints the bands with the
         same level-1 clustered dither Sleep.dc.html uses as its field -- white
         bands read as unpainted glass.

         Deliberately NOT `FILL` / `FIT`: one letter apart is bad at 25px on this
         glass.

         The row is focusable only while `Shows` shows a cover, DERIVED rather than
         tabulated -- Typography's own precedent, where `Font` is unreachable while
         one body face is vendored.

         Measured: 163 of 224 corpus covers are 2:3, and the X3 is 2:3 exactly, so
         this is a no-op for 73% of books on that panel. It earns itself on the X4
         (10% median loss) and on the tail -- the squarest corpus cover is 877x973
         and FILL cuts its title off at both edges. -->
    <div style="display: flex; justify-content: space-between; align-items: center; height: 54px; padding: 0 24px;">
      <div style="font-size: var(--t-value); font-weight: 500;">Cover fit</div>
      <div style="font-size: var(--t-value); font-weight: 700;">FILL</div>
    </div>
```

Then **remove the old `Sleep screen` row** from `DEVICE` and restore `border-bottom` to `Refresh on screen change`, which is no longer the last row of its section — **and check what IS now last in `DEVICE`**: the last row of a section carries no `border-bottom`, because the next section's `border-top` is the line between them. Getting this wrong is the defect that put every row below `DEVICE` a pixel low, and the reported symptom was a line at the top.

- [ ] **Step 2: Render the board and look at it**

```bash
make compare COMPARE_ARGS="--only settings --export build/overlay"
```

Open `build/overlay/settings-*.png` and confirm: three section headers, the first with no rule, nine items, and no doubled rule anywhere.

- [ ] **Step 3: Commit**

```bash
git add design/Settings.dc.html
git diff --cached --stat
git commit -m "design: Settings gains a SLEEP SCREEN section with Shows and Cover fit"
```

---

## Task 11: `Settings` fields, and the Settings screen follows the board

**Files:**
- Modify: `core/include/reader/settings.h`, `core/src/settings.cpp`
- Modify: `core/include/reader/screen_settings.h`, `core/src/screen_settings.cpp`
- Test: `test/unit/test_settings.cpp`, `test/unit/test_focus_restore.cpp`

- [ ] **Step 1: Write the failing tests**

Add to `test/unit/test_settings.cpp`:

```cpp
TEST_CASE("the two sleep-screen fields default to today's behaviour plus a cover") {
  const reader::Settings s;
  CHECK(s.sleepShows == reader::SleepShows::CoverAndDetails);
  CHECK(s.coverFit == reader::CoverFit::Fill);
}

TEST_CASE("the sleep-screen fields survive a save and load") {
  FakeFileSystem fs;
  reader::Settings in;
  in.sleepShows = reader::SleepShows::Details;
  in.coverFit = reader::CoverFit::Whole;
  REQUIRE(reader::saveSettings(fs, in));

  reader::Settings out;
  REQUIRE(reader::loadSettings(fs, out));
  CHECK(out.sleepShows == reader::SleepShows::Details);
  CHECK(out.coverFit == reader::CoverFit::Whole);
}

TEST_CASE("a file from before this feature loads and keeps today's behaviour") {
  // kSettingsVersion did NOT move, so an older file must load clean -- not
  // DEFAULTED -- and take the new fields' defaults. Typography set this precedent
  // and this is the case the rule exists for.
  FakeFileSystem fs;
  fs.write("/.reader/settings.json",
           R"({"version":1,"sleepAfterMs":600000,"fullRefreshEvery":0,)"
           R"("fullOnTransition":true,"logToCard":false,"bodyPpem":32,)"
           R"("margins":18,"lineSpacing":1700,"justify":true})");
  reader::Settings out;
  CHECK(reader::loadSettings(fs, out));  // true: nothing was corrected
  CHECK(out.sleepShows == reader::SleepShows::CoverAndDetails);
  CHECK(out.coverFit == reader::CoverFit::Fill);
  CHECK(out.sleepAfterMs == 600000u);
}

TEST_CASE("a nonsense value in either field is CORRECTED, not fatal") {
  FakeFileSystem fs;
  fs.write("/.reader/settings.json",
           R"({"version":1,"sleepShows":47,"coverFit":-3,"sleepAfterMs":600000})");
  reader::Settings out;
  CHECK_FALSE(reader::loadSettings(fs, out));  // false: something was corrected
  CHECK(out.sleepShows == reader::SleepShows::CoverAndDetails);
  CHECK(out.coverFit == reader::CoverFit::Fill);
  CHECK(out.sleepAfterMs == 600000u);  // the rest of the file still loaded
}
```

Add a new `test/unit/test_screen_settings.cpp` (or extend the existing screen test if one exists — check with `ls test/unit | grep settings`):

```cpp
TEST_CASE("the SLEEP SCREEN section is drawn and Shows cycles three ways") {
  reader::Settings s;
  reader::SettingsScreen scr(s, nullptr);
  scr.setMetrics(600, 54, 44);

  // The constructor lands on the first focusable row, which is Typography at
  // index 1 -- index 0 is the READING header, and setFocus refuses it.
  REQUIRE(scr.focus() == 1);
  // Index 2 is the SLEEP SCREEN header and is skipped, so one Next lands on
  // `Shows` at index 3.
  scr.onGesture({reader::Gesture::Next, 1, false});
  const int shows = scr.focus();
  REQUIRE(shows == 3);

  scr.onGesture({reader::Gesture::Activate, 1, false});
  CHECK(scr.settings().sleepShows == reader::SleepShows::Details);
  scr.onGesture({reader::Gesture::Activate, 1, false});
  CHECK(scr.settings().sleepShows == reader::SleepShows::Cover);
  scr.onGesture({reader::Gesture::Activate, 1, false});
  CHECK(scr.settings().sleepShows == reader::SleepShows::CoverAndDetails);  // wrapped
  CHECK(scr.focus() == shows);  // cycling never moves the focus
}

TEST_CASE("Cover fit is focusable only while Shows shows a cover") {
  // DERIVED, not tabulated -- Typography's precedent. A row that cannot act must
  // not be selectable, which is this screen's standing rule.
  reader::Settings s;
  s.sleepShows = reader::SleepShows::Details;
  reader::SettingsScreen hidden(s, nullptr);
  hidden.setMetrics(600, 54, 44);
  // Walk the whole ring and assert Cover fit is never landed on.
  const int n = 40;
  bool landedOnCoverFit = false;
  for (int i = 0; i < n; ++i) {
    hidden.onGesture({reader::Gesture::Next, 1, false});
    if (hidden.vm().rows[static_cast<size_t>(hidden.vm().focusedRow)].label == "Cover fit")
      landedOnCoverFit = true;
  }
  CHECK_FALSE(landedOnCoverFit);

  s.sleepShows = reader::SleepShows::Cover;
  reader::SettingsScreen shown(s, nullptr);
  shown.setMetrics(600, 54, 44);
  bool reached = false;
  for (int i = 0; i < n; ++i) {
    shown.onGesture({reader::Gesture::Next, 1, false});
    if (shown.vm().rows[static_cast<size_t>(shown.vm().focusedRow)].label == "Cover fit")
      reached = true;
  }
  CHECK(reached);
}

TEST_CASE("no row carries a placeholder any more") {
  // `Sleep screen` was the last row with a board placeholder and nothing behind
  // it -- the row CLAUDE.md ties to issue #11 by number. This asserts the field is
  // dead rather than assuming it, exactly as ListRow::trackingEm1000 is handled.
  reader::Settings s;
  reader::SettingsScreen scr(s, nullptr);
  scr.setMetrics(600, 54, 44);
  for (const reader::SettingsRow& r : scr.vm().rows)
    if (!r.isHeader && !r.discloses) CHECK(!r.value.empty());
}
```

- [ ] **Step 2: Run to verify they fail**

```bash
make test 2>&1 | tail -20
```

Expected: compile errors — `SleepShows` and `Settings::sleepShows` do not exist.

- [ ] **Step 3: Add the fields**

In `core/include/reader/settings.h`, above `validate()`:

```cpp
  // --- The sleep screen (design/Sleep.dc.html, SleepCover*.dc.html) -----------
  //
  // kSettingsVersion is NOT bumped, for the reason stated at the top of this
  // header and for Typography's precedent: an added field takes its default from
  // an older file. Both defaults below are chosen so a card carrying today's file
  // behaves EXACTLY as it does today -- with no cached cover, every mode paints
  // byte-identically to the shipped screen, so the goldens do not move either.
  SleepShows sleepShows = SleepShows::CoverAndDetails;
  CoverFit coverFit = CoverFit::Fill;
```

And, near the top of the header, `SleepShows`. **`CoverFit` comes from `core/include/reader/cover_fit.h`, a leaf that includes nothing** — not from `imagefit.h`, and this is a correction: an earlier draft said `settings.h` should include `imagefit.h` directly, on the assumption that it was cheap. Measured, it is not — `imagefit.h` is **72,949** preprocessed lines because of `<vector>`, against `settings.h`'s **895**, and `settings.h:146` already refuses `layout.h` at 73,976 for exactly this reason. Including it would have undone the decision that header's own comment exists to defend. `cover_fit.h` holds the enum alone, so `settings.h` stays a leaf and the imaging layer does not become a dependency of the settings layer:

```cpp
// WHAT THE SLEEP SCREEN SHOWS. The order is the cycle's order and it wraps.
enum class SleepShows { Cover, CoverAndDetails, Details };
```

In `core/src/settings.cpp`, add to `loadSettings` (an enum read through a bounded int, `ok = false` on anything out of range) and to `saveSettings`, using the keys `"sleepShows"` and `"coverFit"`. Add clamping to `validate()`.

- [ ] **Step 4: Update the Settings screen table**

In `core/src/screen_settings.cpp`, the table becomes nine items in the board's order, with `Field::SleepShows` and `Field::CoverFit`, and **`Item::placeholder` is removed entirely** — nothing produces one now. Add the two cycle cases to `cycleFocused()`, add value labels, and make `focusable()` read `settings_.sleepShows` for the `CoverFit` row.

Update `screen_settings.h`'s class comment: `Sleep screen` is no longer "the last one with nothing behind it", and the screen now has **no** inert rows.

- [ ] **Step 5: Run to verify they pass**

```bash
make test 2>&1 | tail -20
```

Expected: PASS. `test_focus_restore.cpp`'s hand-maintained `movable`/`wrapping` counts may need updating — if they do, update them and read the surrounding comment first; that file's `static_assert` is known-weak (#42).

- [ ] **Step 6: Re-bless the Settings golden**

```bash
make test 2>&1 | grep -i settings | head
```

Inspect `build/settings_candidate.png` (and its X3 sibling) **and say what you see** before blessing: three section headers, nine items, the first header with no rule, no doubled rules, `COVER + DETAILS` right-aligned and not colliding with `Shows`. Then bless.

- [ ] **Step 7: Prove the tests bite (mutation)**

```bash
git add -A && git commit -m "wip: settings before mutation" && cp core/src/screen_settings.cpp /tmp/ss.bak
```

1. Make `Cover fit` unconditionally focusable → the derived-focusability test must fail.
2. Make `Shows` cycle two values instead of three → the cycle test must fail.
3. Drop the `sleepShows` line from `saveSettings` → the round-trip test must fail.

- [ ] **Step 8: Commit**

```bash
git add core/include/reader/settings.h core/src/settings.cpp \
        core/include/reader/screen_settings.h core/src/screen_settings.cpp \
        test/unit/test_settings.cpp test/unit/test_screen_settings.cpp \
        test/unit/test_focus_restore.cpp test/golden
git diff --cached --stat
git commit -m "Settings: a SLEEP SCREEN section, and the last inert row goes live (#11)"
```

---

## Task 12: The two sleep boards, and the generated cover asset

**Design first, again**: these boards are what Task 13 implements.

**Files:**
- Create: `design/SleepCover.dc.html`, `design/SleepCoverDetails.dc.html`
- Create: `design/assets/sleep-cover-480x800.png`, `design/assets/sleep-cover-528x792.png`

- [ ] **Step 1: Generate the cover asset from our own pipeline**

```bash
mkdir -p design/assets
reader_sim cover ~/.cache/encre-corpus/standardebooks/<pick-one>.epub \
  design/assets/sleep-cover-480x800.png --canvas 480x800 --fit fill
reader_sim cover ~/.cache/encre-corpus/standardebooks/<pick-one>.epub \
  design/assets/sleep-cover-528x792.png --canvas 528x792 --fit fill
```

**This is the load-bearing decision of the task.** `make compare` renders the board in Chrome and diffs it against the firmware per pixel, and **Chrome cannot Floyd–Steinberg** — a hand-authored or CSS-generated cover would mismatch across the whole image and the percentage would measure the rasteriser instead of the design. Using our own output as the board's image means the comparison measures the chrome around the cover, which is the part that can actually drift. Same principle as `iconc.py` reading each icon's SVG from its named board: generated from one source, never transcribed twice.

Commit the two PNGs. They are ~50 KB each and they are the only way this board can be compared at all.

- [ ] **Step 2: Write `design/SleepCoverDetails.dc.html`**

Copy `design/Sleep.dc.html` and change exactly two things: replace the `.dither-field` background with the asset as a full-bleed `<img>`, and add a comment block recording why the image is generated and why the badge stays. Keep the card, the badge and every type role identical — this board must differ from `Sleep.dc.html` **only** in its background.

- [ ] **Step 3: Write `design/SleepCover.dc.html`**

The same, with the card **and the badge** removed. Record the reasoning in the board, because it overrides a written rule:

```html
  <!-- NO BADGE, AND THIS OVERRIDES A RULE THIS PROJECT WROTE DOWN.
       CLAUDE.md says the badge "is the load-bearing half and it stays", because
       e-ink holds its last image and a screen left on the glass gives no clue the
       device is asleep rather than frozen. The reason it may go HERE does not
       generalise: a full-bleed book cover is not a screen the device can otherwise
       be in, so it is unambiguous by itself, where a Library or a half-read page is
       not.
       It holds only while a cover is actually on the glass. The badge is hidden IFF
       a cover was painted AND the mode is COVER -- one predicate, asked once. Every
       fallback puts it back. -->
```

- [ ] **Step 4: Look at both boards**

```bash
make compare COMPARE_ARGS="--only sleep_cover --export build/overlay"
```

This will report the screen as not implemented — that is correct at this point, and Task 14 registers it. For now open the two `.dc.html` files in a browser and confirm they render.

- [ ] **Step 5: Commit**

```bash
git add design/SleepCover.dc.html design/SleepCoverDetails.dc.html design/assets
git diff --cached --stat
git commit -m "design: the two cover sleep screens, with the cover generated by our own pipeline"
```

---

## Task 13: `SleepScreen` gets a `CoverSource` and a dynamic fidelity

**Files:**
- Modify: `core/include/reader/screen_sleep.h`, `core/src/screen_sleep.cpp`
- Modify: `core/include/reader/viewmodel.h`
- Modify: `core/include/reader/theme.h`, `core/src/theme_quiet.cpp`
- Test: `test/unit/test_theme_sleep_golden.cpp` (extend), new `test/unit/test_theme_sleep_cover_golden.cpp`

- [ ] **Step 1: Write the failing test**

Create `test/unit/test_theme_sleep_cover_golden.cpp`:

```cpp
// The two cover sleep screens, at four grey levels, at both panel geometries.
//
// THE FIRST CALLER checkGoldenGray HAS EVER HAD. It composes the Lsb and Msb
// planes into one level per pixel exactly as the panel combines them, which is the
// only way a four-level screen can be pinned at all.
#include <string>
#include <vector>

#include "doctest.h"
#include "golden.h"
#include "ramp.h"
#include "reader/framebuffer.h"
#include "reader/screen_sleep.h"
#include "reader/theme_quiet.h"

namespace {

// A CoverSource over two plane buffers held in memory. The device reads them from
// the card a plane at a time; a test may hold both.
struct FakeCover : reader::CoverSource {
  std::vector<uint8_t> msb, lsb;
  bool ok = true;
  bool loadPlane(reader::Plane plane, reader::Framebuffer& fb) override {
    if (!ok) return false;
    // Bw inks where coverage >= 2, which is exactly "MSB set" -- so Bw and Msb read
    // the SAME plane. That identity is the whole reason the cache is two files and
    // not three, and asserting it here is what keeps it true.
    const std::vector<uint8_t>& src =
        (plane == reader::Plane::Lsb) ? lsb : msb;
    if (static_cast<int>(src.size()) != fb.sizeBytes()) return false;
    __builtin_memcpy(fb.data(), src.data(), src.size());
    return true;
  }
};

FakeCover coverFor(int w, int h);  // fills both planes from design/assets via a
                                   // deterministic synthetic image; see Step 3

reader::SleepViewModel sampleWithCover(reader::SleepShows shows) {
  reader::SleepViewModel vm;
  vm.label = "NOW READING";
  vm.title = "Gullible's Travels";
  vm.author = "Ring Lardner";
  vm.progressPercent = 34;
  vm.progress = "34% \xC2\xB7 CH. 07";
  vm.note = "ASLEEP \xC2\xB7 PRESS POWER TO WAKE";
  vm.shows = shows;
  return vm;
}

}  // namespace

TEST_CASE("a cover sleep screen declares Grayscale and a plain one does not") {
  // Dynamic fidelity is what keeps DETAILS at today's single ~825 ms waveform.
  // Pinned here rather than assumed, exactly as the Sleep golden pins Mono.
  FakeCover cov = coverFor(480, 800);
  CHECK(reader::SleepScreen(sampleWithCover(reader::SleepShows::CoverAndDetails), &cov)
            .fidelity() == reader::Fidelity::Grayscale);
  CHECK(reader::SleepScreen(sampleWithCover(reader::SleepShows::Details), &cov)
            .fidelity() == reader::Fidelity::Mono);
  // No cover source at all: Mono, and the screen falls back to the card.
  CHECK(reader::SleepScreen(sampleWithCover(reader::SleepShows::Cover), nullptr)
            .fidelity() == reader::Fidelity::Mono);
}

TEST_CASE("QuietTheme renders both cover sleep screens to golden at both geometries") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;

  auto renderOne = [&](int w, int h, reader::SleepShows shows, const std::string& name) {
    FakeCover cov = coverFor(w, h);
    reader::SleepScreen scr(sampleWithCover(shows), &cov);
    REQUIRE(scr.fidelity() == reader::Fidelity::Grayscale);
    reader::Framebuffer lsb(w, h), msb(w, h);
    scr.render(lsb, ramp.fonts(), theme, reader::Plane::Lsb);
    scr.render(msb, ramp.fonts(), theme, reader::Plane::Msb);
    golden::checkGoldenGray(lsb, msb, name);
  };

  renderOne(480, 800, reader::SleepShows::CoverAndDetails, "sleep_cover_details_480x800");
  renderOne(528, 792, reader::SleepShows::CoverAndDetails, "sleep_cover_details_528x792");
  renderOne(480, 800, reader::SleepShows::Cover, "sleep_cover_480x800");
  renderOne(528, 792, reader::SleepShows::Cover, "sleep_cover_528x792");
}

TEST_CASE("COVER hides the badge and every fallback puts it back") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  FakeCover cov = coverFor(480, 800);

  auto inkAt = [&](reader::SleepShows shows, bool coverOk) {
    FakeCover c = coverFor(480, 800);
    c.ok = coverOk;
    reader::SleepScreen scr(sampleWithCover(shows), &c);
    reader::Framebuffer fb(480, 800);
    scr.render(fb, ramp.fonts(), theme,
               scr.fidelity() == reader::Fidelity::Grayscale ? reader::Plane::Msb
                                                             : reader::Plane::Bw);
    // Count ink in the badge's band -- the bottom 34+ rows the board pins it to.
    long ink = 0;
    for (int y = 800 - 90; y < 800 - 20; ++y)
      for (int x = 0; x < 480; ++x)
        if (!fb.getPixel(x, y)) ++ink;
    return ink;
  };

  const long withBadge = inkAt(reader::SleepShows::CoverAndDetails, true);
  const long noBadge = inkAt(reader::SleepShows::Cover, true);
  // A COVER with no badge still has cover ink down there, so this is not "zero" --
  // it is "measurably less than the badge's box adds".
  CHECK(noBadge < withBadge);

  // THE FALLBACK IS THE POINT: a COVER whose cover would not load must draw the
  // card AND the badge, because the reason the badge may be hidden -- that a
  // full-bleed cover is unmistakable -- is false when there is no cover.
  CHECK(inkAt(reader::SleepShows::Cover, false) == inkAt(reader::SleepShows::Details, false));
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake -S . -B build && make test 2>&1 | tail -20
```

Expected: compile error — `CoverSource` does not exist.

- [ ] **Step 3: Write `coverFor()` deterministically**

Generate both planes in the test from a synthetic gradient through `CoverFitter`, **not** by reading `design/assets`. A golden must not depend on a PNG decoder in the test harness, and a synthetic source makes the golden reproducible on any machine.

- [ ] **Step 4: Add `CoverSource`, the view-model fields, and the render path**

In `screen_sleep.h`:

```cpp
// WHERE THE CACHED COVER'S PLANES COME FROM.
//
// An interface rather than a buffer, and this is what makes four grey levels
// affordable at the 42,152-byte reading floor: each grayscale pass reads ONE plane
// straight into Framebuffer::data(), so painting a cover costs no extra RAM at all.
// Holding a 2 bpp image would be 104 KB.
//
// Same shape as SettingsSink, for the same reason -- core/ never learns what a
// filesystem is. The shell implements it over the card, the simulator over
// HostFileSystem, the tests over a buffer.
class CoverSource {
 public:
  virtual ~CoverSource() = default;
  // Fill `fb`'s physical store with the plane. False leaves `fb` untouched and the
  // screen falls back to DETAILS -- so a card pulled between two passes costs the
  // cover, never a half-drawn screen.
  virtual bool loadPlane(Plane plane, Framebuffer& fb) = 0;
};
```

`SleepScreen` takes `CoverSource*` (null-able) beside its view model. `SleepViewModel` gains `SleepShows shows = SleepShows::Details;`.

`fidelity()` answers `Grayscale` iff `shows != Details && cover_ != nullptr`, `Mono` otherwise.

`QuietTheme::renderSleep` gains a cover branch: ask the source for the plane; on success **the cover replaces the clear** and the dither field is not drawn; on failure `clear(true)`, draw the field, and draw the card and badge as today. The badge is drawn unless (cover loaded **and** `shows == Cover`).

- [ ] **Step 5: Run, inspect the candidates, bless**

```bash
make test 2>&1 | tail -20
```

Four candidates land in `build/`. **Open each and say what you see** before blessing — is the cover recognisable, is the card legible over it, is the badge present/absent as intended, and **are there any 8-pixel-wide horizontal artefacts** (which would mean Task 4's plane bit order is mirrored). Then bless.

- [ ] **Step 6: Assert the existing Sleep goldens did NOT move**

```bash
make test 2>&1 | grep -i "sleep" | head
```

Expected: `sleep_480x800`, `sleep_528x792`, `sleep_idle_*` all still pass **unchanged**. This is the check that proves the default changes nothing without a cover. If any moved, something reads `shows` where it should not.

- [ ] **Step 7: Commit**

```bash
git add core/include/reader/screen_sleep.h core/src/screen_sleep.cpp \
        core/include/reader/viewmodel.h core/include/reader/theme.h \
        core/src/theme_quiet.cpp test/unit/test_theme_sleep_cover_golden.cpp test/golden
git diff --cached --stat
git commit -m "Sleep: the cover behind the card, at four grey levels (#11)"
```

---

## Task 14: Simulator subcommands and the comparison sheet

**Files:**
- Modify: `sim/main.cpp`
- Modify: `tools/compare-design.py`

- [ ] **Step 1: Add `sleep_cover` and `sleep_cover_details` subcommands**

Both build a `SleepViewModel` with the right `shows` and a `CoverSource` backed by the committed `design/assets/sleep-cover-<W>x<H>.png` decoded through `HostFileSystem` — so the simulator renders **the same pixels the board displays**, which is what makes the comparison meaningful.

- [ ] **Step 2: Register both boards in `tools/compare-design.py`**

Add to `FLOW_SCREENS`:

```python
    ("sleep_cover",         "SleepCover.dc.html",        "Sleep / cover"),
    ("sleep_cover_details", "SleepCoverDetails.dc.html", "Sleep / cover + details"),
```

- [ ] **Step 3: Run the comparison**

```bash
make compare COMPARE_ARGS="--only sleep_cover sleep_cover_details"
```

Expected: both render. **`ok` means the simulator produced a frame, NOT that it matches** — CLAUDE.md records a board drifting 3.02% → 13.02% while the sheet said `ok` the whole time. Export and measure:

```bash
make compare COMPARE_ARGS="--only sleep_cover sleep_cover_details --export build/overlay"
```

Compare like with like: these are **grayscale** screens, so a threshold-at-128 count inflates the figure. Judge them against `reader` (5.34%/6.38%) and `peek` (3.99%/4.02%), **not** against `reader_menu`'s ~3%.

- [ ] **Step 4: Run the full sheet**

```bash
make compare
```

Expected: every board renders, no board named-but-absent error, and nothing else moved.

- [ ] **Step 5: Commit**

```bash
git add sim/main.cpp tools/compare-design.py
git diff --cached --stat
git commit -m "sim: render both cover sleep screens, and put them on the comparison sheet"
```

---

## Task 15: The shell writes and reads the cache

**Files:**
- Modify: `shell/src/main.cpp`

**Nothing on the desktop compiles this file.** Everything here is verified on glass in Task 18.

- [ ] **Step 1: Write the plane writer as a free function**

`static bool writeCoverPlanes(...)` implementing `reader::CoverPlaneSink` over SdFat directly, **not** through `FileSystem`. 104 KB cannot go through `writeAll`, which takes a whole buffer, and the contract has no write handle — `appendToCard` is a shell free function for exactly that reason, and widening a 27-clause contract driven by two harnesses for one caller is the trade this project already declined.

Order: write the header with `complete = 0`, stream every plane row, then **seek back and write `complete = 1`**. `finish(false)` must leave `complete = 0` so the file is never accepted.

Take `SpiBusGuard` for the whole write — this is the display's bus.

- [ ] **Step 2: Write the `CoverSource` over the card**

`loadPlane` opens `/.reader/sleep.cover` through `gSd.openRead`, parses the header, checks `sleepCoverUsable` against the current book and the frame's geometry, seeks to the requested plane and reads `fb.sizeBytes()` into `fb.data()`. `Plane::Bw` and `Plane::Msb` read plane 0; `Plane::Lsb` reads plane 1.

- [ ] **Step 3: Build**

```bash
git submodule update --init   # a fresh worktree has an EMPTY freeink-sdk/
make firmware 2>&1 | tail -20
```

Expected: builds. If "Failed to install Python dependencies into penv" — that is **transient, retry it**, and do not run two builds concurrently.

- [ ] **Step 4: Commit**

```bash
git add shell/src/main.cpp
git diff --cached --stat
git commit -m "shell: write the cover cache atomically, and read a plane per pass"
```

---

## Task 16: `sleepVmFromCard` learns the mode

**Files:**
- Modify: `shell/src/main.cpp`

- [ ] **Step 1: Set `vm.shows` from the settings, and gate it on there being a book**

`sleepVmFromCard` already returns `nothingToContinue = true` when nothing is open. When it does, force `vm.shows = SleepShows::Details` — `SleepIdle` is the badge alone and has no cover to show, and this keeps the fallback ladder in one place rather than splitting it between the view model and the theme.

- [ ] **Step 2: Build**

```bash
make firmware 2>&1 | tail -5
```

- [ ] **Step 3: Commit**

```bash
git add shell/src/main.cpp
git diff --cached --stat
git commit -m "shell: the sleep view model carries the mode, and nothing-open forces DETAILS"
```

---

## Task 17: The sleep path decodes after the first paint

**Files:**
- Modify: `shell/src/main.cpp` (`sleepNow`)

- [ ] **Step 1: Insert the decode between the two paints**

```cpp
  saveReadingPosition("sleep");
  paintSleepScreen();

  // THE SLEEP SCREEN REFINES, EXACTLY AS A READER PAGE DOES. The card is painted
  // first because it is correct and honest immediately; the cover arrives on a
  // second waveform a few seconds later. The user has pressed power and walked
  // away, so the first sleep of a new book still ENDS with the cover on the glass.
  //
  // SLEEP IS THE ONLY MOMENT IN THIS FIRMWARE WHERE FREEING EVERYTHING IS FREE.
  // Deep sleep is a chip reset, so nothing needs to survive -- which is what makes
  // ~87 KB available at the one moment a decode wants it.
  if (coverWanted() && !coverCacheUsable()) {
    // NOTHING NEW IS NEEDED TO FREE THE HEAP: releaseChapter() already exists,
    // built for the peek, which needs the same 36,956 bytes for the same reason.
    //
    // AND IT HAS TO BE THAT FUNCTION. `inflater_` is a VALUE member, not a
    // unique_ptr, and the window lives behind its private Scratch* -- so an
    // "obvious" release that drops the BlockReader, the InflateSource wrapper, the
    // buffer view and the file handle frees NONE of the bytes this path exists to
    // get. Inflater::release() is the load-bearing call and
    // ChapterReader::inflateWindowHeld() is the only observation point that can see
    // the difference: held() and bytesRead() both go false either way.
    // RELEASE THE WHOLE APP, NOT JUST THE CHAPTER -- and the gate is why.
    //
    // Measured on the X3: a deflated JPEG peaks at 81,088 bytes, 17.5 KB ABOVE the
    // desktop's 63,560 for the same work. Releasing only the chapter leaves ~87 KB
    // when the book was opened through the LIBRARY (203 books, ~59 KB, resident
    // under the Reader) -- a margin of about SIX kilobytes on the commonest way to
    // open a book. And the failure would be silent: decodeCover answers OutOfMemory,
    // the screen falls back to the reading card, and it reads as "covers don't work
    // for some books" rather than as a defect anybody reports.
    //
    // Nothing needs the App after the first sleep paint. saveWhereWeAre wrote the
    // session record at NAVIGATION time, not here; paintSleepScreen bypasses App by
    // design (pushing SleepScreen would make the next wake restore INTO it); and the
    // next statement is a chip reset. So this is the sentence the spec always
    // carried -- sleep is the only moment where freeing everything is free -- finally
    // spent. ~65 KB of margin instead of 6.
    //
    // Capture the screen NAME first: the log line below reads it, and by then there
    // is no stack to ask.
    const char* sleptFrom = reader::screenName(gApp->top().id());
    gApp.reset();
    const reader::CoverResult r = decodeCoverToCache();
    logf("[cover] %s in %lums\n", reader::coverResultName(r), (unsigned long)elapsed);
    logFlush();
    if (r == reader::CoverResult::Ok) paintSleepScreen();
    // reacquireChapter() is deliberately NOT called. There is nothing to come back
    // to: the next statement is deepSleep(), which is a chip reset.
  }

  markSleeping();
```

- [ ] **Step 2: Wire the stop predicate**

Pass `rawSamplesPending()` as `decodeCover`'s `stop`, so any button press abandons the decode and the device sleeps at once. An abandoned decode leaves `complete = 0` and is simply re-attempted next sleep.

- [ ] **Step 2b: VERIFY THE RELEASE ON GLASS, because the desktop cannot**

The `App` release is the change the gate forced, and its whole value is a heap figure
no desktop test can produce. Re-run the probe build **after** Stage 2 lands and confirm
from a real sleep — not from boot — that a deflated JPEG decodes with the book opened
**through the Library** on a large card. That is the ~6 KB case; if it still refuses,
the release did not free what it was supposed to and `[cover]` will say `OutOfMemory`.

Also re-check the **deflated PNG**: at ~146 KB free its 120 KB peak should now fit, which
would narrow the stated limit. **Do not claim that in the spec until it is measured** —
the current text says "may", deliberately.

- [ ] **Step 3: Check the ordering against `markSleeping`**

`markSleeping()` writes the flag the next boot needs, and the log is only what a human needs — so the flag must be written **after** the decode, not before. If a decode hangs and the device is reset, a boot that had already set the flag would resume; one that had not starts cold, which is the correct answer for a sleep that never completed.

- [ ] **Step 4: Build**

```bash
make firmware 2>&1 | tail -5
```

- [ ] **Step 5: Commit**

```bash
git add shell/src/main.cpp
git diff --cached --stat
git commit -m "shell: decode the cover after the first sleep paint, and refine into it (#11)"
```

---

## Task 18: On glass

**Flashing must be run by the user.** Remove the Task 9 probe first.

- [ ] **Step 1: Remove the `ENCRE_COVER_PROBE` scaffolding**

```bash
git add shell/src/main.cpp && git commit -m "shell: drop the cover timing probe, its measurement is in the spec"
```

- [ ] **Step 2: Build and hand over the flash command**

```bash
make firmware
```

Give the user the upload command and ask for `pio device monitor -e xteink | tee run.log`.

- [ ] **Step 3: The checklist only the glass can answer**

Ask the user to confirm each, since the desktop can answer none of them:

1. **Does a four-level cover read as a photograph or as noise?** This is the decision that was taken on the simulator and deferred to the panel.
2. **Does `COVER` mode without a badge read as *asleep*?** This is the rule the design overrides.
3. **What does the decode actually cost**, from the `[cover]` line, and does the two-paint refinement read as deliberate or as a glitch?
4. **Does the first sleep of a new book end with the cover on the glass?**
5. **Does a button press during the decode get the device to sleep promptly?**
6. **Is `FILL`'s crop acceptable as a default** on the user's own books?
7. **`DETAILS` mode is pixel-identical to today** — no cover, no grayscale, one waveform.
8. **A card pulled mid-decode** falls back to the card and does not leave a bad file: sleep, wake, sleep again, confirm it recovers.

- [ ] **Step 4: Read the log before believing anything about a wake**

`[boot] reset reason=… slept-flag=… -> RESUME|cold start` distinguishes a real resume from the host resetting the chip. **Attaching a serial logger can turn a wake into a cold boot** — three consecutive attempts came back `USB_UART_CHIP_RESET` with `wake cause=0`. The decisive test needs no logger: sleep, press power, and see whether the screen you left comes back.

- [ ] **Step 5: Move the card to `On glass`**

```bash
gh project item-edit --id PVTI_lAHOAkvc3c4BhZ5gzg36kXU \
  --project-id PVT_kwHOAkvc3c4BhZ5g \
  --field-id PVTSSF_lAHOAkvc3c4BhZ5gzhgVwC4 \
  --single-select-option-id 5012a8f7
```

**Do not write `Closes #11`.** `On glass` → `Done` needs device evidence and only the user can produce it.

---

## Task 19: Documentation

**Files:**
- Modify: `CLAUDE.md`
- Modify: `docs/superpowers/plans/2026-08-20-v1-roadmap.md`

- [ ] **Step 1: Add a CLAUDE.md section**

Under **The chrome screens**, add `Sleep / cover` and `Sleep / cover + details` rows to the screen table. Then a new section recording what a future reader would otherwise re-derive:

- A cover is universal (225/225) and 2.94 MP at the median, so **it can never be held** — every layer streams.
- **Two planes serve three passes**, because `Plane::Bw` is exactly "MSB set". This is what makes four levels affordable at the reading floor.
- **Sleep is the only moment where freeing everything is free**, and `releaseChapter()` is the call — with the `inflater_`-is-a-value-member trap and `inflateWindowHeld()` as the observation point.
- **The badge rule and the rule it overrides**, with the reason it does not generalise.
- **`make compare` cannot compare a dithered photograph**, so the board's cover is generated by our own pipeline.
- The measured decode cost from Task 9.
- The stated limits table.

Update the **Rendering model** section: `Fidelity::Grayscale` now has a caller, and the sentence "No screen declares it today" is false.

Update **Goldens**: `checkGoldenGray` now has callers.

- [ ] **Step 2: Update the roadmap's Phase 5 line**

Mark "sleep screens from covers/user images" as covers-done, user-images-cut-with-a-card.

- [ ] **Step 3: Verify the edits landed**

```bash
grep -n "sleep.cover\|Two planes serve three passes" CLAUDE.md | head
```

**A script with several asserts writes ONCE at the end**, so a later assert failing means none of the earlier edits were written — and if the next command is a `git commit`, it commits the code without the documentation. That has happened twice in this repo.

- [ ] **Step 4: Commit**

```bash
git add CLAUDE.md docs/superpowers/plans/2026-08-20-v1-roadmap.md
git diff --cached --stat
git commit -m "docs: what a cover costs, why two planes, and the badge rule it overrides"
```

---

## Self-review notes

**Spec coverage.** Every section maps to a task: the three modes and the badge rule → 11, 13; fallbacks → 13; four grey levels → 4, 13; fit → 4, 10, 11; Settings IA → 10, 11; the four layers → 2, 3, 4, 6; TJpgDec-vs-ours → 2, 3; memory → 2, 3, 6; the cache → 8, 15; `CoverSource` → 13; writer/reader asymmetry → 15; the sleep path and the three rejected alternatives → 17; boards and the `make compare` problem → 12, 14; testing → every task's mutation step, plus 7 and 13; stated limits → 2, 3, 6; the unknown number → 9; the board card → 18.

**Known weak points, stated rather than hidden:**

- **Task 2's TJpgDec integration is the least specified step in the plan.** Its output callback is block-shaped and `nextRow` is row-shaped, and reconciling them is real work that depends on the vendored source. Expect to spend time there and do not treat the step's brevity as a measure of its size.
- **Task 4's plane bit order is not actually pinned by Task 4's tests** — they compare our packing against our own reference, so a consistently mirrored packing passes. Task 13's golden is what catches it, which is why that task's inspection step names the artefact to look for.
- **Task 11 leaves the device with a live setting that nothing reads** until Task 13. That is the dead-button defect if shipped, so Stage 2 must not be released part-done.
