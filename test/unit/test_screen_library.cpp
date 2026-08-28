#include <cstdio>
#include <string>
#include <vector>

#include "doctest.h"
#include "fake_fs.h"
#include "golden.h"
#include "home_vm.h"
#include "library_app.h"
#include "ramp.h"
#include "reader/app.h"
#include "reader/booklist.h"
#include "reader/components.h"
#include "reader/framebuffer.h"
#include "reader/reading_store.h"
#include "reader/screen_library.h"
#include "reader/screen_sd_missing.h"
#include "reader/screens.h"
#include "reader/theme_quiet.h"

using ramp::Ramp;
using reader::Action;
using reader::Button;
using reader::InputEvent;
using reader::LibraryScreen;
using reader::PressKind;
using reader::ScreenId;

namespace {

const InputEvent kDown{Button::Down, PressKind::Short};
const InputEvent kUp{Button::Up, PressKind::Short};
const InputEvent kConfirm{Button::Confirm, PressKind::Short};
const InputEvent kHold{Button::Confirm, PressKind::Long};
const InputEvent kBack{Button::Back, PressKind::Short};

// A card with the shape the filtering rules care about: books, a folder with
// books in it, and the debris a Mac leaves behind.
FakeFileSystem cardWithBooks() {
  FakeFileSystem fs;
  fs.mkdirs("/books/Classics");
  fs.writeAll("/books/Middlemarch.epub", "m");
  fs.writeAll("/books/Walden.txt", "w");
  fs.writeAll("/books/notes.pdf", "skip me");
  fs.writeAll("/books/._Walden.txt", "an AppleDouble sidecar");
  fs.writeAll("/books/Classics/Dubliners.epub", "d");
  fs.writeAll("/books/Classics/Odyssey.epub", "o");
  fs.mkdirs("/books/.Spotlight-V100");
  return fs;
}

// The one place per-book state would live, so a test can assert nothing touched
// it. Spec 4.0: a delete "never erases reading progress".
const char* const kStatePath = "/.reader/state/Middlemarch.json";

}  // namespace

TEST_CASE("the Library lists a card's books and counts them the way the board does") {
  Ramp r;
  reader::QuietTheme theme;
  FakeFileSystem fs = cardWithBooks();
  LibraryScreen lib(fs, "/books");
  lib.setVisibleRows(theme.libraryVisibleRows(800, r.fonts));

  // The folder, then the two books, alphabetically -- and neither the .pdf, the
  // AppleDouble sidecar nor .Spotlight-V100.
  REQUIRE(lib.itemCount() == 3);
  CHECK(lib.vm().rows.size() == 3);
  CHECK(lib.vm().rows[0].title == "Classics");
  CHECK(lib.vm().rows[0].isFolder);
  CHECK(lib.vm().rows[1].title == "Middlemarch");
  CHECK(lib.vm().rows[2].title == "Walden");
  CHECK(lib.vm().title == "LIBRARY");

  // The board's count is the books here plus the books one level down: two
  // beside the folder and two inside it.
  CHECK(lib.vm().bookCount == 4);
  // ...and the folder's own line says how many, as `FOLDER - 2 BOOKS` does on
  // the board.
  CHECK(lib.vm().rows[0].meta.find("2 BOOKS") != std::string::npos);
  // Every book reads NEW: per-book state is Phase 3's, and a percentage nothing
  // could have recorded would be a lie on glass.
  CHECK(lib.vm().rows[1].value == "NEW");
  // The author line is blank on a card, because an author needs the EPUB's OPF.
  CHECK(lib.vm().rows[1].meta.empty());
  // A book row states a value; a folder discloses instead.
  CHECK(lib.vm().rows[0].value.empty());
}

TEST_CASE("a second rescan asks the card for no folder counts at all") {
  // ISSUE #33, WHICH IS #21 ONE SCREEN LATER. rescan() calls countBooks per
  // DIRECTORY row for the board's `FOLDER - 2 BOOKS` line, and the Library is
  // destroyed by the pop that leaves it -- so Home > Library > Back > Library
  // paid one listing per folder every time, at ~2.90 ms an ENTRY.
  //
  // The count is memoised on the filesystem now, so only the /books listing
  // itself reaches the card on the second push. That one is the DirListingCache's
  // job on the device and the fake has no equivalent, which is why it is still
  // counted here.
  Ramp r;
  reader::QuietTheme theme;
  FakeFileSystem fs = cardWithBooks();
  fs.writeAll("/books/Poetry/a.epub", "x");
  fs.writeAll("/books/Essays/b.epub", "x");

  const size_t before = fs.listCalls();
  LibraryScreen first(fs, "/books");
  // /books plus one per folder: Classics, Essays, Poetry.
  CHECK(fs.listCalls() - before == 4);
  const int counted = first.vm().bookCount;

  const size_t second = fs.listCalls();
  LibraryScreen again(fs, "/books");
  CHECK(fs.listCalls() - second == 1);

  // AND THE ROWS SAY THE SAME THING, which is the half that matters: a cheaper
  // rescan that drew a different number would be worse than the cost.
  REQUIRE(again.itemCount() == first.itemCount());
  CHECK(again.vm().bookCount == counted);
  for (size_t i = 0; i < again.vm().rows.size(); ++i) {
    CAPTURE(i);
    CHECK(again.vm().rows[i].title == first.vm().rows[i].title);
    CHECK(again.vm().rows[i].meta == first.vm().rows[i].meta);
  }
}

TEST_CASE("deleting a book inside a folder updates the folder's count above it") {
  // THE STALENESS CASE THE MEMO HAS TO SURVIVE, and the one a memo owned by the
  // Library or by the shell would get wrong: the user descends into a folder,
  // deletes a book there, and comes back up to the row that states that folder's
  // count. The delete goes through FileSystem::remove, which drops the memo, so
  // the row cannot state the old number.
  Ramp r;
  reader::QuietTheme theme;
  FakeFileSystem fs = cardWithBooks();
  LibraryScreen lib(fs, "/books");
  lib.setVisibleRows(theme.libraryVisibleRows(800, r.fonts));
  REQUIRE(lib.vm().rows[0].title == "Classics");
  REQUIRE(lib.vm().rows[0].meta.find("2 BOOKS") != std::string::npos);
  const int wholeLibrary = lib.vm().bookCount;

  // Confirm on a folder row descends; Back at a subdirectory comes back up.
  REQUIRE(lib.onEvent(kConfirm).kind == Action::Kind::Redraw);
  REQUIRE(lib.path() == "/books/Classics");
  REQUIRE(lib.itemCount() == 2);
  CHECK(lib.deleteFocused());
  REQUIRE(lib.onEvent(kBack).kind == Action::Kind::Redraw);
  REQUIRE(lib.path() == "/books");

  REQUIRE(lib.vm().rows[0].title == "Classics");
  CHECK(lib.vm().rows[0].meta.find("1 BOOK") != std::string::npos);
  CHECK(lib.vm().bookCount == wholeLibrary - 1);
}

TEST_CASE("the Library's focus starts on the first row and wraps at both ends") {
  Ramp r;
  reader::QuietTheme theme;
  FakeFileSystem fs = cardWithBooks();
  LibraryScreen lib(fs, "/books");
  lib.setVisibleRows(theme.libraryVisibleRows(800, r.fonts));

  CHECK(lib.focus() == 0);
  CHECK(lib.vm().focusedRow == 0);
  // Up from the first row goes to the last.
  CHECK(lib.onEvent(kUp).kind == Action::Kind::Redraw);
  CHECK(lib.focus() == 2);
  CHECK(lib.onEvent(kDown).kind == Action::Kind::Redraw);
  CHECK(lib.focus() == 0);
  CHECK(lib.onEvent(kDown).kind == Action::Kind::Redraw);
  CHECK(lib.onEvent(kDown).kind == Action::Kind::Redraw);
  CHECK(lib.focus() == 2);
}

TEST_CASE("Confirm descends into a folder and Back climbs back out") {
  Ramp r;
  reader::QuietTheme theme;
  FakeFileSystem fs = cardWithBooks();
  LibraryScreen lib(fs, "/books");
  lib.setVisibleRows(theme.libraryVisibleRows(800, r.fonts));

  REQUIRE(lib.vm().rows[0].isFolder);
  CHECK(lib.onEvent(kConfirm).kind == Action::Kind::Redraw);
  CHECK(lib.path() == "/books/Classics");
  CHECK(lib.itemCount() == 2);
  // The band names the folder, shouted: it is a caps label whose text came from
  // a directory name.
  CHECK(lib.vm().title == "CLASSICS");
  // A folder opens at its first row rather than inheriting the parent's scroll.
  CHECK(lib.focus() == 0);

  CHECK(lib.onEvent(kBack).kind == Action::Kind::Redraw);
  CHECK(lib.path() == "/books");
  CHECK(lib.itemCount() == 3);
  // ...and Back at the root leaves the screen entirely.
  CHECK(lib.onEvent(kBack).kind == Action::Kind::Pop);
  CHECK(lib.path() == "/books");
}

TEST_CASE("CONFIRM ON A BOOK ASKS THE SHELL TO OPEN IT, AND DOES NOT PUSH") {
  Ramp r;
  reader::QuietTheme theme;
  FakeFileSystem fs = cardWithBooks();
  LibraryScreen lib(fs, "/books");
  lib.setVisibleRows(theme.libraryVisibleRows(800, r.fonts));
  lib.onEvent(kDown);  // onto Middlemarch
  REQUIRE_FALSE(lib.vm().rows[1].isFolder);
  // NOT a Push, and that distinction is the design: a Reader needs a Document,
  // and building one means inflating and parsing this file -- storage, which is
  // not core/'s. So the screen ASKS and the shell answers. See Action::open().
  //
  // This test used to assert None, with a comment saying the Reader was Phase 3.
  // It is here now.
  CHECK(lib.onEvent(kConfirm).kind == Action::Kind::Open);
  // And the Library stays where it is: the shell decides whether the book opened,
  // so the screen must not have moved in anticipation.
  CHECK(lib.path() == "/books");
}

TEST_CASE("an Open request is LATCHED by the app, not acted on") {
  // Mirrors the Retry latch, and for the same reason: the mount and the card are
  // the shell's. Asserted on App because a screen returning the right Action is
  // only half of it -- the app must not try to push a Reader itself.
  Ramp r;
  reader::QuietTheme theme;
  FakeFileSystem fs = cardWithBooks();
  reader::DemoScreenFactory factory(fs, "/books");
  auto lib = std::make_unique<LibraryScreen>(fs, "/books");
  lib->setVisibleRows(theme.libraryVisibleRows(800, r.fonts));
  reader::App app(std::move(lib), factory);
  CHECK_FALSE(app.openRequested());
  app.dispatch(kDown);
  app.dispatch(kConfirm);
  CHECK(app.openRequested());
  // The stack is untouched: no Reader was pushed behind the shell's back.
  CHECK(app.depth() == 1);
  CHECK(app.top().id() == reader::ScreenId::Library);
  app.clearOpenRequest();
  CHECK_FALSE(app.openRequested());
}

TEST_CASE("a held Confirm opens the actions overlay, and the ring says so") {
  Ramp r;
  reader::QuietTheme theme;
  FakeFileSystem fs = cardWithBooks();
  LibraryScreen lib(fs, "/books");
  lib.setVisibleRows(theme.libraryVisibleRows(800, r.fonts));

  // On a FOLDER, nothing: the overlay's four rows are Open / Book details /
  // Mark as finished / Delete..., three of which mean nothing for a directory,
  // and there is no board for a folder's actions.
  REQUIRE(lib.vm().rows[0].isFolder);
  CHECK(lib.onEvent(kHold).kind == Action::Kind::None);

  lib.onEvent(kDown);  // onto a book
  const Action a = lib.onEvent(kHold);
  CHECK(a.kind == Action::Kind::Push);
  CHECK(a.target == ScreenId::ItemActions);
  // The ring the bar draws and the mask the recognizer is given are the same
  // declaration: the Confirm slot, and only it.
  CHECK(lib.vm().holds == std::array<bool, 4>{false, true, false, false});
  CHECK(lib.longPressable() == reader::hintHoldMask(lib.vm().holds));
  CHECK(lib.longPressable() == reader::buttonBit(Button::Confirm));
  // A hold on a button with no ring is not reachable through the recognizer, and
  // is ignored here rather than treated as a press if it arrives anyway.
  CHECK(lib.onEvent({Button::Down, PressKind::Long}).kind == Action::Kind::None);
}

TEST_CASE("the Library scrolls by one row, and never shows a row it was not given") {
  Ramp r;
  reader::QuietTheme theme;
  // Three visible rows over seven items: the board's own content, windowed.
  LibraryScreen lib(reader::demoLibraryItems());
  lib.setVisibleRows(3);
  REQUIRE(lib.itemCount() == 7);
  CHECK(lib.vm().rows.size() == 3);
  CHECK(lib.vm().rows[0].title == "Classics");

  for (int i = 0; i < 3; ++i) lib.onEvent(kDown);
  CHECK(lib.focus() == 3);
  // Scrolled by exactly the overflow, not by a page: the focus is the last of
  // three rows, and the window starts one row further down than it did.
  CHECK(lib.vm().rows.size() == 3);
  CHECK(lib.vm().focusedRow == 2);
  CHECK(lib.vm().rows[2].title == "Walden");

  // A window with no height is a legitimate state -- a panel with no room for a
  // row -- and it draws nothing rather than a row nobody asked for.
  lib.setVisibleRows(0);
  CHECK(lib.vm().rows.empty());
  CHECK(lib.vm().focusedRow == -1);
  CHECK(lib.onEvent(kDown).kind == Action::Kind::None);
}

TEST_CASE("an empty directory is a state, not a crash") {
  Ramp r;
  reader::QuietTheme theme;
  FakeFileSystem fs;
  fs.mkdirs("/books");
  LibraryScreen lib(fs, "/books");
  lib.setVisibleRows(theme.libraryVisibleRows(800, r.fonts));
  CHECK(lib.itemCount() == 0);
  CHECK(lib.vm().rows.empty());
  CHECK(lib.vm().focusedRow == -1);
  CHECK(lib.focus() == -1);
  CHECK(lib.focusedItem() == nullptr);
  CHECK(lib.vm().bookCount == 0);
  // Nothing to act on, so nothing acts.
  CHECK(lib.onEvent(kConfirm).kind == Action::Kind::None);
  CHECK(lib.onEvent(kHold).kind == Action::Kind::None);
  CHECK(lib.onEvent(kDown).kind == Action::Kind::None);
  // ...and it still renders, which is the part a crash would take out.
  reader::Framebuffer fb(480, 800);
  lib.render(fb, r.fonts, theme, reader::Plane::Bw);
}

TEST_CASE("a card that is gone empties the list rather than leaving it stale") {
  Ramp r;
  reader::QuietTheme theme;
  FakeFileSystem fs = cardWithBooks();
  LibraryScreen lib(fs, "/books");
  lib.setVisibleRows(theme.libraryVisibleRows(800, r.fonts));
  REQUIRE(lib.itemCount() == 3);
  fs.setMounted(false);
  CHECK_FALSE(lib.rescan());
  // Those rows would open files that are no longer addressable, so they go.
  CHECK(lib.itemCount() == 0);
  CHECK(lib.vm().rows.empty());
}

TEST_CASE("deleting the last book pulls the focus back into range") {
  Ramp r;
  reader::QuietTheme theme;
  FakeFileSystem fs = cardWithBooks();
  fs.writeAll(kStatePath, "{\"page\":78}");
  LibraryScreen lib(fs, "/books");
  lib.setVisibleRows(theme.libraryVisibleRows(800, r.fonts));

  lib.onEvent(kDown);
  lib.onEvent(kDown);  // Walden, the last row
  REQUIRE(lib.focus() == 2);
  REQUIRE(lib.focusedItem() != nullptr);
  CHECK(lib.focusedItem()->entry.name == "Walden.txt");

  CHECK(lib.deleteFocused());
  CHECK(lib.itemCount() == 2);
  // Not 2, which is one past the end and would index off the vector on the next
  // paint -- and not -1, because there are still rows to be on.
  CHECK(lib.focus() == 1);
  CHECK(lib.vm().focusedRow == 1);
  CHECK(lib.vm().rows.size() == 2);
  CHECK_FALSE(fs.exists("/books/Walden.txt"));

  // Reading progress survives the book. Spec 4.0: delete "never erases reading
  // progress" -- a book that comes back should still know where you were.
  CHECK(fs.exists(kStatePath));

  // The focus is already on the new last row, so asking for it moves nothing --
  // which is the honest answer and what stops a repaint that changes nothing.
  CHECK_FALSE(lib.setFocus(1));
  CHECK(lib.focusedItem()->entry.name == "Middlemarch.epub");
  CHECK(lib.deleteFocused());
  CHECK(lib.itemCount() == 1);  // just the folder now, and the focus is on it
  CHECK(lib.focus() == 0);
  // A folder is not deletable: FileSystem::remove is files-only by contract, and
  // recursively deleting a directory is not a decision one long press makes.
  CHECK_FALSE(lib.deleteFocused());
  CHECK(lib.itemCount() == 1);
  CHECK(fs.exists("/books/Classics"));
}

TEST_CASE("the Library matches its golden at both geometries") {
  Ramp r;
  reader::QuietTheme theme;
  struct Case {
    int w, h;
    const char* name;
  };
  for (const Case c : {Case{480, 800, "library"}, Case{528, 792, "library_x3"}}) {
    // Built and navigated exactly as the simulator does it, through the App, so
    // the golden and `make compare`'s firmware render are the same pixels from
    // the same code path -- including App::render, which is what an overlay's
    // golden will need and what the shell will paint through.
    libapp::LibraryApp app(theme, r.fonts, c.h);
    REQUIRE(app.app.top().id() == ScreenId::Library);
    REQUIRE(app.app.top().fidelity() == reader::Fidelity::Mono);
    // The board focuses its second row, Middlemarch, and one Down after opening
    // the screen is how a user gets there.
    REQUIRE(app.library().focus() == 1);
    // Seven rows fit on both panels: 800 - 66 - 64 over a 90px row is 7.4, and
    // 792 - 66 - 64 is 7.3.
    CHECK(app.library().visibleRows() == 7);
    reader::Framebuffer fb(c.w, c.h);
    app.app.render(fb, r.fonts, theme, reader::Plane::Bw);
    golden::checkGolden(fb, c.name);
  }
}

// --- The focus pair, and the one rule stated in two places ----------------

TEST_CASE("BookList::countLibrary agrees with the band the Library draws") {
  // Home's LIBRARY row and the Library's own header band show the same number by
  // two different routes: countLibrary reads the card, and syncVm derives it from
  // a listing the screen already has. Two expressions of one rule, so this is
  // what stops them drifting -- if they disagree, the user sees one count on Home
  // and a different one after pressing Confirm on it.
  FakeFileSystem fs;
  fs.writeAll("/books/Middlemarch.epub", "x");
  fs.writeAll("/books/Walden.txt", "x");
  fs.writeAll("/books/Classics/Dubliners.epub", "x");
  fs.writeAll("/books/Classics/Odyssey.epub", "x");
  fs.writeAll("/books/Classics/notes.pdf", "x");  // not a book on either route
  fs.mkdirs("/books/Empty");
  LibraryScreen lib(fs, "/books");
  lib.setVisibleRows(6);
  CHECK(lib.vm().bookCount == 4);
  CHECK(reader::BookList::countLibrary(fs, "/books") == lib.vm().bookCount);
}

TEST_CASE("the Library reports and restores its focus through Screen") {
  // THE POINT IS THE BASE-CLASS HANDLE. The session record is read and written by
  // the shell, which holds a reader::Screen& and nothing more specific, so the
  // pair has to work without knowing which screen it is holding.
  FakeFileSystem fs;
  for (int i = 0; i < 9; ++i) fs.writeAll("/books/b" + std::to_string(i) + ".epub", "x");
  LibraryScreen lib(fs, "/books");
  lib.setVisibleRows(3);
  reader::Screen& s = lib;

  CHECK(s.focus() == 0);
  // An index into the WHOLE list, not into the three rows on glass: row 7 is off
  // the bottom of the first window, and restoring it has to scroll there.
  CHECK(s.setFocus(7));
  CHECK(s.focus() == 7);
  CHECK(lib.vm().focusedRow >= 0);
  CHECK(lib.vm().focusedRow < 3);

  // Same value twice is not a change, which is the signal a caller uses to skip
  // a repaint or an NVS write.
  CHECK_FALSE(s.setFocus(7));

  // A record written before some books were deleted still has to land somewhere.
  CHECK(s.setFocus(400));
  CHECK(s.focus() == 8);
}

TEST_CASE("a screen with no focus of its own says so rather than lying") {
  // The base-class defaults. A screen that cannot restore a focus must return
  // false, so the shell can tell "put back on row 7" from "ignored".
  reader::SdMissingScreen sd;
  reader::Screen& s = sd;
  CHECK(s.focus() == 0);
  CHECK_FALSE(s.setFocus(7));
  CHECK(s.focus() == 0);
}

TEST_CASE("an empty library reports no selection, which is not row 0") {
  FakeFileSystem fs;
  fs.mkdirs("/books");
  LibraryScreen lib(fs, "/books");
  lib.setVisibleRows(6);
  reader::Screen& s = lib;
  // -1, not 0: there is no row 0 to be on. The shell stores the -1 as itself
  // now that Session::focus is signed -- and restoring either -1 or the 0 an
  // older record holds clamps back to -1 here, so the round trip is stable
  // whichever it reads.
  CHECK(s.focus() == -1);
  CHECK_FALSE(s.setFocus(0));
  CHECK(s.focus() == -1);
}

TEST_CASE("popping the Library clears the pointer the factory kept") {
  // THE HOLE THAT WAS LEFT WHEN THE POINTER'S SAFETY WAS A COMMENT. The factory
  // outlives the App, and the App destroys a Library in two entirely different
  // ways: the whole App is replaced (a lost card, a successful retry), or the
  // Library is simply POPPED while the App lives on. Only the first had anything
  // clearing the pointer -- the shell called forgetLibrary() at each swap site --
  // so Home > Library > Back left the factory naming freed memory. Nothing could
  // reach it, which is exactly why nothing failed: the overlays are only ever
  // pushed by a live Library and handleOpen's Library branch only runs with one on
  // top. A hole nothing can reach today is still a hole, and the next caller added
  // would not have known the rule.
  //
  // The Library nulls the pointer from its own destructor now, so this holds for
  // every way it can die and no caller has to remember anything.
  Ramp ramp;
  reader::QuietTheme theme;
  libapp::LibraryApp app(theme, ramp.fonts, 800);
  REQUIRE(app.factory.library() != nullptr);
  app.app.dispatch(kBack);
  REQUIRE(app.app.top().id() == ScreenId::Home);
  CHECK(app.factory.library() == nullptr);
  // ...and with no Library to act on, an overlay is REFUSED rather than built
  // over freed memory.
  CHECK(app.factory.create(ScreenId::ItemActions) == nullptr);
  CHECK(app.factory.create(ScreenId::DeleteConfirm) == nullptr);
  CHECK(app.factory.create(ScreenId::BookDetails) == nullptr);
}

TEST_CASE("destroying the App clears the pointer too") {
  // The other half of the same fact, and the one the shell's replaceApp used to
  // own by hand: the App owns the Library, so the App going away takes it with it.
  // The factory has to outlive both, which is what it does on the device -- a
  // global beside a gApp that is replaced under it -- so this fixture puts the
  // factory outside the scope the App lives in.
  Ramp ramp;
  reader::QuietTheme theme;
  reader::DemoScreenFactory factory;
  factory.setLibraryVisibleRows(theme.libraryVisibleRows(800, ramp.fonts));
  {
    reader::App app(std::make_unique<reader::HomeScreen>(reader::demoHomeVm(),
                                                        reader::demoHomeTargets()),
                    factory);
    app.dispatch(kDown);
    app.dispatch(kConfirm);
    REQUIRE(app.top().id() == ScreenId::Library);
    REQUIRE(factory.library() != nullptr);
  }
  CHECK(factory.library() == nullptr);
}

TEST_CASE("a Library notifies only the watcher still watching it") {
  // THE MIRROR OF THE DANGLE THIS ALL STARTED WITH, and it is a hazard the fix
  // INTRODUCED: the Library now holds a pointer to its watcher, so a watcher that
  // dies first is a use-after-free in the other direction. The shell's globals are
  // declared gApp-then-gFactory, so at exit the factory would go first and the
  // App's Library would call into it -- unreachable on a device that never exits,
  // which is exactly the reasoning that let the original hole stand.
  //
  // So the link is two-way and one slot: stopWatching is how it is broken, and it
  // is guarded on WHO is asking so that one watcher cannot cancel another's.
  struct Counting : reader::LibraryWatcher {
    int calls = 0;
    void libraryGone(const reader::LibraryScreen*) override { ++calls; }
  };
  Counting watching;
  Counting other;
  {
    LibraryScreen lib(reader::demoLibraryItems());
    lib.watchedBy(watching);
    lib.stopWatching(other);  // not yours to cancel
  }
  CHECK(watching.calls == 1);
  CHECK(other.calls == 0);
  {
    LibraryScreen lib(reader::demoLibraryItems());
    lib.watchedBy(watching);
    lib.stopWatching(watching);
  }
  CHECK(watching.calls == 1);  // ...and no second call: nobody is watching now
}

TEST_CASE("an older Library's destruction does not clear a newer one") {
  // A FACTORY CAN HAVE BEEN HANDED A NEWER LIBRARY BEFORE THE OLD ONE DIES, and
  // clearing on the older one's death would null a pointer to a live screen -- an
  // overlay refused over a Library that is sitting right there. That failure would
  // be REACHABLE, where the dangle this all replaces was not.
  //
  // Two things make it true and they are not redundant. Adopting a newer Library
  // releases the old one, so the old one notifies nobody; and libraryGone is
  // guarded on which Library is going, which is what keeps it safe for a direct
  // caller -- it is a public method taking an arbitrary pointer.
  reader::DemoScreenFactory factory;
  std::unique_ptr<reader::Screen> first = factory.create(ScreenId::Library);
  REQUIRE(first != nullptr);
  REQUIRE(static_cast<reader::Screen*>(factory.library()) == first.get());
  std::unique_ptr<reader::Screen> second = factory.create(ScreenId::Library);
  REQUIRE(second != nullptr);
  REQUIRE(static_cast<reader::Screen*>(factory.library()) == second.get());
  first.reset();
  CHECK(static_cast<reader::Screen*>(factory.library()) == second.get());
}

// --- The rail, through renderLibrary ----------------------------------------
//
// The rail's own geometry is pinned in test_components.cpp, but NOTHING covered
// the wiring: whether a Library with more books than rows actually reaches
// drawScrollRail with the right numbers. Library's golden shows seven rows of
// seven, so it never overflows and the rail never draws in it -- and the rail
// failed to appear on a 200-book card while every test passed.

namespace {
FakeFileSystem cardWithManyBooks(int n) {
  FakeFileSystem fs;
  for (int i = 0; i < n; ++i) {
    char name[64];
    std::snprintf(name, sizeof(name), "/books/book_%04d.epub", i);
    fs.writeAll(name, "x");
  }
  return fs;
}
// Ink in the rail's column that is NOT one of the full-width rules crossing it.
//
// The header band's 2px rule and the hint bar's 1px rule both span the panel, so
// counting the whole column reports ink for a screen that has no rail at all --
// which a first version of this helper did, and it failed the no-rail case while
// the rail itself was working. The rail stops kRailRightGap short of the edge by
// construction, so a row inked all the way to the edge is a rule, not the rail.
int railInkIn(const reader::Framebuffer& fb) {
  const int x0 = fb.width() - reader::kRailRightGap - reader::kRailW;
  int n = 0;
  for (int y = 0; y < fb.height(); ++y) {
    bool spansToEdge = true;
    for (int x = fb.width() - reader::kRailRightGap; x < fb.width(); ++x)
      if (fb.getPixel(x, y)) spansToEdge = false;
    if (spansToEdge) continue;
    for (int x = x0; x < fb.width() - reader::kRailRightGap; ++x)
      if (!fb.getPixel(x, y)) ++n;
  }
  return n;
}
}  // namespace

TEST_CASE("a Library with more books than rows reports the numbers the rail needs") {
  Ramp r;
  reader::QuietTheme theme;
  FakeFileSystem fs = cardWithManyBooks(200);
  LibraryScreen lib(fs, "/books");
  const int visible = theme.libraryVisibleRows(800, r.fonts);
  lib.setVisibleRows(visible);

  REQUIRE(lib.itemCount() == 200);
  CHECK(lib.vm().rows.size() == static_cast<size_t>(visible));
  // The two the theme cannot derive from `rows`.
  CHECK(lib.vm().totalRows == 200);
  CHECK(lib.vm().firstRow == 0);
}

TEST_CASE("...and renderLibrary actually draws the rail") {
  Ramp r;
  reader::QuietTheme theme;
  FakeFileSystem fs = cardWithManyBooks(200);
  LibraryScreen lib(fs, "/books");
  lib.setVisibleRows(theme.libraryVisibleRows(800, r.fonts));

  reader::Framebuffer fb(480, 800);
  theme.renderLibrary(fb, r.fonts, lib.vm(), reader::Plane::Bw);
  CHECK(railInkIn(fb) > 0);
}

TEST_CASE("a Library that fits draws no rail and gives up no width") {
  Ramp r;
  reader::QuietTheme theme;
  FakeFileSystem fs = cardWithBooks();  // three rows
  LibraryScreen lib(fs, "/books");
  lib.setVisibleRows(theme.libraryVisibleRows(800, r.fonts));
  lib.onEvent(kDown);  // focus a row so there is a full-bleed fill to check

  reader::Framebuffer fb(480, 800);
  theme.renderLibrary(fb, r.fonts, lib.vm(), reader::Plane::Bw);
  CHECK(railInkIn(fb) == 0);
  // And the focused row's fill reaches the panel edge -- no reserved-but-empty
  // gutter, which is what a white strip beside black looks like on the device.
  bool touchesEdge = false;
  for (int y = 0; y < 800; ++y)
    if (!fb.getPixel(479, y)) touchesEdge = true;
  CHECK(touchesEdge);
}

TEST_CASE("scrolling down moves the thumb, because firstRow follows the window") {
  Ramp r;
  reader::QuietTheme theme;
  FakeFileSystem fs = cardWithManyBooks(200);
  LibraryScreen lib(fs, "/books");
  const int visible = theme.libraryVisibleRows(800, r.fonts);
  lib.setVisibleRows(visible);

  for (int i = 0; i < visible + 20; ++i) lib.onEvent(kDown);
  CHECK(lib.vm().firstRow > 0);
  CHECK(lib.vm().totalRows == 200);
}

// --- Progress on the rows -----------------------------------------------------
//
// design/Library.dc.html gives each row a percentage or `NEW`. Every book read NEW on
// the device because the percentage needed `/.reader/state/` and there was nothing in
// it -- and this file's own fixture already had a `kStatePath` constant for asserting
// that a delete does not touch that directory, so the seam was here before the data
// was.
//
// Asserted through `vm().rows`, which is what the theme draws, rather than through the
// items behind them: a row's `value` is the field on the glass.

namespace {

// The drawn value for one row, by title, or "<absent>" if that row is not on screen.
std::string valueOf(const LibraryScreen& lib, std::string_view title) {
  for (const reader::LibraryRow& r : lib.vm().rows)
    if (r.title == title) return r.value;
  return "<absent>";
}

}  // namespace

TEST_CASE("a started book shows its percentage and an unopened one reads NEW") {
  FakeFileSystem fs = cardWithBooks();
  reader::ReadingPosition p;
  p.bookPath = "/books/Middlemarch.epub";
  p.spine = 4;
  p.percent = 31;
  p.bookBytes = 1;
  p.ppem = 32;
  p.columnW = 492;
  REQUIRE(reader::savePosition(fs, p) == reader::SaveResult::Written);

  LibraryScreen lib(fs, "/books");
  lib.setVisibleRows(8);
  CHECK(valueOf(lib, "Middlemarch") == "31%");
  // NEW, not a blank and not 0% -- a book at 0% has been opened. Both halves
  // asserted, or the case is checking one row and calling it a rule.
  CHECK(valueOf(lib, "Walden") == "NEW");
}

TEST_CASE("a folder row has no progress value at all") {
  // The board gives a folder `FOLDER - 6 BOOKS` in its meta line, so a percentage in
  // the value slot would be two facts in one field.
  FakeFileSystem fs = cardWithBooks();
  LibraryScreen lib(fs, "/books");
  lib.setVisibleRows(8);
  for (const reader::LibraryRow& r : lib.vm().rows)
    if (r.isFolder) CHECK(r.value.empty());
}

TEST_CASE("a book in a subfolder is matched by its full path, not its name") {
  // The index is keyed by the path stored in the sidecar, and two folders may hold
  // books with the same filename. Matching on the leaf would give one the other's
  // position.
  FakeFileSystem fs = cardWithBooks();
  fs.writeAll("/books/Dubliners.epub", "top-level namesake");
  reader::ReadingPosition p;
  p.bookPath = "/books/Classics/Dubliners.epub";
  p.percent = 48;
  p.bookBytes = 1;
  p.ppem = 32;
  p.columnW = 492;
  REQUIRE(reader::savePosition(fs, p) == reader::SaveResult::Written);

  LibraryScreen top(fs, "/books");
  top.setVisibleRows(8);
  CHECK(valueOf(top, "Dubliners") == "NEW");  // the namesake, not the started one

  LibraryScreen inner(fs, "/books/Classics");
  inner.setVisibleRows(8);
  CHECK(valueOf(inner, "Dubliners") == "48%");
}


// --- Progress that changed while this screen was standing ---------------------
//
// THE REPORTED BUG: "start reading a book from the library and go back, and the row
// still says NEW". The Reader is pushed ON TOP of the Library, so the pop that leaves
// the book hands back the very same screen -- with the rows it was built with, before
// the book had ever been opened. The save on the way out lands on the card and nothing
// re-reads it.
//
// Deliberately NOT rescan(): the set of books cannot change while the firmware runs
// (V1 transfers by card), and the save has just dropped the listing cache, so a rescan
// would pay a fresh `/books` listing -- ~600 ms on a 203-book card -- plus one listing
// per folder, on the critical path of a Back. Only `/.reader/state` has changed.

TEST_CASE("a book read while the Library stood under the Reader stops reading NEW") {
  FakeFileSystem fs = cardWithBooks();
  LibraryScreen lib(fs, "/books");
  lib.setVisibleRows(8);
  REQUIRE(valueOf(lib, "Middlemarch") == "NEW");

  // The reader opens it, reads, and saveReadingPosition("leaving") writes the sidecar
  // while this screen sits underneath the Reader.
  reader::ReadingPosition p;
  p.bookPath = "/books/Middlemarch.epub";
  p.spine = 4;
  p.percent = 31;
  p.chapter = "MISS BROOKE";
  p.bookBytes = 1;
  p.ppem = 32;
  p.columnW = 492;
  REQUIRE(reader::savePosition(fs, p) == reader::SaveResult::Written);

  REQUIRE(lib.refreshProgress());
  CHECK(valueOf(lib, "Middlemarch") == "31%");
  // The book nobody opened is untouched -- or the case is asserting that every row
  // changed, which a bug that blanked the lot would also satisfy.
  CHECK(valueOf(lib, "Walden") == "NEW");
}

TEST_CASE("refreshing progress moves neither the focus nor the window") {
  // This runs on the press that returns to the Library, so anything it disturbs is a
  // selection the user did not touch. It re-derives values over the rows that are
  // already there; it does not re-read the directory.
  Ramp r;
  reader::QuietTheme theme;
  FakeFileSystem fs = cardWithManyBooks(200);
  LibraryScreen lib(fs, "/books");
  lib.setVisibleRows(theme.libraryVisibleRows(800, r.fonts));
  for (int i = 0; i < 40; ++i) lib.onEvent(kDown);

  const int focus = lib.focus();
  const int first = lib.vm().firstRow;
  REQUIRE(focus == 40);
  REQUIRE(first > 0);

  REQUIRE(lib.refreshProgress());
  CHECK(lib.focus() == focus);
  CHECK(lib.vm().firstRow == first);
  CHECK(lib.itemCount() == 200);
}

TEST_CASE("refreshing progress refreshes Book details' fields, not just the row") {
  // The details screen reads its Progress and Current chapter rows off this same item
  // -- one lookup for both, so one refresh has to serve both or the two disagree about
  // one number.
  FakeFileSystem fs = cardWithBooks();
  LibraryScreen lib(fs, "/books");
  lib.setVisibleRows(8);

  reader::ReadingPosition p;
  p.bookPath = "/books/Middlemarch.epub";
  p.percent = 31;
  p.chapter = "MISS BROOKE";
  p.bookBytes = 1;
  p.ppem = 32;
  p.columnW = 492;
  REQUIRE(reader::savePosition(fs, p) == reader::SaveResult::Written);
  REQUIRE(lib.refreshProgress());

  const reader::LibraryItem* item = nullptr;
  for (int i = 0; i < lib.itemCount(); ++i) {
    lib.setFocus(i);
    if (lib.focusedItem()->entry.name == "Middlemarch.epub") item = lib.focusedItem();
  }
  REQUIRE(item != nullptr);
  CHECK(item->details.progress == "31%");
  CHECK(item->details.chapter == "MISS BROOKE");
}

TEST_CASE("a book whose sidecar has gone reads NEW again") {
  // The fields are re-derived, not merged: a refresh writes what the card says now,
  // including when what it says is nothing. Leaving the old value would make a row
  // state a position that no longer exists.
  FakeFileSystem fs = cardWithBooks();
  reader::ReadingPosition p;
  p.bookPath = "/books/Middlemarch.epub";
  p.percent = 31;
  p.chapter = "MISS BROOKE";
  p.bookBytes = 1;
  p.ppem = 32;
  p.columnW = 492;
  REQUIRE(reader::savePosition(fs, p) == reader::SaveResult::Written);

  LibraryScreen lib(fs, "/books");
  lib.setVisibleRows(8);
  REQUIRE(valueOf(lib, "Middlemarch") == "31%");

  REQUIRE(fs.remove(reader::statePathFor("/books/Middlemarch.epub")));
  REQUIRE(lib.refreshProgress());
  CHECK(valueOf(lib, "Middlemarch") == "NEW");

  const reader::LibraryItem* item = nullptr;
  for (int i = 0; i < lib.itemCount(); ++i) {
    lib.setFocus(i);
    if (lib.focusedItem()->entry.name == "Middlemarch.epub") item = lib.focusedItem();
  }
  REQUIRE(item != nullptr);
  CHECK(item->details.progress.empty());
  CHECK(item->details.chapter.empty());
}

TEST_CASE("a sample-content Library has no card to re-read") {
  // The goldens and the comparison sheet build one of these. There is no filesystem,
  // so a refresh refuses rather than blanking the board's own values -- which is
  // rescan()'s rule on the same screen.
  LibraryScreen lib(reader::demoLibraryItems());
  lib.setVisibleRows(8);
  const std::string was = lib.vm().rows[0].value;
  CHECK_FALSE(lib.refreshProgress());
  CHECK(lib.vm().rows[0].value == was);
}
