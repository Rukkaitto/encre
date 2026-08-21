#include <map>

#include "doctest.h"
#include "reader/app.h"

using namespace reader;

namespace {

// A screen that records what it was sent and returns a scripted action. Enough
// to test the stack without dragging fonts or a theme in.
class FakeScreen : public Screen {
 public:
  FakeScreen(ScreenId id, Action next, ButtonMask holds = 0)
      : id_(id), next_(next), holds_(holds) {}
  ScreenId id() const override { return id_; }
  ButtonMask longPressable() const override { return holds_; }
  Action onEvent(const InputEvent&) override {
    ++events;
    return next_;
  }
  void render(Framebuffer&, const FontSet&, Theme&, Plane) const override {}
  void setNext(Action a) { next_ = a; }
  int events = 0;

 private:
  ScreenId id_;
  Action next_;
  ButtonMask holds_;
};

class FakeFactory : public ScreenFactory {
 public:
  std::map<ScreenId, Action> actions;
  std::map<ScreenId, ButtonMask> holds;
  bool refuse = false;
  std::unique_ptr<Screen> create(ScreenId id) override {
    if (refuse) return nullptr;
    return std::make_unique<FakeScreen>(id, actions.count(id) ? actions[id] : Action::none(),
                                        holds.count(id) ? holds[id] : 0);
  }
};

// A screen that wants true 4-level grey. Nothing in the product does yet; this
// exists so the opt-in itself is pinned, because the retained grayscale path is
// only reachable through it.
class GrayscaleScreen : public FakeScreen {
 public:
  GrayscaleScreen() : FakeScreen(ScreenId::Home, Action::none()) {}
  Fidelity fidelity() const override { return Fidelity::Grayscale; }
};

// A screen that wants stippled edges rather than hard ones. Same story: nothing
// in the product declares it now that chrome ships Mono, so this is what keeps
// the opt-in reachable and stops the dithered path being read as dead code.
class DitheredScreen : public FakeScreen {
 public:
  DitheredScreen() : FakeScreen(ScreenId::Home, Action::none()) {}
  Fidelity fidelity() const override { return Fidelity::Dithered; }
};

const InputEvent kConfirm{Button::Confirm, PressKind::Short};

}  // namespace

TEST_CASE("fidelity defaults to Mono, and the other two are explicit opt-ins") {
  // The default is the hard 1-bit, one-waveform path -- what chrome ships and
  // what the reference firmware does on this panel. A screen that says nothing
  // gets it; the stipple and the three-waveform path each cost an override. The
  // inverse default is what made every chrome screen pay 1363 ms per paint.
  FakeScreen quiet(ScreenId::Home, Action::none());
  CHECK(quiet.fidelity() == Fidelity::Mono);
  DitheredScreen stippled;
  CHECK(stippled.fidelity() == Fidelity::Dithered);
  GrayscaleScreen expensive;
  CHECK(expensive.fidelity() == Fidelity::Grayscale);
  // All three are distinct, so a switch over them cannot collapse two paths.
  CHECK(quiet.fidelity() != stippled.fidelity());
  CHECK(stippled.fidelity() != expensive.fidelity());
}

TEST_CASE("hint slots map to the hardware button order") {
  // Back, Confirm, Up, Down -- NOT the enum's own order, which puts Left and
  // Right between Confirm and Up.
  CHECK(hintHoldMask({true, false, false, false}) == buttonBit(Button::Back));
  CHECK(hintHoldMask({false, true, false, false}) == buttonBit(Button::Confirm));
  CHECK(hintHoldMask({false, false, true, false}) == buttonBit(Button::Up));
  CHECK(hintHoldMask({false, false, false, true}) == buttonBit(Button::Down));
  CHECK(hintHoldMask({false, false, false, false}) == 0);
  CHECK(hintHoldMask({true, true, true, true}) ==
        (buttonBit(Button::Back) | buttonBit(Button::Confirm) | buttonBit(Button::Up) |
         buttonBit(Button::Down)));
}

TEST_CASE("the first frame is dirty and counts as a transition") {
  FakeFactory f;
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::none()), f);
  CHECK(app.dirty());
  CHECK(app.transition());
  app.clearDirty();
  CHECK_FALSE(app.dirty());
  CHECK_FALSE(app.transition());
}

TEST_CASE("push and pop move the top of the stack") {
  FakeFactory f;
  // Script the factory BEFORE the push: a screen's action is baked in when the
  // factory builds it, so setting f.actions afterwards would change nothing and
  // the test would pass without exercising the pop.
  f.actions[ScreenId::Library] = Action::pop();
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::push(ScreenId::Library)), f);
  app.clearDirty();

  app.dispatch(kConfirm);
  CHECK(app.depth() == 2);
  CHECK(app.top().id() == ScreenId::Library);
  CHECK(app.dirty());
  CHECK(app.transition());
  app.clearDirty();

  app.dispatch(kConfirm);  // Library pops
  CHECK(app.depth() == 1);
  CHECK(app.top().id() == ScreenId::Home);
  CHECK(app.dirty());
  CHECK(app.transition());
}

TEST_CASE("popping the root is refused and never empties the stack") {
  FakeFactory f;
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::pop()), f);
  app.clearDirty();
  app.dispatch(kConfirm);
  CHECK(app.depth() == 1);
  CHECK(app.top().id() == ScreenId::Home);
  // Nothing changed on screen, so nothing needs repainting.
  CHECK_FALSE(app.dirty());
}

TEST_CASE("a popped screen returns to the one underneath, which kept its state") {
  FakeFactory f;
  auto root = std::make_unique<FakeScreen>(ScreenId::Home, Action::push(ScreenId::Settings));
  FakeScreen* home = root.get();
  App app(std::move(root), f);
  f.actions[ScreenId::Settings] = Action::pop();

  app.dispatch(kConfirm);  // Home pushes Settings
  REQUIRE(app.depth() == 2);
  app.dispatch(kConfirm);  // Settings pops
  CHECK(app.depth() == 1);
  CHECK(app.top().id() == ScreenId::Home);
  // The same object, not a rebuilt one: its event count survived the round trip.
  CHECK(home->events == 1);
  CHECK(&app.top() == home);
}

TEST_CASE("a redraw is dirty but is not a transition") {
  FakeFactory f;
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::redraw()), f);
  app.clearDirty();
  app.dispatch(kConfirm);
  CHECK(app.dirty());
  CHECK_FALSE(app.transition());
}

TEST_CASE("an action of None leaves the screen clean") {
  FakeFactory f;
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::none()), f);
  app.clearDirty();
  app.dispatch(kConfirm);
  CHECK_FALSE(app.dirty());
}

TEST_CASE("a factory that cannot build the screen leaves the stack intact") {
  FakeFactory f;
  f.refuse = true;
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::push(ScreenId::Library)), f);
  app.clearDirty();
  app.dispatch(kConfirm);
  CHECK(app.depth() == 1);
  CHECK(app.top().id() == ScreenId::Home);
  CHECK_FALSE(app.dirty());
}

TEST_CASE("pushScreen puts a screen on the stack with no event behind it") {
  // The shell's wake restore: the session record names a screen and there is no
  // press that implies it. Home stays underneath, so Back still works.
  FakeFactory f;
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::none()), f);
  app.clearDirty();

  CHECK(app.pushScreen(ScreenId::Settings));
  CHECK(app.depth() == 2);
  CHECK(app.top().id() == ScreenId::Settings);
  // A restored screen still has to be painted, and it is a screen change.
  CHECK(app.dirty());
  CHECK(app.transition());
}

TEST_CASE("pushScreen refuses what it cannot build and leaves the stack alone") {
  // A session record written by a newer firmware can name a screen this build
  // has no factory case for. Pushing the nullptr would crash on the next render.
  FakeFactory f;
  f.refuse = true;
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::none()), f);
  app.clearDirty();

  CHECK_FALSE(app.pushScreen(ScreenId::Settings));
  CHECK(app.depth() == 1);
  CHECK(app.top().id() == ScreenId::Home);
  CHECK_FALSE(app.dirty());
}

TEST_CASE("the stack refuses to grow past the depth it reserved for") {
  // The constructor reserves kMaxDepth and the firmware is built
  // -fno-exceptions, so a vector that reallocated and could not would abort()
  // with no diagnostic. Refusing the push loses a screen; growing can lose the
  // device. V1's deepest real path is four (Home > Library > actions > confirm).
  FakeFactory f;
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::none()), f);
  int pushed = 1;
  while (app.pushScreen(ScreenId::Settings)) ++pushed;
  CHECK(pushed == 8);
  CHECK(app.depth() == 8);
  // ...and it is still a usable app afterwards, not a wedged one.
  CHECK(app.top().id() == ScreenId::Settings);
}

TEST_CASE("the long-press mask follows the top of the stack") {
  FakeFactory f;
  f.holds[ScreenId::Library] = buttonBit(Button::Confirm);
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::push(ScreenId::Library), 0), f);
  CHECK(app.longPressable() == 0);
  app.dispatch(kConfirm);
  CHECK(app.longPressable() == buttonBit(Button::Confirm));
}

TEST_CASE("a sleep action is latched until the shell clears it") {
  FakeFactory f;
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::sleep()), f);
  CHECK_FALSE(app.sleepRequested());
  app.dispatch(kConfirm);
  CHECK(app.sleepRequested());
  app.clearSleepRequest();
  CHECK_FALSE(app.sleepRequested());
}
