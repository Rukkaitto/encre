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
#include "reader/framebuffer.h"
#include "reader/layout.h"
#include "reader/scalablefont.h"
#include "reader/screen_reader.h"
#include "reader/screens.h"
#include "reader/theme_quiet.h"

namespace {

// The body face at the board's `font-size: 32px`, with its bytes beside it: a
// ScalableFont borrows the buffer it was initialised from and never copies it.
struct Body {
  std::vector<uint8_t> bytes = golden::slurp(std::string(ASSETS_DIR) + "/built/literata_body.ttf");
  reader::ScalableFont face;
  Body() {
    REQUIRE(face.init(bytes.data(), bytes.size(), reader::kBodyPpem));
    REQUIRE(face.ready());
  }
};

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
    std::unique_ptr<reader::Screen> scr = factory.create(reader::ScreenId::Reader);
    REQUIRE(scr != nullptr);
    REQUIRE(scr->fidelity() == reader::Fidelity::Grayscale);
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
  std::unique_ptr<reader::Screen> scr = factory.create(reader::ScreenId::Reader);
  REQUIRE(scr != nullptr);
  auto& rd = static_cast<reader::ReaderScreen&>(*scr);
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
