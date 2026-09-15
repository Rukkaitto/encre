#include <memory>

#include "doctest.h"
#include "home_vm.h"
#include "reader/app.h"
#include "reader/screen_article_actions.h"
#include "reader/screen_article_end.h"
#include "reader/screen_articles.h"
#include "reader/screen_home.h"
#include "reader/screen_wallabag_account.h"
#include "reader/screen_wallabag_dialogs.h"
#include "reader/screens.h"

using namespace reader;

namespace {

// THE CONTRACT THIS FILE EXISTS FOR: a screen's own test asserts what its
// `onGesture` RETURNS, and that was correct for all five connect-flow screens
// while the shell could not read a single one of their outcomes. What decides it
// is what survives `App::dispatch` -- so every case here goes through a real
// one, with a real factory, and asks the stack afterwards.
struct Flow {
  DemoScreenFactory factory;
  App app;
  Flow() : app(std::make_unique<HomeScreen>(demoHomeVm(), demoHomeTargets()), factory) {
    factory.setArticlesDemo();
    factory.setArticlesVisibleRows(5);
  }
};

const InputEvent kConfirm{Button::Confirm, PressKind::Short};
const InputEvent kHoldConfirm{Button::Confirm, PressKind::Long};
const InputEvent kBack{Button::Back, PressKind::Short};
const InputEvent kDown{Button::Down, PressKind::Short};
const InputEvent kUp{Button::Up, PressKind::Short};

}  // namespace

TEST_CASE("Sync now latches and leaves the list standing, so the shell can ask it") {
  Flow f;
  REQUIRE(f.app.pushScreen(ScreenId::Articles));
  const int depth = f.app.depth();
  // Up from the first article reaches the sync row.
  f.app.dispatch(kUp);
  REQUIRE(f.app.top().focus() == -1);
  f.app.clearDirty();

  f.app.dispatch(kConfirm);
  CHECK(f.app.articleRequested());
  // NOTHING IS POPPED, which is the whole contract: after a pop there is no
  // screen left to ask, and dispatch's Pop DESTROYS it.
  CHECK(f.app.depth() == depth);
  CHECK(f.app.top().id() == ScreenId::Articles);
  CHECK(static_cast<ArticlesScreen&>(f.app.top()).chosen() == ArticlesScreen::Chosen::Sync);
  // And nothing repainted on its own: what a sync changes on glass is the
  // shell's to decide.
  CHECK_FALSE(f.app.dirty());
}

TEST_CASE("Confirm on an article row asks the shell to open it, and pops nothing") {
  Flow f;
  REQUIRE(f.app.pushScreen(ScreenId::Articles));
  REQUIRE(f.app.top().focus() == 0);
  f.app.dispatch(kConfirm);
  CHECK(f.app.openRequested());
  CHECK_FALSE(f.app.articleRequested());
  CHECK(f.app.top().id() == ScreenId::Articles);
}

TEST_CASE("a HOLD on a row pushes the overlay, and its rows latch without popping") {
  Flow f;
  REQUIRE(f.app.pushScreen(ScreenId::Articles));
  const int listDepth = f.app.depth();
  f.app.dispatch(kHoldConfirm);
  REQUIRE(f.app.top().id() == ScreenId::ArticleActions);
  REQUIRE(f.app.depth() == listDepth + 1);

  f.app.dispatch(kConfirm);
  CHECK(f.app.articleRequested());
  CHECK(f.app.depth() == listDepth + 1);
  CHECK(f.app.top().id() == ScreenId::ArticleActions);
  CHECK(static_cast<ArticleActionsScreen&>(f.app.top()).chosen() ==
        ArticleActionsScreen::Chosen::Archive);

  f.app.clearArticleRequest();
  f.app.dispatch(kDown);
  f.app.dispatch(kConfirm);
  CHECK(f.app.articleRequested());
  CHECK(static_cast<ArticleActionsScreen&>(f.app.top()).chosen() ==
        ArticleActionsScreen::Chosen::Star);

  // Back is navigation and pops itself -- the rule that a screen latches when
  // the shell has work and pops when it has not.
  f.app.clearArticleRequest();
  f.app.dispatch(kBack);
  CHECK(f.app.top().id() == ScreenId::Articles);
  CHECK_FALSE(f.app.articleRequested());
}

TEST_CASE("every slab on the end screen latches, and Back returns to the page") {
  Flow f;
  REQUIRE(f.app.pushScreen(ScreenId::Articles));
  REQUIRE(f.app.pushScreen(ScreenId::ArticleEnd));
  const int depth = f.app.depth();

  using C = ArticleEndScreen::Chosen;
  const C expected[] = {C::Archive, C::Star, C::NextArticle, C::BackToList};
  for (int i = 0; i < 4; ++i) {
    CAPTURE(i);
    while (f.app.top().focus() != i) f.app.dispatch(kDown);
    f.app.clearArticleRequest();
    f.app.dispatch(kConfirm);
    CHECK(f.app.articleRequested());
    CHECK(f.app.depth() == depth);
    CHECK(static_cast<ArticleEndScreen&>(f.app.top()).chosen() == expected[i]);
  }

  f.app.clearArticleRequest();
  f.app.dispatch(kBack);
  CHECK(f.app.top().id() == ScreenId::Articles);
  CHECK_FALSE(f.app.articleRequested());
}

TEST_CASE("Back on the connecting dialog latches and does NOT pop") {
  // The case Action::wifi()'s rule was written for: a sync is in flight and the
  // engine has to be told, so this is not navigation. A pop here would destroy
  // the screen the shell is about to ask.
  Flow f;
  REQUIRE(f.app.pushScreen(ScreenId::Articles));
  REQUIRE(f.app.pushScreen(ScreenId::WallabagConnecting));
  const int depth = f.app.depth();
  f.app.dispatch(kBack);
  CHECK(f.app.articleRequested());
  CHECK(f.app.depth() == depth);
  CHECK(f.app.top().id() == ScreenId::WallabagConnecting);
  CHECK(static_cast<WallabagConnectingScreen&>(f.app.top()).cancelled());
}

TEST_CASE("the failure dialog's slab and its Back both latch") {
  Flow f;
  f.factory.setWallabagFailure(WallabagErrorScreen::Shape::Offline);
  REQUIRE(f.app.pushScreen(ScreenId::Articles));
  REQUIRE(f.app.pushScreen(ScreenId::WallabagError));
  const int depth = f.app.depth();

  f.app.dispatch(kConfirm);
  CHECK(f.app.articleRequested());
  CHECK(f.app.depth() == depth);
  CHECK(static_cast<WallabagErrorScreen&>(f.app.top()).chosen() ==
        WallabagErrorScreen::Chosen::TryAgain);

  f.app.clearArticleRequest();
  f.app.dispatch(kBack);
  CHECK(f.app.articleRequested());
  CHECK(f.app.depth() == depth);
}

TEST_CASE("the account screen's cycling row latches and its disclosing row pushes") {
  Flow f;
  REQUIRE(f.app.pushScreen(ScreenId::WallabagAccount));
  const int depth = f.app.depth();
  REQUIRE(f.app.top().focus() == 3);

  f.app.dispatch(kConfirm);
  CHECK(f.app.articleRequested());
  CHECK(f.app.depth() == depth);
  CHECK(static_cast<WallabagAccountScreen&>(f.app.top()).chosen() ==
        WallabagAccountScreen::Chosen::KeepOffline);

  f.app.clearArticleRequest();
  f.app.dispatch(kDown);
  f.app.dispatch(kConfirm);
  // A PUSH, NOT A LATCH: nothing has happened yet, and the confirmation is what
  // asks. Its REMOVE slab is where the latch lives.
  CHECK_FALSE(f.app.articleRequested());
  CHECK(f.app.top().id() == ScreenId::ArticlesRemoveConfirm);
  CHECK(f.app.depth() == depth + 1);
}

TEST_CASE("the remove confirmation latches on REMOVE and pops on CANCEL") {
  Flow f;
  REQUIRE(f.app.pushScreen(ScreenId::WallabagAccount));
  REQUIRE(f.app.pushScreen(ScreenId::ArticlesRemoveConfirm));
  const int depth = f.app.depth();

  // The focus starts on CANCEL, which is DeleteConfirm's order: a confirmation
  // whose default is the thing being confirmed is a second press of the button
  // that opened it.
  REQUIRE(f.app.top().focus() == 0);
  f.app.dispatch(kDown);
  f.app.dispatch(kConfirm);
  CHECK(f.app.articleRequested());
  CHECK(f.app.depth() == depth);
  CHECK(static_cast<ArticlesRemoveConfirmScreen&>(f.app.top()).chosen() ==
        ArticlesRemoveConfirmScreen::Chosen::RemoveAll);

  f.app.clearArticleRequest();
  f.app.dispatch(kUp);
  f.app.dispatch(kConfirm);
  CHECK_FALSE(f.app.articleRequested());
  CHECK(f.app.top().id() == ScreenId::WallabagAccount);
}

TEST_CASE("an article outcome is never read as one of the other six latches") {
  // The shell branches on exactly one of the seven per iteration, so a latch
  // that fired two of them would have it doing two jobs for one press.
  Flow f;
  REQUIRE(f.app.pushScreen(ScreenId::Articles));
  f.app.dispatch(kUp);
  f.app.dispatch(kConfirm);
  REQUIRE(f.app.articleRequested());
  CHECK_FALSE(f.app.wifiRequested());
  CHECK_FALSE(f.app.deleteRequested());
  CHECK_FALSE(f.app.finishRequested());
  CHECK_FALSE(f.app.openRequested());
  CHECK_FALSE(f.app.retryRequested());
  CHECK_FALSE(f.app.sleepRequested());
}
