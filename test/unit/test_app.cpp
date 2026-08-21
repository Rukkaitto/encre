#include <map>
#include <set>
#include <vector>

#include "doctest.h"
#include "reader/app.h"
#include "reader/framebuffer.h"
#include "reader/fontset.h"
#include "reader/theme.h"

using namespace reader;

namespace {

// A screen that records what it was sent and returns a scripted action. Enough
// to test the stack without dragging fonts or a theme in.
class FakeScreen : public Screen {
 public:
  FakeScreen(ScreenId id, Action next, ButtonMask holds = 0, bool overlay = false,
             std::vector<ScreenId>* renderLog = nullptr)
      : id_(id), next_(next), holds_(holds), overlay_(overlay), log_(renderLog) {}
  ScreenId id() const override { return id_; }
  ButtonMask longPressable() const override { return holds_; }
  bool isOverlay() const override { return overlay_; }
  Action onEvent(const InputEvent&) override {
    ++events;
    return next_;
  }
  // Counting in a const method, so mutable: what the render tests need is the
  // order the App walked the stack in, and the walk is a const operation.
  void render(Framebuffer&, const FontSet&, Theme&, Plane) const override {
    ++renders;
    if (log_) log_->push_back(id_);
  }
  void setNext(Action a) { next_ = a; }
  int events = 0;
  mutable int renders = 0;

 private:
  ScreenId id_;
  Action next_;
  ButtonMask holds_;
  bool overlay_;
  std::vector<ScreenId>* log_;
};

class FakeFactory : public ScreenFactory {
 public:
  std::map<ScreenId, Action> actions;
  std::map<ScreenId, ButtonMask> holds;
  // Which ids the factory builds as overlays. The three real overlay screens
  // land in a later task, so the overlay tests mark existing ids instead --
  // isOverlay is a property of the screen, not of its id.
  std::set<ScreenId> overlays;
  std::vector<ScreenId>* renderLog = nullptr;
  bool refuse = false;
  std::unique_ptr<Screen> create(ScreenId id) override {
    if (refuse) return nullptr;
    return std::make_unique<FakeScreen>(id, actions.count(id) ? actions[id] : Action::none(),
                                        holds.count(id) ? holds[id] : 0, overlays.count(id) != 0,
                                        renderLog);
  }
};

// A Theme that draws nothing. The App's render walk does not care what a screen
// paints, only which screens it asks -- and a real theme would drag the whole
// font ramp into a test about a stack.
class NullTheme : public Theme {
 public:
  void renderHome(Framebuffer&, const FontSet&, const HomeViewModel&, Plane) override {}
  void renderSdMissing(Framebuffer&, const FontSet&, const SdMissingViewModel&, Plane) override {}
  void renderLibrary(Framebuffer&, const FontSet&, const LibraryViewModel&, Plane) override {}
  void renderItemActions(Framebuffer&, const FontSet&, const ItemActionsViewModel&,
                         Plane) override {}
  int libraryVisibleRows(int, const FontSet&) const override { return 0; }
  void renderStub(Framebuffer&, const FontSet&, const StubViewModel&, Plane) override {}
};

// Everything App::render needs, none of it load-bearing here.
struct RenderTarget {
  Framebuffer fb{8, 8};
  FontSet fonts;
  NullTheme theme;
  void paint(const App& app) { app.render(fb, fonts, theme, Plane::Bw); }
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

// --- overlays ---------------------------------------------------------------

TEST_CASE("a screen is not an overlay unless it says so") {
  // Default false: an overlay costs an override, so a screen cannot become one
  // by accident and start letting the screen beneath it show through.
  FakeScreen plain(ScreenId::Home, Action::none());
  CHECK_FALSE(plain.isOverlay());
}

TEST_CASE("an ordinary screen renders alone") {
  std::vector<ScreenId> log;
  FakeFactory f;
  f.renderLog = &log;
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::push(ScreenId::Library), 0, false,
                                       &log),
          f);
  RenderTarget t;
  t.paint(app);
  CHECK(log == std::vector<ScreenId>{ScreenId::Home});

  // ...and pushing another ordinary screen does NOT render the one beneath. The
  // stack is a stack of screens, and only an overlay is see-through.
  log.clear();
  app.dispatch(kConfirm);
  REQUIRE(app.depth() == 2);
  t.paint(app);
  CHECK(log == std::vector<ScreenId>{ScreenId::Library});
}

TEST_CASE("an overlay renders the screen beneath it, then itself") {
  // design/LibraryActions.dc.html is a centred panel over a still-visible,
  // veiled Library, so rendering the top of the stack is no longer enough.
  std::vector<ScreenId> log;
  FakeFactory f;
  f.renderLog = &log;
  f.overlays.insert(ScreenId::Settings);
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::push(ScreenId::Settings), 0, false,
                                       &log),
          f);
  app.dispatch(kConfirm);
  REQUIRE(app.depth() == 2);
  REQUIRE(app.top().isOverlay());
  log.clear();
  RenderTarget t;
  t.paint(app);
  // Order matters: the parent first, then the overlay's veil and panel over it.
  CHECK(log == std::vector<ScreenId>{ScreenId::Home, ScreenId::Settings});
}

TEST_CASE("two stacked overlays render three screens, bottom-up") {
  // Library > actions > delete confirm, which is V1's deepest path.
  std::vector<ScreenId> log;
  FakeFactory f;
  f.renderLog = &log;
  f.overlays.insert(ScreenId::Settings);
  f.overlays.insert(ScreenId::InputMonitor);
  f.actions[ScreenId::Settings] = Action::push(ScreenId::InputMonitor);
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::push(ScreenId::Settings), 0, false,
                                       &log),
          f);
  app.dispatch(kConfirm);
  app.dispatch(kConfirm);
  REQUIRE(app.depth() == 3);
  log.clear();
  RenderTarget t;
  t.paint(app);
  CHECK(log ==
        std::vector<ScreenId>{ScreenId::Home, ScreenId::Settings, ScreenId::InputMonitor});
}

TEST_CASE("the walk stops at the first non-overlay, not at the root") {
  // Home > Library > actions overlay paints Library and the overlay. Painting
  // Home as well would draw a screen that is entirely hidden, at the cost of the
  // whole frame's worth of text rendering.
  std::vector<ScreenId> log;
  FakeFactory f;
  f.renderLog = &log;
  f.overlays.insert(ScreenId::Settings);
  f.actions[ScreenId::Library] = Action::push(ScreenId::Settings);
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::push(ScreenId::Library), 0, false,
                                       &log),
          f);
  app.dispatch(kConfirm);  // Home -> Library
  app.dispatch(kConfirm);  // Library -> the overlay
  REQUIRE(app.depth() == 3);
  log.clear();
  RenderTarget t;
  t.paint(app);
  CHECK(log == std::vector<ScreenId>{ScreenId::Library, ScreenId::Settings});
}

TEST_CASE("an overlay at the root renders only itself") {
  // It should not happen -- an overlay veils a parent, and the root has none --
  // but it must not read off the end of the stack looking for one.
  std::vector<ScreenId> log;
  FakeFactory f;
  f.renderLog = &log;
  App app(std::make_unique<FakeScreen>(ScreenId::Settings, Action::none(), 0, true, &log), f);
  REQUIRE(app.top().isOverlay());
  RenderTarget t;
  t.paint(app);
  CHECK(log == std::vector<ScreenId>{ScreenId::Settings});
}

TEST_CASE("an overlay takes the input, and the screen beneath it sees none") {
  // The invariant to pin hardest. An overlay whose parent still received events
  // would move a focus the user cannot see -- and it would look like a rendering
  // bug rather than a dispatch bug, because the symptom appears on the screen
  // that reappears when the overlay is dismissed.
  FakeFactory f;
  f.overlays.insert(ScreenId::Settings);
  // The parent's scripted action is another push, so if it ever saw an event the
  // stack would grow -- a second, independent signal beside its event count.
  auto root = std::make_unique<FakeScreen>(ScreenId::Home, Action::push(ScreenId::Settings));
  FakeScreen* home = root.get();
  App app(std::move(root), f);
  app.dispatch(kConfirm);  // Home pushes the overlay
  REQUIRE(app.depth() == 2);
  REQUIRE(home->events == 1);
  FakeScreen* overlay = static_cast<FakeScreen*>(&app.top());

  for (int i = 0; i < 3; ++i) app.dispatch(kConfirm);
  CHECK(overlay->events == 3);
  CHECK(home->events == 1);   // still just the press that opened the overlay
  CHECK(app.depth() == 2);    // ...so nothing pushed a third screen
  CHECK(&app.top() == overlay);

  // Down and Up too, which is the pair that would silently move a hidden focus.
  app.dispatch(InputEvent{Button::Down, PressKind::Short});
  app.dispatch(InputEvent{Button::Up, PressKind::Short});
  CHECK(home->events == 1);
  CHECK(overlay->events == 5);
}

TEST_CASE("fidelity and the long-press mask come from the overlay, not its parent") {
  // Both already follow the top of the stack; an overlay must not be the case
  // that changes it. A veiled parent is still one paint, at whatever fidelity
  // the thing on top declares.
  FakeFactory f;
  f.overlays.insert(ScreenId::Settings);
  f.holds[ScreenId::Settings] = buttonBit(Button::Back);
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::push(ScreenId::Settings),
                                       buttonBit(Button::Confirm)),
          f);
  CHECK(app.longPressable() == buttonBit(Button::Confirm));
  app.dispatch(kConfirm);
  REQUIRE(app.depth() == 2);
  CHECK(app.longPressable() == buttonBit(Button::Back));
  CHECK(app.top().fidelity() == Fidelity::Mono);
}

TEST_CASE("popping an overlay goes back to painting the parent alone") {
  std::vector<ScreenId> log;
  FakeFactory f;
  f.renderLog = &log;
  f.overlays.insert(ScreenId::Settings);
  f.actions[ScreenId::Settings] = Action::pop();
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::push(ScreenId::Settings), 0, false,
                                       &log),
          f);
  app.dispatch(kConfirm);
  app.dispatch(kConfirm);  // the overlay pops itself
  REQUIRE(app.depth() == 1);
  log.clear();
  RenderTarget t;
  t.paint(app);
  CHECK(log == std::vector<ScreenId>{ScreenId::Home});
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
