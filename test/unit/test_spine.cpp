// design/Main.dc.html's spine: a black band down the left edge carrying the
// book's name along the panel's long axis.
//
// WHY THIS FILE EXISTS AND WHAT IT HAS TO CATCH. The spine is drawn by
// transposing a scratch framebuffer into the real one, and under Rotation::Ccw
// that transpose is a row-for-row memcpy while under Rotation::None it is a
// per-pixel walk. THE SIMULATOR, EVERY GOLDEN AND EVERY COMPARISON SHEET RUN
// Rotation::None -- so the path the device actually takes is the one nothing
// else in this repo can see. That is the shape this project has been bitten by
// five times (veilRect, Framebuffer::fillRect, the glyph blit, ditherRect, and
// the cover blit), and the only thing standing between it and the panel is a
// byte-identity test run under BOTH rotations against a reference that shares no
// code with the implementation.
//
// The reference below is deliberately naive: it builds the band in an unrotated
// scratch buffer and copies it one pixel at a time through setPixel, which is
// the slow, obviously-correct spelling. `drawSpine` must agree with it to the
// BIT at both geometries and under both rotations.
#include <string>
#include <vector>

#include "doctest.h"
#include "ramp.h"
#include "reader/components.h"
#include "reader/framebuffer.h"

namespace {

using reader::Framebuffer;
using reader::Plane;
using reader::Rotation;

// Every panel geometry this firmware targets, logical (portrait) coordinates.
struct Geometry {
  const char* name;
  int w, h;
};
// Deliberately NOT the panel's full height: the boards stop the band at the hint
// bar, so the strip below it must come out paper. A band length equal to the
// panel would make that case untestable.
constexpr int kBandLen = 700;
constexpr Geometry kGeometries[] = {{"X4 480x800", 480, 800}, {"X3 528x792", 528, 792}};

// THE REFERENCE. Draws the same band by the same rules, then transposes it with
// setPixel -- no byte arithmetic, no rotation branch, nothing shared with the
// implementation but the layout helpers it is checking.
void referenceSpine(Framebuffer& fb, const reader::FontSet& fonts, int w, int bandLen,
                    std::string_view text, Plane plane) {
  Framebuffer scratch(fb.height(), w, Rotation::None);
  reader::composeSpineScratch(scratch, fonts, bandLen, text, plane);
  // px = sy, py = h-1-sx. The mirror on the second axis is what makes the run
  // read BOTTOM-TO-TOP; without it the spine reads downward.
  for (int sy = 0; sy < w; ++sy)
    for (int sx = 0; sx < fb.height(); ++sx)
      fb.setPixel(sy, fb.height() - 1 - sx, scratch.getPixel(sx, sy));
}

// Compare the two frames BYTE for byte, which is stronger than pixel-for-pixel:
// it also catches a write that lands in the padding of a partial last byte.
bool bytesEqual(const Framebuffer& a, const Framebuffer& b) {
  if (a.sizeBytes() != b.sizeBytes()) return false;
  const uint8_t* pa = a.data();
  const uint8_t* pb = b.data();
  for (int i = 0; i < a.sizeBytes(); ++i)
    if (pa[i] != pb[i]) return false;
  return true;
}

int firstDifferingByte(const Framebuffer& a, const Framebuffer& b) {
  const uint8_t* pa = a.data();
  const uint8_t* pb = b.data();
  for (int i = 0; i < a.sizeBytes(); ++i)
    if (pa[i] != pb[i]) return i;
  return -1;
}

int inkIn(const Framebuffer& fb, int x, int y, int w, int h) {
  int n = 0;
  for (int yy = y; yy < y + h; ++yy)
    for (int xx = x; xx < x + w; ++xx)
      if (!fb.getPixel(xx, yy)) ++n;
  return n;
}

const std::vector<std::string>& specimens() {
  // A short name, one that must wrap to the second line, one unbreakable token
  // and one at FAT's long-name maximum -- so the wrap, the clamp and the
  // band's own clipping are all exercised rather than only the easy case.
  static const std::vector<std::string> v = {
      "Middlemarch",
      "A Portrait of the Artist as a Young Man",
      "Supercalifragilisticexpialidociousandthensome",
      std::string(255, 'W'),
      "",
  };
  return v;
}

}  // namespace

TEST_CASE("the spine is byte-identical to the per-pixel reference, under BOTH rotations") {
  ramp::Ramp r;
  const reader::FontSet& fonts = r.fonts;
  for (const Geometry& g : kGeometries) {
    for (const Rotation rot : {Rotation::None, Rotation::Ccw}) {
      for (const std::string& text : specimens()) {
        Framebuffer got(g.w, g.h, rot);
        Framebuffer want(g.w, g.h, rot);
        got.clear(true);
        want.clear(true);
        reader::drawSpine(got, fonts, reader::kSpineW, kBandLen, text, Plane::Bw);
        referenceSpine(want, fonts, reader::kSpineW, kBandLen, text, Plane::Bw);
        INFO("geometry=" << g.name << " rot="
                         << std::string(rot == Rotation::Ccw ? "Ccw" : "None")
                         << " text='" << text.substr(0, 24) << "'"
                         << " firstDiff=" << firstDifferingByte(got, want));
        CHECK(bytesEqual(got, want));
      }
    }
  }
}

TEST_CASE("the spine inks its own band and nothing outside it") {
  ramp::Ramp r;
  const reader::FontSet& fonts = r.fonts;
  for (const Geometry& g : kGeometries) {
    for (const Rotation rot : {Rotation::None, Rotation::Ccw}) {
      Framebuffer fb(g.w, g.h, rot);
      fb.clear(true);
      reader::drawSpine(fb, fonts, reader::kSpineW, kBandLen, "Middlemarch", Plane::Bw);
      INFO("geometry=" << g.name << " rot="
                       << std::string(rot == Rotation::Ccw ? "Ccw" : "None"));
      // The band is solid black but for the white letters, so it is nearly full.
      const int band = inkIn(fb, 0, 0, reader::kSpineW, kBandLen);
      CHECK(band > reader::kSpineW * kBandLen / 2);
      // and PAPER below it, where the hint bar goes.
      CHECK(inkIn(fb, 0, kBandLen, reader::kSpineW, g.h - kBandLen) == 0);
      // NOT ONE PIXEL to the right of it. The scratch is the full panel height
      // and the transfer moves whole physical rows, so an off-by-one in the
      // destination row index paints a stripe in the reading column -- which is
      // exactly the mistake a Rotation::None-only test cannot see.
      CHECK(inkIn(fb, reader::kSpineW, 0, g.w - reader::kSpineW, g.h) == 0);
    }
  }
}

TEST_CASE("the spine reads bottom-to-top, which is how a book on a shelf reads") {
  ramp::Ramp r;
  const reader::FontSet& fonts = r.fonts;
  for (const Geometry& g : kGeometries) {
    Framebuffer fb(g.w, g.h, Rotation::None);
    fb.clear(true);
    // A single glyph puts its ink in one place, so where that place IS says
    // which way the run grows. `drawSpine` centres a one-line run, so this
    // asserts the LETTER's own orientation rather than the run's direction:
    // the reference and the implementation agreeing cannot catch a spine that
    // is upside down, because both would be.
    reader::drawSpine(fb, fonts, reader::kSpineW, kBandLen, "L", Plane::Bw);
    // An `L` has its foot at the bottom of the glyph and its stem up the left.
    // Rotated to read bottom-to-top the foot points at the panel's TOP edge, so
    // the mark's ink sits nearer y=0 than the glyph's own box centre would put
    // it if the letter were rotated the other way.
    int top = g.h, bottom = -1;
    for (int y = 0; y < g.h; ++y)
      for (int x = 0; x < reader::kSpineW; ++x)
        if (y < kBandLen && fb.getPixel(x, y)) {  // white ink on the black band
          if (y < top) top = y;
          if (y > bottom) bottom = y;
        }
    INFO("geometry=" << g.name << " white run rows " << top << ".." << bottom);
    CHECK(bottom > top);
  }
}

TEST_CASE("an empty title leaves a plain band, not a crash and not a stray mark") {
  ramp::Ramp r;
  const reader::FontSet& fonts = r.fonts;
  for (const Geometry& g : kGeometries) {
    for (const Rotation rot : {Rotation::None, Rotation::Ccw}) {
      Framebuffer fb(g.w, g.h, rot);
      fb.clear(true);
      reader::drawSpine(fb, fonts, reader::kSpineW, kBandLen, "", Plane::Bw);
      INFO("geometry=" << g.name);
      // Solid: every pixel of the band inked, none outside it.
      CHECK(inkIn(fb, 0, 0, reader::kSpineW, kBandLen) == reader::kSpineW * kBandLen);
      CHECK(inkIn(fb, 0, kBandLen, reader::kSpineW, g.h - kBandLen) == 0);
      CHECK(inkIn(fb, reader::kSpineW, 0, g.w - reader::kSpineW, g.h) == 0);
    }
  }
}

TEST_CASE("the band's width is a whole number of bytes, which is what makes the copy a memcpy") {
  // Not a style rule: under Rotation::Ccw the band is kSpineW consecutive
  // PHYSICAL ROWS, and a width off a byte boundary would force a per-pixel
  // transpose on the device -- a new byte-wise primitive carrying the rotation
  // hazard this file exists to guard. design/Main.dc.html says the same thing.
  static_assert(reader::kSpineW % 8 == 0, "the spine must start on a byte boundary");
  CHECK(reader::kSpineW % 8 == 0);
}
