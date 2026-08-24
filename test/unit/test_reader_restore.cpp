// Restoring a saved reading position: does the reader come back to the page it left?
//
// The strongest test here is the one this project already learned to want for paging
// -- READING BACKWARD GIVES EXACTLY THE PAGES READING FORWARD GAVE. Its restore
// equivalent is that a cursor saved on a page and handed back later reproduces THAT
// page, and it is checked for every page of a chapter rather than for one, because a
// walk that stops one boundary early or late is right at page 1 and wrong everywhere
// else.
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

namespace {

// Every page of a chapter, read forward: what it said and where it began.
struct Walked {
  std::vector<Cursor> cursors;
  std::vector<std::string> text;
};

Walked readForward(const std::string& doc, int maxPages = 400) {
  Walked w;
  Reading r(doc);
  for (int i = 0; i < maxPages; ++i) {
    w.cursors.push_back(r.scr->currentCursor());
    w.text.push_back(pageText(r.scr->page()));
    const int was = r.scr->pageIndex();
    r.scr->onGesture({reader::Gesture::Next});
    if (r.scr->pageIndex() == was) break;  // the chapter's last page
  }
  return w;
}

}  // namespace

TEST_CASE("a saved cursor restores THE SAME PAGE, for every page of a chapter") {
  const std::string doc = longChapter(40);
  const Walked w = readForward(doc);
  // A guard against the test passing by testing nothing -- the defect this project
  // has hit twice, most recently a probe whose check `0 == 0` satisfied.
  REQUIRE(w.cursors.size() > 5);

  for (size_t p = 0; p < w.cursors.size(); ++p) {
    Reading back(doc, /*settled=*/true, 480, 800, w.cursors[p]);
    CHECK(pageText(back.scr->page()) == w.text[p]);
    // And the FOOTER is right, not just the text: the walk records the boundaries it
    // passes precisely so the page number is a count of them.
    CHECK(back.scr->pageIndex() == static_cast<int>(p));
    CHECK(back.scr->vm().page == static_cast<int>(p) + 1);
  }
}

TEST_CASE("restoring page one is the same as not restoring at all") {
  // Cursor{} is both "no target" and "the top of the chapter", and openAtCursor takes
  // the cheap path for it rather than walking to a boundary it already knows.
  const std::string doc = longChapter(40);
  Reading plain(doc);
  Reading restored(doc, /*settled=*/true, 480, 800, Cursor{});
  CHECK(pageText(restored.scr->page()) == pageText(plain.scr->page()));
  CHECK(restored.scr->pageIndex() == 0);
  CHECK(restored.scr->pageCount() == plain.scr->pageCount());
}

TEST_CASE("a mid-chapter restore knows its page number before it knows the total") {
  // The whole reason the walk stops at the target: a restore costs a walk to the
  // reader's page, not to the end of the chapter. So the total is still unknown at
  // that moment -- and the footer must say `7 / -`, with a right numerator and an
  // honest denominator, rather than `1 / -`.
  const std::string doc = deferredChapter();
  const Walked w = readForward(doc, 12);
  REQUIRE(w.cursors.size() >= 6);
  const size_t target = 5;

  Reading r(doc, /*settled=*/false, 480, 800, w.cursors[target]);
  CHECK(pageText(r.scr->page()) == w.text[target]);
  CHECK(r.scr->pageIndex() == static_cast<int>(target));
  CHECK(r.scr->vm().page == static_cast<int>(target) + 1);
  CHECK(r.scr->indexPending());       // the count has NOT been paid
  CHECK(r.scr->vm().pageTotal == 0);  // ...and is reported as unknown, not as a guess

  // Completing it in the quiet window fills the total in and does not move the reader.
  REQUIRE(r.scr->completeIndex());
  CHECK_FALSE(r.scr->indexPending());
  CHECK(r.scr->vm().page == static_cast<int>(target) + 1);
  CHECK(r.scr->vm().pageTotal > static_cast<int>(target) + 1);
  CHECK(pageText(r.scr->page()) == w.text[target]);
}

TEST_CASE("a cursor past the end of the chapter lands on its last page") {
  // A shorter chapter at the same path, or a record written against a shorter
  // column. The end of the chapter is the closest honest answer to "past the end".
  const std::string doc = longChapter(10);
  Reading plain(doc);
  const int pages = plain.scr->pageCount();
  REQUIRE(pages > 1);

  Reading r(doc, /*settled=*/true, 480, 800, Cursor{999999, 0});
  CHECK(r.scr->pageIndex() == pages - 1);
  CHECK(r.scr->pageCount() == pages);
  // The whole chapter really was walked, so the count is known rather than pending.
  CHECK_FALSE(r.scr->indexPending());
  CHECK_FALSE(r.scr->page().lines.empty());
}

TEST_CASE("a restored page still turns, forward and back") {
  // The landing has to leave the stream in the state an ordinary landing leaves it,
  // or the first page turn after a restore misbehaves -- and a restore is followed by
  // a page turn essentially always.
  const std::string doc = longChapter(40);
  const Walked w = readForward(doc);
  REQUIRE(w.cursors.size() > 4);
  const size_t target = 3;

  Reading r(doc, /*settled=*/true, 480, 800, w.cursors[target]);
  REQUIRE(r.scr->pageIndex() == static_cast<int>(target));

  r.scr->onGesture({reader::Gesture::Next});
  CHECK(r.scr->pageIndex() == static_cast<int>(target) + 1);
  CHECK(pageText(r.scr->page()) == w.text[target + 1]);

  r.scr->onGesture({reader::Gesture::Prev});
  CHECK(r.scr->pageIndex() == static_cast<int>(target));
  CHECK(pageText(r.scr->page()) == w.text[target]);

  // ...and all the way back to the top, which is the path that rewinds and re-decodes.
  for (int i = 0; i < 8 && r.scr->pageIndex() > 0; ++i)
    r.scr->onGesture({reader::Gesture::Prev});
  CHECK(r.scr->pageIndex() == 0);
  CHECK(pageText(r.scr->page()) == w.text[0]);
}

TEST_CASE("a restore target is spent, not remembered") {
  // It is cleared as it is used, so a later chapter opened by paging off the end of
  // this one starts at ITS page one. A cursor that persisted would land every
  // subsequent chapter at some unrelated block.
  const std::string doc = longChapter(40);
  const Walked w = readForward(doc);
  REQUIRE(w.cursors.size() > 4);

  Reading r(doc, /*settled=*/true, 480, 800, w.cursors[3]);
  REQUIRE(r.scr->pageIndex() == 3);
  // There is no book behind an in-memory chapter, so paging off the end stops here --
  // what matters is that a second landing does not re-consume the cursor.
  r.scr->onGesture({reader::Gesture::Prev});
  r.scr->onGesture({reader::Gesture::Prev});
  r.scr->onGesture({reader::Gesture::Prev});
  CHECK(r.scr->pageIndex() == 0);
  CHECK(pageText(r.scr->page()) == w.text[0]);
}

TEST_CASE("a cursor keeps its BLOCK across a re-layout, which is what makes it worth saving") {
  // reading_position.h grades a geometry change as Relaid: the block survives, the
  // line does not. This is the property that claim rests on -- the same block index
  // at a different column width still lands on the text that block holds, where a
  // saved page NUMBER would not.
  const std::string doc = longChapter(40);
  const Walked wide = readForward(doc);
  REQUIRE(wide.cursors.size() > 6);
  const Cursor saved = wide.cursors[6];
  REQUIRE(saved.block > 0);

  // The narrower X3-shaped panel: more lines per page or fewer, so page 6 is a
  // different stretch of the chapter.
  Reading narrow(doc, /*settled=*/true, 528, 792, Cursor{saved.block, 0});
  CHECK_FALSE(narrow.scr->page().lines.empty());
  // The block the cursor named is on the page it landed on -- checked through
  // LaidLine::block, which the page index needs anyway.
  bool holdsBlock = false;
  for (const reader::LaidLine& ln : narrow.scr->page().lines)
    if (ln.block == saved.block) holdsBlock = true;
  CHECK(holdsBlock);
}

// --- The chapter's name ---------------------------------------------------------

TEST_CASE("the header shows the chapter's NAME when the contents supply one") {
  // The label was composed from a spine POSITION for two phases, because the spine gives
  // an order and no names. toc.h supplies them.
  Reading r(longChapter(10));
  // The in-memory chapter is spine 0 as far as the screen is concerned.
  r.scr->setChapterNames({{0, 1, "LE CERCLE S'OUVRE"}});
  CHECK(r.scr->vm().chapter == "LE CERCLE S'OUVRE");
}

TEST_CASE("the position is the fallback, for a book or a chapter with no name") {
  Reading r(longChapter(10));
  // No contents at all -- a book with no NCX still reads.
  r.scr->setChapterNames({});
  CHECK(r.scr->vm().chapter == "CH. 01");
  // ...and a contents that does not mention THIS chapter. Spine entry 0 of a real book
  // is its cover, and nothing names that.
  r.scr->setChapterNames({{7, 1, "LIVRE I"}});
  CHECK(r.scr->vm().chapter == "CH. 01");
}

TEST_CASE("an empty label falls back rather than showing nothing") {
  // A malformed NCX can carry an entry with a target and no text -- toc.h skips those,
  // but the screen must not depend on that to avoid an empty header.
  Reading r(longChapter(10));
  r.scr->setChapterNames({{0, 1, ""}});
  CHECK(r.scr->vm().chapter == "CH. 01");
}

TEST_CASE("the LAST entry naming a chapter wins, which is where you are in it") {
  // Several entries can point into one file; the later ones are further into it.
  Reading r(longChapter(10));
  r.scr->setChapterNames({{0, 1, "PART ONE"}, {0, 2, "Section two"}});
  CHECK(r.scr->vm().chapter == "Section two");
}
