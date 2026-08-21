#include <array>
#include <string>

#include "doctest.h"
#include "fake_fs.h"
#include "golden.h"
#include "library_app.h"
#include "ramp.h"
#include "reader/app.h"
#include "reader/components.h"
#include "reader/framebuffer.h"
#include "reader/screen_delete_confirm.h"
#include "reader/screen_library.h"
#include "reader/screens.h"
#include "reader/theme_quiet.h"

using ramp::Ramp;
using reader::Action;
using reader::Button;
using reader::InputEvent;
using reader::PressKind;
using reader::ScreenId;

namespace {

const InputEvent kDown{Button::Down, PressKind::Short};
const InputEvent kUp{Button::Up, PressKind::Short};
const InputEvent kConfirm{Button::Confirm, PressKind::Short};
const InputEvent kBack{Button::Back, PressKind::Short};
const InputEvent kHold{Button::Confirm, PressKind::Long};

// The board's state: the Library focused on Dubliners, the actions panel over it
// with `Delete...` selected, and the confirmation over that. Reached by pressing,
// so the render pins the route as well as the pixels.
libapp::LibraryApp confirmOver(const reader::Theme& theme, const reader::FontSet& fonts,
                               int panelH) {
  libapp::LibraryApp app(theme, fonts, panelH, 5);
  app.app.dispatch(kHold);
  for (int i = 0; i < 3; ++i) app.app.dispatch(kDown);  // down to Delete...
  app.app.dispatch(kConfirm);
  REQUIRE(app.app.top().id() == ScreenId::DeleteConfirm);
  return app;
}

}  // namespace

TEST_CASE("the delete confirmation names the book, and starts on CANCEL") {
  Ramp r;
  reader::QuietTheme theme;
  libapp::LibraryApp app = confirmOver(theme, r.fonts, 800);
  const auto& confirm = static_cast<const reader::DeleteConfirmScreen&>(app.app.top());

  // A confirmation that does not name the thing is one people learn to dismiss
  // without reading, so the book's name in the caption is load-bearing. The
  // board's quotes are U+201C/U+201D.
  CHECK(confirm.vm().title == "DELETE \xE2\x80\x9C" "DUBLINERS\xE2\x80\x9D?");
  CHECK(confirm.vm().message.find("progress and bookmarks are kept") != std::string::npos);
  CHECK(confirm.vm().cancelLabel == "CANCEL");
  CHECK(confirm.vm().confirmLabel == "DELETE");
  // The focus starts on CANCEL, which the board draws as the FILLED slab: a
  // destructive prompt focused on its destructive action deletes a book on a
  // press made before reading anything.
  CHECK(confirm.focus() == 0);
  CHECK(confirm.vm().focusedAction == 0);
  CHECK(app.app.top().isOverlay());
  CHECK(app.app.depth() == 4);  // Home, Library, actions, confirm
}

TEST_CASE("cancelling leaves the actions panel up; confirming returns to the Library") {
  Ramp r;
  reader::QuietTheme theme;
  {
    libapp::LibraryApp app = confirmOver(theme, r.fonts, 800);
    app.app.dispatch(kBack);
    // Back is cancel, and cancel is one Pop: the panel this came from is still
    // the right place to be.
    CHECK(app.app.top().id() == ScreenId::ItemActions);
    CHECK(app.app.depth() == 3);
  }
  {
    libapp::LibraryApp app = confirmOver(theme, r.fonts, 800);
    app.app.dispatch(kConfirm);  // CANCEL is focused
    CHECK(app.app.top().id() == ScreenId::ItemActions);
  }
  {
    libapp::LibraryApp app = confirmOver(theme, r.fonts, 800);
    app.app.dispatch(kDown);  // onto DELETE
    app.app.dispatch(kConfirm);
    // BOTH overlays go: the actions panel was acting on a book that no longer
    // exists. One Action, one screen change, however deep the flow was.
    CHECK(app.app.top().id() == ScreenId::Library);
    CHECK(app.app.depth() == 2);
    CHECK(app.app.dirty());
    CHECK(app.app.transition());
  }
}

TEST_CASE("the confirmation's focus is two rows, clamped, and it binds no hold") {
  Ramp r;
  reader::QuietTheme theme;
  libapp::LibraryApp app = confirmOver(theme, r.fonts, 800);
  auto& confirm = static_cast<reader::DeleteConfirmScreen&>(app.app.top());
  CHECK(confirm.onEvent(kUp).kind == Action::Kind::None);  // already at the top
  CHECK(confirm.onEvent(kDown).kind == Action::Kind::Redraw);
  CHECK(confirm.focus() == 1);
  CHECK(confirm.onEvent(kDown).kind == Action::Kind::None);  // only two slabs
  CHECK(confirm.vm().holds == std::array<bool, 4>{false, false, false, false});
  CHECK(confirm.longPressable() == 0);
  CHECK(confirm.onEvent(kHold).kind == Action::Kind::None);
}

TEST_CASE("confirming deletes the file, keeps reading progress, and rescans") {
  Ramp r;
  reader::QuietTheme theme;
  FakeFileSystem fs;
  fs.mkdirs("/books");
  fs.writeAll("/books/Dubliners.epub", "d");
  fs.writeAll("/books/Middlemarch.epub", "m");
  // The one place per-book state would live. Spec 4.0: a delete "never erases
  // reading progress" -- a book that comes back should still know where you were.
  fs.writeAll("/.reader/state/Middlemarch.json", "{\"page\":78}");

  reader::LibraryScreen lib(fs, "/books");
  lib.setVisibleRows(theme.libraryVisibleRows(800, r.fonts));
  lib.onEvent(kDown);  // onto Middlemarch, the last row
  REQUIRE(lib.focus() == 1);
  REQUIRE(lib.focusedItem()->entry.name == "Middlemarch.epub");

  reader::DeleteConfirmScreen confirm(lib);
  CHECK(confirm.vm().title.find("MIDDLEMARCH") != std::string::npos);
  confirm.onEvent(kDown);  // onto DELETE
  const Action a = confirm.onEvent(kConfirm);
  CHECK(a.kind == Action::Kind::PopTo);
  CHECK(a.target == ScreenId::Library);

  CHECK_FALSE(fs.exists("/books/Middlemarch.epub"));
  // The state file is untouched, and so is the rest of the card.
  CHECK(fs.exists("/.reader/state/Middlemarch.json"));
  CHECK(fs.exists("/books/Dubliners.epub"));
  // The list was re-read and the focus pulled back into range rather than left
  // one past the end, which is what would index off the vector on the next paint.
  CHECK(lib.itemCount() == 1);
  CHECK(lib.focus() == 0);
  CHECK(lib.vm().rows.size() == 1);
  CHECK(lib.vm().rows[0].title == "Dubliners");
}

TEST_CASE("the confirmation matches its golden at both geometries") {
  Ramp r;
  reader::QuietTheme theme;
  struct Case {
    int w, h;
    const char* name;
  };
  for (const Case c : {Case{480, 800, "delete_confirm"}, Case{528, 792, "delete_confirm_x3"}}) {
    libapp::LibraryApp app = confirmOver(theme, r.fonts, c.h);
    reader::Framebuffer fb(c.w, c.h);
    app.app.render(fb, r.fonts, theme, reader::Plane::Bw);
    golden::checkGolden(fb, c.name);
  }
}

TEST_CASE("the confirm panel covers the actions panel completely, on both geometries") {
  Ramp r;
  reader::QuietTheme theme;
  // Which is why DeleteConfirm's board can show the LIBRARY behind it while the
  // real stack has the actions panel in between, and still be right: 380 wide
  // against 340, taller, and both centred.
  for (const int w : {480, 528}) {
    const int actionsX = reader::panelLeft(w, 340);
    const int confirmX = reader::panelLeft(w, 380);
    CHECK(confirmX < actionsX);
    CHECK(confirmX + 380 > actionsX + 340);

    const int h = (w == 480) ? 800 : 792;
    libapp::LibraryApp app = confirmOver(theme, r.fonts, h);
    reader::Framebuffer fb(w, h);
    app.app.render(fb, r.fonts, theme, reader::Plane::Bw);
    // Nothing of the actions panel survives. The strip the actions panel's own
    // 2px LEFT BORDER occupies -- x in [actionsX, actionsX + 2) -- falls inside
    // the confirm panel's 20px padding, one pixel left of where its buttons and
    // its text column start, so on this render it is blank paper for the panel's
    // whole height. Any ink there would be the actions panel showing through.
    int leftovers = 0;
    for (int y = h / 2 - 40; y < h / 2 + 40; ++y)
      for (int x = actionsX; x < actionsX + 2; ++x)
        if (!fb.getPixel(x, y)) ++leftovers;
    CHECK(leftovers == 0);
  }
}
