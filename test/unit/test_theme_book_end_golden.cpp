// design/BookEnd.dc.html, pinned per pixel at both panel geometries.
//
// BOTH GEOMETRIES, because a layout that fits one can clip the other and the X3 is the
// dev device. The X4 is 480x800 and the X3 528x792, and this screen's header band puts
// two runs on one line with nothing but `space-between` between them -- the case a 48px
// narrower panel fails first.
//
// AND THE GOLDEN IS THE ONLY THING THAT CAN CATCH THE BAND'S MARK. drawHeaderBand
// DEFAULTS to the battery and this board draws none, so renderBookEnd passes null. A
// structural test cannot see the difference: the battery is 21px and Value700 is 25px,
// so the band does not change height and every run below it stays exactly where it was.
// Only a pixel does.
#include <memory>
#include <string>

#include "doctest.h"
#include "golden.h"
#include "ramp.h"
#include "reader/framebuffer.h"
#include "reader/screen_book_end.h"
#include "reader/screens.h"
#include "reader/theme_quiet.h"

TEST_CASE("QuietTheme renders the end of a book to golden on both panel geometries") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;

  // The fidelity is asserted, not assumed: this test names a plane, and if the screen's
  // declared path ever moves the golden must stop matching rather than quietly keep
  // pinning a path nothing paints. BookEnd takes the default Fidelity::Mono, so the
  // golden is the single 1-bit frame the panel is handed -- one pass with Plane::Bw,
  // exactly what paintMono() in the shell and the simulator render. The tick's
  // diagonals are hard-thresholded by it, which is what the reference firmware does to
  // its chrome.
  REQUIRE(reader::BookEndScreen(reader::demoBookEndFacts()).fidelity() ==
          reader::Fidelity::Mono);

  auto renderOne = [&](int w, int h, const std::string& name) {
    reader::DemoScreenFactory factory;
    // ASKED FOR, as the Reader's and Contents' demos are: the factory refuses a BookEnd
    // that nothing primed rather than substituting another book's title for one the
    // reader just finished.
    factory.setBookEndDemo();
    std::unique_ptr<reader::Screen> scr = factory.create(reader::ScreenId::BookEnd);
    REQUIRE(scr != nullptr);
    reader::Framebuffer fb(w, h);
    scr->render(fb, ramp.fonts, theme, reader::Plane::Bw);
    golden::checkGolden(fb, name);
  };

  // The demo's `libraryBeneath` is true, so both frames draw BACK TO LIBRARY. The BACK
  // TO HOME spelling deliberately has NO golden: it is on no board, and a golden for an
  // unboarded state pins pixels nobody designed. test_screen_book_end.cpp covers the
  // string.
  SUBCASE("X4 480x800") { renderOne(480, 800, "book_end"); }
  SUBCASE("X3 528x792") { renderOne(528, 792, "book_end_x3"); }
}
