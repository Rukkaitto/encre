// The built screens that had no golden.
//
// Seven of the seventeen screens this firmware renders were checked by unit tests and
// by `make compare` and by nothing that looked at a pixel in CI. Four of those seven
// are the reader family, which matters more than the count: the emphasis work
// restructured the shared text path -- `drawRun` became an F26 core, `PageBuilder`'s
// emit loop grew per-kind columns, per-block tracking and whole-row spacing -- and a
// screen with no golden cannot report a regression in any of it. The two styled
// specimens live in test_theme_reader_golden.cpp beside `reader_quiet`; the rest are
// here.
//
// EACH SETUP IS THE SIMULATOR'S, deliberately duplicated rather than shared. The sim
// reaches Settings and the scrolled Library BY PRESSING -- two Downs and a Confirm,
// thirteen Downs and an Up -- and that is load-bearing: it pins the navigation as well
// as the paint, and getting it wrong once showed up immediately as a Library in the
// comparison sheet rather than as a subtly wrong Settings. A golden that assigned the
// state directly would pin the same pixels and defend less.
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "golden.h"
#include "ramp.h"
#include "reader_fixture.h"
#include "reader/app.h"
#include "reader/framebuffer.h"
#include "reader/screen_contents.h"
#include "reader/screen_home.h"
#include "reader/screen_reader.h"
#include "reader/screens.h"
#include "reader/settings.h"
#include "reader/theme_quiet.h"

namespace {

using readerfix::Body;
using readerfix::Italic;

// The simulator's own entry sequence. Home's focus starts BEFORE its menu -- on the
// CONTINUE block -- so the first Down reaches LIBRARY and the Confirm enters it.
std::vector<reader::InputEvent> libraryEntry(int downsInLibrary = 1) {
  using reader::Button;
  using reader::PressKind;
  std::vector<reader::InputEvent> out{{Button::Down, PressKind::Short},
                                      {Button::Confirm, PressKind::Short}};
  for (int i = 0; i < downsInLibrary; ++i) out.push_back({Button::Down, PressKind::Short});
  return out;
}

// A factory primed the way the simulator primes it for the Home-rooted journeys.
void primeForJourney(reader::DemoScreenFactory& factory, reader::QuietTheme& theme,
                     const reader::FontSet& fonts, int h, bool scrolledLibrary) {
  factory.setLibraryVisibleRows(theme.libraryVisibleRows(h, fonts));
  // THE SCROLLED LIBRARY NEEDS A LONGER LIST, not a different screen: the rail's thumb
  // is visible/total, so a seven-item list cannot produce a rail at all.
  if (scrolledLibrary) factory.setLibraryItems(reader::demoLibraryScrolledItems());
  int listH = 0, rowH = 0, headerH = 0;
  theme.settingsMetrics(h, fonts, listH, rowH, headerH);
  factory.setSettingsMetrics(listH, rowH, headerH);
  // The BOARD's values and not the defaults: design/Settings.dc.html states `10 MIN`
  // and `EVERY 15 PAGES`, a configured state rather than a fresh device's.
  reader::Settings shown;
  shown.sleepAfterMs = 10u * 60u * 1000u;
  shown.fullRefreshEvery = 15;
  shown.fullOnTransition = true;
  factory.setSettings(shown);
}

}  // namespace

// --- The reader's menu ---------------------------------------------------------
//
// design/ReaderMenu.dc.html. AN OVERLAY NEEDS ITS PARENT, so this goes through an App
// rooted at the Reader: App::render walks down to the topmost non-overlay, paints it,
// then paints each overlay above. Rendering `top().render` alone is the mistake that
// paints a panel floating on white -- and nothing on the desktop catches it, because
// every other path goes through App::render.
TEST_CASE("QuietTheme renders the reader menu over its page, to golden") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  Body body;
  Italic italic;

  auto renderOne = [&](int w, int h, const std::string& name) {
    reader::PageMetrics m;
    theme.readerMetrics(w, h, ramp.fonts, body.face, m);
    m.italic = &italic.face;
    reader::DemoScreenFactory factory;
    factory.setReaderBody(&body.face);
    factory.setReaderItalic(&italic.face);
    factory.setReaderMetrics(m);
    factory.setReaderDemo();
    factory.setContentsDemo();  // the menu's header comes from the same catalogue
    std::unique_ptr<reader::Screen> page = factory.create(reader::ScreenId::Reader);
    REQUIRE(page != nullptr);
    static_cast<reader::ReaderScreen*>(page.get())->completeIndex();
    reader::App app(std::move(page), factory);
    REQUIRE(app.pushScreen(reader::ScreenId::ReaderMenu));
    // THE MENU IS ONE WAVEFORM, which is why this is checkGolden and not the
    // grayscale sibling: the panel under the veil is hard-thresholded for these
    // frames so the whole overlay paints in one pass. The trade is stated on the
    // board -- the menu is chrome, and the page is the thing that wanted four levels.
    REQUIRE(app.top().fidelity() != reader::Fidelity::Grayscale);
    reader::Framebuffer fb(w, h);
    app.render(fb, ramp.fonts, theme, reader::Plane::Bw);
    golden::checkGolden(fb, name);
  };

  SUBCASE("X4 480x800") { renderOne(480, 800, "reader_menu"); }
  SUBCASE("X3 528x792") { renderOne(528, 792, "reader_menu_x3"); }
}

// --- The chapter list ----------------------------------------------------------
//
// design/Contents.dc.html. A full screen, so it renders on its own -- and its row
// count comes from the theme, as the Library's does, because a list told nothing
// renders empty.
TEST_CASE("QuietTheme renders the chapter list to golden") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;

  auto renderOne = [&](int w, int h, const std::string& name) {
    reader::DemoScreenFactory factory;
    // ASKED FOR, as the Reader's demo is: the factory refuses a Contents that nothing
    // primed, so a device that failed to read a real one shows no list rather than
    // the board's.
    factory.setContentsDemo();
    factory.setContentsVisibleRows(theme.contentsVisibleRows(h, ramp.fonts));
    std::unique_ptr<reader::Screen> scr = factory.create(reader::ScreenId::Contents);
    REQUIRE(scr != nullptr);
    reader::Framebuffer fb(w, h);
    scr->render(fb, ramp.fonts, theme, reader::Plane::Bw);
    golden::checkGolden(fb, name);
  };

  SUBCASE("X4 480x800") { renderOne(480, 800, "contents"); }
  SUBCASE("X3 528x792") { renderOne(528, 792, "contents_x3"); }
}

// --- Settings, reached by pressing ---------------------------------------------
//
// design/Settings.dc.html.
TEST_CASE("QuietTheme renders Settings to golden, reached by pressing") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;

  auto renderOne = [&](int w, int h, const std::string& name) {
    reader::DemoScreenFactory factory;
    primeForJourney(factory, theme, ramp.fonts, h, /*scrolledLibrary=*/false);
    reader::App app(
        std::make_unique<reader::HomeScreen>(reader::demoHomeVm(), reader::demoHomeTargets()),
        factory);
    // TWO Downs then Confirm: Home's focus starts on the CONTINUE block, so the
    // first Down reaches LIBRARY and the second SETTINGS.
    app.dispatch({reader::Button::Down, reader::PressKind::Short});
    app.dispatch({reader::Button::Down, reader::PressKind::Short});
    app.dispatch({reader::Button::Confirm, reader::PressKind::Short});
    REQUIRE(app.top().id() == reader::ScreenId::Settings);
    reader::Framebuffer fb(w, h);
    app.render(fb, ramp.fonts, theme, reader::Plane::Bw);
    golden::checkGolden(fb, name);
  };

  SUBCASE("X4 480x800") { renderOne(480, 800, "settings"); }
  SUBCASE("X3 528x792") { renderOne(528, 792, "settings_x3"); }
}

// --- The scrolled Library ------------------------------------------------------
//
// design/LibraryScrolled.dc.html. THE ONLY STATE IN WHICH THE RAIL IS COMPARED
// AGAINST A PIXEL: a list that fits produces no rail at all, so until this state
// existed the rail was checked by unit tests and by nothing that looked.
TEST_CASE("QuietTheme renders the scrolled Library to golden, reached by pressing") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;

  auto renderOne = [&](int w, int h, const std::string& name) {
    reader::DemoScreenFactory factory;
    primeForJourney(factory, theme, ramp.fonts, h, /*scrolledLibrary=*/true);
    reader::App app(
        std::make_unique<reader::HomeScreen>(reader::demoHomeVm(), reader::demoHomeTargets()),
        factory);
    // THIRTEEN DOWNS AND AN UP, not twelve downs -- and the difference is the point.
    // ScrollWindow scrolls only as far as it must to keep the focus visible, so
    // arriving at row 12 from above lands the focus on the window's BOTTOM edge. The
    // board draws the focus sixth of seven, which is the state after going one
    // further and stepping back inside a window that has already moved.
    for (const reader::InputEvent& ev : libraryEntry(13)) app.dispatch(ev);
    app.dispatch({reader::Button::Up, reader::PressKind::Short});
    REQUIRE(app.top().id() == reader::ScreenId::Library);
    reader::Framebuffer fb(w, h);
    app.render(fb, ramp.fonts, theme, reader::Plane::Bw);
    golden::checkGolden(fb, name);
  };

  SUBCASE("X4 480x800") { renderOne(480, 800, "library_scrolled"); }
  SUBCASE("X3 528x792") { renderOne(528, 792, "library_scrolled_x3"); }
}

// --- Home with an empty card ---------------------------------------------------
//
// design/HomeEmpty.dc.html. A different ROOT rather than a different navigation:
// there is no journey that reaches it, because the CARD is what decides.
TEST_CASE("QuietTheme renders Home with an empty card to golden") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;

  auto renderOne = [&](int w, int h, const std::string& name) {
    reader::DemoScreenFactory factory;
    primeForJourney(factory, theme, ramp.fonts, h, /*scrolledLibrary=*/false);
    reader::App app(std::make_unique<reader::HomeScreen>(reader::demoHomeEmptyVm(),
                                                        reader::demoHomeTargets()),
                    factory);
    reader::Framebuffer fb(w, h);
    app.render(fb, ramp.fonts, theme, reader::Plane::Bw);
    golden::checkGolden(fb, name);
  };

  SUBCASE("X4 480x800") { renderOne(480, 800, "home_empty"); }
  SUBCASE("X3 528x792") { renderOne(528, 792, "home_empty_x3"); }
}
