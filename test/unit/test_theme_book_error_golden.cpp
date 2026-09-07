// design/BookError.dc.html and design/BookErrorUnreadable.dc.html, pinned per pixel
// at both panel geometries.
//
// BOTH GEOMETRIES, because a layout that fits one can clip the other and the X3 is
// the dev device. This panel is 380 wide on a 480 canvas -- 50px of margin either
// side on the X4 against 74 on the X3 -- and its paragraph wraps against a column
// that is the same width on both, so the two differ in where the panel is CENTRED
// and in how much veil surrounds it.
//
// BOTH SHAPES, because the only difference between them is a sentence, and a
// sentence is exactly what a structural test cannot see: the two wrap to different
// heights, which moves the panel, both slabs and the whole centring. Only a pixel
// catches that.
#include <string>
#include <utility>

#include "doctest.h"
#include "golden.h"
#include "library_app.h"
#include "ramp.h"
#include "reader/framebuffer.h"
#include "reader/screen_book_error.h"
#include "reader/screens.h"
#include "reader/theme_quiet.h"

TEST_CASE("QuietTheme renders both corrupt-book shapes to golden at both geometries") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;

  // The fidelity is asserted, not assumed: this test names a plane, and if the
  // screen's declared path ever moves, the golden must stop matching rather than
  // quietly keep pinning a path nothing paints. BookError takes the default
  // Fidelity::Mono, so the golden is the single 1-bit frame the panel is handed --
  // one pass with Plane::Bw, exactly what paintMono() in the shell and the
  // simulator render. The warning triangle's three diagonals are hard-thresholded
  // by it, which is what the reference firmware does to its chrome.
  REQUIRE(reader::BookErrorScreen(reader::demoBookErrorFacts()).fidelity() ==
          reader::Fidelity::Mono);

  auto renderOne = [&](reader::BookErrorScreen::Facts facts, int w, int h,
                       const std::string& name) {
    // THE SIMULATOR'S OWN JOURNEY, which is `libraryEntry(5)` then a direct push --
    // Home, Down, Confirm into the Library, then five Downs to Dubliners, which is
    // the sixth row and the row both boards draw focused under the veil. Reached by
    // pressing rather than by assignment, so the golden pins the navigation too;
    // and it is LibraryApp rather than a bare factory.create(ScreenId::Library),
    // because a Library nobody handed a visible-row count to renders EMPTY and the
    // boards draw a full list.
    libapp::LibraryApp app(theme, ramp.fonts, h, 5);
    REQUIRE(app.app.top().id() == reader::ScreenId::Library);
    // NOT reached by pressing, for the sleep screen's reason: no gesture on a
    // Library row raises this dialog. The SHELL raises it, when the open it tried
    // refuses -- so pushing it directly is the honest model of what happens on the
    // device.
    app.factory.setBookErrorFacts(std::move(facts));
    REQUIRE(app.app.pushScreen(reader::ScreenId::BookError));

    reader::Framebuffer fb(w, h);
    // App::render, NEVER top().render -- an overlay rendered on its own is a panel
    // floating on white, and nothing on the desktop catches it because the
    // simulator and every golden go through App::render. It has happened once.
    app.app.render(fb, ramp.fonts, theme, reader::Plane::Bw);
    golden::checkGolden(fb, name);
  };

  SUBCASE("damaged X4 480x800") {
    renderOne(reader::demoBookErrorFacts(), 480, 800, "book_error");
  }
  SUBCASE("damaged X3 528x792") {
    renderOne(reader::demoBookErrorFacts(), 528, 792, "book_error_x3");
  }
  SUBCASE("unreadable X4 480x800") {
    renderOne(reader::demoBookErrorUnreadableFacts(), 480, 800, "book_error_unreadable");
  }
  SUBCASE("unreadable X3 528x792") {
    renderOne(reader::demoBookErrorUnreadableFacts(), 528, 792, "book_error_unreadable_x3");
  }
}
