// Book progress: does it advance as the reader does?
//
// The device reported it stuck at 40% after reading on, then briefly 42% on the
// sleep screen against 40% on Home, then back to 40%. Two faults in one number:
//
//   * WITHIN A CHAPTER it interpolated on page/pageTotal, and pageTotal is 0 until
//     the deferred count lands -- so it did not advance at all in the meantime, and
//     for a long chapter that is the first seconds and is longer still while the
//     reader keeps pressing.
//   * A SAVE TAKEN IN THAT WINDOW persisted the un-advanced figure over a better
//     one, which is how it went BACKWARDS.
//
// The fix is the byte position, which is what the percentage is made of everywhere
// else and needs no count.
#include "card_book_fixture.h"
#include "doctest.h"
#include "reader/book.h"
#include "reader/inflate_stream.h"
#include "reader/reading_store.h"
#include "reader/screen_reader.h"

using reader::OpenedBook;
using reader::progressPercent;

namespace {
// Ten equal chapters, so a chapter is exactly ten percent of the book and the
// arithmetic is checkable by hand.
OpenedBook tenChapters(uint32_t each = 1000) {
  OpenedBook b;
  for (int i = 0; i < 10; ++i) {
    reader::ChapterSpan s;
    s.uncompressedSize = each;
    b.chapters.push_back(s);
  }
  return b;
}
}  // namespace

TEST_CASE("progress counts the chapters already behind you") {
  const OpenedBook b = tenChapters();
  CHECK(progressPercent(b, 0, 1, 0) == 0);
  CHECK(progressPercent(b, 4, 1, 0) == 40);
  CHECK(progressPercent(b, 9, 1, 0) == 90);
}

TEST_CASE("BYTES advance it within a chapter, with no page count at all") {
  // The reported bug: pageTotal is 0 and the reader is half way through chapter 4,
  // which is 45% of the book. It used to answer 40 and stay there.
  const OpenedBook b = tenChapters();
  CHECK(progressPercent(b, 4, 0, 0, 0) == 40);
  CHECK(progressPercent(b, 4, 0, 0, 500) == 45);
  CHECK(progressPercent(b, 4, 0, 0, 1000) == 50);
  // ...and it is monotonic through the chapter, which is what "advanced but still
  // 40%" was really about.
  int last = -1;
  for (uint32_t at = 0; at <= 1000; at += 100) {
    const int pct = progressPercent(b, 4, 0, 0, at);
    CHECK(pct >= last);
    last = pct;
  }
}

TEST_CASE("bytes WIN over the page fraction when both are offered") {
  // Not two ways of answering one question: the bytes are the truth and the pages
  // are what is used when there are none. A disagreement must resolve to the bytes.
  const OpenedBook b = tenChapters();
  CHECK(progressPercent(b, 4, 1, 10, 500) == 45);   // pages would say 40
  CHECK(progressPercent(b, 4, 10, 10, 500) == 45);  // ...and would say 49
}

TEST_CASE("the page fraction is still the fallback where bytes are unknowable") {
  // A stored archive entry and an in-memory chapter have no inflater to ask.
  const OpenedBook b = tenChapters();
  CHECK(progressPercent(b, 4, 1, 10, 0) == 40);
  CHECK(progressPercent(b, 4, 6, 10, 0) == 45);
  CHECK(progressPercent(b, 4, 11, 10, 0) == 50);
}

TEST_CASE("it cannot run past the end, however wrong its inputs") {
  const OpenedBook b = tenChapters();
  CHECK(progressPercent(b, 9, 0, 0, 999999) == 100);
  CHECK(progressPercent(b, 99, 0, 0, 0) == 100);
  CHECK(progressPercent(b, 0, 0, 0, 0) == 0);
  CHECK(progressPercent(OpenedBook{}, 0, 1, 1, 100) == 0);  // no chapters at all
}

TEST_CASE("a chapter of nothing does not divide by it") {
  OpenedBook b = tenChapters();
  b.chapters[4].uncompressedSize = 0;
  CHECK(progressPercent(b, 4, 1, 10, 0) == 44);
  CHECK(progressPercent(b, 4, 1, 10, 50) == 44);
}

// --- AND THE INPUT, WHICH IS WHERE THE SECOND REPORT OF THIS NUMBER LANDED --------
//
// Everything above drives the arithmetic with bytes handed to it. #148 was reported
// off a wallabag article -- "one big chapter surrounded by chapters that have only
// one page; I'm at 50% in the chapter and it says 83%" -- and the arithmetic was
// right both times. What was wrong was `ChapterReader::bytesRead()`, which answered
// `Inflater::produced()`: the DECODER's position, a whole 16 KB chunk in front of the
// page on the glass.
//
// SO THE CHAPTER HAS TO BE SMALLER THAN ONE CHUNK, which is the shape an article is
// and a novel's chapter is not -- the whole chapter inflates on the first `next()`,
// so page 1 reported every byte of it read and the percentage then stood still for
// the length of the chapter. Measured on the corpus, the worst page-by-page
// disagreement went median 4pp -> 2pp, p99 69pp -> 15pp, 194 books of 226 improved
// and two moved by one point of rounding; on the article shape it went 83pp -> 7pp.
namespace {

// The largest `longChapter` that still fits inside one inflate chunk, sized against
// the constant rather than pinned to a paragraph count -- `deferredChapter()`'s own
// idiom, and for its reason: this stays the right fixture if kChunkBytes ever moves.
std::string oneChunkChapter() {
  std::string d = readerfix::longChapter(2);
  for (int paragraphs = 4; paragraphs <= 4096; paragraphs *= 2) {
    const std::string bigger = readerfix::longChapter(paragraphs);
    if (bigger.size() >= reader::Inflater::kChunkBytes) break;
    d = bigger;
  }
  return d;
}

}  // namespace

TEST_CASE("A CHAPTER SHORTER THAN ONE INFLATE CHUNK DOES NOT ARRIVE FULLY READ") {
  cardfix::CardReading r(oneChunkChapter());
  const int spine = r.scr->chapterIndex();
  const uint32_t size = r.ob.chapters[static_cast<size_t>(spine)].uncompressedSize;

  // THE FIXTURE GUARDS, and the first two are what make this case able to see the
  // defect at all: a STORED entry has no inflater to be ahead of, and a chapter over
  // a chunk long is the case that always worked.
  REQUIRE(r.ob.locate(spine).deflated);
  REQUIRE(size < reader::Inflater::kChunkBytes);
  REQUIRE(r.scr->completeIndex());
  REQUIRE(r.scr->pageCount() >= 8);

  // Page 1 has read a page, not a chapter. This is the assertion that fails against
  // `produced()`, where it read `size == size`.
  CHECK(r.scr->chapterBytesRead() < size / 2);

  // AND THE NUMBER TRACKS THE READER ACROSS THE CHAPTER, which is the report itself.
  // Chapter 2 of this fixture is one short paragraph, so the chapter is very nearly
  // the whole book and its halfway page is very nearly half of it.
  const int pages = r.scr->pageCount();
  const auto percentNow = [&] {
    return progressPercent(r.ob, r.scr->chapterIndex(), r.scr->vm().page,
                           r.scr->vm().pageTotal, r.scr->chapterBytesRead());
  };
  const int first = percentNow();
  int middle = first;
  int last = first;
  int previous = first;
  for (int i = 1; i < pages; ++i) {
    r.scr->onGesture({reader::Gesture::Next});
    const int now = percentNow();
    // NEVER BACKWARDS. The first report of this number was that it went backwards,
    // and a lead that is spent early and then waits is not the only way to produce
    // one -- so it is asserted per turn rather than end to end.
    CHECK(now >= previous);
    previous = now;
    if (r.scr->pageIndex() + 1 == pages / 2) middle = now;
    last = now;
  }
  CHECK(first <= 20);
  CHECK(middle >= 35);
  CHECK(middle <= 65);
  CHECK(last >= 90);
}
