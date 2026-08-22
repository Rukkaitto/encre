// The no-card prompt: design/SdMissing.dc.html, spec 6.
//
// Two things are worth pinning here and they are different in kind. The retry is
// BEHAVIOUR -- spec 6 asks for a prompt "with retry", and a button that redraws
// the same screen would satisfy every visual check while being useless -- so the
// action it produces and the request the App latches are asserted directly. The
// rest is LAYOUT, and it is asserted against the board's box model rather than
// against transcribed pixel positions: the goldens hold the pixels, and a test
// that repeated them would only pin them twice.
#include <string>
#include <vector>

#include "doctest.h"
#include "golden.h"
#include "ramp.h"
#include "reader/app.h"
#include "reader/components.h"
#include "reader/framebuffer.h"
#include "reader/icons.h"
#include "reader/screen_sd_missing.h"
#include "reader/screens.h"
#include "reader/theme_quiet.h"

using namespace reader;
using ramp::Ramp;

namespace {

struct Box {
  int x0 = 1 << 20, y0 = 1 << 20, x1 = -1, y1 = -1;
  bool empty() const { return x1 < 0; }
  int w() const { return x1 - x0 + 1; }
  int h() const { return y1 - y0 + 1; }
};

// Bounding box of the ink in [yTop, yBot), which is how every layout assertion
// below finds an element: the render is the only evidence about what was drawn.
Box inkBox(const Framebuffer& fb, int yTop, int yBot) {
  Box b;
  for (int y = yTop; y < yBot; ++y)
    for (int x = 0; x < fb.width(); ++x)
      if (!fb.getPixel(x, y)) {
        if (x < b.x0) b.x0 = x;
        if (x > b.x1) b.x1 = x;
        if (y < b.y0) b.y0 = y;
        if (y > b.y1) b.y1 = y;
      }
  return b;
}

bool rowHasInk(const Framebuffer& fb, int y) {
  for (int x = 0; x < fb.width(); ++x)
    if (!fb.getPixel(x, y)) return true;
  return false;
}

// A mark's box is not its ink: both marks on this screen are drawn from an SVG
// with padding inside its own viewBox -- the dot's circle is inset 4.5px in a
// 25px box, the card's outline 2px in a 105px one. So an assertion about where a
// mark's BOX is has to allow for that, and the allowance is read off the mark
// rather than measured off the render it is checking. `Plane::Bw` inks coverage
// >= 2, which is what these renders are.
int firstInkCol(const Icon& icon) {
  for (int x = 0; x < icon.w; ++x)
    for (int y = 0; y < icon.h; ++y)
      if (coverage(icon, x, y) >= 2) return x;
  return icon.w;
}

int firstInkRow(const Icon& icon) {
  for (int y = 0; y < icon.h; ++y)
    for (int x = 0; x < icon.w; ++x)
      if (coverage(icon, x, y) >= 2) return y;
  return icon.h;
}

// The screen's hint bar, rebuilt from the view model the way the theme does, so
// the expected geometry below is derived rather than transcribed.
void hintsOf(const SdMissingViewModel& vm, Hint out[4]) {
  buildHints(kHintSlotMarks, vm.hints, vm.holds, out);
}

const InputEvent kConfirm{Button::Confirm, PressKind::Short};
const InputEvent kLongConfirm{Button::Confirm, PressKind::Long};

}  // namespace

TEST_CASE("Confirm retries the mount, and nothing else on the screen does") {
  SdMissingScreen s;
  CHECK(s.onEvent(kConfirm).kind == Action::Kind::Retry);
  // Repeatable: a retry that failed leaves the user on this screen, and the only
  // thing they can do is try again.
  CHECK(s.onEvent(kConfirm).kind == Action::Kind::Retry);

  // No hold is bound and no slot shows a ring, so a Long is drift between the
  // mask and the view model rather than a second gesture.
  CHECK(s.onEvent(kLongConfirm).kind == Action::Kind::None);
  CHECK(s.longPressable() == 0);
  CHECK(s.longPressable() == hintHoldMask(s.vm().holds));

  // The three dead buttons. Each has an empty hint slot on the board, and a
  // button that does something unadvertised is worse than one that does nothing.
  for (const Button b : {Button::Back, Button::Up, Button::Down, Button::Left, Button::Right,
                         Button::Power}) {
    CAPTURE(buttonName(b));
    CHECK(s.onEvent({b, PressKind::Short}).kind == Action::Kind::None);
  }

  // Mono, like all chrome: this must be the path the goldens below pin.
  CHECK(s.fidelity() == Fidelity::Mono);
  CHECK(s.id() == ScreenId::SdMissing);
}

TEST_CASE("the App latches the retry for the shell, and changes nothing itself") {
  DemoScreenFactory factory;
  App app(std::make_unique<SdMissingScreen>(), factory);
  app.clearDirty();
  REQUIRE(!app.dirty());
  REQUIRE(!app.retryRequested());

  app.dispatch(kConfirm);
  CHECK(app.retryRequested());
  // The mount is the shell's, and so is the screen swap that follows a successful
  // one. A retry that failed changes nothing on the panel, so nothing here asks
  // for a repaint or moves the stack.
  CHECK(!app.dirty());
  CHECK(!app.transition());
  CHECK(app.depth() == 1);
  CHECK(app.top().id() == ScreenId::SdMissing);
  CHECK(!app.sleepRequested());

  app.clearRetryRequest();
  CHECK(!app.retryRequested());
  // And it re-latches: the shell clears the flag before it tries, so a second
  // press after a failed mount must be visible as a second request.
  app.dispatch(kConfirm);
  CHECK(app.retryRequested());
}

TEST_CASE("the factory can build the screen, and it names itself in a log") {
  DemoScreenFactory factory;
  auto s = factory.create(ScreenId::SdMissing);
  REQUIRE(s != nullptr);
  CHECK(s->id() == ScreenId::SdMissing);
  CHECK(std::string(screenName(ScreenId::SdMissing)) == "SD-MISSING");
}

TEST_CASE("the prompt is laid out from the board's box model at both geometries") {
  Ramp r;
  QuietTheme theme;
  const SdMissingScreen screen;
  const SdMissingViewModel& vm = screen.vm();

  struct Case {
    int w, h;
  };
  for (const Case c : {Case{480, 800}, Case{528, 792}}) {
    CAPTURE(c.w);
    Framebuffer fb(c.w, c.h);
    screen.render(fb, r.fonts, theme, Plane::Bw);

    Hint hints[4];
    hintsOf(vm, hints);
    const int barTop = c.h - hintBarHeight(r.fonts, hints);
    const int usableW = c.w - 2 * kMargin;

    // --- the hint bar ------------------------------------------------------
    // Its rule is the only ink that reaches the panel edge; everything else is
    // inside the margins. Overflow at 480 is the failure this project sees first.
    CHECK(rowHasInk(fb, barTop));
    int overflow = 0, firstOverflowY = -1;
    for (int y = 0; y < c.h; ++y) {
      if (y == barTop) continue;
      for (int x = 0; x < c.w; ++x) {
        if (x == kMargin) x = c.w - kMargin;
        if (x < c.w && !fb.getPixel(x, y)) {
          ++overflow;
          if (firstOverflowY < 0) firstOverflowY = y;
        }
      }
    }
    CAPTURE(firstOverflowY);
    CHECK(overflow == 0);

    // Three of the four slots are the board's empty 36px placeholder, so the one
    // hint that exists does NOT start on the margin: it starts a placeholder and
    // a space-between gap in. Measuring an empty slot as zero-wide would put the
    // dot 36px left of here.
    const Font& meta = r.fonts[Role::Meta400];
    const int hintW = icons::kDot.w + kHintIconGap + meta.measure(vm.hints[1], trackingEm(meta, kHintEm));
    const int leftover = usableW - (3 * kHintEmptySlotW + hintW);
    const int expectedX = kMargin + kHintEmptySlotW + (leftover + 1) / 3;
    const Box bar = inkBox(fb, barTop + kHintRuleH, c.h);
    CHECK(bar.x0 == expectedX + firstInkCol(icons::kDot));
    // ...and the slot ends where its label ends, with the two trailing
    // placeholders and their gaps left empty.
    CHECK(bar.x1 < kMargin + kHintEmptySlotW + hintW + leftover / 3 + kHintEmptySlotW);

    // --- the centred column ------------------------------------------------
    const Box column = inkBox(fb, 0, barTop);
    REQUIRE(!column.empty());
    // The card mark, top of the column and centred on the content box.
    CHECK(column.x0 >= kMargin);
    CHECK(column.x1 < c.w - kMargin);
    const Box mark = inkBox(fb, 0, column.y0 + icons::kSdCard.h);
    CHECK(mark.x0 >= centreIn(kMargin, usableW, icons::kSdCard.w));
    CHECK(mark.x1 <= centreIn(kMargin, usableW, icons::kSdCard.w) + icons::kSdCard.w - 1);

    // The button is the bottom of the column: a solid slab of the board's own
    // 260x68 with its label knocked out of it.
    const int slabX = centreIn(kMargin, usableW, 260);
    // A row is the slab's top edge when the WHOLE 260px span is inked, not
    // merely its two ends. Testing the ends alone is a heuristic and it was
    // wrong: any prose row with a glyph at both x=slabX and x=slabX+259 answers
    // it, and once the chrome ramp gained kerning one of them did -- this
    // reported the slab at y=420, in the middle of the paragraph, and four
    // assertions below then failed on a screen that was drawn correctly. The
    // BOARD's own render trips the two-end version too, at y=378, so the
    // heuristic never held; the unkerned firmware just happened not to break
    // it. The slab is a solid rectangle 68px tall with a 33px label centred in
    // it, so its first 25 rows are fully inked and no line of type can be.
    int slabTop = -1;
    for (int y = 0; y < barTop && slabTop < 0; ++y) {
      bool solid = true;
      for (int x = slabX; solid && x < slabX + 260; ++x)
        if (fb.getPixel(x, y)) solid = false;
      if (solid) slabTop = y;
    }
    REQUIRE(slabTop > 0);
    CHECK(slabTop + kActionH - 1 == column.y1);
    int slabPaperEdges = 0;
    for (const int y : {slabTop, slabTop + kActionH - 1})
      for (int x = slabX; x < slabX + 260; ++x)
        if (fb.getPixel(x, y)) ++slabPaperEdges;
    CHECK(slabPaperEdges == 0);
    // Nothing below the slab but paper, up to the bar's rule: the column ends
    // there and the board draws nothing between.
    int inkBelowSlab = 0;
    for (int y = slabTop + kActionH; y < barTop; ++y)
      if (rowHasInk(fb, y)) ++inkBelowSlab;
    CHECK(inkBelowSlab == 0);
    // The label reversed out of it.
    int white = 0;
    for (int y = slabTop; y < slabTop + kActionH; ++y)
      for (int x = slabX; x < slabX + 260; ++x)
        if (fb.getPixel(x, y)) ++white;
    CHECK(white > 100);

    // `justify-content: center`: the space above the column equals the space
    // below it, to the pixel the halving loses. This is the assertion that fails
    // if any item's height is pinned instead of derived -- a wrong height moves
    // the whole column by half of its error. Measured off the column's BOX: its
    // top is the card mark's box, which starts a couple of rows above its first
    // inked one, and its bottom is the slab's own edge.
    const int above = column.y0 - firstInkRow(icons::kSdCard);
    const int below = barTop - (slabTop + kActionH);
    CHECK(above >= below - 1);
    CHECK(above <= below + 1);

    // --- the paragraph -----------------------------------------------------
    // The board's three lines, in the board's own break positions, and the same
    // three on both panels because `max-width` binds before the margins do.
    const Prose prose = wrapProse(r.fonts[Role::Body400], vm.message, kProseMaxW, kProseLeadEm);
    CHECK(prose.lineCount() == 3);
    CHECK(prose.lines[0] == "Books, articles, fonts, and");
    CHECK(prose.lines[1] == "reading progress live on the");
    CHECK(prose.lines[2] == "card. Insert one, then retry.");
  }
}

TEST_CASE("SdMissing matches its golden at both geometries") {
  Ramp r;
  QuietTheme theme;
  const SdMissingScreen screen;
  // The shipped path: the screen declares no fidelity, so it takes Mono -- one
  // pass, one 1-bit frame, hard-thresholded edges. Asserted before the plane is
  // named, so a change to the path fails here rather than leaving the goldens
  // pinning something nothing paints.
  REQUIRE(screen.fidelity() == Fidelity::Mono);

  struct Case {
    int w, h;
    const char* name;
  };
  for (const Case c : {Case{480, 800, "sd_missing"}, Case{528, 792, "sd_missing_x3"}}) {
    Framebuffer fb(c.w, c.h);
    screen.render(fb, r.fonts, theme, Plane::Bw);
    golden::checkGolden(fb, c.name);
  }
}
