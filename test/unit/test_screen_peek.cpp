// THE READER'S HALF OF THE PEEK: letting go of a chapter, taking it back, and
// jumping to a cursor rather than to page one.
//
// PeekScreen's own cases join this file in a later task. These come first because the
// peek cannot be built without them.
#include <cstdint>
#include <memory>
#include <string>

#include "card_book_fixture.h"
#include "doctest.h"
#include "reader_fixture.h"
#include "reader/screen_reader.h"

using reader::Cursor;
using reader::Gesture;

namespace {

// A CARD-BACKED BOOK, and it has to be: chapterBytesRead() is the inflater's count on
// the page it was recorded on, and readerfix::Reading has no inflater -- so every
// assertion about it over that fixture is `0 == 0` and passes with the field
// clobbered. That is how a hole in the restream tests was found; this file does not
// reintroduce it.
struct Peeking {
  cardfix::CardReading r;
  explicit Peeking(int paragraphs = 40)
      : r(readerfix::longChapter(paragraphs)) {
    r.scr->completeIndex();
    REQUIRE(r.scr->pageCount() > 4);
  }
  reader::ReaderScreen& s() { return *r.scr; }
};

void pageForward(reader::ReaderScreen& s, int n) {
  for (int i = 0; i < n; ++i) s.onGesture({Gesture::Next});
}
void pageBackward(reader::ReaderScreen& s, int n) {
  for (int i = 0; i < n; ++i) s.onGesture({Gesture::Prev});
}

}  // namespace

// --- LETTING GO ----------------------------------------------------------------

TEST_CASE("a released Reader still renders the page it was on") {
  // THE PROPERTY THE WHOLE DESIGN RESTS ON. App::render walks down to the topmost
  // non-overlay and paints it, so the frame a peek sits on is drawn by a Reader whose
  // stream has been handed to the peek -- and ReaderScreen::render reads only `page_`
  // and `vm_`. If a release disturbed either, the page under the veil would be blank.
  Peeking p;
  pageForward(p.s(), 3);
  REQUIRE(p.s().pageIndex() == 3);
  REQUIRE(p.s().hasChapter());

  const int wasPage = p.s().pageIndex();
  const int wasCount = p.s().pageCount();
  const std::string wasText = readerfix::pageText(p.s().page());
  const Cursor wasCursor = p.s().currentCursor();
  const uint32_t wasBytes = p.s().chapterBytesRead();
  const int wasVmPage = p.s().vm().page;
  const int wasVmTotal = p.s().vm().pageTotal;
  // NOT A `0 == 0` ASSERTION. See the fixture note above: without this REQUIRE the
  // byte check below would hold over any implementation at all.
  REQUIRE(wasBytes > 0);
  REQUIRE_FALSE(wasText.empty());

  p.s().releaseChapter();
  CHECK_FALSE(p.s().hasChapter());

  CHECK(p.s().pageIndex() == wasPage);
  CHECK(p.s().pageCount() == wasCount);
  CHECK(readerfix::pageText(p.s().page()) == wasText);
  CHECK(p.s().currentCursor() == wasCursor);
  CHECK(p.s().vm().page == wasVmPage);
  CHECK(p.s().vm().pageTotal == wasVmTotal);
  // WHAT MAKES A SAVE SAFE WITH THE STREAM GONE. chapterBytesRead() is `pageBytes_`, a
  // plain member recorded at the end of the page on screen -- NOT
  // ChapterReader::bytesRead(), which gates on the InflateSource pointer and would
  // answer 0 the moment the window is freed. A 0 here would push progressPercent onto
  // its page/pageTotal fallback and persist a smaller number over a bigger one, which
  // is exactly the percentage-going-backwards bug this project has already shipped.
  CHECK(p.s().chapterBytesRead() == wasBytes);
}

// --- TAKING IT BACK -------------------------------------------------------------

TEST_CASE("reacquiring costs no seekTo, and the page comes back untouched") {
  Peeking p;
  pageForward(p.s(), 3);
  const int wasPage = p.s().pageIndex();
  const int wasCount = p.s().pageCount();
  const std::string wasText = readerfix::pageText(p.s().page());

  p.s().releaseChapter();
  // THE DECODE COUNT IS THE ASSERTION, on the same principle as the page ring's: the
  // claim is "closing a peek pays no rewind", and a test that only checked the page
  // came out right would pass just as happily with a full seekTo hidden inside. A
  // rewind costs what page you are ON -- ~1010 ms at page 99 on the device -- so
  // discarding a peek would cost more than committing one.
  const uint32_t decodes = p.s().ringStats().decodes;

  REQUIRE(p.s().reacquireChapter());
  CHECK(p.s().hasChapter());
  CHECK(p.s().ringStats().decodes == decodes);
  CHECK(p.s().pageIndex() == wasPage);
  CHECK(p.s().pageCount() == wasCount);
  CHECK(readerfix::pageText(p.s().page()) == wasText);
  // LEFT NULL ON PURPOSE. `pb_ == nullptr` is an already-handled state and its repair
  // has a home -- restreamAtCurrentPage, in a quiet window, where abandoning the walk
  // is free. Establishing it here would put the rewind on the press instead.
  CHECK_FALSE(p.s().hasLiveStream());
}

TEST_CASE("a reacquired Reader can be paged again, forward and back") {
  // THE HALF THE DECODE COUNT CANNOT MAKE: "nothing was decoded" is equally consistent
  // with a reacquire that established nothing usable at all.
  //
  // ONE PAGE OF RING, deliberately. At the default depth of 3 the forward turn below
  // would be answered by showCached() out of RAM, which proves nothing about the
  // stream -- the ring survives a release, since it is copies of laid-out lines. With
  // depth 1 the ring holds only the page on screen, so the turn has to go to the card.
  Peeking p;
  p.s().setPageCacheDepth(1);

  pageForward(p.s(), 1);
  const int nextPage = p.s().pageIndex();
  const std::string nextText = readerfix::pageText(p.s().page());
  REQUIRE(nextPage == 1);
  REQUIRE_FALSE(nextText.empty());
  pageBackward(p.s(), 1);
  REQUIRE(p.s().pageIndex() == 0);

  p.s().releaseChapter();
  REQUIRE(p.s().reacquireChapter());

  pageForward(p.s(), 1);
  CHECK(p.s().pageIndex() == nextPage);
  CHECK(readerfix::pageText(p.s().page()) == nextText);
  // AND BACK AGAIN, which is the rewind path rather than the advance one.
  pageBackward(p.s(), 1);
  CHECK(p.s().pageIndex() == 0);
}

// --- JUMPING TO A POSITION ------------------------------------------------------

TEST_CASE("goToPosition lands on the page containing a cursor and anchors the departure") {
  Peeking p;
  // WHERE THE READER IS BEFORE THE PEEK, and what the anchor must come to hold.
  const reader::AnchorPos departure = p.s().here();
  REQUIRE(p.s().pageIndex() == 0);

  // A REAL CURSOR FROM FURTHER DOWN, taken by going there rather than by inventing
  // one: a page-start cursor is `starts_[at_]`, and a hand-built {block, line} would
  // be a different assertion (that the walk lands on the page CONTAINING an arbitrary
  // cursor) dressed as this one.
  pageForward(p.s(), 2);
  const Cursor target = p.s().currentCursor();
  const int targetPage = p.s().pageIndex();
  const std::string targetText = readerfix::pageText(p.s().page());
  REQUIRE(targetPage == 2);
  REQUIRE(target != Cursor{});

  pageBackward(p.s(), 2);
  // ASSERTED PROPERLY, not as `x ? true : true`. The jump below is only about a
  // departure if the reader really is standing at the departure when it is made.
  REQUIRE(p.s().pageIndex() == 0);
  REQUIRE(p.s().here() == departure);

  REQUIRE(p.s().goToPosition(0, target));

  CHECK(p.s().chapterIndex() == 0);
  CHECK(p.s().pageIndex() == targetPage);
  CHECK(p.s().currentCursor() == target);
  CHECK(readerfix::pageText(p.s().page()) == targetText);
  // THE DEPARTURE, not the arrival. Paging back set an anchor of its own on the way
  // here; a jump overwrites it unconditionally, which is the rule that keeps a commit
  // from chapter 2 into chapter 8 from clearing the one breadcrumb the reader wanted.
  CHECK(p.s().anchor().isSet());
  CHECK(p.s().anchor().get() == departure);
}

TEST_CASE("goToPosition across a chapter boundary anchors and lands") {
  // THE CASE A PAGE-NUMBER SCHEME WOULD FAIL. `at_` indexes the CURRENT chapter's
  // `starts_`, so "page 3" means nothing once the spine entry has changed -- which is
  // the whole reason the anchor is a (spine, block, line) triple.
  Peeking p;
  pageForward(p.s(), 2);
  const reader::AnchorPos departure = p.s().here();
  REQUIRE(departure.spine == 0);

  REQUIRE(p.s().goToPosition(1, Cursor{}));

  CHECK(p.s().chapterIndex() == 1);
  CHECK(p.s().pageIndex() == 0);
  CHECK(p.s().anchor().isSet());
  CHECK(p.s().anchor().get() == departure);
  CHECK(p.s().anchor().get().spine == 0);
}

TEST_CASE("a refused goToPosition leaves the screen exactly where it was") {
  // A REFUSED JUMP IS NOT A DEPARTURE. Anchoring one would leave a way back to a page
  // the reader never left.
  //
  // TWO REFUSALS, because they take different exits and only the second can see where
  // the anchor is set. Out of range returns before anything is touched; an in-range
  // spine entry that paginates to nothing goes all the way into openChapterAt, which
  // restores the previous chapter itself. Spine 1 is empty here for exactly that --
  // walkToChapter SKIPS an entry with no pages and carries on in the direction it was
  // going, so an empty LAST entry is the only in-range spine that can fail.
  cardfix::CardReading r(readerfix::longChapter(40), reader::kBodyPpem,
                         reader::Settings{}.margins, Cursor{},
                         "<html><body><img src=\"cover.png\"/></body></html>");
  r.scr->completeIndex();
  reader::ReaderScreen& s = *r.scr;
  pageForward(s, 2);
  REQUIRE(s.pageIndex() == 2);

  const int wasChapter = s.chapterIndex();
  const int wasPage = s.pageIndex();
  const int wasCount = s.pageCount();
  const std::string wasText = readerfix::pageText(s.page());
  const Cursor wasCursor = s.currentCursor();
  const bool wasAnchored = s.anchor().isSet();
  const reader::AnchorPos wasAnchor = s.anchor().get();

  CHECK_FALSE(s.goToPosition(99, Cursor{4, 0}));

  CHECK(s.chapterIndex() == wasChapter);
  CHECK(s.pageIndex() == wasPage);
  CHECK(s.pageCount() == wasCount);
  CHECK(readerfix::pageText(s.page()) == wasText);
  CHECK(s.currentCursor() == wasCursor);
  CHECK(s.anchor().isSet() == wasAnchored);
  CHECK(s.anchor().get() == wasAnchor);

  // ...and the one that reaches the walk.
  CHECK_FALSE(s.goToPosition(1, Cursor{}));

  CHECK(s.chapterIndex() == wasChapter);
  CHECK(s.pageIndex() == wasPage);
  CHECK(s.pageCount() == wasCount);
  CHECK(readerfix::pageText(s.page()) == wasText);
  CHECK(s.currentCursor() == wasCursor);
  CHECK(s.anchor().isSet() == wasAnchored);
  CHECK(s.anchor().get() == wasAnchor);
}
