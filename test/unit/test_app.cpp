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
      : id_(id), next_(next), overlay_(overlay), log_(renderLog) {
    // DECLARED, not overridden: longPressable is no longer a virtual, because nine
    // screens answered it with the same expression. A fake declares what a real
    // screen declares.
    const std::array<bool, 4> ring{maskHas(holds, Button::Back), maskHas(holds, Button::Confirm),
                                   maskHas(holds, Button::Up), maskHas(holds, Button::Down)};
    declareHints(ring);
  }
  ScreenId id() const override { return id_; }
  bool isOverlay() const override { return overlay_; }
  // Default 0 -- "no promise" -- exactly as Screen's is, so the partial-repaint
  // tests below have to opt each screen in the way a real one does.
  uint32_t paintFootprint() const override { return footprint; }
  uint32_t footprint = 0;
  Action onGesture(const GestureEvent&) override {
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
  // What paintFootprint() should answer for each id. Absent means 0, which is
  // Screen's "no promise" and forbids a partial repaint.
  std::map<ScreenId, uint32_t> footprints;
  std::vector<ScreenId>* renderLog = nullptr;
  bool refuse = false;
  std::unique_ptr<Screen> create(ScreenId id) override {
    if (refuse) return nullptr;
    auto s = std::make_unique<FakeScreen>(id, actions.count(id) ? actions[id] : Action::none(),
                                          holds.count(id) ? holds[id] : 0, overlays.count(id) != 0,
                                          renderLog);
    if (footprints.count(id)) s->footprint = footprints[id];
    return s;
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
  void renderDeleteConfirm(Framebuffer&, const FontSet&, const DeleteConfirmViewModel&,
                           Plane) override {}
  void renderBookDetails(Framebuffer&, const FontSet&, const BookDetailsViewModel&,
                         Plane) override {}
  int libraryVisibleRows(int, const FontSet&) const override { return 0; }
  void renderSettings(Framebuffer&, const FontSet&, const SettingsViewModel&, Plane) override {}
  void renderTypography(Framebuffer&, const FontSet&, const GlyphSource*,
                        const TypographyViewModel&, Plane) override {}
  void renderSleep(Framebuffer&, const FontSet&, const SleepViewModel&, Plane) override {}
  void renderReaderMenu(Framebuffer&, const FontSet&, const ReaderMenuViewModel&,
                        Plane) override {}
  void renderContents(Framebuffer&, const FontSet&, const ContentsViewModel&, Plane) override {}
  int contentsVisibleRows(int, const FontSet&) override { return 8; }
  void readerMetrics(int, int, const FontSet&, const GlyphSource&, const Settings&,
                     PageMetrics&) const override {}
  void renderReader(Framebuffer&, const FontSet&, const GlyphSource&, const GlyphSource*,
                    const ReaderViewModel&, const Page&, Plane) override {}
  void peekMetrics(int, int, const FontSet&, const GlyphSource&, const Settings&,
                   PageMetrics&) const override {}
  int peekVisibleLines(const FontSet&, const GlyphSource&, const Settings&) const override {
    return 8;
  }
  void renderPeek(Framebuffer&, const FontSet&, const GlyphSource&, const GlyphSource*,
                  const PeekViewModel&, const Page&, Plane) override {}
  void settingsMetrics(int, const FontSet&, int& listH, int& rowH, int& headerH) const override {
    listH = 0;
    rowH = 1;
    headerH = 1;
  }
};

const InputEvent kConfirm{Button::Confirm, PressKind::Short};

// Everything App::render needs, none of it load-bearing here.
struct RenderTarget {
  Framebuffer fb{8, 8};
  FontSet fonts;
  NullTheme theme;
  void paint(const App& app) { app.render(fb, fonts, theme, Plane::Bw); }
  void paint(const App& app, Plane plane) { app.render(fb, fonts, theme, plane); }
  bool paintTop(const App& app) { return app.renderTopOnly(fb, fonts, theme, Plane::Bw); }
  bool paintTop(const App& app, Plane plane) {
    return app.renderTopOnly(fb, fonts, theme, plane);
  }
};

// THE ONE STATE A PARTIAL REPAINT IS ALLOWED IN: an App whose top screen is an
// overlay, whose frame this App has already painted in full, and which is now
// dirty from a plain Redraw -- which is exactly what a focus move inside an
// overlay leaves behind. Every test below starts here and breaks one condition.
struct PartialFixture {
  std::vector<ScreenId> log;
  FakeFactory f;
  App app;
  RenderTarget t;

  explicit PartialFixture(uint32_t footprint = 7)
      : app(std::make_unique<FakeScreen>(ScreenId::Home, Action::push(ScreenId::Settings), 0, false,
                                        &log),
            f) {
    f.renderLog = &log;
    f.overlays.insert(ScreenId::Settings);
    f.footprints[ScreenId::Settings] = footprint;
    f.actions[ScreenId::Settings] = Action::redraw();
    app.dispatch(kConfirm);  // Home pushes the overlay -- a transition
    REQUIRE(app.depth() == 2);
    REQUIRE(app.top().isOverlay());
    t.paint(app);  // ...and the whole stack is painted, which is what a push needs
    app.clearDirty();
    app.dispatch(kConfirm);  // the overlay redraws itself -- NOT a transition
    REQUIRE(app.dirty());
    REQUIRE_FALSE(app.transition());
    log.clear();
  }

  FakeScreen& overlay() { return static_cast<FakeScreen&>(app.top()); }
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

TEST_CASE("markDirty dirties the app without making it a transition") {
  FakeFactory f;
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::none()), f);
  app.clearDirty();
  REQUIRE_FALSE(app.dirty());
  app.markDirty();
  CHECK(app.dirty());
  // NOT a transition. A charge state appearing is not a screen change, and
  // kFullOnTransition would spend the 693 ms GC waveform on it instead of the
  // 389 ms DU -- a flash the user did not ask for.
  CHECK_FALSE(app.transition());
  // And it must not move the stack or the focus.
  CHECK(app.depth() == 1);
  CHECK(app.top().id() == ScreenId::Home);
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
  f.overlays.insert(ScreenId::SdMissing);
  f.actions[ScreenId::Settings] = Action::push(ScreenId::SdMissing);
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
        std::vector<ScreenId>{ScreenId::Home, ScreenId::Settings, ScreenId::SdMissing});
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

// --- the partial repaint ----------------------------------------------------
//
// A focus move inside an overlay changes nothing below it, so re-rendering the
// parent and re-veiling every pixel of the frame is work with no result. What
// makes skipping it safe is a precondition about the FRAME, not about the stack,
// and the frame is the one thing App cannot see -- so the conditions live here
// and the shell asks rather than deciding. Every refusal below is a correct full
// repaint; the cost of a wrong ACCEPT is stale pixels from a previous frame,
// which reads as a rendering bug and is the hardest kind to trace back to a
// caching decision.

TEST_CASE("a screen promises nothing about its footprint unless it says so") {
  // Zero is "no promise", and it is the default: a partial repaint of a screen
  // that has not opted in would repaint it over its own previous paint with no
  // guarantee the new paint covers the old one.
  FakeScreen plain(ScreenId::Home, Action::none());
  CHECK(plain.paintFootprint() == 0);
}

TEST_CASE("a focus move inside an overlay repaints the overlay alone") {
  PartialFixture fx;
  CHECK(fx.app.canRenderTopOnly(fx.t.fb, Plane::Bw));
  CHECK(fx.t.paintTop(fx.app));
  // The overlay, and NOT the parent underneath it. That is the whole saving: the
  // parent's text pass and a veil over every pixel of the frame.
  CHECK(fx.log == std::vector<ScreenId>{ScreenId::Settings});
}

TEST_CASE("two partial repaints in a row are both allowed") {
  // The record must survive a partial repaint: the frame, the screen, the plane,
  // the depth and the footprint are all still what they were. A record that only
  // a full paint could refresh would make every other focus move expensive for no
  // reason.
  PartialFixture fx;
  REQUIRE(fx.t.paintTop(fx.app));
  fx.app.clearDirty();
  fx.app.dispatch(kConfirm);
  REQUIRE(fx.app.dirty());
  CHECK(fx.t.paintTop(fx.app));
  CHECK(fx.log == std::vector<ScreenId>{ScreenId::Settings, ScreenId::Settings});
}

TEST_CASE("the first paint after boot is never partial") {
  // There is nothing in the frame yet. TWO independent conditions refuse it -- a
  // fresh App is in transition, and it has painted nothing -- so this holds even
  // for a caller that has somehow cleared the transition flag without painting.
  FakeFactory f;
  f.overlays.insert(ScreenId::Home);
  f.footprints[ScreenId::Home] = 7;
  auto root = std::make_unique<FakeScreen>(ScreenId::Home, Action::none(), 0, true);
  root->footprint = 7;
  App app(std::move(root), f);
  RenderTarget t;
  REQUIRE(app.dirty());
  REQUIRE(app.transition());
  CHECK_FALSE(app.canRenderTopOnly(t.fb, Plane::Bw));
  CHECK_FALSE(t.paintTop(app));

  app.clearDirty();
  // Still refused with the transition flag gone, because the frame holds nothing.
  CHECK_FALSE(app.canRenderTopOnly(t.fb, Plane::Bw));
}

TEST_CASE("a push is never partial, and neither is a pop") {
  // The stack changed, so everything below the top may be different. This is the
  // condition transition() already expresses, which is why it is the signal.
  PartialFixture fx;
  fx.f.overlays.insert(ScreenId::SdMissing);
  fx.f.footprints[ScreenId::SdMissing] = 7;
  REQUIRE(fx.t.paintTop(fx.app));
  fx.app.clearDirty();

  fx.overlay().setNext(Action::push(ScreenId::SdMissing));
  fx.app.dispatch(kConfirm);
  REQUIRE(fx.app.depth() == 3);
  REQUIRE(fx.app.transition());
  CHECK_FALSE(fx.app.canRenderTopOnly(fx.t.fb, Plane::Bw));
  CHECK_FALSE(fx.t.paintTop(fx.app));
  // ...and the full paint that follows re-establishes the record.
  fx.t.paint(fx.app);
  fx.app.clearDirty();

  static_cast<FakeScreen&>(fx.app.top()).setNext(Action::pop());
  fx.app.dispatch(kConfirm);
  REQUIRE(fx.app.depth() == 2);
  REQUIRE(fx.app.transition());
  CHECK_FALSE(fx.t.paintTop(fx.app));
}

TEST_CASE("a transition is refused even when the stack came back to where it was") {
  // A PUSH OR A POP IS NEVER PARTIAL, and this is the case where that rule is
  // doing the work on its own rather than being backed up by the frame/top/depth
  // checks: push an overlay and pop it again with no paint in between, and the top
  // screen and the depth are the ones the frame was painted at.
  //
  // The rule is deliberately conservative here -- the frame IS still valid, so a
  // partial repaint would happen to be correct. It is refused anyway, because the
  // pointer comparison it would otherwise rest on is an ABA: a popped screen's
  // memory is freed and the next push allocates a screen of similar size, so
  // `painted_.top == stack_.back().get()` can be true of a DIFFERENT screen at the
  // same address. transition() is the only condition that does not depend on an
  // address staying unique, so it is the one that has to hold the line, and one
  // wasted repaint per push-pop pair is the right price.
  PartialFixture fx;
  fx.f.overlays.insert(ScreenId::SdMissing);
  fx.f.footprints[ScreenId::SdMissing] = 7;
  REQUIRE(fx.t.paintTop(fx.app));
  fx.app.clearDirty();
  const Screen* paintedTop = &fx.app.top();
  const int paintedDepth = fx.app.depth();

  fx.overlay().setNext(Action::push(ScreenId::SdMissing));
  fx.app.dispatch(kConfirm);
  REQUIRE(fx.app.depth() == paintedDepth + 1);
  static_cast<FakeScreen&>(fx.app.top()).setNext(Action::pop());
  fx.app.dispatch(kConfirm);
  // Back to exactly the screen and depth the frame was painted at...
  REQUIRE(&fx.app.top() == paintedTop);
  REQUIRE(fx.app.depth() == paintedDepth);
  REQUIRE(fx.app.transition());
  // ...and refused all the same.
  CHECK_FALSE(fx.app.canRenderTopOnly(fx.t.fb, Plane::Bw));
  CHECK_FALSE(fx.t.paintTop(fx.app));
}

TEST_CASE("a push not yet painted stays a transition through a later redraw") {
  // Presses coalesce into one paint, so a push and a redraw can both land before
  // anything is drawn. Redraw must not clear the transition the push set, or the
  // paint that finally happens would be partial over a frame holding the screen
  // BELOW the one that was pushed.
  PartialFixture fx;
  fx.f.overlays.insert(ScreenId::SdMissing);
  fx.f.footprints[ScreenId::SdMissing] = 7;
  REQUIRE(fx.t.paintTop(fx.app));
  fx.app.clearDirty();
  fx.overlay().setNext(Action::push(ScreenId::SdMissing));
  fx.app.dispatch(kConfirm);  // push, not painted
  static_cast<FakeScreen&>(fx.app.top()).setNext(Action::redraw());
  fx.app.dispatch(kConfirm);  // ...then a redraw on the new top
  CHECK(fx.app.transition());
  CHECK_FALSE(fx.t.paintTop(fx.app));
}

TEST_CASE("markDirty invalidates the partial-repaint record, even over an overlay") {
  // PartialFixture is the one state a partial repaint is legal in: an overlay on
  // top, a frame this App has already painted in full, dirty from a plain
  // Redraw. Every clause canRenderTopOnly checks was written assuming dirty_ only
  // ever turns true for a reason the TOP screen knows about -- so with an overlay
  // up, markDirty's dirty_ = true alone would satisfy every one of them: the
  // frame, the top, the depth and the footprint are all still what they were.
  // renderTopOnly would then repaint the overlay's own pixels and leave whatever
  // markDirty was actually called for stale on glass, with dirty_ cleared and
  // nothing left to correct it. ItemActions and DeleteConfirm both carry
  // non-zero footprints, so this is reachable on real screens, not theoretical.
  PartialFixture fx;
  REQUIRE(fx.app.canRenderTopOnly(fx.t.fb, Plane::Bw));
  fx.app.markDirty();
  CHECK(fx.app.dirty());
  CHECK_FALSE(fx.app.canRenderTopOnly(fx.t.fb, Plane::Bw));
  CHECK_FALSE(fx.t.paintTop(fx.app));
}

TEST_CASE("only an overlay can be partially repainted") {
  // A non-overlay fills the frame, so there is nothing beneath it to preserve --
  // and it would be actively wrong, because a full paint clears the frame first
  // and a partial one must not, leaving every pixel the screen does not draw
  // stale.
  FakeFactory f;
  f.footprints[ScreenId::Settings] = 7;  // opted in, but NOT an overlay
  f.actions[ScreenId::Settings] = Action::redraw();
  App app(std::make_unique<FakeScreen>(ScreenId::Home, Action::push(ScreenId::Settings)), f);
  RenderTarget t;
  app.dispatch(kConfirm);
  REQUIRE(app.depth() == 2);
  REQUIRE_FALSE(app.top().isOverlay());
  t.paint(app);
  app.clearDirty();
  app.dispatch(kConfirm);
  REQUIRE(app.dirty());
  REQUIRE_FALSE(app.transition());
  CHECK_FALSE(app.canRenderTopOnly(t.fb, Plane::Bw));
  CHECK_FALSE(t.paintTop(app));
}

TEST_CASE("a screen with no footprint promise is never partially repainted") {
  PartialFixture fx(0);
  CHECK(fx.overlay().paintFootprint() == 0);
  CHECK_FALSE(fx.app.canRenderTopOnly(fx.t.fb, Plane::Bw));
  CHECK_FALSE(fx.t.paintTop(fx.app));
  // Refused, and it painted NOTHING -- the caller falls back to render(), and a
  // renderTopOnly that half-painted before refusing would have already ruined the
  // frame it was checking.
  CHECK(fx.log.empty());
}

TEST_CASE("a footprint that moved is never partially repainted") {
  // The screen's own promise is "equal tokens cover the same pixels". A changed
  // token means the box may have moved, and the pixels the old paint reached and
  // the new one does not would survive as stale ink.
  PartialFixture fx(7);
  REQUIRE(fx.t.paintTop(fx.app));
  fx.app.clearDirty();
  fx.overlay().footprint = 8;
  fx.app.dispatch(kConfirm);
  REQUIRE(fx.app.dirty());
  REQUIRE_FALSE(fx.app.transition());
  CHECK_FALSE(fx.app.canRenderTopOnly(fx.t.fb, Plane::Bw));
  // ...and going back to the footprint the frame was painted at is allowed again.
  fx.overlay().footprint = 7;
  CHECK(fx.app.canRenderTopOnly(fx.t.fb, Plane::Bw));
}

TEST_CASE("a different framebuffer is never partially repainted") {
  // The precondition is about the frame's CONTENTS, and a second framebuffer does
  // not hold them. This is the check a caller cannot be trusted with, because the
  // caller is the thing that would have clobbered the frame.
  PartialFixture fx;
  RenderTarget other;
  CHECK_FALSE(fx.app.canRenderTopOnly(other.fb, Plane::Bw));
  CHECK_FALSE(other.paintTop(fx.app));
  CHECK(fx.app.canRenderTopOnly(fx.t.fb, Plane::Bw));
}

TEST_CASE("a different plane is never partially repainted") {
  // The frame holds one plane's render. Repainting the top screen in another
  // plane over it would mix two planes in one frame, which on the panel is
  // fringing rather than an obviously wrong screen.
  PartialFixture fx;
  CHECK_FALSE(fx.app.canRenderTopOnly(fx.t.fb, Plane::BwDithered));
  CHECK_FALSE(fx.app.canRenderTopOnly(fx.t.fb, Plane::Lsb));
  CHECK_FALSE(fx.app.canRenderTopOnly(fx.t.fb, Plane::Msb));
  CHECK(fx.app.canRenderTopOnly(fx.t.fb, Plane::Bw));
}

TEST_CASE("the dithered path can be partially repainted, in its own plane") {
  // Nothing declares Fidelity::Dithered today, so this is what keeps the other
  // one-pass path eligible rather than leaving it accidentally excluded.
  PartialFixture fx;
  fx.t.paint(fx.app, Plane::BwDithered);
  fx.app.clearDirty();
  fx.app.dispatch(kConfirm);
  CHECK(fx.app.canRenderTopOnly(fx.t.fb, Plane::BwDithered));
  CHECK_FALSE(fx.app.canRenderTopOnly(fx.t.fb, Plane::Bw));
}

TEST_CASE("the grayscale path is never partially repainted") {
  // It renders the screen three times plus a rebase, and the frame between passes
  // holds a DIFFERENT plane -- so "the frame holds the previous paint of this
  // plane" is false for every pass but the first. The plane check alone would
  // refuse it; the fidelity check says so on purpose, so a future two-frame
  // grayscale path cannot satisfy the plane check and quietly become eligible.
  class GrayOverlay : public FakeScreen {
   public:
    GrayOverlay() : FakeScreen(ScreenId::Settings, Action::redraw(), 0, true) { footprint = 7; }
    Fidelity fidelity() const override { return Fidelity::Grayscale; }
  };
  FakeFactory f;
  App app(std::make_unique<GrayOverlay>(), f);
  RenderTarget t;
  t.paint(app);
  app.clearDirty();
  app.dispatch(kConfirm);
  REQUIRE(app.dirty());
  REQUIRE_FALSE(app.transition());
  REQUIRE(app.top().isOverlay());
  REQUIRE(app.top().paintFootprint() != 0);
  CHECK_FALSE(app.canRenderTopOnly(t.fb, Plane::Bw));
}

TEST_CASE("a clean app is not partially repainted either") {
  PartialFixture fx;
  fx.app.clearDirty();
  CHECK_FALSE(fx.app.dirty());
  CHECK_FALSE(fx.app.canRenderTopOnly(fx.t.fb, Plane::Bw));
  CHECK_FALSE(fx.t.paintTop(fx.app));
}

TEST_CASE("a fresh App carries no repaint permission from the one it replaced") {
  // The shell replaces its App in three places -- a successful RETRY, a card lost
  // at runtime, and boot. A new stack over the old frame is precisely the
  // stale-pixel case, and the record lives in the App, so it goes with it.
  PartialFixture fx;
  REQUIRE(fx.app.canRenderTopOnly(fx.t.fb, Plane::Bw));
  FakeFactory f2;
  f2.overlays.insert(ScreenId::SdMissing);
  f2.footprints[ScreenId::SdMissing] = 7;
  auto root = std::make_unique<FakeScreen>(ScreenId::SdMissing, Action::redraw(), 0, true);
  root->footprint = 7;
  App fresh(std::move(root), f2);
  fresh.clearDirty();
  fresh.dispatch(kConfirm);
  REQUIRE(fresh.dirty());
  REQUIRE_FALSE(fresh.transition());
  // Same framebuffer, same plane, an overlay with a footprint -- and refused,
  // because THIS App has painted nothing into it.
  CHECK_FALSE(fresh.canRenderTopOnly(fx.t.fb, Plane::Bw));
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
