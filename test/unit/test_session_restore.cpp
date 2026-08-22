// PUTTING THE WHOLE STACK BACK, not just the screen on top of it.
//
// The wake record used to hold one screen id, so Home > Library > actions came
// back as Home: the ladder pushed the overlay onto a fresh App, the factory
// refused it (correctly -- an overlay reads the focused row of the Library under
// it, and there was no Library under it), and the user landed at the root having
// lost both the screen and the row. Restoring the stack in order fixes the
// refusal as a side effect: the Library is pushed first and IS the thing the
// overlay then finds.
//
// The mechanic lives on App because App owns the stack, and it is one loop with
// no screen named in it -- the shell's restore used to be a ladder of
// `if (id == Home) ... else if (id == SdMissing) ...`, which is how three screens
// ended up each being a special case that had to be discovered from a device.
#include <vector>

#include "doctest.h"
#include "home_vm.h"
#include "reader/app.h"
#include "reader/screen_home.h"
#include "reader/screen_sd_missing.h"
#include "reader/screens.h"

using namespace reader;

namespace {

const InputEvent kDown{Button::Down, PressKind::Short};
const InputEvent kConfirm{Button::Confirm, PressKind::Short};
const InputEvent kHold{Button::Confirm, PressKind::Long};

struct Fixture {
  DemoScreenFactory factory;
  App app;

  Fixture()
      : app(std::make_unique<HomeScreen>(demoHomeVm(), demoHomeTargets()), factory) {
    factory.setLibraryVisibleRows(7);
  }
};

}  // namespace

TEST_CASE("a snapshot names every screen on the stack, root first, with its focus") {
  Fixture f;
  f.app.dispatch(kDown);     // Home: CONTINUE -> the LIBRARY row
  f.app.dispatch(kConfirm);  // open it
  REQUIRE(f.app.top().id() == ScreenId::Library);
  f.app.dispatch(kDown);
  f.app.dispatch(kDown);  // Library row 2
  f.app.dispatch(kHold);  // the actions overlay over it
  REQUIRE(f.app.top().id() == ScreenId::ItemActions);
  f.app.dispatch(kDown);  // its second action

  const std::vector<StackEntry> snap = f.app.snapshot();
  REQUIRE(snap.size() == 3);
  CHECK(snap[0].screen == ScreenId::Home);
  CHECK(snap[0].focus == 0);  // the LIBRARY row, which is where Confirm was pressed
  CHECK(snap[1].screen == ScreenId::Library);
  CHECK(snap[1].focus == 2);
  CHECK(snap[2].screen == ScreenId::ItemActions);
  CHECK(snap[2].focus == 1);
}

TEST_CASE("restoring a snapshot rebuilds the stack, focus and all") {
  Fixture live;
  live.app.dispatch(kDown);
  live.app.dispatch(kConfirm);
  live.app.dispatch(kDown);
  live.app.dispatch(kDown);
  live.app.dispatch(kHold);
  live.app.dispatch(kDown);
  const std::vector<StackEntry> snap = live.app.snapshot();

  // A SECOND App, as a wake gets: a fresh factory, a fresh Home, nothing else.
  Fixture woken;
  const App::RestoreReport r = woken.app.restore(snap);
  CHECK(r.rootMatched);
  CHECK(r.requested == 3);
  CHECK(r.restored == 3);
  CHECK(woken.app.depth() == 3);
  CHECK(woken.app.snapshot() == snap);
}

TEST_CASE("the overlay a wake could never rebuild comes back, because its parent goes first") {
  // The refusal this fixes: an overlay holds a reference to the Library under it,
  // and the factory hands back nullptr when there is none. Pushing one screen
  // could never satisfy that; pushing the stack in order always does.
  Fixture woken;
  const App::RestoreReport r = woken.app.restore({{ScreenId::Home, -1},
                                                  {ScreenId::Library, 3},
                                                  {ScreenId::ItemActions, 2},
                                                  {ScreenId::DeleteConfirm, 1}});
  CHECK(r.restored == 4);
  CHECK(woken.app.depth() == 4);
  CHECK(woken.app.top().id() == ScreenId::DeleteConfirm);
  CHECK(woken.app.top().focus() == 1);
}

TEST_CASE("a record whose root is not this App's root is refused whole") {
  // This is the SD-missing case, and it used to be a hand-written branch in the
  // shell: the card went away while the device slept, so the boot path rooted the
  // App at the no-card screen, and a record naming Home must not be layered over
  // it. Stated as "the roots disagree", it needs no screen name at all -- and it
  // covers the mirror case (a record naming SD-MISSING when the card mounted)
  // that the ladder had to spell out separately.
  DemoScreenFactory factory;
  App app(std::make_unique<SdMissingScreen>(), factory);
  const App::RestoreReport r = app.restore({{ScreenId::Home, 0}, {ScreenId::Library, 4}});
  CHECK_FALSE(r.rootMatched);
  CHECK(r.restored == 0);
  CHECK(app.depth() == 1);
  CHECK(app.top().id() == ScreenId::SdMissing);
}

TEST_CASE("a screen this build cannot make stops the restore there and keeps what stands") {
  // A record from a newer firmware, or one naming a screen whose parent is gone.
  // Losing the tail is the honest outcome; losing everything would throw away a
  // Library the user really was in.
  Fixture woken;
  // BookDetails over Home is unbuildable -- it needs a Library it can read.
  const App::RestoreReport r =
      woken.app.restore({{ScreenId::Home, 1}, {ScreenId::BookDetails, 0}});
  CHECK(r.rootMatched);
  CHECK(r.requested == 2);
  CHECK(r.restored == 1);
  CHECK(woken.app.depth() == 1);
  CHECK(woken.app.top().focus() == 1);  // the root's focus still landed
}

TEST_CASE("an empty record restores nothing and says so") {
  Fixture f;
  const App::RestoreReport r = f.app.restore({});
  CHECK_FALSE(r.rootMatched);
  CHECK(r.requested == 0);
  CHECK(r.restored == 0);
  CHECK(f.app.depth() == 1);
}

TEST_CASE("a restore onto a stack that is already deep is refused") {
  // Only the boot path restores, and it restores onto a fresh App. Anything else
  // is a caller bug, and layering a record over a live stack would put screens
  // the user is looking at underneath ones they are not.
  Fixture f;
  f.app.dispatch(kDown);
  f.app.dispatch(kConfirm);
  REQUIRE(f.app.depth() == 2);
  const App::RestoreReport r = f.app.restore({{ScreenId::Home, 0}, {ScreenId::Settings, 1}});
  CHECK_FALSE(r.rootMatched);
  CHECK(r.restored == 0);
  CHECK(f.app.depth() == 2);
}

TEST_CASE("a restored stack still needs painting, and paints as a screen change") {
  Fixture f;
  f.app.clearDirty();
  REQUIRE_FALSE(f.app.dirty());
  f.app.restore({{ScreenId::Home, 1}, {ScreenId::Settings, 1}});
  CHECK(f.app.dirty());
  CHECK(f.app.transition());
}
