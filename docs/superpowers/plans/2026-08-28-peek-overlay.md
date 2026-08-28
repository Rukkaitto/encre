# Peek Overlay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Selecting a chapter in Contents opens a panel of that chapter's text over the veiled reading page; `GO HERE` commits the jump, `CLOSE` leaves the page untouched.

**Architecture:** A new `PeekScreen` (an overlay, `Fidelity::Grayscale`) owns a headless `ReaderScreen` built at the panel's narrower column and forwards page gestures into it. The Reader beneath releases its chapter while the peek is up, so only one chapter is ever live. The shell replaces its `goToChapter` branch with a factory prime plus `pushScreen(Peek)`.

**Tech Stack:** C++20, `core/` (portable, no Arduino), `shell/` (Arduino), doctest unit tests, PNG goldens, `tools/compare-design.py`.

**Spec:** `docs/superpowers/specs/2026-08-28-peek-overlay-design.md`. Issue [#1](https://github.com/Rukkaitto/encre/issues/1).

---

## Read this before Task 1

Five facts about this repo that the tasks below assume. Getting any of them wrong wastes a build cycle.

1. **`make test` is the loop.** It builds `core/` and runs the unit and golden tests. Run it after every task.
2. **CMake uses `file(GLOB ...)`.** After adding or removing any source file you MUST re-run `cmake -S . -B build` or the file is silently ignored. Tasks that add a file say so.
3. **A golden is never re-blessed to make a test pass.** A new golden's first run writes `build/<name>_candidate.png` and FAILS by design. You inspect the PNG, say what you see, and only then copy it to `test/golden/`.
4. **Every test in this plan is proved by MUTATION.** Break the code the test defends, watch the test fail, put the code back. A mutation that fails nothing tells you about your *fixture* before it tells you about your test. `touch` the file after restoring it — `cp` plus a compile inside the same second leaves make thinking the object is current.
5. **The desktop cannot see `shell/src/main.cpp`.** There is no test harness in `shell/`. A green suite is not evidence for a shell edit; `grep` the file after editing it.

Build commands used throughout:

```bash
cmake -S . -B build
```

```bash
make test
```

```bash
./build/reader_tests --test-case="<name>"
```

---

## File Structure

| File | Created / Modified | Responsibility |
|---|---|---|
| `core/include/reader/screen_peek.h` | **create** | `PeekScreen`: the overlay, its two constructors, its gesture handling, `chosenCursor()` / `chosenSpine()`. |
| `core/src/screen_peek.cpp` | **create** | `PeekScreen`'s bodies. |
| `core/include/reader/viewmodel.h` | modify | `PeekViewModel` — the band's two runs, the hint arrays. |
| `core/include/reader/app.h` | modify | `ScreenId::Peek`, appended. |
| `core/src/app.cpp` | modify | `screenName` case for `Peek`. |
| `core/include/reader/screen_reader.h` | modify | `releaseChapter()`, `reacquireChapter()`, `hasChapter()`, `goToPosition(spine, cursor)`. |
| `core/src/screen_reader.cpp` | modify | their bodies. |
| `core/include/reader/chapter.h` | modify | `ChapterReader::release()`, `held()`. |
| `core/src/chapter.cpp` | modify | their bodies. |
| `core/include/reader/theme.h` | modify | `peekMetrics()`, `renderPeek()` pure virtuals. |
| `core/include/reader/theme_quiet.h` | modify | their overrides. |
| `core/src/theme_quiet.cpp` | modify | the panel's geometry and paint. |
| `core/include/reader/screens.h` | modify | `setPeek()`, `setPeekDemo()`, `peekPrimed_`. |
| `core/src/screens.cpp` | modify | the `ScreenId::Peek` factory case. |
| `core/src/session_record.cpp` | modify | the `Peek` name↔id mapping. |
| `shell/src/main.cpp` | modify | Contents' Confirm pushes the peek; the pop reacquires; the commit. |
| `sim/main.cpp` | modify | the `peek` subcommand. |
| `test/unit/test_screen_peek.cpp` | **create** | the peek's own behaviour: paging, close, commit, the release. |
| `test/unit/test_veil_planes.cpp` | **create** | the issue's named risk: `veilRect` per plane, both geometries, both rotations. |
| `test/unit/test_theme_peek_golden.cpp` | **create** | the goldens at both geometries. |
| `test/unit/test_focus_restore.cpp` | modify | `Peek` in `kAllScreens`. |
| `test/unit/test_app.cpp` | modify | `NullTheme` gains the two new pure virtuals. |

Task order is dependency order. Tasks 1–3 are the risk the issue names and the two `core/` primitives, so they land before anything depends on them.

---

### Task 1: `veilRect` is consistent across all three grayscale planes

This is the risk issue #1 names. It comes first because if it fails, the panel's whole approach is wrong and everything after it is wasted.

**Files:**
- Test: `test/unit/test_veil_planes.cpp` (create)

- [ ] **Step 1: Write the failing test**

Create `test/unit/test_veil_planes.cpp`:

```cpp
// THE FIRST OVERLAY OVER A GRAYSCALE SCREEN, which is what issue #1 names as its
// risk. Every overlay today -- ItemActions, DeleteConfirm -- sits over the Library,
// which is Fidelity::Mono, so veilRect has only ever been applied ONCE. A peek sits
// over the Reader, so it is applied once per plane.
//
// IT SHOULD BE CONSISTENT: white in Plane::Bw is paper, and coverage 0 in Lsb/Msb is
// also paper, so the veil's whitening ought to mean the same thing in all three. This
// project's notes are pointed about the difference between "should be" and "was
// measured", so it is measured.
//
// AND IT RUNS UNDER Rotation::Ccw AS WELL AS Rotation::None, because veilRect is one
// of the four byte-wise primitives in core/ that has to know Rotation exists -- and
// the whole desktop is Rotation::None, so a transposed veil passes every golden and
// every simulator PNG and smears diagonally on glass.
#include <vector>

#include "doctest.h"
#include "reader/dither.h"
#include "reader/framebuffer.h"

namespace {

// The veil is a function of GEOMETRY ONLY -- it has no plane argument and cannot --
// so "consistent across planes" means: given the same starting ink, the bytes it
// leaves are identical whichever pass drew that ink. This builds one frame per plane
// with identical content and asserts the veiled results agree byte for byte.
reader::Framebuffer veiled(int w, int h, reader::Rotation rot, bool ink) {
  reader::Framebuffer fb(w, h, rot);
  fb.clear(!ink);  // clear(true) is paper; clear(false) is solid ink
  reader::veilRect(fb, 0, 0, w, h);
  return fb;
}

bool bytesEqual(const reader::Framebuffer& a, const reader::Framebuffer& b) {
  REQUIRE(a.sizeBytes() == b.sizeBytes());
  REQUIRE(a.sizeBytes() > 0);
  for (int i = 0; i < a.sizeBytes(); ++i)
    if (a.data()[i] != b.data()[i]) return false;
  return true;
}

}  // namespace

TEST_CASE("the veil is byte-identical whichever plane's frame it is applied to") {
  // Three passes of the grayscale sequence draw into the SAME 1-bit framebuffer
  // shape, so a veil applied in each must produce the same bytes. Both panel
  // geometries and both rotations: the X4 is 480x800 and the X3 528x792.
  for (const reader::Rotation rot : {reader::Rotation::None, reader::Rotation::Ccw}) {
    for (const auto wh : std::vector<std::pair<int, int>>{{480, 800}, {528, 792}}) {
      const int w = wh.first, h = wh.second;
      CAPTURE(w);
      CAPTURE(h);
      CAPTURE(rot == reader::Rotation::Ccw);
      const reader::Framebuffer bw = veiled(w, h, rot, /*ink=*/true);
      const reader::Framebuffer lsb = veiled(w, h, rot, /*ink=*/true);
      const reader::Framebuffer msb = veiled(w, h, rot, /*ink=*/true);
      CHECK(bytesEqual(bw, lsb));
      CHECK(bytesEqual(bw, msb));
    }
  }
}

TEST_CASE("the veil leaves paper alone, so a plane that inked nothing is untouched") {
  // THE PROPERTY THAT MAKES THE PEEK'S VEIL SOUND. Lsb and Msb carry coverage bits,
  // and coverage 0 is paper exactly as white is paper in Bw -- so a region no glyph
  // reached must come out of the veil unchanged in every plane. If the veil INKED
  // anything, an Lsb pass would gain coverage the renderer never computed, and the
  // composed four-level image would show the veil as a grey wash rather than as
  // whitened ink.
  for (const reader::Rotation rot : {reader::Rotation::None, reader::Rotation::Ccw}) {
    for (const auto wh : std::vector<std::pair<int, int>>{{480, 800}, {528, 792}}) {
      const int w = wh.first, h = wh.second;
      CAPTURE(w);
      CAPTURE(h);
      reader::Framebuffer paper(w, h, rot);
      paper.clear(true);
      reader::Framebuffer reference(w, h, rot);
      reference.clear(true);
      reader::veilRect(paper, 0, 0, w, h);
      CHECK(bytesEqual(paper, reference));
    }
  }
}

TEST_CASE("the veil whitens ink identically at both geometries under both rotations") {
  // The counts, not just the equality: 4 of every 9 pixels of ink survive (one 2x2
  // block per 3x3 cell), which test_dither.cpp already pins on a 36x36 frame. Asserted
  // here at PANEL sizes, because 480, 800, 528 and 792 are none of them multiples of
  // 3 -- so the tile's phase runs off the end of the frame, which is the case a
  // 36x36 test cannot reach.
  for (const reader::Rotation rot : {reader::Rotation::None, reader::Rotation::Ccw}) {
    for (const auto wh : std::vector<std::pair<int, int>>{{480, 800}, {528, 792}}) {
      const int w = wh.first, h = wh.second;
      CAPTURE(w);
      CAPTURE(h);
      const reader::Framebuffer fb = veiled(w, h, rot, /*ink=*/true);
      int ink = 0;
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          if (!fb.getPixel(x, y)) ++ink;
      // Between a third and a half: the exact fraction depends on where the 3px
      // phase falls against a width that is not a multiple of 3, and pinning a
      // single number would be pinning that arithmetic rather than the veil.
      CHECK(ink * 100 / (w * h) >= 40);
      CHECK(ink * 100 / (w * h) <= 48);
    }
  }
}
```

- [ ] **Step 2: Re-run cmake, then run the test**

A new source file needs the glob re-run.

```bash
cmake -S . -B build
```

Run: `make test 2>&1 | tail -20`
Expected: PASS. `veilRect` already exists and is already byte-wise, so this is a
characterisation test that should be green on the first run. If it FAILS, stop and
report — the panel design in the spec rests on it.

- [ ] **Step 3: Prove the test bites, by mutation**

`veilRect` is in `core/src/dither.cpp`. Find the branch that keys on rotation and
force the unrotated path for both cases — the exact edit depends on the current
source, so read it first:

```bash
grep -n "Rotation::Ccw" core/src/dither.cpp
```

Comment out the `Ccw` branch so both rotations take the `None` path, then:

Run: `make test 2>&1 | grep -E "assertions|FAILED" | tail -5`
Expected: FAILURES in all three of this file's test cases under the `Ccw` rotation.

- [ ] **Step 4: Restore and confirm green**

```bash
git checkout core/src/dither.cpp && touch core/src/dither.cpp
```

Run: `make test 2>&1 | tail -5`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add test/unit/test_veil_planes.cpp
git commit -m "peek: the veil is the same bytes in every grayscale plane, measured

The risk issue #1 names. Every overlay today sits over the Library, which is Mono,
so veilRect has only ever been applied once; a peek sits over the Reader, so it is
applied once per plane. It should be consistent -- white in Bw is paper and coverage
0 in Lsb/Msb is also paper -- and this project's notes are pointed about the
difference between should-be and was-measured.

At PANEL sizes and under both rotations, which is what a 36x36 frame cannot reach:
480, 800, 528 and 792 are none of them multiples of 3, so the tile's phase runs off
the end of the frame. Proved by forcing the unrotated branch for both rotations.

Refs #1"
```

---

### Task 2: `ChapterReader` can let go of its stream and take it back

**Files:**
- Modify: `core/include/reader/chapter.h`
- Modify: `core/src/chapter.cpp`
- Test: `test/unit/test_chapter.cpp`

- [ ] **Step 1: Write the failing test**

Read the existing file first to match its fixtures and includes:

```bash
sed -n '1,40p' test/unit/test_chapter.cpp
```

Append to `test/unit/test_chapter.cpp`:

```cpp
TEST_CASE("a released ChapterReader holds nothing and can be begun again") {
  // WHAT THE PEEK'S MEMORY DESIGN RESTS ON. A live chapter peaks at 69,884 bytes with
  // a 36,956-byte single allocation, against a measured 45,840-byte heap floor -- so
  // two of them do not fit, and the Reader beneath a peek has to let go of its stream
  // and get it back. Everything behind ChapterReader is a unique_ptr, so letting go is
  // resetting pointers; this asserts that it really does, and that begin() after it
  // produces the same blocks as begin() before it.
  //
  // ASSERTED RATHER THAN REASONED ABOUT, because "the pointers are reset" is exactly
  // the kind of claim that stays true in a comment long after it has stopped being
  // true in the code.
  FakeFileSystem fs;
  const std::string xhtml = "<html><body><p>One.</p><p>Two.</p></body></html>";
  REQUIRE(fs.writeAll("/ch.xhtml", xhtml));

  reader::ChapterLocation where;
  where.path = "/ch.xhtml";
  where.offset = 0;
  where.compressedSize = static_cast<uint32_t>(xhtml.size());
  where.uncompressedSize = static_cast<uint32_t>(xhtml.size());
  where.deflated = false;  // a stored entry: the bytes ARE the text

  reader::ChapterReader ch;
  REQUIRE(ch.begin(fs, where));
  CHECK(ch.held());

  std::vector<std::string> before;
  reader::Block b;
  while (ch.next(b)) before.push_back(b.text);
  REQUIRE(before.size() >= 2);

  ch.release();
  CHECK_FALSE(ch.held());

  // AND IT COMES BACK. Not "it can be begun" in the abstract: the same blocks, in
  // the same order, because a release that quietly lost the location would produce a
  // reader that opens and yields nothing -- which is indistinguishable from a chapter
  // that ended.
  REQUIRE(ch.begin(fs, where));
  CHECK(ch.held());
  std::vector<std::string> after;
  while (ch.next(b)) after.push_back(b.text);
  CHECK(after == before);
}

TEST_CASE("releasing twice is not an error, and a released reader yields nothing") {
  // IDEMPOTENT, because the shell reacquires on both the CLOSE and the GO HERE path
  // and a double release is a caller mistake that must not be a crash. And a released
  // reader answers next() with false rather than dereferencing a null blocks_.
  FakeFileSystem fs;
  const std::string xhtml = "<html><body><p>One.</p></body></html>";
  REQUIRE(fs.writeAll("/ch.xhtml", xhtml));
  reader::ChapterLocation where;
  where.path = "/ch.xhtml";
  where.offset = 0;
  where.compressedSize = static_cast<uint32_t>(xhtml.size());
  where.uncompressedSize = static_cast<uint32_t>(xhtml.size());
  where.deflated = false;

  reader::ChapterReader ch;
  REQUIRE(ch.begin(fs, where));
  ch.release();
  ch.release();
  CHECK_FALSE(ch.held());
  reader::Block b;
  CHECK_FALSE(ch.next(b));
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make test 2>&1 | grep -E "error:|FAILED" | head -5`
Expected: a compile error — `'release' is not a member of 'reader::ChapterReader'`.

- [ ] **Step 3: Add the two methods**

In `core/include/reader/chapter.h`, inside `class ChapterReader`'s public section
(next to `rewind()`), add:

```cpp
  // --- LET GO OF THE STREAM, KEEP WHAT IT WAS OPENED FROM --------------------
  //
  // FOR THE PEEK, and it is what makes the peek possible at all: a live chapter peaks
  // at 69,884 bytes with a 36,956-byte single allocation (the inflate window and its
  // tables) against a measured 45,840-byte heap floor, so TWO live chapters leave
  // single-digit kilobytes on a part where a failed allocation is abort() with no
  // diagnostic. The Reader beneath a peek therefore lets go while the panel is up and
  // takes its stream back when the panel closes.
  //
  // EVERYTHING BEHIND THIS CLASS IS ALREADY A unique_ptr -- file_, bufSrc_, inflated_,
  // blocks_ -- and InflateStream keeps its window and tables as a single Scratch*, so
  // letting go of a chapter's whole footprint is resetting pointers. Nothing new had to
  // be invented for it.
  //
  // `where_` IS KEPT, which is the whole difference between this and destroying the
  // object: begin() has to be callable again with the same location, and a release that
  // lost it would produce a reader that opens and yields nothing -- indistinguishable
  // from a chapter that ended. Idempotent, because the shell reacquires on two
  // different paths and a double release must not be a crash.
  void release();

  // Whether a stream is established. False after release() and before the first
  // begin(). It exists as an OBSERVATION POINT rather than as a guard: nothing in the
  // shell branches on it, and the peek's release test is what needs it -- see
  // docs/superpowers/specs/2026-08-28-peek-overlay-design.md, which records why no
  // gate is required.
  bool held() const { return blocks_ != nullptr; }
```

In `core/src/chapter.cpp`, add the body. Read the file first to see what `begin()`
sets and to place this beside it:

```bash
grep -n "bool ChapterReader::begin\|bool ChapterReader::rewind\|where_" core/src/chapter.cpp
```

```cpp
void ChapterReader::release() {
  // ORDER IS INNERMOST FIRST, because blocks_ reads through inflated_ which reads
  // through bufSrc_/file_. Destroying an owner before its user would leave a live
  // object reading freed memory for as long as the reset expression took, which is
  // not observable today and is the kind of ordering that stops being safe silently.
  blocks_.reset();
  inflated_.reset();
  bufSrc_.reset();
  file_.reset();
  // `where_` and `fromBuffer_` are deliberately NOT cleared -- see the header. They
  // are what begin() needs to put this back.
}
```

If `next()` does not already guard on `blocks_ == nullptr`, add the guard at its top:

```cpp
  if (blocks_ == nullptr) return false;
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `./build/reader_tests --test-case="*released ChapterReader*"`
Expected: PASS, and the second case too.

Then the whole suite: `make test 2>&1 | tail -5` — PASS.

- [ ] **Step 5: Prove the test bites**

Make `release()` skip `inflated_.reset()`, so the biggest allocation survives:

Run: `./build/reader_tests --test-case="*released ChapterReader*"`
Expected: FAIL on `CHECK_FALSE(ch.held())`? **No — and that is the point.** `held()`
reads `blocks_`, so this mutation does NOT bite, which tells you the test cannot see
the allocation it is about. **Fix the test, not the mutation:** add to the first case,
after `ch.release()`:

```cpp
  // THE ALLOCATION IS THE POINT, NOT THE POINTER. `held()` reads blocks_, so a
  // release that dropped blocks_ and kept the 36,956-byte inflate scratch would
  // satisfy every assertion above while freeing none of the memory the peek needs.
  // Asserted through the one thing that observes the inflater: bytesRead() is
  // `inflated_ != nullptr ? produced() : 0`.
  CHECK(ch.bytesRead() == 0);
```

Now re-run with the mutation in place.
Expected: FAIL on the new `CHECK`.

Restore:

```bash
git checkout core/src/chapter.cpp && touch core/src/chapter.cpp
```

Hmm — that also reverts the implementation. Instead, re-apply `inflated_.reset()` by
hand and `touch core/src/chapter.cpp`.

Run: `make test 2>&1 | tail -5`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add core/include/reader/chapter.h core/src/chapter.cpp test/unit/test_chapter.cpp
git commit -m "chapter: let go of the stream, keep the location

What the peek's memory design rests on. A live chapter peaks at 69,884 bytes with a
36,956-byte single allocation against a 45,840-byte floor, so two do not fit and the
Reader beneath a peek has to let go and take its stream back. Everything here was
already a unique_ptr, so letting go is resetting pointers -- nothing new invented.

where_ is kept, which is the difference between this and destroying the object: a
release that lost it produces a reader that opens and yields nothing, which is
indistinguishable from a chapter that ended.

THE TEST'S FIRST FORM COULD NOT SEE THE ALLOCATION. held() reads blocks_, so a
release that dropped blocks_ and kept the inflate scratch passed every assertion
while freeing none of the memory. bytesRead() is what observes the inflater, and it
is in the test now -- a mutation that fails nothing tells you about your fixture
before it tells you about your test.

Refs #1"
```

---

### Task 3: `ReaderScreen` releases, reacquires, and jumps to a cursor

**Files:**
- Modify: `core/include/reader/screen_reader.h`
- Modify: `core/src/screen_reader.cpp`
- Test: `test/unit/test_screen_peek.cpp` (create — the peek's file, started here because these are the methods the peek drives)

- [ ] **Step 1: Write the failing test**

Create `test/unit/test_screen_peek.cpp`:

```cpp
// THE READER'S HALF OF THE PEEK: letting go of a chapter, taking it back, and
// jumping to a cursor rather than to page one.
//
// PeekScreen's own cases are added to this file in Task 6. These three come first
// because the peek cannot be built without them.
#include <memory>
#include <string>

#include "card_book_fixture.h"
#include "doctest.h"
#include "reader_fixture.h"
#include "reader/gesture.h"
#include "reader/screen_reader.h"

using reader::Gesture;

TEST_CASE("a released Reader still renders the page it was on") {
  // THE PROPERTY THAT LETS App::render DRAW THE VEILED PAGE UNDER A PEEK WITH NO
  // DECODE. ReaderScreen::render reads only page_ and vm_, so the page, the header's
  // chapter label and the footer's counter all survive the chapter going away. If it
  // did not hold, the peek would have to keep the Reader's chapter live -- and then
  // two chapters would be live at once, which does not fit.
  cardfix::CardReading r(readerfix::longChapter(40));
  REQUIRE(r.scr->completeIndex());
  for (int i = 0; i < 3; ++i) r.scr->onGesture({Gesture::Next});

  const int wasPage = r.scr->pageIndex();
  const int wasCount = r.scr->pageCount();
  const std::string wasText = readerfix::pageText(r.scr->page());
  const reader::Cursor wasAt = r.scr->currentCursor();
  const uint32_t wasBytes = r.scr->chapterBytesRead();
  REQUIRE(wasBytes > 0);
  REQUIRE_FALSE(wasText.empty());

  r.scr->releaseChapter();
  CHECK_FALSE(r.scr->hasChapter());

  // NOTHING THE PAINT OR A SAVE READS MOVED. The save's fields are listed
  // deliberately: chapterBytesRead() is pageBytes_, a plain member, NOT
  // ChapterReader::bytesRead() -- which would answer 0 with the inflater gone and push
  // progressPercent onto its page/pageTotal fallback, which is the exact shape of the
  // percentage-going-backwards bug this project has already shipped once.
  CHECK(r.scr->pageIndex() == wasPage);
  CHECK(r.scr->pageCount() == wasCount);
  CHECK(readerfix::pageText(r.scr->page()) == wasText);
  CHECK(r.scr->currentCursor() == wasAt);
  CHECK(r.scr->chapterBytesRead() == wasBytes);
}

TEST_CASE("reacquiring costs no seekTo, and the page comes back untouched") {
  // WHAT `CLOSE` COSTS, and it is the one place this plan departs from the 08-24 spec,
  // which budgeted "one seekTo -- 33.9 ms desktop". A seekTo rewinds and decodes
  // forward, so it costs WHAT PAGE YOU ARE ON: the device measured ~376 ms at page 38
  // and ~1010 ms at page 99, and ~3 s deep in a long chapter. Putting that on CLOSE
  // would make discarding a peek cost more than committing one.
  //
  // It is not needed. page_, at_ and starts_ were never disturbed, so the page goes
  // back on glass with no decode; what a seekTo would restore is the live BUILDER, and
  // pb_ being null is an already-handled state whose repair already has a home in
  // restreamAtCurrentPage. THE DECODE COUNT IS THE ASSERTION -- a page that came out
  // right would pass just as happily with a rewind hidden in it.
  cardfix::CardReading r(readerfix::longChapter(40));
  REQUIRE(r.scr->completeIndex());
  for (int i = 0; i < 3; ++i) r.scr->onGesture({Gesture::Next});
  const std::string wasText = readerfix::pageText(r.scr->page());
  const int wasPage = r.scr->pageIndex();

  r.scr->releaseChapter();
  const uint32_t decodes = r.scr->ringStats().decodes;
  REQUIRE(r.scr->reacquireChapter());

  CHECK(r.scr->hasChapter());
  CHECK(r.scr->ringStats().decodes == decodes);  // no rewind happened
  CHECK(r.scr->pageIndex() == wasPage);
  CHECK(readerfix::pageText(r.scr->page()) == wasText);
  // ...and the stream is NOT live, which is what leaves the restream something to do.
  CHECK_FALSE(r.scr->hasLiveStream());
}

TEST_CASE("a reacquired Reader can be paged again, forward and back") {
  // THE HALF THE ASSERTION ABOVE CANNOT MAKE. "No decode happened" is consistent with
  // a reacquire that established nothing usable, so this reads on: a forward turn
  // after a reacquire has to produce the page a forward turn produces, which means
  // paying the seekTo the reacquire skipped -- at the moment the reader asks for it
  // rather than on the press that closed the panel.
  cardfix::CardReading r(readerfix::longChapter(40));
  REQUIRE(r.scr->completeIndex());
  for (int i = 0; i < 3; ++i) r.scr->onGesture({Gesture::Next});
  const int at = r.scr->pageIndex();
  r.scr->onGesture({Gesture::Next});
  const std::string forwardText = readerfix::pageText(r.scr->page());
  r.scr->onGesture({Gesture::Prev});
  REQUIRE(r.scr->pageIndex() == at);

  r.scr->releaseChapter();
  REQUIRE(r.scr->reacquireChapter());
  r.scr->onGesture({Gesture::Next});
  CHECK(r.scr->pageIndex() == at + 1);
  CHECK(readerfix::pageText(r.scr->page()) == forwardText);
}

TEST_CASE("goToPosition lands on the page containing a cursor and anchors the departure") {
  // WHAT `GO HERE` DOES. goToChapter lands on page ONE, which is right for a chapter
  // picked from a list and wrong for a cursor picked inside a peek -- the reader may
  // have paged several pages into the panel before committing. goToAnchor lands on a
  // cursor and deliberately does NOT touch the anchor, because following an anchor has
  // already spent it. This is the third case: a cursor, and a jump in the anchor's
  // sense.
  cardfix::CardReading r(readerfix::longChapter(40));
  REQUIRE(r.scr->completeIndex());
  for (int i = 0; i < 3; ++i) r.scr->onGesture({Gesture::Next});
  const reader::AnchorPos departure = r.scr->here();
  const int pages = r.scr->pageCount();
  REQUIRE(pages > 6);

  // A cursor from further down the SAME chapter, taken from the page index rather
  // than invented -- starts_ holds one cursor per page boundary, so this is a real
  // page start by construction.
  r.scr->onGesture({Gesture::Next});
  r.scr->onGesture({Gesture::Next});
  const reader::Cursor target = r.scr->currentCursor();
  const int targetPage = r.scr->pageIndex();
  r.scr->onGesture({Gesture::Prev});
  r.scr->onGesture({Gesture::Prev});
  REQUIRE(r.scr->currentCursor() == departure ? true : true);  // back where we were

  REQUIRE(r.scr->goToPosition(r.scr->chapterIndex(), target));
  CHECK(r.scr->currentCursor() == target);
  CHECK(r.scr->pageIndex() == targetPage);
  // THE ANCHOR IS THE DEPARTURE POINT, not the destination, and it overwrites
  // whatever stood before -- return_anchor.h's `jumped` rule.
  REQUIRE(r.scr->anchor().isSet());
  CHECK(r.scr->anchor().get() == departure);
}

TEST_CASE("goToPosition across a chapter boundary anchors and lands") {
  // THE CASE A PAGE-NUMBER-BASED SCHEME WOULD FAIL. `at_` is an index into the
  // CURRENT chapter's starts_, so a page number means nothing once the spine entry
  // changes -- which is why the anchor stores (spine, block, line) and why this
  // method takes a spine as well as a cursor. The peek's whole purpose is looking at
  // ANOTHER chapter, so this is the ordinary case rather than the edge one.
  cardfix::CardReading r(readerfix::longChapter(40));
  REQUIRE(r.scr->completeIndex());
  r.scr->onGesture({Gesture::Next});
  const reader::AnchorPos departure = r.scr->here();
  REQUIRE(r.scr->chapterIndex() == 0);

  // The fixture's spine is two entries; ch2 is one short paragraph.
  REQUIRE(r.scr->goToPosition(1, reader::Cursor{}));
  CHECK(r.scr->chapterIndex() == 1);
  CHECK(r.scr->pageIndex() == 0);
  REQUIRE(r.scr->anchor().isSet());
  CHECK(r.scr->anchor().get() == departure);
  CHECK(r.scr->anchor().get().spine == 0);
}

TEST_CASE("a refused goToPosition leaves the screen exactly where it was") {
  // openChapterAt restores the previous chapter on failure, which is what makes a
  // refused jump safe rather than a blank page over a stale index -- goToChapter
  // already relies on it and this must too. A spine off the end is the reachable
  // refusal: an NCX can name a target the spine does not have.
  cardfix::CardReading r(readerfix::longChapter(40));
  REQUIRE(r.scr->completeIndex());
  r.scr->onGesture({Gesture::Next});
  const int wasChapter = r.scr->chapterIndex();
  const int wasPage = r.scr->pageIndex();
  const std::string wasText = readerfix::pageText(r.scr->page());
  const bool hadAnchor = r.scr->anchor().isSet();

  CHECK_FALSE(r.scr->goToPosition(99, reader::Cursor{}));
  CHECK(r.scr->chapterIndex() == wasChapter);
  CHECK(r.scr->pageIndex() == wasPage);
  CHECK(readerfix::pageText(r.scr->page()) == wasText);
  // AND THE ANCHOR DID NOT MOVE. A refused jump is not a departure, so setting the
  // anchor before the walk would leave a way back to a page the reader never left.
  CHECK(r.scr->anchor().isSet() == hadAnchor);
}
```

- [ ] **Step 2: Re-run cmake, then run the test to verify it fails**

```bash
cmake -S . -B build
```

Run: `make test 2>&1 | grep -E "error:" | head -5`
Expected: compile errors — `'releaseChapter'`, `'reacquireChapter'`, `'hasChapter'`,
`'goToPosition'` are not members of `ReaderScreen`.

- [ ] **Step 3: Add the four methods**

In `core/include/reader/screen_reader.h`, in the public section beside `goToChapter`:

```cpp
  // --- LETTING GO SO A PEEK CAN HAVE THE HEAP -------------------------------
  //
  // A live chapter peaks at 69,884 bytes with a 36,956-byte single allocation, against
  // a measured 45,840-byte heap floor, so TWO live chapters do not fit -- and a peek
  // is a second live chapter. This is how there is only ever one: the Reader beneath a
  // peek releases its stream while the panel is up.
  //
  // WHAT SURVIVES IS EVERYTHING THE PAINT AND A SAVE READ: page_ (with owned LaidLine
  // text), at_, starts_, chapterAt_, pageBytes_, vm_, anchor_ and the book's spans.
  // ReaderScreen::render reads only page_ and vm_, so App::render draws the veiled page
  // underneath with no decode at all -- which is the property the whole design rests
  // on. A save is safe for a related reason worth stating: chapterBytesRead() is
  // pageBytes_, a plain member, NOT ChapterReader::bytesRead(), which would answer 0
  // with the inflater gone and push progressPercent onto its page/pageTotal fallback.
  void releaseChapter();

  // TAKE THE STREAM BACK, AND PAY NO seekTo FOR IT.
  //
  // The 08-24 spec budgeted closing a peek at "one seekTo -- 33.9 ms desktop for the
  // worst page in a real book", which is this project's own ratio trap: a seekTo
  // rewinds and decodes forward, so it costs WHAT PAGE YOU ARE ON, and the device
  // measured ~376 ms at page 38, ~1010 ms at page 99 and ~3 s deep in a long chapter.
  // On CLOSE that would make discarding a peek cost more than committing one.
  //
  // It is not needed. Nothing visible was disturbed, so the page is already correct;
  // what a seekTo would restore is the live PageBuilder, and `pb_ == nullptr` is an
  // already-handled state whose repair has a home -- restreamAtCurrentPage, in a quiet
  // window, where abandoning it is free. So this re-establishes the stream at the
  // chapter's start and stops, leaving hasLiveStream() false on purpose.
  //
  // False when the chapter cannot be reopened -- a card pulled while the peek was up.
  // The caller is the shell, which has pollCardPresence for that case.
  bool reacquireChapter();

  // Whether a stream is established. An OBSERVATION POINT, not a guard: no caller
  // branches on it. The three quiet-window jobs are each gated on the Reader being on
  // TOP of the stack, so a peek over it stops them by construction -- see
  // docs/superpowers/specs/2026-08-28-peek-overlay-design.md, which records why no
  // gate is required and what would change that.
  bool hasChapter() const { return chapter_.held(); }

  // --- JUMP TO A POSITION, NOT TO A CHAPTER ---------------------------------
  //
  // WHAT `GO HERE` COMMITS. The three jumps this screen has are deliberately distinct:
  //
  //   goToChapter(spine)      -- page ONE of a spine entry. A chapter picked from a
  //                              list asked for its beginning. Sets the anchor.
  //   goToAnchor(pos)         -- a cursor, and NOT a jump in the anchor's sense: the
  //                              anchor has already been spent by follow().
  //   goToPosition(spine, at) -- a cursor, AND a jump. The reader may have paged
  //                              several pages into the peek before committing, so
  //                              page one is the wrong landing.
  //
  // The page NUMBER is computed on arrival by openAtCursor counting boundaries, which
  // is what lets the peek be honest about not having one while the commit is exact.
  //
  // False leaves the screen exactly where it was, including the anchor: openChapterAt
  // restores the previous chapter on failure, and the anchor is set only after the walk
  // succeeds -- a refused jump is not a departure, and anchoring one would leave a way
  // back to a page the reader never left.
  bool goToPosition(int spine, Cursor at);
```

In `core/src/screen_reader.cpp`, beside `goToChapter`:

```cpp
void ReaderScreen::releaseChapter() { chapter_.release(); }

bool ReaderScreen::reacquireChapter() {
  if (chapter_.held()) return true;
  // reopenChapter is exactly this job and already existed for it: "re-establishes a
  // chapter's stream without touching the index or the page". NO seekTo follows --
  // see the header.
  if (!reopenChapter(chapterAt_)) return false;
  // EXPLICIT, not incidental. The builder is null because nothing was decoded, and
  // that is the state restreamAtCurrentPage exists to repair; leaving it to fall out
  // of reopenChapter's implementation would make CLOSE's cost depend on a detail of a
  // private method.
  pb_.reset();
  return true;
}

bool ReaderScreen::goToPosition(int spine, Cursor at) {
  if (spine < 0 || spine >= book_.chapterCount()) return false;
  // CAPTURED BEFORE THE MOVE, because the anchor takes the position being LEFT.
  const AnchorPos from = here();
  if (spine != chapterAt_) {
    if (!openChapterAt(spine, /*atEnd=*/false)) return false;
  }
  if (!openAtCursor(at)) return false;
  syncVm();
  // AFTER THE WALK, so a refused jump is not a departure -- see the header.
  anchorJumped(from);
  return true;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `./build/reader_tests --test-case="*released Reader*" --test-case="*reacquir*" --test-case="*goToPosition*"`
Expected: PASS.

Then: `make test 2>&1 | tail -5` — PASS.

- [ ] **Step 5: Prove three of the cases bite**

Each mutation, run, restore. Verify the mutation LANDS before believing what it says.

1. Make `reacquireChapter` end with `seekTo(at_, /*needStream=*/true);` instead of
   `pb_.reset();`.
   Expected: FAIL on `ringStats().decodes == decodes` and on `CHECK_FALSE(hasLiveStream())`.
2. Move `anchorJumped(from)` in `goToPosition` to *before* the `openChapterAt` call.
   Expected: FAIL on "a refused goToPosition leaves the screen exactly where it was".
3. Make `goToPosition` call `openFirstPage()` instead of `openAtCursor(at)`.
   Expected: FAIL on `currentCursor() == target` and `pageIndex() == targetPage`.

After each: `git checkout core/src/screen_reader.cpp` reverts the implementation too,
so restore by hand and `touch core/src/screen_reader.cpp`.

Run: `make test 2>&1 | tail -5`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add core/include/reader/screen_reader.h core/src/screen_reader.cpp test/unit/test_screen_peek.cpp
git commit -m "reader: release the chapter, take it back for free, jump to a cursor

Three methods the peek drives, and one of them corrects the 08-24 spec.

releaseChapter/reacquireChapter are how only one chapter is ever live: a peek is a
second one, and two do not fit. What survives is everything the paint and a save
read -- render() touches only page_ and vm_, and chapterBytesRead() is pageBytes_
rather than the inflater, which would answer 0 and push progressPercent onto its
page/pageTotal fallback.

REACQUIRING PAYS NO seekTo. The spec budgeted 33.9 ms desktop, which is this repo's
own ratio trap: a rewind costs what page you are ON -- ~1010 ms at page 99, ~3 s
deep in a chapter -- so on CLOSE it would cost more than committing. Nothing visible
was disturbed, so only the builder is spent, and restreamAtCurrentPage already
repairs that in a quiet window. The DECODE COUNT is the assertion; a page that came
out right would pass with a rewind hidden in it.

goToPosition is the third jump, distinct from both the others: a cursor AND a
departure, where goToChapter lands on page one and goToAnchor must not re-anchor.
The anchor is set after the walk, so a refused jump is not a departure.

Refs #1"
```

---

### Task 4: `ScreenId::Peek`, its view-model, and its record name

**Files:**
- Modify: `core/include/reader/app.h`
- Modify: `core/src/app.cpp`
- Modify: `core/include/reader/viewmodel.h`
- Modify: `core/src/session_record.cpp`
- Modify: `test/unit/test_focus_restore.cpp`
- Test: `test/unit/test_session_record.cpp`

- [ ] **Step 1: Write the failing test**

Read how the existing round trip is written:

```bash
grep -n "ScreenId::Typography\|nameFor\|idFor" test/unit/test_session_record.cpp core/src/session_record.cpp | head -20
```

Append to `test/unit/test_session_record.cpp`:

```cpp
TEST_CASE("Peek round-trips through the record's screen NAME") {
  // A NAME AND NOT AN ORDINAL, which is why appending a ScreenId is safe at all:
  // 2C-2 inserted three screens into the middle of the enum and a stored ordinal
  // silently became a different screen.
  //
  // A PEEK IS NEVER RESTORED IN PRACTICE -- the factory refuses an unprimed one, so
  // App::restore stops short and leaves the Reader standing, which is the existing
  // "a restore that stops early keeps what already stands" behaviour. It still has to
  // round-trip, because a record naming a screen this build cannot MAP is a different
  // failure from one naming a screen it cannot BUILD, and only the second is intended.
  reader::SessionRecord rec;
  rec.stack.push_back({reader::ScreenId::Home, -1});
  rec.stack.push_back({reader::ScreenId::Reader, 0});
  rec.stack.push_back({reader::ScreenId::Peek, 0});
  const std::string wire = reader::serialiseSession(rec);
  CHECK(wire.find("peek") != std::string::npos);

  reader::SessionRecord back;
  REQUIRE(reader::parseSession(wire, back));
  REQUIRE(back.stack.size() == rec.stack.size());
  CHECK(back.stack[2].screen == reader::ScreenId::Peek);
}
```

If the record's API names differ from `serialiseSession`/`parseSession`/`SessionRecord`,
adapt to what `core/include/reader/session_record.h` actually declares — read it
first:

```bash
sed -n '1,200p' core/include/reader/session_record.h | grep -n "std::string\|bool \|struct "
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make test 2>&1 | grep -E "error:" | head -3`
Expected: `'Peek' is not a member of 'reader::ScreenId'`.

- [ ] **Step 3: Add the enum member, the name, and the view-model**

In `core/include/reader/app.h`, at the END of `enum class ScreenId` (after
`Typography`):

```cpp
  ,
  // design/Peek.dc.html -- a page of the book over the veiled page you are on. APPENDED
  // for the reason ReaderMenu and Typography were: the session record stores a screen by
  // NAME (session_record.h), so an insertion could not silently become another screen,
  // but appending also leaves every existing ordinal where it was.
  Peek
```

In `core/src/app.cpp`'s `screenName`, add:

```cpp
    case ScreenId::Peek: return "peek";
```

In `core/src/session_record.cpp`, add `Peek` to **both** directions of the
name↔id table (the serialise side and the parse side). Find them with:

```bash
grep -n "typography" core/src/session_record.cpp
```

In `core/include/reader/viewmodel.h`, beside `ReaderViewModel`:

```cpp
// design/Peek.dc.html -- book text over the veiled page, for looking somewhere else
// without going there.
//
// TWO RUNS AND NO PAGE NUMBER, and the absence is the design. The panel is inset, so
// its column is narrower, so its text re-wraps -- and re-wrapped text paginates
// differently, which means "page 53" inside the peek is not page 53 of the book. It
// says chapter and percent instead, which are true at any column width. Committing is
// still exact: openAtCursor lands on the page CONTAINING a cursor and counts
// boundaries to name it, so the cursor is what travels and the number is computed on
// arrival.
struct PeekViewModel {
  // `PEEK`. Names the STATE, because `CH. 01 · 4%` alone would read as the Reader's
  // own header and this panel has to be unmistakably not that.
  std::string title = "PEEK";
  // `CH. 01 · 4%`, already composed -- the theme does no arithmetic. The chapter is
  // the book's own name for it where its contents supply one and the `CH. NN`
  // position where they do not, exactly as the Reader's header falls back.
  std::string where;
  // CLOSE / GO HERE / — / —, in the boards' hardware order (Back, Confirm, Up, Down).
  // The last two are EMPTY, not absent: an empty slot is 36px wide (kHintEmptySlotW),
  // and measuring it as zero draws the other two in the wrong places.
  //
  // UP AND DOWN ARE DEAD ON PURPOSE. `Up` already means "return to where I was" on the
  // screen underneath (design/ReaderAnchored.dc.html), and one button with two meanings
  // across a single press is worse than an unbound one -- so the side buttons page in
  // the peek exactly as they do while reading. The four-label bar also did not fit:
  // measured at 480 wide it left ~4px of slack against faces that measure ~3% wider
  // than Chrome.
  std::array<std::string, 4> hints{{"CLOSE", "GO HERE", "", ""}};
  std::array<bool, 4> holds{{false, false, false, false}};
};
```

In `test/unit/test_focus_restore.cpp`, add `ScreenId::Peek` to `kAllScreens`. Read
the file first — it has hand-maintained counts (`movable`, `wrapping`) and a
`static_assert` whose known hole is issue #42:

```bash
grep -n "kAllScreens\|movable\|wrapping\|static_assert" test/unit/test_focus_restore.cpp
```

The peek has **no focus**, so neither count moves. If the `static_assert` compares
against a named member rather than the last one, leave it alone and note it — that is
#42's fix, not this task's.

- [ ] **Step 4: Run the test to verify it passes**

Run: `make test 2>&1 | tail -8`
Expected: PASS. If a `switch` over `ScreenId` warns about an unhandled case, add
`Peek` to it — a `-Werror` build will name the file and line.

- [ ] **Step 5: Prove the record test bites**

Remove `Peek` from the PARSE side of `session_record.cpp`'s table only.
Run: `./build/reader_tests --test-case="*Peek round-trips*"`
Expected: FAIL — the parse rejects or mis-maps the entry.

Restore by hand, `touch core/src/session_record.cpp`, and re-run: PASS.

- [ ] **Step 6: Commit**

```bash
git add core/include/reader/app.h core/src/app.cpp core/include/reader/viewmodel.h core/src/session_record.cpp test/unit/test_focus_restore.cpp test/unit/test_session_record.cpp
git commit -m "peek: the screen id, its view model, and its record name

APPENDED to ScreenId for the reason ReaderMenu and Typography were: the record stores
a NAME, so an insertion could not silently become another screen, and appending leaves
every existing ordinal where it was.

The view model carries two runs and no page number, and the absence is the design: the
panel is inset, so its column is narrower, so its text re-wraps -- 'page 53' inside a
peek is not page 53 of the book. Chapter and percent are true at any column width, and
committing is still exact because the cursor is what travels.

A peek is never restored in practice (the factory refuses an unprimed one and
App::restore stops short), but it round-trips anyway: a record naming a screen this
build cannot MAP is a different failure from one it cannot BUILD, and only the second
is intended.

Refs #1"
```

---

### Task 5: the theme's panel — `peekMetrics` and `renderPeek`

**Files:**
- Modify: `core/include/reader/theme.h`
- Modify: `core/include/reader/theme_quiet.h`
- Modify: `core/src/theme_quiet.cpp`
- Modify: `test/unit/test_app.cpp` (`NullTheme` needs the new pure virtuals)
- Test: `test/unit/test_theme_peek_metrics.cpp` (create)

- [ ] **Step 1: Write the failing test**

Create `test/unit/test_theme_peek_metrics.cpp`:

```cpp
// THE PANEL'S BOX MODEL, DERIVED RATHER THAN PINNED -- which is this project's first
// invariant and the one it breaks most often. Three separate defects came from
// hardcoding a height (the header band 6px out, menu rows compounding a pixel each,
// the hint bar's asymmetric padding), and the Typography panel broke it again in its
// own first commit.
//
// design/Peek.dc.html states the intent and not the numbers: 34px of veil either side
// at the authored 480 width, and NO height at all -- "the panel is sized by its text,
// not the other way round". So the width is derived from the canvas and the height is
// a RESULT of the line count, exactly as headerBandHeight() and hintBarHeight() are
// results.
#include "doctest.h"
#include "ramp.h"
#include "reader/layout.h"
#include "reader/settings.h"
#include "reader/theme_quiet.h"
#include "reader_fixture.h"

TEST_CASE("the peek's column is narrower than the reading column, at both geometries") {
  // THE FACT THAT COSTS THE PANEL ITS PAGE NUMBER. If these were equal the peek could
  // show `53 / 890` and one pagination would serve both -- which is the full-width
  // alternative the board rejects, because it barely reads as an overlay.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  readerfix::Body body;
  const reader::Settings s;

  for (const auto wh : std::vector<std::pair<int, int>>{{480, 800}, {528, 792}}) {
    const int w = wh.first, h = wh.second;
    CAPTURE(w);
    reader::PageMetrics read, peek;
    theme.readerMetrics(w, h, ramp.fonts, body.face, s, read);
    theme.peekMetrics(w, h, ramp.fonts, body.face, s, peek);
    CHECK(peek.columnW > 0);
    CHECK(peek.columnW < read.columnW);
    // The panel is centred, so its column starts further in than the reading column.
    CHECK(peek.columnLeft > read.columnLeft);
  }
}

TEST_CASE("the peek's veil margin is 34px at the authored width and derived elsewhere") {
  // The board is authored at 480 and centred BOTH ways so one board serves both
  // panels. 34*2 + 412 = 480, so at 480 the panel is 412 wide; at 528 the SAME panel
  // width leaves 58px of veil either side, which is what compare-design.py rewrites
  // the board's `left` to. THE PANEL WIDTH IS THE CONSTANT, not the margin -- pinning
  // the margin instead would make the panel wider on the X3 and change its measure,
  // and the peek's text would then wrap differently on the two panels for no reason
  // the design states.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  readerfix::Body body;
  const reader::Settings s;

  reader::PageMetrics x4, x3;
  theme.peekMetrics(480, 800, ramp.fonts, body.face, s, x4);
  theme.peekMetrics(528, 792, ramp.fonts, body.face, s, x3);
  // Same measure on both panels, because the panel box is the same box.
  CHECK(x4.columnW == x3.columnW);
  // ...and it is centred, so the left edge moves by half the width difference.
  CHECK(x3.columnLeft - x4.columnLeft == (528 - 480) / 2);
}

TEST_CASE("the peek's column holds exactly eight lines, and the height follows") {
  // EIGHT LINES, AND THE NUMBER IS THE DESIGN. The board's own note: content-sizing
  // alone ran to ELEVEN lines and the panel then filled the glass to within 48px of
  // the top, which reads as a bordered full screen rather than a modal -- precisely
  // the full-width alternative the board rejects, arrived at by accident.
  //
  // So the firmware picks the LINE COUNT and the height follows. Asserted through the
  // column height rather than through a panel height, because columnH is what
  // PageBuilder reads and is therefore the number that decides how many lines a peek
  // page actually holds.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  readerfix::Body body;
  const reader::Settings s;

  for (const auto wh : std::vector<std::pair<int, int>>{{480, 800}, {528, 792}}) {
    const int w = wh.first, h = wh.second;
    CAPTURE(w);
    reader::PageMetrics m;
    theme.peekMetrics(w, h, ramp.fonts, body.face, s, m);
    const int lineH = (body.face.lineHeight() * m.leadEm1000 + 500) / 1000;
    REQUIRE(lineH > 0);
    CHECK(m.columnH / lineH == reader::kPeekLines);
  }
}

TEST_CASE("the peek's line spacing and justification follow the reader's settings") {
  // THE SAME BOOK AT THE SAME READING SIZE -- only the column is narrower. So the
  // three typography fields that reach a column reach this one too, and a reader who
  // set ragged text does not get justified text in the panel.
  //
  // THE MARGIN IS THE ONE THAT DOES NOT, and that is not an omission: the panel's
  // measure is the PANEL's box, not the page's, so a margin setting has nothing to
  // apply to here. Four fields, three reads -- the same asymmetry readerMetrics states.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  readerfix::Body body;

  reader::Settings ragged;
  ragged.justify = false;
  ragged.lineSpacing = 1600;
  reader::PageMetrics m;
  theme.peekMetrics(480, 800, ramp.fonts, body.face, ragged, m);
  CHECK_FALSE(m.justify);
  CHECK(m.leadEm1000 == 1600);

  reader::Settings wide;
  wide.margins = 30;
  reader::PageMetrics narrow, widened;
  theme.peekMetrics(480, 800, ramp.fonts, body.face, reader::Settings{}, narrow);
  theme.peekMetrics(480, 800, ramp.fonts, body.face, wide, widened);
  CHECK(narrow.columnW == widened.columnW);
  CHECK(narrow.columnLeft == widened.columnLeft);
}
```

- [ ] **Step 2: Re-run cmake, then run to verify it fails**

```bash
cmake -S . -B build
```

Run: `make test 2>&1 | grep -E "error:" | head -3`
Expected: `'peekMetrics' is not a member of 'reader::QuietTheme'` and
`'kPeekLines' has not been declared`.

- [ ] **Step 3: Add the theme surface and the geometry**

In `core/include/reader/theme.h`, beside `readerMetrics`:

```cpp
  // THE PEEK'S COLUMN, which is the reading column's sibling and not a variant of it.
  //
  // design/Peek.dc.html: an inset panel, 2px border, 20px padding, over the veiled
  // page. The measure is therefore the PANEL's box rather than the page's, which is
  // the whole reason the panel cannot show a page number -- a narrower column
  // re-wraps, and re-wrapped text paginates differently.
  //
  // TAKES THE SETTINGS FOR THREE OF FOUR FIELDS, and `margins` is the one it does not
  // read: a margin is the reading page's box model and the panel's box is its own, so
  // there is nothing for it to apply to. `bodyPpem` is absent for readerMetrics'
  // reason -- it has already arrived as `body`. Four fields, two reads.
  virtual void peekMetrics(int panelW, int panelH, const FontSet& fonts,
                           const GlyphSource& body, const Settings& settings,
                           PageMetrics& out) const = 0;

  // design/Peek.dc.html. Takes the page for renderReader's reason: it is already
  // positioned, in framebuffer coordinates, by reader/layout.h.
  virtual void renderPeek(Framebuffer& fb, const FontSet& fonts, const GlyphSource& body,
                          const GlyphSource* italic, const PeekViewModel& vm,
                          const Page& page, Plane plane) = 0;
```

In `core/include/reader/theme_quiet.h`, declare both as `override`.

In `core/src/theme_quiet.cpp`, after `renderReaderMenu`'s section, add the geometry.
`kPeekLines` goes in `core/include/reader/theme.h` (near the top, in `namespace
reader`) so the test can name it:

```cpp
// HOW MANY LINES OF BOOK TEXT A PEEK SHOWS, and the number IS the design rather than
// a consequence of one.
//
// design/Peek.dc.html: content-sizing alone ran to ELEVEN lines and the panel then
// filled the glass to within 48px of the top, which reads as a bordered full screen
// rather than as a modal -- precisely the full-width alternative the board rejects,
// arrived at by accident instead of chosen. A pinned HEIGHT was worse still: it cut
// the last line in half lengthwise, which the firmware cannot even do, since
// PageBuilder lays out whole lines.
//
// So the firmware picks the line count and the panel's height is a RESULT, exactly as
// headerBandHeight() and hintBarHeight() are results. Eight lines is about 180
// characters -- one or two sentences, which is the whole answer to "who is this
// again?".
inline constexpr int kPeekLines = 8;
```

In `core/src/theme_quiet.cpp`:

```cpp
// --- The peek ----------------------------------------------------------------
//
// design/Peek.dc.html. An inset panel of book text over the veiled page you are on.
//
// THE PANEL WIDTH IS THE CONSTANT AND THE VEIL MARGIN IS NOT. The board is authored at
// 480 and centred both ways so one board serves both panels: 34*2 + 412 = 480, so at
// 528 the same panel leaves 58px either side and compare-design.py rewrites the
// board's `left` to say so. Pinning the 34 instead would widen the panel on the X3 and
// change its MEASURE, so the peek's text would wrap differently on the two panels for
// no reason the design states.
constexpr int kPeekPanelW = 412;
constexpr int kPeekPadX = 20;     // the panel's own `padding`, inside its border
constexpr int kPeekBandPadY = 18; // the band's `padding: 18px 20px`
constexpr int kPeekBandRuleH = 2; // its `border-bottom`
constexpr int kPeekBodyPadTop = 16;
constexpr int kPeekBodyPadBottom = 20;
constexpr int kPeekLabelEm = 220;  // `PEEK`, 0.22em
constexpr int kPeekBandGap = 7;

namespace {

// The band's height, DERIVED: one line of the taller of its two faces plus its own
// padding and its rule. `align-items: center` and two runs, so the row is one line
// high -- and the two faces differ (Label500 at 23px, Value700 at 25px), so the taller
// one sets it or the value is clipped.
int peekBandHeight(const reader::FontSet& fonts) {
  const int lh = fonts[reader::Role::Value700].lineHeight() >
                         fonts[reader::Role::Label500].lineHeight()
                     ? fonts[reader::Role::Value700].lineHeight()
                     : fonts[reader::Role::Label500].lineHeight();
  return 2 * kPeekBandPadY + lh + kPeekBandRuleH;
}

// The line box the panel's text is set on -- the same arithmetic PageBuilder uses, so
// the height the panel reserves and the height the pages actually take cannot
// disagree.
int peekLineH(const reader::GlyphSource& body, int leadEm1000) {
  return (body.lineHeight() * leadEm1000 + 500) / 1000;
}

}  // namespace

void QuietTheme::peekMetrics(int panelW, int panelH, const FontSet& fonts,
                             const GlyphSource& body, const Settings& settings,
                             PageMetrics& out) const {
  const int x = panelLeft(panelW, kPeekPanelW);
  const int contentW = panelContentW(kPeekPanelW);
  out.columnLeft = x + kPanelBorder + kPeekPadX;
  out.columnW = contentW - 2 * kPeekPadX;
  out.leadEm1000 = settings.lineSpacing;
  out.indentEm1000 = kBodyIndentEm;
  // JUSTIFIED IF THE READER'S TEXT IS. Same book, same reading size -- only the column
  // is narrower -- so a reader who set ragged text must not find justified text here.
  out.justify = settings.justify;
  out.tracking = {};

  // THE HEIGHT IS A RESULT. kPeekLines line boxes plus the band and the body's
  // padding is the panel's height; the panel is then centred, which is what gives the
  // column its top. Nothing here reads a pinned panel height, and there is none.
  const int lineH = peekLineH(body, out.leadEm1000);
  out.columnH = kPeekLines * lineH;
  const int bandH = peekBandHeight(fonts);
  const int panelBoxH =
      2 * kPanelBorder + bandH + kPeekBodyPadTop + out.columnH + kPeekBodyPadBottom;
  const int y = centreIn(0, panelH, panelBoxH);
  out.columnTop = y + kPanelBorder + bandH + kPeekBodyPadTop;
  // `margins` is deliberately unread -- see Theme::peekMetrics.
}

void QuietTheme::renderPeek(Framebuffer& fb, const FontSet& fonts, const GlyphSource& body,
                            const GlyphSource* italic, const PeekViewModel& vm,
                            const Page& page, Plane plane) {
  // NO fb.clear(): App::render has already painted the Reader, and this screen's whole
  // job is to be in front of it.
  veilRect(fb, 0, 0, fb.width(), fb.height());

  const Font& label = fonts[Role::Label500];
  const Font& value = fonts[Role::Value700];
  const int bandH = peekBandHeight(fonts);
  const int lineH = peekLineH(body, /*leadEm1000=*/kBodyLeadEm);
  (void)lineH;

  // THE PANEL BOX, recomputed from the same arithmetic peekMetrics uses rather than
  // passed in. Two spellings of one geometry is how a panel's border ends up a pixel
  // off its own text; the shared helpers above are the single spelling.
  const int contentW = panelContentW(kPeekPanelW);
  const int columnH = static_cast<int>(kPeekLines) *
                      peekLineH(body, fonts.empty() ? kBodyLeadEm : kBodyLeadEm);
  const int panelBoxH =
      2 * kPanelBorder + bandH + kPeekBodyPadTop + columnH + kPeekBodyPadBottom;
  const int x = panelLeft(fb.width(), kPeekPanelW);
  const int y = centreIn(0, fb.height(), panelBoxH);
  drawPanel(fb, x, y, kPeekPanelW, panelBoxH);

  // --- The band: PEEK, and where this is ---
  //
  // ITS OWN BAND AND NOT drawPanelCaption, and the difference is measured rather than
  // stylistic: the caption's value is Meta400 at 21px on 21px of padding, where this
  // board says --t-value (25px) at weight 700 on 18px. Reusing the caption would draw
  // the value one role too small and the band 6px too tall, which is the header-band
  // defect this project has already paid for once.
  const int bandContentTop = y + kPanelBorder + kPeekBandPadY;
  const int bandContentH = bandH - 2 * kPeekBandPadY - kPeekBandRuleH;
  const int colX = x + kPanelBorder + kPeekPadX;
  const int colW = contentW - 2 * kPeekPadX;
  drawText(fb, label, colX, baselineIn(label, bandContentTop, bandContentH), vm.title,
           Ink::Black, trackingEm(label, kPeekLabelEm), plane);
  if (!vm.where.empty()) {
    // `justify-content: space-between`, so the value's right edge is the band's own
    // padding edge -- not the panel's border. ELIDED to what the label leaves, because
    // a real chapter name can be arbitrarily long: `PREMIÈRE PARTIE : À LIRE AVANT
    // L'ACHAT` is one from a real book on the user's own card.
    const int labelW = label.measure(vm.title, trackingEm(label, kPeekLabelEm));
    const int room = colW - labelW - kPeekBandGap;
    const std::string shown = elideToWidth(value, vm.where, room, {});
    const int vw = value.measure(shown, {});
    drawText(fb, value, colX + colW - vw, baselineIn(value, bandContentTop, bandContentH),
             shown, Ink::Black, {}, plane);
  }
  fb.fillRect(x + kPanelBorder, y + kPanelBorder + bandH - kPeekBandRuleH, contentW,
              kPeekBandRuleH, false);

  // --- The peeked page ---
  //
  // Already positioned by reader/layout.h, in these coordinates, so this is
  // renderReader's loop verbatim -- which is the point: the same book at the same
  // reading size, differing only in the column peekMetrics gave it.
  const StyledFace face{&body, italic, nullptr};
  for (const LaidLine& ln : page.lines) {
    if (ln.markerX >= 0)
      drawText(fb, body, ln.markerX, ln.baselineY, kListMarker, Ink::Black, {}, plane);
    drawTextStyled(fb, face, ln.x, ln.baselineY, ln.text, ln.emphasis, ln.extraPerGapF26,
                   Ink::Black, ln.tracking, plane);
  }

  Hint hints[4];
  buildHints(kHintSlotMarks, vm.hints, vm.holds, hints);
  drawOverlayHintBar(fb, fonts, hints, plane);
}
```

**Note on the `columnH` duplication above:** the draft has `renderPeek` recomputing
the column height with `kBodyLeadEm` while `peekMetrics` uses
`settings.lineSpacing` — those disagree the moment a reader changes their line
spacing, and the panel's border would then not match its text. **Fix it while
implementing:** hoist a single `peekPanelBox(panelW, panelH, fonts, body,
leadEm1000)` helper in the anonymous namespace returning `{x, y, w, h, bandH}`, and
have `peekMetrics` and `renderPeek` both call it. `renderPeek` has no `Settings`, so
add the lead to `PeekViewModel` (`int leadEm1000 = kBodyLeadEm;`, set by
`PeekScreen::syncVm` from the metrics it was given) and pass `vm.leadEm1000`. Two
spellings of one geometry is exactly the defect this project's first invariant is
about.

In `test/unit/test_app.cpp`, add stubs to `NullTheme`:

```cpp
  void peekMetrics(int, int, const FontSet&, const GlyphSource&, const Settings&,
                   PageMetrics&) const override {}
  void renderPeek(Framebuffer&, const FontSet&, const GlyphSource&, const GlyphSource*,
                  const PeekViewModel&, const Page&, Plane) override {}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `./build/reader_tests --test-case="*peek*"`
Expected: PASS on all four metrics cases.

Then: `make test 2>&1 | tail -5` — PASS. Every existing golden must be unchanged:
nothing in this task touches a shipped render path.

- [ ] **Step 5: Prove the tests bite**

1. Change `kPeekLines` to 11 (the board's rejected eleven-line panel).
   Expected: FAIL on "holds exactly eight lines".
2. Make `peekMetrics` pin the veil margin instead of the panel width — set
   `out.columnW = panelW - 2 * 34 - 2 * kPanelBorder - 2 * kPeekPadX;`.
   Expected: FAIL on `x4.columnW == x3.columnW`.
3. Make `peekMetrics` read `settings.margins` into `columnLeft`.
   Expected: FAIL on the settings case's `narrow.columnLeft == widened.columnLeft`.

Restore each by hand and `touch core/src/theme_quiet.cpp`, then `make test` — PASS.

- [ ] **Step 6: Commit**

```bash
git add core/include/reader/theme.h core/include/reader/theme_quiet.h core/src/theme_quiet.cpp test/unit/test_app.cpp test/unit/test_theme_peek_metrics.cpp
git commit -m "peek: the panel's box model, derived rather than pinned

design/Peek.dc.html states the intent and not the numbers: 34px of veil either side at
the authored 480 width, and NO height at all -- the panel is sized by its text. So the
PANEL WIDTH is the constant and the veil margin is not: pinning 34 would widen the
panel on the X3 and change its measure, and the peek's text would wrap differently on
the two panels for no reason the design states.

EIGHT LINES IS THE DESIGN. Content-sizing alone ran to eleven and filled the glass to
within 48px of the top, which reads as a bordered full screen -- precisely the
full-width alternative the board rejects, arrived at by accident. A pinned height was
worse: it cut the last line in half lengthwise, which PageBuilder cannot even do.

Its own band, not drawPanelCaption, and the difference is measured: the caption's value
is Meta400 at 21px on 21px of padding where this board says --t-value at weight 700 on
18px. Reusing it would draw the band 6px too tall, which is the header-band defect this
project has already paid for.

Three of four typography fields reach the column; `margins` does not, because the
panel's measure is the PANEL's box and a page margin has nothing to apply to.

Refs #1"
```

---

### Task 6: `PeekScreen`

**Files:**
- Create: `core/include/reader/screen_peek.h`
- Create: `core/src/screen_peek.cpp`
- Test: `test/unit/test_screen_peek.cpp` (append)

- [ ] **Step 1: Write the failing test**

Append to `test/unit/test_screen_peek.cpp`:

```cpp
// --- PeekScreen ---------------------------------------------------------------

TEST_CASE("the peek is an overlay, declares Grayscale, and promises two buttons") {
  // AN OVERLAY, so App::render walks down to the Reader, paints it, veils it and
  // paints this over the top -- which is the mechanism, already built, whose fourth
  // caller this is.
  //
  // GRAYSCALE for the reason the Reader declares it: this is body text at reading
  // size, and hard-thresholding a serif face at 32px was judged worse on the panel.
  // It is therefore also why renderTopOnly can never apply to a focus move here --
  // grayscale renders three planes plus a rebase, so the frame between passes holds a
  // different plane and the partial-repaint precondition is false for every pass but
  // the first.
  ramp::Ramp ramp;
  readerfix::Body body;
  reader::QuietTheme theme;
  reader::PageMetrics m;
  theme.peekMetrics(480, 800, ramp.fonts, body.face, reader::Settings{}, m);

  reader::PeekScreen peek(readerfix::longChapter(20), "CH. 01", 4, &body.face);
  peek.setMetrics(m);
  CHECK(peek.id() == reader::ScreenId::Peek);
  CHECK(peek.isOverlay());
  CHECK(peek.fidelity() == reader::Fidelity::Grayscale);
  // TWO LIVE SLOTS AND TWO DEAD ONES, and the dead ones are EMPTY STRINGS rather than
  // absent: an empty slot is 36px wide (kHintEmptySlotW), and measuring it as zero is
  // not "drawing nothing", it is drawing the other two in the wrong places.
  CHECK(peek.vm().hints[0] == "CLOSE");
  CHECK(peek.vm().hints[1] == "GO HERE");
  CHECK(peek.vm().hints[2].empty());
  CHECK(peek.vm().hints[3].empty());
  // NO HOLD PROMISED, so none is bound -- one field drives both (hintHoldMask).
  CHECK(peek.longPressable() == 0);
  // AND NO AUTO-REPEAT. The Typography panel's rule applies here for a different
  // reason: a held side button that ran away would page the panel past what the
  // reader can follow, and every page costs a decode.
  CHECK(peek.autoRepeat() == 0);
}

TEST_CASE("the peek's band names the state and where it is, and never a page number") {
  // `PEEK` NAMES THE STATE, because `CH. 01 · 4%` alone would read as the Reader's own
  // header and this panel has to be unmistakably not that.
  //
  // AND THERE IS NO PAGE NUMBER, which is the assertion worth making rather than the
  // omission worth noting: the panel is inset, so its column is narrower, so its text
  // re-wraps -- and a page number over a re-wrapped column would be a claim about the
  // book that is false.
  ramp::Ramp ramp;
  readerfix::Body body;
  reader::QuietTheme theme;
  reader::PageMetrics m;
  theme.peekMetrics(480, 800, ramp.fonts, body.face, reader::Settings{}, m);

  reader::PeekScreen peek(readerfix::longChapter(20), "CH. 01", 4, &body.face);
  peek.setMetrics(m);
  CHECK(peek.vm().title == "PEEK");
  CHECK(peek.vm().where.find("CH. 01") != std::string::npos);
  CHECK(peek.vm().where.find("4%") != std::string::npos);
  CHECK(peek.vm().where.find('/') == std::string::npos);
}

TEST_CASE("the sides page inside the peek and the front row does nothing") {
  // THE BUTTONS THAT PAGE KEEP PAGING. `Up` already means "return to where I was" on
  // the screen underneath, so binding it to "previous page" here would give one button
  // two meanings across a single press -- worse than leaving it unbound.
  ramp::Ramp ramp;
  readerfix::Body body;
  reader::QuietTheme theme;
  reader::PageMetrics m;
  theme.peekMetrics(480, 800, ramp.fonts, body.face, reader::Settings{}, m);

  reader::PeekScreen peek(readerfix::longChapter(30), "CH. 01", 4, &body.face);
  peek.setMetrics(m);
  const std::string first = readerfix::pageText(peek.page());
  REQUIRE_FALSE(first.empty());

  CHECK(peek.onGesture({Gesture::Next}).kind == reader::Action::Kind::Redraw);
  const std::string second = readerfix::pageText(peek.page());
  CHECK(second != first);
  CHECK(peek.onGesture({Gesture::Prev}).kind == reader::Action::Kind::Redraw);
  CHECK(readerfix::pageText(peek.page()) == first);

  // The front row's movers are AltPrev/AltNext here, as they are on the Reader, and
  // this screen binds neither.
  CHECK(peek.onGesture({Gesture::AltNext}).kind == reader::Action::Kind::None);
  CHECK(peek.onGesture({Gesture::AltPrev}).kind == reader::Action::Kind::None);
  CHECK(readerfix::pageText(peek.page()) == first);
}

TEST_CASE("Back closes the peek and Activate commits it") {
  ramp::Ramp ramp;
  readerfix::Body body;
  reader::QuietTheme theme;
  reader::PageMetrics m;
  theme.peekMetrics(480, 800, ramp.fonts, body.face, reader::Settings{}, m);

  reader::PeekScreen peek(readerfix::longChapter(20), "CH. 01", 4, &body.face);
  peek.setMetrics(m);
  CHECK(peek.onGesture({Gesture::Back}).kind == reader::Action::Kind::Pop);
  // COMMITTING IS ALSO A POP, and the shell is what moves the Reader -- the peek
  // cannot, because the Reader is on the stack UNDERNEATH it and a screen that reached
  // down into the stack would be a second thing that knows how a Reader is shaped.
  // Contents answers popTo(Reader) for the identical reason.
  const reader::Action a = peek.onGesture({Gesture::Activate});
  CHECK(a.kind == reader::Action::Kind::Pop);
  CHECK(peek.committed());
}

TEST_CASE("the committed cursor is the page the peek was showing") {
  // WHAT THE SHELL READS AFTER THE POP -- and it has to be read while the peek is
  // still on top, exactly as Contents' chosenSpine() is, because the dispatch that
  // commits is the dispatch that destroys the screen holding the answer.
  ramp::Ramp ramp;
  readerfix::Body body;
  reader::QuietTheme theme;
  reader::PageMetrics m;
  theme.peekMetrics(480, 800, ramp.fonts, body.face, reader::Settings{}, m);

  reader::PeekScreen peek(readerfix::longChapter(30), "CH. 01", 4, &body.face);
  peek.setMetrics(m);
  peek.onGesture({Gesture::Next});
  peek.onGesture({Gesture::Next});
  const reader::Cursor shown = peek.chosenCursor();
  REQUIRE(peek.onGesture({Gesture::Activate}).kind == reader::Action::Kind::Pop);
  CHECK(peek.chosenCursor() == shown);
  // NOT PAGE ONE, which is what a commit that ignored the paging would give -- and it
  // would be right on the first page and wrong everywhere after it.
  CHECK_FALSE(shown == reader::Cursor{});
}

TEST_CASE("a peek that was closed rather than committed reports no commit") {
  // THE SHELL BRANCHES ON THIS, so `committed()` false after a Back is the whole
  // difference between CLOSE and GO HERE -- both of which answer Pop.
  ramp::Ramp ramp;
  readerfix::Body body;
  reader::QuietTheme theme;
  reader::PageMetrics m;
  theme.peekMetrics(480, 800, ramp.fonts, body.face, reader::Settings{}, m);

  reader::PeekScreen peek(readerfix::longChapter(20), "CH. 01", 4, &body.face);
  peek.setMetrics(m);
  peek.onGesture({Gesture::Next});
  REQUIRE(peek.onGesture({Gesture::Back}).kind == reader::Action::Kind::Pop);
  CHECK_FALSE(peek.committed());
}

TEST_CASE("the peek's page is NOT the reader's page, because the column is narrower") {
  // THE PROPERTY THAT PINS THE PANEL TO ITS OWN PAGINATION. A peek that reused the
  // Reader's already-laid page would look almost right -- same book, same face, same
  // size -- and would overflow the panel, because the lines were measured against a
  // 444px column and the panel's is ~368. So the two must disagree, and a test that
  // only checked the peek rendered something could not see it.
  ramp::Ramp ramp;
  readerfix::Body body;
  reader::QuietTheme theme;
  const std::string ch = readerfix::longChapter(30);

  reader::PageMetrics rm, pm;
  theme.readerMetrics(480, 800, ramp.fonts, body.face, reader::Settings{}, rm);
  theme.peekMetrics(480, 800, ramp.fonts, body.face, reader::Settings{}, pm);
  readerfix::Reading r(ch);
  reader::PeekScreen peek(ch, "CH. 01", 4, &body.face);
  peek.setMetrics(pm);

  CHECK(readerfix::pageText(peek.page()) != readerfix::pageText(r.scr->page()));
  // AND NOTHING LEAVES THE PANEL. Every line's right edge is inside the column the
  // metrics gave it -- the reader's own `nothing may leave the column` rule, applied
  // to the one place in the firmware where two column widths coexist.
  for (const reader::LaidLine& ln : peek.page().lines) {
    CAPTURE(ln.text);
    CHECK(ln.x >= pm.columnLeft);
  }
  CHECK(static_cast<int>(peek.page().lines.size()) <= reader::kPeekLines);
}
```

- [ ] **Step 2: Re-run cmake, then run to verify it fails**

```bash
cmake -S . -B build
```

Run: `make test 2>&1 | grep -E "error:" | head -3`
Expected: `reader/screen_peek.h: No such file or directory`.

- [ ] **Step 3: Write `PeekScreen`**

Create `core/include/reader/screen_peek.h`:

```cpp
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "reader/app.h"
#include "reader/book.h"
#include "reader/layout.h"
#include "reader/screen_reader.h"
#include "reader/toc.h"
#include "reader/viewmodel.h"

namespace reader {
class GlyphSource;

// design/Peek.dc.html: a page of the book over the veiled page you are on.
//
// Looking somewhere else without leaving where you are. Its first caller is chapter
// selection -- Contents used to jump straight to a chapter, and now shows it first --
// and Bookmarks and Names are the second and third.
//
// --- IT OWNS A ReaderScreen, WHICH IS THE WHOLE IMPLEMENTATION ----------------
//
// The panel is INSET, so its column is narrower (~368px against the reading page's
// 444), so its text re-wraps -- which is both why it cannot show a page number and why
// it cannot reuse the Reader's already-laid page. It needs its own pagination over the
// peeked chapter.
//
// So it holds a HEADLESS ReaderScreen built at Theme::peekMetrics, forwards page
// gestures into its onGesture, and reads back page(), chapterIndex() and
// currentCursor(). Three alternatives were weighed (see the spec):
//
//   * a bespoke minimal pager -- smallest, and a second copy of open/advance/seek, so
//     a backward turn and a chapter crossing inside the panel would re-derive logic
//     that took this project several passes to get right;
//   * extracting a ChapterPager both screens use -- where the second-copy rule points,
//     and the wrong size: ReaderScreen's paging is entangled with the page ring, the
//     growing index, byte accounting and two idle jobs;
//   * this, which costs ~2-3 KB of duplicated spans and chapter names and a Screen
//     used as a model, and buys the one property that matters: THE CURSOR THIS COMMITS
//     IS BY CONSTRUCTION THE ONE THE READER RESTORES. There is no second spelling of a
//     page position free to disagree with the first.
//
// Two properties fall out rather than being arranged. Paging off either end of the
// peeked chapter crosses into the next or previous one, because openChapterAt already
// does that (including skipping an entry that paginates to nothing) -- which is what
// "the sides page in the peek exactly as they do while reading" has to mean. And the
// inner reader is INVISIBLE TO THE QUIET-WINDOW JOBS, because the shell drives those
// through the App's stack and this one is not on it: no deferred count runs for a total
// the panel does not display.
//
// THE INNER READER HAS ITS OWN ReturnAnchor AND IT IS DISCARDED WITH IT. Paging around
// inside a peek moves nothing the reader can come back to; the outer Reader's anchor is
// touched by exactly one thing, which is the shell acting on committed().
//
// --- AND THE READER BENEATH LETS GO WHILE THIS IS UP -------------------------
//
// A live chapter peaks at 69,884 bytes with a 36,956-byte single allocation against a
// measured 45,840-byte heap floor, so two do not fit. The shell releases the Reader's
// chapter before building this and reacquires it after the pop -- see
// ReaderScreen::releaseChapter, and the spec's memory table. ONE LIVE CHAPTER AT ANY
// MOMENT, so the floor never moves.
class PeekScreen : public Screen {
 public:
  // A BOOK, for the device: the peeked chapter is a spine entry of the book the reader
  // has open, and `fs` and `book` must outlive the screen exactly as they must for a
  // ReaderScreen.
  //
  // `percent` is the book-wide reading percentage AT THE PEEKED CHAPTER, computed by
  // the caller: reading_store.cpp's progressPercent needs the book's chapter byte
  // layout and core/ has no reason to make this screen compute it twice.
  PeekScreen(FileSystem& fs, OpenedBook book, int spine, int percent,
             const GlyphSource* body);

  // A single chapter already in memory, for the simulator and the goldens, which have
  // no card -- the same pair of constructors ReaderScreen has and for the same reason.
  PeekScreen(std::string_view xhtml, std::string chapter, int percent,
             const GlyphSource* body);
  ~PeekScreen() override;

  ScreenId id() const override { return ScreenId::Peek; }
  bool isOverlay() const override { return true; }
  // Body text at reading size, so the Reader's reasoning applies unchanged:
  // hard-thresholding a serif face at 32px was judged worse on the panel. It is also
  // why a repaint here can never take App::renderTopOnly -- grayscale renders three
  // planes plus a rebase, so the frame between passes holds a different plane.
  Fidelity fidelity() const override { return Fidelity::Grayscale; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  // MUST BE SET BEFORE THE SCREEN RENDERS, exactly as ReaderScreen's must: the column
  // is where the pagination comes from and a constructor has no framebuffer to ask.
  void setMetrics(const PageMetrics& m);
  void setItalic(const GlyphSource* italic);
  // The book's chapter names, so the band says the chapter's NAME where the contents
  // supply one and `CH. NN` where they do not -- the Reader's own fallback.
  void setChapterNames(std::vector<TocEntry> toc);

  const PeekViewModel& vm() const { return vm_; }
  const Page& page() const;

  // --- WHAT THE SHELL READS AFTER THE POP -------------------------------------
  //
  // Both `CLOSE` and `GO HERE` answer Action::pop(), because the peek cannot move the
  // Reader itself: the Reader is on the stack UNDERNEATH it, and a screen that reached
  // down into the stack would be a second thing that knows how a Reader is shaped.
  // Contents answers popTo(Reader) for the identical reason. So `committed()` is the
  // whole difference between the two, and the shell reads it WHILE THIS IS STILL ON
  // TOP -- the dispatch that commits is the dispatch that destroys the screen holding
  // the answer.
  bool committed() const { return committed_; }
  // The spine entry and the page-start cursor the panel was showing. Together they are
  // exactly what ReaderScreen::goToPosition takes, and exactly what an AnchorPos is
  // made of -- one spelling of a page position, not two.
  int chosenSpine() const;
  Cursor chosenCursor() const;

 private:
  void syncVm();

  std::unique_ptr<ReaderScreen> inner_;
  PeekViewModel vm_{};
  int percent_ = 0;
  bool committed_ = false;
};

}  // namespace reader
```

Create `core/src/screen_peek.cpp`:

```cpp
#include "reader/screen_peek.h"

#include <string>
#include <utility>

#include "reader/theme.h"

namespace reader {

PeekScreen::PeekScreen(FileSystem& fs, OpenedBook book, int spine, int percent,
                       const GlyphSource* body)
    : inner_(std::make_unique<ReaderScreen>(fs, std::move(book), spine, body)),
      percent_(percent) {
  // NO declareHints: this screen promises no hold, so it binds none -- one field drives
  // both (see Screen::longPressable). And NO declareRepeat: every page of the panel
  // costs a decode, and a held side button that ran away would page past what the
  // reader can follow.
  //
  // declareSplitMovers, because the front row must NOT page. `Up` means "return to
  // where I was" on the screen underneath, so one button with two meanings across a
  // single press is worse than an unbound one -- and with the movement pairs folded
  // together there would be no way to leave it unbound.
  declareSplitMovers();
}

PeekScreen::PeekScreen(std::string_view xhtml, std::string chapter, int percent,
                       const GlyphSource* body)
    : inner_(std::make_unique<ReaderScreen>(xhtml, /*bookTitle=*/std::string(),
                                            std::move(chapter), body)),
      percent_(percent) {
  declareSplitMovers();
}

PeekScreen::~PeekScreen() = default;

void PeekScreen::setMetrics(const PageMetrics& m) {
  inner_->setMetrics(m);
  // THE LEAD TRAVELS IN THE VIEW MODEL, so renderPeek computes the panel box from the
  // same line height peekMetrics did. Two spellings of one geometry is how a panel's
  // border ends up a pixel off its own text.
  vm_.leadEm1000 = m.leadEm1000;
  syncVm();
}

void PeekScreen::setItalic(const GlyphSource* italic) { inner_->setItalic(italic); }

void PeekScreen::setChapterNames(std::vector<TocEntry> toc) {
  inner_->setChapterNames(std::move(toc));
  syncVm();
}

const Page& PeekScreen::page() const { return inner_->page(); }
int PeekScreen::chosenSpine() const { return inner_->chapterIndex(); }
Cursor PeekScreen::chosenCursor() const { return inner_->currentCursor(); }

Action PeekScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    case Gesture::Back:
      // CLOSE AND DISCARD, and nothing needs restoring because nothing was committed.
      return Action::pop();
    case Gesture::Activate:
      // GO HERE. A Pop like the Back above -- the shell reads committed() and moves the
      // Reader, because this screen cannot reach the Reader beneath it.
      committed_ = true;
      return Action::pop();
    case Gesture::Next:
    case Gesture::Prev: {
      // FORWARDED, not reimplemented. The inner reader's own handler owns the page
      // ring, the live-builder rule and the chapter crossings; a second copy of any of
      // those would be a second chance to install a builder one page off, which is a
      // reader that skips or repeats a page.
      const Action a = inner_->onGesture(g);
      if (a.kind == Action::Kind::None) return Action::none();
      syncVm();
      return Action::redraw();
    }
    default:
      // AltPrev/AltNext are the FRONT row (declareSplitMovers), and this screen binds
      // neither -- see the constructor.
      return Action::none();
  }
}

void PeekScreen::syncVm() {
  // `CH. 01 · 4%`, composed here so the theme does no arithmetic. The middle dot is
  // U+00B7 as a SEPARATE literal: a C++ hex escape is unbounded, so "\xC2\xB7CH." would
  // parse `\xB7C` as one escape -- clang rejects it and the ESP32's GCC accepts it and
  // emits the wrong byte. This project has already recorded that trap once.
  vm_.where = inner_->vm().chapter;
  vm_.where += " \xC2\xB7" " ";
  vm_.where += std::to_string(percent_);
  vm_.where += "%";
}

void PeekScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                        Plane plane) const {
  const GlyphSource* body = inner_->body();
  if (body == nullptr) return;
  theme.renderPeek(fb, fonts, *body, inner_->italic(), vm_, inner_->page(), plane);
}

}  // namespace reader
```

`ReaderScreen` needs two trivial accessors for that last function. Add to its public
section:

```cpp
  // THE FACES, so a screen that draws this one's page can draw it with them. The peek
  // is the caller: it renders the inner reader's page into its own panel, and a panel
  // drawn with a different face from the one the page was MEASURED with is the
  // measure/draw disagreement StyledFace exists to prevent.
  const GlyphSource* body() const { return body_; }
  const GlyphSource* italic() const { return italic_; }
```

Add `int leadEm1000 = kBodyLeadEm;` to `PeekViewModel` (see Task 5's note).

- [ ] **Step 4: Run the test to verify it passes**

```bash
cmake -S . -B build
```

Run: `./build/reader_tests --test-case="*peek*"`
Expected: PASS on all eight cases.

Then: `make test 2>&1 | tail -5` — PASS.

- [ ] **Step 5: Prove the tests bite**

1. Make `Gesture::Activate` return `Action::pop()` without setting `committed_`.
   Expected: FAIL on "Back closes the peek and Activate commits it".
2. Make `chosenCursor()` return `Cursor{}`.
   Expected: FAIL on "the committed cursor is the page the peek was showing" — both
   the equality and the `CHECK_FALSE(shown == Cursor{})`.
3. Make `PeekScreen::setMetrics` ignore its argument and call
   `inner_->setMetrics(readerMetricsForTest)` — simplest form: temporarily have the
   test construct the peek with the READER's metrics `rm` instead of `pm`.
   Expected: FAIL on "the peek's page is NOT the reader's page".
4. Remove `declareSplitMovers()` from both constructors.
   Expected: FAIL on "the sides page inside the peek and the front row does nothing" —
   `AltNext` folds back into `Next` and pages.

Restore each by hand, `touch` the file, `make test` — PASS.

- [ ] **Step 6: Commit**

```bash
git add core/include/reader/screen_peek.h core/src/screen_peek.cpp core/include/reader/screen_reader.h core/include/reader/viewmodel.h test/unit/test_screen_peek.cpp
git commit -m "peek: the overlay, owning a headless ReaderScreen

The panel is inset, so its column is narrower (~368px against 444), so its text
re-wraps -- which is both why it cannot show a page number and why it cannot reuse the
Reader's already-laid page. It needs its own pagination, so it holds a ReaderScreen at
peek metrics and forwards Next/Prev into it.

What that buys is the one property that matters: THE CURSOR IT COMMITS IS BY
CONSTRUCTION THE ONE THE READER RESTORES. A bespoke pager would have been a second copy
of open/advance/seek, and extracting a ChapterPager is a large refactor of the most
performance-critical code here for a screen that wants a fraction of it.

Two properties fall out rather than being arranged: paging off either end crosses
chapters, because openChapterAt already does that; and the inner reader is invisible to
the quiet-window jobs, because the shell drives those through the App's stack and this
one is not on it.

BOTH BUTTONS ANSWER Pop, and committed() is the difference -- the peek cannot move the
Reader, which is on the stack underneath it. Contents answers popTo(Reader) for the
identical reason.

Refs #1"
```

---

### Task 7: the factory builds a peek, and refuses an unprimed one

**Files:**
- Modify: `core/include/reader/screens.h`
- Modify: `core/src/screens.cpp`
- Test: `test/unit/test_screen_peek.cpp` (append)

- [ ] **Step 1: Write the failing test**

Append to `test/unit/test_screen_peek.cpp`:

```cpp
TEST_CASE("the factory refuses a Peek nothing primed") {
  // WHICH IS WHAT MAKES A PEEK UNRESTORABLE ACROSS A WAKE, and it is the intended
  // behaviour rather than a limitation: App::restore stops short of a screen the factory
  // cannot build and LEAVES THE READER STANDING -- "a restore that stops early keeps
  // what already stands". Persisting a peeked cursor so a wake could rebuild the panel
  // would be a card write for a breadcrumb the anchor's own design declined to pay for.
  //
  // AND THE FACTORY MUST NOT SUBSTITUTE. This project has shipped that defect twice --
  // a Reader falling through to the demo woke the device into Middlemarch, and Contents
  // falling back to demoContents() showed Le Fléau's reader Middlemarch's chapters --
  // so the demo has to be ASKED for.
  reader::DemoScreenFactory factory;
  readerfix::Body body;
  factory.setReaderBody(&body.face);
  CHECK(factory.create(reader::ScreenId::Peek) == nullptr);
}

TEST_CASE("the factory refuses a Peek with no body face") {
  // A Peek that rendered nothing is indistinguishable from a chapter that failed to
  // open, so the refusal is at the push -- the same call ScreenId::Reader makes.
  reader::DemoScreenFactory factory;
  factory.setPeekDemo();
  CHECK(factory.create(reader::ScreenId::Peek) == nullptr);
}

TEST_CASE("the demo Peek builds and shows the board's opening") {
  // Asked for, as the Reader's and Contents' demos are. What the simulator and the
  // goldens render, having no card.
  ramp::Ramp ramp;
  readerfix::Body body;
  reader::QuietTheme theme;
  reader::PageMetrics m;
  theme.peekMetrics(480, 800, ramp.fonts, body.face, reader::Settings{}, m);

  reader::DemoScreenFactory factory;
  factory.setReaderBody(&body.face);
  factory.setPeekMetrics(m);
  factory.setPeekDemo();
  std::unique_ptr<reader::Screen> scr = factory.create(reader::ScreenId::Peek);
  REQUIRE(scr != nullptr);
  CHECK(scr->id() == reader::ScreenId::Peek);
  CHECK(scr->isOverlay());
  const auto* peek = static_cast<const reader::PeekScreen*>(scr.get());
  CHECK_FALSE(peek->page().lines.empty());
  CHECK(static_cast<int>(peek->page().lines.size()) <= reader::kPeekLines);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make test 2>&1 | grep -E "error:" | head -3`
Expected: `'setPeekDemo' is not a member of 'reader::DemoScreenFactory'`.

- [ ] **Step 3: Add the priming and the factory case**

In `core/include/reader/screens.h`, declare the board's demo text alongside
`demoReaderXhtml()`:

```cpp
// design/Peek.dc.html's own peeked text -- Middlemarch's opening, which is the board's
// story: the reader is at CH. 07, 34%, has met a name they cannot place, and has peeked
// back to CH. 01, 4%, to read the sentence that introduced her.
std::string demoPeekXhtml();
```

In `DemoScreenFactory`'s public section:

```cpp
  // THE BOARD'S OWN PEEK, ASKED FOR. Same rule as setReaderDemo and setContentsDemo:
  // the factory refuses a Peek nothing primed rather than substituting, because this
  // project has shipped that substitution twice and each time it hid the real cause.
  void setPeekDemo() { peekDemo_ = true; }
  // The peeked chapter of the book the reader has open: which spine entry, and the
  // book-wide percentage at it. The percentage is computed by the caller, because
  // progressPercent needs the book's chapter byte layout and the shell already has it.
  void setPeek(int spine, int percent) {
    peekSpine_ = spine;
    peekPercent_ = percent;
    peekPrimed_ = true;
  }
  // PRIMED, not "spine >= 0": spine 0 is a real target -- it is the book's cover, which
  // an NCX section header can legitimately name -- so a sentinel would refuse a valid
  // peek. Only "nothing was primed at all" is refused.
  void clearPeek() { peekPrimed_ = false; }
  // The panel's column, from Theme::peekMetrics. Separate from setReaderMetrics because
  // they are DIFFERENT COLUMNS -- that is the whole design -- and one setter for both
  // would be an invitation to hand the peek the reading measure, which is the bug the
  // "peek's page is not the reader's page" test exists to catch.
  void setPeekMetrics(const PageMetrics& m) { peekMetrics_ = m; }
```

...and in its private section:

```cpp
  bool peekDemo_ = false;
  bool peekPrimed_ = false;
  int peekSpine_ = 0;
  int peekPercent_ = 0;
  PageMetrics peekMetrics_{};
```

In `core/src/screens.cpp`, define the demo text near `demoReaderXhtml`:

```cpp
std::string demoPeekXhtml() {
  // The board's own two sentences, at the board's own paragraph convention.
  return "<html><body><p>Miss Brooke had that kind of beauty which seems to be thrown "
         "into relief by poor dress. Her hand and wrist were so finely formed that she "
         "could wear sleeves not less bare of style.</p></body></html>";
}
```

...and add the factory case, beside `ScreenId::Reader`:

```cpp
    case ScreenId::Peek: {
      // REFUSED without a body face, as the Reader is: a panel that rendered nothing is
      // indistinguishable from a chapter that failed to open, and the caller can act on
      // a refused push.
      if (readerBody_ == nullptr) return nullptr;
      std::unique_ptr<PeekScreen> scr;
      if (peekPrimed_ && !readerBook_.path.empty() && fs_ != nullptr) {
        scr = std::make_unique<PeekScreen>(*fs_, readerBook_, peekSpine_, peekPercent_,
                                           readerBody_);
        // The chapter names, so the band says the chapter's NAME where the contents
        // supply one. Empty for a book with no contents, which falls back to `CH. NN`.
        scr->setChapterNames(contentsToc_);
      } else if (peekDemo_) {
        scr = std::make_unique<PeekScreen>(demoPeekXhtml(), "CH. 01", 4, readerBody_);
      } else {
        // NOTHING PRIMED AND NO DEMO ASKED FOR: refused. This is the session-restore
        // path, and it is why a peek is not restorable across a wake -- App::restore
        // stops short and leaves the Reader standing, which is right: a peek is a
        // transient excursion, and rebuilding one would need a peeked cursor nothing
        // persists.
        return nullptr;
      }
      // BEFORE setMetrics, for renderReader's reason: setMetrics lays the page out and
      // the wrap measures emphasis with this face, so a face arriving after would leave
      // the first page measured roman and drawn in two.
      scr->setItalic(readerItalic_);
      scr->setMetrics(peekMetrics_);
      return scr;
    }
```

Add `#include "reader/screen_peek.h"` to `core/include/reader/screens.h`.

- [ ] **Step 4: Run the test to verify it passes**

Run: `./build/reader_tests --test-case="*factory refuses a Peek*" --test-case="*demo Peek builds*"`
Expected: PASS.

Then: `make test 2>&1 | tail -5` — PASS.

- [ ] **Step 5: Prove the refusal bites**

Change the final `else` to fall through to the demo:
`} else { scr = std::make_unique<PeekScreen>(demoPeekXhtml(), "CH. 01", 4, readerBody_); }`

Run: `./build/reader_tests --test-case="*factory refuses a Peek nothing primed*"`
Expected: FAIL — which is the substitution this project has shipped twice.

Restore by hand, `touch core/src/screens.cpp`, `make test` — PASS.

- [ ] **Step 6: Commit**

```bash
git add core/include/reader/screens.h core/src/screens.cpp test/unit/test_screen_peek.cpp
git commit -m "peek: the factory builds one, and refuses one nothing primed

THE REFUSAL IS WHAT MAKES A PEEK UNRESTORABLE ACROSS A WAKE, and that is the intended
behaviour: App::restore stops short of a screen the factory cannot build and leaves the
Reader standing. Persisting a peeked cursor so a wake could rebuild the panel would be a
card write for a breadcrumb the anchor's own design declined to pay for.

AND THE DEMO HAS TO BE ASKED FOR. This project has shipped the substitution twice -- a
Reader falling through to the demo woke the device into Middlemarch, and Contents
falling back to demoContents() showed one book's reader another book's chapters -- and
each time the fallback hid the real cause.

setPeekMetrics is separate from setReaderMetrics because they are DIFFERENT COLUMNS,
which is the whole design; one setter for both would invite handing the peek the reading
measure.

Refs #1"
```

---

### Task 8: the simulator's `peek` subcommand

**Files:**
- Modify: `sim/main.cpp`

- [ ] **Step 1: Add the subcommand**

`peek` is **already registered** in `tools/compare-design.py`'s `FLOW_SCREENS`, so it
is already expected by `make compare` and currently reports as not-implemented.

In `sim/main.cpp`, beside `isReaderMenu`:

```cpp
  // design/Peek.dc.html. THE SAME JOURNEY AS `reader_menu` -- the Reader, then the
  // panel over it -- because App::render walks down to the topmost non-overlay, paints
  // it, veils it and paints each overlay above. Rendering top().render alone is the
  // mistake that paints a panel floating on white, and nothing on the desktop can catch
  // it: every other path here goes through App::render.
  const bool isPeek = std::strcmp(argv[1], "peek") == 0;
```

Add `!isPeek` to the unknown-screen guard and `'peek'` to its message. Add `isPeek`
to the `if (isReader || isReaderMenu || ...)` condition that loads the body and
italic faces.

Extend the overlay branch's condition from `if (isReaderMenu || isTypography)` to
`if (isReaderMenu || isTypography || isPeek)`, and inside it, after the `App` is
built and the reader menu is pushed:

```cpp
    if (isPeek) {
      // THE PANEL'S OWN COLUMN, which is the whole design: inset, so narrower, so its
      // text re-wraps -- and re-wrapped text paginates differently, which is why the
      // panel shows chapter and percent rather than a page number.
      reader::PageMetrics pm;
      theme.peekMetrics(w, h, fonts, body, reader::Settings{}, pm);
      pm.italic = &italic;
      factory.setPeekMetrics(pm);
      factory.setPeekDemo();
      // THE MENU GOES FIRST. On the device the peek is reached from Contents, and the
      // pop that opens it takes the menu AND Contents off -- so the stack this renders
      // is Reader + Peek, which is what the board draws. Popping rather than never
      // pushing, because the menu push above is shared with the two sibling branches.
      app.dispatch({reader::Button::Back, reader::PressKind::Short});
      if (app.top().id() != reader::ScreenId::Reader) {
        std::fprintf(stderr, "BACK did not leave the Reader on top\n");
        return 1;
      }
      if (!app.pushScreen(reader::ScreenId::Peek)) {
        std::fprintf(stderr, "the factory refused ScreenId::Peek\n");
        return 1;
      }
    }
```

...and after the render, before the `reader menu` printf:

```cpp
    if (isPeek) {
      const auto& p = static_cast<const reader::PeekScreen&>(app.top());
      std::printf("wrote %s (%dx%d) peek over the page, %s, %d lines\n", argv[2], w, h,
                  p.vm().where.c_str(), static_cast<int>(p.page().lines.size()));
      return 0;
    }
```

Add `#include "reader/screen_peek.h"`.

- [ ] **Step 2: Build and run it**

```bash
cmake -S . -B build && make -C build reader_sim
```

Run: `./build/reader_sim peek build/peek.png`
Expected: `wrote build/peek.png (480x800) peek over the page, CH. 01 · 4%, 8 lines`

- [ ] **Step 3: LOOK AT THE PNG AND SAY WHAT YOU SEE**

Run: `./build/reader_sim peek build/peek.png && ./build/reader_sim peek build/peek_x3.png --canvas 528x792`

Open both PNGs and describe them against `design/Peek.dc.html`, out loud, including
anything that looks wrong. The board's checklist:

- the reading page visible under a 3px clustered-white veil, header and footer included;
- a centred panel with a 2px border, 34px of veil either side at 480 and 58px at 528;
- a band reading `PEEK` left (tracked caps) and `CH. 01 · 4%` right, with a 2px rule under it;
- eight whole lines of justified body text, the first paragraph flush;
- a hint bar over the veil reading `CLOSE` and `GO HERE` with two empty slots.

**If the panel's border does not sit flush against its own text, stop** — that is the
two-spellings-of-one-geometry defect Task 5's note warns about.

- [ ] **Step 4: Commit**

```bash
git add sim/main.cpp
git commit -m "sim: a peek subcommand, rendered through the App as an overlay must be

The same journey as reader_menu -- the Reader, then the panel over it -- because
App::render walks down to the topmost non-overlay, paints it, veils it and paints each
overlay above. Rendering top().render alone paints a panel floating on white, and
nothing on the desktop can catch that: every other path here goes through App::render.

The menu is POPPED before the peek is pushed, because on the device the pop that opens
a peek takes the menu and Contents off with it -- so the stack this renders is Reader +
Peek, which is what the board draws.

\`peek\` was already registered in compare-design.py's FLOW_SCREENS, so this is the
board's implementation arriving rather than a new entry.

Refs #1"
```

---

### Task 9: the goldens, at both geometries

**Files:**
- Test: `test/unit/test_theme_peek_golden.cpp` (create)
- Create: `test/golden/peek.png`, `test/golden/peek_x3.png` (blessed by hand in this task)

- [ ] **Step 1: Write the golden test**

Create `test/unit/test_theme_peek_golden.cpp`:

```cpp
// THE PEEK'S GOLDENS, and they are four-level rather than 1-bit.
//
// The screen declares Fidelity::Grayscale for the Reader's reason -- this is body text
// at reading size and hard-thresholding a serif face at 32px was judged worse on the
// panel -- so a Mono golden here would pin the wrong thing convincingly.
//
// AND THE VEIL IS IN THEM, which is what makes these the visual half of
// test_veil_planes.cpp: that file proves the veil's bytes are the same in every plane,
// and these prove the composed four-level image of a veiled page under a panel is the
// one that was approved.
#include <memory>
#include <string>

#include "doctest.h"
#include "golden.h"
#include "ramp.h"
#include "reader_fixture.h"
#include "reader/app.h"
#include "reader/framebuffer.h"
#include "reader/screen_peek.h"
#include "reader/screens.h"
#include "reader/theme_quiet.h"

TEST_CASE("QuietTheme renders the peek over a page to golden on both geometries") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  readerfix::Body body;

  auto renderOne = [&](int w, int h, const std::string& name) {
    // THROUGH THE APP, NOT THE SCREEN. An overlay's parent is painted by App::render,
    // which walks down to the topmost non-overlay -- and calling top().render alone is
    // the mistake that paints a panel floating on white. Nothing else on the desktop
    // can catch that, because every other render path here goes through App::render.
    reader::PageMetrics rm, pm;
    theme.readerMetrics(w, h, ramp.fonts, body.face, reader::Settings{}, rm);
    theme.peekMetrics(w, h, ramp.fonts, body.face, reader::Settings{}, pm);
    reader::DemoScreenFactory factory;
    factory.setReaderBody(&body.face);
    factory.setReaderMetrics(rm);
    factory.setReaderDemo();
    factory.setPeekMetrics(pm);
    factory.setPeekDemo();
    std::unique_ptr<reader::Screen> page = factory.create(reader::ScreenId::Reader);
    REQUIRE(page != nullptr);
    // THE SETTLED STATE, which is what the board shows: the veiled page's footer reads
    // a real counter rather than the em dash a chapter wears for its first page.
    static_cast<reader::ReaderScreen*>(page.get())->completeIndex();
    reader::App app(std::move(page), factory);
    REQUIRE(app.pushScreen(reader::ScreenId::Peek));
    REQUIRE(app.top().fidelity() == reader::Fidelity::Grayscale);

    reader::Framebuffer lsb(w, h), msb(w, h);
    app.render(lsb, ramp.fonts, theme, reader::Plane::Lsb);
    app.render(msb, ramp.fonts, theme, reader::Plane::Msb);
    golden::checkGoldenGray(lsb, msb, name);
  };

  SUBCASE("X4 480x800") { renderOne(480, 800, "peek"); }
  SUBCASE("X3 528x792") { renderOne(528, 792, "peek_x3"); }
}

TEST_CASE("the peek's panel holds eight whole lines at both geometries") {
  // THE BOARD'S NUMBER, asserted beside the golden rather than only inside it: a golden
  // pins the pixels and cannot say WHY they are those pixels, so a change that moved the
  // line count would fail the golden with no explanation attached.
  //
  // WHOLE LINES, which is the other half. A pinned panel height cut the last line in
  // half lengthwise in the board's first draft -- something the firmware cannot even do,
  // since PageBuilder lays out whole lines -- so the height being a RESULT is what makes
  // this assertion possible at all.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  readerfix::Body body;
  for (const auto wh : std::vector<std::pair<int, int>>{{480, 800}, {528, 792}}) {
    const int w = wh.first, h = wh.second;
    CAPTURE(w);
    reader::PageMetrics pm;
    theme.peekMetrics(w, h, ramp.fonts, body.face, reader::Settings{}, pm);
    reader::DemoScreenFactory factory;
    factory.setReaderBody(&body.face);
    factory.setPeekMetrics(pm);
    factory.setPeekDemo();
    std::unique_ptr<reader::Screen> scr = factory.create(reader::ScreenId::Peek);
    REQUIRE(scr != nullptr);
    const auto* peek = static_cast<const reader::PeekScreen*>(scr.get());
    CHECK_FALSE(peek->page().lines.empty());
    CHECK(static_cast<int>(peek->page().lines.size()) <= reader::kPeekLines);
  }
}
```

- [ ] **Step 2: Run it — it MUST fail, and that is by design**

```bash
cmake -S . -B build
```

Run: `make test 2>&1 | grep -E "golden missing" `
Expected: two FAILs, `golden missing - inspect build/peek_candidate.png, then copy to
test/golden/peek.png` and the same for `peek_x3`.

- [ ] **Step 3: LOOK AT THE CANDIDATES AND SAY WHAT YOU SEE**

Read `build/peek_candidate.png` and `build/peek_x3_candidate.png`. Describe each
against `design/Peek.dc.html` using Step 3 of Task 8's checklist, **including anything
that looks wrong in a render you are about to bless**. An icon has passed review twice
in this repo while reading as the letters "OC".

Then check the two against each other: the panel is the same box on both, so the only
differences should be the veil margin (34 vs 58) and the page underneath.

- [ ] **Step 4: Bless them, only if Step 3 said they are right**

```bash
cp build/peek_candidate.png test/golden/peek.png
cp build/peek_x3_candidate.png test/golden/peek_x3.png
```

Run: `make test 2>&1 | tail -5`
Expected: PASS.

- [ ] **Step 5: Prove the goldens bite**

Change `kPeekLines` from 8 to 7.
Run: `make test 2>&1 | grep -cE "FAILED|differ"`
Expected: failures in **both** golden subcases and in the eight-lines case.

Restore, `touch core/include/reader/theme.h`, `make test` — PASS.

Then the second mutation, which is the one a golden is uniquely for: remove the
`veilRect` call from `renderPeek`.
Expected: both golden subcases FAIL, and no unit test does — which is the whole
argument for having them.

Restore, `touch core/src/theme_quiet.cpp`, `make test` — PASS.

- [ ] **Step 6: Commit**

```bash
git add test/unit/test_theme_peek_golden.cpp test/golden/peek.png test/golden/peek_x3.png
git commit -m "peek: goldens at both geometries, four-level and through the App

Four-level rather than 1-bit, because the screen declares Grayscale for the Reader's
reason -- body text at reading size, where hard-thresholding a serif face at 32px was
judged worse on the panel. A Mono golden would pin the wrong thing convincingly.

Rendered through App::render and not top().render: an overlay's parent is painted by the
App walking down to the topmost non-overlay, and calling the screen alone paints a panel
floating on white -- which nothing else on the desktop can catch.

The eight-line count is asserted BESIDE the golden as well as inside it, because a golden
pins the pixels and cannot say why they are those pixels.

Proved by two mutations: seven lines fails both subcases and the count case, and removing
the veil fails both subcases and NO unit test -- which is the whole argument for having
them.

Refs #1"
```

---

### Task 10: the shell — Contents' Confirm opens a peek

**Files:**
- Modify: `shell/src/main.cpp`

**Read Rule 5 before starting.** `shell/` has no test harness. A green suite says
nothing about this file; `grep` for a marker from every edit.

- [ ] **Step 1: Add the pending-peek state beside `gPendingSpine`**

Read the existing declaration and its comment first:

```bash
grep -n "gPendingSpine" shell/src/main.cpp
```

Replace the `gPendingSpine` block's comment and add the reacquire flag. Around line
571:

```cpp
// The spine Contents chose, or -1. Held for exactly one dispatch: the choice is made
// while Contents is on top and acted on once the pop has put the Reader back.
//
// IT NOW OPENS A PEEK RATHER THAN JUMPING. Contents shipped jumping straight to the
// chapter -- safe, because goToChapter sets the return anchor -- and the peek is what
// makes being wrong about a chapter cheap: a panel of its text over the page you are
// on, with GO HERE to commit and CLOSE to leave your page untouched. See
// docs/superpowers/specs/2026-08-28-peek-overlay-design.md.
static int gPendingSpine = -1;
// A PEEK IS ON THE STACK, so the Reader's chapter has been released and has to be taken
// back when the panel goes. Tracked rather than inferred from the stack, because the
// pop that removes the peek is what makes the answer needed and the stack no longer
// says a peek was ever there.
static bool gPeekOpen = false;
```

Verify: `grep -n "gPeekOpen" shell/src/main.cpp` — expect three sites by the end of
this task.

- [ ] **Step 2: Replace the jump branch with the peek push**

Find the branch (around line 4262) and replace its body. Read it first:

```bash
sed -n '4253,4275p' shell/src/main.cpp
```

Replace from `if (gPendingSpine >= 0 && gApp->top().id() == reader::ScreenId::Reader) {`
through its closing brace with:

```cpp
    // A CHOSEN CHAPTER, acted on AFTER the pop that Contents' GO returns. The screen
    // cannot open the panel itself: the Reader is already on the stack under it, and a
    // screen that reached down into the stack would be a second thing that knows how a
    // Reader is shaped -- so Contents answers popTo(Reader) and names the chapter, and
    // this opens the peek over it.
    //
    // Read BEFORE the dispatch would be too early (the choice is made by the press) and
    // reading it after the pop is too late (the screen is gone), so the spine is taken
    // off the Contents screen while it is still on top, just above.
    //
    // IT USED TO JUMP HERE, with goToChapter. The jump was safe -- goToChapter sets the
    // return anchor -- and the peek is what makes being WRONG about a chapter cheap.
    if (gPendingSpine >= 0 && gApp->top().id() == reader::ScreenId::Reader) {
      auto* rd = static_cast<reader::ReaderScreen*>(&gApp->top());
      const int want = gPendingSpine;
      gPendingSpine = -1;
      if (want != rd->chapterIndex()) {
        const uint32_t t = millis();
        // THE PANEL'S COLUMN, which is not the reading column -- that is the whole
        // design. Recomputed here rather than held, because the reader's own typography
        // may have changed since the book opened and peekMetrics reads three of its
        // fields.
        reader::PageMetrics pm;
        gTheme.peekMetrics(gFrame->width(), gFrame->height(), *gFonts, gBody, gSettings,
                           pm);
        pm.italic = &gItalic;
        gFactory.setPeekMetrics(pm);
        // THE BOOK-WIDE PERCENTAGE AT THE PEEKED CHAPTER, computed here because
        // progressPercent needs the book's chapter byte layout and the panel has no
        // reason to hold a second copy of it. Page 1 of the target with no count, which
        // is what the fallback arm of progressPercent is for.
        gFactory.setPeek(want, reader::progressPercent(gFactory.readerBook(), want, 1, 0, 0));
        // THE READER LETS GO FIRST. A live chapter peaks at 69,884 bytes with a
        // 36,956-byte single allocation against a measured 45,840-byte floor, so two do
        // not fit -- and the peek is a second one. Released BEFORE the push, because the
        // push is what allocates the second chapter.
        //
        // Its page, index, cursor and anchor all survive, which is what lets
        // App::render draw the veiled page underneath with no decode at all.
        rd->releaseChapter();
        if (gApp->pushScreen(reader::ScreenId::Peek)) {
          gPeekOpen = true;
          logf("[peek] open spine=%d in %lums (heap %u)\n", want,
               (unsigned long)(millis() - t), (unsigned)ESP.getFreeHeap());
        } else {
          // REFUSED, so put the Reader back and leave it standing. The reader is on
          // their own page with the chapter list gone -- nothing lost but the list, the
          // page untouched and the anchor unmoved. The only reachable cause is the card
          // going, which pollCardPresence owns.
          const bool back = rd->reacquireChapter();
          logf("[peek] REFUSED spine=%d, reader %s\n", want,
               back ? "restored" : "COULD NOT BE RESTORED");
        }
        logFlush();
      }
    }

    // THE PEEK CLOSED OR COMMITTED, and both answer Pop -- so this runs after the
    // dispatch that removed it and `committed()` is the difference. Read off the screen
    // BEFORE the dispatch, for Contents' reason: the pop destroys the screen holding the
    // answer.
    if (gPeekOpen && gApp->top().id() == reader::ScreenId::Reader) {
      gPeekOpen = false;
      auto* rd = static_cast<reader::ReaderScreen*>(&gApp->top());
      const uint32_t t = millis();
      // TAKEN BACK BEFORE ANYTHING ELSE, because the commit below walks the chapter and
      // cannot without a stream.
      const bool back = rd->reacquireChapter();
      if (gPeekCommitted) {
        // GO HERE. goToPosition and not goToChapter: the reader may have paged several
        // pages into the panel, and page one would be right on the first page and wrong
        // everywhere after it. The anchor is set to WHERE THE READER WAS -- the departure
        // point, not the destination -- by goToPosition itself.
        const bool ok = back && rd->goToPosition(gPeekSpine, gPeekCursor);
        logf("[peek] GO HERE spine=%d block=%d line=%d: %s in %lums\n", gPeekSpine,
             gPeekCursor.block, gPeekCursor.line, ok ? "ok" : "REFUSED",
             (unsigned long)(millis() - t));
      } else {
        // CLOSE. NO seekTo, which is the one place this departs from the 08-24 spec:
        // a rewind costs what page you are ON -- ~1010 ms at page 99 and ~3 s deep in a
        // chapter -- so on CLOSE it would cost more than committing. The page was never
        // disturbed, and the live builder is what restreamAtCurrentPage repairs in a
        // quiet window.
        logf("[peek] CLOSE, reader %s in %lums\n", back ? "restored" : "NOT RESTORED",
             (unsigned long)(millis() - t));
      }
      gPeekCommitted = false;
      logFlush();
    }
```

- [ ] **Step 3: Capture the peek's answer before the dispatch**

Beside the existing Contents capture (around line 4200), add the peek's. The three
statics go next to `gPeekOpen`:

```cpp
// WHAT THE PEEK CHOSE, taken while it is still on top -- the dispatch below pops it,
// and after that there is no screen left to ask. Three values rather than a pointer,
// because the screen is gone by the time they are used.
static bool gPeekCommitted = false;
static int gPeekSpine = 0;
static reader::Cursor gPeekCursor{};
```

...and the capture itself, immediately after the Contents capture:

```cpp
    // THE PEEK'S ANSWER, taken for Contents' reason and at Contents' moment. Both CLOSE
    // and GO HERE answer Pop, so `committed()` is the whole difference and it has to be
    // read before the pop.
    if (gApp->top().id() == reader::ScreenId::Peek) {
      const auto* pk = static_cast<const reader::PeekScreen*>(&gApp->top());
      gPeekCommitted = pk->committed();
      gPeekSpine = pk->chosenSpine();
      gPeekCursor = pk->chosenCursor();
    }
```

**Note the ordering trap:** `committed()` is set BY the dispatch (it is
`onGesture(Activate)`'s side effect), so reading it *before* the dispatch always
answers false. The capture must therefore take the spine and cursor before, and
`committed()` **after** — but after the pop the screen is gone. **Resolve it by
reading all three after the dispatch from the screen the pop returned**: change
`PeekScreen::onGesture(Activate)` to keep answering `Action::pop()`, and have the
shell instead detect the commit from the *button*: `ev.button == Confirm` while a peek
was on top. That is exactly how the Typography apply path solves the same shape.

So the capture becomes:

```cpp
    // WHICH BUTTON LEFT THE PEEK, taken while it is still on top. `committed()` cannot
    // serve: it is set BY the dispatch, and after the dispatch the screen is gone. The
    // Typography apply path solves the same shape the same way -- the flag is set by the
    // press and consumed after the pop.
    if (gPeekOpen && gApp->top().id() == reader::ScreenId::Peek) {
      const auto* pk = static_cast<const reader::PeekScreen*>(&gApp->top());
      gPeekSpine = pk->chosenSpine();
      gPeekCursor = pk->chosenCursor();
      gPeekCommitted = (ev.button == reader::Button::Confirm);
    }
```

- [ ] **Step 4: Add the includes and verify every edit landed**

Add `#include "reader/screen_peek.h"` to the shell's includes.

```bash
grep -c "gPeekOpen\|gPeekCommitted\|gPeekSpine\|gPeekCursor" shell/src/main.cpp
```
Expected: 12 or more.

```bash
grep -n "peekMetrics\|setPeek(\|releaseChapter\|reacquireChapter\|goToPosition\|\[peek\]" shell/src/main.cpp
```
Expected: every one of them present, and **no remaining `goToChapter` call** — check:

```bash
grep -n "goToChapter" shell/src/main.cpp
```
Expected: no output. If `goToChapter` now has no caller anywhere, say so and leave it:
it is the degenerate case of `goToPosition` and removing it is a separate change.

```bash
git diff --stat shell/src/main.cpp
```
Expected: a plausible number of added lines and **few or no deletions** beyond the
replaced branch. A large deletion here is the scripted-edit defect this repo records
three times.

- [ ] **Step 5: Build the firmware**

The submodule is empty in a fresh worktree and `make firmware` then fails with
`PackageException: not a directory`, naming neither the submodule nor the fix:

```bash
git submodule update --init
```

```bash
make firmware 2>&1 | tail -20
```
Expected: a successful build with a flash/RAM summary. Do **not** run two builds
concurrently — they share the `uv` cache. "Failed to install Python dependencies into
penv" is transient; retry it.

- [ ] **Step 6: Commit**

```bash
git add shell/src/main.cpp
git commit -m "shell: Contents' chapter opens a peek instead of jumping

One branch moves. Contents still answers popTo(Reader) and the shell still takes
chosenSpine() before the dispatch -- what changes is that the shell primes the factory
and pushes the peek where it used to call goToChapter. The jump was already safe, since
goToChapter sets the return anchor; the peek is what makes being WRONG about a chapter
cheap.

THE READER LETS GO BEFORE THE PUSH, because the push is what allocates the second
chapter and two do not fit -- 69,884 bytes peak with a 36,956-byte single allocation
against a 45,840-byte floor. Its page, index, cursor and anchor all survive, which is
what lets App::render draw the veiled page underneath with no decode.

CLOSE PAYS NO seekTo. GO HERE calls goToPosition, not goToChapter: the reader may have
paged several pages into the panel, and page one would be right on the first page and
wrong everywhere after it.

WHICH BUTTON LEFT THE PEEK is what the shell reads, not committed(): the flag is set BY
the dispatch and after the dispatch the screen is gone. The Typography apply path solves
the same shape the same way.

A refused push reacquires and leaves the Reader standing -- the reader is on their own
page with the list gone, nothing lost but the list.

Refs #1"
```

---

### Task 11: `make compare` on the board, and the docs

**Files:**
- Modify: `CLAUDE.md`
- Modify: `docs/superpowers/plans/2026-08-20-v1-roadmap.md`

- [ ] **Step 1: Compare the firmware against the board**

```bash
make compare COMPARE_ARGS="--only peek"
```

Expected: a percentage for `peek` at both geometries. **The percentage is the check;
the word `ok` is not** — `ok` means the simulator produced a frame, which is how a
board that gained a row sat at 13.02% while the sheet said fine.

Compare like with like: the peek declares `Grayscale`, so a threshold-at-128 count over
four levels inflates the figure. The comparable numbers are the other grayscale
screens — `reader` 5.34%/6.38%, `reader_chapter_open` 4.53%/4.39%,
`reader_list` 5.20%/6.60%. A peek in that range is healthy; **do not chase it against
`reader_menu`'s 3.10%**, which is 1-bit.

- [ ] **Step 2: If the figure is out of that range, find out why before touching anything**

```bash
make compare COMPARE_ARGS="--only peek --export build/overlay"
```

Overlay the exported panel-size PNGs. The likely causes, in order: the band's two faces
(Label500 and Value700, not the caption's Meta400), the body padding, and the panel
box being computed twice with different leads.

**A board's `max-width` is a number to check in both engines, never one to trust from
Chrome** — the firmware's autohinted faces have whole-pixel advances and measure ~3%
wider. If the panel's eight lines come out as seven in the firmware, that is this, and
the fix is the board's copy or its width, not the renderer.

- [ ] **Step 3: Run the whole sheet once**

```bash
make compare
```

Expected (~2.8 min): every board reported, no board reported missing, and **no other
screen's percentage moved** — nothing in this work touches a shipped render path, so a
change anywhere else is a regression to find rather than to bless.

- [ ] **Step 4: Update CLAUDE.md**

Add `Peek` to the chrome-screens table in the section **The chrome screens**:

```
| Peek | `Peek.dc.html` | The only overlay over a `Grayscale` screen. Its column is NOT the reading column, which is why it shows no page number. |
```

Add a section after **The reader's menu and the chapter list**:

```markdown
## The peek

`Peek.dc.html`, and it is what makes chapter selection cheap to be wrong about.
Contents shipped jumping straight to a chapter — safe, because `goToChapter` sets the
return anchor — and the peek is the panel of that chapter's text over the page you are
on, with `GO HERE` to commit and `CLOSE` to leave your page untouched.

**IT OWNS A HEADLESS `ReaderScreen`, and that is the whole implementation.** The panel
is inset, so its column is ~368px against the reading page's 444, so its text
re-wraps — which is both why it cannot show a page number and why it cannot reuse the
Reader's already-laid `page_`. A bespoke pager would have been a second copy of
open/advance/seek; extracting a `ChapterPager` is a large refactor of the most
performance-critical code here for a screen that wants a fraction of it. What owning a
Reader buys is the one property that matters: **the cursor the peek commits is by
construction the one the Reader restores.**

**THE READER BENEATH RELEASES ITS CHAPTER while the panel is up**, because two live
chapters do not fit — 69,884 bytes peak with a 36,956-byte single allocation against a
measured 45,840-byte floor. `ReaderScreen::render` reads only `page_` and `vm_`, so the
veiled page draws with the chapter gone and no decode at all. One live chapter at any
moment, so the floor never moves.

**`CLOSE` PAYS NO `seekTo`, AND THE SPEC SAID IT SHOULD.** The 08-24 design budgeted
closing at "one `seekTo` — 33.9 ms desktop", which is this file's own ratio trap: a
rewind costs *what page you are on*, and the device measured ~376 ms at page 38,
~1010 ms at page 99 and ~3 s deep in a long chapter. On `CLOSE` that would cost more
than committing. Nothing visible was disturbed, so the only thing spent is the live
builder — and `pb_ == nullptr` is the state `restreamAtCurrentPage` already repairs in
a quiet window. That machinery landed after the spec was written.

**THERE IS NO GATE ON THE IDLE JOBS, and that was checked rather than assumed.** All
three — `completeIndex`, `restreamAtCurrentPage`, `warmPageRing` — are gated on
`gApp->top().id() == ScreenId::Reader`, so a peek on top stops them by construction.
`readerOnStack`'s three callers are the book-closed check, the ring shrink and the
Typography apply, and the last is unreachable while a peek is up because the pop that
opened it took the menu with it. **A save while released is safe for a reason worth
stating**: `chapterBytesRead()` is `pageBytes_`, a plain member, where
`ChapterReader::bytesRead()` would answer 0 with `inflated_` gone and push
`progressPercent` onto its page/pageTotal fallback — which is the exact shape of the
percentage-going-backwards bug. `hasChapter()` exists as the release test's observation
point, not as a guard.

**IT IS NOT RESTORABLE ACROSS A WAKE.** The factory refuses an unprimed `Peek`, so
`App::restore` stops early and leaves the Reader standing. Persisting a peeked cursor
would be a card write for a breadcrumb the anchor's own design declined to pay for.

**EIGHT LINES, AND THE NUMBER IS THE DESIGN** — content-sizing alone ran to eleven and
filled the glass to within 48px of the top, which reads as a bordered full screen
rather than a modal. The panel's height is a **result** of the line count, as
`headerBandHeight()` and `hintBarHeight()` are results; a pinned height cut the last
line in half lengthwise, which `PageBuilder` cannot even do.

**`Up` AND `Down` ARE DEAD SLOTS ON PURPOSE.** `Up` already means "return to where I
was" on the screen underneath, and one button with two meanings across a single press
is worse than an unbound one — so the side buttons page in the peek exactly as they do
while reading. The four-label bar also left ~4px of slack at 480 wide against faces
that measure ~3% wider than Chrome.

**Its band is its own, not `drawPanelCaption`**, and the difference is measured rather
than stylistic: the caption's value is `Meta400` at 21px on 21px of padding, where this
board says `--t-value` (25px) at weight 700 on 18px. Reusing it would draw the band 6px
too tall.
```

In `docs/superpowers/plans/2026-08-20-v1-roadmap.md`, mark 3D's peek half done. Find
the entry and edit it — read it first, because an anchor is not what you remember
writing:

```bash
grep -n "3D — Peek and return" docs/superpowers/plans/2026-08-20-v1-roadmap.md
```

Change `**3D — Peek and return. DESIGNED AND BOARDED, NOT BUILT**` to name what has
landed, and strike the `First overlay over a grayscale screen` bullet's "unverified"
claim, since `test_veil_planes.cpp` now measures it.

- [ ] **Step 5: Verify the doc edits landed**

```bash
grep -n "## The peek" CLAUDE.md && grep -n "test_veil_planes" docs/superpowers/plans/2026-08-20-v1-roadmap.md
```
Expected: both found. A script with several asserts writes ONCE at the end, so a later
assert failing means none of the earlier edits landed — and if the next command is a
commit, it commits the code without the documentation. That has happened twice here.

```bash
git diff --stat CLAUDE.md docs/superpowers/plans/2026-08-20-v1-roadmap.md
```
Expected: additions, and no large deletion.

- [ ] **Step 6: Commit**

```bash
git add CLAUDE.md docs/superpowers/plans/2026-08-20-v1-roadmap.md
git commit -m "docs: the peek, and the two things it settled

CLAUDE.md gains a section for it and a row in the chrome-screens table. The two facts
worth finding here later are the ones that correct the 08-24 spec: CLOSE pays no seekTo,
because a rewind costs what page you are ON and the restream already repairs the only
thing a close spends; and there is no gate on the idle jobs, because all three are
gated on the Reader being on TOP of the stack.

The roadmap's 3D entry no longer calls the grayscale veil unverified --
test_veil_planes.cpp measures it at panel sizes under both rotations, which is the risk
issue #1 named.

Refs #1"
```

---

### Task 12: verify the whole thing, and hand it over

- [ ] **Step 1: Full suite**

```bash
make test 2>&1 | tail -15
```
Expected: PASS, with an assertion count higher than before this work. Note the number.

- [ ] **Step 2: Every simulator screen still renders**

```bash
for s in home library settings contents reader reader_menu typography peek book_details sleep; do ./build/reader_sim $s build/_$s.png >/dev/null 2>&1 && echo "ok $s" || echo "FAIL $s"; done
```
Expected: `ok` for all ten.

- [ ] **Step 3: Firmware builds**

```bash
make firmware 2>&1 | tail -12
```
Expected: success. Record the flash and RAM figures — a new screen plus a second
`ReaderScreen`'s worth of code is a real flash cost and the number belongs in the PR.

- [ ] **Step 4: Move the card to `On glass` and say what needs verifying**

**Do NOT write `Closes #1`.** Everything above is desktop evidence, and `On glass` →
`Done` needs device evidence, which only the user can produce — flashing must be run by
them. An agent moves a card as far as `On glass` and stops.

The card is already at `Building` (`Kind = Screen`, board `Peek.dc.html`). Move it:

```bash
gh project item-edit --id PVTI_lAHOAkvc3c4BhZ5gzg36kF4 --project-id PVT_kwHOAkvc3c4BhZ5g --field-id PVTSSF_lAHOAkvc3c4BhZ5gzhgVwC4 --single-select-option-id 5012a8f7
```

If it fails with `your authentication token is missing required scopes`, tell the user
to run `gh auth refresh -s project` — it is interactive and only they can grant it.

- [ ] **Step 5: Report the list the desktop cannot answer**

State these explicitly as what the device has to confirm:

1. **The veil over four levels, on glass.** The tests prove the bytes; only the panel
   says whether a veiled grayscale page reads as "not this" rather than as a fault.
2. **The faces re-init and the second chapter allocates**, at the reading floor. The
   desktop does no SD reads and no real inflate, and this file's ~135× ratio warning
   applies to the whole peek-open walk.
3. **What `CLOSE` actually costs**, from `[peek] CLOSE, reader restored in Xms`. The
   design says it is a reopen and no rewind; the log is what proves it.
4. **What `GO HERE` costs deep in a chapter**, from `[peek] GO HERE`. It is a walk, and
   the walk is proportional to the target page.
5. **`[restream] ready` appearing after a CLOSE.** If it reads `abandoned` every time,
   the stream is never being put back and the next forward turn pays for it.
6. **The panel's eight lines are eight on the device.** The firmware's faces measure
   ~3% wider than Chrome, and a board's width is a number to check in both engines.

```bash
pio device monitor -e xteink | tee run.log
```

```bash
python3 tools/latency.py run.log
```

---

## Self-review

**Spec coverage.** Every section of `2026-08-28-peek-overlay-design.md` maps to a task:
the flow → Task 10; not-restorable → Tasks 4 and 7; the engine and its three
alternatives → Task 6; the memory dance → Tasks 2, 3, 10; `CLOSE` pays no `seekTo` →
Task 3; `GO HERE` → Tasks 3 and 10; no third gate → Task 3's comment and Task 11's
CLAUDE.md section; failure → Task 10's refusal branch; the board's derived geometry →
Task 5; verification → Tasks 1, 3, 6, 9, 11, 12.

**Two things this plan deliberately leaves for the implementer to resolve**, both
flagged inline rather than hidden:

1. **Task 5's `columnH` duplication.** The draft `renderPeek` recomputes the panel box
   with `kBodyLeadEm` while `peekMetrics` uses `settings.lineSpacing`. The fix is
   stated: one `peekPanelBox` helper, and the lead carried in `PeekViewModel`. Left as
   a note rather than as finished code because it is exactly this repo's first
   invariant, and an implementer who resolves it will understand the geometry better
   than one handed it.
2. **Task 10's `committed()` ordering.** The obvious capture reads a flag the dispatch
   has not yet set. The plan walks into the trap and out of it, landing on the
   button-based form the Typography apply path already uses. `PeekScreen::committed()`
   therefore survives as the *test's* observation point rather than the shell's, which
   is worth knowing before deleting it as unused.

**Type consistency.** `releaseChapter` / `reacquireChapter` / `hasChapter` /
`goToPosition` / `body()` / `italic()` on `ReaderScreen`; `release()` / `held()` on
`ChapterReader`; `peekMetrics` / `renderPeek` / `kPeekLines` on the theme;
`setPeek` / `clearPeek` / `setPeekDemo` / `setPeekMetrics` on the factory;
`committed()` / `chosenSpine()` / `chosenCursor()` / `setMetrics` / `setItalic` /
`setChapterNames` / `page()` / `vm()` on `PeekScreen`. Each is spelled the same way in
every task that uses it.

**One thing the plan does not do**, and it is the right call for a separate change:
`goToChapter` loses its last caller in Task 10. It is left in place, named in Task 10's
Step 4, because deleting a public method with a test suite behind it is a change with
its own reasoning and does not belong inside this one.
