// PAGING OFF THE LAST PAGE OF THE LAST CHAPTER.
//
// It was a DEAD BUTTON: openChapterAt failed, Gesture::Next returned none(), and the
// panel did not move -- on the last page of every book. design/BookEnd.dc.html is the
// screen that press should open.
//
// WHAT MAKES THIS MORE THAN A ONE-LINE CHANGE is that walkToChapter's bool conflated
// three outcomes and only one of them is the end of a book:
//
//   * nothing to page into      -- the in-memory demo Reader, which the simulator and
//                                  every golden use;
//   * the spine ran out         -- the edge of the book, and the only one that means
//                                  THE END;
//   * an entry would not open   -- a corrupt local header, where claiming the book had
//                                  ended would be a WRONG statement rather than a
//                                  missing one.
//
// AND A `chapterAt_ + 1 >= chapterCount()` TEST IS NOT A SUBSTITUTE, which is what the
// trailing-empty case below pins: a real EPUB's last spine entries can paginate to
// nothing (three of Le Fléau's 92 do, and spine 0 of any real book is a cover), so "is
// the next index in range" is not "is there another page". The WALK is what knows,
// because skipping empty candidates is exactly what it does.
#include <memory>
#include <string>

#include "card_book_fixture.h"
#include "doctest.h"
#include "reader/app.h"
#include "reader/screen_reader.h"
#include "reader/screens.h"
#include "reader_fixture.h"

using reader::Action;
using reader::Gesture;
using reader::ScreenId;

namespace {

// A spine entry with no text at all -- an `<img>` and nothing document.h models, which
// is what a cover and a title page are in a real book. walkToChapter SKIPS these, so a
// book whose last entry is one still ends at the entry before it.
inline const char* kEmptyChapter = "<html><body><img src=\"plate.jpg\"/></body></html>";

// Turn pages until the screen stops moving, then report the action the NEXT press
// produced. Deliberately not "press N times": the page count of a chapter is a result
// of the face, the column and the copy, so a hardcoded N pins the fixture rather than
// the behaviour.
Action pageToTheEnd(reader::ReaderScreen& scr) {
  for (int guard = 0; guard < 200; ++guard) {
    const int wasChapter = scr.chapterIndex();
    const int wasPage = scr.pageIndex();
    const Action a = scr.onGesture({Gesture::Next});
    if (a.kind != Action::Kind::Redraw) return a;
    // A Redraw that moved nothing would spin this loop forever.
    REQUIRE((scr.chapterIndex() != wasChapter || scr.pageIndex() != wasPage));
  }
  FAIL("the book never ended");
  return Action::none();
}

}  // namespace

TEST_CASE("paging forward off the last page of the last chapter opens BookEnd") {
  // THE DEFECT. The walk ran out of spine entries, openChapterAt returned false, and
  // Gesture::Next answered none() -- nothing on the panel, nothing in the log.
  cardfix::CardReading r("<html><body><p>One.</p></body></html>");
  const Action a = pageToTheEnd(*r.scr);

  CHECK(a.kind == Action::Kind::Push);
  CHECK(a.target == ScreenId::BookEnd);
  // AND THE PAGE IS UNDISTURBED: openChapterAt's restore put the last chapter, its
  // index and its page back, so the frame BookEnd is pushed over is still the last
  // page of the book -- which is also what makes Back from it correct.
  CHECK(r.scr->chapterIndex() == 1);
  CHECK(r.scr->pageCount() > 0);
}

TEST_CASE("trailing empty spine entries still end the book") {
  // THE CASE A RANGE TEST GETS WRONG. Spine entry 1 exists and is in range, and it
  // paginates to nothing -- so `chapterAt_ + 1 < chapterCount()` says there is another
  // page and there is not. Only the walk can answer this, because skipping candidates
  // that paginate to nothing is the walk's own job.
  cardfix::CardReading r("<html><body><p>One.</p></body></html>", reader::kBodyPpem,
                         reader::Settings{}.margins, reader::Cursor{}, kEmptyChapter);
  REQUIRE(r.scr->chapterIndex() == 0);

  const Action a = pageToTheEnd(*r.scr);
  CHECK(a.kind == Action::Kind::Push);
  CHECK(a.target == ScreenId::BookEnd);
  // Still on the chapter that HAS text, not on the empty one the walk stepped over.
  CHECK(r.scr->chapterIndex() == 0);
}

TEST_CASE("a chapter that fails to open does not claim the book has ended") {
  // AN ARCHIVE FAULT IS NOT THE END OF A BOOK. Pushing BookEnd here would put THE END
  // on the glass for a corrupt local header -- a wrong claim, not a missing one.
  //
  // The corruption is the LOCAL header's signature, which is the only fault that
  // survives Epub::open: that validates the spine against the CENTRAL directory, so
  // the book opens and the entry is only found to be unreadable when ChapterReader
  // resolves its data offset -- which is the one route CLAUDE.md names to a span that
  // will not read.
  std::string epub = cardfix::epubWith("<html><body><p>One.</p></body></html>");
  const std::string name = "OEBPS/ch2.xhtml";
  const size_t at = epub.find(name);
  REQUIRE(at != std::string::npos);
  REQUIRE(at >= 30);
  const size_t hdr = at - 30;  // the local header the name is part of
  REQUIRE(static_cast<unsigned char>(epub[hdr]) == 0x50);
  REQUIRE(static_cast<unsigned char>(epub[hdr + 1]) == 0x4B);
  REQUIRE(static_cast<unsigned char>(epub[hdr + 2]) == 0x03);
  REQUIRE(static_cast<unsigned char>(epub[hdr + 3]) == 0x04);
  epub[hdr + 2] = static_cast<char>(0xFF);  // no longer a local file header

  ramp::Ramp ramp;
  reader::QuietTheme theme;
  readerfix::Body body;
  reader::PageMetrics m;
  FakeFileSystem fs;
  theme.readerMetrics(480, 800, ramp.fonts, body.face, reader::Settings{}, m);
  REQUIRE(fs.writeAll("/books/b.epub", epub));
  reader::OpenedBook ob;
  const char* why = "";
  REQUIRE_MESSAGE(reader::openBook(fs, "/books/b.epub", ob, &why), std::string(why));
  reader::ReaderScreen scr(fs, ob, 0, &body.face);
  scr.setMetrics(m);
  REQUIRE(scr.chapterIndex() == 0);
  REQUIRE(scr.pageCount() > 0);

  const Action a = pageToTheEnd(scr);
  CHECK(a.kind == Action::Kind::None);
  // And the reader is where it was, on the chapter that does read.
  CHECK(scr.chapterIndex() == 0);
  CHECK(scr.pageCount() > 0);
}

TEST_CASE("the demo reader does not open BookEnd off its last page") {
  // THE IN-MEMORY READER HAS NO BOOK BEHIND IT -- no filesystem, no spine -- so there
  // is no end of a book to have reached. The simulator and every golden go through
  // this constructor, so a BookEnd here would fire on all of them.
  readerfix::Reading r(readerfix::longChapter(4));
  const Action a = pageToTheEnd(*r.scr);
  CHECK(a.kind == Action::Kind::None);
}

TEST_CASE("paging back off the front of the book still does nothing") {
  // There is no board for the BEGINNING of a book and no reason to invent one.
  cardfix::CardReading r("<html><body><p>One.</p></body></html>");
  REQUIRE(r.scr->chapterIndex() == 0);
  REQUIRE(r.scr->pageIndex() == 0);

  const Action a = r.scr->onGesture({Gesture::Prev});
  CHECK(a.kind == Action::Kind::None);
  CHECK(r.scr->chapterIndex() == 0);
  CHECK(r.scr->pageIndex() == 0);
}

TEST_CASE("an ARTICLE's last page opens ArticleEnd, and the default is unchanged") {
  // ONE READER SERVES BOTH, which is decision 3 of the wallabag note taken to its
  // conclusion: an article is an EPUB on the card and reads through `openBook`
  // like any book, so the only thing that differs at the end of it is which board
  // the last page turns into. A second Reader would be a second copy of paging,
  // the rewind, the ring and the index -- the three routines this project has
  // spent the most effort on.
  //
  // THE DEFAULT IS ASSERTED BESIDE IT ON PURPOSE. This is a setter with a
  // default value, which is the shape that goes wrong silently: a reader told
  // nothing must still reach `BookEnd`, or every book on the card ends on a
  // screen about articles.
  SUBCASE("told ArticleEnd, it pushes ArticleEnd") {
    cardfix::CardReading r("<html><body><p>One.</p></body></html>");
    r.scr->setEndScreen(ScreenId::ArticleEnd);
    const Action a = pageToTheEnd(*r.scr);
    CHECK(a.kind == Action::Kind::Push);
    CHECK(a.target == ScreenId::ArticleEnd);
    // The page is undisturbed here for `BookEnd`'s reason, and it is what makes
    // Back off the end screen land on the article's last page rather than
    // nowhere.
    CHECK(r.scr->chapterIndex() == 1);
    CHECK(r.scr->pageCount() > 0);
  }

  SUBCASE("told nothing, it still pushes BookEnd") {
    cardfix::CardReading r("<html><body><p>One.</p></body></html>");
    const Action a = pageToTheEnd(*r.scr);
    CHECK(a.target == ScreenId::BookEnd);
  }

  SUBCASE("told BookEnd back again, it goes back") {
    // A Reader is reused across opens on this device -- the factory builds one
    // per push, but the shell primes it per book -- so the setter has to be able
    // to go both ways. One-way would leave the first article's board on every
    // book opened after it.
    cardfix::CardReading r("<html><body><p>One.</p></body></html>");
    r.scr->setEndScreen(ScreenId::ArticleEnd);
    r.scr->setEndScreen(ScreenId::BookEnd);
    const Action a = pageToTheEnd(*r.scr);
    CHECK(a.target == ScreenId::BookEnd);
  }
}

// WHICH BOARD THE FACTORY GIVES THE READER, and that it goes back.
//
// `setEndScreen`'s header names the one-way hazard: the factory outlives every
// screen it builds, so a board set for an article leaves that board on every book
// opened afterwards. It used to be avoided by the shell setting it on EVERY open --
// a caller remembering, in the one function that happened to be the only route to a
// Reader (#177). It is the factory's two priming calls that say it now, and this is
// what holds them to it.
TEST_CASE("an article says so with its facts, and the next book takes the board back") {
  cardfix::CardReading r("<p>one</p>");
  reader::DemoScreenFactory f(r.fs, "/books");
  f.setReaderBody(&r.body.face);
  f.setReaderMetrics(r.m);

  const reader::ArticleEndScreen::Facts article{
      1, "The Death and Life of the Great American Essay", "LONGREADS", 22, false, 2, true};

  // A BOOK, THEN AN ARTICLE, THEN A BOOK -- the sequence the hazard needs. Nothing
  // here calls a setter whose only job is the board; setReaderBook and setArticleEnd
  // each carry it, which is the whole change.
  f.setReaderBook(r.ob, 0);
  f.setArticleEnd(article);
  f.setReaderBook(r.ob, 0);

  auto back = f.create(ScreenId::Reader);
  REQUIRE(back != nullptr);
  CHECK(static_cast<reader::ReaderScreen*>(back.get())->endScreen() == ScreenId::BookEnd);

  // ...and the other direction still works from the same factory, so the reset is a
  // reset and not a one-way door of its own.
  f.setArticleEnd(article);
  auto on = f.create(ScreenId::Reader);
  REQUIRE(on != nullptr);
  CHECK(static_cast<reader::ReaderScreen*>(on.get())->endScreen() == ScreenId::ArticleEnd);
}
