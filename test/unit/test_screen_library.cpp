#include <string>
#include <vector>

#include "doctest.h"
#include "fake_fs.h"
#include "golden.h"
#include "home_vm.h"
#include "library_app.h"
#include "ramp.h"
#include "reader/app.h"
#include "reader/framebuffer.h"
#include "reader/screen_library.h"
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

TEST_CASE("the Library's focus starts on the first row and clamps at both ends") {
  Ramp r;
  reader::QuietTheme theme;
  FakeFileSystem fs = cardWithBooks();
  LibraryScreen lib(fs, "/books");
  lib.setVisibleRows(theme.libraryVisibleRows(800, r.fonts));

  CHECK(lib.focus() == 0);
  CHECK(lib.vm().focusedRow == 0);
  // At the top already, so nothing moves and nothing repaints: on this panel a
  // refresh that changes nothing is half a second of a button that feels stuck.
  CHECK(lib.onEvent(kUp).kind == Action::Kind::None);
  CHECK(lib.onEvent(kDown).kind == Action::Kind::Redraw);
  CHECK(lib.focus() == 1);
  CHECK(lib.onEvent(kDown).kind == Action::Kind::Redraw);
  CHECK(lib.onEvent(kDown).kind == Action::Kind::None);  // the end of the list
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

TEST_CASE("Confirm on a book does nothing yet, because the Reader is Phase 3") {
  Ramp r;
  reader::QuietTheme theme;
  FakeFileSystem fs = cardWithBooks();
  LibraryScreen lib(fs, "/books");
  lib.setVisibleRows(theme.libraryVisibleRows(800, r.fonts));
  lib.onEvent(kDown);  // onto Middlemarch
  REQUIRE_FALSE(lib.vm().rows[1].isFolder);
  // Not a Push: there is no Reader to push, and a placeholder screen would be a
  // screen to delete in Phase 3.
  CHECK(lib.onEvent(kConfirm).kind == Action::Kind::None);
  CHECK(lib.path() == "/books");
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
