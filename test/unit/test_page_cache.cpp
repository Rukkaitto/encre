// The two latency defects the reader shipped with, and the properties their fixes
// must not break.
//
//   1. COUNTING A CHAPTER'S PAGES BLOCKED EVERY BUTTON for 2-3.6 s. It is
//      interruptible now, and what that has to be worth is that an abandoned count
//      is INVISIBLE: the index, the page number and the page on glass are exactly as
//      they were, and a later count still lands.
//   2. A BACKWARD PAGE TURN RE-DECODED THE CHAPTER, ~376 ms on the device against
//      20-33 ms forward. A ring of recently laid-out pages answers the common case,
//      and a page it answers must be indistinguishable from the decoded one.
//
// The strongest property in this area is already "reading backward gives exactly the
// pages reading forward gave" (test_layout.cpp) -- this file extends it to the ring,
// per FIELD rather than per line of text, because a cache that dropped a line's
// justification or its emphasis spans would still spell the same words.
#include <cstdint>
#include <string>
#include <vector>

#include "doctest.h"
#include "reader_fixture.h"
#include "reader/layout.h"
#include "reader/screen_reader.h"

using readerfix::deferredChapter;
using readerfix::longChapter;
using readerfix::pageText;
using readerfix::Reading;
using reader::Cursor;
using reader::LaidLine;
using reader::Page;

namespace {

// EVERY FIELD OF EVERY LINE, which is the whole point: `pageText` compares the words
// and a cache bug that lost a line's stretch, its indent or its emphasis would spell
// the same words at the wrong positions. `next` is included because it is what a
// forward turn resumes from; `lastPage` is NOT, because it is not a property of the
// page -- it is "is there another one after this", which depends on how much of the
// chapter has been counted since, and both producers recompute it.
bool sameLine(const LaidLine& a, const LaidLine& b) {
  if (a.text != b.text || a.x != b.x || a.baselineY != b.baselineY) return false;
  if (a.extraPerGapF26 != b.extraPerGapF26 || a.kind != b.kind) return false;
  if (a.tracking.f26() != b.tracking.f26()) return false;
  if (a.block != b.block || a.lastOfBlock != b.lastOfBlock) return false;
  if (a.firstOfBlock != b.firstOfBlock || a.markerX != b.markerX) return false;
  if (a.emphasis.size() != b.emphasis.size()) return false;
  for (size_t i = 0; i < a.emphasis.size(); ++i)
    if (!(a.emphasis[i] == b.emphasis[i])) return false;
  return true;
}

bool samePage(const Page& a, const Page& b) {
  if (a.lines.size() != b.lines.size()) return false;
  if (!(a.next == b.next)) return false;
  for (size_t i = 0; i < a.lines.size(); ++i)
    if (!sameLine(a.lines[i], b.lines[i])) return false;
  return true;
}

// WHAT A PAGE COSTS THE HEAP, which is what kPageCacheDepth is spent against.
//
// Summed from the containers rather than from sizeof, because the lines are where it
// is: a LaidLine is ~100 bytes of struct and its `text` is a ~45-byte string that
// exceeds every small-string buffer this project's toolchains have. `capacity`, not
// `size`, since that is what was really taken from the allocator.
size_t pageHeapBytes(const Page& p) {
  size_t n = sizeof(Page) + p.lines.capacity() * sizeof(LaidLine);
  for (const LaidLine& ln : p.lines) {
    n += ln.text.capacity() + 1;
    n += ln.emphasis.capacity() * sizeof(reader::Span);
  }
  return n;
}

// A stop predicate that gives up after `after` consultations, counting them.
struct StopAfter {
  int after = 0;
  int calls = 0;
};
bool stopAfter(void* ctx) {
  StopAfter* s = static_cast<StopAfter*>(ctx);
  ++s->calls;
  return s->calls > s->after;
}
bool stopNever(void* ctx) {
  ++static_cast<StopAfter*>(ctx)->calls;
  return false;
}

// How many times a FULL count of this chapter asks the predicate. Measured rather
// than assumed so a test can abandon HALFWAY THROUGH: a hardcoded number is either
// past the end of the walk (nothing is abandoned and the test passes vacuously,
// which is exactly how this file first failed) or so early that a walk committing as
// it went would not yet have had anything to clobber.
int stopChecksForFullCount(const std::string& doc) {
  Reading r(doc, /*settled=*/false);
  StopAfter s{0, 0};
  REQUIRE(r.scr->completeIndex(&stopNever, &s));
  return s.calls;
}

}  // namespace

// --- Defect 1: an abandoned count -----------------------------------------------

TEST_CASE("an abandoned page count leaves the index, the page number and the page alone") {
  // The chapter has to be one the device would DEFER, or there is nothing to abandon.
  const std::string doc = deferredChapter();
  Reading r(doc, /*settled=*/false);
  REQUIRE(r.scr->indexPending());

  // Read a few pages in, so `at_` is somewhere the walk could plausibly disturb and
  // the index has real content to be clobbered.
  for (int i = 0; i < 4; ++i) r.scr->onGesture({reader::Gesture::Next});
  const int wasIndex = r.scr->pageIndex();
  const int wasKnown = r.scr->pageCount();
  const std::string wasText = pageText(r.scr->page());
  const Page wasPage = r.scr->page();
  const int wasVmPage = r.scr->vm().page;
  REQUIRE(wasIndex == 4);
  REQUIRE(wasKnown >= 5);
  REQUIRE_FALSE(wasText.empty());

  // Stop HALFWAY through the walk: far enough in that a count committing as it went
  // would have replaced the index with a partial one, and short of the end so that
  // something is really abandoned.
  const int half = stopChecksForFullCount(doc) / 2;
  REQUIRE(half > 0);
  StopAfter s{half, 0};
  CHECK_FALSE(r.scr->completeIndex(&stopAfter, &s));
  CHECK(s.calls > 0);  // the predicate really was consulted; the test is not vacuous

  CHECK(r.scr->pageIndex() == wasIndex);
  CHECK(r.scr->pageCount() == wasKnown);
  CHECK(pageText(r.scr->page()) == wasText);
  CHECK(samePage(r.scr->page(), wasPage));
  CHECK(r.scr->vm().page == wasVmPage);
  CHECK(r.scr->vm().pageTotal == 0);  // still unknown, so still an em dash
  // ...and it is still PENDING, which is what makes the next quiet window try again.
  CHECK(r.scr->indexPending());

  // The retry lands, and lands on the same page.
  REQUIRE(r.scr->completeIndex());
  CHECK_FALSE(r.scr->indexPending());
  CHECK(r.scr->pageIndex() == wasIndex);
  CHECK(pageText(r.scr->page()) == wasText);
  CHECK(samePage(r.scr->page(), wasPage));
  CHECK(r.scr->vm().pageTotal > wasKnown);
}

TEST_CASE("a stop predicate that never fires counts exactly what no predicate counts") {
  // The interruptible path and the uninterruptible one must not be two paginations.
  const std::string doc = deferredChapter();
  Reading plain(doc, /*settled=*/false);
  Reading probed(doc, /*settled=*/false);
  REQUIRE(plain.scr->completeIndex());
  StopAfter s{0, 0};
  REQUIRE(probed.scr->completeIndex(&stopNever, &s));
  CHECK(s.calls > 0);
  CHECK(probed.scr->pageCount() == plain.scr->pageCount());
  CHECK(pageText(probed.scr->page()) == pageText(plain.scr->page()));
}

TEST_CASE("an abandoned count leaves the page still turnable, forward and back") {
  // The one thing an abandoned count really does spend is the live PageBuilder, since
  // the walk rewinds the stream underneath it. That is the state Gesture::Next
  // re-establishes -- and getting it wrong does not read as a slow turn, it reads as
  // the reader jumping to another chapter.
  const std::string doc = deferredChapter();
  Reading r(doc, /*settled=*/false);
  for (int i = 0; i < 3; ++i) r.scr->onGesture({reader::Gesture::Next});
  const int wasIndex = r.scr->pageIndex();

  // What the pages either side of it say, taken from a clean read of the same doc.
  Reading ref(doc, /*settled=*/false);
  std::vector<std::string> text;
  for (int i = 0; i < 6; ++i) {
    text.push_back(pageText(ref.scr->page()));
    ref.scr->onGesture({reader::Gesture::Next});
  }

  StopAfter s{stopChecksForFullCount(doc) / 2, 0};
  REQUIRE(s.after > 0);
  REQUIRE_FALSE(r.scr->completeIndex(&stopAfter, &s));

  r.scr->onGesture({reader::Gesture::Next});
  CHECK(r.scr->pageIndex() == wasIndex + 1);
  CHECK(pageText(r.scr->page()) == text[static_cast<size_t>(wasIndex) + 1]);
  r.scr->onGesture({reader::Gesture::Prev});
  CHECK(r.scr->pageIndex() == wasIndex);
  CHECK(pageText(r.scr->page()) == text[static_cast<size_t>(wasIndex)]);
}

TEST_CASE("stopping a count on a chapter that is already counted changes nothing") {
  // completeIndex refuses when there is nothing pending, predicate or not -- so a
  // shell that calls it every quiet window cannot cost anything on a settled chapter.
  Reading r(longChapter(40));
  REQUIRE_FALSE(r.scr->indexPending());
  const int pages = r.scr->pageCount();
  StopAfter s{0, 0};
  CHECK_FALSE(r.scr->completeIndex(&stopAfter, &s));
  CHECK(s.calls == 0);
  CHECK(r.scr->pageCount() == pages);
}

// --- Defect 2: the ring of laid-out pages ---------------------------------------

TEST_CASE("a page served from the ring is identical to the page decoded for it") {
  const std::string doc = longChapter(40);

  // Forward first, keeping every page as `advance()` produced it.
  Reading fwd(doc);
  std::vector<Page> pages;
  for (int i = 0; i < 400; ++i) {
    pages.push_back(fwd.scr->page());
    const int was = fwd.scr->pageIndex();
    fwd.scr->onGesture({reader::Gesture::Next});
    if (fwd.scr->pageIndex() == was) break;
  }
  REQUIRE(pages.size() > 6);

  // ...and now all the way back. The first few are ring hits and the rest are
  // decodes, and BOTH have to match what reading forward gave.
  for (int i = 0; i < 400 && fwd.scr->pageIndex() > 0; ++i) {
    fwd.scr->onGesture({reader::Gesture::Prev});
    const size_t p = static_cast<size_t>(fwd.scr->pageIndex());
    REQUIRE(p < pages.size());
    CHECK(samePage(fwd.scr->page(), pages[p]));
  }
  CHECK(fwd.scr->pageIndex() == 0);

  // Both mechanisms were exercised -- otherwise this checks one path twice.
  const reader::ReaderScreen::RingStats st = fwd.scr->ringStats();
  CHECK(st.hits > 0);
  CHECK(st.decodes > 0);
}

TEST_CASE("turning back to the page you just left decodes NOTHING") {
  // The claim defect 2 is about, asserted on the ring's own counter rather than on
  // the page coming out right: with the ring removed the page would still be right
  // and this test would still pass on text alone.
  Reading r(longChapter(40));
  for (int i = 0; i < 5; ++i) r.scr->onGesture({reader::Gesture::Next});
  const uint32_t before = r.scr->ringStats().decodes;
  const std::string wasText = pageText(r.scr->page());

  r.scr->onGesture({reader::Gesture::Prev});
  CHECK(r.scr->pageIndex() == 4);
  CHECK(r.scr->ringStats().decodes == before);
  CHECK(r.scr->ringStats().hits >= 1);

  // ...and forward again, which is the turn that used to have to re-establish the
  // stream. The ring answers that too, so a reader bouncing between two pages never
  // decodes either of them again.
  r.scr->onGesture({reader::Gesture::Next});
  CHECK(r.scr->pageIndex() == 5);
  CHECK(pageText(r.scr->page()) == wasText);
  CHECK(r.scr->ringStats().decodes == before);
}

TEST_CASE("a forward turn past what the ring holds does not read as the end of the chapter") {
  // THE SHARP EDGE OF SERVING A FORWARD TURN FROM THE RING. A ring hit leaves no live
  // PageBuilder, so if Gesture::Next took one where it needed a stream, advance()
  // would answer false, the screen would read that as "the chapter ended" and would
  // turn to the NEXT CHAPTER from the middle of this one. There is no book behind an
  // in-memory chapter, so the symptom here is a page turn that does nothing.
  Reading r(longChapter(40));
  const int total = r.scr->pageCount();
  REQUIRE(total > 6);

  for (int i = 0; i < 3; ++i) r.scr->onGesture({reader::Gesture::Next});
  r.scr->onGesture({reader::Gesture::Prev});  // ring hit; no live builder afterwards
  REQUIRE(r.scr->pageIndex() == 2);

  // Now walk forward past every page the ring can be holding.
  for (int i = 0; i < 8; ++i) r.scr->onGesture({reader::Gesture::Next});
  CHECK(r.scr->pageIndex() == 10);
  CHECK_FALSE(r.scr->page().lines.empty());
}

TEST_CASE("the ring holds kPageCacheDepth pages and no more") {
  // Depth is a memory budget, so it has to be observable that it is enforced --
  // an off-by-one here is 1.6 KB against a 42 KB floor.
  Reading r(longChapter(60));
  const int total = r.scr->pageCount();
  REQUIRE(total > reader::ReaderScreen::kPageCacheDepth + 3);

  // Read far enough forward that the pages at the start cannot still be held.
  for (int i = 0; i < reader::ReaderScreen::kPageCacheDepth + 3; ++i)
    r.scr->onGesture({reader::Gesture::Next});

  const uint32_t before = r.scr->ringStats().decodes;
  // The page just left is held...
  r.scr->onGesture({reader::Gesture::Prev});
  CHECK(r.scr->ringStats().decodes == before);
  // ...and one kPageCacheDepth further back is not.
  for (int i = 0; i < reader::ReaderScreen::kPageCacheDepth; ++i)
    r.scr->onGesture({reader::Gesture::Prev});
  CHECK(r.scr->ringStats().decodes > before);
}

TEST_CASE("what the ring costs, measured rather than assumed") {
  // The budget kPageCacheDepth is chosen against, and the reason it is a test and not
  // a comment: a page's heap is the sum of a dozen owned strings, so it moves with the
  // face, the column and anything that changes how many lines fit. The device's
  // measured minimum free heap with a book open is 42,152 bytes.
  //
  // BOTH PANEL GEOMETRIES, because the X3's column is 492px against the X4's 444 and
  // holds a different number of lines -- and the X3 is the device on the desk.
  size_t worst = 0;
  for (const bool x3 : {false, true}) {
    size_t here = 0;
    Reading r(longChapter(40), true, x3 ? 528 : 480, x3 ? 792 : 800);
    for (int i = 0; i < 400; ++i) {
      // A COPY, which is what the ring holds and is SMALLER than the live page: the
      // live one grew by push_back and carries a doubled vector capacity, where a copy
      // allocates exactly what it needs. Measuring `page()` directly would price the
      // ring above what it costs.
      const Page copy = r.scr->page();
      here = here > pageHeapBytes(copy) ? here : pageHeapBytes(copy);
      const int was = r.scr->pageIndex();
      r.scr->onGesture({reader::Gesture::Next});
      if (r.scr->pageIndex() == was) break;
    }
    MESSAGE((x3 ? "X3 528x792" : "X4 480x800") << ": worst page " << here << " bytes");
    worst = worst > here ? worst : here;
  }
  MESSAGE("ring of " << reader::ReaderScreen::kPageCacheDepth << " = "
                     << worst * reader::ReaderScreen::kPageCacheDepth << " bytes");
  // A CEILING, not the measurement -- the number moves with the fixture's copy above
  // it, with the face and with the column, and pinning it exactly would fail for
  // reasons that are not defects. 2 KB a page puts the ring at 6 KB, ~14% of the
  // floor, which is the most this feature may cost before its depth has to come down.
  // Measured today: 1,471 bytes (X4) and 1,512 (X3), so 4,536 for the ring.
  CHECK(worst < 2048u);
  CHECK(worst * reader::ReaderScreen::kPageCacheDepth < 6144u);
}

TEST_CASE("completing the index does not decode the page it returns to") {
  // The count's second leg -- `seekTo(at_)` to put the reader back where they were --
  // was a whole extra pass over the chapter, and `at_` is by definition the page most
  // recently laid out. This is the ring paying for defect 1 as well as defect 2.
  const std::string doc = deferredChapter();
  Reading r(doc, /*settled=*/false);
  for (int i = 0; i < 3; ++i) r.scr->onGesture({reader::Gesture::Next});
  const std::string wasText = pageText(r.scr->page());
  const uint32_t before = r.scr->ringStats().decodes;

  REQUIRE(r.scr->completeIndex());
  CHECK(r.scr->ringStats().decodes == before);
  CHECK(pageText(r.scr->page()) == wasText);
  CHECK(r.scr->pageIndex() == 3);
}
