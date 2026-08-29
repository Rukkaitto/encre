#include "doctest.h"
#include "reader/screen_home.h"
#include "reader/screens.h"

using namespace reader;

TEST_CASE("the demo catalogue reaches Settings from Home, and Back unwinds") {
  // This used to walk on into the Input Monitor, which 2C-3 deleted with
  // StubScreen -- it was reachable only from the stub's first row and no board
  // ever listed it. What is left is the navigation that survives: Home's SETTINGS
  // row opens the real screen, and Back unwinds to a root that cannot be popped.
  DemoScreenFactory f;
  App app(std::make_unique<HomeScreen>(demoHomeVm(), demoHomeTargets()), f);
  const InputEvent down{Button::Down, PressKind::Short};
  const InputEvent confirm{Button::Confirm, PressKind::Short};
  const InputEvent back{Button::Back, PressKind::Short};

  app.dispatch(down);     // focus LIBRARY -- Home's focus starts before its menu
  app.dispatch(down);     // focus SETTINGS
  app.dispatch(confirm);  // push Settings
  REQUIRE(app.top().id() == ScreenId::Settings);
  CHECK(app.top().fidelity() == Fidelity::Mono);
  // Nothing here promises a hold: every focusable row edits in place, so the hint
  // bar has no ring and longPressable must agree with it.
  CHECK(app.top().longPressable() == 0);

  app.dispatch(back);
  CHECK(app.top().id() == ScreenId::Home);
  CHECK(app.depth() == 1);
  app.dispatch(back);  // Home's Back is inert; the root must survive
  CHECK(app.depth() == 1);
}

TEST_CASE("Home is never rebuilt by the factory") {
  // Popping back to Home must return the ORIGINAL screen with its focus, not a
  // fresh one -- which is why create(Home) is null.
  DemoScreenFactory f;
  CHECK(f.create(ScreenId::Home) == nullptr);
}

TEST_CASE("requestRepaint dirties the app without making it a transition") {
  DemoScreenFactory f;
  App app(std::make_unique<HomeScreen>(demoHomeVm(), demoHomeTargets()), f);
  app.clearDirty();
  REQUIRE(app.dirty() == false);
  app.requestRepaint();
  CHECK(app.dirty() == true);
  // NOT a transition. A charge state appearing is not a screen change, and
  // kFullOnTransition would spend the 693 ms GC waveform on it instead of the
  // 389 ms DU -- a flash the user did not ask for.
  CHECK(app.transition() == false);
  // And it must not move the stack or the focus.
  CHECK(app.depth() == 1);
  CHECK(app.top().id() == ScreenId::Home);
}
