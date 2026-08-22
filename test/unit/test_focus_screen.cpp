// THE BASE CLASS THAT MAKES THE FOCUS PAIR STRUCTURAL. Its contract is tested
// here on a minimal screen, so the five real screens' suites test their content
// rather than the mechanism -- and so a change to the mechanism fails here with
// the mechanism's name on it instead of five times with five screens'.
#include <vector>

#include "doctest.h"
#include "reader/focus_screen.h"

using namespace reader;

namespace {

struct MiniScreen : FocusScreen {
  int synced = 0;
  int mirrored = -99;
  std::vector<bool> table;  // empty = everything focusable

  explicit MiniScreen(int count, std::vector<bool> t = {}, Focus::None none = Focus::Noneless)
      : FocusScreen(count, count, none), table(std::move(t)) {
    syncVm();
  }

  // The protected mechanics, exposed so the test can drive them directly.
  using FocusScreen::moveFocus;
  using FocusScreen::window;

  ScreenId id() const override { return ScreenId::Home; }
  ButtonMask longPressable() const override { return 0; }
  Action onEvent(const InputEvent& ev) override {
    if (ev.kind != PressKind::Short) return Action::none();
    if (ev.button == Button::Down) return moveFocus(+1);
    if (ev.button == Button::Up) return moveFocus(-1);
    return Action::none();
  }
  void render(Framebuffer&, const FontSet&, Theme&, Plane) const override {}

  bool focusable(int i) const override {
    if (table.empty()) return true;
    return i >= 0 && i < static_cast<int>(table.size()) && table[static_cast<size_t>(i)];
  }
  void syncVm() override {
    ++synced;
    mirrored = focus();
  }
};

}  // namespace

TEST_CASE("setFocus and moveFocus mirror into the vm exactly when something changed") {
  MiniScreen s(3);
  const int base = s.synced;
  CHECK(s.setFocus(2));
  CHECK(s.mirrored == 2);
  CHECK(s.synced == base + 1);
  // Unchanged is false and owes no mirror call -- the shell reads the bool to
  // decide whether a repaint or an NVS write is owed.
  CHECK_FALSE(s.setFocus(2));
  CHECK(s.synced == base + 1);
  CHECK(s.moveFocus(+1).kind == Action::Kind::Redraw);  // wraps to 0
  CHECK(s.mirrored == 0);
}

TEST_CASE("a clamped screen answers none() at the end instead of paying a redraw") {
  MiniScreen s(3);
  s.window().setWrapping(false);
  s.setFocus(2);
  CHECK(s.moveFocus(+1).kind == Action::Kind::None);
  CHECK(s.focus() == 2);
}

TEST_CASE("the focusable override skips and refuses through the shared mechanism") {
  MiniScreen s(5, {false, true, true, false, true});
  s.setFocus(1);
  CHECK(s.moveFocus(+2).kind == Action::Kind::Redraw);  // 2, skip 3, land 4
  CHECK(s.focus() == 4);
  CHECK(s.moveFocus(+1).kind == Action::Kind::Redraw);  // wraps: skip 0, land 1
  CHECK(s.focus() == 1);
  CHECK_FALSE(s.setFocus(3));  // a refused landing leaves everything alone
  CHECK(s.focus() == 1);
  CHECK(s.mirrored == 1);
}

TEST_CASE("a WithNone screen keeps -1 as a place the focus can be and report") {
  MiniScreen s(2, {}, Focus::WithNone);
  CHECK(s.focus() == -1);
  CHECK(s.setFocus(1));
  CHECK(s.setFocus(-1));
  CHECK(s.focus() == -1);
  CHECK(s.mirrored == -1);
}
