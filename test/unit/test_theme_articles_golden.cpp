#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "golden.h"
#include "home_vm.h"
#include "ramp.h"
#include "reader/app.h"
#include "reader/framebuffer.h"
#include "reader/screen_article_actions.h"
#include "reader/screen_article_end.h"
#include "reader/screen_articles.h"
#include "reader/screen_home.h"
#include "reader/screen_wallabag_account.h"
#include "reader/screen_wallabag_dialogs.h"
#include "reader/screens.h"
#include "reader/theme_quiet.h"

using namespace reader;

namespace {

// The simulator's own stack, built the same way: the list (or the account
// screen) as the parent, and the overlay pushed over it. What these renders are
// evidence about is the SCREENS, so a Home frame nobody draws would only be a
// slower way to the same framebuffer -- the connect flow's own argument.
struct ArticlesApp {
  DemoScreenFactory factory;
  App app;
  ArticlesApp() : app(std::make_unique<HomeScreen>(demoHomeVm(), demoHomeTargets()), factory) {
    factory.setArticlesDemo();
  }
};

}  // namespace

TEST_CASE("every Articles screen declares Mono, so a golden is one 1-bit frame") {
  // The connect flow's own case, and it matters for the same reason: a screen
  // that quietly acquired Grayscale would pay three waveforms a paint and its
  // golden would stop describing what the panel does.
  CHECK(ArticlesScreen({}, "").fidelity() == Fidelity::Mono);
  CHECK(ArticlesScreen().fidelity() == Fidelity::Mono);
  CHECK(ArticleActionsScreen({1, "T", false}).fidelity() == Fidelity::Mono);
  CHECK(ArticleEndScreen({1, "T", "D", 1, false, 1, true}).fidelity() == Fidelity::Mono);
  CHECK(WallabagAccountScreen({"U", 0, "NEVER", 50, 0, true}).fidelity() == Fidelity::Mono);
  CHECK(WallabagConnectingScreen("h").fidelity() == Fidelity::Mono);
  CHECK(WallabagErrorScreen(WallabagErrorScreen::Shape::SignIn).fidelity() == Fidelity::Mono);
  CHECK(ArticlesRemoveConfirmScreen().fidelity() == Fidelity::Mono);
}

TEST_CASE("QuietTheme renders the Articles flow to golden at both geometries") {
  ramp::Ramp ramp;
  QuietTheme theme;

  auto renderOne = [&](int w, int h, const std::string& name, void (*prime)(ArticlesApp&),
                       ScreenId root, ScreenId over, void (*after)(ArticlesApp&),
                       const std::string& statusLine) {
    ArticlesApp a;
    if (prime != nullptr) prime(a);
    // THE ROW COUNT COMES FROM THE THEME, as the Library's and the picker's do:
    // an unset one leaves the window zero-high and the list renders empty, which
    // is a golden of nothing that passes.
    //
    // AND THE STATUS LINE IS PART OF THE QUESTION. The sync-done variant draws a
    // block between the band and the sync row, so it has one row fewer -- and
    // the FIRST golden of it drew the last row's meta line straight through the
    // hint bar, which is what made the query take this argument.
    a.factory.setArticlesVisibleRows(theme.articlesVisibleRows(h, w, ramp.fonts, statusLine));
    REQUIRE(a.app.pushScreen(root));
    if (over != ScreenId::Count) REQUIRE(a.app.pushScreen(over));
    if (after != nullptr) after(a);
    Framebuffer fb(w, h);
    // App::render, NEVER top().render: an overlay rendered alone is a panel
    // floating on white, and nothing on the desktop but this call can catch it.
    a.app.render(fb, ramp.fonts, theme, Plane::Bw);
    golden::checkGolden(fb, name);
  };

  auto both = [&](const std::string& name, void (*prime)(ArticlesApp&), ScreenId root,
                  ScreenId over = ScreenId::Count, void (*after)(ArticlesApp&) = nullptr,
                  const std::string& statusLine = {}) {
    SUBCASE("X4 480x800") { renderOne(480, 800, name, prime, root, over, after, statusLine); }
    SUBCASE("X3 528x792") {
      renderOne(528, 792, name + "_x3", prime, root, over, after, statusLine);
    }
  };

  SUBCASE("the list") { both("articles", nullptr, ScreenId::Articles); }

  SUBCASE("not set up") {
    both("articles_setup", [](ArticlesApp& a) { a.factory.setArticlesNotSetUp(); },
         ScreenId::Articles);
  }

  SUBCASE("sync done") {
    both("articles_sync_done",
         [](ArticlesApp& a) {
           a.factory.setArticles(demoArticles(), "WALLABAG \xC2\xB7 3 NEW");
           a.factory.setArticlesStatusLine(
               "SYNC COMPLETE \xC2\xB7 3 NEW ARTICLES \xC2\xB7 1 ARCHIVE PUSHED");
         },
         ScreenId::Articles, ScreenId::Count, nullptr,
         "SYNC COMPLETE \xC2\xB7 3 NEW ARTICLES \xC2\xB7 1 ARCHIVE PUSHED");
  }

  SUBCASE("the actions overlay") {
    both("article_actions", nullptr, ScreenId::Articles, ScreenId::ArticleActions);
  }

  SUBCASE("the end screen") {
    both("article_end", nullptr, ScreenId::Articles, ScreenId::ArticleEnd);
  }

  SUBCASE("the account screen") {
    both("wallabag_account", nullptr, ScreenId::WallabagAccount);
  }

  SUBCASE("connecting") {
    both("wallabag_connecting", nullptr, ScreenId::Articles, ScreenId::WallabagConnecting);
  }

  SUBCASE("fetching") {
    // THE SECOND STAGE IS THE SAME SCREEN, set on the instance rather than
    // primed into the factory -- which is the whole point of the stage living in
    // the strings. It is also the only thing in the suite that can tell a
    // rendered `SYNCING...` from a rendered `CONNECTING...`.
    both("wallabag_fetching", nullptr, ScreenId::Articles, ScreenId::WallabagConnecting,
         [](ArticlesApp& a) {
           static_cast<WallabagConnectingScreen&>(a.app.atMut(a.app.depth() - 1))
               .setFetching(3, 12);
         });
  }

  // THREE COPY SHAPES, THREE GOLDENS, on the join flow's reason: the slab list
  // is the shape, so two of these draw a panel one slab shorter and folding them
  // into one golden would pin only whichever was rendered.
  SUBCASE("failed / sign-in") {
    both("wallabag_error", nullptr, ScreenId::Articles, ScreenId::WallabagError);
  }
  SUBCASE("failed / offline") {
    both("wallabag_error_offline",
         [](ArticlesApp& a) {
           a.factory.setWallabagFailure(WallabagErrorScreen::Shape::Offline);
         },
         ScreenId::Articles, ScreenId::WallabagError);
  }
  SUBCASE("failed / no network") {
    both("wallabag_error_no_network",
         [](ArticlesApp& a) {
           a.factory.setWallabagFailure(WallabagErrorScreen::Shape::NoNetwork);
         },
         ScreenId::Articles, ScreenId::WallabagError);
  }

  SUBCASE("remove downloads") {
    // THE ONE OVERLAY IN THIS FLOW WHOSE PARENT IS NOT THE LIST, which is what
    // its board says and what this stack has to reproduce: veiling the wrong
    // parent is invisible to every structural assertion, because a stippled list
    // and a stippled settings screen are both grey.
    both("articles_remove_confirm", nullptr, ScreenId::WallabagAccount,
         ScreenId::ArticlesRemoveConfirm);
  }
}
