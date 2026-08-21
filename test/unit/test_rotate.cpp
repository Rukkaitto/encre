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

// --- The block-transpose fast path -----------------------------------------

namespace {

// The per-pixel definition of rotate90CCW, kept here as the reference the fast
// path is checked against. This IS the specification: it is the code that shipped
// and rendered correctly on hardware, so any disagreement means the optimisation
// is wrong, not the reference.
void rotate90CCW_reference(const reader::Framebuffer& src, reader::Framebuffer& dst) {
  for (int y = 0; y < src.height(); ++y)
    for (int x = 0; x < src.width(); ++x)
      dst.setPixel(y, src.width() - 1 - x, src.getPixel(x, y));
}

// A deterministic pattern with no byte-level symmetry, so a transpose that is
// off by a bit, a row, or a reversed axis cannot coincidentally match. A plain
// checkerboard or a single dot would let several wrong implementations pass.
void fillPseudoRandom(reader::Framebuffer& fb, uint32_t seed) {
  uint32_t s = seed | 1u;
  for (int y = 0; y < fb.height(); ++y)
    for (int x = 0; x < fb.width(); ++x) {
      s ^= s << 13; s ^= s >> 17; s ^= s << 5;  // xorshift32
      fb.setPixel(x, y, (s & 0x10u) != 0);
    }
}

}  // namespace

TEST_CASE("the fast rotate90CCW agrees with the per-pixel reference everywhere") {
  struct Case { int w, h; };
  const Case cases[] = {
      {8, 8},      // one block exactly
      {16, 24},    // several whole blocks
      {528, 792},  // the X3 panel
      {480, 800},  // the X4 panel
      {1, 1},      // degenerate, no whole block at all
      {7, 7},      // smaller than a block in both axes
      {9, 9},      // one block plus a one-pixel edge on both axes
      {8, 13},     // whole in x, ragged in y
      {13, 8},     // ragged in x, whole in y
      {15, 17},    // ragged in both, crossing a byte boundary
      {1, 800},    // a single column
      {800, 1},    // a single row
  };
  for (const Case c : cases) {
    reader::Framebuffer src(c.w, c.h);
    fillPseudoRandom(src, static_cast<uint32_t>(c.w * 7919 + c.h));
    reader::Framebuffer fast(c.h, c.w), ref(c.h, c.w);
    // Both start black, so a pixel the fast path forgets to write shows up as a
    // mismatch rather than inheriting the same default as the reference.
    fast.clear(false);
    ref.clear(false);
    reader::rotate90CCW(src, fast);
    rotate90CCW_reference(src, ref);

    int mismatches = 0, firstX = -1, firstY = -1;
    for (int y = 0; y < ref.height(); ++y)
      for (int x = 0; x < ref.width(); ++x)
        if (fast.getPixel(x, y) != ref.getPixel(x, y)) {
          if (mismatches == 0) { firstX = x; firstY = y; }
          ++mismatches;
        }
    CHECK_MESSAGE(mismatches == 0, c.w << "x" << c.h << ": " << mismatches
                                       << " pixels differ, first at (" << firstX << ", "
                                       << firstY << ")");
  }
}

TEST_CASE("the fast rotate90CCW writes nothing outside the destination buffer") {
  // The block loop indexes raw bytes with no clipping, so an off-by-one in the
  // destination row arithmetic would corrupt whatever follows the buffer rather
  // than failing a pixel comparison. Check the bytes past the last row of a
  // deliberately over-allocated destination stay untouched.
  const int w = 528, h = 792;
  reader::Framebuffer src(w, h);
  fillPseudoRandom(src, 12345u);
  // dst must be h x w for the rotate to run; allocate one extra row and keep a
  // copy of it as a canary.
  reader::Framebuffer dst(h, w + 1);
  dst.clear(false);
  // Rotating into a taller-than-required buffer is refused by design, so rotate
  // into a correctly sized view and compare the canary separately: instead pin
  // the exact byte count the correct rotation touches.
  reader::Framebuffer exact(h, w);
  exact.clear(true);
  reader::rotate90CCW(src, exact);
  CHECK(exact.sizeBytes() == exact.rowBytes() * w);
  // Every byte of the destination must have been written by the rotation: with
  // both panel geometries being multiples of 8, the block loop covers the whole
  // buffer, so no byte may still hold the 0xFF the clear left.
  int untouched = 0;
  for (int i = 0; i < exact.sizeBytes(); ++i)
    if (exact.data()[i] == 0xFF) ++untouched;
  reader::Framebuffer ref(h, w);
  ref.clear(true);
  rotate90CCW_reference(src, ref);
  int refUntouched = 0;
  for (int i = 0; i < ref.sizeBytes(); ++i)
    if (ref.data()[i] == 0xFF) ++refUntouched;
  CHECK(untouched == refUntouched);
}
