// Reader: the first golden in this project that is not a 1-bit frame.
//
// The screen declares Fidelity::Grayscale, so what the panel is handed is three
// passes composed into four levels -- and that is the whole reason body text is
// rasterised at runtime rather than pre-rendered: a serif face at 32px has stems
// and serifs that hard-thresholding destroys. A Mono golden here would pin the
// wrong thing convincingly.
#include <string>
#include <vector>

#include "doctest.h"
#include "golden.h"
#include "ramp.h"
#include "reader_fixture.h"
#include "epub_fixtures.h"
#include "fake_fs.h"
#include "reader/book.h"
#include "reader/epub.h"
#include "reader/xml.h"
#include "reader/zip.h"
#include "reader/framebuffer.h"
#include "reader/layout.h"
#include "reader/scalablefont.h"
#include "reader/screen_reader.h"
#include "reader/screens.h"
#include "reader/theme_quiet.h"

namespace {

// The fixtures live in reader_fixture.h -- see its header for why they moved out of
// this file. Aliased so the cases below read exactly as they did.
using readerfix::Body;
using readerfix::Italic;
using readerfix::deferredChapter;
using readerfix::longChapter;
using readerfix::pageText;
using readerfix::Reading;

}  // namespace

TEST_CASE("QuietTheme renders Reader to golden on both panel geometries") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  Body body;

  auto renderOne = [&](int w, int h, const std::string& name) {
    // The column comes from Theme::readerMetrics and the pages from the real
    // ReaderScreen through the real factory, so this golden is laid out by exactly
    // the arithmetic the device runs -- not by a column this file chose.
    reader::PageMetrics m;
    theme.readerMetrics(w, h, ramp.fonts, body.face, m);
    reader::DemoScreenFactory factory;
    factory.setReaderBody(&body.face);
    factory.setReaderMetrics(m);
    factory.setReaderDemo();
    std::unique_ptr<reader::Screen> scr = factory.create(reader::ScreenId::Reader);
    REQUIRE(scr != nullptr);
    REQUIRE(scr->fidelity() == reader::Fidelity::Grayscale);
    // THE SETTLED STATE, which is what the board shows: a chapter opens with its
    // total unknown and the count arrives with the four-level refinement, within
    // five seconds. Rendering before that would pin `1 / —` as the baseline for a
    // screen the board draws as `53 / 890`.
    static_cast<reader::ReaderScreen*>(scr.get())->completeIndex();
    // Two planes composed into four levels, exactly as the panel's controller
    // combines them -- golden::checkGoldenGray has existed unused since the
    // grayscale path landed, waiting for the first screen that declares it.
    reader::Framebuffer lsb(w, h), msb(w, h);
    scr->render(lsb, ramp.fonts, theme, reader::Plane::Lsb);
    scr->render(msb, ramp.fonts, theme, reader::Plane::Msb);
    golden::checkGoldenGray(lsb, msb, name);
  };

  SUBCASE("X4 480x800") { renderOne(480, 800, "reader_quiet"); }
  SUBCASE("X3 528x792") { renderOne(528, 792, "reader_quiet_x3"); }
}

TEST_CASE("the factory refuses a Reader with no body face") {
  // A Reader that rendered nothing is indistinguishable from a book that failed to
  // open, so the refusal is at the push. Asserted because it is the one screen in
  // the factory that can answer null for a reason other than a missing parent.
  reader::DemoScreenFactory factory;
  CHECK(factory.create(reader::ScreenId::Reader) == nullptr);
}

TEST_CASE("A PAGE TURN MOVES THE PAGE, AND THE ENDS DO NOT WRAP") {
  // A list wraps off its end (Focus's rule); a book must not. Turning past the
  // last page landing back on page 1 would lose the reader's place silently.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  Body body;
  reader::PageMetrics m;
  theme.readerMetrics(480, 800, ramp.fonts, body.face, m);

  reader::DemoScreenFactory factory;
  factory.setReaderBody(&body.face);
  factory.setReaderMetrics(m);
  factory.setReaderDemo();
  std::unique_ptr<reader::Screen> scr = factory.create(reader::ScreenId::Reader);
  REQUIRE(scr != nullptr);
  auto& rd = static_cast<reader::ReaderScreen&>(*scr);
  // The settled state: a chapter opens with its count unknown and the device fills it
  // in within five seconds, inside the refinement.
  rd.completeIndex();
  REQUIRE(rd.pageCount() >= 2);

  const reader::InputEvent down{reader::Button::Down, reader::PressKind::Short};
  const reader::InputEvent up{reader::Button::Up, reader::PressKind::Short};

  CHECK(rd.vm().page == 1);
  CHECK(rd.onEvent(down).kind == reader::Action::Kind::Redraw);
  CHECK(rd.vm().page == 2);
  // Off the end: refused, and the page does not move.
  for (int i = 0; i < rd.pageCount() + 3; ++i) rd.onEvent(down);
  CHECK(rd.vm().page == rd.pageCount());
  CHECK(rd.onEvent(down).kind == reader::Action::Kind::None);
  // And back, without wrapping past page 1.
  for (int i = 0; i < rd.pageCount() + 3; ++i) rd.onEvent(up);
  CHECK(rd.vm().page == 1);
  CHECK(rd.onEvent(up).kind == reader::Action::Kind::None);
}

TEST_CASE("every page's lines are inside the column the theme reported") {
  // The overflow check the skill names, at both geometries: the X4 is 48px
  // narrower than the X3 and a justified line is placed to a right margin, so it
  // is the panel that fails first.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  Body body;
  for (const auto geo : {std::pair<int, int>{480, 800}, std::pair<int, int>{528, 792}}) {
    reader::PageMetrics m;
    theme.readerMetrics(geo.first, geo.second, ramp.fonts, body.face, m);
    reader::DemoScreenFactory factory;
    factory.setReaderBody(&body.face);
    factory.setReaderMetrics(m);
    factory.setReaderDemo();
    std::unique_ptr<reader::Screen> scr = factory.create(reader::ScreenId::Reader);
    REQUIRE(scr != nullptr);
    auto& rd = static_cast<reader::ReaderScreen&>(*scr);
    reader::Framebuffer fb(geo.first, geo.second);
    for (int p = 0; p < rd.pageCount(); ++p) {
      for (const reader::LaidLine& ln : rd.page().lines) {
        const int w = reader::drawTextJustified(fb, body.face, ln.x, ln.baselineY, ln.text,
                                                ln.extraPerGapF26);
        CAPTURE(geo.first);
        CAPTURE(std::string(ln.text));
        CHECK(ln.x >= m.columnLeft);
        CHECK(ln.x + w <= m.columnLeft + m.columnW);
        CHECK(ln.baselineY > m.columnTop);
        CHECK(ln.baselineY <= m.columnTop + m.columnH);
      }
      rd.onEvent({reader::Button::Down, reader::PressKind::Short});
    }
  }
}

// --- The streaming reader ----------------------------------------------------
//
// The golden above renders the demo chapter, which is two paragraphs. What these
// exercise is the paging: an index built by one decode, forward turns that continue
// the stream, and backward turns that rewind and decode to a recorded cursor.

namespace {





}  // namespace

TEST_CASE("A LONG CHAPTER PAGINATES AND EVERY PAGE IS REACHABLE FORWARD") {
  Reading r(longChapter(60));
  REQUIRE(r.scr->pageCount() > 8);
  const reader::InputEvent down{reader::Button::Down, reader::PressKind::Short};

  std::vector<std::string> pages;
  pages.push_back(pageText(r.scr->page()));
  for (int i = 1; i < r.scr->pageCount(); ++i) {
    REQUIRE(r.scr->onEvent(down).kind == reader::Action::Kind::Redraw);
    CHECK(r.scr->vm().page == i + 1);
    pages.push_back(pageText(r.scr->page()));
    CHECK_FALSE(pages.back().empty());
  }
  // Off the end: refused, and the page does not move.
  CHECK(r.scr->onEvent(down).kind == reader::Action::Kind::None);
  CHECK(r.scr->pageIndex() == r.scr->pageCount() - 1);

  // No page repeated, which is what an off-by-one in the index would produce.
  for (size_t i = 1; i < pages.size(); ++i) CHECK(pages[i] != pages[i - 1]);
}

TEST_CASE("READING BACKWARD GIVES EXACTLY THE PAGES READING FORWARD GAVE") {
  // The strongest test in this file. A backward turn rewinds the stream and decodes
  // to a cursor recorded during the index pass -- so it exercises the rewind, the
  // buffer reuse, startAt's discard path and the index all at once, and any of them
  // being off by a line shows up as a page that differs from its forward self.
  Reading r(longChapter(40));
  const int n = r.scr->pageCount();
  REQUIRE(n > 6);
  const reader::InputEvent down{reader::Button::Down, reader::PressKind::Short};
  const reader::InputEvent up{reader::Button::Up, reader::PressKind::Short};

  std::vector<std::string> forward;
  forward.push_back(pageText(r.scr->page()));
  for (int i = 1; i < n; ++i) {
    REQUIRE(r.scr->onEvent(down).kind == reader::Action::Kind::Redraw);
    forward.push_back(pageText(r.scr->page()));
  }

  for (int i = n - 2; i >= 0; --i) {
    REQUIRE(r.scr->onEvent(up).kind == reader::Action::Kind::Redraw);
    CAPTURE(i);
    CHECK(r.scr->vm().page == i + 1);
    CHECK(pageText(r.scr->page()) == forward[static_cast<size_t>(i)]);
  }
  // And the first page does not wrap to the last.
  CHECK(r.scr->onEvent(up).kind == reader::Action::Kind::None);
  CHECK(r.scr->pageIndex() == 0);
}

TEST_CASE("a forward turn after a backward one still continues correctly") {
  // A backward turn spends the builder, so the next forward turn takes the seek
  // path rather than the fast one. Both must land on the same page.
  Reading r(longChapter(30));
  REQUIRE(r.scr->pageCount() > 4);
  const reader::InputEvent down{reader::Button::Down, reader::PressKind::Short};
  const reader::InputEvent up{reader::Button::Up, reader::PressKind::Short};

  r.scr->onEvent(down);
  r.scr->onEvent(down);
  const std::string atThree = pageText(r.scr->page());
  REQUIRE(r.scr->vm().page == 3);

  r.scr->onEvent(up);
  REQUIRE(r.scr->vm().page == 2);
  r.scr->onEvent(down);
  CHECK(r.scr->vm().page == 3);
  CHECK(pageText(r.scr->page()) == atThree);
}

TEST_CASE("THE PAGE INDEX IS THE ONLY THING THAT GROWS WITH THE CHAPTER") {
  // 10x the chapter must not mean 10x the reader. The index is one cursor a page --
  // 8 bytes -- and everything else is fixed: the inflate window, the tokenizer's
  // buffers, one block, one page.
  Reading small(longChapter(10));
  Reading large(longChapter(200));
  REQUIRE(small.scr->pageCount() > 1);
  REQUIRE(large.scr->pageCount() > 10 * small.scr->pageCount() / 2);

  // The fixed parts, named so a buffer growing shows up here.
  CAPTURE(sizeof(reader::Xml));
  CAPTURE(reader::Inflater::kHeapBytes);
  CHECK(sizeof(reader::Xml) <= 3072);
  CHECK(reader::Inflater::kHeapBytes <= 40u * 1024u);
  // And the index's own cost, for the largest chapter this book has.
  const size_t indexBytes = static_cast<size_t>(large.scr->pageCount()) * sizeof(reader::Cursor);
  CAPTURE(indexBytes);
  CHECK(indexBytes < 8192);
}

TEST_CASE("a chapter streamed from a card reads the same as one from memory") {
  // The two entry points through ChapterReader -- an inflating stream off a
  // FileHandle, and a BufferSource over bytes already held -- must produce the same
  // pages. Everything above uses the second because the goldens have no card.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  Body body;
  reader::PageMetrics m;
  theme.readerMetrics(480, 800, ramp.fonts, body.face, m);

  FakeFileSystem fs;
  REQUIRE(fs.writeAll("/books/b.epub",
                      std::string_view(reinterpret_cast<const char*>(epubfix::kEpubGood),
                                       epubfix::kEpubGoodLen)));
  reader::OpenedBook ob;
  const char* why = "";
  REQUIRE_MESSAGE(reader::openBook(fs, "/books/b.epub", ob, &why), std::string(why));

  reader::ReaderScreen fromCard(fs, ob, 0, &body.face);
  fromCard.setMetrics(m);

  // The same chapter's bytes, read out whole and streamed from memory instead.
  std::unique_ptr<reader::FileHandle> f = fs.openRead("/books/b.epub");
  REQUIRE(f != nullptr);
  reader::Zip zip;
  REQUIRE(zip.open(*f));
  reader::Epub ep;
  REQUIRE(ep.open(*f, zip));
  std::string xhtml;
  REQUIRE(zip.read(*f, *zip.find(ep.chapters()[0].path), xhtml));
  reader::ReaderScreen fromMemory(xhtml, "T", "CH. 01", &body.face);
  fromMemory.setMetrics(m);

  REQUIRE(fromCard.pageCount() == fromMemory.pageCount());
  CHECK(fromCard.pageCount() > 0);
  CHECK(pageText(fromCard.page()) == pageText(fromMemory.page()));
}


TEST_CASE("PAGING OFF THE END OF A CHAPTER OPENS THE NEXT ONE") {
  // What makes this a reader rather than a chapter viewer -- and the defect the
  // device found: spine entry 0 of a real EPUB is a cover, one <img> and no text, so
  // it paginates to nothing. Skipping to the first chapter with text would only have
  // moved the dead end to the bottom of that chapter.
  //
  // The fixture is a two-chapter EPUB, so reaching the second one can only happen
  // by paging into it.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  Body body;
  reader::PageMetrics m;
  theme.readerMetrics(480, 800, ramp.fonts, body.face, m);

  FakeFileSystem fs;
  REQUIRE(fs.writeAll("/books/b.epub",
                      std::string_view(reinterpret_cast<const char*>(epubfix::kEpubGood),
                                       epubfix::kEpubGoodLen)));
  reader::OpenedBook ob;
  const char* why = "";
  REQUIRE_MESSAGE(reader::openBook(fs, "/books/b.epub", ob, &why), std::string(why));

  reader::ReaderScreen scr(fs, ob, 0, &body.face);
  scr.setMetrics(m);
  REQUIRE(scr.pageCount() > 0);
  const int firstChapter = scr.chapterIndex();
  const std::string firstPage = pageText(scr.page());

  const reader::InputEvent down{reader::Button::Down, reader::PressKind::Short};
  const reader::InputEvent up{reader::Button::Up, reader::PressKind::Short};

  // Page to the end of this chapter.
  int guard = 0;
  while (scr.chapterIndex() == firstChapter && guard++ < 200) {
    if (scr.onEvent(down).kind != reader::Action::Kind::Redraw) break;
  }
  if (ob.chapterCount() > 1) {
    // It moved on rather than stopping.
    CHECK(scr.chapterIndex() > firstChapter);
    CHECK(scr.pageCount() > 0);
    CHECK(scr.vm().page == 1);  // landed on the new chapter's FIRST page
    // The label follows the spine position.
    CHECK(scr.vm().chapter != "CH. 01");

    // And back off the top lands on the PREVIOUS chapter's LAST page, not its first.
    REQUIRE(scr.onEvent(up).kind == reader::Action::Kind::Redraw);
    CHECK(scr.chapterIndex() == firstChapter);
    CHECK(scr.vm().page == scr.pageCount());
    // Paging back to the very start gives the page we began on.
    int g2 = 0;
    while (scr.vm().page > 1 && g2++ < 200) scr.onEvent(up);
    CHECK(pageText(scr.page()) == firstPage);
  }
  // Off the very front of the book: refused, and nothing moves.
  CHECK(scr.onEvent(up).kind == reader::Action::Kind::None);
  CHECK(scr.chapterIndex() == firstChapter);
}

TEST_CASE("A CHAPTER THAT PAGINATES TO NOTHING IS SKIPPED, not shown blank") {
  // The device's exact symptom: a blank page reading 0/0. Three of the 92 spine
  // entries in one real book are an <img> and nothing document.h models.
  //
  // Built here rather than mocked: a two-entry EPUB whose FIRST entry has no text.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  Body body;
  reader::PageMetrics m;
  theme.readerMetrics(480, 800, ramp.fonts, body.face, m);

  // The in-memory constructor is the one place a textless chapter can be handed
  // over directly, and it must not pretend to have a page.
  reader::ReaderScreen empty("<html><body><img src=\"cover.png\"/></body></html>", "T",
                             "CH. 01", &body.face);
  empty.setMetrics(m);
  // Nought pages is a KNOWN count, so nothing is pending.
  CHECK_FALSE(empty.indexPending());
  CHECK(empty.pageCount() == 0);
  CHECK(empty.vm().pageTotal == 0);
  CHECK(empty.page().lines.empty());
  // And it does not claim a page it does not have.
  CHECK(empty.vm().page == 0);
  CHECK(empty.vm().progressPercent == 0);
}

TEST_CASE("A REFUSED CHAPTER TURN LEAVES THE SCREEN WHERE IT WAS") {
  // Running off either end of the book used to corrupt the screen: the walk opens
  // each candidate before it can know whether that candidate has pages, so the last
  // one tried was left in `chapterAt_` with an empty index -- the device reported
  // "spine 0, page 1/7" for a spine entry with no pages at all, with a stale page
  // still on the panel.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  Body body;
  reader::PageMetrics m;
  theme.readerMetrics(480, 800, ramp.fonts, body.face, m);

  FakeFileSystem fs;
  REQUIRE(fs.writeAll("/books/b.epub",
                      std::string_view(reinterpret_cast<const char*>(epubfix::kEpubGood),
                                       epubfix::kEpubGoodLen)));
  reader::OpenedBook ob;
  const char* why = "";
  REQUIRE(reader::openBook(fs, "/books/b.epub", ob, &why));

  reader::ReaderScreen scr(fs, ob, 0, &body.face);
  scr.setMetrics(m);
  REQUIRE(scr.pageCount() > 0);

  const reader::InputEvent up{reader::Button::Up, reader::PressKind::Short};
  // Page back to the very front of the book.
  int guard = 0;
  while (scr.onEvent(up).kind == reader::Action::Kind::Redraw && guard++ < 2000) {
  }
  // Whatever it landed on, it is a REAL position: a page count, a page inside it,
  // and lines on the screen.
  CHECK(scr.pageCount() > 0);
  // pageTotal is 0 while the count is unknown -- pageCount() is pages KNOWN. Either
  // it is unknown, or it agrees.
  CHECK((scr.vm().pageTotal == 0 || scr.vm().pageTotal == scr.pageCount()));
  CHECK(scr.vm().page >= 1);
  CHECK(scr.vm().page <= scr.pageCount());
  CHECK_FALSE(scr.page().lines.empty());
  // And a further refusal changes nothing.
  const int wasChapter = scr.chapterIndex();
  const int wasPage = scr.vm().page;
  CHECK(scr.onEvent(up).kind == reader::Action::Kind::None);
  CHECK(scr.chapterIndex() == wasChapter);
  CHECK(scr.vm().page == wasPage);
  CHECK_FALSE(scr.page().lines.empty());
}

TEST_CASE("THE FACTORY REFUSES A READER IT HAS NO BOOK FOR") {
  // It used to fall through to the demo chapter, so a session restore -- where
  // nothing has called setReaderBook, because the shell only calls it from a button
  // press -- silently built a Reader full of Middlemarch. The device woke from sleep
  // showing fiction from a book the user was not reading.
  //
  // A factory that substitutes content is worse than one that refuses: a refused
  // push leaves the Library standing, which is wrong in a way the user can see
  // through.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  Body body;
  reader::PageMetrics m;
  theme.readerMetrics(480, 800, ramp.fonts, body.face, m);

  reader::DemoScreenFactory bare;
  bare.setReaderBody(&body.face);
  bare.setReaderMetrics(m);
  // A body face and metrics but no book and no demo asked for.
  CHECK(bare.create(reader::ScreenId::Reader) == nullptr);

  // The demo has to be ASKED for, and then it builds.
  bare.setReaderDemo();
  std::unique_ptr<reader::Screen> demo = bare.create(reader::ScreenId::Reader);
  REQUIRE(demo != nullptr);
  CHECK(static_cast<reader::ReaderScreen*>(demo.get())->pageCount() > 0);
}


// --- The index built by reading ----------------------------------------------

TEST_CASE("A CHAPTER OPENS WITH ITS TOTAL UNKNOWN, and completeIndex fills it in") {
  // Counting the whole chapter before the first page appeared cost ~545 ms on the
  // device and made a crossing twice an ordinary turn. So the count arrives later,
  // and until it does the footer says so -- pageTotal 0, which the theme draws as an
  // em dash (design/Reader.dc.html states it).
  Reading r(deferredChapter(), /*settled=*/false);
  REQUIRE(r.scr->pageCount() >= 1);
  CHECK(r.scr->indexPending());
  CHECK(r.scr->vm().pageTotal == 0);
  CHECK(r.scr->vm().progressPercent == 0);  // a percentage of an unknown is not a number
  CHECK(r.scr->vm().page == 1);
  CHECK_FALSE(r.scr->page().lines.empty());

  // Completing it does not move the reader.
  const std::string wasOn = pageText(r.scr->page());
  REQUIRE(r.scr->completeIndex());
  CHECK_FALSE(r.scr->indexPending());
  CHECK(r.scr->vm().pageTotal == r.scr->pageCount());
  CHECK(r.scr->vm().pageTotal > 5);
  CHECK(r.scr->vm().page == 1);
  CHECK(pageText(r.scr->page()) == wasOn);
  // A percentage, not necessarily a positive one: page 1 of ~300 rounds to 0, which
  // is the honest answer. What matters is that it is now derived from a real total
  // rather than from an unknown.
  CHECK(r.scr->vm().progressPercent >= 0);
  CHECK(r.scr->vm().progressPercent <= 100);
  // And it is idempotent.
  CHECK_FALSE(r.scr->completeIndex());
}

TEST_CASE("THE INDEX GROWS BY READING, and the pages are the same either way") {
  // The risk in building the index lazily is that a page reached by streaming
  // differs from the same page reached through a completed index. Every page, both
  // ways.
  const std::string doc = deferredChapter();
  const reader::InputEvent down{reader::Button::Down, reader::PressKind::Short};

  // The count first, so the walk below has a guard derived from the chapter rather
  // than a number picked out of the air -- an arbitrary 200 truncated this at 201
  // pages of 299 and the comparison then failed for the guard's reasons, not the
  // code's.
  Reading eager(doc, /*settled=*/false);
  REQUIRE(eager.scr->completeIndex());
  const int total = eager.scr->pageCount();
  REQUIRE(total > 4);

  Reading lazy(doc, /*settled=*/false);
  std::vector<std::string> viaStream;
  viaStream.push_back(pageText(lazy.scr->page()));
  CHECK(lazy.scr->pageCount() == 1);  // only page one is known at the start
  for (int i = 0; i < total + 8; ++i) {
    if (lazy.scr->onEvent(down).kind != reader::Action::Kind::Redraw) break;
    viaStream.push_back(pageText(lazy.scr->page()));
    // It grew by exactly one each time.
    CHECK(lazy.scr->pageCount() == static_cast<int>(viaStream.size()));
  }
  CHECK_FALSE(lazy.scr->indexPending());  // the end was reached, so the count is known
  REQUIRE(static_cast<int>(viaStream.size()) == total);
  std::vector<std::string> viaIndex;
  viaIndex.push_back(pageText(eager.scr->page()));
  for (size_t i = 1; i < viaStream.size(); ++i) {
    REQUIRE(eager.scr->onEvent(down).kind == reader::Action::Kind::Redraw);
    viaIndex.push_back(pageText(eager.scr->page()));
  }
  for (size_t i = 0; i < viaStream.size(); ++i) {
    CAPTURE(i);
    CHECK(viaStream[i] == viaIndex[i]);
  }
}

TEST_CASE("going back works on an index that was built by reading") {
  // The pages visited are in the index, so a backward turn has a cursor to seek to
  // even though the chapter was never counted.
  Reading r(deferredChapter(), /*settled=*/false);
  const reader::InputEvent down{reader::Button::Down, reader::PressKind::Short};
  const reader::InputEvent up{reader::Button::Up, reader::PressKind::Short};

  std::vector<std::string> forward{pageText(r.scr->page())};
  for (int i = 0; i < 4; ++i) {
    REQUIRE(r.scr->onEvent(down).kind == reader::Action::Kind::Redraw);
    forward.push_back(pageText(r.scr->page()));
  }
  CHECK(r.scr->indexPending());  // still not counted to the end
  for (int i = 3; i >= 0; --i) {
    REQUIRE(r.scr->onEvent(up).kind == reader::Action::Kind::Redraw);
    CAPTURE(i);
    CHECK(pageText(r.scr->page()) == forward[static_cast<size_t>(i)]);
  }
  // And forward again after the backward turns, which takes the re-seek path.
  REQUIRE(r.scr->onEvent(down).kind == reader::Action::Kind::Redraw);
  CHECK(pageText(r.scr->page()) == forward[1]);
}


// --- The styled specimens ------------------------------------------------------
//
// design/ReaderChapterOpen.dc.html and design/ReaderList.dc.html. These two exist
// because the reader can now do four things to a block that `reader_quiet` shows
// none of -- a heading, an italic inset blockquote, inline emphasis and a
// hanging-indent list -- and because the work that added them RESTRUCTURED the
// shared text path: `drawRun` became an F26 core with the integer form as a wrapper,
// and PageBuilder's emit loop grew per-kind columns, tracking and blank rows.
//
// `reader_quiet` cannot catch a regression in any of that. Its demo has no heading,
// no quote, no list and no `<em>`, so every one of those code paths is dead in it.
// These are the goldens that make the refactor defended rather than merely tested.

TEST_CASE("QuietTheme renders the styled reader specimens to golden") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  Body body;
  Italic italic;

  auto renderOne = [&](int w, int h, reader::DemoScreenFactory::ReaderStyleDemo which,
                       const std::string& name) {
    reader::PageMetrics m;
    theme.readerMetrics(w, h, ramp.fonts, body.face, m);
    // THE ITALIC GOES IN THE METRICS, not only in the draw: the WRAP measures
    // emphasis with it, and the two faces differ in width by 6%-9%. A golden blessed
    // with it missing here would pin a page measured roman and drawn in two faces.
    m.italic = &italic.face;
    reader::DemoScreenFactory factory;
    factory.setReaderBody(&body.face);
    factory.setReaderItalic(&italic.face);
    factory.setReaderMetrics(m);
    factory.setReaderStyleDemo(which);
    std::unique_ptr<reader::Screen> scr = factory.create(reader::ScreenId::Reader);
    REQUIRE(scr != nullptr);
    REQUIRE(scr->fidelity() == reader::Fidelity::Grayscale);
    static_cast<reader::ReaderScreen*>(scr.get())->completeIndex();
    reader::Framebuffer lsb(w, h), msb(w, h);
    scr->render(lsb, ramp.fonts, theme, reader::Plane::Lsb);
    scr->render(msb, ramp.fonts, theme, reader::Plane::Msb);
    golden::checkGoldenGray(lsb, msb, name);
  };

  using Demo = reader::DemoScreenFactory::ReaderStyleDemo;
  SUBCASE("chapter open, X4") { renderOne(480, 800, Demo::ChapterOpen, "reader_chapter_open"); }
  SUBCASE("chapter open, X3") {
    renderOne(528, 792, Demo::ChapterOpen, "reader_chapter_open_x3");
  }
  SUBCASE("list, X4") { renderOne(480, 800, Demo::List, "reader_list"); }
  SUBCASE("list, X3") { renderOne(528, 792, Demo::List, "reader_list_x3"); }
}
