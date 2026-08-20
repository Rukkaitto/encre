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

TEST_CASE("down walks from Continue into the menu and stops at the last row") {
  HomeScreen h = makeHome();
  CHECK(h.focus() == -1);
  CHECK(h.onEvent(kDown).kind == Action::Kind::Redraw);
  CHECK(h.focus() == 0);
  CHECK(h.onEvent(kDown).kind == Action::Kind::Redraw);
  CHECK(h.focus() == 1);
  // Clamped, not wrapped -- and no redraw, because nothing moved.
  CHECK(h.onEvent(kDown).kind == Action::Kind::None);
  CHECK(h.focus() == 1);
}

TEST_CASE("up walks back to Continue and stops there") {
  HomeScreen h = makeHome();
  h.onEvent(kDown);
  h.onEvent(kDown);
  REQUIRE(h.focus() == 1);
  CHECK(h.onEvent(kUp).kind == Action::Kind::Redraw);
  CHECK(h.onEvent(kUp).kind == Action::Kind::Redraw);
  CHECK(h.focus() == -1);
  CHECK(h.onEvent(kUp).kind == Action::Kind::None);
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
