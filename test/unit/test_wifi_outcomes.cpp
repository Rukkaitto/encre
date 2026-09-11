// THE CONNECT FLOW'S OUTCOME CONTRACT, driven through a real App::dispatch.
//
// EVERY ONE OF THESE FIVE SCREENS SHIPPED WITH A GETTER THE SHELL COULD NOT
// CALL. `joinChosen()`, `cancelled()`, `chosen()`, `chosenSsid()` and
// `forgetChosen()` each latched a result and returned `Action::pop()` -- and
// dispatch's Pop is `stack_.pop_back()`, which DESTROYS the screen. Four of the
// five headers said in as many words that the shell reads the value after the
// pop.
//
// NOTHING IN THE SUITE COULD SEE IT, and the reason is the shape worth keeping:
// every existing test called `onEvent` on a bare screen and then read the
// getter off the object it still held. That is not what the shell does and it
// is not what App does -- it skips the dispatch entirely, which is the only
// place the pop happens. So the tests here go through an App, and the
// assertion that matters is `app.depth()` and `app.top().id()` AFTER the press:
// the outcome is only readable if the screen is still standing.
//
// The picker is the same defect wearing a different hat: it never popped, so
// its getters were readable, but the Action it returned was `Action::redraw()`
// -- a side channel that spent a ~520 ms repaint of an identical frame and gave
// the shell nothing to test. `wifiRequested()` is the signal now.
#include <memory>
#include <string>

#include "doctest.h"
#include "home_vm.h"
#include "reader/app.h"
#include "reader/screen_home.h"
#include "reader/screen_wifi_connect.h"
#include "reader/screen_wifi_error.h"
#include "reader/screen_wifi_network_actions.h"
#include "reader/screen_wifi_password.h"
#include "reader/screen_wifi_picker.h"
#include "reader/screen_wifi_settings.h"
#include "reader/screens.h"

using namespace reader;

namespace {

struct Flow {
  DemoScreenFactory factory;
  App app;
  Flow() : app(std::make_unique<HomeScreen>(demoHomeVm(), demoHomeTargets()), factory) {
    factory.setWifiDemo();
    factory.setWifiPickerVisibleRows(7);
  }
};

const InputEvent kConfirm{Button::Confirm, PressKind::Short};
const InputEvent kHoldBack{Button::Back, PressKind::Long};
const InputEvent kBack{Button::Back, PressKind::Short};
const InputEvent kDown{Button::Down, PressKind::Short};

}  // namespace

TEST_CASE("a Wi-Fi outcome is readable after the dispatch that latched it") {
  // THE REGRESSION TEST FOR THE WHOLE CLASS. One SUBCASE per screen, each
  // asserting the same three things: the latch fired, the stack did NOT move,
  // and the getter reads -- in that order, because the middle one is what the
  // other two rest on.
  Flow f;
  REQUIRE(f.app.pushScreen(ScreenId::WifiSettings));

  SUBCASE("the picker's chosen network") {
    REQUIRE(f.app.pushScreen(ScreenId::WifiPicker));
    const int depth = f.app.depth();
    // Cleared so the check below is about the PRESS and not about the push
    // that got us here, which legitimately dirtied the frame. Measured
    // without it, the assertion reads the push and passes whatever the press
    // does -- a mutation telling you about your input before your test.
    f.app.clearDirty();
    f.app.dispatch(kConfirm);
    CHECK(f.app.wifiRequested());
    REQUIRE(f.app.depth() == depth);
    REQUIRE(f.app.top().id() == ScreenId::WifiPicker);
    auto* p = static_cast<WifiPickerScreen*>(&f.app.top());
    CHECK_FALSE(p->chosenSsid().empty());
    CHECK_FALSE(p->rescanChosen());
    // A CHOICE COSTS NO REPAINT. It used to return Action::redraw(), which
    // spent a full ~520 ms frame drawing a screen that had not changed -- and
    // spent it immediately in front of the push that replaces that screen.
    CHECK_FALSE(f.app.dirty());
  }

  SUBCASE("the picker's Rescan row") {
    REQUIRE(f.app.pushScreen(ScreenId::WifiPicker));
    auto* p = static_cast<WifiPickerScreen*>(&f.app.top());
    // Past the last network onto the Rescan row, which is one below the list.
    const int rows = static_cast<int>(p->vm().rows.size());
    REQUIRE(p->setFocus(rows));
    const int depth = f.app.depth();
    f.app.dispatch(kConfirm);
    CHECK(f.app.wifiRequested());
    REQUIRE(f.app.depth() == depth);
    CHECK(p->rescanChosen());
    CHECK(p->chosenSsid().empty());
  }

  SUBCASE("the keyboard's JOIN, and the passphrase it was holding") {
    f.factory.setWifiTarget("HOME", "correcthorse");
    REQUIRE(f.app.pushScreen(ScreenId::WifiPassword));
    auto* kb = static_cast<WifiPasswordScreen*>(&f.app.top());
    // The JOIN cell is the last of the four function keys.
    REQUIRE(kb->setFocus(static_cast<int>(kb->vm().cells.size()) - 1));
    const int depth = f.app.depth();
    f.app.dispatch(kConfirm);
    CHECK(f.app.wifiRequested());
    // THE SHARPEST CASE: the pop destroyed the object holding the passphrase,
    // so this getter was not merely unreadable, it was the one the shell needs
    // most.
    REQUIRE(f.app.depth() == depth);
    REQUIRE(f.app.top().id() == ScreenId::WifiPassword);
    CHECK(kb->joinChosen());
    CHECK(kb->entered() == "correcthorse");
  }

  SUBCASE("the keyboard's held Back") {
    f.factory.setWifiTarget("HOME");
    REQUIRE(f.app.pushScreen(ScreenId::WifiPassword));
    const int depth = f.app.depth();
    f.app.dispatch(kHoldBack);
    CHECK(f.app.wifiRequested());
    REQUIRE(f.app.depth() == depth);
    auto* kb = static_cast<WifiPasswordScreen*>(&f.app.top());
    CHECK(kb->cancelled());
    CHECK_FALSE(kb->joinChosen());
  }

  SUBCASE("the connecting dialog's cancel") {
    f.factory.setWifiTarget("HOME");
    REQUIRE(f.app.pushScreen(ScreenId::WifiConnect));
    const int depth = f.app.depth();
    f.app.dispatch(kBack);
    CHECK(f.app.wifiRequested());
    // NOT NAVIGATION: a join is in flight, so the radio has to be told before
    // the screen goes.
    REQUIRE(f.app.depth() == depth);
    CHECK(static_cast<WifiConnectScreen*>(&f.app.top())->cancelled());
  }

  SUBCASE("the error panel's three slabs") {
    f.factory.setWifiFailure(JoinFailure::BadPassword);
    REQUIRE(f.app.pushScreen(ScreenId::WifiError));
    auto* e = static_cast<WifiErrorScreen*>(&f.app.top());
    // BadPassword is the only shape with EDIT PASSWORD, so it is the only one
    // with three -- and it is row 0.
    REQUIRE(e->vm().actions.size() == 3);
    const int depth = f.app.depth();
    f.app.dispatch(kConfirm);
    CHECK(f.app.wifiRequested());
    REQUIRE(f.app.depth() == depth);
    CHECK(e->chosen() == WifiErrorScreen::Chosen::EditPassword);

    // And the slab below it, on the same still-standing screen.
    f.app.clearWifiRequest();
    f.app.dispatch(kDown);
    f.app.dispatch(kConfirm);
    CHECK(f.app.wifiRequested());
    REQUIRE(f.app.depth() == depth);
    CHECK(e->chosen() == WifiErrorScreen::Chosen::TryAgain);
  }

  SUBCASE("the error panel's Back is Cancel") {
    f.factory.setWifiFailure(JoinFailure::NotFound);
    REQUIRE(f.app.pushScreen(ScreenId::WifiError));
    const int depth = f.app.depth();
    f.app.dispatch(kBack);
    CHECK(f.app.wifiRequested());
    REQUIRE(f.app.depth() == depth);
    CHECK(static_cast<WifiErrorScreen*>(&f.app.top())->chosen() ==
          WifiErrorScreen::Chosen::Cancel);
  }

  SUBCASE("the actions overlay's FORGET") {
    REQUIRE(f.app.pushScreen(ScreenId::WifiNetworkActions));
    const int depth = f.app.depth();
    f.app.dispatch(kConfirm);
    CHECK(f.app.wifiRequested());
    REQUIRE(f.app.depth() == depth);
    CHECK(static_cast<WifiNetworkActionsScreen*>(&f.app.top())->forgetChosen());
  }
}

TEST_CASE("a Wi-Fi screen with no outcome pops itself, and latches nothing") {
  // THE OTHER HALF OF THE RULE, and it is what keeps the latch from becoming
  // machinery bought for nothing: a screen latches when the shell has WORK to
  // do and pops itself when it has not. A plain Back off the picker is
  // navigation and the shell owes it nothing.
  //
  // Without this, "latch everything" and "latch the outcomes" look identical,
  // and the first quietly routes every Back in the flow through the shell.
  Flow f;
  REQUIRE(f.app.pushScreen(ScreenId::WifiSettings));

  SUBCASE("Back off the picker") {
    REQUIRE(f.app.pushScreen(ScreenId::WifiPicker));
    const int depth = f.app.depth();
    f.app.dispatch(kBack);
    CHECK_FALSE(f.app.wifiRequested());
    CHECK(f.app.depth() == depth - 1);
    CHECK(f.app.top().id() == ScreenId::WifiSettings);
  }

  SUBCASE("Back off the actions overlay") {
    REQUIRE(f.app.pushScreen(ScreenId::WifiNetworkActions));
    const int depth = f.app.depth();
    f.app.dispatch(kBack);
    CHECK_FALSE(f.app.wifiRequested());
    CHECK(f.app.depth() == depth - 1);
  }
}

TEST_CASE("the Wi-Fi latch is a pull latch, and clearing it is the shell's") {
  // Retry, Open, Finish and Delete all work this way, and the reason is in
  // each of their headers: the shell clears FIRST, so an attempt that fails
  // does not re-fire on every pass forever.
  Flow f;
  REQUIRE(f.app.pushScreen(ScreenId::WifiSettings));
  REQUIRE(f.app.pushScreen(ScreenId::WifiNetworkActions));
  CHECK_FALSE(f.app.wifiRequested());
  f.app.dispatch(kConfirm);
  CHECK(f.app.wifiRequested());
  // It STAYS set until the shell takes it -- a latch nothing consumed is a
  // request nobody answered, not a request that expired.
  CHECK(f.app.wifiRequested());
  f.app.clearWifiRequest();
  CHECK_FALSE(f.app.wifiRequested());
}
