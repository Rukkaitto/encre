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
#include "fake_fs.h"
#include "home_vm.h"
#include "reader/app.h"
#include "reader/screen_home.h"
#include "reader/screen_item_actions.h"
#include "reader/screen_library.h"
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

// --- THE FOLDER THE LIBRARY WAS IN (#14) -------------------------------------
//
// A focus is an index into a list, and the Library can be listing a SUBFOLDER of
// /books. The record used to carry only the index, so sleeping in
// /books/Classics on row 2 woke on /books row 2 -- a plausible-looking wrong row,
// which is worse than losing the position: nothing on the glass says the restore
// went wrong, and the row opens a book the reader never chose.
//
// OVER A CARD, because that is the only place the defect lives: the sample
// Library the goldens use has one directory and cannot descend. FakeFileSystem is
// what makes that testable in core/ at all -- shell/ has no harness.

namespace {

// /books with three books and two folders, one of which holds three more. Deep
// enough that a row index valid inside the subfolder is ALSO valid at the root,
// which is the whole hazard: a restore that lands on the wrong list still finds
// a row there and looks like it worked.
FakeFileSystem cardWithFolders() {
  FakeFileSystem fs;
  fs.mkdirs("/books/Classics");
  fs.mkdirs("/books/Poetry");
  fs.writeAll("/books/Middlemarch.epub", "m");
  fs.writeAll("/books/Walden.epub", "w");
  fs.writeAll("/books/Ulysses.epub", "u");
  fs.writeAll("/books/Classics/Dubliners.epub", "d");
  fs.writeAll("/books/Classics/Iliad.epub", "i");
  fs.writeAll("/books/Classics/Odyssey.epub", "o");
  return fs;
}

// Home over a card, and a factory that builds its Library through the real
// filesystem interface rather than over sample rows.
//
// THE FILESYSTEM IS DECLARED FIRST ON PURPOSE: the factory holds a reference to
// it and the App holds a reference to the factory, so destruction runs the other
// way round.
struct CardApp {
  FakeFileSystem fs;
  DemoScreenFactory factory;
  App app;

  explicit CardApp(FakeFileSystem card)
      : fs(std::move(card)),
        factory(fs, "/books"),
        app(std::make_unique<HomeScreen>(demoHomeVm(), demoHomeTargets()), factory) {
    factory.setLibraryVisibleRows(7);
  }

  LibraryScreen& library() {
    LibraryScreen* lib = factory.library();
    REQUIRE(lib != nullptr);
    return *lib;
  }
};

// Home > Library > into /books/Classics > down to its second book. Folders sort
// first and Classics before Poetry, so row 0 of /books is the folder to descend
// into.
std::vector<StackEntry> inTheSubfolder(CardApp& live, int downsInside) {
  live.app.dispatch(kDown);     // Home: CONTINUE -> the LIBRARY row
  live.app.dispatch(kConfirm);  // open it
  REQUIRE(live.app.top().id() == ScreenId::Library);
  REQUIRE(live.library().path() == "/books");
  live.app.dispatch(kConfirm);  // descend into row 0, which is Classics
  REQUIRE(live.library().path() == "/books/Classics");
  for (int i = 0; i < downsInside; ++i) live.app.dispatch(kDown);
  return live.app.snapshot();
}

}  // namespace

TEST_CASE("a snapshot says WHICH list each focus indexes, not just where in it") {
  CardApp live(cardWithFolders());
  const std::vector<StackEntry> snap = inTheSubfolder(live, 2);

  REQUIRE(snap.size() == 2);
  CHECK(snap[0].place.empty());  // Home has one list and needs no place
  CHECK(snap[1].screen == ScreenId::Library);
  CHECK(snap[1].focus == 2);
  CHECK(snap[1].place == "/books/Classics");
}

TEST_CASE("a wake comes back to the folder the Library was in, on the row it was on") {
  CardApp live(cardWithFolders());
  const std::vector<StackEntry> snap = inTheSubfolder(live, 2);
  const std::string was = live.library().focusedPath();
  REQUIRE(was == "/books/Classics/Odyssey.epub");

  // A SECOND App over the same card, as a wake gets.
  CardApp woken(cardWithFolders());
  const App::RestoreReport r = woken.app.restore(snap);
  CHECK(r.restored == 2);
  CHECK(woken.library().path() == "/books/Classics");
  CHECK(woken.library().focus() == 2);
  // The row is the same BOOK, which is the thing the reader would notice. Before
  // this field existed the path came back /books and the focus came back 2, which
  // is Ulysses -- a book they had not opened, on a screen that looked right.
  CHECK(woken.library().focusedPath() == was);
  CHECK(woken.app.snapshot() == snap);
}

TEST_CASE("a folder deleted while the device slept costs the ROW as well") {
  // The commonest way a place fails, and the case the whole design turns on: the
  // place cannot be honoured, so the focus that indexed it is not applied either.
  // Landing at the top of /books is honest; landing on row 2 of /books is the
  // plausible-looking wrong row this closes.
  CardApp live(cardWithFolders());
  const std::vector<StackEntry> snap = inTheSubfolder(live, 2);

  // The same card with the folder gone. Built rather than deleted from, because
  // FileSystem::remove is files-only by contract -- which is also why the device
  // itself cannot produce this state and a computer with the card in it can.
  FakeFileSystem thinner;
  thinner.mkdirs("/books/Poetry");
  thinner.writeAll("/books/Middlemarch.epub", "m");
  thinner.writeAll("/books/Walden.epub", "w");
  thinner.writeAll("/books/Ulysses.epub", "u");
  CardApp woken(std::move(thinner));

  const App::RestoreReport r = woken.app.restore(snap);
  CHECK(r.restored == 2);  // the SCREEN still comes back; only its position does not
  // Row 2 EXISTS at the root, which is what makes the wrong answer plausible: it
  // is Ulysses, a book the reader never opened. A test on a card too short to
  // hold the row would pass on the clamp instead of on the drop.
  REQUIRE(woken.library().itemCount() > snap[1].focus);
  CHECK(woken.library().path() == "/books");
  CHECK(woken.library().focus() == 0);
  // Said the other way round, because this is the assertion that is really about
  // the defect: the row the record named must NOT be applied to another list.
  CHECK(woken.library().focus() != snap[1].focus);
}

TEST_CASE("a place outside the Library's own root is refused, before any listing") {
  // A record is written by a device and read by a device, but not necessarily
  // with the same card in the slot or the same --root on the command line, so a
  // prefix test is the containment this layer performs.
  //
  // AND THE ASSERTION THAT MATTERS IS THAT NO LISTING WAS ATTEMPTED, not that
  // setPlace answered false. The fake compares literal keys, so `/books/../etc`
  // fails to list whatever this code does -- a case that asserted only the bool
  // passed with the whole check deleted, which is this project's own rule about a
  // mutation telling you about your INPUT first. Whether a `..` resolves is a
  // property of what is behind the interface (SdFat skips dot entries and does
  // not; HostFileSystem hands the path to the OS and does), so the refusal has to
  // happen HERE, on the path, and the way to see that is that the card is never
  // touched.
  FakeFileSystem fs = cardWithFolders();
  LibraryScreen lib(fs, "/books");
  const size_t before = fs.listCalls();
  CHECK_FALSE(lib.setPlace("/elsewhere/Classics"));
  CHECK_FALSE(lib.setPlace("/booksmith"));  // a prefix, not a parent
  CHECK_FALSE(lib.setPlace("/books/../etc"));
  CHECK_FALSE(lib.setPlace("books/Classics"));  // not absolute
  CHECK_FALSE(lib.setPlace(""));               // no place at all is not a place
  CHECK(fs.listCalls() == before);
  CHECK(lib.path() == "/books");

  // And the same thing through the restore, where it costs the row: /books row 2
  // is a real row, so this is refused on the path and not on the index.
  CardApp other(cardWithFolders());
  other.app.restore({{ScreenId::Home, 0, ""}, {ScreenId::Library, 2, "/elsewhere"}});
  CHECK(other.library().path() == "/books");
  CHECK(other.library().focus() == 0);
}

TEST_CASE("asking for the directory already listed costs no listing") {
  // The ordinary case -- the constructor lists the root, and the root is where
  // most records were written -- and it must touch no card: a rescan on the wake
  // path would be a directory listing for nothing, at ~2.9 ms an entry on the
  // device.
  FakeFileSystem fs = cardWithFolders();
  LibraryScreen lib(fs, "/books");
  const size_t before = fs.listCalls();
  CHECK(lib.setPlace("/books"));
  CHECK(fs.listCalls() == before);
  // ...and a real move does list, which is what says the check above measures
  // something. Without it, a setPlace that had quietly stopped listing anything
  // would pass here and hand the record's row to a stale directory.
  CHECK(lib.setPlace("/books/Classics"));
  CHECK(fs.listCalls() > before);
}

TEST_CASE("the place lands before the overlay above it is built") {
  // The ordering App::restore already had for the focus, now with something in
  // front of it: an overlay reads the focused ROW of the screen underneath it AT
  // CONSTRUCTION, so a Library restored to the right folder only afterwards would
  // caption the panel with a book from the wrong directory.
  CardApp live(cardWithFolders());
  const std::vector<StackEntry> snap = inTheSubfolder(live, 2);

  std::vector<StackEntry> withOverlay = snap;
  withOverlay.push_back({ScreenId::ItemActions, 0, ""});

  CardApp woken(cardWithFolders());
  const App::RestoreReport r = woken.app.restore(withOverlay);
  REQUIRE(r.restored == 3);
  REQUIRE(woken.app.top().id() == ScreenId::ItemActions);
  const ItemActionsScreen& panel = static_cast<const ItemActionsScreen&>(woken.app.top());
  CHECK(panel.vm().title == "Odyssey");
}
