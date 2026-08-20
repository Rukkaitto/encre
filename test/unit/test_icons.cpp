#include "doctest.h"
#include "reader/framebuffer.h"
#include "reader/icons.h"

TEST_CASE("every icon draws something inside its own box and nothing outside") {
  // Not all icons are 13x13 -- kBattery is 22x12 -- so the box is each icon's
  // own w/h, never a shared constant.
  const reader::Icon* all[] = {&reader::icons::kBack,    &reader::icons::kDot,
                              &reader::icons::kUp,      &reader::icons::kDown,
                              &reader::icons::kChevron, &reader::icons::kBook,
                              &reader::icons::kFolder,  &reader::icons::kBattery};
  for (const reader::Icon* ic : all) {
    reader::Framebuffer fb(48, 48);
    REQUIRE(ic->w > 0);
    REQUIRE(ic->h > 0);
    REQUIRE(8 + ic->w <= 48);
    REQUIRE(8 + ic->h <= 48);
    reader::drawIcon(fb, *ic, 8, 8, reader::Ink::Black);
    int inked = 0, outside = 0;
    for (int y = 0; y < 48; ++y)
      for (int x = 0; x < 48; ++x)
        if (!fb.getPixel(x, y)) {
          ++inked;
          const bool inBox = x >= 8 && y >= 8 && x < 8 + ic->w && y < 8 + ic->h;
          if (!inBox) ++outside;
        }
    CHECK(inked > 0);
    CHECK(outside == 0);
  }
}

TEST_CASE("icons can draw in white for inverted rows") {
  reader::Framebuffer fb(48, 48);
  fb.fillRect(0, 0, 48, 48, false);
  reader::drawIcon(fb, reader::icons::kDot, 8, 8, reader::Ink::White);
  bool anyWhite = false;
  for (int y = 0; y < 48 && !anyWhite; ++y)
    for (int x = 0; x < 48 && !anyWhite; ++x)
      if (fb.getPixel(x, y)) anyWhite = true;
  CHECK(anyWhite);
}
