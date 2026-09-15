#include <vector>

#include "doctest.h"
#include "reader/screen_articles.h"

using namespace reader;

namespace {

// Five fixture rows, three unread, in the board's own order.
std::vector<ArticleItem> fixture() {
  return {
      {1, "The Death and Life of the Great American Essay", "LONGREADS", 22, false, false},
      {2, "Why We Forget Most of the Books We Read", "THE ATLANTIC", 9, false, false},
      {3, "In Praise of Slow Reading", "AEON", 14, false, false},
      {4, "The Tyranny of the To-Be-Read Pile", "LIT HUB", 7, true, false},
      {5, "E Ink: The Quiet Display Technology That Refused to Die", "IEEE SPECTRUM", 16, true,
       false},
  };
}

GestureEvent ev(Gesture g, int steps = 1, bool held = false) {
  GestureEvent e;
  e.what = g;
  e.steps = steps;
  e.held = held;
  return e;
}

}  // namespace

TEST_CASE("the sync row is -1, above the list, and the focus starts on the first article") {
  // -1 IS THE SYNC ROW. The board fixes it between the band and the list, so it
  // does not scroll and cannot be a member of the ScrollWindow that does.
  // Focus::WithNone already has a position outside the list for exactly this,
  // and Home spends it the same way on its CONTINUE block -- the only difference
  // being that Home's sits BELOW the first item and this one sits above it.
  //
  // THE FOCUS STARTS ON ROW 0 THOUGH, and the board is what says so: it draws
  // row 0 inverted and the sync row plain. A reader who opens this screen has
  // come to read.
  ArticlesScreen s(fixture(), "WALLABAG \xC2\xB7 NO NEW");
  s.setVisibleRows(5);
  CHECK(s.focus() == 0);
  CHECK(s.vm().focusedRow == 0);

  // Up from the first article reaches the sync row rather than wrapping to the
  // bottom, which is what -1 being a real position buys.
  REQUIRE(s.onGesture(ev(Gesture::Prev)).kind == Action::Kind::Redraw);
  CHECK(s.focus() == -1);
  const Action a = s.onGesture(ev(Gesture::Activate));
  CHECK(a.kind == Action::Kind::Article);
  CHECK(s.chosen() == ArticlesScreen::Chosen::Sync);
}

TEST_CASE("Confirm on an article row asks the shell to open it") {
  ArticlesScreen s(fixture(), "WALLABAG \xC2\xB7 NO NEW");
  s.setVisibleRows(5);
  REQUIRE(s.focus() == 0);
  const Action a = s.onGesture(ev(Gesture::Activate));
  CHECK(a.kind == Action::Kind::Open);
  CHECK(s.focusedId() == 1);
  CHECK(s.focusedTitle() == "The Death and Life of the Great American Essay");
}

TEST_CASE("a HOLD on an article row opens the actions overlay, and never on the sync row") {
  ArticlesScreen s(fixture(), "WALLABAG \xC2\xB7 NO NEW");
  s.setVisibleRows(5);
  // On the sync row there is nothing to act on, so the hold does nothing rather
  // than opening an overlay captioned with an article the reader did not choose.
  REQUIRE(s.onGesture(ev(Gesture::Prev)).kind == Action::Kind::Redraw);
  REQUIRE(s.focus() == -1);
  CHECK(s.onGesture(ev(Gesture::Secondary)).kind == Action::Kind::None);
  REQUIRE(s.onGesture(ev(Gesture::Next)).kind == Action::Kind::Redraw);
  const Action a = s.onGesture(ev(Gesture::Secondary));
  CHECK(a.kind == Action::Kind::Push);
  CHECK(a.target == ScreenId::ArticleActions);
}

TEST_CASE("the band counts the UNREAD rows, not the rows") {
  ArticlesScreen s(fixture(), "WALLABAG \xC2\xB7 NO NEW");
  s.setVisibleRows(5);
  CHECK(s.vm().bandValue == "3 UNREAD");
  CHECK_FALSE(s.vm().notSetUp);
  // And the read rows carry it into the row itself, which is what draws the
  // hollow bullet -- a flag, not the theme reading the end of `meta`.
  CHECK_FALSE(s.vm().rows[0].read);
  CHECK(s.vm().rows[3].read);
  CHECK(s.vm().rows[3].meta == "LIT HUB \xC2\xB7 7 MIN \xC2\xB7 READ");
  CHECK(s.vm().rows[0].meta == "LONGREADS \xC2\xB7 22 MIN");
}

TEST_CASE("the hint bar promises a hold on Confirm and nothing else") {
  ArticlesScreen s(fixture(), "WALLABAG \xC2\xB7 NO NEW");
  s.setVisibleRows(5);
  CHECK(s.vm().hints[0] == "BACK");
  CHECK(s.vm().hints[1] == "READ");
  CHECK(s.vm().hints[2] == "UP");
  CHECK(s.vm().hints[3] == "DOWN");
  CHECK_FALSE(s.vm().holds[0]);
  CHECK(s.vm().holds[1]);
  CHECK_FALSE(s.vm().holds[2]);
  CHECK_FALSE(s.vm().holds[3]);
  // A hold and an auto-repeat are mutually exclusive per button, so the repeat
  // is on the two movers and not on Confirm.
  CHECK(maskHas(s.autoRepeat(), Button::Up));
  CHECK(maskHas(s.autoRepeat(), Button::Down));
  CHECK_FALSE(maskHas(s.autoRepeat(), Button::Confirm));
  CHECK(maskHas(s.longPressable(), Button::Confirm));
}

TEST_CASE("the not-set-up variant is one flag, and every gesture but Back is refused") {
  // HomeEmpty's rule: a variant is what this struct SAYS, never which function
  // draws it. There is nothing to sync and nothing to read, so a Confirm that
  // latched would be a press with no work behind it.
  ArticlesScreen s;
  CHECK(s.vm().notSetUp);
  CHECK(s.vm().bandValue == "NOT SET UP");
  CHECK(s.vm().rows.empty());
  CHECK(s.vm().syncStamp.empty());
  CHECK(s.vm().hints[0] == "BACK");
  CHECK(s.vm().hints[1].empty());
  CHECK(s.vm().hints[2].empty());
  CHECK(s.vm().hints[3].empty());

  CHECK(s.onGesture(ev(Gesture::Activate)).kind == Action::Kind::None);
  CHECK(s.onGesture(ev(Gesture::Secondary)).kind == Action::Kind::None);
  CHECK(s.onGesture(ev(Gesture::Next)).kind == Action::Kind::None);
  CHECK(s.onGesture(ev(Gesture::Prev)).kind == Action::Kind::None);
  CHECK(s.onGesture(ev(Gesture::Back)).kind == Action::Kind::Pop);
  // The prose is on the model, not in the theme: the board owns the words.
  CHECK_FALSE(s.vm().setupTitle.empty());
  CHECK_FALSE(s.vm().setupProse.empty());
  CHECK_FALSE(s.vm().setupNote.empty());
}

TEST_CASE("configured with no articles is neither variant: the sync row and an empty list") {
  // The state a reader is in the moment they fill the file in and before they
  // press anything. It must NOT be the not-set-up screen -- that would tell them
  // to go and edit a file they have just edited.
  ArticlesScreen s({}, "WALLABAG \xC2\xB7 NEVER");
  s.setVisibleRows(5);
  CHECK_FALSE(s.vm().notSetUp);
  CHECK(s.vm().bandValue == "0 UNREAD");
  CHECK(s.vm().rows.empty());
  CHECK(s.vm().syncStamp == "WALLABAG \xC2\xB7 NEVER");
  CHECK(s.focus() == -1);
  CHECK(s.onGesture(ev(Gesture::Activate)).kind == Action::Kind::Article);
  CHECK(s.chosen() == ArticlesScreen::Chosen::Sync);
  // Down from the sync row has nowhere to go and must not wrap onto itself in a
  // way that reads as a dead button: with no rows the focus simply stays.
  CHECK(s.focus() == -1);
}

TEST_CASE("the slice moves as the Library's does") {
  ArticlesScreen s(fixture(), "WALLABAG \xC2\xB7 NO NEW");
  s.setVisibleRows(2);
  CHECK(s.vm().totalRows == 5);
  CHECK(s.vm().rows.size() == 2);
  CHECK(s.vm().firstRow == 0);

  for (int i = 0; i < 2; ++i) s.onGesture(ev(Gesture::Next));
  REQUIRE(s.focus() == 2);
  // Scrolled by the overflow rather than by a page, which is ScrollWindow's rule:
  // the focus lands on the window's BOTTOM edge arriving from above.
  CHECK(s.vm().firstRow == 1);
  CHECK(s.vm().focusedRow == 1);
  CHECK(s.vm().rows[0].title == "Why We Forget Most of the Books We Read");
}

TEST_CASE("the sync-done variant is the stamp and a status line, and nothing else") {
  ArticlesScreen s(fixture(), "WALLABAG \xC2\xB7 3 NEW");
  s.setVisibleRows(5);
  CHECK(s.vm().statusLine.empty());
  s.setStatusLine("SYNC COMPLETE \xC2\xB7 3 NEW ARTICLES \xC2\xB7 1 ARCHIVE PUSHED");
  CHECK_FALSE(s.vm().statusLine.empty());
  CHECK(s.vm().syncStamp == "WALLABAG \xC2\xB7 3 NEW");
  // Same rows, same focus, same everything else: a variant is what the model
  // says and never a second screen.
  CHECK(s.vm().rows.size() == 5);
  CHECK(s.id() == ScreenId::Articles);
}

// --- built from the card -----------------------------------------------------

#include "fake_fs.h"
#include "reader/article_store.h"
#include "reader/reading_position.h"
#include "reader/reading_store.h"
#include "reader/settings.h"
#include "reader/screen_wallabag_account.h"
#include "reader/screens.h"
#include "reader/wallabag_credentials.h"

namespace {

void writeCredentials(FakeFileSystem& fs) {
  REQUIRE(fs.writeAll("/.reader/wallabag.json",
                      "{\"server\":\"http://w.lan\",\"clientId\":\"a\",\"clientSecret\":\"b\""
                      ",\"username\":\"lucasg\",\"password\":\"p\"}"));
}

void writeArticle(FakeFileSystem& fs, int id, const std::string& updated,
                  const std::string& title, bool starred = false) {
  ArticleStore s(fs);
  REQUIRE(s.writeMeta({id, title, "LONGREADS", 12, starred, false, updated}));
  REQUIRE(fs.writeAll(s.epubPath(id), "PK"));
}

void finish(FakeFileSystem& fs, int id) {
  ArticleStore s(fs);
  ReadingPosition p;
  p.bookPath = s.epubPath(id);
  p.bookBytes = 2;
  p.finished = true;
  REQUIRE(savePosition(fs, p) != SaveResult::Failed);
}

}  // namespace

TEST_CASE("over a card: three sidecars list newest first, with READ marked") {
  FakeFileSystem fs;
  writeCredentials(fs);
  writeArticle(fs, 1, "2026-09-01T10:00:00Z", "Oldest");
  writeArticle(fs, 2, "2026-09-03T10:00:00Z", "Newest");
  writeArticle(fs, 3, "2026-09-02T10:00:00Z", "Middle");
  finish(fs, 3);

  ArticlesScreen s(fs);
  s.setVisibleRows(5);
  REQUIRE(s.vm().rows.size() == 3);
  CHECK(s.vm().rows[0].title == "Newest");
  CHECK(s.vm().rows[1].title == "Middle");
  CHECK(s.vm().rows[2].title == "Oldest");
  CHECK(s.vm().rows[1].read);
  CHECK_FALSE(s.vm().rows[0].read);
  CHECK(s.vm().bandValue == "2 UNREAD");
  CHECK_FALSE(s.vm().notSetUp);
}

TEST_CASE("over a card: no credentials is the not-set-up variant, whatever is in the directory") {
  // THE CREDENTIALS DECIDE, NEVER THE ROW COUNT. A card with articles left from
  // a previous account and no credentials file is still "nobody has set this
  // up", and drawing a list the reader cannot sync would be worse.
  FakeFileSystem fs;
  writeArticle(fs, 1, "2026-09-01T10:00:00Z", "Left over");
  ArticlesScreen s(fs);
  CHECK(s.vm().notSetUp);
  CHECK(s.vm().bandValue == "NOT SET UP");
  CHECK(s.vm().rows.empty());
}

TEST_CASE("over a card: credentials and no articles is the LIST, not the setup screen") {
  // The state a reader is in the moment they fill the file in. Telling them to
  // go and fill in a file they have just filled in is the defect this separates.
  FakeFileSystem fs;
  writeCredentials(fs);
  ArticlesScreen s(fs);
  s.setVisibleRows(5);
  CHECK_FALSE(s.vm().notSetUp);
  CHECK(s.vm().bandValue == "0 UNREAD");
  CHECK(s.vm().rows.empty());
  CHECK(s.vm().syncStamp == "WALLABAG \xC2\xB7 NEVER");
}

TEST_CASE("over a card: the stamp is the watermark's outcome, in four words") {
  FakeFileSystem fs;
  writeCredentials(fs);
  ArticleStore store(fs);
  struct Case { const char* stored; const char* shown; };
  const Case cases[] = {
      {"never", "WALLABAG \xC2\xB7 NEVER"},
      {"upToDate", "WALLABAG \xC2\xB7 NO NEW"},
      {"new:3", "WALLABAG \xC2\xB7 3 NEW"},
      {"new:100", "WALLABAG \xC2\xB7 100 NEW"},
      {"failed", "WALLABAG \xC2\xB7 FAILED"},
      // A watermark from a newer firmware must not make this screen say
      // something false; NEVER is the safe reading of a word we cannot parse.
      {"something-else", "WALLABAG \xC2\xB7 NEVER"},
      // `new:0` cannot be written by the engine (it writes upToDate), so this is
      // a malformed record -- and the honest word beats a zero to interpret.
      {"new:0", "WALLABAG \xC2\xB7 NO NEW"},
  };
  for (const Case& c : cases) {
    CAPTURE(c.stored);
    SyncWatermark w;
    w.lastOutcome = c.stored;
    REQUIRE(store.saveWatermark(w));
    ArticlesScreen s(fs);
    CHECK(s.vm().syncStamp == c.shown);
  }
}

TEST_CASE("rescan re-reads the directory and carries the focus") {
  FakeFileSystem fs;
  writeCredentials(fs);
  for (int i = 1; i <= 4; ++i)
    writeArticle(fs, i, "2026-09-0" + std::to_string(i) + "T10:00:00Z", "A" + std::to_string(i));
  ArticlesScreen s(fs);
  s.setVisibleRows(5);
  REQUIRE(s.vm().rows.size() == 4);
  REQUIRE(s.onGesture(ev(Gesture::Next)).kind == Action::Kind::Redraw);
  REQUIRE(s.focus() == 1);

  ArticleStore store(fs);
  REQUIRE(store.queueArchive(4));  // the NEWEST, so row 0 goes
  CHECK(s.rescan());
  CHECK(s.vm().rows.size() == 3);
  // Carried, not reset: a sync that removed rows must not move a selection the
  // reader did not touch any further than it has to.
  CHECK(s.focus() == 1);
  CHECK_FALSE(s.rescan());
}

TEST_CASE("refreshProgress marks READ without re-listing the articles directory") {
  FakeFileSystem fs;
  writeCredentials(fs);
  writeArticle(fs, 1, "2026-09-01T10:00:00Z", "One");
  ArticlesScreen s(fs);
  s.setVisibleRows(5);
  REQUIRE_FALSE(s.vm().rows[0].read);
  CHECK_FALSE(s.refreshProgress());

  finish(fs, 1);
  CHECK(s.refreshProgress());
  CHECK(s.vm().rows[0].read);
  CHECK(s.vm().bandValue == "0 UNREAD");
  CHECK_FALSE(s.refreshProgress());
}

TEST_CASE("the account screen reads the card, and one watermark feeds both screens") {
  FakeFileSystem fs;
  writeCredentials(fs);
  writeArticle(fs, 1, "2026-09-01T10:00:00Z", "One");
  writeArticle(fs, 2, "2026-09-02T10:00:00Z", "Two");
  ArticleStore store(fs);
  REQUIRE(store.queueStar(1, true));
  SyncWatermark w;
  w.lastOutcome = "upToDate";
  REQUIRE(store.saveWatermark(w));

  Settings settings;
  settings.articlesKeepOffline = 100;
  WallabagAccountScreen a(fs, settings);
  CHECK(a.vm().bandValue == "SIGNED IN");
  CHECK(a.vm().rows[0].value == "lucasg");
  CHECK(a.vm().rows[1].value == "2 ARTICLES");
  CHECK(a.vm().rows[2].value == "NO NEW");
  CHECK(a.vm().rows[3].value == "NEWEST 100");
  CHECK(a.vm().rows[4].value == "1 TO PUSH");

  // THE SAME FOUR WORDS THE LIST DRAWS, from the same watermark through the same
  // formatter -- two screens naming one fact differently is two spellings of it.
  ArticlesScreen l(fs);
  CHECK(l.vm().syncStamp == "WALLABAG \xC2\xB7 NO NEW");
}

TEST_CASE("the account screen says NOT SET UP with no credentials") {
  FakeFileSystem fs;
  writeArticle(fs, 1, "2026-09-01T10:00:00Z", "One");
  WallabagAccountScreen a(fs, Settings{});
  CHECK(a.vm().bandValue == "NOT SET UP");
  CHECK(a.vm().rows[0].value.empty());
  CHECK_FALSE(a.vm().rows[6].focusable);
}

TEST_CASE("the account screen's refresh picks up a queue push and leaves the cycled value") {
  FakeFileSystem fs;
  writeCredentials(fs);
  writeArticle(fs, 1, "2026-09-01T10:00:00Z", "One");
  Settings settings;
  WallabagAccountScreen a(fs, settings);
  REQUIRE(a.vm().rows[4].value == "0 TO PUSH");

  // The cycle moves the value HERE, and a refresh must not undo a press the
  // reader has already seen take effect -- the shell commits it afterwards.
  REQUIRE(a.onGesture(ev(Gesture::Activate)).kind == Action::Kind::Article);
  REQUIRE(a.keepOffline() == 100);

  ArticleStore(fs).queueStar(1, true);
  CHECK(a.refresh());
  CHECK(a.vm().rows[4].value == "1 TO PUSH");
  CHECK(a.keepOffline() == 100);
  CHECK_FALSE(a.refresh());
}

TEST_CASE("the factory builds both from the card when it has one, and refuses when it has neither") {
  // Their `Restore::Ready` declaration rests on exactly this: the factory holds
  // a FileSystem* for them as it holds one for the Library, so a wake rebuilds
  // both off the card.
  FakeFileSystem fs;
  writeCredentials(fs);
  writeArticle(fs, 1, "2026-09-01T10:00:00Z", "One");

  DemoScreenFactory f;
  CHECK(f.create(ScreenId::Articles) == nullptr);
  CHECK(f.create(ScreenId::WallabagAccount) == nullptr);

  f.setArticleStore(&fs);
  f.setArticlesVisibleRows(5);
  auto list = f.create(ScreenId::Articles);
  REQUIRE(list != nullptr);
  CHECK(static_cast<ArticlesScreen&>(*list).vm().rows.size() == 1);
  auto account = f.create(ScreenId::WallabagAccount);
  REQUIRE(account != nullptr);
  CHECK(static_cast<WallabagAccountScreen&>(*account).vm().bandValue == "SIGNED IN");

  // THE DEMO CLEARS THE CARD POINTER, so a fixture cannot be quietly overlaid on
  // a real card -- which is the substitution this flow's refusals prevent, in
  // the other direction.
  f.setArticlesDemo();
  auto demo = f.create(ScreenId::Articles);
  REQUIRE(demo != nullptr);
  CHECK(static_cast<ArticlesScreen&>(*demo).vm().rows.size() == 5);
}

TEST_CASE("a card-backed list draws NOTHING until it is told how many rows fit") {
  // THIS IS THE DEFECT, PINNED AS A PROPERTY RATHER THAN FIXED HERE. Reported off
  // the device after the first successful sync: the article downloaded, Home said
  // `1 UNREAD`, and the list was empty with `Sync now` doing nothing.
  //
  // The cause is a setter with no caller. `ArticlesScreen(FileSystem&)` starts
  // from `FocusScreen(0, 0, ...)` and `load()` CARRIES `visibleRows` forward --
  // deliberately, so a rescan cannot throw away what the shell set -- which on a
  // fresh construction means carrying the base class's zero. The factory has
  // `setArticlesVisibleRows` and the shell never called it, so the guard
  // `articlesRows_ > 0` skipped it and the window stayed zero high.
  //
  // EVERY OTHER CARD-BACKED CASE IN THIS FILE CALLS `setVisibleRows(5)` ON THE
  // NEXT LINE, which is precisely why none of them could see it: the tests always
  // told it and the shell never did. So this one deliberately does not.
  FakeFileSystem fs;
  writeCredentials(fs);
  writeArticle(fs, 1, "2026-09-01T10:00:00Z", "Only");

  ArticlesScreen s(fs);

  // The MODEL has the article -- `unreadCount()` and Home's row agree, which is
  // why the device said 1 UNREAD while showing nothing.
  CHECK_FALSE(s.vm().notSetUp);
  CHECK(s.vm().bandValue == "1 UNREAD");
  // ...and the VIEW has no rows at all, the sync row included. That second half
  // is what made the symptom confusing: a reader pressing Confirm hit the
  // article, not `Sync now`, so the sync appeared dead too.
  CHECK(s.vm().rows.empty());

  // One call is the whole fix, and it is the shell's to make.
  s.setVisibleRows(5);
  REQUIRE(s.vm().rows.size() == 1);
  CHECK(s.vm().rows[0].title == "Only");
}

TEST_CASE("the sync row is focusable, says so, and the Confirm hint follows it") {
  // THREE REPORTS OFF THE DEVICE, ONE CAUSE. "The sync now row doesn't look
  // focused, even when it is"; "on an empty article list, sync now looks
  // unfocused"; "...and the hint bar says READ, even though no article is
  // selected". `focusedRow` is an index into the VISIBLE ARTICLE ROWS, so -1
  // says "no article" and cannot say where the focus went instead -- a renderer
  // reading only that draws a screen with nothing selected at all.
  FakeFileSystem fs;
  writeCredentials(fs);

  SUBCASE("with articles: -1 is the sync row, 0 is the first article") {
    writeArticle(fs, 1, "2026-09-01T10:00:00Z", "Only");
    ArticlesScreen s(fs);
    s.setVisibleRows(5);

    // The constructor lands on the first article, so READ is right there.
    REQUIRE(s.focus() == 0);
    CHECK_FALSE(s.vm().syncFocused);
    CHECK(s.vm().hints[1] == "READ");
    // ...and both movers are live, because there are two places to be.
    CHECK(s.vm().hints[2] == "UP");
    CHECK(s.vm().hints[3] == "DOWN");

    s.onGesture({Gesture::Prev, 1, false});
    CHECK(s.focus() == -1);
    CHECK(s.vm().syncFocused);
    CHECK(s.vm().focusedRow == -1);
    CHECK(s.vm().hints[1] == "SYNC");
  }

  SUBCASE("with none: the sync row is the only row and always has the focus") {
    ArticlesScreen s(fs);
    s.setVisibleRows(5);
    CHECK(s.vm().rows.empty());
    CHECK(s.vm().syncFocused);
    CHECK(s.vm().hints[1] == "SYNC");
    // THE MOVERS GO QUIET, WifiSettingsEmpty's rule: one focusable row means UP
    // and DOWN would promise a press that changes nothing.
    CHECK(s.vm().hints[2].empty());
    CHECK(s.vm().hints[3].empty());
  }

  SUBCASE("not set up: no sync row to focus, so it claims none") {
    FakeFileSystem bare;
    ArticlesScreen s(bare);
    s.setVisibleRows(5);
    REQUIRE(s.vm().notSetUp);
    CHECK_FALSE(s.vm().syncFocused);
  }
}
