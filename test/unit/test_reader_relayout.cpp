// RE-PAGINATING WHERE THE READER IS STANDING.
//
// The Typography panel's apply path, and the one part of it that can be tested at
// all: the shell has no harness, so `relayout` carries the logic and the shell gets
// one call. What it has to get right is the thing a reader can perceive -- after a
// type change the page in front of them is the page holding the text they were
// reading, and NOT page one, which is what `setMetrics` alone would give.
//
// --- WHAT "WHERE THE READER IS" MEANS HERE, BECAUSE IT IS NOT OBVIOUS -----------
//
// `currentCursor()` is the START of the page on screen, which is also exactly what
// saveReadingPosition persists -- so it is this project's own notion of where the
// reader is, and relayout targets `{that block, 0}`.
//
// THE PAGE THAT COMES BACK IS THEREFORE NOT ALWAYS THE SAME PAGE, and asserting that
// it is passes only by luck. A page that began mid-block starts at `{b, 6}`, so
// dropping the line targets `{b, 0}`, which lives on the page BEFORE it -- and the
// reader lands a few lines earlier, re-reading rather than skipping. That is the safe
// direction and it is the same call reading_position.h's `Relaid` makes.
//
// So these tests compare against a REFERENCE screen: the same book opened fresh at
// the new geometry and landed on `{b, 0}` through restoreAt, which is by construction
// the page relayout must produce. A reference implementation rather than a pinned
// number, for the reason every byte-wise primitive in this repo has one.
#include <memory>
#include <string>

#include "card_book_fixture.h"
#include "doctest.h"
#include "ramp.h"
#include "reader_fixture.h"
#include "reader/layout.h"
#include "reader/screen_reader.h"
#include "reader/settings.h"
#include "reader/theme_quiet.h"

using reader::Cursor;
using reader::Gesture;

namespace {

// A chapter with enough pages that page one is not the answer to anything, and
// DEFERRED, so the re-pagination goes through the branch a real book's median
// chapter takes.
const int kBigPpem = 46;      // the top of kBodyPpemSteps
const int kWideMargin = 30;   // the top of kMarginSteps: 480 - 60 = a 420px column

// THE SHELL'S TWO HALVES, applied the way the device applies them: the FACE is
// re-inited in place (exactly as applyBodyPpem does) and the COLUMN is recomputed
// from the new margins.
reader::PageMetrics biggerInANarrowerColumn(cardfix::CardReading& r, int ppem, int margins) {
  REQUIRE(r.body.face.init(r.body.bytes.data(), r.body.bytes.size(), ppem));
  reader::Settings s;
  s.margins = margins;
  reader::PageMetrics m;
  r.theme.readerMetrics(480, 800, r.ramp.fonts, r.body.face, s, m);
  REQUIRE(m.columnW == 480 - 2 * margins);
  return m;
}

void readIn(cardfix::CardReading& r, int pages) {
  for (int i = 0; i < pages; ++i) r.scr->onGesture({Gesture::Next});
  REQUIRE(r.scr->pageIndex() == pages);
}

}  // namespace

TEST_CASE("relayout lands on the page holding the top of the reader's block") {
  // THE PROPERTY THAT MATTERS, and the only one the reader can perceive.
  const std::string ch = readerfix::deferredChapter();
  cardfix::CardReading r(ch);
  REQUIRE(r.scr->completeIndex());
  readIn(r, 4);
  const Cursor was = r.scr->currentCursor();
  REQUIRE(was.block > 0);
  REQUIRE(r.scr->pageIndex() > 0);

  r.scr->relayout(biggerInANarrowerColumn(r, kBigPpem, kWideMargin));

  // THE REFERENCE: the same book opened fresh at the new geometry, landed on the top
  // of the same block. Whatever page that is, it is the page relayout owes.
  cardfix::CardReading ref(ch, kBigPpem, kWideMargin, Cursor{was.block, 0});

  CHECK(r.scr->pageIndex() == ref.scr->pageIndex());
  CHECK(r.scr->currentCursor() == ref.scr->currentCursor());
  CHECK(readerfix::pageText(r.scr->page()) == readerfix::pageText(ref.scr->page()));
  // ...and it is a real page rather than the top of the chapter. Without this the
  // checks above would all be satisfied by both screens failing the same way.
  CHECK(r.scr->pageIndex() > 0);
  CHECK_FALSE(r.scr->page().lines.empty());
}

TEST_CASE("relayout drops the reader's LINE, so a mid-block page lands one earlier") {
  // WHAT THE DROPPED LINE ACTUALLY COSTS, isolated by relaying out at metrics that
  // move nothing measurable: `justify` changes how a finished line is SET and never
  // where it ends, so the pagination is identical either side and the ONLY thing that
  // can move the page is the cursor this function chose to target.
  //
  // A page that began mid-block therefore lands one page earlier, which is the safe
  // direction -- a few lines re-read rather than skipped. Keeping the line would land
  // on the page already showing, which is why this is the case that catches it.
  const std::string ch = readerfix::deferredChapter();
  cardfix::CardReading r(ch);
  REQUIRE(r.scr->completeIndex());

  // Walk forward to a page that starts mid-block. Most do, on a chapter of
  // multi-line paragraphs, but the walk is what makes the test say so.
  int page = 0;
  while (r.scr->currentCursor().line == 0 && page < 8) {
    r.scr->onGesture({Gesture::Next});
    ++page;
  }
  const Cursor was = r.scr->currentCursor();
  REQUIRE(was.line > 0);
  const int wasPage = r.scr->pageIndex();
  REQUIRE(wasPage > 0);

  reader::PageMetrics ragged = r.m;
  ragged.justify = false;
  r.scr->relayout(ragged);

  CHECK(r.scr->pageIndex() == wasPage - 1);
  // AND IT IS THE PAGE HOLDING `{was.block, 0}`, not merely the one before. Checked
  // against a screen landed on that cursor rather than against the previous page's
  // start block, which was the first attempt and is not a property: a page spans
  // however many blocks fit, so the page before one starting in block 2 can perfectly
  // well start in block 0.
  cardfix::CardReading ref(ch, reader::kBodyPpem, reader::Settings{}.margins,
                           Cursor{was.block, 0});
  CHECK(r.scr->pageIndex() == ref.scr->pageIndex());
  CHECK(r.scr->currentCursor() == ref.scr->currentCursor());
}

TEST_CASE("relayout re-paginates: the page count changes with the type") {
  cardfix::CardReading r(readerfix::deferredChapter());
  REQUIRE(r.scr->completeIndex());
  const int small = r.scr->pageCount();
  REQUIRE(small > 1);

  r.scr->relayout(biggerInANarrowerColumn(r, kBigPpem, kWideMargin));
  // THE TOTAL IS UNKNOWN AGAIN, which is what design/Typography.dc.html's footnote
  // promises when it says the book re-paginates in the background: the footer draws
  // its em dash until the deferred count lands.
  CHECK(r.scr->indexPending());

  REQUIRE(r.scr->completeIndex());
  // Bigger type in a narrower column is strictly more pages. Asserted as an
  // INEQUALITY rather than a number, because the exact count is the layout's business
  // and pinning it here would make this a golden in disguise.
  CHECK(r.scr->pageCount() > small);
}

TEST_CASE("relayout serves no page that was laid at the old metrics") {
  // A cached page is lines measured against one column and one face, so every page in
  // the ring is wrong the moment either moves -- and cachePage does NOT refresh an
  // entry it already holds ("already held; move it to the front rather than
  // re-copying it"), so a stale page survives the walk that would otherwise overwrite
  // it.
  //
  // PAGE 0 IS THE CASE THAT CATCHES IT, and finding that out was what the mutation was
  // for. Two things had to line up:
  //
  //   * ITS KEY IS `{0, 0}` IN EVERY LAYOUT. Every other page's start cursor moves when
  //     the column or the face does, so a stale entry for it simply never matches the
  //     new index and is dead weight rather than a wrong answer. Page 0's collides.
  //   * NOTHING MAY HAPPEN IN BETWEEN. The first version of this read four pages in and
  //     then paged back, and the walk's own insertions had evicted the stale entry long
  //     before anything could read one -- so deleting the dropPageRing() this test is
  //     about changed nothing and the test passed. On page 0 the stale page is the page
  //     ON SCREEN the instant relayout returns, with no eviction arithmetic in the way.
  //
  // NOT ASSERTED AS AN EMPTY RING either, which was the other wrong attempt: the walk
  // to the reader's block legitimately repopulates it, with pages laid at the NEW
  // metrics. What has to be true is about the CONTENT, so it is checked against a
  // reference screen that never had a ring to go stale.
  const std::string ch = readerfix::deferredChapter();
  cardfix::CardReading r(ch);
  REQUIRE(r.scr->completeIndex());
  REQUIRE(r.scr->pageIndex() == 0);
  const size_t oldLines = r.scr->page().lines.size();

  r.scr->relayout(biggerInANarrowerColumn(r, kBigPpem, kWideMargin));

  cardfix::CardReading ref(ch, kBigPpem, kWideMargin);
  REQUIRE(ref.scr->pageIndex() == 0);
  CHECK(readerfix::pageText(r.scr->page()) == readerfix::pageText(ref.scr->page()));
  // AND THE TWO LAYOUTS REALLY DO DIFFER, so the check above is not comparing a page
  // with itself: bigger type in a narrower column fits fewer lines.
  CHECK(ref.scr->page().lines.size() < oldLines);
}

TEST_CASE("relayout with nothing open changes nothing and does not crash") {
  // The in-memory constructor, which the goldens and the simulator use. There is no
  // book to page into, so this takes the other branch of relayout entirely -- the same
  // shape openChapterAt needed for its own edge, where the moved-out index was never
  // put back and pressing past the last page left the screen reporting zero pages.
  readerfix::Reading r(readerfix::longChapter(20));
  REQUIRE(r.scr->pageCount() > 1);
  for (int i = 0; i < 2; ++i) r.scr->onGesture({Gesture::Next});
  const Cursor was = r.scr->currentCursor();
  REQUIRE(was.block > 0);

  reader::PageMetrics narrower = r.m;
  narrower.columnW = 420;
  r.scr->relayout(narrower);

  CHECK(r.scr->pageCount() >= 1);
  CHECK_FALSE(r.scr->page().lines.empty());
  // The in-memory path goes through setMetrics' own restore landing, so it keeps the
  // same promise the card path does rather than being a second pagination that only
  // the desktop takes: never past the reader's block, and never back at page one when
  // the reader was not there.
  CHECK(r.scr->currentCursor().block <= was.block);
  CHECK(r.scr->pageIndex() > 0);
}
