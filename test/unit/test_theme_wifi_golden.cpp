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
      // THE SCANNING STATE, WHICH IS NOT A BOARD AND NEEDS A GOLDEN -- the
      // keyboard layers' argument. design/WifiPickerEmpty.dc.html declines to
      // board it deliberately, because drawStatusBar's box is already
      // specified by LibraryOpening and SleepWaking and a third board would be
      // a third copy of it.
      //
      // SO NOTHING HAD EVER RENDERED IT, which is exactly how it shipped
      // drawing the "scan found nothing" copy UNDER a bar saying SCANNING:
      // two states at once, and no instrument in the repo that put both on a
      // frame together. A mutation pass flagged the branch as unexercised
      // before a finger found it.
      {"wifi_picker_scanning", [](WifiApp& a) { a.factory.setWifiScan({}); },
       [](WifiApp& a) {
         REQUIRE(a.app.pushScreen(ScreenId::WifiPicker));
         auto* p = static_cast<WifiPickerScreen*>(&a.app.top());
         REQUIRE(p->setScanning(true));
       }},
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
      // THE FOURTH SHAPE, WHOSE JOIN WORKED (#162). It is the only one of the
      // four with a single slab, so it is also the only render that pins the
      // one-slab hint bar -- `OK` in the Confirm slot and two 36px empty ones
      // where UP and DOWN are on the other three. Measuring them as nothing
      // would draw CANCEL and OK in the wrong places, which is a defect no
      // structural assertion in this file can see.
      {"wifi_error_list_full",
       [](WifiApp& a) { a.factory.setWifiFailure(JoinFailure::ListFull); },
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

