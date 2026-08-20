// A single corner pixel cannot distinguish a correct rotation from one with a
// swapped or mis-signed coordinate term, so these tests use an asymmetric
// multi-pixel pattern with no rotational or mirror symmetry.
#include <utility>
#include <vector>

#include "doctest.h"
#include "reader/framebuffer.h"
#include "reader/rotate.h"

using reader::Framebuffer;

namespace {

struct Point {
  int x, y;
};

// Distinct distances from each edge and from the diagonal, so any coordinate
// mix-up moves at least one point somewhere the expectations do not allow.
const std::vector<Point> kPattern = {{0, 0}, {1, 0}, {0, 2}, {3, 5}, {7, 1}, {2, 15}, {7, 15}};

void inkPattern(Framebuffer& fb) {
  fb.clear(true);
  for (const Point& p : kPattern) fb.setPixel(p.x, p.y, false);
}

// Fails on the first mismatch, naming the coordinate, so a broken mapping says
// which pixel moved rather than just "false".
void checkOnlyInkAt(const Framebuffer& fb, const std::vector<Point>& ink) {
  for (int y = 0; y < fb.height(); ++y) {
    for (int x = 0; x < fb.width(); ++x) {
      bool expectInk = false;
      for (const Point& p : ink)
        if (p.x == x && p.y == y) expectInk = true;
      const bool isInk = !fb.getPixel(x, y);
      if (isInk != expectInk) {
        INFO("mismatch at (", x, ", ", y, "): expected ", expectInk ? "ink" : "white");
        REQUIRE(isInk == expectInk);
      }
    }
  }
}

}  // namespace

TEST_CASE("rotate90CW maps portrait to landscape") {
  Framebuffer portrait(8, 16);
  Framebuffer landscape(16, 8);
  inkPattern(portrait);
  reader::rotate90CW(portrait, landscape);

  // Clockwise: (x, y) -> (srcH - 1 - y, x). Top-left goes to top-right.
  std::vector<Point> expected;
  for (const Point& p : kPattern) expected.push_back({portrait.height() - 1 - p.y, p.x});
  checkOnlyInkAt(landscape, expected);
  CHECK_FALSE(landscape.getPixel(15, 0));  // portrait (0,0) -> landscape (15,0)
  CHECK(landscape.getPixel(0, 0));
}

TEST_CASE("rotate90CCW maps portrait to landscape the other way") {
  Framebuffer portrait(8, 16);
  Framebuffer landscape(16, 8);
  inkPattern(portrait);
  reader::rotate90CCW(portrait, landscape);

  // Counter-clockwise: (x, y) -> (y, srcW - 1 - x). Top-left goes to bottom-left.
  std::vector<Point> expected;
  for (const Point& p : kPattern) expected.push_back({p.y, portrait.width() - 1 - p.x});
  checkOnlyInkAt(landscape, expected);
  CHECK_FALSE(landscape.getPixel(0, 7));  // portrait (0,0) -> landscape (0,7)
  CHECK(landscape.getPixel(0, 0));
}

TEST_CASE("CW then CCW is the identity") {
  Framebuffer portrait(8, 16);
  inkPattern(portrait);
  Framebuffer landscape(16, 8);
  Framebuffer back(8, 16);

  reader::rotate90CW(portrait, landscape);
  reader::rotate90CCW(landscape, back);
  checkOnlyInkAt(back, {kPattern.begin(), kPattern.end()});

  // And the other way round, so neither direction hides a compensating error.
  Framebuffer landscape2(16, 8);
  Framebuffer back2(8, 16);
  reader::rotate90CCW(portrait, landscape2);
  reader::rotate90CW(landscape2, back2);
  checkOnlyInkAt(back2, {kPattern.begin(), kPattern.end()});
}

TEST_CASE("CW and CCW are not the same mapping") {
  // The header used to claim swapping CW for CCW fixes an upside-down image;
  // it does not -- the two differ by 180 degrees, which is not a flip.
  Framebuffer portrait(8, 16);
  inkPattern(portrait);
  Framebuffer cw(16, 8);
  Framebuffer ccw(16, 8);
  reader::rotate90CW(portrait, cw);
  reader::rotate90CCW(portrait, ccw);

  bool differs = false;
  for (int y = 0; y < cw.height() && !differs; ++y)
    for (int x = 0; x < cw.width() && !differs; ++x)
      if (cw.getPixel(x, y) != ccw.getPixel(x, y)) differs = true;
  CHECK(differs);
}

TEST_CASE("a destination that is not the transpose of the source is a no-op") {
  Framebuffer portrait(8, 16);
  inkPattern(portrait);

  // Same shape as the source rather than its transpose: a stale buffer reused
  // after a geometry change. Rotating must leave it untouched, not half-fill it.
  for (auto dims : {std::pair{8, 16}, std::pair{16, 16}, std::pair{8, 8}, std::pair{0, 0}}) {
    Framebuffer wrong(dims.first, dims.second);
    wrong.clear(true);
    reader::rotate90CW(portrait, wrong);
    for (int y = 0; y < wrong.height(); ++y)
      for (int x = 0; x < wrong.width(); ++x) REQUIRE(wrong.getPixel(x, y));
    reader::rotate90CCW(portrait, wrong);
    for (int y = 0; y < wrong.height(); ++y)
      for (int x = 0; x < wrong.width(); ++x) REQUIRE(wrong.getPixel(x, y));
  }
}
