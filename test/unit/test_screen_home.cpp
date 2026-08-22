#include "doctest.h"
#include "reader/screen_home.h"

using namespace reader;

namespace {

HomeViewModel vmWithTwoRows() {
  HomeViewModel vm;
  vm.title = "Middlemarch";
  vm.menu = {{"LIBRARY", "12"}, {"SETTINGS", ""}};
  vm.focusedMenuIndex = -1;
  vm.hints = {"READ", "SELECT", "UP", "DOWN"};
  return vm;
}

HomeScreen makeHome() {
  return HomeScreen(vmWithTwoRows(), {ScreenId::Library, ScreenId::Settings});
}

const InputEvent kDown{Button::Down, PressKind::Short};
const InputEvent kUp{Button::Up, PressKind::Short};
const InputEvent kConfirm{Button::Confirm, PressKind::Short};
const InputEvent kBack{Button::Back, PressKind::Short};

}  // namespace

TEST_CASE("down walks from Continue into the menu and wraps back to it") {
  HomeScreen h = makeHome();
  CHECK(h.focus() == -1);
  CHECK(h.onEvent(kDown).kind == Action::Kind::Redraw);
  CHECK(h.focus() == 0);
  CHECK(h.onEvent(kDown).kind == Action::Kind::Redraw);
  CHECK(h.focus() == 1);
  // CONTINUE is in the ring, not a wall before it: it is a position the user can
  // be in, so Down off the last row returns to it rather than to row 0.
  CHECK(h.onEvent(kDown).kind == Action::Kind::Redraw);
  CHECK(h.focus() == -1);
}

TEST_CASE("up from Continue wraps to the last menu row") {
  HomeScreen h = makeHome();
  REQUIRE(h.focus() == -1);
  CHECK(h.onEvent(kUp).kind == Action::Kind::Redraw);
  CHECK(h.focus() == 1);
  CHECK(h.onEvent(kUp).kind == Action::Kind::Redraw);
  CHECK(h.focus() == 0);
  CHECK(h.onEvent(kUp).kind == Action::Kind::Redraw);
  CHECK(h.focus() == -1);
}

TEST_CASE("confirm on a menu row pushes that row's screen") {
  HomeScreen h = makeHome();
  h.onEvent(kDown);
  Action a = h.onEvent(kConfirm);
  CHECK(a.kind == Action::Kind::Push);
  CHECK(a.target == ScreenId::Library);
  h.onEvent(kDown);
  a = h.onEvent(kConfirm);
  CHECK(a.kind == Action::Kind::Push);
  CHECK(a.target == ScreenId::Settings);
}

TEST_CASE("confirm on Continue does nothing yet -- the Reader is Phase 3") {
  HomeScreen h = makeHome();
  REQUIRE(h.focus() == -1);
  CHECK(h.onEvent(kConfirm).kind == Action::Kind::None);
}

TEST_CASE("back on Home does nothing -- its board binds Back to Read, which is Phase 3") {
  HomeScreen h = makeHome();
  CHECK(h.onEvent(kBack).kind == Action::Kind::None);
}

TEST_CASE("a row with no target screen is inert rather than pushing the wrong one") {
  // A menu longer than the target list must not read off the end.
  HomeScreen h(vmWithTwoRows(), {ScreenId::Library});
  h.onEvent(kDown);
  h.onEvent(kDown);
  REQUIRE(h.focus() == 1);
  CHECK(h.onEvent(kConfirm).kind == Action::Kind::None);
}

TEST_CASE("Home binds no long press, so its mask is empty and its bar shows no ring") {
  HomeScreen h = makeHome();
  CHECK(h.longPressable() == 0);
}

TEST_CASE("a long press on a button Home does not bind is ignored, not mistaken for a short one") {
  // The recognizer should never deliver this, but a screen that silently treated
  // Long as Short would hide a mask bug rather than surfacing it.
  HomeScreen h = makeHome();
  const InputEvent longDown{Button::Down, PressKind::Long};
  CHECK(h.onEvent(longDown).kind == Action::Kind::None);
  CHECK(h.focus() == -1);
}

TEST_CASE("an empty menu leaves focus on Continue") {
  HomeViewModel vm = vmWithTwoRows();
  vm.menu.clear();
  HomeScreen h(vm, {});
  CHECK(h.onEvent(kDown).kind == Action::Kind::None);
  CHECK(h.focus() == -1);
}

// --- Restoring a focus after a wake ------------------------------------------
//
// Home reports its focus so the session record can store it, and until now it
// did not accept one back: the record named Home, the restore ladder pushed
// nothing (Home is already the root) and the base-class setFocus no-op swallowed
// the value. Waking always landed on CONTINUE whatever the user had selected.
// These pin the other half of the round trip.

TEST_CASE("a restored focus lands on the menu row the record named") {
  HomeScreen h = makeHome();
  reader::Screen& s = h;
  REQUIRE(s.focus() == -1);
  CHECK(s.setFocus(1));
  CHECK(s.focus() == 1);
}

TEST_CASE("restoring CONTINUE is a restore like any other, not 'nothing selected'") {
  // -1 is a position on this screen, not the absence of one, so a record holding
  // it has to come back as the CONTINUE block rather than as the first menu row.
  HomeScreen h = makeHome();
  reader::Screen& s = h;
  REQUIRE(s.setFocus(1));
  CHECK(s.setFocus(-1));
  CHECK(s.focus() == -1);
}

TEST_CASE("restoring the focus that is already set changes nothing and says so") {
  // Same contract as ScrollWindow::setFocus: the bool means "something moved",
  // which is what the shell reads to decide whether a repaint or an NVS write is
  // owed. A successful restore that happens to be a no-op still returns false.
  HomeScreen h = makeHome();
  reader::Screen& s = h;
  REQUIRE(s.focus() == -1);
  CHECK_FALSE(s.setFocus(-1));
  CHECK(s.focus() == -1);
}

TEST_CASE("a focus past the end of the menu clamps to the last row") {
  // The menu is built at boot from what is on the card, so a record written when
  // it was longer has to land somewhere rather than off the end.
  HomeScreen h = makeHome();
  reader::Screen& s = h;
  CHECK(s.setFocus(400));
  CHECK(s.focus() == 1);
}

TEST_CASE("a focus below CONTINUE clamps to CONTINUE") {
  HomeScreen h = makeHome();
  reader::Screen& s = h;
  REQUIRE(s.setFocus(1));
  CHECK(s.setFocus(-9));
  CHECK(s.focus() == -1);
}

TEST_CASE("restoring a row onto an empty menu lands on CONTINUE") {
  HomeViewModel vm = vmWithTwoRows();
  vm.menu.clear();
  HomeScreen h(vm, {});
  reader::Screen& s = h;
  CHECK_FALSE(s.setFocus(0));
  CHECK(s.focus() == -1);
}
