// THE FOCUS ROUND TRIP, FOR EVERY SCREEN THERE IS.
//
// A wake stores Screen::focus() and hands it back through Screen::setFocus(), so
// the two have to be inverse on every screen or the user wakes somewhere they
// never were. That is one rule, and it kept being implemented one screen at a
// time: Library got it in 2C-2, Home only after "why does the Library come back
// where I left it and Home does not", and Settings only after the same question
// again. Each time, the screens that had not been done reported a focus, dropped
// the restored one on the base class's no-op, and said nothing about it -- a
// silent wrong-place-on-wake rather than a failure.
//
// So this walks the CATALOGUE rather than a screen. A new screen is covered the
// day it is added, and one that reports a focus it cannot accept back fails here
// instead of on someone's device.
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "home_vm.h"
#include "reader/app.h"
#include "reader/screen_home.h"
#include "golden.h"
#include "ramp.h"
#include "reader/layout.h"
#include "reader/scalablefont.h"
#include "reader/screens.h"
#include "reader/theme_quiet.h"

using namespace reader;

namespace {

// EVERY ScreenId, and the size assertion below is what keeps it every. An added
// screen changes the count and fails here, which is the prompt to add its row --
// there is no -Wswitch to lean on over an array.
constexpr ScreenId kAllScreens[] = {
    ScreenId::Home,     ScreenId::Library,      ScreenId::ItemActions, ScreenId::DeleteConfirm,
    ScreenId::BookDetails, ScreenId::Settings,  ScreenId::Sleep,       ScreenId::Reader,
    ScreenId::ReaderMenu,  ScreenId::Contents,  ScreenId::SdMissing,
    ScreenId::Typography,  ScreenId::Peek,     ScreenId::BookEnd,
    ScreenId::BookError,   ScreenId::BatteryEmpty,
    ScreenId::WifiSettings, ScreenId::WifiPicker, ScreenId::WifiPassword,
    ScreenId::WifiConnect,  ScreenId::WifiError,  ScreenId::WifiNetworkActions,
};
// NAMES THE SENTINEL, so an append cannot satisfy it unchanged. It used to name the
// last member by hand -- `ScreenId::Peek + 1`, then `ScreenId::BookEnd + 1` -- and
// both times an append left both sides equal and the guard that exists to force a
// new screen into kAllScreens said nothing. That was #42.
//
// AND THE FIX HELD ON ITS FIRST REAL TEST. BatteryEmpty and BookError were appended
// on two branches at once, and merging them failed this assert rather than sliding
// past it -- which is what every earlier append did while this line named a member.
static_assert(sizeof(kAllScreens) / sizeof(kAllScreens[0]) ==
                  static_cast<size_t>(ScreenId::Count),
              "a ScreenId was added or removed; give it a row in kAllScreens");

// One screen, plus whatever has to outlive it. The three screens built over a
// Library hold a REFERENCE to it, so the Library cannot be a temporary -- and it
// must be a different Library per fixture, or two fixtures would share a focus
// and the round trip would pass by accident.
struct Standalone {
  DemoScreenFactory factory;
  std::unique_ptr<Screen> parent;
  std::unique_ptr<Screen> screen;
  // Reader and Peek are the two screens the factory refuses without a body face,
  // and a face is a TTF plus a rasteriser rather than a value -- so it is held
  // here, beside the Library the overlays hold a reference to, for the same
  // lifetime reason.
  std::vector<uint8_t> ttf;
  ScalableFont body;
  // THE PEEK'S COLUMN COMES FROM THE THEME, not from four numbers written out here
  // as the Reader's are. Its panel is inset and its height is a fixed box the theme
  // owns (kPeekPanelH), so a hand-built PageMetrics would be a second, disagreeing
  // spelling of the geometry -- and a peek built at the reading measure is exactly
  // the state test_screen_peek.cpp's last case exists to refuse. Held by pointer so
  // the other twelve screens do not each load the twelve-file ramp for nothing.
  std::unique_ptr<ramp::Ramp> ramp;
  QuietTheme theme;

  Screen& get() const { return *screen; }
};

std::unique_ptr<Standalone> build(ScreenId id) {
  auto b = std::make_unique<Standalone>();
  // Any non-zero row count will do here; this is not a layout test. Zero would
  // give the Library an empty window and hide the very thing being checked.
  b->factory.setLibraryVisibleRows(7);
  // Any workable geometry, for the same reason: a Settings whose metrics were
  // never told has a zero-height window, and movement on a window with no height
  // is refused (ScrollWindow's rule, which Settings now shares instead of
  // hand-rolling around it).
  b->factory.setSettingsMetrics(700, 55, 45);
  // AND Contents, for the same reason again -- a window with no height refuses
  // movement, so a Contents that was never told a row count would sit in the loop
  // below reporting an immovable focus and be counted as one of the screens that
  // legitimately cannot move. That is the "reports on less than it claims" failure the
  // comment above is about, and it happened: the count came back 6 where the two new
  // screens should have made it 7.
  b->factory.setContentsVisibleRows(8);
  // AND THE DEMO ASKED FOR, because the factory now refuses a reader menu or a contents
  // that nothing primed -- it used to substitute the board's own, which is how a real
  // book came to show Middlemarch's chapters on the device. A fixture that did not ask
  // would get a null screen, which is the refusal working.
  b->factory.setContentsDemo();
  // AND BookEnd, for that same reason. It is primed unconditionally rather than
  // under `if (id == ScreenId::BookEnd)` because it is a value copy and needs no
  // face, no metrics and no parent -- the Reader's and the Peek's demos are gated
  // only because a body face costs a TTF load the other screens should not pay.
  b->factory.setBookEndDemo();
  // AND BookError, on exactly that argument: Facts is a value copy, so priming it
  // unconditionally costs nothing and keeps the screen inside every loop below. The
  // factory REFUSES an unprimed one, so a fixture that did not prime it would get a
  // null and the REQUIRE would fire -- which is the refusal working, not a reason to
  // let the screen out of the catalogue.
  b->factory.setBookErrorFacts(demoBookErrorFacts());
  // AND THE CONNECT FLOW, on exactly that argument: every one of its six
  // screens is refused unprimed, so a fixture that did not ask would get a
  // null and the REQUIRE below would fire -- which is the refusal working
  // rather than a reason to let six screens out of the catalogue.
  //
  // setWifiDemo primes all six at once because they share their content: the
  // saved list, the scan, and the SSID a join is about. Six setters here would
  // be six chances to prime five.
  b->factory.setWifiDemo();
  // The picker is a WINDOWED list and gets its row count from the theme, so an
  // unset one refuses movement -- which would make it look like a screen whose
  // focus legitimately cannot move and quietly shrink the `movable` count
  // below. That is the trap Contents already sprang here once.
  b->factory.setWifiPickerVisibleRows(7);
  if (id == ScreenId::Reader || id == ScreenId::Peek) {
    // GIVEN a body face rather than skipped. Excluding either from the loop would
    // have been a screen this file claims to cover and does not -- and both are
    // Screens with no movable focus, which is exactly the case the `movable` count
    // below exists to keep honest.
    b->ttf = golden::slurp(std::string(ASSETS_DIR) + "/built/literata_body.ttf");
    REQUIRE(b->body.init(b->ttf.data(), b->ttf.size(), reader::kBodyPpem));
    b->factory.setReaderBody(&b->body);
  }
  if (id == ScreenId::Reader) {
    PageMetrics m;
    m.columnLeft = 18;
    m.columnTop = 100;
    m.columnW = 444;
    m.columnH = 600;
    b->factory.setReaderMetrics(m);
    b->factory.setReaderDemo();
  }
  if (id == ScreenId::Peek) {
    // AND THE DEMO ASKED FOR, as Contents and the Reader are: an unprimed Peek is
    // refused, which is what makes one unrestorable across a wake. A fixture that
    // did not ask would get a null screen, and that is the refusal working.
    b->ramp = std::make_unique<ramp::Ramp>();
    PageMetrics m;
    b->theme.peekMetrics(480, 800, b->ramp->fonts, b->body, Settings{}, m);
    b->factory.setPeekMetrics(m);
    b->factory.setPeekDemo();
  }
  if (id == ScreenId::Home) {
    // The factory refuses Home on purpose -- the root is never rebuilt -- so the
    // one screen the shell constructs by hand is constructed by hand here too.
    b->screen = std::make_unique<HomeScreen>(demoHomeVm(), demoHomeTargets());
    return b;
  }
  if (id == ScreenId::ItemActions || id == ScreenId::DeleteConfirm ||
      id == ScreenId::BookDetails) {
    b->parent = b->factory.create(ScreenId::Library);
    // ONTO A BOOK, because all three of these are about one. The demo list's first
    // row is a FOLDER, and the Library refuses Gesture::Secondary over one -- so a
    // fixture left on row 0 asks the factory for a state no press can reach. It is
    // DeleteConfirm that says so out loud: it refuses a folder, on FileSystem::
    // remove's own files-only contract, exactly as LibraryScreen::deleteFocused
    // always has.
    b->parent->onEvent(InputEvent{Button::Down, PressKind::Short});
  }
  b->screen = b->factory.create(id);
  return b;
}

const InputEvent kDown{Button::Down, PressKind::Short};

}  // namespace

TEST_CASE("every screen accepts back the focus it reports") {
  // Counted, not assumed. A refactor that made every screen report a fixed focus
  // would leave the loop below passing on nothing at all, which is the failure
  // mode this project keeps hitting -- a check that reports on less than it
  // claims. NINE screens can move their focus today: Home, Library, the two Library
  // overlays, Settings, the reader menu, the contents, Typography and BookEnd, whose
  // two slabs are the ninth, and BookError, whose OK/DELETE pair is the tenth.
  // BookDetails, Sleep, the Reader, the Peek and the
  // SD-missing prompt have one thing on them and legitimately report 0 -- the Peek has
  // no selection at all, only a page.
  //
  // THERE ARE TWO OF THESE COUNTS, this one and `wrapping` below, and they live in
  // different test cases. This project's own notes have only ever mentioned one, so a
  // screen added to the catalogue moves both or fails on the half nobody looked at.
  int movable = 0;

  for (const ScreenId id : kAllScreens) {
    CAPTURE(std::string(screenName(id)));
    auto live = build(id);
    REQUIRE(live->screen != nullptr);

    const int fresh = live->get().focus();
    live->get().onEvent(kDown);
    const int moved = live->get().focus();
    if (moved == fresh) continue;  // nothing to preserve on this screen
    ++movable;

    // A SECOND instance, as a wake gets: the shell builds the screen from
    // scratch and then restores. Asking the screen that already moved would
    // prove nothing, since it is already sitting on the answer.
    auto restored = build(id);
    REQUIRE(restored->screen != nullptr);
    REQUIRE(restored->get().focus() == fresh);
    CHECK(restored->get().setFocus(moved));
    CHECK(restored->get().focus() == moved);
  }

  // TEN, PLUS THE FOUR OF THE CONNECT FLOW WHOSE FOCUS CAN MOVE. The count is
  // hand-maintained on purpose -- it is what stops this loop quietly testing
  // nothing when a screen stops being constructible -- and it has been wrong
  // at every revision before the one that wrote it, so the arithmetic is
  // spelled out rather than left to be re-derived:
  //
  //   WifiSettings        two saved networks and a SETUP row
  //   WifiPicker          five scan rows plus Rescan
  //   WifiPassword        44 cells, and the FIRST GridFocusScreen here
  //   WifiError           three slabs on the bad-password shape
  //
  // The two that cannot move are WifiConnect, which has no focus at all
  // because it has one action and it is CANCEL, and WifiNetworkActions, whose
  // single row means a move that cannot change anything.
  CHECK(movable == 14);
}

TEST_CASE("every screen with a movable focus wraps off the end") {
  // THE CATALOGUE CHECK THAT WOULD HAVE CAUGHT SETTINGS. Wrapping is Focus's
  // default, so a screen gets it by using the primitive -- and a screen that
  // hand-rolls its own stepping (Settings has to, to skip section headers) can
  // silently keep clamping. It did. Nothing else would have noticed: the firmware
  // would simply have had one list that stopped at the end while every other list
  // rolled over.
  int wrapping = 0;
  for (const ScreenId id : kAllScreens) {
    CAPTURE(std::string(screenName(id)));
    auto s = build(id);
    REQUIRE(s->screen != nullptr);
    const int start = s->get().focus();
    s->get().onEvent(kDown);
    if (s->get().focus() == start) continue;  // no movable focus on this screen

    // Walk down until the focus goes BACKWARDS, which only a wrap can do. The cap
    // is a runaway guard, not an expected bound -- no list in V1 is near it.
    bool wrapped = false;
    int prev = s->get().focus();
    for (int i = 0; i < 512 && !wrapped; ++i) {
      s->get().onEvent(kDown);
      if (s->get().focus() < prev) wrapped = true;
      prev = s->get().focus();
    }
    CHECK(wrapped);
    ++wrapping;
  }
  // The same fourteen, and it must stay the same number as `movable` above:
  // every list in this firmware wraps, so a screen that can move and does not
  // wrap is the Settings defect this case was written for.
  CHECK(wrapping == 14);
}

TEST_CASE("restoring the focus a screen is already on is a no-op, not a failure") {
  // The bool means "something moved", not "the restore was accepted" -- the shell
  // reads it to decide whether a repaint or an NVS write is owed, and a screen
  // that returned true for an unchanged focus would cost a panel refresh on every
  // wake. Same contract on every screen, so it is checked on every screen.
  for (const ScreenId id : kAllScreens) {
    CAPTURE(std::string(screenName(id)));
    auto s = build(id);
    REQUIRE(s->screen != nullptr);
    const int where = s->get().focus();
    CHECK_FALSE(s->get().setFocus(where));
    CHECK(s->get().focus() == where);
  }
}

TEST_CASE("every screen that reports a place accepts that place back") {
  // THE FOCUS ROUND TRIP'S SECOND HALF (#14). A focus is an index into a list,
  // and the Library can be listing a SUBFOLDER of /books -- so the record stores
  // WHICH list beside the index into it, and the pair has the same obligation the
  // focus pair does: a screen that reports a place and cannot accept one back
  // hands the wake a row with nothing to index.
  //
  // WHY THIS WALKS THE CATALOGUE RATHER THAN ASKING THE LIBRARY. Screen::focus/
  // setFocus shipped one-way on three screens, each behind a comment arguing its
  // own case was the exception, and FocusScreen's final pair is what made that
  // unwritable. There is no equivalent here on purpose -- one screen has a place,
  // and a shared base for one caller is a header edge bought for nothing -- so
  // this walk plus App::restore's refusal to apply a focus whose place was
  // refused are what stand in for it. The second screen to want a place is the
  // extraction point.
  //
  // COUNTED, so the loop cannot quietly end up testing nothing: ONE screen has a
  // place today, the Library, whose place is the directory it is listing.
  int placed = 0;
  for (const ScreenId id : kAllScreens) {
    CAPTURE(std::string(screenName(id)));
    auto s = build(id);
    REQUIRE(s->screen != nullptr);
    const std::string where(s->get().place());
    if (where.empty()) {
      // A SCREEN WITH NO PLACE REFUSES EVERY PLACE, and that default is what
      // makes the whole mechanism degrade rather than mislead: a record naming a
      // place for a screen that has none is a record from a build whose lists are
      // not this build's, and App::restore drops the row rather than applying it.
      CHECK_FALSE(s->get().setPlace("/books/Classics"));
      continue;
    }
    ++placed;

    // A SECOND instance, as a wake gets: the shell builds the screen from scratch
    // and then restores. Asking the one that is already there would prove nothing.
    auto restored = build(id);
    REQUIRE(restored->screen != nullptr);
    CHECK(restored->get().setPlace(where));
    CHECK(restored->get().place() == where);
  }
  CHECK(placed == 1);
}
