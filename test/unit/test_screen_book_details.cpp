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

  // The board's six rows, in the board's order.
  REQUIRE(details.vm().fields.size() == 6);
  CHECK(details.vm().fields[0].label == "Progress");
  CHECK(details.vm().fields[1].label == "Current story");
  CHECK(details.vm().fields[2].label == "Bookmarks");
  CHECK(details.vm().fields[3].label == "File size");
  CHECK(details.vm().fields[4].label == "Added");
  CHECK(details.vm().fields[5].label == "Location");
  // Two values are real today: the file's size, and where it lives.
  CHECK(details.vm().fields[3].value == "0.4 MB");
  CHECK(details.vm().fields[5].value == "/BOOKS/");
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
  reader::BookDetailsScreen details(lib);

  CHECK(details.vm().title == "Walden");
  CHECK(details.vm().format == "TXT");
  CHECK(details.vm().author.empty());
  CHECK(details.vm().subtitle.empty());
  // Blank, not "0%" and not a fabricated date: a row with no value is honest and
  // a made-up one is a claim.
  CHECK(details.vm().fields[0].value.empty());  // Progress
  CHECK(details.vm().fields[1].value.empty());  // Current story
  CHECK(details.vm().fields[4].value.empty());  // Added
  // The two the filesystem knows.
  CHECK(details.vm().fields[3].value == "0.7 MB");
  CHECK(details.vm().fields[5].value == "/BOOKS/");
  // ...and it renders with four of its six values missing, which is the state the
  // device is in until Phase 3.
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
  // Measured off design/BookDetails.dc.html in Chrome: the band's 2px rule at
  // 64-65, the 2px rule above the fields at 290-291, then the five field rules,
  // and the hint bar's at 736 (728 on the shorter X3 panel).
  for (const int h : {800, 792}) {
    libapp::LibraryApp app = detailsOf(theme, r.fonts, h);
    reader::Framebuffer fb(480, h);
    app.app.render(fb, r.fonts, theme, reader::Plane::Bw);
    auto fullWidthRule = [&](int y) {
      for (int x = 0; x < fb.width(); ++x)
        if (fb.getPixel(x, y)) return false;
      return true;
    };
    for (const int y : {64, 65, 290, 291, 356, 421, 486, 551, 616}) CHECK(fullWidthRule(y));
    CHECK(fullWidthRule(h - 64));
    // The last field row has no rule: the board leaves the list's bottom edge
    // open above the slack, as the Library's does.
    CHECK_FALSE(fullWidthRule(681));
  }
}
