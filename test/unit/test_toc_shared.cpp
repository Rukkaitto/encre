// ONE TABLE OF CONTENTS, HOWEVER MANY SCREENS DRAW FROM IT.
//
// Reported off the device: `L'Avenir en commun` opened to white pages, with the
// header band and the footer drawn and no body text at all. The desktop rendered
// every page of it, through the real ReaderScreen and through the real shell, which
// is what pointed at the heap rather than at the layout.
//
// The book's NCX gives 145 entries whose labels average 51 bytes -- nearly all too long
// for the small-string buffer, so each is a heap block of its own. About 13 KB on
// the device's 32-bit heap, and the shell, the factory and the Reader each held a
// COPY, with Contents and the peek making a fourth while open: ~40 KB resident
// against a reading floor measured at 28,508 bytes. The Reader's own comment priced
// its copy at "~1.2 KB for a 96-entry book", which was right for that book.
//
// WHAT RAN OUT IS THE GLYPH RASTERISER, and it fails without a sound:
// stb_truetype's transient allocation returns null, ScalableFont::glyph answers
// nullopt, and drawText leaves the glyph's box empty. Layout needs no bitmaps and the
// header, footer and chrome are pre-built faces, so everything on the page drew
// except the text.
//
// So the list is ONE immutable object and every holder shares it. These tests pin
// that the screens the factory builds hold the factory's list rather than a copy of
// it -- a by-value member would compile and pass every content test while costing a
// long book's contents again.
#include <memory>
#include <vector>

#include "card_book_fixture.h"
#include "doctest.h"
#include "reader/screen_contents.h"
#include "reader/screen_peek.h"
#include "reader/screen_reader.h"
#include "reader/screens.h"
#include "reader/toc.h"

using reader::ScreenId;
using reader::SharedToc;
using reader::TocEntry;

TEST_CASE("the Reader, Contents and the peek hold the factory's list, not copies of it") {
  cardfix::CardReading r("<p>one</p>");
  reader::DemoScreenFactory f(r.fs, "/books");
  f.setReaderBody(&r.body.face);
  f.setReaderMetrics(r.m);
  f.setPeekMetrics(r.m);
  f.setContentsVisibleRows(8);
  f.setReaderBook(r.ob, 0);

  const SharedToc toc = reader::shareToc({{0, 1, "PARTIE 1"}, {1, 2, "Chapitre 1"}});
  f.setContents(toc, 0);
  f.setPeek(1);

  auto rd = f.create(ScreenId::Reader);
  REQUIRE(rd != nullptr);
  CHECK(static_cast<reader::ReaderScreen*>(rd.get())->chapterNames().get() == toc.get());

  auto contents = f.create(ScreenId::Contents);
  REQUIRE(contents != nullptr);
  CHECK(static_cast<reader::ContentsScreen*>(contents.get())->toc().get() == toc.get());

  auto peek = f.create(ScreenId::Peek);
  REQUIRE(peek != nullptr);
  CHECK(static_cast<reader::PeekScreen*>(peek.get())->chapterNames().get() == toc.get());

  // The caller's handle, the factory's, and one per screen. Each holder that took a
  // deep copy instead would leave this one short.
  CHECK(toc.use_count() == 5);
}

TEST_CASE("sharing a list gives back the slack a doubling growth left in it") {
  // loadToc and fillTocGaps grow through pushOrRefuse, which doubles: L'Avenir en
  // commun's 145 entries sat in a 256-slot buffer, 3.5 KB of TocEntry nothing reads.
  // The list is immutable once shared, so that is the one moment to trim it.
  std::vector<TocEntry> grown;
  grown.reserve(256);
  for (int i = 0; i < 145; ++i) grown.push_back({i, 1, "x"});
  REQUIRE(grown.capacity() == 256);

  const SharedToc toc = reader::shareToc(std::move(grown));
  REQUIRE(toc != nullptr);
  CHECK(toc->size() == 145);
  CHECK(toc->capacity() == 145);
}

TEST_CASE("an empty list is still a list, so no holder has to ask about null") {
  // A book with no NCX reads fine and simply cannot name its chapters. Sharing that
  // must give a real empty vector, or every holder grows a null check that one of
  // them will forget.
  const SharedToc toc = reader::shareToc({});
  REQUIRE(toc != nullptr);
  CHECK(toc->empty());
}
