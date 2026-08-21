#include "doctest.h"
#include "reader/screen_home.h"
#include "reader/screens.h"

using namespace reader;

TEST_CASE("the demo catalogue can reach the input monitor from Home") {
  DemoScreenFactory f;
  App app(std::make_unique<HomeScreen>(demoHomeVm(), demoHomeTargets()), f);
  const InputEvent down{Button::Down, PressKind::Short};
  const InputEvent confirm{Button::Confirm, PressKind::Short};
  const InputEvent back{Button::Back, PressKind::Short};

  app.dispatch(down);     // focus LIBRARY
  app.dispatch(down);     // focus SETTINGS
  app.dispatch(confirm);  // push Settings
  REQUIRE(app.top().id() == ScreenId::Settings);
  app.dispatch(confirm);  // its first row is INPUT MONITOR
  REQUIRE(app.top().id() == ScreenId::InputMonitor);
  CHECK(app.top().fidelity() == Fidelity::Dithered);
  // Confirm carries the hold here, and the ring on that slot is the same array.
  CHECK(app.top().longPressable() == buttonBit(Button::Confirm));

  app.dispatch(back);
  CHECK(app.top().id() == ScreenId::Settings);
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
