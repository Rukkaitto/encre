#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/components.h"
#include "reader/fontset.h"
#include "reader/framebuffer.h"

static std::vector<uint8_t> slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  REQUIRE(f.good());
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

struct Fixture {
  std::vector<uint8_t> a, b, c, d, e;
  reader::FontSet fonts;
  Fixture() {
    const std::string dir = std::string(ASSETS_DIR) + "/built/";
    a = slurp(dir + "spacegrotesk_500_12.rfnt");
    b = slurp(dir + "spacegrotesk_500_13.rfnt");
    c = slurp(dir + "spacegrotesk_700_14.rfnt");
    d = slurp(dir + "spacegrotesk_500_17.rfnt");
    e = slurp(dir + "spacegrotesk_700_24.rfnt");
    fonts.load(reader::Role::Meta, a.data(), a.size());
    fonts.load(reader::Role::Label, b.data(), b.size());
    fonts.load(reader::Role::Value, c.data(), c.size());
    fonts.load(reader::Role::Body, d.data(), d.size());
    fonts.load(reader::Role::Title, e.data(), e.size());
    REQUIRE(fonts.ready());
  }
};

TEST_CASE("the header band right-aligns its value on any canvas width") {
  Fixture f;
  for (int width : {480, 528}) {
    reader::Framebuffer fb(width, 200);
    const int h = reader::drawHeaderBand(fb, f.fonts, "NOW READING", "87%");
    CHECK(h > 0);
    // Ink must reach close to the right margin, and never past it. The scan
    // stops above the band's 2px rule: the rule is deliberately full-bleed (the
    // design canvas has `border-bottom: 2px solid` spanning the whole width),
    // so including its rows would only ever report width - 1 and would say
    // nothing about where the value landed.
    int rightmost = -1;
    for (int y = 0; y < h - 2; ++y)
      for (int x = 0; x < width; ++x)
        if (!fb.getPixel(x, y) && x > rightmost) rightmost = x;
    CHECK(rightmost <= width - reader::kMargin);
    CHECK(rightmost > width - reader::kMargin - 60);
    // The rule itself spans edge to edge, on either width.
    CHECK_FALSE(fb.getPixel(0, h - 1));
    CHECK_FALSE(fb.getPixel(width - 1, h - 1));
  }
}

TEST_CASE("a focused row inverts: black field, white text") {
  Fixture f;
  reader::Framebuffer fb(480, 120);
  reader::drawRow(fb, f.fonts, 0, "LIBRARY", "12", /*focused=*/true);
  // The row's field is black...
  CHECK_FALSE(fb.getPixel(2, 10));
  // ...and contains white glyph pixels.
  bool anyWhite = false;
  for (int y = 0; y < 56 && !anyWhite; ++y)
    for (int x = 0; x < 480 && !anyWhite; ++x)
      if (fb.getPixel(x, y)) anyWhite = true;
  CHECK(anyWhite);
}

// Lowest and highest ink row inside [x0, x1), ignoring the bar's top rule.
struct Rows {
  int top = 9999, bottom = -1;
};
static Rows inkRows(const reader::Framebuffer& fb, int barTop, int x0, int x1) {
  Rows r;
  for (int y = barTop + 1; y < fb.height(); ++y)
    for (int x = x0; x < x1; ++x)
      if (!fb.getPixel(x, y)) {
        if (y < r.top) r.top = y;
        if (y > r.bottom) r.bottom = y;
      }
  return r;
}

TEST_CASE("a hold line stays inside the bar wherever its slot sits") {
  Fixture f;
  // The real bars carry the hold on the Confirm slot, not the first one, so the
  // vertical placement cannot be decided by looking at slot 0 alone.
  for (int holdSlot : {0, 1, 2, 3}) {
    reader::Framebuffer fb(480, 120);
    reader::Hint hints[4] = {{&reader::icons::kBack, "BACK", ""},
                             {&reader::icons::kDot, "OPEN", ""},
                             {&reader::icons::kUp, "UP", ""},
                             {&reader::icons::kDown, "DOWN", ""}};
    hints[holdSlot].hold = "HOLD: Q";  // Q descends below the baseline
    int slotX[4] = {};
    const int barH = reader::drawHintBar(fb, f.fonts, hints, slotX);
    const int barTop = fb.height() - barH;
    const Rows all = inkRows(fb, barTop, 0, 480);
    // Nothing may reach the last row: that is where a clipped descender lands.
    CHECK(all.bottom < fb.height() - 1);
    CHECK(all.top > barTop);
    // The two-line slot straddles the bar's centre rather than hanging below it.
    const int slotEnd = holdSlot < 3 ? slotX[holdSlot + 1] : 480;
    const Rows held = inkRows(fb, barTop, slotX[holdSlot], slotEnd);
    const int barCentre = barTop + barH / 2;
    CHECK(held.top < barCentre);
    CHECK(held.bottom > barCentre);
  }
}

TEST_CASE("a hold line in one slot does not move the other slots") {
  Fixture f;
  reader::Hint plain[4] = {{&reader::icons::kBack, "BACK", ""},
                           {&reader::icons::kDot, "OPEN", ""},
                           {&reader::icons::kUp, "UP", ""},
                           {&reader::icons::kDown, "DOWN", ""}};
  reader::Framebuffer noHold(480, 120);
  int ax[4] = {};
  const int barH = reader::drawHintBar(noHold, f.fonts, plain, ax);
  const int barTop = noHold.height() - barH;
  const Rows before = inkRows(noHold, barTop, ax[2], ax[3]);

  reader::Hint withHold[4] = {plain[0], plain[1], plain[2], plain[3]};
  withHold[0].hold = "HOLD";
  reader::Framebuffer held(480, 120);
  int bx[4] = {};
  reader::drawHintBar(held, f.fonts, withHold, bx);
  const Rows after = inkRows(held, barTop, bx[2], bx[3]);

  CHECK(after.top == before.top);
  CHECK(after.bottom == before.bottom);
}

TEST_CASE("hint slots distribute across the canvas and never overlap") {
  Fixture f;
  for (int width : {480, 528}) {
    reader::Framebuffer fb(width, 120);
    const reader::Hint hints[4] = {{&reader::icons::kBack, "BACK", ""},
                                   {&reader::icons::kDot, "OPEN", "HOLD"},
                                   {&reader::icons::kUp, "UP", ""},
                                   {&reader::icons::kDown, "DOWN", ""}};
    int slotX[4] = {};
    reader::drawHintBar(fb, f.fonts, hints, slotX);
    for (int i = 1; i < 4; ++i) CHECK(slotX[i] > slotX[i - 1]);
    CHECK(slotX[0] >= reader::kMargin);
    CHECK(slotX[3] < width - reader::kMargin);
  }
}
