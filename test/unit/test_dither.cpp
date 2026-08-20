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

TEST_CASE("dither stays inside its rect") {
  reader::Framebuffer fb(32, 32);
  reader::ditherRect(fb, 8, 8, 16, 16, 3);
  CHECK(inkCount(fb, 0, 0, 32, 8) == 0);
  CHECK(inkCount(fb, 0, 0, 8, 32) == 0);
  CHECK(inkCount(fb, 24, 0, 8, 32) == 0);
}
