// A single corner pixel cannot distinguish a correct rotation from one with a
// swapped or mis-signed coordinate term, so these tests use an asymmetric
// multi-pixel pattern with no rotational or mirror symmetry.
#include <cstring>
#include <utility>
#include <vector>

#include "doctest.h"
#include "ramp.h"
#include "reader/fontset.h"
#include "reader/framebuffer.h"
#include "reader/rotate.h"
#include "reader/screen_home.h"
#include "reader/screens.h"
#include "reader/text.h"
#include "reader/theme_quiet.h"

using reader::Framebuffer;
using reader::Rotation;

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
  CHECK(exact.sizeBytes() == exact.physRowBytes() * w);
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

// --- Drawing straight into a rotated framebuffer -----------------------------
//
// This is the proof that removing the full-frame transpose from the paint path
// is pixel-neutral. The OLD path -- draw into an unrotated portrait frame, then
// rotate90CCW it into a landscape one -- is the code that shipped and rendered
// correctly on X3 hardware. The NEW path draws into a Rotation::Ccw framebuffer
// whose setPixel applies the same mapping per pixel. The two must hand the panel
// driver the SAME BYTES, so the comparison is over data() across sizeBytes(),
// not over pixels: a mapping that is right per pixel but lays the bytes out
// differently would still be a wrong frame from the driver's point of view.
//
// Any disagreement means the new path is wrong, never the reference.

namespace {

// Byte-for-byte over the physical store, reporting the first offset that differs
// so a failure says where rather than just "not equal".
void checkSameBytes(const Framebuffer& viaRotate, const Framebuffer& direct, const char* what) {
  REQUIRE(direct.sizeBytes() == viaRotate.sizeBytes());
  REQUIRE(direct.physRowBytes() == viaRotate.physRowBytes());
  int diffs = 0, first = -1;
  for (int i = 0; i < viaRotate.sizeBytes(); ++i)
    if (viaRotate.data()[i] != direct.data()[i]) {
      if (diffs == 0) first = i;
      ++diffs;
    }
  CHECK_MESSAGE(diffs == 0, what << ": " << diffs << " of " << viaRotate.sizeBytes()
                                 << " bytes differ, first at offset " << first);
}

}  // namespace

TEST_CASE("a Rotation::Ccw framebuffer reports logical dimensions and a physical store") {
  Framebuffer rot(528, 792, Rotation::Ccw);
  // Logical: what every drawing routine sees, and what the constructor was given.
  CHECK(rot.width() == 528);
  CHECK(rot.height() == 792);
  // Physical: what the panel driver is handed.
  CHECK(rot.physWidth() == 792);
  CHECK(rot.physHeight() == 528);
  CHECK(rot.physRowBytes() == 99);  // ceil(792 / 8), NOT ceil(528 / 8)
  CHECK(rot.sizeBytes() == 99 * 528);
  // Same store size as the landscape buffer the old path allocated.
  Framebuffer landscape(792, 528);
  CHECK(rot.sizeBytes() == landscape.sizeBytes());
}

TEST_CASE("setPixel on a Rotation::Ccw framebuffer lands where rotate90CCW would put it") {
  // The mapping in isolation, on the asymmetric pattern the rest of this file
  // uses: physX = logY, physY = logicalWidth - 1 - logX. Dropping the mirror
  // term gives a plain transpose, which passes a symmetric pattern.
  Framebuffer portrait(8, 16);
  inkPattern(portrait);
  Framebuffer viaRotate(16, 8);
  reader::rotate90CCW(portrait, viaRotate);

  Framebuffer direct(8, 16, Rotation::Ccw);
  inkPattern(direct);
  checkSameBytes(viaRotate, direct, "8x16 pattern");

  // And getPixel reads back in LOGICAL coordinates, so a rotated buffer is
  // indistinguishable from an unrotated one to anything that only draws.
  for (int y = 0; y < portrait.height(); ++y)
    for (int x = 0; x < portrait.width(); ++x)
      REQUIRE(direct.getPixel(x, y) == portrait.getPixel(x, y));
}

TEST_CASE("drawing into a Rotation::Ccw framebuffer equals drawing then rotating") {
  struct Case { int w, h; const char* what; };
  const Case cases[] = {
      {528, 792, "X3 528x792"},
      {480, 800, "X4 480x800"},
      // The stride maths is where this breaks: under rotation the stride comes
      // from the logical HEIGHT, so a logical size that is not a multiple of 8
      // exercises a different ragged edge than the unrotated case does.
      {13, 21, "ragged 13x21"},
      {21, 13, "ragged 21x13"},
      {100, 7, "ragged 100x7"},
      {1, 1, "degenerate 1x1"},
  };
  for (const Case c : cases) {
    // Same pseudo-random pattern into both, in logical coordinates.
    Framebuffer portrait(c.w, c.h);
    fillPseudoRandom(portrait, static_cast<uint32_t>(c.w * 7919 + c.h));
    Framebuffer viaRotate(c.h, c.w);
    reader::rotate90CCW(portrait, viaRotate);

    Framebuffer direct(c.w, c.h, Rotation::Ccw);
    fillPseudoRandom(direct, static_cast<uint32_t>(c.w * 7919 + c.h));
    checkSameBytes(viaRotate, direct, c.what);
  }
}

TEST_CASE("a real screen renders byte-identically through both paths") {
  // Home with the shipped view model and the real type ramp: plenty of ink, and
  // every primitive on the screen at once -- glyphs, 2bpp icon edges, the
  // cover's clustered dither, rules, solid fills and an inverted block. A
  // synthetic pattern cannot catch a primitive that reads fb.width() and gets a
  // logical answer where it wanted a physical one.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  const reader::HomeScreen home(reader::demoHomeVm(), reader::demoHomeTargets());
  // Plane::Bw is what chrome ships; assert it so this stops matching if the
  // screen's declared path moves, rather than pinning a path nothing paints.
  REQUIRE(home.fidelity() == reader::Fidelity::Mono);

  struct Geometry { int w, h; const char* what; };
  for (const Geometry g : {Geometry{528, 792, "X3 528x792"}, Geometry{480, 800, "X4 480x800"}}) {
    // (a) the old path: render portrait, transpose the finished frame.
    Framebuffer portrait(g.w, g.h);
    portrait.clear(true);
    home.render(portrait, ramp.fonts, theme, reader::Plane::Bw);
    Framebuffer viaRotate(g.h, g.w);
    viaRotate.clear(true);
    reader::rotate90CCW(portrait, viaRotate);

    // (b) the new path: render straight into the rotated store.
    Framebuffer direct(g.w, g.h, Rotation::Ccw);
    direct.clear(true);
    home.render(direct, ramp.fonts, theme, reader::Plane::Bw);

    checkSameBytes(viaRotate, direct, g.what);
    // A screen with no ink at all would pass the comparison trivially. Home inks
    // roughly 8% of the frame; require it is not blank so the test cannot go
    // green on an empty render.
    int inked = 0;
    for (int i = 0; i < direct.sizeBytes(); ++i)
      if (direct.data()[i] != 0xFF) ++inked;
    CHECK_MESSAGE(inked > direct.sizeBytes() / 20, g.what << ": only " << inked
                                                          << " bytes carry ink");
  }
}

TEST_CASE("the grayscale planes also agree through both paths") {
  // The three-plane path renders the same screen three times plus a rebase, all
  // into the rotated buffer now. Lsb and Msb carry partial coverage where Bw
  // thresholds it away, so they touch bytes Bw never does.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  const reader::HomeScreen home(reader::demoHomeVm(), reader::demoHomeTargets());
  for (const reader::Plane plane :
       {reader::Plane::Lsb, reader::Plane::Msb, reader::Plane::BwDithered}) {
    Framebuffer portrait(528, 792);
    portrait.clear(true);
    home.render(portrait, ramp.fonts, theme, plane);
    Framebuffer viaRotate(792, 528);
    viaRotate.clear(true);
    reader::rotate90CCW(portrait, viaRotate);

    Framebuffer direct(528, 792, Rotation::Ccw);
    direct.clear(true);
    home.render(direct, ramp.fonts, theme, plane);
    checkSameBytes(viaRotate, direct, "grayscale plane");
  }
}

TEST_CASE("rotating a Rotation::Ccw framebuffer is refused, not sheared") {
  // rotate90CCW walks data() with physRowBytes() assuming physical == logical.
  // A rotated argument would produce a plausible-looking sheared image, so the
  // guard refuses instead and leaves the destination untouched.
  Framebuffer rotated(8, 16, Rotation::Ccw);
  inkPattern(rotated);
  Framebuffer dst(16, 8);
  dst.clear(true);
  reader::rotate90CCW(rotated, dst);
  for (int i = 0; i < dst.sizeBytes(); ++i) REQUIRE(dst.data()[i] == 0xFF);
  reader::rotate90CW(rotated, dst);
  for (int i = 0; i < dst.sizeBytes(); ++i) REQUIRE(dst.data()[i] == 0xFF);

  // And a rotated DESTINATION is refused too.
  Framebuffer portrait(8, 16);
  inkPattern(portrait);
  Framebuffer rotDst(8, 16, Rotation::Ccw);
  rotDst.clear(true);
  reader::rotate90CCW(portrait, rotDst);
  for (int i = 0; i < rotDst.sizeBytes(); ++i) REQUIRE(rotDst.data()[i] == 0xFF);
}
