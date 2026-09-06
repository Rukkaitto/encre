// design/LowBattery.dc.html. The Reader declares Fidelity::Grayscale, so this goes
// through checkGoldenGray -- a Mono golden of this screen would pin the wrong thing
// convincingly.
#include <memory>
#include <string>

#include "doctest.h"
#include "golden.h"
#include "ramp.h"
#include "reader_fixture.h"
#include "reader/framebuffer.h"
#include "reader/screen_reader.h"
#include "reader/screens.h"
#include "reader/theme_quiet.h"

using namespace reader;

TEST_CASE("QuietTheme renders the low-battery banner to golden at both geometries") {
  ramp::Ramp ramp;
  QuietTheme theme;
  readerfix::Body body;

  auto renderOne = [&](int w, int h, const std::string& name) {
    PageMetrics m;
    theme.readerMetrics(w, h, ramp.fonts, body.face, Settings{}, m);
    DemoScreenFactory factory;
    factory.setReaderBody(&body.face);
    factory.setReaderMetrics(m);
    factory.setReaderDemo();
    std::unique_ptr<Screen> scr = factory.create(ScreenId::Reader);
    REQUIRE(scr != nullptr);
    REQUIRE(scr->fidelity() == Fidelity::Grayscale);
    auto* r = static_cast<ReaderScreen*>(scr.get());
    // The settled state, as test_theme_reader_golden.cpp renders it: the board draws
    // `53 / 890`, not the transient `1 / —`.
    r->completeIndex();
    // The board's own 5%.
    r->setBatteryLow(5);
    Framebuffer lsb(w, h), msb(w, h);
    scr->render(lsb, ramp.fonts, theme, Plane::Lsb);
    scr->render(msb, ramp.fonts, theme, Plane::Msb);
    golden::checkGoldenGray(lsb, msb, name);
  };

  SUBCASE("X4 480x800") { renderOne(480, 800, "low_battery"); }
  SUBCASE("X3 528x792") { renderOne(528, 792, "low_battery_x3"); }
}

TEST_CASE("the banner does not change how many lines the page holds") {
  // THE PROPERTY THE WHOLE DESIGN RESTS ON. A band inside the column takes a
  // default page from 12 lines to 10 and re-paginates the chapter; drawn over it,
  // the page is byte-for-byte the page that was already there. Asserted on the
  // LINES rather than on the pixels, because the pixels legitimately differ.
  ramp::Ramp ramp;
  QuietTheme theme;
  readerfix::Body body;
  PageMetrics m;
  theme.readerMetrics(480, 800, ramp.fonts, body.face, Settings{}, m);
  DemoScreenFactory factory;
  factory.setReaderBody(&body.face);
  factory.setReaderMetrics(m);
  factory.setReaderDemo();
  std::unique_ptr<Screen> scr = factory.create(ScreenId::Reader);
  REQUIRE(scr != nullptr);
  auto* r = static_cast<ReaderScreen*>(scr.get());
  r->completeIndex();
  const size_t before = r->page().lines.size();
  const int pageBefore = r->vm().page;
  const int totalBefore = r->vm().pageTotal;
  r->setBatteryLow(5);
  CHECK(r->page().lines.size() == before);
  CHECK(r->vm().page == pageBefore);
  CHECK(r->vm().pageTotal == totalBefore);
}
