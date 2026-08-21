// The provisional Phase 2B surface: the stub screen, the Input Monitor, and the
// theme method they both render through.
//
// No goldens here on purpose. These screens are scaffolding Phase 2C deletes,
// and pinning provisional pixels would only create churn. What is worth pinning
// is the behaviour -- focus, the pops and pushes, the declared fidelity, the hold
// mask and the log -- plus the two things a render can get wrong regardless of
// how it looks: drawing nothing at all, and drawing past the panel's edges.
#include <string>
#include <vector>

#include "doctest.h"
#include "ramp.h"
#include "reader/components.h"
#include "reader/framebuffer.h"
#include "reader/icons.h"
#include "reader/screen_input_monitor.h"
#include "reader/screen_stub.h"
#include "reader/theme_quiet.h"

using namespace reader;
using ramp::Ramp;

namespace {

const InputEvent kDown{Button::Down, PressKind::Short};
const InputEvent kUp{Button::Up, PressKind::Short};
const InputEvent kConfirm{Button::Confirm, PressKind::Short};
const InputEvent kBack{Button::Back, PressKind::Short};
const InputEvent kLongConfirm{Button::Confirm, PressKind::Long};
const InputEvent kLongBack{Button::Back, PressKind::Long};

StubScreen makeStub() {
  // The middle row has no target: a placeholder row for a screen that does not
  // exist yet must be inert, not push whatever is next in the list.
  return StubScreen(ScreenId::Library, "LIBRARY",
                    {{"SETTINGS", ScreenId::Settings},
                     {"NOT BUILT YET", std::nullopt},
                     {"INPUT MONITOR", ScreenId::InputMonitor}});
}

int inkCount(const Framebuffer& fb) {
  int n = 0;
  for (int y = 0; y < fb.height(); ++y)
    for (int x = 0; x < fb.width(); ++x)
      if (!fb.getPixel(x, y)) ++n;
  return n;
}

bool rowIsWhite(const Framebuffer& fb, int y) {
  for (int x = 0; x < fb.width(); ++x)
    if (!fb.getPixel(x, y)) return false;
  return true;
}

}  // namespace

TEST_CASE("stub focus clamps at both ends rather than wrapping") {
  StubScreen s = makeStub();
  CHECK(s.focus() == 0);
  // Already at the top: no move, and no redraw to pay a panel refresh for.
  CHECK(s.onEvent(kUp).kind == Action::Kind::None);
  CHECK(s.focus() == 0);
  CHECK(s.onEvent(kDown).kind == Action::Kind::Redraw);
  CHECK(s.onEvent(kDown).kind == Action::Kind::Redraw);
  CHECK(s.focus() == 2);
  CHECK(s.onEvent(kDown).kind == Action::Kind::None);
  CHECK(s.focus() == 2);
  CHECK(s.onEvent(kUp).kind == Action::Kind::Redraw);
  CHECK(s.focus() == 1);
}

TEST_CASE("an empty stub has nothing to focus and moving does nothing") {
  StubScreen s(ScreenId::Settings, "SETTINGS", {});
  CHECK(s.focus() == -1);
  CHECK(s.onEvent(kDown).kind == Action::Kind::None);
  CHECK(s.onEvent(kUp).kind == Action::Kind::None);
  CHECK(s.focus() == -1);
}

TEST_CASE("back pops out of a stub") {
  StubScreen s = makeStub();
  CHECK(s.onEvent(kBack).kind == Action::Kind::Pop);
}

TEST_CASE("confirm on a row with a target pushes it") {
  StubScreen s = makeStub();
  Action a = s.onEvent(kConfirm);
  CHECK(a.kind == Action::Kind::Push);
  CHECK(a.target == ScreenId::Settings);
  s.onEvent(kDown);
  s.onEvent(kDown);
  REQUIRE(s.focus() == 2);
  a = s.onEvent(kConfirm);
  CHECK(a.kind == Action::Kind::Push);
  CHECK(a.target == ScreenId::InputMonitor);
}

TEST_CASE("confirm on a row with no target is inert") {
  StubScreen s = makeStub();
  s.onEvent(kDown);
  REQUIRE(s.focus() == 1);
  CHECK(s.onEvent(kConfirm).kind == Action::Kind::None);
  // And it did not move the focus on the way out.
  CHECK(s.focus() == 1);
}

TEST_CASE("a stub binds no hold, so its mask is empty") {
  StubScreen s = makeStub();
  CHECK(s.longPressable() == 0);
}

TEST_CASE("every screen takes the dithered path by default -- grayscale is opt-in") {
  // The default is the cheap path, so a screen gets the fast refresh by saying
  // nothing. That is the whole point of the inversion: the expensive path costs
  // three panel waveforms, and it should take a deliberate override to reach it.
  InputMonitorScreen m;
  CHECK(m.fidelity() == Fidelity::Dithered);
  StubScreen s = makeStub();
  CHECK(s.fidelity() == Fidelity::Dithered);
}

TEST_CASE("the input monitor's mask is Confirm only, and comes from its hint slots") {
  InputMonitorScreen m;
  CHECK(m.longPressable() == buttonBit(Button::Confirm));
  // The same array the theme reads for the ring: the affordance and the binding
  // cannot drift, because there is only one declaration.
  CHECK(m.longPressable() == hintHoldMask(m.vm().holds));
  CHECK(m.vm().holds == std::array<bool, 4>{false, true, false, false});
}

TEST_CASE("a hold on Confirm is logged as LONG, a press as SHORT") {
  InputMonitorScreen m;
  CHECK(m.onEvent(kLongConfirm).kind == Action::Kind::Redraw);
  REQUIRE(m.vm().lines.size() == 1);
  CHECK(m.vm().lines[0].find("CONFIRM") != std::string::npos);
  CHECK(m.vm().lines[0].find("LONG") != std::string::npos);
  CHECK(m.vm().lines[0].find("SHORT") == std::string::npos);

  CHECK(m.onEvent(kConfirm).kind == Action::Kind::Redraw);
  REQUIRE(m.vm().lines.size() == 2);
  // Newest first: the line just logged is at the top of the list.
  CHECK(m.vm().lines[0].find("SHORT") != std::string::npos);
  CHECK(m.vm().lines[1].find("LONG") != std::string::npos);
}

TEST_CASE("the log is capped, so a held button cannot grow it without bound") {
  InputMonitorScreen m;
  for (int i = 0; i < 20; ++i) m.onEvent(kDown);
  CHECK(m.vm().lines.size() == 8);
  // The cap drops the oldest, not the newest: the last press must be visible.
  m.onEvent(kLongConfirm);
  CHECK(m.vm().lines.size() == 8);
  CHECK(m.vm().lines[0].find("CONFIRM") != std::string::npos);
  CHECK(m.vm().lines[1].find("DOWN") != std::string::npos);
}

TEST_CASE("Back short-presses out of the monitor but a held Back is logged") {
  InputMonitorScreen m;
  CHECK(m.onEvent(kLongBack).kind == Action::Kind::Redraw);
  REQUIRE(m.vm().lines.size() == 1);
  CHECK(m.vm().lines[0].find("BACK") != std::string::npos);
  CHECK(m.vm().lines[0].find("LONG") != std::string::npos);
  // Only the short press leaves, which is what makes the hold observable at all.
  CHECK(m.onEvent(kBack).kind == Action::Kind::Pop);
  CHECK(m.vm().lines.size() == 1);
}

TEST_CASE("both provisional screens render inside the panel on both geometries") {
  Ramp ramp;
  QuietTheme theme;

  auto checkFrame = [&](const Screen& screen, int w, int h, Plane plane) {
    Framebuffer fb(w, h);
    screen.render(fb, ramp.fonts, theme, plane);
    const int ink = inkCount(fb);
    // Non-blank, and not a solid black rectangle either -- both would pass a
    // "something happened" check while being useless on the panel.
    CHECK(ink > 0);
    CHECK(ink < w * h);
    // Nothing drawn outside the framebuffer: the boards' box model keeps the
    // band's padding above the first ink and the hint bar's below the last, so
    // ink touching either extreme row means a run escaped its box.
    CHECK(rowIsWhite(fb, 0));
    CHECK(rowIsWhite(fb, h - 1));
  };

  StubScreen stub = makeStub();
  InputMonitorScreen monitor;
  for (int i = 0; i < 4; ++i) monitor.onEvent(kLongConfirm);

  SUBCASE("X4 480x800") {
    // The stub is Gray, so it is drawn three times, once per plane.
    for (Plane p : {Plane::Bw, Plane::Lsb, Plane::Msb}) checkFrame(stub, 480, 800, p);
    // The monitor is Mono: one thresholded pass is the whole frame.
    checkFrame(monitor, 480, 800, Plane::Bw);
  }
  SUBCASE("X3 528x792") {
    for (Plane p : {Plane::Bw, Plane::Lsb, Plane::Msb}) checkFrame(stub, 528, 792, p);
    checkFrame(monitor, 528, 792, Plane::Bw);
  }
}

TEST_CASE("a list longer than the panel stops at the hint bar instead of drawing under it") {
  Ramp ramp;
  QuietTheme theme;
  // A list that overflows by a wide margin, against an empty one. Everything at
  // or below the top of the hint bar has to be identical between the two: the
  // rows stop there. Where "there" is comes from the primitive that owns the
  // bar's box model, exactly as the theme gets it -- nothing here pins a height.
  // The bar's height does not depend on its labels, only on its type role and
  // its marks, so these stand in for both screens' bars.
  const Hint bar[4] = {{&icons::kBack, "BACK", false},
                       {&icons::kDot, "OPEN", false},
                       {&icons::kUp, "UP", false},
                       {&icons::kDown, "DOWN", false}};

  std::vector<StubScreen::Row> many;
  for (int i = 0; i < 60; ++i) many.push_back({"ROW " + std::to_string(i), std::nullopt});
  StubScreen full(ScreenId::Library, "LIBRARY", many);
  StubScreen empty(ScreenId::Library, "LIBRARY", {});

  for (int w : {480, 528}) {
    const int h = w == 480 ? 800 : 792;
    Framebuffer fFull(w, h), fEmpty(w, h);
    full.render(fFull, ramp.fonts, theme, Plane::Bw);
    empty.render(fEmpty, ramp.fonts, theme, Plane::Bw);

    const int barTop = h - hintBarHeight(ramp.fonts, bar);
    int intruding = 0, aboveDiff = 0;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x)
        if (fFull.getPixel(x, y) != fEmpty.getPixel(x, y)) {
          if (y >= barTop)
            ++intruding;
          else
            ++aboveDiff;
        }
    CHECK(intruding == 0);
    // ...and the rows really did draw above it, or the check above is vacuous.
    CHECK(aboveDiff > 0);
  }
}
