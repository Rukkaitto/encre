#include <array>
#include <memory>
#include <string>

#include "doctest.h"
#include "golden.h"
#include "library_app.h"
#include "ramp.h"
#include "reader/app.h"
#include "reader/components.h"
#include "reader/framebuffer.h"
#include "reader/gesture.h"
#include "reader/screen_delete_confirm.h"
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

TEST_CASE("cancelling leaves the actions panel up; confirming latches the delete") {
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
    // LATCHED, and the stack is left exactly where it was. The removal's
    // consequences -- forgetCardFacts, the Library's rescan, gHomeStale and
    // gLibraryStale -- are all the shell's, and core/ has no filesystem, so this
    // is shaped like Retry, Open and Finish. The shell pops with
    // popTo(facts().returnTo) once the file is gone; nothing here does.
    CHECK(app.app.deleteRequested());
    CHECK(app.app.top().id() == ScreenId::DeleteConfirm);
    CHECK(app.app.depth() == 4);
  }
}

TEST_CASE("the confirmation's focus is two rows, wrapping, and it binds no hold") {
  Ramp r;
  reader::QuietTheme theme;
  libapp::LibraryApp app = confirmOver(theme, r.fonts, 800);
  auto& confirm = static_cast<reader::DeleteConfirmScreen&>(app.app.top());
  // Two slabs, so Up and Down both simply alternate between them.
  CHECK(confirm.onEvent(kUp).kind == Action::Kind::Redraw);
  CHECK(confirm.focus() == 1);
  CHECK(confirm.onEvent(kDown).kind == Action::Kind::Redraw);
  CHECK(confirm.focus() == 0);
  CHECK(confirm.onEvent(kDown).kind == Action::Kind::Redraw);
  CHECK(confirm.focus() == 1);
  CHECK(confirm.vm().holds == std::array<bool, 4>{false, false, false, false});
  CHECK(confirm.longPressable() == 0);
  CHECK(confirm.onEvent(kHold).kind == Action::Kind::None);
}

TEST_CASE("the confirmation names the book from its facts, not from a Library") {
  reader::DeleteConfirmScreen s(
      {"/books/dubliners.epub", "Dubliners", reader::ScreenId::Library});
  // ADJACENT LITERALS, not one: a C++ hex escape is UNBOUNDED, so
  // "\x9CDUBLINERS" parses \x9CD as a single escape. clang refuses it and the
  // ESP32's GCC would have accepted it and emitted the wrong byte. This project
  // has recorded the same trap once already, on the sleep screen's middle dot.
  CHECK(s.vm().title == "DELETE \xE2\x80\x9C" "DUBLINERS" "\xE2\x80\x9D?");
  CHECK(s.facts().path == "/books/dubliners.epub");
}

TEST_CASE("confirming latches the delete rather than doing it") {
  // The removal is the SHELL's: forgetCardFacts, the Library's rescan, gHomeStale
  // and gLibraryStale all live there, and core/ has no filesystem. Shaped like
  // Open/Retry/Finish for that reason.
  reader::DeleteConfirmScreen s(
      {"/books/dubliners.epub", "Dubliners", reader::ScreenId::Library});
  REQUIRE(s.onGesture({reader::Gesture::Next}).kind != reader::Action::Kind::None);
  REQUIRE(s.focus() == 1);
  const reader::Action a = s.onGesture({reader::Gesture::Activate});
  CHECK(a.kind == reader::Action::Kind::Delete);
}

TEST_CASE("the app latches a delete request") {
  Ramp r;
  reader::QuietTheme theme;
  libapp::LibraryApp app = confirmOver(theme, r.fonts, 800);
  CHECK_FALSE(app.app.deleteRequested());
  app.app.dispatch(kDown);  // onto DELETE
  app.app.dispatch(kConfirm);
  CHECK(app.app.deleteRequested());
  app.app.clearDeleteRequest();
  CHECK_FALSE(app.app.deleteRequested());
}

TEST_CASE("where a completed delete returns to comes from the facts") {
  // The whole reason for Facts: BookError over Home's CONTINUE has no Library.
  CHECK(reader::DeleteConfirmScreen({"/b/x.epub", "X", reader::ScreenId::Home})
            .facts().returnTo == reader::ScreenId::Home);
  // ...and the actions panel's route still lands on the Library, which is the
  // default and what the factory's Library fallback fills in.
  CHECK(reader::DeleteConfirmScreen({"/b/x.epub", "X"}).facts().returnTo ==
        reader::ScreenId::Library);
}

TEST_CASE("the factory fills the facts from the Library's focused row") {
  // The fallback the simulator and the goldens take. It answers BOTH facts from
  // the row, so the caption and the path agree about which book this is.
  Ramp r;
  reader::QuietTheme theme;
  libapp::LibraryApp app = confirmOver(theme, r.fonts, 800);
  const auto& confirm = static_cast<const reader::DeleteConfirmScreen&>(app.app.top());
  CHECK(confirm.facts().displayName == "Dubliners");
  CHECK(confirm.facts().path == "/books/Dubliners.epub");
  // ...and it is the LIBRARY's spelling of that path, not a second one. The
  // factory built the join by hand for one release; join() has a root-is-"/"
  // case that is easy to get subtly wrong, and a wrong path here is a delete
  // aimed at the wrong file.
  CHECK(confirm.facts().path == app.library().focusedPath());
  CHECK(confirm.facts().returnTo == ScreenId::Library);
}

TEST_CASE("the factory builds a confirmation from primed facts, with NO Library at all") {
  // THE WHOLE REASON FOR FACTS, and the case the stale guard would have eaten.
  // BookError's `DELETE FILE...` is raised over Home's CONTINUE as well as over a
  // Library row, and Home has no Library under it -- so this factory has never
  // built one and library() is null. A `library_ == nullptr` guard left above the
  // facts check refuses here, silently, which is exactly the defect recorded on
  // the BookDetails case: the screen stays refused for the very reason it was
  // meant to stop being refused.
  reader::DemoScreenFactory factory;
  REQUIRE(factory.library() == nullptr);
  factory.setDeleteFacts({"/books/dubliners.epub", "dubliners.epub", ScreenId::Home});
  std::unique_ptr<reader::Screen> s = factory.create(ScreenId::DeleteConfirm);
  REQUIRE(s != nullptr);
  const auto& confirm = static_cast<const reader::DeleteConfirmScreen&>(*s);
  CHECK(confirm.facts().path == "/books/dubliners.epub");
  CHECK(confirm.facts().returnTo == ScreenId::Home);
  CHECK(confirm.vm().title == "DELETE \xE2\x80\x9C" "DUBLINERS.EPUB" "\xE2\x80\x9D?");

  // And clearing puts it back on the Library fallback -- which, with no Library,
  // is a refusal rather than a substitution.
  factory.clearDeleteFacts();
  CHECK(factory.create(ScreenId::DeleteConfirm) == nullptr);
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
