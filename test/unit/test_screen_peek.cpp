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
  CHECK(readerfix::pageText(peek.page()) != readerfix::pageText(r.scr->page()));

  // AND THE LINES ARE INSIDE THE PANEL, not on the page's own left edge.
  for (const reader::LaidLine& ln : peek.page().lines) CHECK(ln.x >= pm.columnLeft);

  // THE PANEL HOLDS kPeekLines LINE BOXES AND NO MORE. Its HEIGHT is a result of that
  // count, so a page laid at the reading column's height would run out through the
  // border.
  CHECK(peek.page().lines.size() <= static_cast<size_t>(reader::kPeekLines));
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
  // the bound it really is -- the panel's height is a result of kPeekLines -- and the
  // left edge is what carries the claim.
  for (const reader::LaidLine& ln : peek->page().lines) CHECK(ln.x >= f.peekLeft);
  CHECK(peek->page().lines.size() <= static_cast<size_t>(reader::kPeekLines));
  // AND IT IS THE BOARD'S OWN SENTENCE. Checked on the text rather than only on the
  // line count, because a peek built from the READER's demo chapter would also fit --
  // demoReaderXhtml opens with this same first sentence and then carries a second
  // paragraph the panel has no room for.
  CHECK(readerfix::pageText(peek->page()).find("Miss Brooke") != std::string::npos);
}
