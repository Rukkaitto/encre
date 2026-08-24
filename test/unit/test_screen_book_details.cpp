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
#include "reader/screen_book_details.h"
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
const InputEvent kConfirm{Button::Confirm, PressKind::Short};
const InputEvent kBack{Button::Back, PressKind::Short};
const InputEvent kUp{Button::Up, PressKind::Short};
const InputEvent kHold{Button::Confirm, PressKind::Long};

// Dubliners' details, reached the way the board's user reaches them: the sixth
// Library row, a hold, then `Book details`.
libapp::LibraryApp detailsOf(const reader::Theme& theme, const reader::FontSet& fonts,
                             int panelH) {
  libapp::LibraryApp app(theme, fonts, panelH, 5);
  app.app.dispatch(kHold);
  app.app.dispatch(kDown);  // onto Book details
  app.app.dispatch(kConfirm);
  REQUIRE(app.app.top().id() == ScreenId::BookDetails);
  return app;
}

}  // namespace

TEST_CASE("book details is a whole screen, not an overlay") {
  Ramp r;
  reader::QuietTheme theme;
  libapp::LibraryApp app = detailsOf(theme, r.fonts, 800);
  // Checked against the board rather than assumed from its neighbours: it has no
  // veil div, no panel, and its own header band and hint bar. Its `.dim-veil`
  // rule is declared and never used.
  CHECK_FALSE(app.app.top().isOverlay());
  // So App::render paints this alone -- the Library and the actions panel beneath
  // it are not drawn at all, which is what makes a full-screen render cost one
  // screen's worth of text and not three.
  reader::Framebuffer fb(480, 800);
  fb.clear(false);
  app.app.render(fb, r.fonts, theme, reader::Plane::Bw);
  // Cleared, which an overlay must not do and this must: the top-left corner is
  // paper even though the framebuffer arrived solid ink.
  CHECK(fb.getPixel(1, 1));
}

TEST_CASE("book details shows the board's fields, and leaves the unknowable blank") {
  Ramp r;
  reader::QuietTheme theme;
  libapp::LibraryApp app = detailsOf(theme, r.fonts, 800);
  const auto& details = static_cast<const reader::BookDetailsScreen&>(app.app.top());

  CHECK(details.vm().title == "Dubliners");
  // Sentence case here, where the Library row's meta line is the caps run its own
  // board sets. Two fields for one fact, because the two boards state two
  // different runs and ASCII folding would render the diaeresis in another row's
  // author as a lowercase letter.
  CHECK(details.vm().author == "James Joyce");
  CHECK(details.vm().format == "EPUB");

  // The board's FIVE rows, in the board's order. `Added` was a sixth and is gone: it
  // wanted a file timestamp and DirEntry is {name, isDir, size}.
  REQUIRE(details.vm().fields.size() == 5);
  CHECK(details.vm().fields[0].label == "Progress");
  // `Current chapter`, not `Current story` -- every other slot on the device that names
  // this thing calls it a chapter, and one screen calling it a story was the odd one out.
  CHECK(details.vm().fields[1].label == "Current chapter");
  CHECK(details.vm().fields[2].label == "Bookmarks");
  CHECK(details.vm().fields[3].label == "File size");
  CHECK(details.vm().fields[4].label == "Location");
  // Two values are real today: the file's size, and where it lives.
  CHECK(details.vm().fields[3].value == "0.4 MB");
  CHECK(details.vm().fields[4].value == "/BOOKS/");
  // And one is honestly zero rather than blank: there is no way to make a
  // bookmark yet, so nought is a fact.
  CHECK(details.vm().fields[2].value == "0");
}

TEST_CASE("on a card, the fields that need EPUB metadata are blank rather than invented") {
  Ramp r;
  reader::QuietTheme theme;
  FakeFileSystem fs;
  fs.mkdirs("/books");
  fs.writeAll("/books/Walden.txt", std::string(700 * 1024, 'w'));
  reader::LibraryScreen lib(fs, "/books");
  lib.setVisibleRows(theme.libraryVisibleRows(800, r.fonts));
  // Built from FACTS, as the screen now is -- the Library is one source of them and the
  // Reader is the other, which is what lets the reader menu's `About this book` work from
  // a stack with no Library on it. Derived here the way the factory derives them.
  const reader::LibraryItem* it = lib.focusedItem();
  REQUIRE(it != nullptr);
  reader::BookDetailsScreen::Facts f;
  f.title = std::string(it->entry.title());
  f.fileName = it->entry.name;
  f.directory = lib.path();
  f.bytes = it->entry.size;
  reader::BookDetailsScreen details(f);

  CHECK(details.vm().title == "Walden");
  CHECK(details.vm().format == "TXT");
  CHECK(details.vm().author.empty());
  // No subtitle field at all now: no real book carries the data, and a field that can
  // never be filled reads as a failure to load.
  // Blank, not "0%" and not a fabricated date: a row with no value is honest and
  // a made-up one is a claim.
  CHECK(details.vm().fields[0].value.empty());  // Progress -- this book has no sidecar
  CHECK(details.vm().fields[1].value.empty());  // Current chapter, likewise
  // The two the filesystem knows.
  CHECK(details.vm().fields[3].value == "0.7 MB");
  CHECK(details.vm().fields[4].value == "/BOOKS/");
  // ...and it renders with two of its five values missing, which is the state a book
  // nobody has opened is legitimately in.
  reader::Framebuffer fb(480, 800);
  details.render(fb, r.fonts, theme, reader::Plane::Bw);
}

TEST_CASE("book details binds Back and nothing else") {
  Ramp r;
  reader::QuietTheme theme;
  libapp::LibraryApp app = detailsOf(theme, r.fonts, 800);
  auto& details = static_cast<reader::BookDetailsScreen&>(app.app.top());
  // One label and three of the boards' dead-button placeholders: there is nothing
  // here to move a focus through or to select, and the board draws exactly that.
  CHECK(details.vm().hints == std::array<std::string, 4>{"BACK", "", "", ""});
  CHECK(details.vm().holds == std::array<bool, 4>{false, false, false, false});
  CHECK(details.longPressable() == 0);
  CHECK(details.onEvent(kConfirm).kind == Action::Kind::None);
  CHECK(details.onEvent(kUp).kind == Action::Kind::None);
  CHECK(details.onEvent(kDown).kind == Action::Kind::None);
  CHECK(details.onEvent(kHold).kind == Action::Kind::None);
  // Back returns to the actions panel it was opened from.
  CHECK(details.onEvent(kBack).kind == Action::Kind::Pop);
  app.app.dispatch(kBack);
  CHECK(app.app.top().id() == ScreenId::ItemActions);
}

TEST_CASE("book details matches its golden at both geometries") {
  Ramp r;
  reader::QuietTheme theme;
  struct Case {
    int w, h;
    const char* name;
  };
  for (const Case c : {Case{480, 800, "book_details"}, Case{528, 792, "book_details_x3"}}) {
    libapp::LibraryApp app = detailsOf(theme, r.fonts, c.h);
    REQUIRE(app.app.top().fidelity() == reader::Fidelity::Mono);
    reader::Framebuffer fb(c.w, c.h);
    app.app.render(fb, r.fonts, theme, reader::Plane::Bw);
    golden::checkGolden(fb, c.name);
  }
}

TEST_CASE("book details' rules land where the board's do") {
  Ramp r;
  reader::QuietTheme theme;
  // RE-MEASURED off design/BookDetails.dc.html after `Added` and the subtitle were
  // removed: the band's 2px rule at 64-65, the 2px rule above the fields still at
  // 290-291, then FOUR field rules (five rows, and the last has none), and the hint
  // bar's at h-64.
  //
  // The block above did NOT move when the subtitle went, and that is worth knowing
  // rather than assuming: the cover is 180px tall and the column was shorter than it,
  // so the block's height is the COVER's and losing a column run changed nothing above
  // the fields.
  for (const int h : {800, 792}) {
    libapp::LibraryApp app = detailsOf(theme, r.fonts, h);
    reader::Framebuffer fb(480, h);
    app.app.render(fb, r.fonts, theme, reader::Plane::Bw);
    auto fullWidthRule = [&](int y) {
      for (int x = 0; x < fb.width(); ++x)
        if (fb.getPixel(x, y)) return false;
      return true;
    };
    for (const int y : {64, 65, 290, 291, 356, 421, 486, 551}) CHECK(fullWidthRule(y));
    CHECK(fullWidthRule(h - 64));
    // The last field row has no rule: the board leaves the list's bottom edge
    // open above the slack, as the Library's does.
    CHECK_FALSE(fullWidthRule(616));
  }
}

// --- Built from facts, with no Library anywhere ---------------------------------

TEST_CASE("BOOK DETAILS BUILDS FROM FACTS WITH NO LIBRARY ON THE STACK") {
  // THE CASE THE WHOLE CHANGE EXISTS FOR, and nothing covered it: a `library_ == nullptr`
  // guard survived above the facts check, so `About this book` from a Reader opened
  // through Home's CONTINUE was refused before the facts were consulted. Every test
  // passed before and after the fix, because every one of them had a Library.
  reader::DemoScreenFactory f;  // no Library ever built
  reader::BookDetailsScreen::Facts facts;
  facts.title = "Le Fleau";
  facts.author = "Stephen King";
  facts.fileName = "Le Fleau.epub";
  facts.directory = "/books";
  facts.progress = "42%";
  facts.chapter = "LIVRE I";
  facts.bytes = 12'700'000;
  f.setDetailsFacts(facts);

  auto scr = f.create(reader::ScreenId::BookDetails);
  REQUIRE(scr != nullptr);
  const auto& vm = static_cast<reader::BookDetailsScreen*>(scr.get())->vm();
  CHECK(vm.title == "Le Fleau");
  CHECK(vm.author == "Stephen King");
  CHECK(vm.format == "EPUB");
  REQUIRE(vm.fields.size() == 5);
  CHECK(vm.fields[0].value == "42%");
  CHECK(vm.fields[1].value == "LIVRE I");
  CHECK(vm.fields[4].value == "/BOOKS/");
}

TEST_CASE("with neither facts nor a Library it is refused, not built empty") {
  // A screen with nothing on it looks exactly like a screen that failed to load, and
  // this factory refuses rather than substituting -- the rule the Reader established.
  reader::DemoScreenFactory f;
  CHECK(f.create(reader::ScreenId::BookDetails) == nullptr);
}

TEST_CASE("clearing the facts puts the screen back on the Library's row") {
  // The Library path clears them, and it has to: opening details from the Library after
  // opening them from a book would otherwise show the BOOK -- a stale answer wearing the
  // right screen's clothes.
  Ramp r;
  reader::QuietTheme theme;
  FakeFileSystem fs;
  fs.mkdirs("/books");
  fs.writeAll("/books/Middlemarch.epub", std::string(400 * 1024, 'm'));
  reader::DemoScreenFactory f(fs, "/books");
  f.setLibraryVisibleRows(theme.libraryVisibleRows(800, r.fonts));
  auto lib = f.create(reader::ScreenId::Library);
  REQUIRE(lib != nullptr);

  reader::BookDetailsScreen::Facts facts;
  facts.title = "A DIFFERENT BOOK";
  facts.fileName = "other.epub";
  f.setDetailsFacts(facts);
  auto fromFacts = f.create(reader::ScreenId::BookDetails);
  REQUIRE(fromFacts != nullptr);
  CHECK(static_cast<reader::BookDetailsScreen*>(fromFacts.get())->vm().title ==
        "A DIFFERENT BOOK");

  f.clearDetailsFacts();
  auto fromLibrary = f.create(reader::ScreenId::BookDetails);
  REQUIRE(fromLibrary != nullptr);
  CHECK(static_cast<reader::BookDetailsScreen*>(fromLibrary.get())->vm().title !=
        "A DIFFERENT BOOK");
}
