// THE PEEK, BOTH HALVES OF IT.
//
// First the READER's: letting go of a chapter, taking it back, and jumping to a cursor
// rather than to page one. Those come first in this file because the peek cannot be
// built without them.
//
// Then the PANEL's, from `--- THE PANEL ITSELF ---` down: what it declares, what its
// band says, what its four buttons do, and the one property everything else rests on --
// that its page is not the Reader's, because its column is narrower.
#include <cstdint>
#include <memory>
#include <string>

#include "card_book_fixture.h"
#include "doctest.h"
#include "ramp.h"
#include "reader_fixture.h"
#include "reader/screen_peek.h"
#include "reader/screen_reader.h"
#include "reader/reading_store.h"
#include "reader/screens.h"
#include "reader/theme.h"
#include "reader/theme_quiet.h"

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
  // ChapterReader::bytesRead(), which gates on `inflateActive_` and would
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

TEST_CASE("goToPosition lands on the page containing a cursor, and the mark travels with it") {
  Peeking p;
  // WHERE THE READER IS BEFORE THE PEEK.
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
  // THE HIGH-WATER MARK, which the forward paging above already put on the target page
  // -- so committing FORWARD lands ON it and promises nothing. That is the cost
  // return_anchor.h states outright: a forward commit leaves no way back, where the old
  // departure rule nominally offered one and measurably kept it for a single press.
  CHECK(p.s().anchor().isSet());
  CHECK(p.s().anchor().get() == p.s().here());
  CHECK_FALSE(p.s().anchor().aheadOf(p.s().here()));
  CHECK(departure < p.s().anchor().get());  // and the mark really did move on

  // AND THE BACKWARD COMMIT IS THE ONE THAT LEAVES A WAY BACK, which is the case the
  // whole rule turns on: nothing lowers the mark, so it stands where the reader was.
  const reader::AnchorPos furthest = p.s().anchor().get();
  REQUIRE(p.s().goToPosition(0, Cursor{}));
  CHECK(p.s().pageIndex() == 0);
  CHECK(p.s().anchor().get() == furthest);
  CHECK(p.s().anchor().aheadOf(p.s().here()));
}

TEST_CASE("goToPosition across a chapter boundary lands, and the mark crosses with it") {
  // THE CASE A PAGE-NUMBER SCHEME WOULD FAIL. `at_` indexes the CURRENT chapter's
  // `starts_`, so "page 3" means nothing once the spine entry has changed -- which is
  // the whole reason the mark is a (spine, block, line) triple, and the reason the
  // comparison below can be made at all.
  Peeking p;
  pageForward(p.s(), 2);
  const reader::AnchorPos departure = p.s().here();
  REQUIRE(departure.spine == 0);

  REQUIRE(p.s().goToPosition(1, Cursor{}));

  CHECK(p.s().chapterIndex() == 1);
  CHECK(p.s().pageIndex() == 0);
  CHECK(p.s().anchor().isSet());
  // FORWARD ACROSS A BOUNDARY, so the mark comes too and nothing is promised.
  CHECK(p.s().anchor().get().spine == 1);
  CHECK(p.s().anchor().get() == p.s().here());
  CHECK_FALSE(p.s().anchor().aheadOf(p.s().here()));

  // ...and jumping BACK into chapter 0 leaves it standing in chapter 1, which is the
  // way back a reader who overshot actually wants.
  REQUIRE(p.s().goToPosition(0, Cursor{}));
  CHECK(p.s().chapterIndex() == 0);
  CHECK(p.s().anchor().get().spine == 1);
  CHECK(p.s().anchor().aheadOf(p.s().here()));
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
  // THE VIEW MODEL IS PART OF "WHERE IT WAS", and it was the half nothing checked.
  // `vm_` is what the panel underneath is drawn from and what the footer reads, so a
  // refusal that left the index empty and the view model describing the old page is
  // a screen whose two halves disagree -- a blank reading column under a footer still
  // reading `3 / 24`. Cheap to assert and it is the observable the device reported.
  const std::string wasVmChapter = s.vm().chapter;
  const int wasVmPage = s.vm().page;
  const int wasVmTotal = s.vm().pageTotal;

  CHECK_FALSE(s.goToPosition(99, Cursor{4, 0}));

  CHECK(s.chapterIndex() == wasChapter);
  CHECK(s.pageIndex() == wasPage);
  CHECK(s.pageCount() == wasCount);
  CHECK(readerfix::pageText(s.page()) == wasText);
  CHECK(s.currentCursor() == wasCursor);
  CHECK(s.anchor().isSet() == wasAnchored);
  CHECK(s.anchor().get() == wasAnchor);
  CHECK(s.vm().chapter == wasVmChapter);
  CHECK(s.vm().page == wasVmPage);
  CHECK(s.vm().pageTotal == wasVmTotal);

  // ...and the one that reaches the walk. THE TARGET CHAPTER IS REALLY OPENED HERE --
  // chapter_.begin() succeeds on spine 1 and it is the PAGINATION that yields nothing
  // -- so this is the refusal that happens with the walk already standing on the
  // target, which is the shape the atomicity claim is about.
  CHECK_FALSE(s.goToPosition(1, Cursor{}));

  CHECK(s.chapterIndex() == wasChapter);
  CHECK(s.pageIndex() == wasPage);
  CHECK(s.pageCount() == wasCount);
  CHECK(readerfix::pageText(s.page()) == wasText);
  CHECK(s.currentCursor() == wasCursor);
  CHECK(s.anchor().isSet() == wasAnchored);
  CHECK(s.anchor().get() == wasAnchor);
  CHECK(s.vm().chapter == wasVmChapter);
  CHECK(s.vm().page == wasVmPage);
  CHECK(s.vm().pageTotal == wasVmTotal);
}

TEST_CASE("a refused goToPosition WITHIN the open chapter leaves the screen alone too") {
  // THE HALF THE TEST ABOVE CANNOT SEE, and the half that was unprotected: a commit
  // whose spine is the chapter already open. It used to skip openChapterAt entirely
  // and call openAtCursor directly -- and openAtCursor clears `starts_`, drops `pb_`
  // and assigns `page_ = Page{}` BEFORE its first failure check, so a failure there
  // left the screen with no index, no page and a view model still describing the one
  // that had gone.
  //
  // THE FAILURE IS THE REVIEWER'S OWN SCENARIO: the card pulled between the shell's
  // reacquireChapter() and the walk. The shell logs a refusal and carries on, so the
  // seconds before pollCardPresence reroutes are seconds in which a quiet-window save
  // can fire -- and it would have written `block=0` over the reader's real place.
  cardfix::CardReading r(readerfix::longChapter(40));
  reader::ReaderScreen& s = *r.scr;
  s.completeIndex();
  pageForward(s, 3);
  REQUIRE(s.pageIndex() == 3);
  const Cursor target = s.currentCursor();
  REQUIRE(target != Cursor{});

  const int wasChapter = s.chapterIndex();
  const int wasCount = s.pageCount();
  const std::string wasText = readerfix::pageText(s.page());
  const std::string wasVmChapter = s.vm().chapter;
  const int wasVmTotal = s.vm().pageTotal;
  REQUIRE_FALSE(wasText.empty());
  REQUIRE(wasCount > 4);

  // A JUMP FORWARD INSIDE THIS CHAPTER, to a page that is genuinely elsewhere -- so a
  // screen that moved would be visible, rather than one that happened to stay put.
  pageBackward(s, 3);
  REQUIRE(s.pageIndex() == 0);
  const Cursor home = s.currentCursor();
  const std::string homeText = readerfix::pageText(s.page());
  REQUIRE(homeText != wasText);
  // TAKEN HERE, NOT ABOVE: paging raises an anchor of its own, so a snapshot from
  // before the paging would be asserting that the JUMP did not move something the
  // walk back already had.
  const bool wasAnchored = s.anchor().isSet();
  const reader::AnchorPos wasAnchor = s.anchor().get();

  r.fs.setMounted(false);
  CHECK_FALSE(s.goToPosition(0, target));

  CHECK(s.chapterIndex() == wasChapter);
  CHECK(s.pageIndex() == 0);
  CHECK(s.pageCount() == wasCount);
  CHECK(readerfix::pageText(s.page()) == homeText);
  CHECK(s.currentCursor() == home);
  CHECK(s.anchor().isSet() == wasAnchored);
  CHECK(s.anchor().get() == wasAnchor);
  CHECK(s.vm().chapter == wasVmChapter);
  CHECK(s.vm().page == 1);
  CHECK(s.vm().pageTotal == wasVmTotal);
}

TEST_CASE("goToPosition walks the target chapter ONCE") {
  // A GREEN SUITE CANNOT TELL ONE WALK FROM TWO -- both produce the right page. This
  // is the same instrument test_reader_restore.cpp uses for the identical claim about
  // a restored position: `stored` counts pages LAID OUT, which is the walk's real unit
  // of work, and `decodes` counts the rewind-and-walk-forward that seekTo is.
  //
  // WHAT IT WAS: openChapterAt landed on page ONE (one page laid out, one decode) and
  // openAtCursor then rewound and walked the whole prefix again. On the device that is
  // ~380 ms thrown away on a median 53 KB chapter and ~2.3 s on a long one, paid on
  // the press the reader is waiting on.
  cardfix::CardReading r(readerfix::longChapter(40), reader::kBodyPpem,
                         reader::Settings{}.margins, Cursor{},
                         readerfix::longChapter(40));
  reader::ReaderScreen& s = *r.scr;

  // A REAL CURSOR FROM CHAPTER 1, taken by going there: a hand-built {block, line}
  // would be a different assertion dressed as this one.
  REQUIRE(s.goToChapter(1));
  pageForward(s, 4);
  const Cursor target = s.currentCursor();
  const int targetPage = s.pageIndex();
  const std::string targetText = readerfix::pageText(s.page());
  REQUIRE(targetPage == 4);
  REQUIRE(s.pageCount() > 5);  // the chapter really does run past the target
  REQUIRE(s.goToChapter(0));
  REQUIRE(s.chapterIndex() == 0);

  const uint32_t storedBefore = s.ringStats().stored;
  const uint32_t decodesBefore = s.ringStats().decodes;

  REQUIRE(s.goToPosition(1, target));

  CHECK(s.chapterIndex() == 1);
  CHECK(s.pageIndex() == targetPage);
  CHECK(readerfix::pageText(s.page()) == targetText);
  // FIVE PAGES FOR A FIVE-PAGE PREFIX, and not a sixth: the sixth is the page-one
  // landing the old form paid for and then threw away. Measured, not predicted -- the
  // two-call form reads 6 and 1 here.
  CHECK(s.ringStats().stored - storedBefore == static_cast<uint32_t>(targetPage) + 1);
  // AND NO seekTo AT ALL. `stored` alone cannot see the eager count a small chapter
  // takes, because a counting walk lays out no lines; the decode counter can, because
  // the page-one landing that follows one is a seekTo.
  CHECK(s.ringStats().decodes == decodesBefore);
}

// --- THE PANEL ITSELF -----------------------------------------------------------

namespace {

// A PEEK OVER AN IN-MEMORY CHAPTER, at the panel's own metrics rather than the page's.
// The distinction is the whole feature: peekMetrics places a ~368px column inside the
// inset panel where readerMetrics places the reading page's 444px one, so a fixture
// that reached for the wrong one would build a screen that agrees with the Reader --
// which is precisely the state case 7 exists to refuse.
struct PeekFix {
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  readerfix::Body body;
  reader::PageMetrics m;
  std::unique_ptr<reader::PeekScreen> scr;

  explicit PeekFix(int percent = 4, int w = 480, int h = 800) {
    theme.peekMetrics(w, h, ramp.fonts, body.face, reader::Settings{}, m);
    scr = std::make_unique<reader::PeekScreen>(readerfix::longChapter(40), "CH. 01",
                                               percent, &body.face);
    scr->setMetrics(m);
  }
  reader::PeekScreen& s() { return *scr; }
  std::string text() { return readerfix::pageText(scr->page()); }
  // WHAT THE BOX DERIVED, asked of the theme rather than read off a constant: the
  // panel's height is fixed and its line count follows from the type ramp and the
  // reader's lead, so there is no number a test can pin instead.
  int lines() const {
    return theme.peekVisibleLines(ramp.fonts, body.face, reader::Settings{});
  }
};

}  // namespace

TEST_CASE("the peek is an overlay, declares Grayscale, and promises two buttons") {
  PeekFix p;
  CHECK(p.s().id() == reader::ScreenId::Peek);
  // A PANEL OVER THE PAGE, not a screen instead of it -- App::render paints the Reader
  // underneath and this draws its veil over it.
  CHECK(p.s().isOverlay());
  // BODY TEXT AT READING SIZE, so the Reader's reason applies verbatim. It is the one
  // overlay that does not declare Mono, and it costs the partial repaint for it.
  CHECK(p.s().fidelity() == reader::Fidelity::Grayscale);

  CHECK(p.s().vm().hints[0] == "CLOSE");
  CHECK(p.s().vm().hints[1] == "GO HERE");
  // EMPTY, NOT ABSENT. An empty hint slot is 36px wide (kHintEmptySlotW) and the bar
  // divides its leftover around it -- measuring one as zero is not "drawing nothing",
  // it is drawing the other two in the wrong places.
  CHECK(p.s().vm().hints[2] == "");
  CHECK(p.s().vm().hints[3] == "");

  // NO HOLD PROMISED AND THEREFORE NONE BOUND -- one field drives the ring and the
  // binding, so these cannot disagree.
  CHECK(p.s().longPressable() == 0);
  // AND NO AUTO-REPEAT: every page of this panel costs a decode, so a held side button
  // would run away from what the reader can follow.
  CHECK(p.s().autoRepeat() == 0);
}

TEST_CASE("the peek's band names the state and where it is, and never a page number") {
  PeekFix p(7);
  // NAMES THE STATE. `CH. 01 · 7%` alone would read as the Reader's own header, and
  // this panel has to be unmistakably not that.
  CHECK(p.s().vm().title == "PEEK");

  const std::string where = p.s().vm().where;
  CHECK(where.find("CH. 01") != std::string::npos);
  CHECK(where.find("7%") != std::string::npos);
  // THE ASSERTION WORTH MAKING. The panel is inset, so its column re-wraps, so "page 53"
  // in here is not page 53 of the book -- a page number would be a claim about the book
  // that is false. Chapter and percent are true at any column width.
  CHECK(where.find('/') == std::string::npos);
  // AND THE DOT IS THE REAL U+00B7. A C++ hex escape is unbounded, so "\xC2\xB7CH."
  // parses `\xB7C` as one escape -- clang rejects it and the ESP32's GCC accepts it,
  // emitting a byte that is not this. Adjacent literals are what end the escape, and
  // this is what says they did.
  CHECK(where.find("\xC2\xB7") != std::string::npos);
}

namespace {

// A CARD-BACKED PEEK, which the band's percent needs and PeekFix cannot give: the
// number is a fraction of the BOOK's bytes, so a peek with no book behind it has
// nothing to derive it from and correctly keeps the caller's figure. Same two-spine
// EPUB cardfix builds for the Reader, at the PANEL's metrics.
struct CardPeek {
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  readerfix::Body body;
  reader::PageMetrics m;
  FakeFileSystem fs;
  reader::OpenedBook ob;
  std::unique_ptr<reader::PeekScreen> scr;

  CardPeek(const std::string& ch1, const std::string& ch2, int spine) {
    theme.peekMetrics(480, 800, ramp.fonts, body.face, reader::Settings{}, m);
    REQUIRE(fs.writeAll("/books/b.epub", cardfix::epubWith(ch1, ch2)));
    const char* why = "";
    REQUIRE_MESSAGE(reader::openBook(fs, "/books/b.epub", ob, &why), std::string(why));
    scr = std::make_unique<reader::PeekScreen>(fs, ob, spine, &body.face);
    scr->setMetrics(m);
  }
  reader::PeekScreen& s() { return *scr; }
};

// The band is one composed run, so the percent is asserted on the END of it rather
// than with find() -- `find("5%")` is satisfied by "15%" and by "5%" alike.
bool bandSays(const reader::PeekScreen& p, int percent) {
  const std::string want = " " + std::to_string(percent) + "%";
  const std::string& where = p.vm().where;
  return where.size() >= want.size() &&
         where.compare(where.size() - want.size(), want.size(), want) == 0;
}

}  // namespace

TEST_CASE("the band's percent follows the chapter the peek is SHOWING") {
  // PAGING OFF THE END OF A PEEKED CHAPTER CROSSES INTO THE NEXT ONE -- the class
  // header lists that as a designed property -- and the percent used to be fixed at
  // construction while the label followed the crossing. The band then read
  // `CH. 02 · 0%` with 0% being chapter 1's start: two halves of one run describing
  // different chapters, in the only positional information this panel offers, and the
  // number the reader decides GO HERE on.
  //
  // A SHORT FIRST CHAPTER AND A LONG SECOND, so the two chapters' start fractions are
  // far apart -- the percentage is a fraction of the book's BYTES, so two chapters of
  // equal size would put the boundary at 50% and a bug that reported the wrong one of
  // them would still have to be told from a rounding difference.
  CardPeek p(readerfix::longChapter(2), readerfix::longChapter(40), 0);
  const int atFirst = reader::progressPercent(p.ob, 0, 1, 0, 0);
  const int atSecond = reader::progressPercent(p.ob, 1, 1, 0, 0);
  REQUIRE(atFirst == 0);          // the book starts at its start
  REQUIRE(atSecond > atFirst);    // ...and the fixture really does separate them

  REQUIRE(p.s().chosenSpine() == 0);
  const std::string wasWhere = p.s().vm().where;
  CHECK(bandSays(p.s(), atFirst));

  // PAGE OFF THE END. Bounded rather than counted: how many pages a two-paragraph
  // chapter makes in a 368px column is a fact about the wrap, and this test is not
  // about the wrap.
  bool crossed = false;
  for (int i = 0; i < 20 && !crossed; ++i) {
    p.s().onGesture({Gesture::Next});
    crossed = p.s().chosenSpine() == 1;
  }
  REQUIRE(crossed);

  CHECK(bandSays(p.s(), atSecond));
  // AND BOTH HALVES MOVED. Without this the check above would still pass if the label
  // had frozen instead -- the defect being fixed is precisely one half moving alone.
  CHECK(p.s().vm().where != wasWhere);
  CHECK(p.s().vm().where.find("CH. 02") != std::string::npos);

  // ...AND BACK. The crossing is symmetric, so the number has to come back with it.
  bool returned = false;
  for (int i = 0; i < 20 && !returned; ++i) {
    p.s().onGesture({Gesture::Prev});
    returned = p.s().chosenSpine() == 0;
  }
  REQUIRE(returned);
  CHECK(bandSays(p.s(), atFirst));
}

TEST_CASE("the sides page inside the peek and the front row does nothing") {
  PeekFix p;
  const std::string first = p.text();
  REQUIRE_FALSE(first.empty());

  reader::Action a = p.s().onGesture({Gesture::Next});
  CHECK(a.kind == reader::Action::Kind::Redraw);
  const std::string second = p.text();
  CHECK(second != first);

  a = p.s().onGesture({Gesture::Prev});
  CHECK(a.kind == reader::Action::Kind::Redraw);
  CHECK(p.text() == first);

  // THE FRONT ROW IS DEAD, and it has to be asserted through onEvent rather than
  // through onGesture. `declareSplitMovers()` governs gestureFor and NOTHING ELSE, so
  // a case that hands AltNext straight to onGesture asserts only that the switch has no
  // branch for it -- true with the declaration removed, which is a mutation that does
  // not bite. `Button::Up`/`Down` ARE the front row (the shell's mapping is crossed --
  // read it rather than the names), and without the split they fold into Prev/Next and
  // page. So this is the half only the declaration can satisfy.
  a = p.s().onEvent({reader::Button::Down, reader::PressKind::Short});
  CHECK(a.kind == reader::Action::Kind::None);
  CHECK(p.text() == first);
  a = p.s().onEvent({reader::Button::Up, reader::PressKind::Short});
  CHECK(a.kind == reader::Action::Kind::None);
  CHECK(p.text() == first);

  // ...and the sides, through the same door, which is what the split leaves paging.
  a = p.s().onEvent({reader::Button::Right, reader::PressKind::Short});
  CHECK(a.kind == reader::Action::Kind::Redraw);
  CHECK(p.text() == second);
  a = p.s().onEvent({reader::Button::Left, reader::PressKind::Short});
  CHECK(a.kind == reader::Action::Kind::Redraw);
  CHECK(p.text() == first);

  // The gestures themselves, for completeness: the screen ignores the alternate pair
  // even when one reaches it directly. Weaker than the two above and kept because it is
  // the property the switch states.
  a = p.s().onGesture({Gesture::AltNext});
  CHECK(a.kind == reader::Action::Kind::None);
  CHECK(p.text() == first);
  a = p.s().onGesture({Gesture::AltPrev});
  CHECK(a.kind == reader::Action::Kind::None);
  CHECK(p.text() == first);
}

TEST_CASE("Back closes the peek and Activate commits it") {
  // BOTH ANSWER Pop, and the flag is the entire difference -- the peek cannot move the
  // Reader, which is on the stack underneath it.
  {
    PeekFix p;
    CHECK_FALSE(p.s().committed());
    const reader::Action a = p.s().onGesture({Gesture::Back});
    CHECK(a.kind == reader::Action::Kind::Pop);
    CHECK_FALSE(p.s().committed());
  }
  {
    PeekFix p;
    CHECK_FALSE(p.s().committed());
    const reader::Action a = p.s().onGesture({Gesture::Activate});
    CHECK(a.kind == reader::Action::Kind::Pop);
    CHECK(p.s().committed());
  }
}

TEST_CASE("the committed cursor is the page the peek was showing") {
  // NOT PAGE ONE. The reader may have paged several pages into the peek before pressing
  // GO HERE, so the cursor that travels is the one under the panel at that moment --
  // which is why the assertion is against a cursor taken by GOING there rather than a
  // hand-built one.
  PeekFix p;
  p.s().onGesture({Gesture::Next});
  p.s().onGesture({Gesture::Next});

  const Cursor showing = p.s().chosenCursor();
  const int spine = p.s().chosenSpine();
  // PAGE ONE WOULD BE RIGHT ON THE FIRST PAGE AND WRONG EVERYWHERE AFTER IT, so the
  // assertion has to stand somewhere Cursor{} is not the answer.
  REQUIRE(showing != Cursor{});

  const reader::Action a = p.s().onGesture({Gesture::Activate});
  REQUIRE(a.kind == reader::Action::Kind::Pop);
  CHECK(p.s().committed());
  // COMMITTING MOVES NOTHING HERE. The screen is read while it is still on top and the
  // shell does the jump; a commit that disturbed its own position would hand over a
  // cursor for a page it was no longer showing.
  CHECK(p.s().chosenCursor() == showing);
  CHECK(p.s().chosenSpine() == spine);
}

TEST_CASE("a peek that was closed rather than committed reports no commit") {
  // CLOSE LEAVES THE READER'S PAGE UNTOUCHED, and this is the half of that the peek
  // itself can state: whatever paging happened inside the panel, nothing asked for it.
  PeekFix p;
  p.s().onGesture({Gesture::Next});
  p.s().onGesture({Gesture::Next});
  REQUIRE(p.s().chosenCursor() != Cursor{});

  const reader::Action a = p.s().onGesture({Gesture::Back});
  CHECK(a.kind == reader::Action::Kind::Pop);
  CHECK_FALSE(p.s().committed());
}

TEST_CASE("the peek's page is NOT the reader's page, because the column is narrower") {
  // THE FACT THE WHOLE DESIGN RESTS ON. A peek that reused the Reader's already-laid
  // page would look almost right -- same text, same face -- and would overflow the
  // panel, because those lines were measured against a 444px column and this one is
  // ~368. It is also why the band can never show a page number.
  const std::string ch = readerfix::longChapter(40);

  readerfix::Reading r(ch);
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  readerfix::Body body;
  reader::PageMetrics pm;
  theme.peekMetrics(480, 800, ramp.fonts, body.face, reader::Settings{}, pm);
  reader::PeekScreen peek(ch, "CH. 01", 4, &body.face);
  peek.setMetrics(pm);

  REQUIRE_FALSE(readerfix::pageText(r.scr->page()).empty());
  REQUIRE_FALSE(readerfix::pageText(peek.page()).empty());
  // DIFFERENT WRAP, so different text on page one. The two columns differ by ~76px,
  // which is two or three words a line.
  //
  // ...AND THE WHOLE-PAGE COMPARISON CANNOT SAY THAT, which is why the line below it
  // exists. pageText concatenates every line, and this page comes out as seven lines in
  // the panel against eleven on the reading page -- so these two strings differ in
  // their line COUNT alone, and the assertion passes unchanged over two columns that
  // wrapped IDENTICALLY, which is the one thing the case is named for. Proved by
  // mutation: hand the peek the reader's columnLeft and columnW while leaving it the
  // panel's columnH, and this still passes while the check below is the only failure.
  // It is kept as the coarse check it is.
  CHECK(readerfix::pageText(peek.page()) != readerfix::pageText(r.scr->page()));

  // THE ASSERTION THAT BITES ON THE MEASURE. Line 0 is `<h1>Chapter One</h1>`, short
  // enough to fit either column and therefore identical in both -- so the first line
  // that can differ is line 1, and it differs because 368px holds `Paragraph 0 of a
  // chapter` where 444px holds `Paragraph 0 of a chapter long`. Built at the READER's
  // metrics this fails; built at the panel's it is the two-or-three-words-a-line the
  // comment above claims.
  REQUIRE(peek.page().lines.size() > 1);
  REQUIRE(r.scr->page().lines.size() > 1);
  CHECK(peek.page().lines[1].text != r.scr->page().lines[1].text);

  // AND THE LINES ARE INSIDE THE PANEL, not on the page's own left edge.
  for (const reader::LaidLine& ln : peek.page().lines) CHECK(ln.x >= pm.columnLeft);

  // THE PANEL HOLDS WHAT ITS BOX HOLDS AND NO MORE. The height is FIXED
  // (kPeekPanelH) and the count is what fits whole inside it, so a page laid at the
  // reading column's height would run out through the border.
  //
  // A CEILING AND NOT AN EQUALITY *HERE*, and the reason is the heading: this page
  // opens with one, and a block boundary costs a blank line box that carries no
  // LaidLine -- so a full eight boxes come back as seven lines. The equality lives on
  // the next case, over a page with no heading in it.
  CHECK(peek.page().lines.size() <=
        static_cast<size_t>(theme.peekVisibleLines(ramp.fonts, body.face, reader::Settings{})));
}

TEST_CASE("a peek page in the body of a chapter fills every line box its panel holds") {
  // THE COUNT IS THE DESIGN'S RESULT, AND NOTHING RAN IT THROUGH THE LAYOUT ENGINE.
  // Every check on this panel's line count was `<=` -- here, in the demo case below,
  // and in test_theme_peek_golden.cpp -- and a ceiling is satisfied by seven. What
  // made that worth more than tidiness is that test_theme_peek_metrics.cpp used to
  // carry PageBuilder's `rows_` TRANSCRIBED, checking peekMetrics against a copy of
  // the rule rather than against PageBuilder. (That copy is gone: both sides call
  // rowsThatFit now.) This is still the assertion that goes through the real engine.
  //
  // NOT PAGE ONE. `longChapter` opens with `<h1>Chapter One</h1>`, and a block boundary
  // spends a line box that produces no LaidLine, so page one is seven lines over eight
  // boxes -- an honest full page that an equality would read as a defect. Page two is
  // paragraph text throughout.
  for (const auto geo : {std::pair<int, int>{480, 800}, std::pair<int, int>{528, 792}}) {
    PeekFix p(4, geo.first, geo.second);
    CAPTURE(geo.first);
    const std::string first = p.text();
    p.s().onGesture({Gesture::Next});
    REQUIRE(p.text() != first);  // it really did turn
    CHECK(p.s().page().lines.size() == static_cast<size_t>(p.lines()));
  }
}

TEST_CASE("the peek's line count follows the reader's typography, through real pagination") {
  // THE DERIVED COUNT AGAINST WHAT PageBuilder ACTUALLY LAYS, at the corners of both
  // settings ramps rather than only at the default. The case above pins the equality
  // and the walk in test_theme_peek_metrics.cpp pins the arithmetic; neither of them
  // runs the layout engine at a non-default ppem, and "8" is the one count where a
  // theme that ignored its arguments would still be right.
  //
  // 17 LINES AT ppem 25 / lead 1.000 AND 4 AT ppem 46 / lead 2.000, in a box that does
  // not move -- which is the feature in one assertion. Under the old rule these were 8
  // and 8, in panels of 310px and 846px.
  //
  // THE EXPECTED COUNTS ARE SPELLED OUT rather than compared against the theme's own
  // answer, because the theme's answer is what is under test here. They are the four
  // corners of the box: 436px of column over a `ppem * lead` line box. Note 25/2.000
  // lands on EIGHT -- the same count as the default, by coincidence of 25 * 2.0 being
  // near 32 * 1.7 -- which is why a `n != 8` guard was wrong here and this table is not.
  struct Corner {
    int ppem, lead, lines;
  };
  const Corner corners[] = {{25, 1000, 17}, {25, 2000, 8}, {46, 1000, 9}, {46, 2000, 4}};
  for (const auto& c : corners) {
    for (const auto geo : {std::pair<int, int>{480, 800}, std::pair<int, int>{528, 792}}) {
      const int ppem = c.ppem, lead = c.lead;
      CAPTURE(ppem);
      CAPTURE(lead);
      CAPTURE(geo.first);

      ramp::Ramp ramp;
      reader::QuietTheme theme;
      readerfix::Body body(ppem);
      reader::Settings s;
      s.bodyPpem = ppem;
      s.lineSpacing = lead;
      reader::PageMetrics m;
      theme.peekMetrics(geo.first, geo.second, ramp.fonts, body.face, s, m);

      reader::PeekScreen peek(readerfix::longChapter(40), "CH. 01", 4, &body.face);
      peek.setMetrics(m);
      // NOT PAGE ONE, for the reason the case above gives: `longChapter` opens with a
      // heading, and a block boundary spends a line box that produces no LaidLine.
      const std::string first = readerfix::pageText(peek.page());
      peek.onGesture({Gesture::Next});
      REQUIRE(readerfix::pageText(peek.page()) != first);

      const int n = theme.peekVisibleLines(ramp.fonts, body.face, s);
      // THE THEME'S ANSWER AGAINST THE TABLE, so the equality below cannot be satisfied
      // by a theme and a layout that are wrong together.
      CHECK(n == c.lines);
      CHECK(static_cast<int>(peek.page().lines.size()) == n);
    }
  }
}

// --- WHAT THE FACTORY WILL AND WILL NOT BUILD -----------------------------------

namespace {

// A factory with the panel's column and a body face, and nothing else primed. The
// three cases below differ by exactly which of those they withhold, so the fixture
// hands the pieces over rather than the finished state.
struct FactoryFix {
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  readerfix::Body body;
  reader::DemoScreenFactory factory;

  void withBody() { factory.setReaderBody(&body.face); }
  void withMetrics() {
    reader::PageMetrics m;
    theme.peekMetrics(480, 800, ramp.fonts, body.face, reader::Settings{}, m);
    factory.setPeekMetrics(m);
    // AND THE READING COLUMN TOO, WHICH IS NOT PADDING. The factory on a device holds
    // both, and the bug the two setters exist to prevent is the peek being handed the
    // READER's measure -- so a fixture that left readerMetrics_ default would let
    // `setMetrics(readerMetrics_)` fail only because an unset column paginates to
    // nothing. That is a mutation biting for the wrong reason, which this project's
    // rule says tells you about the INPUT before it tells you about the test. With a
    // real reading column set, the ceiling below cannot see the swap at all and the
    // left edge can.
    reader::PageMetrics rm;
    theme.readerMetrics(480, 800, ramp.fonts, body.face, reader::Settings{}, rm);
    factory.setReaderMetrics(rm);
    peekLeft = m.columnLeft;
  }
  int peekLeft = 0;
  int lines() const {
    return theme.peekVisibleLines(ramp.fonts, body.face, reader::Settings{});
  }
};

}  // namespace

TEST_CASE("the factory refuses a Peek nothing primed") {
  // AND THAT REFUSAL IS WHAT MAKES A PEEK UNRESTORABLE ACROSS A WAKE, which is the
  // intended behaviour rather than a limitation: App::restore pushes the record's stack
  // through this factory, so a Peek in a record stops the restore short and leaves the
  // READER standing -- "a restore that stops early keeps what already stands".
  // Rebuilding one would need a peeked cursor nothing persists, and the return anchor's
  // own design already declined to pay a card write for that breadcrumb.
  //
  // THE ALTERNATIVE IS THE DEFECT THIS PROJECT HAS SHIPPED TWICE. A Reader falling
  // through to the demo woke the device into Middlemarch; Contents falling back to
  // demoContents() showed one book's reader another book's chapters, and hid the
  // allocation failure that caused it. A refused push is wrong in a way the reader can
  // see through.
  FactoryFix f;
  f.withBody();
  f.withMetrics();
  CHECK(f.factory.create(reader::ScreenId::Peek) == nullptr);
}

TEST_CASE("the factory refuses a Peek with no body face") {
  // THE READER'S RULE VERBATIM. A panel that rendered nothing is indistinguishable from
  // a chapter that failed to open, and the caller can act on a refused push where it
  // cannot act on an empty one. Asked for and still refused, so this is the face and
  // not the priming.
  FactoryFix f;
  f.withMetrics();
  f.factory.setPeekDemo();
  CHECK(f.factory.create(reader::ScreenId::Peek) == nullptr);
}

TEST_CASE("the demo Peek builds and shows the board's opening") {
  FactoryFix f;
  f.withBody();
  f.withMetrics();
  f.factory.setPeekDemo();

  std::unique_ptr<reader::Screen> s = f.factory.create(reader::ScreenId::Peek);
  REQUIRE(s != nullptr);
  CHECK(s->id() == reader::ScreenId::Peek);
  CHECK(s->isOverlay());

  auto* peek = static_cast<reader::PeekScreen*>(s.get());
  REQUIRE_FALSE(peek->page().lines.empty());
  // THE LINES ARE INSIDE THE PANEL, and this is what says the factory handed over the
  // PEEK's column and not the reading page's. Measured at 480x800: the peek's column is
  // left=56 w=368 against the page's left=18 w=444, so a peek built at the wrong
  // metrics draws its text on the page's own left edge -- 38px outside its border.
  //
  // THE LINE-COUNT CEILING BELOW CANNOT MAKE THIS CHECK, and that was proved by
  // mutation rather than assumed: `setMetrics(readerMetrics_)` lays this two-sentence
  // specimen in SEVEN lines at the reading measure, under the panel's eight, so the
  // ceiling passes over exactly the swap it looks like it is guarding. It is kept as
  // the bound it really is -- the panel's line count is a result of its fixed box --
  // and the left edge is what carries the claim.
  for (const reader::LaidLine& ln : peek->page().lines) CHECK(ln.x >= f.peekLeft);
  // Still a ceiling here, and deliberately: the case above -- "a peek page in the body
  // of a chapter fills every line box its panel holds" -- is where the count is pinned
  // to what the box derived, over real pagination rather than over a specimen authored
  // to fit.
  CHECK(peek->page().lines.size() <= static_cast<size_t>(f.lines()));
  // AND IT IS THE BOARD'S OWN SENTENCE. Checked on the text rather than only on the
  // line count, because a peek built from the READER's demo chapter would also fit --
  // and "Miss Brooke", which this used to assert, IS THAT CHAPTER'S OPENING TOO. The
  // two literals are byte-identical for 194 characters and diverge at `bare of style.`
  // against `bare of style than those in which the Blessed Virgin...`, so the only
  // thing that tells them apart on an eight-line panel is where the specimen STOPS:
  // rendered side by side the pages differ in their last line alone,
  // `less bare of style.` against `less bare of style than`.
  //
  // WHICH IS ALSO THE POINT OF THE BOARD'S SHORTER TEXT. demoPeekXhtml is the reading
  // board's sentence truncated to what the panel holds, so `make compare` measures the
  // panel rather than where a longer specimen happened to break -- and a peek built
  // from the reader's chapter would be cut off mid-clause instead.
  const std::string text = readerfix::pageText(peek->page());
  CHECK(text.find("Miss Brooke") != std::string::npos);
  CHECK(text.find("bare of style.") != std::string::npos);
  REQUIRE_FALSE(peek->page().lines.empty());
  REQUIRE_FALSE(peek->page().lines.back().text.empty());
  CHECK(peek->page().lines.back().text.back() == '.');
}

// --- WHAT A COMMIT MAY CARRY ACROSS THE TWO MEASURES (#48) ----------------------
//
// The panel's column is ~368px and the reading page's is 444, and a `Cursor`'s `line`
// is a line WITHIN A BLOCK AT ONE COLUMN WIDTH -- so a line index handed straight
// across means a different place in the text on each side. `reading_position.h` grades
// exactly that change as `Relaid` and ZEROES the field for it, and
// `ReaderScreen::relayout` drops the line for the same reason. `chosenCursor()` is the
// third place the same question is asked, and it was the one answering it differently.
//
// WHICH DIRECTION IT WAS WRONG IN IS THE WHOLE POINT. The panel is NARROWER, so a block
// has MORE lines there -- line L of the panel has consumed LESS text than line L of the
// page. Reading L as a reading line therefore lands the reader PAST the text they
// pressed GO HERE on, which is the one failure a commit must not have: the passage you
// chose is behind you, and nothing on the screen says so.

namespace {

// ONE PARAGRAPH LONG ENOUGH TO SPAN PAGES, which is what `longChapter` is not -- and
// that is why #48 reached a device with 1,377 green test cases behind it.
//
// `line` is block-relative, so the disagreement between the two measures grows with the
// line index INSIDE ONE BLOCK. `longChapter`'s paragraphs are four or five panel lines
// each, so its block-relative index never leaves single figures; measured over all 45 of
// its panel pages, the two answers are the same reading page in 20 of them and one page
// apart in the rest. A fixture built from it cannot see this defect at all, which is
// exactly the shape this project records: a mutation tells you about your INPUT before
// it tells you about your test.
//
// NUMBERED WORDS RATHER THAN PROSE, so a token taken off the panel's page can be found
// again on the reading page and the question "where is this text NOW" has an exact
// answer. There is no hyphenation here, so a word is never split across a line -- where
// a phrase would be broken by whichever column happened to wrap inside it, and a word of
// real prose repeats and would match the wrong paragraph.
std::string oneLongParagraph(int words) {
  // A BLOCK IN FRONT OF IT, A WHOLE READING PAGE LONG, and that is a test-fixture fix
  // rather than decoration -- it was added because a mutation went unnoticed twice over.
  // With the long paragraph at block 0, `Cursor{block, 0}` and `Cursor{}` are the SAME
  // VALUE, so dropping the block as well as the line passed every assertion here: the
  // cases could see the line being kept and could not see the block being lost. Making
  // it block 1 fixes the exact-cursor case; making the front matter fill a reading page
  // fixes the behavioural one too, since otherwise block 1 begins on reading page 0 and
  // landing at the front of the chapter is indistinguishable from landing at the top of
  // the block. Caught by running the mutation rather than by reading the test.
  std::string d = "<html><body><p>";
  for (int i = 0; i < 70; ++i) {
    if (i > 0) d += ' ';
    d += 'f';
    d += static_cast<char>('0' + (i / 100) % 10);
    d += static_cast<char>('0' + (i / 10) % 10);
    d += static_cast<char>('0' + i % 10);
  }
  d += "</p><p>";
  for (int i = 0; i < words; ++i) {
    if (i > 0) d += ' ';
    d += 'w';
    d += static_cast<char>('0' + (i / 100) % 10);
    d += static_cast<char>('0' + (i / 10) % 10);
    d += static_cast<char>('0' + i % 10);
  }
  // A SECOND, SHORT BLOCK AFTER IT, and it is not decoration: it is what makes the
  // sharper half of the defect visible. A panel line index deep in block 0 can name a
  // line the reading column's block 0 DOES NOT HAVE -- 150 panel lines against 120
  // reading ones -- and `openAtCursor` then takes its documented `!found` exit, "the end
  // of the chapter is the closest honest answer". With one block that end is next door;
  // with this one it is a different paragraph.
  d += "</p><p>tail alpha beta gamma delta</p></body></html>";
  return d;
}

// WHICH READING PAGE HOLDS A TOKEN, walked from the top rather than computed: the point
// is where the text really is at the reading measure, and only the layout knows that.
// Leaves the screen wherever it stopped; every caller re-jumps afterwards.
int readingPageHolding(reader::ReaderScreen& s, const std::string& token) {
  REQUIRE(s.goToPosition(0, Cursor{}));
  s.completeIndex();
  for (int i = 0; i < s.pageCount(); ++i) {
    if (readerfix::pageText(s.page()).find(token) != std::string::npos) return i;
    s.onGesture({Gesture::Next});
  }
  return -1;
}

}  // namespace

TEST_CASE("the committed cursor keeps the block and drops the line") {
  // THE FIX IN ONE ASSERTION. The block is a fact about the DOCUMENT and survives the
  // crossing; the line is a fact about a LAYOUT that the Reader does not share.
  cardfix::CardReading r(oneLongParagraph(600));

  reader::PageMetrics pm;
  r.theme.peekMetrics(480, 800, r.ramp.fonts, r.body.face, reader::Settings{}, pm);
  reader::PeekScreen peek(r.fs, r.ob, 0, &r.body.face);
  peek.setMetrics(pm);

  // THE PANEL'S OWN PAGINATION, SPELLED TWICE ON PURPOSE. `PeekScreen` holds a
  // `ReaderScreen` built at these metrics and forwards page gestures into it, so a
  // sibling built the same way and paged the same number of times is that inner
  // reader -- the layout is deterministic in the xhtml, the metrics and the face. It is
  // the only way to see the cursor the peek is degrading, because the peek exposes the
  // degraded one and nothing else.
  reader::ReaderScreen panel(r.fs, r.ob, 0, &r.body.face);
  panel.setMetrics(pm);
  panel.completeIndex();

  for (int i = 0; i < 10; ++i) {
    peek.onGesture({Gesture::Next});
    panel.onGesture({Gesture::Next});
  }
  const Cursor raw = panel.currentCursor();
  // NOT A `0 == 0` ASSERTION, and this is the REQUIRE the whole file was missing: over
  // `longChapter` this line is a single digit and the case below holds for a reason that
  // has nothing to do with the fix. Measured here it is 66.
  REQUIRE(raw.line >= 60);
  // AND NOT BLOCK 0, or the assertion below cannot tell a kept block from a dropped one
  // -- `Cursor{0, 0}` IS `Cursor{}`. See the fixture.
  REQUIRE(raw.block > 0);
  REQUIRE(peek.chosenSpine() == 0);

  CHECK(peek.chosenCursor() == Cursor{raw.block, 0});
}

TEST_CASE("committing from deep inside one block does not land past the peeked text") {
  // THE BEHAVIOURAL HALF, and it is self-proving: the same fixture shows the raw line
  // landing PAST the text the panel was showing and the committed cursor landing at or
  // before it. So the case cannot pass because the fixture is too shallow to reach the
  // branch -- the REQUIRE on the raw landing is what says it is deep enough.
  const std::string ch = oneLongParagraph(600);
  cardfix::CardReading r(ch);
  r.scr->completeIndex();

  reader::PageMetrics pm;
  r.theme.peekMetrics(480, 800, r.ramp.fonts, r.body.face, reader::Settings{}, pm);

  // TWO DEPTHS, because the defect has two failure classes and only the first looks like
  // "a page off". At panel page 8 the raw line exists in the reading column's block and
  // simply names a later place in it; at panel page 18 it names line 130 of a block that
  // has 120 reading lines, so `openAtCursor` falls through to the END of the chapter --
  // a different paragraph, from a commit made in the middle of the first one.
  for (const int deep : {8, 18}) {
    CAPTURE(deep);

    reader::PeekScreen peek(r.fs, r.ob, 0, &r.body.face);
    peek.setMetrics(pm);
    reader::ReaderScreen panel(r.fs, r.ob, 0, &r.body.face);
    panel.setMetrics(pm);
    panel.completeIndex();
    for (int i = 0; i < deep; ++i) {
      peek.onGesture({Gesture::Next});
      panel.onGesture({Gesture::Next});
    }
    REQUIRE(panel.pageIndex() == deep);  // the panel really did get that far
    const Cursor raw = panel.currentCursor();
    // THE LONG PARAGRAPH, AND NOT BLOCK 0 -- see the fixture: at block 0 the assertions
    // below cannot tell `{block, 0}` from `Cursor{}`.
    REQUIRE(raw.block == 1);
    REQUIRE(raw.line > 0);

    // THE TEXT THE READER PRESSED GO HERE ON: the first token of the panel's top line.
    REQUIRE_FALSE(peek.page().lines.empty());
    const std::string token = peek.page().lines.front().text.substr(0, 4);
    const int truePage = readingPageHolding(*r.scr, token);
    // IT HAS TO BE SOMEWHERE, and not on the first page -- a commit cannot be measurably
    // wrong about a passage that is already on the page the reader would land on anyway.
    REQUIRE(truePage > 0);

    // THE DEFECT, DEMONSTRATED. Handed across unchanged, the panel's line index lands the
    // reader STRICTLY PAST the page holding the passage they chose.
    REQUIRE(r.scr->goToPosition(0, raw));
    REQUIRE(r.scr->pageIndex() > truePage);
    if (deep == 18) {
      // AND THE SHARPER FORM: not a page off but the END of the chapter, in the OTHER
      // paragraph, reached by naming a line the reading column does not have.
      //
      // ASSERTED ON THE TEXT AND NOT ON THE LANDING CURSOR, which was the first
      // spelling and does not say this: the last reading page's start cursor is still
      // block 0 -- `{0, 120}` is where block 0's lines run out, and the boundary between
      // two blocks has a spelling on each side. What the reader is looking at is the
      // second paragraph, and that is the claim worth making.
      CHECK(r.scr->pageIndex() == r.scr->pageCount() - 1);
      CHECK(readerfix::pageText(r.scr->page()).find("tail alpha") != std::string::npos);
    }

    // AND THE FIX. At or before the passage, so pressing forward reaches it and nothing
    // was skipped -- and on the page that holds the TOP of the peeked paragraph, which
    // is the landing the fix promises.
    //
    // ASSERTED ON THE TEXT, NOT ON THE LANDED CURSOR, and the first spelling of it was
    // simply wrong about goToPosition: it lands on the page CONTAINING the cursor, and
    // `currentCursor()` then answers that PAGE's start -- which is only the target when
    // the target happens to be a page boundary. `{1, 0}` here is mid-page, so the landed
    // cursor is `{0, N}` and an equality against the target fails for a reason that has
    // nothing to do with #48. The exact cursor is pinned on `chosenCursor()` itself in
    // the case above; what belongs here is what the reader can see.
    REQUIRE(r.scr->goToPosition(0, peek.chosenCursor()));
    CHECK(r.scr->pageIndex() <= truePage);
    CHECK(readerfix::pageText(r.scr->page()).find("w000") != std::string::npos);
    // AND NOT THE FRONT OF THE CHAPTER, which is what a dropped BLOCK would give: the
    // front matter is a whole reading page, so the top of block 1 is not on page 0.
    CHECK(r.scr->pageIndex() > 0);
  }
}


