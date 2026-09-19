// A PEEK OPENED AT A BLOCK, which is what a sighting on the Mentions screen needs.
//
// The peek took a SPINE and nothing else, and landed on page one of it. That is
// right for Contents -- you picked a chapter -- and wrong for a mention, which is a
// place inside one. The landing mechanism already existed: `ReaderScreen::restoreAt`
// is public and has to be called BEFORE `setMetrics`, so all that was missing was
// the parameter and the ordering.
#include <memory>
#include <string>

#include "card_book_fixture.h"
#include "doctest.h"
#include "ramp.h"
#include "reader/screen_peek.h"
#include "reader/screens.h"
#include "reader/theme_quiet.h"

using reader::Cursor;

namespace {

// Many separate paragraphs, so a later block is unmistakably not page one.
std::string manyBlocks() {
  std::string x;
  for (int i = 0; i < 40; ++i) {
    x += "<p>Paragraph number ";
    x += std::to_string(i);
    x += ", which is long enough to take a line or two of the panel and so to make "
         "the page it lands on unambiguous.</p>";
  }
  return x;
}

}  // namespace

TEST_CASE("a peek opens at a block, and without one it opens at the chapter") {
  cardfix::CardReading r(manyBlocks());
  reader::PageMetrics pm;
  r.theme.peekMetrics(480, 800, r.ramp.fonts, r.body.face, reader::Settings{}, pm);

  const auto landedBlock = [&](Cursor at) {
    reader::PeekScreen peek(r.fs, r.ob, 0, &r.body.face, at);
    peek.setMetrics(pm);
    return peek.chosenCursor().block;
  };

  SUBCASE("no cursor lands at the start of the chapter") {
    CHECK(landedBlock(Cursor{}) == 0);
  }
  SUBCASE("a cursor lands on its own page") {
    // THE POINT OF THE WHOLE CHANGE. A sighting in block 24 opens on the page that
    // holds it rather than on page one -- which is what makes a Mentions row worth
    // pressing. The landing is the page's FIRST block, so it is at or before the
    // sighting and never past it.
    const int landed = landedBlock(Cursor{24, 0});
    CHECK(landed > 0);
    CHECK(landed <= 24);
  }
  SUBCASE("a zero cursor is the absence of a request, not a request for block 0") {
    // Both spellings land identically, which is why the constructor does not arm
    // `restoreAt` for a zero cursor: block 0 line 0 IS where the walk goes anyway,
    // and arming it would cost an openAtCursor walk to reach where it already is.
    CHECK(landedBlock(Cursor{}) == landedBlock(Cursor{0, 0}));
  }
}

TEST_CASE("the factory carries the cursor, and Contents' overload clears it") {
  cardfix::CardReading r(manyBlocks());
  reader::PageMetrics pm;
  r.theme.peekMetrics(480, 800, r.ramp.fonts, r.body.face, reader::Settings{}, pm);

  const auto open = [&](bool withCursor) {
    reader::DemoScreenFactory f(r.fs, "/books");
    f.setReaderBody(&r.body.face);
    f.setReaderBook(r.ob, 0);
    f.setPeekMetrics(pm);
    // A LATCHED CURSOR WOULD BE THE SILENT-WRONG-PLACE BUG this change exists to
    // avoid, so `setPeek` clears what `setPeekAt` left: Contents must never inherit
    // a landing from a mention that happened to be opened first.
    f.setPeekAt(0, Cursor{24, 0});
    if (!withCursor) f.setPeek(0);
    std::unique_ptr<reader::Screen> s = f.create(reader::ScreenId::Peek);
    REQUIRE(s != nullptr);
    return static_cast<reader::PeekScreen&>(*s).chosenCursor().block;
  };
  CHECK(open(false) == 0);
  CHECK(open(true) > 0);
}
