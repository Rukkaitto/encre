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
#include "doctest.h"
#include "reader/book.h"
#include "reader/reading_store.h"

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
