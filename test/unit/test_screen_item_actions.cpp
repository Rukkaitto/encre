#include <array>
#include <string>

#include "doctest.h"
#include "golden.h"
#include "library_app.h"
#include "ramp.h"
#include "reader/app.h"
#include "reader/components.h"
#include "reader/framebuffer.h"
#include "reader/screen_item_actions.h"
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

// The board's own state: the Library focused on its sixth row, Dubliners, with
// the actions panel over it. `downs` is 5 because LibraryActions' parent copy
// focuses that row -- Library.dc.html focuses the second, and the two boards
// genuinely disagree.
libapp::LibraryApp actionsOver(const reader::Theme& theme, const reader::FontSet& fonts,
                              int panelH) {
  libapp::LibraryApp app(theme, fonts, panelH, 5);
  app.app.dispatch(kHold);
  REQUIRE(app.app.top().id() == ScreenId::ItemActions);
  return app;
}

}  // namespace

TEST_CASE("the actions overlay is captioned with the book it acts on") {
  Ramp r;
  reader::QuietTheme theme;
  libapp::LibraryApp app = actionsOver(theme, r.fonts, 800);
  const auto& actions = static_cast<const reader::ItemActionsScreen&>(app.app.top());

  // The book under the panel, and its state. Both are the board's: `DUBLINERS`
  // and `31%`. The theme shouts the title; the view-model carries it as the
  // Library knows it.
  CHECK(actions.vm().title == "Dubliners");
  CHECK(actions.vm().status == "31%");

  // The board's four rows, in the board's order, with chevrons on the two that
  // lead somewhere.
  REQUIRE(actions.vm().actions.size() == 4);
  CHECK(actions.vm().actions[0].label == "Open");
  CHECK(actions.vm().actions[0].discloses);
  CHECK(actions.vm().actions[1].label == "Book details");
  CHECK(actions.vm().actions[1].discloses);
  CHECK(actions.vm().actions[2].label == "Mark as finished");
  CHECK_FALSE(actions.vm().actions[2].discloses);
  CHECK(actions.vm().actions[3].label == "Delete\xE2\x80\xA6");
  CHECK_FALSE(actions.vm().actions[3].discloses);
  CHECK(actions.vm().focusedAction == 0);
}

TEST_CASE("the actions overlay is an overlay, and input stops at the top of the stack") {
  Ramp r;
  reader::QuietTheme theme;
  libapp::LibraryApp app = actionsOver(theme, r.fonts, 800);
  CHECK(app.app.top().isOverlay());
  CHECK(app.app.depth() == 3);  // Home, Library, the overlay

  // The Library's focus must not move while the panel is up: it is behind a veil,
  // so a focus that moved there would surface only after the panel was dismissed
  // and would read as a rendering bug rather than a dispatch one.
  const int before = app.library().focus();
  app.app.dispatch(kDown);
  app.app.dispatch(kDown);
  CHECK(app.library().focus() == before);
  // ...and the fidelity and the long-press mask come from the top alone.
  CHECK(app.app.longPressable() == 0);
  CHECK(app.app.top().fidelity() == reader::Fidelity::Mono);
}

TEST_CASE("the actions overlay's four rows do what the plan says, including nothing") {
  Ramp r;
  reader::QuietTheme theme;
  libapp::LibraryApp app = actionsOver(theme, r.fonts, 800);
  auto& actions = static_cast<reader::ItemActionsScreen&>(app.app.top());

  // Open: the Reader is Phase 3.
  CHECK(actions.onEvent(kConfirm).kind == Action::Kind::None);

  CHECK(actions.onEvent(kDown).kind == Action::Kind::Redraw);
  const Action details = actions.onEvent(kConfirm);
  CHECK(details.kind == Action::Kind::Push);
  CHECK(details.target == ScreenId::BookDetails);

  // Mark as finished: NOTHING, and deliberately. Per-book state has no format
  // yet, and a state file invented here would commit 2C-3 and Phase 3 to a
  // schema chosen by the screen with the least stake in it -- on a card that
  // outlives the firmware.
  CHECK(actions.onEvent(kDown).kind == Action::Kind::Redraw);
  CHECK(actions.focus() == 2);
  CHECK(actions.onEvent(kConfirm).kind == Action::Kind::None);

  CHECK(actions.onEvent(kDown).kind == Action::Kind::Redraw);
  const Action del = actions.onEvent(kConfirm);
  CHECK(del.kind == Action::Kind::Push);
  CHECK(del.target == ScreenId::DeleteConfirm);

  // The end of a four-row list, so nothing moves and nothing repaints.
  CHECK(actions.onEvent(kDown).kind == Action::Kind::None);
  CHECK(actions.focus() == 3);
  // ...and back up to the top, clamping there.
  for (int i = 0; i < 3; ++i) CHECK(actions.onEvent(kUp).kind == Action::Kind::Redraw);
  CHECK(actions.onEvent(kUp).kind == Action::Kind::None);
  CHECK(actions.focus() == 0);

  // Back dismisses the panel.
  CHECK(actions.onEvent(kBack).kind == Action::Kind::Pop);
  // No slot shows a ring, so no hold is bound; one arriving anyway is ignored
  // rather than treated as a press.
  CHECK(actions.vm().holds == std::array<bool, 4>{false, false, false, false});
  CHECK(actions.longPressable() == 0);
  CHECK(actions.onEvent(kHold).kind == Action::Kind::None);
}

TEST_CASE("the actions overlay matches its golden at both geometries") {
  Ramp r;
  reader::QuietTheme theme;
  struct Case {
    int w, h;
    const char* name;
  };
  for (const Case c : {Case{480, 800, "library_actions"}, Case{528, 792, "library_actions_x3"}}) {
    libapp::LibraryApp app = actionsOver(theme, r.fonts, c.h);
    reader::Framebuffer fb(c.w, c.h);
    // App::render, which is the whole point: the parent has to be under the
    // panel. top().render() alone would draw a panel floating on white, and the
    // golden would happily pin that.
    app.app.render(fb, r.fonts, theme, reader::Plane::Bw);
    golden::checkGolden(fb, c.name);
  }
}

TEST_CASE("the veiled parent is visible behind an opaque panel, centred on either panel") {
  Ramp r;
  reader::QuietTheme theme;
  for (const int w : {480, 528}) {
    const int h = (w == 480) ? 800 : 792;
    libapp::LibraryApp app = actionsOver(theme, r.fonts, h);
    reader::Framebuffer fb(w, h);
    app.app.render(fb, r.fonts, theme, reader::Plane::Bw);

    // The panel is 340 wide and centred, so its border is where panelLeft says.
    const int px = reader::panelLeft(w, 340);
    CHECK(px == (w - 340) / 2);
    // Its vertical middle is somewhere in the band around the screen's centre;
    // find the panel's top border by walking down the panel's own left column.
    int top = -1;
    for (int y = 0; y < h && top < 0; ++y)
      if (!fb.getPixel(px, y) && !fb.getPixel(px + 1, y) && !fb.getPixel(px + 339, y)) top = y;
    REQUIRE(top > 0);
    // Opaque: two pixels inside the border is paper, whatever the parent drew
    // there -- and the parent DID draw there, since the panel covers the middle
    // of a list of rows.
    CHECK(fb.getPixel(px + 3, top + 3));
    // Veiled, not blank: the parent's header band rule survives above the panel,
    // knocked back to the veil's 4-of-9 density rather than erased.
    //
    // BOTH rows of the 2px rule together, not one of them: a 3px grid on a 2px
    // rule does not divide evenly, and which row loses depends on where the rule
    // landed. Here row 64 falls on the veil's fully-whitened middle row and
    // vanishes outright while row 65 keeps two thirds -- 4 of 9 over the pair,
    // which is the density the board's `.dim-veil` states, but nothing a
    // per-scanline assertion could see.
    int bandInk = 0;
    for (int y : {64, 65})
      for (int x = 0; x < w; ++x)
        if (!fb.getPixel(x, y)) ++bandInk;
    CHECK(bandInk > 2 * w / 4);
    CHECK(bandInk < 2 * w * 3 / 5);
  }
}
