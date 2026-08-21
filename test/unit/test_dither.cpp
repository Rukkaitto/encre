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

// --- the veil ---------------------------------------------------------------
//
// A third pattern for a third job, and the reason it cannot be the first two is
// arithmetic rather than taste. The overlay boards say:
//
//   .dim-veil { background-image: radial-gradient(circle, #fff 1.3px,
//                                                 transparent 1.5px);
//               background-size: 3px 3px; }
//
// A WHITE dot ~2.6px across on a THREE-pixel grid. ditherRect sets BLACK on a
// FOUR-pixel grid.

TEST_CASE("the veil is a white dot on the board's 3px pitch, not the dither's 4px") {
  // A 2.6px circle centred in a 3px cell covers the cell's centre and its four
  // edge-adjacent neighbours (distance 1 < 1.3) and misses its four diagonal
  // ones (distance 1.414 > 1.3). Five cells of nine, arranged as a plus -- which
  // is what a round dot rasterises to at this size.
  reader::Framebuffer fb(36, 36);
  fb.clear(false);  // solid ink, so the veil's own shape is what is left white
  reader::veilRect(fb, 0, 0, 36, 36);
  for (int y = 0; y < 36; ++y)
    for (int x = 0; x < 36; ++x) {
      const bool inDot = (x % 3 == 1) || (y % 3 == 1);
      CAPTURE(x);
      CAPTURE(y);
      CHECK(fb.getPixel(x, y) == inDot);
    }
  // The ink that survives is 4 of every 9: one 2x2 corner block per cell.
  CHECK(inkCount(fb, 0, 0, 36, 36) == 36 * 36 * 4 / 9);
}

TEST_CASE("the veil takes down about half the ink, which a 4px grid could not") {
  // This is the number the plan is about. The board's dot covers
  // pi * 1.3^2 = 5.31 of every 9 px^2, so the veil should leave roughly
  // 1 - 5.31/9 = 41% of the ink; the rasterised plus leaves 4/9 = 44%.
  //
  // Reusing ditherRect's grid instead -- a 2x2 dot on a 4px pitch, its level-1
  // case -- would whiten 4 of every 16, leaving 75% of the ink standing. That is
  // the "veils about half as densely" the plan warns about, and on glass it
  // reads as a screen that is merely smudged rather than one deliberately behind
  // something.
  reader::Framebuffer veiled(48, 48), quarterGrid(48, 48);
  veiled.clear(false);
  quarterGrid.clear(false);
  reader::veilRect(veiled, 0, 0, 48, 48);
  // ditherRect only sets ink, so stand in for "the 4px grid used as a veil" by
  // counting what its level-1 dot would have whitened.
  reader::Framebuffer level1(48, 48);
  reader::ditherRect(level1, 0, 0, 48, 48, 1);
  const int veilWhitened = 48 * 48 - inkCount(veiled, 0, 0, 48, 48);
  const int fourPxWhitened = inkCount(level1, 0, 0, 48, 48);
  CHECK(veilWhitened == 48 * 48 * 5 / 9);
  CHECK(fourPxWhitened == 48 * 48 / 4);
  // Not "a bit denser": the whole point is that it is about twice as dense.
  CHECK(veilWhitened * 100 / fourPxWhitened >= 190);
}

TEST_CASE("the veil's pitch is three pixels, and it is not four") {
  // Pinned directly, because the tempting implementation is to reuse the 4x4
  // matrix that is already there and invert it.
  reader::Framebuffer fb(24, 24);
  fb.clear(false);
  reader::veilRect(fb, 0, 0, 24, 24);
  int differAt4 = 0;
  for (int y = 0; y < 20; ++y)
    for (int x = 0; x < 20; ++x) {
      CAPTURE(x);
      CAPTURE(y);
      CHECK(fb.getPixel(x, y) == fb.getPixel(x + 3, y));
      CHECK(fb.getPixel(x, y) == fb.getPixel(x, y + 3));
      if (fb.getPixel(x, y) != fb.getPixel(x + 4, y)) ++differAt4;
    }
  CHECK(differAt4 > 0);
}

TEST_CASE("the ink the veil leaves is clustered, never isolated pixels") {
  // The same trap ditherRect's comment describes, in reverse. A dispersed
  // pattern of identical coverage would leave the surviving ink as lone pixels,
  // and a lone black pixel on white carries contrast on all four sides and reads
  // heavier than its area -- so a dispersed veil at the board's arithmetic
  // density would look darker than the board's, exactly as the dispersed tint
  // did. The board draws one round dot per cell, so the ink must recede as one
  // shape: what is left here is a 2x2 block per cell.
  reader::Framebuffer fb(36, 36);
  fb.clear(false);
  reader::veilRect(fb, 0, 0, 36, 36);
  int isolated = 0, inBlock = 0;
  // Interior only: a pixel on the rect's edge has neighbours the rect does not
  // contain, which would report as isolation the tiling does not have.
  for (int y = 1; y < 35; ++y)
    for (int x = 1; x < 35; ++x) {
      if (fb.getPixel(x, y)) continue;  // white
      const bool lone = fb.getPixel(x - 1, y) && fb.getPixel(x + 1, y) &&
                        fb.getPixel(x, y - 1) && fb.getPixel(x, y + 1);
      if (lone) ++isolated;
      // Every surviving ink pixel has an orthogonal ink neighbour on each axis:
      // that is what makes it part of a 2x2 rather than a speck.
      const bool pairedX = !fb.getPixel(x - 1, y) || !fb.getPixel(x + 1, y);
      const bool pairedY = !fb.getPixel(x, y - 1) || !fb.getPixel(x, y + 1);
      if (pairedX && pairedY) ++inBlock;
    }
  CHECK(isolated == 0);
  CHECK(inBlock == inkCount(fb, 1, 1, 34, 34));
}

TEST_CASE("the veil's grid is continuous across adjoining rects") {
  // Keyed on absolute framebuffer coordinates, as ditherRect is, so two veiled
  // regions share one grid instead of showing a seam where their phases
  // disagree. Split at 13 and 20, neither a multiple of 3, which is where a
  // pattern phased on each rect's own origin would break.
  reader::Framebuffer split(36, 36), whole(36, 36);
  split.clear(false);
  whole.clear(false);
  reader::veilRect(split, 0, 0, 13, 36);
  reader::veilRect(split, 13, 0, 23, 36);
  reader::veilRect(whole, 0, 0, 36, 36);
  int differ = 0;
  for (int y = 0; y < 36; ++y)
    for (int x = 0; x < 36; ++x)
      if (split.getPixel(x, y) != whole.getPixel(x, y)) ++differ;
  CHECK(differ == 0);

  // Vertically too: an overlay's veil is a full-width band above and below its
  // panel, so the two halves meet on a horizontal seam.
  reader::Framebuffer band(36, 36);
  band.clear(false);
  reader::veilRect(band, 0, 0, 36, 20);
  reader::veilRect(band, 0, 20, 36, 16);
  differ = 0;
  for (int y = 0; y < 36; ++y)
    for (int x = 0; x < 36; ++x)
      if (band.getPixel(x, y) != whole.getPixel(x, y)) ++differ;
  CHECK(differ == 0);
}

TEST_CASE("veiling paper leaves paper, and veiling twice changes nothing further") {
  reader::Framebuffer fb(24, 24);
  reader::veilRect(fb, 0, 0, 24, 24);
  CHECK(inkCount(fb, 0, 0, 24, 24) == 0);
  // Idempotent: it only ever sets white, so a veil drawn over an already veiled
  // region is not progressively lighter. Overlays stack, and the second veil
  // must not bleach the first one's ink away.
  reader::Framebuffer twice(24, 24);
  twice.clear(false);
  reader::veilRect(twice, 0, 0, 24, 24);
  const int once = inkCount(twice, 0, 0, 24, 24);
  reader::veilRect(twice, 0, 0, 24, 24);
  CHECK(inkCount(twice, 0, 0, 24, 24) == once);
}

TEST_CASE("the veil stays inside its rect") {
  reader::Framebuffer fb(36, 36);
  fb.clear(false);
  reader::veilRect(fb, 9, 9, 18, 18);
  CHECK(inkCount(fb, 0, 0, 36, 9) == 36 * 9);
  CHECK(inkCount(fb, 0, 0, 9, 36) == 9 * 36);
  CHECK(inkCount(fb, 27, 0, 9, 36) == 9 * 36);
  CHECK(inkCount(fb, 0, 27, 36, 9) == 36 * 9);
}

TEST_CASE("a degenerate veil rect draws nothing and does not walk off the buffer") {
  reader::Framebuffer fb(16, 16);
  fb.clear(false);
  reader::veilRect(fb, 4, 4, 0, 8);
  reader::veilRect(fb, 4, 4, 8, 0);
  reader::veilRect(fb, 4, 4, -5, -5);
  CHECK(inkCount(fb, 0, 0, 16, 16) == 16 * 16);
  // Partly off-screen, including negative origins: the pattern is keyed on
  // absolute coordinates, so a negative one must still index the tile rather
  // than the wrong tile or nothing at all.
  reader::veilRect(fb, -4, -4, 8, 8);
  CHECK(inkCount(fb, 0, 0, 4, 4) < 4 * 4);
  reader::Framebuffer ref(16, 16);
  ref.clear(false);
  reader::veilRect(ref, 0, 0, 4, 4);
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 4; ++x) CHECK(fb.getPixel(x, y) == ref.getPixel(x, y));
}

// --- the veil's byte-wise fast path -----------------------------------------
//
// veilRect used to be a per-pixel loop over setPixel with the cell index
// computed as ((v % 3) + 3) % 3 on both axes -- four integer divisions per pixel
// -- and it now writes the pattern eight columns at a time straight into the
// physical store. That is PURE OPTIMISATION: every case above, and the two
// overlay goldens, pin the output it must keep producing.
//
// The reference below is that per-pixel loop, kept here for the same reason
// test_rotate.cpp keeps rotate90CCW_reference: it IS the specification, because
// it is the code whose output the goldens were blessed against. Any disagreement
// means the fast path is wrong, never the reference.
//
// The comparison is over data() rather than over pixels, because the fast path
// writes bytes: a mapping that is right per pixel but lays the bytes out
// differently is still a wrong frame from the panel driver's point of view. And
// it runs under Rotation::Ccw as well as Rotation::None, because CCW is how the
// device paints -- a byte-wise path that assumed a logical row is a physical row
// would pass every desktop test and every golden and smear the veil diagonally
// on glass.

namespace {

constexpr bool kVeilDotRef[3][3] = {
    {false, true, false},
    {true, true, true},
    {false, true, false},
};
constexpr int cell3Ref(int v) { return ((v % 3) + 3) % 3; }

void veilRectReference(reader::Framebuffer& fb, int x, int y, int w, int h) {
  for (int yy = y; yy < y + h; ++yy)
    for (int xx = x; xx < x + w; ++xx)
      if (kVeilDotRef[cell3Ref(yy)][cell3Ref(xx)]) fb.setPixel(xx, yy, true);
}

// A deterministic ground with no byte-level symmetry, so a fast path that is off
// by a bit, a byte or a row cannot coincidentally match. A plain white or plain
// black ground would hide any mask that is too WIDE, since the veil only sets
// white: over paper an over-wide mask changes nothing at all.
void fillPseudoRandom(reader::Framebuffer& fb, uint32_t seed) {
  uint32_t s = seed | 1u;
  for (int y = 0; y < fb.height(); ++y)
    for (int x = 0; x < fb.width(); ++x) {
      s ^= s << 13; s ^= s >> 17; s ^= s << 5;  // xorshift32
      fb.setPixel(x, y, (s & 0x10u) != 0);
    }
}

struct VeilCase {
  int fw, fh, x, y, w, h;
  const char* what;
};

void checkVeilMatchesReference(const VeilCase& c, reader::Rotation rot) {
  reader::Framebuffer fast(c.fw, c.fh, rot), ref(c.fw, c.fh, rot);
  const uint32_t seed = static_cast<uint32_t>(c.fw * 7919 + c.fh * 104729 + c.x * 31 + c.w);
  fillPseudoRandom(fast, seed);
  fillPseudoRandom(ref, seed);
  reader::veilRect(fast, c.x, c.y, c.w, c.h);
  veilRectReference(ref, c.x, c.y, c.w, c.h);

  REQUIRE(fast.sizeBytes() == ref.sizeBytes());
  int diffs = 0, first = -1;
  for (int i = 0; i < ref.sizeBytes(); ++i)
    if (fast.data()[i] != ref.data()[i]) {
      if (diffs == 0) first = i;
      ++diffs;
    }
  const char* rn = rot == reader::Rotation::Ccw ? "Ccw" : "None";
  CHECK_MESSAGE(diffs == 0, c.what << " rot=" << rn << ": " << diffs << " of " << ref.sizeBytes()
                                   << " bytes differ, first at offset " << first);
}

}  // namespace

TEST_CASE("the byte-wise veil is byte-identical to the per-pixel one") {
  const VeilCase cases[] = {
      // The two panels, full frame: what an overlay actually asks for.
      {528, 792, 0, 0, 528, 792, "X3 full frame"},
      {480, 800, 0, 0, 480, 800, "X4 full frame"},
      // Origins and widths that are not multiples of 8, which is where a
      // byte-wise path's edge masks are the whole of the correctness. Both panel
      // widths are multiples of 8, so nothing on the device exercises this.
      {528, 792, 1, 0, 526, 792, "X3 inset by one pixel"},
      {528, 792, 7, 3, 513, 785, "X3 ragged origin and width"},
      {528, 792, 3, 0, 5, 792, "a five-pixel column inside one byte"},
      {528, 792, 6, 0, 4, 10, "a run straddling one byte boundary"},
      {64, 64, 0, 0, 64, 64, "aligned 64x64"},
      {64, 64, 5, 5, 54, 54, "inset 64x64"},
      // Every phase of the 3px grid, so none of the three byte masks is missed.
      {48, 48, 0, 0, 48, 48, "phase 0"},
      {48, 48, 1, 1, 46, 46, "phase 1"},
      {48, 48, 2, 2, 44, 44, "phase 2"},
      // Negative origins: the pattern is keyed on absolute coordinates, so
      // clipping must not re-phase it.
      {36, 36, -4, -4, 8, 8, "negative origin"},
      {36, 36, -7, -5, 20, 20, "negative origin, odd offsets"},
      // Off the far edge, so the clip has to shorten the run rather than write
      // past the row.
      {36, 36, 30, 30, 20, 20, "overhanging the far edge"},
      // Degenerate: nothing drawn, nothing walked off.
      {36, 36, 4, 4, 0, 8, "zero width"},
      {36, 36, 4, 4, 8, 0, "zero height"},
      {36, 36, 4, 4, -5, -5, "negative extent"},
      {36, 36, 100, 100, 8, 8, "wholly off-screen"},
      // Frames whose own dimensions are not multiples of 8, in both axes, which
      // under rotation is where the stride comes from the other one.
      {13, 21, 0, 0, 13, 21, "ragged frame 13x21"},
      {21, 13, 0, 0, 21, 13, "ragged frame 21x13"},
      {1, 1, 0, 0, 1, 1, "single pixel"},
      {1, 800, 0, 0, 1, 800, "single column"},
      {800, 1, 0, 0, 800, 1, "single row"},
  };
  for (const VeilCase& c : cases) {
    checkVeilMatchesReference(c, reader::Rotation::None);
    checkVeilMatchesReference(c, reader::Rotation::Ccw);
  }
}

TEST_CASE("the byte-wise veil writes nothing outside the rect it was given") {
  // The fast path indexes raw bytes, so an off-by-one in an edge mask corrupts a
  // neighbour rather than failing a pattern check. Veil a rect inset by one pixel
  // on every side of an all-ink frame and require the border ring is untouched --
  // one pixel is inside the first and last byte of every row, so the masks are
  // doing the work rather than the byte arithmetic.
  for (const reader::Rotation rot : {reader::Rotation::None, reader::Rotation::Ccw}) {
    reader::Framebuffer fb(64, 48, rot);
    fb.clear(false);
    reader::veilRect(fb, 1, 1, 62, 46);
    for (int x = 0; x < 64; ++x) {
      CHECK_FALSE(fb.getPixel(x, 0));
      CHECK_FALSE(fb.getPixel(x, 47));
    }
    for (int y = 0; y < 48; ++y) {
      CHECK_FALSE(fb.getPixel(0, y));
      CHECK_FALSE(fb.getPixel(63, y));
    }
    // ...and it did draw something, so the check above is not passing on a no-op.
    CHECK(inkCount(fb, 1, 1, 62, 46) < 62 * 46);
  }
}

TEST_CASE("a white-inked tint is the same cells as a black one, in paper") {
  // The boards declare the cover placeholder twice, `.dither-dots` and
  // `.dither-dots-inv`: the same 1.1px circle on the same 4px grid, colours
  // swapped, because a focused Library row's ground is already black. So the
  // inverted form must ink exactly the cells the upright one does -- if it
  // chose different cells the two covers would be out of phase with each other
  // and a focus move would visibly re-stipple the thumbnail.
  for (int level = 1; level <= 4; ++level) {
    reader::Framebuffer black(48, 48), white(48, 48);
    black.clear(true);   // paper ground, black dots
    white.clear(false);  // ink ground, paper dots
    reader::ditherRect(black, 0, 0, 48, 48, level, reader::Ink::Black);
    reader::ditherRect(white, 0, 0, 48, 48, level, reader::Ink::White);
    int cells = 0;
    for (int y = 0; y < 48; ++y)
      for (int x = 0; x < 48; ++x) {
        // Exactly complementary: a pixel is a dot in one iff it is a dot in the
        // other, so black.getPixel == white.getPixel is false everywhere.
        CHECK(black.getPixel(x, y) != white.getPixel(x, y));
        if (!black.getPixel(x, y)) ++cells;
      }
    // ...and the density is still level/4 of the field.
    CHECK(cells == 48 * 48 * level / 4);
  }
}

TEST_CASE("a white tint leaves an ink ground alone outside its rect") {
  reader::Framebuffer fb(32, 32);
  fb.clear(false);
  reader::ditherRect(fb, 8, 8, 16, 16, 2, reader::Ink::White);
  for (int y = 0; y < 32; ++y)
    for (int x = 0; x < 32; ++x)
      if (x < 8 || x >= 24 || y < 8 || y >= 24) CHECK_FALSE(fb.getPixel(x, y));
}
