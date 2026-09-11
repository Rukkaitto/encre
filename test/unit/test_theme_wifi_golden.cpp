// THE V1.1 CONNECT FLOW, pinned per pixel at both panel geometries.
//
// BOTH GEOMETRIES, because a layout that fits one can clip the other and the X3
// is the dev device. The three overlays are centred panels, so the two differ
// in where the panel lands and how much veil surrounds it; the three
// full-screen lists differ by 48px of row width, which is where a long SSID
// elides.
//
// EVERY BOARDED STATE, because the states differ by things a structural test
// cannot see. The three failure shapes differ by ONE SENTENCE, and a sentence
// is exactly what moves a centred panel: they wrap to different heights, which
// moves the slabs and the centring with them. The two empty states replace a
// list with a centred block whose height is what its copy wraps to. Only a
// pixel catches any of it.
//
// AND THE TWO KEYBOARD LAYERS, which are NOT boards: SHIFT and #+= are the same
// 44 cells with different glyphs, so a board would be a third copy of one
// geometry. A golden is the right instrument for a state that differs only in
// what is drawn in the cells -- and it is the only thing that can prove the
// symbol layer reaches the punctuation a WPA2 passphrase may contain rather
// than drawing notdef boxes, which ink rows exactly like letters do.
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "golden.h"
#include "home_vm.h"
#include "ramp.h"
#include "reader/app.h"
#include "reader/framebuffer.h"
#include "reader/screen_home.h"
#include "reader/screen_wifi_connect.h"
#include "reader/screen_wifi_error.h"
#include "reader/screen_wifi_password.h"
#include "reader/screen_wifi_picker.h"
#include "reader/screen_wifi_settings.h"
#include "reader/screens.h"
#include "reader/theme_quiet.h"

using namespace reader;

namespace {

// The flow reached the way the simulator reaches it: an App rooted at Home with
// WifiSettings pushed onto it, which is what the three overlay boards veil. A
// struct rather than a helper with accessors, for library_app.h's reason.
struct WifiApp {
  DemoScreenFactory factory;
  App app;

  WifiApp()
      : app(std::make_unique<HomeScreen>(demoHomeVm(), demoHomeTargets()), factory) {
    factory.setWifiDemo();
    factory.setWifiPickerVisibleRows(7);
  }
};

}  // namespace

TEST_CASE("the connect flow's screens declare Mono, so a golden is one 1-bit frame") {
  // Asserted rather than assumed: these goldens name a plane, and if a screen's
  // declared path ever moved the golden must stop matching rather than quietly
  // keep pinning a path nothing paints. Every one of the six is chrome.
  CHECK(WifiSettingsScreen(SavedNetworks{}, nullptr).fidelity() == Fidelity::Mono);
  CHECK(WifiPickerScreen({}, 7).fidelity() == Fidelity::Mono);
  CHECK(WifiPasswordScreen("N").fidelity() == Fidelity::Mono);
  CHECK(WifiConnectScreen("N").fidelity() == Fidelity::Mono);
  CHECK(WifiErrorScreen("N", JoinFailure::BadPassword).fidelity() == Fidelity::Mono);
  CHECK(WifiNetworkActionsScreen({"N", true}).fidelity() == Fidelity::Mono);
}

TEST_CASE("QuietTheme renders the connect flow to golden at both geometries") {
  ramp::Ramp ramp;
  QuietTheme theme;

  // `prime` runs before the push, `walk` after it -- the two things a state
  // needs, and keeping them separate is what lets the scrolled picker press
  // its way to the board's window rather than having one assigned.
  auto renderOne = [&](int w, int h, const std::string& name,
                       void (*prime)(WifiApp&), void (*walk)(WifiApp&)) {
    WifiApp a;
    if (prime != nullptr) prime(a);
    REQUIRE(a.app.pushScreen(ScreenId::WifiSettings));
    if (walk != nullptr) walk(a);
    Framebuffer fb(w, h);
    // App::render, NEVER top().render -- an overlay rendered on its own is a
    // panel floating on white, and nothing on the desktop catches it because
    // every golden goes through App::render. It has happened once.
    a.app.render(fb, ramp.fonts, theme, Plane::Bw);
    golden::checkGolden(fb, name);
  };

  struct Case {
    const char* name;
    void (*prime)(WifiApp&);
    void (*walk)(WifiApp&);
  };

  static const Case kCases[] = {
      {"wifi_settings", nullptr, nullptr},
      // THE EMPTY VARIANT IS THE SAME ScreenId with a different list, which is
      // what makes it a variant rather than a second screen.
      {"wifi_settings_empty",
       [](WifiApp& a) { a.factory.setWifiNetworks(SavedNetworks{}); }, nullptr},
      {"wifi_network_actions", nullptr,
       [](WifiApp& a) { REQUIRE(a.app.pushScreen(ScreenId::WifiNetworkActions)); }},
      {"wifi_picker", nullptr,
       [](WifiApp& a) { REQUIRE(a.app.pushScreen(ScreenId::WifiPicker)); }},
      {"wifi_picker_empty", [](WifiApp& a) { a.factory.setWifiScan({}); },
       [](WifiApp& a) { REQUIRE(a.app.pushScreen(ScreenId::WifiPicker)); }},
      {"wifi_picker_scrolled",
       [](WifiApp& a) { a.factory.setWifiScan(demoWifiScanLong()); },
       [](WifiApp& a) {
         REQUIRE(a.app.pushScreen(ScreenId::WifiPicker));
         // PAST THE WINDOW AND BACK, which is LibraryScrolled's own recipe:
         // arriving from above lands the focus on the window's bottom edge, so
         // the board's window needs going past it and coming back.
         for (int i = 0; i < 11; ++i) a.app.dispatch({Button::Down, PressKind::Short});
         for (int i = 0; i < 4; ++i) a.app.dispatch({Button::Up, PressKind::Short});
       }},
      {"wifi_password",
       [](WifiApp& a) { a.factory.setWifiTarget("PENDRAGON", "correcthor"); },
       [](WifiApp& a) {
         REQUIRE(a.app.pushScreen(ScreenId::WifiPicker));
         REQUIRE(a.app.pushScreen(ScreenId::WifiPassword));
       }},
      // THE CONNECTING DIALOG AND THE FAILURE SHAPES SIT ON WifiSettings, not
      // on the picker: Action::replace collapses the join stack, which is what
      // makes one veiled parent truthful for both entry paths.
      {"wifi_connect", [](WifiApp& a) { a.factory.setWifiTarget("HOME"); },
       [](WifiApp& a) { REQUIRE(a.app.pushScreen(ScreenId::WifiConnect)); }},
      {"wifi_error",
       [](WifiApp& a) { a.factory.setWifiFailure(JoinFailure::BadPassword); },
       [](WifiApp& a) { REQUIRE(a.app.pushScreen(ScreenId::WifiError)); }},
      {"wifi_error_not_found",
       [](WifiApp& a) { a.factory.setWifiFailure(JoinFailure::NotFound); },
       [](WifiApp& a) { REQUIRE(a.app.pushScreen(ScreenId::WifiError)); }},
      {"wifi_error_failed",
       [](WifiApp& a) { a.factory.setWifiFailure(JoinFailure::Incomplete); },
       [](WifiApp& a) { REQUIRE(a.app.pushScreen(ScreenId::WifiError)); }},
  };

  for (const Case& c : kCases) {
    CAPTURE(c.name);
    SUBCASE(c.name) {
      renderOne(480, 800, c.name, c.prime, c.walk);
      renderOne(528, 792, std::string(c.name) + "_x3", c.prime, c.walk);
    }
  }
}

TEST_CASE("the keyboard's three layers are pinned, because a notdef box inks like a letter") {
  // THE SYMBOL LAYER IS THE ONE THAT MATTERS. A WPA2 passphrase may contain any
  // printable ASCII, and this is the only thing in the suite that can tell a
  // rendered `~` from a rendered notdef box -- ink that spells nothing inks
  // rows exactly like ink that does, so no structural assertion can see it.
  // Home shipped precisely that mistake once and it took a PNG to find.
  ramp::Ramp ramp;
  QuietTheme theme;

  auto renderLayer = [&](WifiPasswordScreen::Layer layer, const std::string& name) {
    WifiPasswordScreen screen("PENDRAGON");
    screen.setEntered("correcthor");
    screen.setLayer(layer);
    Framebuffer fb(480, 800);
    // A whole screen rather than an overlay, so it renders alone.
    screen.render(fb, ramp.fonts, theme, Plane::Bw);
    golden::checkGolden(fb, name);
  };

  SUBCASE("upper") { renderLayer(WifiPasswordScreen::Layer::Upper, "wifi_password_upper"); }
  SUBCASE("symbols") { renderLayer(WifiPasswordScreen::Layer::Symbols, "wifi_password_symbols"); }
}

TEST_CASE("the connecting dialog's READY step is pinned") {
  // The board draws CONNECTING...; READY is the other half of the same panel
  // and is what a successful join actually puts on the glass. Not a board,
  // because the two differ by one word -- but a golden, because that word
  // changes the caption's measured width and the panel is centred on it.
  ramp::Ramp ramp;
  QuietTheme theme;
  WifiApp a;
  a.factory.setWifiTarget("HOME");
  REQUIRE(a.app.pushScreen(ScreenId::WifiSettings));
  REQUIRE(a.app.pushScreen(ScreenId::WifiConnect));
  auto* dialog = static_cast<WifiConnectScreen*>(&a.app.top());
  CHECK(dialog->markReady());
  // A second call changes nothing, so it owes no repaint -- the bool the shell
  // reads to decide whether a ~520 ms paint is owed.
  CHECK_FALSE(dialog->markReady());
  Framebuffer fb(480, 800);
  a.app.render(fb, ramp.fonts, theme, Plane::Bw);
  golden::checkGolden(fb, "wifi_connect_ready");
}
