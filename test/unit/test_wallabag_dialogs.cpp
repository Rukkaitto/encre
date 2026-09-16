#include "doctest.h"
#include "reader/screen_wallabag_dialogs.h"

using namespace reader;

namespace {
GestureEvent ev(Gesture g) {
  GestureEvent e;
  e.what = g;
  e.steps = 1;
  return e;
}
using Shape = WallabagErrorScreen::Shape;
}  // namespace

TEST_CASE("the connecting dialog names the host unquoted and takes only a cancel") {
  WallabagConnectingScreen s("wallabag.lan");
  CHECK(s.isOverlay());
  CHECK(s.id() == ScreenId::WallabagConnecting);
  CHECK(s.vm().caption == "CONNECTING\xE2\x80\xA6");
  CHECK(s.vm().message == "Connecting to wallabag.lan.");
  CHECK(s.vm().hints[0] == "CANCEL");
  CHECK(s.vm().hints[1].empty());
  CHECK(s.vm().hints[2].empty());
  CHECK(s.vm().hints[3].empty());
  // Nothing else is bound: a dialog with no focus has nothing for a mover.
  CHECK(s.onGesture(ev(Gesture::Activate)).kind == Action::Kind::None);
  CHECK(s.onGesture(ev(Gesture::Next)).kind == Action::Kind::None);
  CHECK(s.onGesture(ev(Gesture::Prev)).kind == Action::Kind::None);
}

TEST_CASE("Back on the connecting dialog latches rather than popping") {
  // The case Action::wifi()'s rule was written for: a sync is in flight and the
  // engine has to be told, so this is not navigation.
  WallabagConnectingScreen s("wallabag.lan");
  CHECK_FALSE(s.cancelled());
  const Action a = s.onGesture(ev(Gesture::Back));
  CHECK(a.kind == Action::Kind::Article);
  CHECK(s.cancelled());
}

TEST_CASE("the fetching stage is the same screen with different strings") {
  WallabagConnectingScreen s("wallabag.lan");
  CHECK(s.setFetching(3, 12));
  CHECK(s.vm().caption == "SYNCING\xE2\x80\xA6");
  // The word `article` is deliberately absent: with it, the message reflows to a
  // second line partway through a sync and the panel changes height while the
  // reader watches. See design/WallabagFetching.dc.html's measurements.
  CHECK(s.vm().message == "Fetching 3 of 12.");
  CHECK(s.vm().note == "ANYTHING FETCHED IS KEPT.");
  CHECK(s.id() == ScreenId::WallabagConnecting);

  // It REPORTS whether anything moved rather than repainting: a screen cannot
  // mark the App dirty, and one paint per file is the whole budget.
  CHECK(s.setFetching(4, 12));
  CHECK_FALSE(s.setFetching(4, 12));
}

TEST_CASE("three copy shapes, and the slab LIST is the shape") {
  // BookError's argument and the join flow's precedent: a sync fails three
  // distinguishable ways and one sentence would be a lie.
  WallabagErrorScreen signIn(Shape::SignIn);
  CHECK(signIn.vm().caption == "COULDN\xE2\x80\x99T SIGN IN");
  CHECK(signIn.vm().actions.size() == 1);
  CHECK(signIn.vm().actions[0] == "OK");

  WallabagErrorScreen offline(Shape::Offline);
  CHECK(offline.vm().caption == "COULDN\xE2\x80\x99T CONNECT");
  CHECK(offline.vm().actions.size() == 2);
  CHECK(offline.vm().actions[0] == "TRY AGAIN");

  WallabagErrorScreen none(Shape::NoNetwork);
  CHECK(none.vm().caption == "COULDN\xE2\x80\x99T CONNECT");
  CHECK(none.vm().actions.size() == 1);
  CHECK(none.vm().actions[0] == "OK");
  CHECK(none.vm().message == "No Wi-Fi network is saved. Join one in Settings first.");
}

TEST_CASE("TRY AGAIN is present on exactly one shape, and absent rather than inert") {
  // A rejected grant is deterministic -- the same file gives the same answer --
  // and so is an empty saved-network list. Only a round trip fails spuriously,
  // so only that shape can offer a retry that could succeed.
  for (const Shape sh : {Shape::SignIn, Shape::NoNetwork}) {
    WallabagErrorScreen s(sh);
    for (const std::string& label : s.vm().actions) CHECK(label != "TRY AGAIN");
  }
}

TEST_CASE("the two-slab shape is the only one with live movers") {
  // SdMissing's rule: with one slab `SELECT` would promise a choice, and Up and
  // Down would have no second row to reach.
  WallabagErrorScreen one(Shape::SignIn);
  CHECK(one.vm().hints[1] == "OK");
  CHECK(one.vm().hints[2].empty());
  CHECK(one.vm().hints[3].empty());

  WallabagErrorScreen two(Shape::Offline);
  CHECK(two.vm().hints[1] == "SELECT");
  CHECK(two.vm().hints[2] == "UP");
  CHECK(two.vm().hints[3] == "DOWN");
}

TEST_CASE("each slab latches its own outcome, resolved through the list") {
  WallabagErrorScreen offline(Shape::Offline);
  CHECK(offline.onGesture(ev(Gesture::Activate)).kind == Action::Kind::Article);
  CHECK(offline.chosen() == WallabagErrorScreen::Chosen::TryAgain);
  REQUIRE(offline.onGesture(ev(Gesture::Next)).kind == Action::Kind::Redraw);
  offline.onGesture(ev(Gesture::Activate));
  CHECK(offline.chosen() == WallabagErrorScreen::Chosen::Ok);

  // Slab 0 is `OK` on the other two shapes, which is why the outcome is resolved
  // through the shape and the list rather than through the index.
  WallabagErrorScreen signIn(Shape::SignIn);
  signIn.onGesture(ev(Gesture::Activate));
  CHECK(signIn.chosen() == WallabagErrorScreen::Chosen::Ok);
}

TEST_CASE("Back on a failure dialog latches too, because a radio is still up") {
  WallabagErrorScreen s(Shape::Offline);
  const Action a = s.onGesture(ev(Gesture::Back));
  CHECK(a.kind == Action::Kind::Article);
  CHECK(s.chosen() == WallabagErrorScreen::Chosen::Ok);
}

TEST_CASE("the remove confirmation focuses CANCEL, which is DeleteConfirm's order") {
  // A confirmation whose default is the thing being confirmed is a second press
  // of the button that opened it. The action is recoverable -- the next sync
  // brings the files back -- and the ordering still holds, because what makes it
  // a confirm is having to move to reach the verb.
  ArticlesRemoveConfirmScreen s;
  CHECK(s.isOverlay());
  CHECK(s.focus() == 0);
  CHECK(s.vm().cancelLabel == "CANCEL");
  CHECK(s.vm().confirmLabel == "REMOVE");
  CHECK(s.vm().title == "REMOVE THE DOWNLOADS?");

  CHECK(s.onGesture(ev(Gesture::Activate)).kind == Action::Kind::Pop);
  CHECK(s.chosen() == ArticlesRemoveConfirmScreen::Chosen::None);

  REQUIRE(s.onGesture(ev(Gesture::Next)).kind == Action::Kind::Redraw);
  CHECK(s.onGesture(ev(Gesture::Activate)).kind == Action::Kind::Article);
  CHECK(s.chosen() == ArticlesRemoveConfirmScreen::Chosen::RemoveAll);
}

TEST_CASE("the remove confirmation's footprint is constant, unlike the actions overlay's") {
  // Its panel is a caption, a paragraph and two slabs, and none of those moves
  // with the focus: a slab's fill changes its ink and never its box.
  ArticlesRemoveConfirmScreen s;
  const uint32_t first = s.paintFootprint();
  s.onGesture(ev(Gesture::Next));
  CHECK(s.paintFootprint() == first);
  CHECK(first != 0u);
}
