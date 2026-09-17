#include <string>

#include "doctest.h"
#include "reader/screens.h"
#include "reader/screen_home.h"
#include "reader/text.h"

using namespace reader;

namespace {

HomeViewModel vmWithTwoRows() {
  HomeViewModel vm;
  vm.title = "Middlemarch";
  vm.menu = {{"LIBRARY", "12"}, {"SETTINGS", ""}};
  vm.focusedMenuIndex = -1;
  vm.hints = {"READ", "SELECT", "UP", "DOWN"};
  return vm;
}

HomeScreen makeHome() {
  return HomeScreen(vmWithTwoRows(), {ScreenId::Library, ScreenId::Settings});
}

const InputEvent kDown{Button::Down, PressKind::Short};
const InputEvent kUp{Button::Up, PressKind::Short};
const InputEvent kConfirm{Button::Confirm, PressKind::Short};
const InputEvent kBack{Button::Back, PressKind::Short};

}  // namespace

TEST_CASE("down walks from Continue into the menu and wraps back to it") {
  HomeScreen h = makeHome();
  CHECK(h.focus() == -1);
  CHECK(h.onEvent(kDown).kind == Action::Kind::Redraw);
  CHECK(h.focus() == 0);
  CHECK(h.onEvent(kDown).kind == Action::Kind::Redraw);
  CHECK(h.focus() == 1);
  // CONTINUE is in the ring, not a wall before it: it is a position the user can
  // be in, so Down off the last row returns to it rather than to row 0.
  CHECK(h.onEvent(kDown).kind == Action::Kind::Redraw);
  CHECK(h.focus() == -1);
}

TEST_CASE("up from Continue wraps to the last menu row") {
  HomeScreen h = makeHome();
  REQUIRE(h.focus() == -1);
  CHECK(h.onEvent(kUp).kind == Action::Kind::Redraw);
  CHECK(h.focus() == 1);
  CHECK(h.onEvent(kUp).kind == Action::Kind::Redraw);
  CHECK(h.focus() == 0);
  CHECK(h.onEvent(kUp).kind == Action::Kind::Redraw);
  CHECK(h.focus() == -1);
}

TEST_CASE("confirm on a menu row pushes that row's screen") {
  HomeScreen h = makeHome();
  h.onEvent(kDown);
  Action a = h.onEvent(kConfirm);
  CHECK(a.kind == Action::Kind::Push);
  CHECK(a.target == ScreenId::Library);
  h.onEvent(kDown);
  a = h.onEvent(kConfirm);
  CHECK(a.kind == Action::Kind::Push);
  CHECK(a.target == ScreenId::Settings);
}

// These two cases used to assert that CONTINUE and READ did NOTHING -- "the Reader
// is Phase 3". It is not any more, and a CONTINUE slab that draws and does nothing is
// the dead-button defect this project has shipped twice. They pin the action now.

TEST_CASE("confirm on Continue asks to open the book") {
  HomeScreen h = makeHome();
  REQUIRE(h.focus() == -1);
  // Action::open(), not a push: opening a book reads a file off the card, and
  // storage is not core/'s. The shell resolves WHICH book.
  CHECK(h.onEvent(kConfirm).kind == Action::Kind::Open);
}

TEST_CASE("back on Home is the board's READ shortcut, and opens the book too") {
  // Spec 4.1: there is nothing to go back to from the root, so the slot carries the
  // one action worth a shortcut.
  HomeScreen h = makeHome();
  CHECK(h.onEvent(kBack).kind == Action::Kind::Open);
}

TEST_CASE("neither fires when there is no CONTINUE block to press") {
  // Every variant that draws no CONTINUE slab draws an EMPTY first hint slot too,
  // and a bar that promises nothing must not do something. CONTINUE is unreachable
  // by the model -- the focus ring is built Noneless -- and READ is gated on the
  // same predicate.
  //
  // THREE VARIANTS NOW, and the third is the one that makes this a rule rather than
  // a fact about the two empty states: HomeMissing HAS a book to name and cannot
  // open it, so a READ here would resolve from the same pointer, fail the same
  // `exists` check and paint nothing.
  for (const reader::HomeViewModel& vm : {reader::demoHomeEmptyVm(),
                                          reader::demoHomeUnopenedVm(),
                                          reader::demoHomeMissingVm()}) {
    HomeScreen h(vm, reader::demoHomeTargets());
    CHECK_FALSE(vm.offersContinue());
    CHECK(vm.hints[0].empty());
    CHECK(h.onEvent(kBack).kind == Action::Kind::None);
    // ...and the focus cannot be on a CONTINUE block, so Confirm is a menu push.
    REQUIRE(h.focus() >= 0);
    CHECK(h.onEvent(kConfirm).kind == Action::Kind::Push);
  }
}

TEST_CASE("offersContinue is the ONE predicate, and the ring follows it") {
  // The three things that have to agree about whether a CONTINUE block exists: the
  // focus ring (-1 is the block's own position), the Back gesture, and -- in the
  // theme -- the slab. Two of them spelled `!nothingToContinue` independently until
  // a third state arrived that is not `nothingToContinue` and still has no block.
  //
  // Driven over EVERY Home view model this project builds, so a fourth state cannot
  // adopt half the contract: the listing below is what a new variant has to join.
  for (const reader::HomeViewModel& vm :
       {reader::demoHomeVm(), reader::demoHomeEmptyVm(), reader::demoHomeUnopenedVm(),
        reader::demoHomeMissingVm()}) {
    HomeScreen h(vm, reader::demoHomeTargets());
    reader::Screen& s = h;
    // The ring admits -1 exactly when the screen draws a block to put it on.
    s.setFocus(-1);
    CHECK((s.focus() == -1) == vm.offersContinue());
    // ...and Back is READ exactly then, too.
    HomeScreen fresh(vm, reader::demoHomeTargets());
    CHECK((fresh.onEvent(kBack).kind == Action::Kind::Open) == vm.offersContinue());
    // ...and the bar says so, which is the half a reader sees before pressing.
    CHECK(vm.hints[0].empty() == !vm.offersContinue());
  }
}

TEST_CASE("a row with no target screen is inert rather than pushing the wrong one") {
  // A menu longer than the target list must not read off the end.
  HomeScreen h(vmWithTwoRows(), {ScreenId::Library});
  h.onEvent(kDown);
  h.onEvent(kDown);
  REQUIRE(h.focus() == 1);
  CHECK(h.onEvent(kConfirm).kind == Action::Kind::None);
}

TEST_CASE("Home binds no long press, so its mask is empty and its bar shows no ring") {
  HomeScreen h = makeHome();
  CHECK(h.longPressable() == 0);
}

TEST_CASE("a long press on a button Home does not bind is ignored, not mistaken for a short one") {
  // The recognizer should never deliver this, but a screen that silently treated
  // Long as Short would hide a mask bug rather than surfacing it.
  HomeScreen h = makeHome();
  const InputEvent longDown{Button::Down, PressKind::Long};
  CHECK(h.onEvent(longDown).kind == Action::Kind::None);
  CHECK(h.focus() == -1);
}

TEST_CASE("an empty menu leaves focus on Continue") {
  HomeViewModel vm = vmWithTwoRows();
  vm.menu.clear();
  HomeScreen h(vm, {});
  CHECK(h.onEvent(kDown).kind == Action::Kind::None);
  CHECK(h.focus() == -1);
}

// --- Restoring a focus after a wake ------------------------------------------
//
// Home reports its focus so the session record can store it, and until now it
// did not accept one back: the record named Home, the restore ladder pushed
// nothing (Home is already the root) and the base-class setFocus no-op swallowed
// the value. Waking always landed on CONTINUE whatever the user had selected.
// These pin the other half of the round trip.

TEST_CASE("a restored focus lands on the menu row the record named") {
  HomeScreen h = makeHome();
  reader::Screen& s = h;
  REQUIRE(s.focus() == -1);
  CHECK(s.setFocus(1));
  CHECK(s.focus() == 1);
}

TEST_CASE("restoring CONTINUE is a restore like any other, not 'nothing selected'") {
  // -1 is a position on this screen, not the absence of one, so a record holding
  // it has to come back as the CONTINUE block rather than as the first menu row.
  HomeScreen h = makeHome();
  reader::Screen& s = h;
  REQUIRE(s.setFocus(1));
  CHECK(s.setFocus(-1));
  CHECK(s.focus() == -1);
}

TEST_CASE("restoring the focus that is already set changes nothing and says so") {
  // Same contract as ScrollWindow::setFocus: the bool means "something moved",
  // which is what the shell reads to decide whether a repaint or an NVS write is
  // owed. A successful restore that happens to be a no-op still returns false.
  HomeScreen h = makeHome();
  reader::Screen& s = h;
  REQUIRE(s.focus() == -1);
  CHECK_FALSE(s.setFocus(-1));
  CHECK(s.focus() == -1);
}

TEST_CASE("a focus past the end of the menu clamps to the last row") {
  // The menu is built at boot from what is on the card, so a record written when
  // it was longer has to land somewhere rather than off the end.
  HomeScreen h = makeHome();
  reader::Screen& s = h;
  CHECK(s.setFocus(400));
  CHECK(s.focus() == 1);
}

TEST_CASE("a focus below CONTINUE clamps to CONTINUE") {
  HomeScreen h = makeHome();
  reader::Screen& s = h;
  REQUIRE(s.setFocus(1));
  CHECK(s.setFocus(-9));
  CHECK(s.focus() == -1);
}

TEST_CASE("restoring a row onto an empty menu lands on CONTINUE") {
  HomeViewModel vm = vmWithTwoRows();
  vm.menu.clear();
  HomeScreen h(vm, {});
  reader::Screen& s = h;
  CHECK_FALSE(s.setFocus(0));
  CHECK(s.focus() == -1);
}

// --- The empty state --------------------------------------------------------
//
// design/HomeEmpty.dc.html: /books holds no readable book, so the reading column
// is replaced. A VARIANT of Home rather than a screen of its own, which is why it
// shares HomeViewModel and ScreenId::Home -- the menu and the hint bar are Home's
// and must not move between the two.

TEST_CASE("the empty variant keeps Home's identity and menu") {
  const reader::HomeViewModel vm = reader::demoHomeEmptyVm();
  reader::HomeScreen screen(vm, reader::demoHomeTargets());
  CHECK(screen.id() == reader::ScreenId::Home);
  CHECK(vm.nothingToContinue);
  // Same three rows, in the same order, so navigation is unchanged.
  REQUIRE(vm.menu.size() == 3);
  CHECK(vm.menu[0].label == "LIBRARY");
  CHECK(vm.menu[1].label == "ARTICLES");
  CHECK(vm.menu[2].label == "SETTINGS");
  // LIBRARY says EMPTY where Home says a count.
  CHECK(vm.menu[0].value == "EMPTY");
  // ARTICLES CARRIES NO VALUE HERE, so it draws the chevron -- SETTINGS' own
  // mechanism, and Main.dc.html's menu note refuses `NOT SET UP` on this row: a
  // device nobody has set up should not open onto a list of chores.
  CHECK(vm.menu[1].value.empty());
}

TEST_CASE("the empty variant focuses LIBRARY, because there is no CONTINUE block") {
  const reader::HomeViewModel vm = reader::demoHomeEmptyVm();
  // -1 means "the CONTINUE block", and this state has none -- leaving it there
  // would give the hint bar a SELECT with nothing selected.
  CHECK(vm.focusedMenuIndex == 0);
}

TEST_CASE("the empty variant offers no READ hint") {
  const reader::HomeViewModel vm = reader::demoHomeEmptyVm();
  CHECK(vm.hints[0].empty());  // nothing to read
  CHECK(vm.hints[1] == "SELECT");
  CHECK(vm.hints[2] == "UP");
  CHECK(vm.hints[3] == "DOWN");
  // And no ring anywhere: an empty Home binds no hold.
  for (bool h : vm.holds) CHECK_FALSE(h);
}

TEST_CASE("the empty variant carries the board's copy, not the theme's") {
  // The words are the design's, so they live in the view model -- a theme holding
  // them would be a theme deciding what the device tells the user.
  const reader::HomeViewModel vm = reader::demoHomeEmptyVm();
  CHECK(vm.emptyTitle == "NO BOOKS YET");
  CHECK(vm.emptyBody.find("/books") != std::string::npos);
  CHECK(vm.emptyBody.find("Wi-Fi") == std::string::npos);  // V1 is card-only
}

// design/HomeUnopened.dc.html: books on the card, none of them open. The THIRD
// Home state, and also a variant rather than a screen -- so what these cases pin
// is mostly that it behaves identically to the empty variant, because the whole
// argument for reusing that mechanism is that the two states differ in words and
// in one value, never in layout or navigation.

TEST_CASE("the unopened variant is the empty variant's mechanism, not a new one") {
  const reader::HomeViewModel unopened = reader::demoHomeUnopenedVm();
  const reader::HomeViewModel empty = reader::demoHomeEmptyVm();
  reader::HomeScreen screen(unopened, reader::demoHomeTargets());
  CHECK(screen.id() == reader::ScreenId::Home);
  // The same flag drives both, which is what keeps them one layout.
  CHECK(unopened.nothingToContinue);
  // EVERYTHING STRUCTURAL IS EQUAL. Asserted as a comparison against the other
  // variant rather than against literals, so a change to one that is not made to
  // the other fails here -- that drift is exactly what a second render branch or a
  // second flag would have allowed, and the boards say the states share a layout.
  CHECK(unopened.focusedMenuIndex == empty.focusedMenuIndex);
  CHECK(unopened.hints == empty.hints);
  CHECK(unopened.holds == empty.holds);
  REQUIRE(unopened.menu.size() == empty.menu.size());
  CHECK(unopened.menu[0].label == empty.menu[0].label);
  CHECK(unopened.menu[1].label == empty.menu[1].label);
}

TEST_CASE("the unopened variant says there ARE books, which is the whole difference") {
  const reader::HomeViewModel vm = reader::demoHomeUnopenedVm();
  // A count, not `EMPTY`. This is the one fact the user can act on: the books are
  // there, so the sentence above it is worth following. The shell overwrites the
  // value with the card's real number, so what matters here is that it is not the
  // empty variant's word.
  CHECK(vm.menu[0].value != "EMPTY");
  CHECK(vm.menu[0].value.find_first_not_of("0123456789") == std::string::npos);
  CHECK_FALSE(vm.menu[0].value.empty());
}

TEST_CASE("the unopened variant carries its own copy, and not the empty one's") {
  const reader::HomeViewModel vm = reader::demoHomeUnopenedVm();
  CHECK(vm.emptyTitle == "NOTHING OPEN YET");
  CHECK(vm.emptyBody.find("library") != std::string::npos);
  // NOT HomeEmpty's words. Both states used to be one, and the failure mode of
  // sharing a mechanism is sharing the copy with it -- telling a user with twelve
  // books to go and copy some onto the card.
  CHECK(vm.emptyBody.find("SD card") == std::string::npos);
  CHECK(vm.emptyBody.find("/books") == std::string::npos);
  CHECK(vm.emptyTitle != reader::demoHomeEmptyVm().emptyTitle);
  CHECK(vm.emptyBody != reader::demoHomeEmptyVm().emptyBody);
}

TEST_CASE("the unopened variant has no CONTINUE slot either, in either direction") {
  // The same closure the empty variant gets, and tested separately rather than
  // assumed from the shared flag: this is the property a user can reach with a
  // button, and it was a real bug on the empty variant before lists wrapped.
  reader::HomeScreen h(reader::demoHomeUnopenedVm(), reader::demoHomeTargets());
  reader::Screen& s = h;
  REQUIRE(s.focus() == 0);
  h.onEvent({reader::Button::Up, reader::PressKind::Short});
  CHECK(s.focus() == 2);  // wrapped to SETTINGS, not down to a CONTINUE block
  h.onEvent({reader::Button::Down, reader::PressKind::Short});
  CHECK(s.focus() == 0);
  s.setFocus(-1);
  CHECK(s.focus() == 0);
}

// --- The missing-book state -------------------------------------------------
//
// design/HomeMissing.dc.html: /.reader/last.json names a book the card no longer
// has. It is NOT one of the two states above, and the difference is the whole
// design -- the pointer still knows the name, the author, the percentage and the
// chapter, so the reading column stays and a bordered strip over it says why the
// numbers under it describe a book that will not open.

TEST_CASE("the missing variant keeps the reading column, not the empty block") {
  const reader::HomeViewModel vm = reader::demoHomeMissingVm();
  reader::HomeScreen screen(vm, reader::demoHomeTargets());
  CHECK(screen.id() == reader::ScreenId::Home);
  CHECK(vm.bookMissing);
  // THE DISTINCTION THIS STATE EXISTS FOR. Falling back to `nothingToContinue` is
  // what the firmware did before the state was built, and it throws away four
  // facts the pointer carries and that are all still true.
  CHECK_FALSE(vm.nothingToContinue);
  CHECK(vm.emptyTitle.empty());
  CHECK(vm.emptyBody.empty());
  const reader::HomeViewModel ordinary = reader::demoHomeVm();
  CHECK(vm.title == ordinary.title);
  CHECK(vm.author == ordinary.author);
  CHECK(vm.percent == ordinary.percent);
  CHECK(vm.chapterLabel == ordinary.chapterLabel);
}

TEST_CASE("the missing variant's note names the book the spine names") {
  // Two runs on one screen naming one book. Composed by `missingBookNote` from the
  // view model's own title rather than typed beside it, so there is no second
  // spelling free to name a different book -- which is what a literal in both the
  // shell and the demo would have been.
  const reader::HomeViewModel vm = reader::demoHomeMissingVm();
  REQUIRE_FALSE(vm.missingNote.empty());
  CHECK(vm.missingNote == reader::missingBookNote(vm.title));
  CHECK(vm.missingNote.find(reader::upperLatin1(vm.title)) != std::string::npos);
}

TEST_CASE("missingBookNote shouts the title and quotes it the board's way") {
  // design/HomeMissing.dc.html's own sentence.
  CHECK(reader::missingBookNote("Middlemarch") ==
        "\xE2\x80\x9CMIDDLEMARCH\xE2\x80\x9D IS GONE FROM THE SD CARD.");
  // CURLY, not a straight ASCII quote: the board says `&ldquo;`/`&rdquo;`, and
  // fontc.py's subset carries U+201C/U+201D, so these are real glyphs rather than
  // the notdef boxes a codepoint outside the subset would draw.
  CHECK(reader::missingBookNote("x").compare(0, 3, "\xE2\x80\x9C") == 0);
  CHECK(reader::missingBookNote("x").find("\xE2\x80\x9D") != std::string::npos);
  // THE ACCENTED PATH, which is upperLatin1's whole reason for existing: an
  // ASCII-only shout renders `LE FLeAU` on the glass, and the device showed it.
  //
  // THE LITERALS ARE SPLIT, and this file's first draft was not: a C++ hex escape
  // is UNBOUNDED, so `"\xA9au"` is ONE escape reading `A9A` and not `\xA9` followed
  // by `au`. clang refused it outright here -- the ESP32's GCC is the toolchain that
  // ACCEPTS it and emits a different byte, which is how this trap reaches glass.
  CHECK(reader::missingBookNote("Le Fl\xC3\xA9" "au").find("LE FL\xC3\x89" "AU") !=
        std::string::npos);
}

TEST_CASE("the missing variant focuses LIBRARY, because that is the way out") {
  const reader::HomeViewModel vm = reader::demoHomeMissingVm();
  CHECK(vm.focusedMenuIndex == 0);
  // And the closure in both directions, as the empty variants have: there is no
  // CONTINUE block, so nothing may land on -1.
  reader::HomeScreen h(vm, reader::demoHomeTargets());
  reader::Screen& s = h;
  REQUIRE(s.focus() == 0);
  h.onEvent(kUp);
  CHECK(s.focus() == 2);  // wrapped to SETTINGS, not down to a CONTINUE block
  h.onEvent(kDown);
  CHECK(s.focus() == 0);
  s.setFocus(-1);
  CHECK(s.focus() == 0);
}

TEST_CASE("the missing variant keeps Home's menu and rows") {
  // A variant, not a screen: the menu and the bar are Home's and must not move
  // between the states -- which is what makes this read as Home in a different
  // condition rather than as a different screen.
  const reader::HomeViewModel vm = reader::demoHomeMissingVm();
  const reader::HomeViewModel ordinary = reader::demoHomeVm();
  REQUIRE(vm.menu.size() == ordinary.menu.size());
  for (size_t i = 0; i < vm.menu.size(); ++i) {
    CHECK(vm.menu[i].label == ordinary.menu[i].label);
    CHECK(vm.menu[i].value == ordinary.menu[i].value);
  }
  CHECK(vm.holds == ordinary.holds);
  // The bar differs in exactly one slot, and only that one.
  CHECK(vm.hints[0].empty());
  CHECK(vm.hints[1] == ordinary.hints[1]);
  CHECK(vm.hints[2] == ordinary.hints[2]);
  CHECK(vm.hints[3] == ordinary.hints[3]);
}

TEST_CASE("an ordinary Home is not the empty variant") {
  const reader::HomeViewModel vm = reader::demoHomeVm();
  CHECK_FALSE(vm.nothingToContinue);
  CHECK(vm.focusedMenuIndex == -1);  // the CONTINUE block
  CHECK(vm.hints[0] == "READ");
}

TEST_CASE("the empty variant has no CONTINUE slot to focus, in either direction") {
  // -1 is Home's CONTINUE block, and this variant does not draw one -- its first
  // hint slot is empty because there is nothing to read. Letting the focus reach
  // -1 anyway would put the selection on an invisible row with a blank action.
  //
  // It was reachable before lists wrapped, by pressing Up from LIBRARY, and
  // wrapping added a second way in (Down off the last menu row). The ring is
  // built Noneless for this variant instead, which is the model being right
  // rather than the ends being special-cased.
  reader::HomeScreen h(reader::demoHomeEmptyVm(), reader::demoHomeTargets());
  reader::Screen& s = h;
  REQUIRE(s.focus() == 0);
  h.onEvent({reader::Button::Up, reader::PressKind::Short});
  CHECK(s.focus() == 2);  // wrapped to SETTINGS, not down to a CONTINUE block
  h.onEvent({reader::Button::Down, reader::PressKind::Short});
  CHECK(s.focus() == 0);
  // ...and a record naming the CONTINUE block cannot put one here either.
  s.setFocus(-1);
  CHECK(s.focus() == 0);
}

TEST_CASE("an ordinary Home still has its CONTINUE slot") {
  reader::HomeScreen h(reader::demoHomeVm(), reader::demoHomeTargets());
  reader::Screen& s = h;
  CHECK(s.focus() == -1);
  CHECK(s.setFocus(0));
  CHECK(s.setFocus(-1));
  CHECK(s.focus() == -1);
}

TEST_CASE("DOWN MOVES AN ORDINARY HOME OFF THE CONTINUE BLOCK") {
  // The device reported Home's buttons doing nothing, with `[session] stored home:-1`
  // after each press -- the dispatch ran and the focus never left -1.
  //
  // NOTHING HERE PRESSED DOWN ON AN ORDINARY HOME. The empty variant's closure is
  // tested in both directions, and the ordinary one only through setFocus -- so the one
  // path a user takes on the screen the device boots to was uncovered.
  reader::HomeScreen h(reader::demoHomeVm(), reader::demoHomeTargets());
  reader::Screen& s = h;
  REQUIRE(s.focus() == -1);  // the CONTINUE block
  const Action a = h.onEvent(kDown);
  CHECK(a.kind == Action::Kind::Redraw);
  CHECK(s.focus() == 0);  // LIBRARY
  h.onEvent(kDown);
  CHECK(s.focus() == 1);  // ARTICLES
  h.onEvent(kDown);
  CHECK(s.focus() == 2);  // SETTINGS
  // ...and round, because every list wraps.
  h.onEvent(kDown);
  CHECK(s.focus() == -1);
}

TEST_CASE("UP moves an ordinary Home the other way") {
  reader::HomeScreen h(reader::demoHomeVm(), reader::demoHomeTargets());
  reader::Screen& s = h;
  REQUIRE(s.focus() == -1);
  h.onEvent(kUp);
  CHECK(s.focus() == 2);  // wraps up to SETTINGS
}

TEST_CASE("setBattery mirrors into the view model") {
  HomeScreen h = makeHome();
  // The default is unknown, not flat: a view model nobody has told about the
  // battery must not claim one.
  CHECK(h.vm().batteryPercent == -1);
  CHECK(h.vm().batteryCharging == false);
  h.setBattery(64, true);
  CHECK(h.vm().batteryPercent == 64);
  CHECK(h.vm().batteryCharging == true);
  h.setBattery(-1, false);
  CHECK(h.vm().batteryPercent == -1);
  CHECK(h.vm().batteryCharging == false);
}

TEST_CASE("setBattery does not disturb the focus") {
  // It is called from the shell's paint path, on every Home paint. Moving a
  // selection here would move one the user never touched.
  //
  // TWO presses, not one: makeHome()'s ring starts at -1 (CONTINUE), and one
  // Down lands on row 0 -- which is also setFocus's own reset value, so a
  // setBattery that accidentally reset the focus to 0 would have left `was`
  // unchanged and the assertion would not have noticed. A second Down moves
  // focus to row 1, which the accidental reset actually disturbs.
  HomeScreen h = makeHome();
  REQUIRE(h.onEvent(kDown).kind == Action::Kind::Redraw);
  REQUIRE(h.onEvent(kDown).kind == Action::Kind::Redraw);
  const int was = h.focus();
  h.setBattery(11, false);
  CHECK(h.focus() == was);
}
