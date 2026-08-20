#include "doctest.h"
#include "reader/dither.h"
#include "reader/framebuffer.h"

static int inkCount(const reader::Framebuffer& fb, int x, int y, int w, int h) {
  int n = 0;
  for (int yy = y; yy < y + h; ++yy)
    for (int xx = x; xx < x + w; ++xx)
      if (!fb.getPixel(xx, yy)) ++n;
  return n;
}

TEST_CASE("dither density rises monotonically with the requested level") {
  int last = -1;
  for (int level = 0; level <= 4; ++level) {
    reader::Framebuffer fb(64, 64);
    reader::ditherRect(fb, 0, 0, 64, 64, level);
    const int n = inkCount(fb, 0, 0, 64, 64);
    CHECK(n > last);
    last = n;
  }
}

TEST_CASE("level 0 leaves the area white and level 4 fills it solid") {
  reader::Framebuffer fb(32, 32);
  reader::ditherRect(fb, 0, 0, 32, 32, 0);
  CHECK(inkCount(fb, 0, 0, 32, 32) == 0);
  reader::ditherRect(fb, 0, 0, 32, 32, 4);
  CHECK(inkCount(fb, 0, 0, 32, 32) == 32 * 32);
}

TEST_CASE("the dither is a clustered dot on the design's 4px pitch") {
  // The board's tint is `radial-gradient(circle, #000 1.1px, transparent 1.3px)`
  // at `background-size: 4px 4px`: one round dot, ~2.4px across, per 4px cell.
  // Level 1 is that case, so level 1 must ink a 2x2 blob in each 4x4 tile and
  // nothing else.
  reader::Framebuffer fb(32, 32);
  reader::ditherRect(fb, 0, 0, 32, 32, 1);
  int inked = 0;
  for (int y = 0; y < 32; ++y)
    for (int x = 0; x < 32; ++x)
      if (!fb.getPixel(x, y)) {
        ++inked;
        // Every inked pixel is in the middle 2x2 of its tile.
        CHECK((x & 3) >= 1);
        CHECK((x & 3) <= 2);
        CHECK((y & 3) >= 1);
        CHECK((y & 3) <= 2);
      }
  CHECK(inked == 32 * 32 / 4);  // a quarter coverage, as a 2x2 of 4x4 is
}

TEST_CASE("no dithered level scatters isolated pixels") {
  // This is the assertion a Bayer matrix fails, and the reason the placeholder
  // cover read denser and grainier than the board's at identical arithmetic
  // coverage: Bayer's whole purpose is to disperse, so at a quarter coverage it
  // inks four lone pixels per tile where a clustered dot inks one blob of four.
  // A lone black pixel on white carries contrast on all four sides and reads
  // heavier than its area. Levels 1..3 must all grow as connected dots.
  for (int level = 1; level <= 3; ++level) {
    CAPTURE(level);
    reader::Framebuffer fb(32, 32);
    reader::ditherRect(fb, 0, 0, 32, 32, level);
    int isolated = 0;
    // Interior only: a pixel on the rect's edge has neighbours the rect does not
    // contain, which would report as isolation that the tiling does not have.
    for (int y = 1; y < 31; ++y)
      for (int x = 1; x < 31; ++x)
        if (!fb.getPixel(x, y) && fb.getPixel(x - 1, y) && fb.getPixel(x + 1, y) &&
            fb.getPixel(x, y - 1) && fb.getPixel(x, y + 1))
          ++isolated;
    CHECK(isolated == 0);
  }
}

TEST_CASE("the dither grid is continuous across adjoining rects") {
  // Phased on absolute framebuffer coordinates, not on each rect's own origin,
  // so two dithered areas that touch do not show a seam where their tiles
  // disagree. Two halves of one region must equal the whole drawn at once.
  reader::Framebuffer split(32, 32), whole(32, 32);
  reader::ditherRect(split, 0, 0, 13, 32, 2);
  reader::ditherRect(split, 13, 0, 19, 32, 2);
  reader::ditherRect(whole, 0, 0, 32, 32, 2);
  int differ = 0;
  for (int y = 0; y < 32; ++y)
    for (int x = 0; x < 32; ++x)
      if (split.getPixel(x, y) != whole.getPixel(x, y)) ++differ;
  CHECK(differ == 0);
}

TEST_CASE("dither stays inside its rect") {
  reader::Framebuffer fb(32, 32);
  reader::ditherRect(fb, 8, 8, 16, 16, 3);
  CHECK(inkCount(fb, 0, 0, 32, 8) == 0);
  CHECK(inkCount(fb, 0, 0, 8, 32) == 0);
  CHECK(inkCount(fb, 24, 0, 8, 32) == 0);
}
