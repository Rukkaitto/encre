#include "doctest.h"
#include "reader/screen_home.h"
#include "reader/screens.h"

using namespace reader;

TEST_CASE("the demo catalogue reaches Settings from Home, and Back unwinds") {
  // This used to walk on into the Input Monitor, which 2C-3 deleted with
  // StubScreen -- it was reachable only from the stub's first row and no board
  // ever listed it. What is left is the navigation that survives: Home's SETTINGS
  // row opens the real screen, and Back unwinds to a root that cannot be popped.
  DemoScreenFactory f;
  App app(std::make_unique<HomeScreen>(demoHomeVm(), demoHomeTargets()), f);
  const InputEvent down{Button::Down, PressKind::Short};
  const InputEvent confirm{Button::Confirm, PressKind::Short};
  const InputEvent back{Button::Back, PressKind::Short};

  app.dispatch(down);     // focus LIBRARY -- Home's focus starts before its menu
  app.dispatch(down);     // focus SETTINGS
  app.dispatch(confirm);  // push Settings
  REQUIRE(app.top().id() == ScreenId::Settings);
  CHECK(app.top().fidelity() == Fidelity::Mono);
  // Nothing here promises a hold: every focusable row edits in place, so the hint
  // bar has no ring and longPressable must agree with it.
  CHECK(app.top().longPressable() == 0);

  app.dispatch(back);
  CHECK(app.top().id() == ScreenId::Home);
  CHECK(app.depth() == 1);
  app.dispatch(back);  // Home's Back is inert; the root must survive
  CHECK(app.depth() == 1);
}

TEST_CASE("Home is never rebuilt by the factory") {
  // Popping back to Home must return the ORIGINAL screen with its focus, not a
  // fresh one -- which is why create(Home) is null.
  DemoScreenFactory f;
  CHECK(f.create(ScreenId::Home) == nullptr);
}

TEST_CASE("the factory refuses a BookEnd nothing primed") {
  // A REFUSED PUSH LEAVES THE READER STANDING. The alternative -- falling back to the
  // board's Middlemarch -- is the substitution this project has shipped twice, once
  // waking the device into a book the user was not reading and once showing one book's
  // chapters over another's.
  DemoScreenFactory f;
  CHECK(f.create(ScreenId::BookEnd) == nullptr);
}

TEST_CASE("the factory refuses a BookError nothing primed") {
  // THE SAME RULE, and the reason is sharper here: this dialog NAMES A FILE. A
  // substituted one would tell the reader a book they did not try to open is damaged,
  // and its `DELETE FILE...` slab would then offer to remove that book. A refused
  // push leaves the parent standing -- the Library, or Home on the CONTINUE path --
  // which is wrong in a way the reader can see through, and the shell logs why.
  //
  // The refusal was exercised only indirectly, through the simulator's own guard on
  // an unprimed push. This asks the factory.
  DemoScreenFactory f;
  CHECK(f.create(ScreenId::BookError) == nullptr);
  // ...and it is the PRIMING that lifts it, not the mere existence of a demo: the
  // simulator has to ask for the board's content by name, exactly as setBookEndDemo
  // and setReaderDemo are asked for.
  f.setBookErrorFacts(demoBookErrorFacts());
  CHECK(f.create(ScreenId::BookError) != nullptr);
  // An EMPTY display name is representable and does not mean "nothing primed it",
  // which is why the factory keeps its own flag rather than inferring one from the
  // facts. Clearing is the only thing that puts the refusal back.
  f.setBookErrorFacts({});
  CHECK(f.create(ScreenId::BookError) != nullptr);
  f.clearBookErrorFacts();
  CHECK(f.create(ScreenId::BookError) == nullptr);
}

TEST_CASE("a primed BookEnd states the facts it was given") {
  DemoScreenFactory f;
  BookEndScreen::Facts facts;
  facts.bookTitle = "Walden";
  facts.author = "Henry David Thoreau";
  facts.chapterCount = 18;
  facts.libraryBeneath = false;
  f.setBookEndFacts(facts);

  auto s = f.create(ScreenId::BookEnd);
  REQUIRE(s != nullptr);
  const auto& vm = static_cast<BookEndScreen*>(s.get())->vm();
  CHECK(vm.byline == "Walden \xC2\xB7 Henry David Thoreau");
  CHECK(vm.meta == "18 CHAPTERS");
  // No Library beneath, so the slab names where it actually lands.
  CHECK(vm.leaveLabel == "BACK TO HOME");
}

TEST_CASE("the demo BookEnd is the board's own content") {
  DemoScreenFactory f;
  f.setBookEndDemo();
  auto s = f.create(ScreenId::BookEnd);
  REQUIRE(s != nullptr);
  const auto& vm = static_cast<BookEndScreen*>(s.get())->vm();
  // design/Main.dc.html gives this same demo book `CH. 01 OF 24`, and two boards
  // drawing one demo book must agree.
  CHECK(vm.meta == "24 CHAPTERS");
  CHECK(vm.leaveLabel == "BACK TO LIBRARY");
}
