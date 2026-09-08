// The streaming fitter against a NAIVE WHOLE-IMAGE reference.
//
// This is the shape test_dither.cpp and test_framebuffer.cpp use, and it is the
// only one that can catch the defect that matters here: a streaming downscale
// and diffusion is easy to get subtly wrong -- a dropped error term, a row
// phase off by one -- in a way that still produces a plausible picture. A
// reference that holds the whole image is trivial to write and obviously
// correct, and byte identity against it is a real assertion.
//
// WHAT IT CANNOT CATCH, stated here so nobody reads a green run as more than it
// is: the reference SHARES the packing, so a consistently mirrored bit order --
// or a consistently inverted one -- satisfies every comparison below. Task 13's
// grayscale golden is what finally pins that. The two cases at the bottom of
// this file are the cheap half of it: they read a packed row back through a
// real reader::Framebuffer, which does know which bit is the leftmost pixel and
// which value is paper.
#include <cstring>
#include <vector>

#include "doctest.h"
#include "reader/framebuffer.h"
#include "reader/imagefit.h"

namespace {

// A deterministic pseudo-image with real gradients, so the diffusion has
// something to diffuse. A flat field would make every mutation below invisible.
std::vector<uint8_t> ramp(int w, int h) {
  std::vector<uint8_t> px(static_cast<size_t>(w) * h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      px[static_cast<size_t>(y) * w + x] =
          static_cast<uint8_t>((x * 255 / (w - 1) + y * 255 / (h - 1)) / 2);
  return px;
}

struct Planes {
  std::vector<uint8_t> msb, lsb;
  int rows = 0, bytes = 0;
};

// The reference: whole image in memory, box filter then Floyd-Steinberg, no
// streaming. Deliberately written the obvious way -- it is the entire defence
// of this file, so it must be readable straight through with no cleverness to
// audit.
Planes reference(const std::vector<uint8_t>& src, int sw, int sh, int pw, int ph,
                 reader::CoverFit fit) {
  const reader::FitBox b = reader::fitCover(sw, sh, pw, ph, fit);
  Planes out;
  out.bytes = (pw + 7) / 8;
  if (b.dstW <= 0 || b.dstH <= 0) return out;

  // 1. Resample the crop rectangle to dstW x dstH cell means, rounding to
  //    nearest.
  //
  //    WRITTEN AS AN INVERSE MAP FOR BOTH DIRECTIONS, which is a DIFFERENT
  //    spelling from the implementation's and is the point of a reference. The
  //    fitter walks the source forward and scatters each pixel into the one cell
  //    it lands in; this walks the destination and states, per axis, exactly
  //    which source indices a cell covers:
  //
  //      downscale (dst <= src): the j with floor(j * dst / src) == c, which is
  //                              [ceil(c * src / dst), ceil((c + 1) * src / dst))
  //      upscale   (dst >  src): the single j = floor(c * src / dst), because a
  //                              cell narrower than a source pixel sits inside
  //                              one -- nearest neighbour, which is all a box
  //                              filter can do when enlarging (imagefit.h)
  //
  //    The two forms agreeing on every downscale shape below is itself a check:
  //    a ceil written as a floor would redden the whole sweep.
  auto range = [](int c, int srcN, int dstN, int& lo, int& hi) {
    const long long s = srcN, d = dstN;
    if (dstN > srcN) {
      lo = static_cast<int>(c * s / d);
      hi = lo + 1;
    } else {
      lo = static_cast<int>((c * s + d - 1) / d);
      hi = static_cast<int>(((c + 1) * s + d - 1) / d);
    }
  };
  std::vector<int> grey(static_cast<size_t>(b.dstW) * b.dstH, 255);
  for (int y = 0; y < b.dstH; ++y) {
    int rlo = 0, rhi = 0;
    range(y, b.srcH, b.dstH, rlo, rhi);
    for (int x = 0; x < b.dstW; ++x) {
      int clo = 0, chi = 0;
      range(x, b.srcW, b.dstW, clo, chi);
      long sum = 0, num = 0;
      for (int i = rlo; i < rhi; ++i)
        for (int j = clo; j < chi; ++j) {
          sum += src[static_cast<size_t>(b.srcY + i) * sw + b.srcX + j];
          ++num;
        }
      if (num > 0) grey[static_cast<size_t>(y) * b.dstW + x] = static_cast<int>((sum + num / 2) / num);
    }
  }

  // 2. Floyd-Steinberg over the whole buffer, to the four levels the panel has.
  std::vector<int> level(static_cast<size_t>(b.dstW) * b.dstH, 0);
  for (int y = 0; y < b.dstH; ++y) {
    for (int x = 0; x < b.dstW; ++x) {
      const size_t at = static_cast<size_t>(y) * b.dstW + x;
      const int v = grey[at];
      int idx = (v + 42) / 85;
      if (idx < 0) idx = 0;
      if (idx > 3) idx = 3;
      const int e = v - idx * 85;
      // grey is 0 = black, so a HIGH index is a LIGHT pixel and level 3 is ink.
      level[at] = 3 - idx;
      if (x + 1 < b.dstW) grey[at + 1] += e * 7 / 16;
      if (y + 1 < b.dstH) {
        const size_t below = at + b.dstW;
        if (x > 0) grey[below - 1] += e * 3 / 16;
        grey[below] += e * 5 / 16;
        if (x + 1 < b.dstW) grey[below + 1] += e * 1 / 16;
      }
    }
  }

  // 3. Pack. A SET bit is paper (framebuffer.h: true/1 = white), so a level's
  //    bits are stored complemented, MSB-first with bit 7 the leftmost pixel.
  for (int y = 0; y < b.dstH; ++y) {
    std::vector<uint8_t> m(static_cast<size_t>(out.bytes), 0xFF);
    std::vector<uint8_t> l(static_cast<size_t>(out.bytes), 0xFF);
    for (int x = 0; x < b.dstW; ++x) {
      const int px = b.dstX + x;
      const uint8_t bit = static_cast<uint8_t>(0x80u >> (px & 7));
      const int lv = level[static_cast<size_t>(y) * b.dstW + x];
      if (lv & 2) m[px >> 3] = static_cast<uint8_t>(m[px >> 3] & ~bit);
      if (lv & 1) l[px >> 3] = static_cast<uint8_t>(l[px >> 3] & ~bit);
    }
    out.msb.insert(out.msb.end(), m.begin(), m.end());
    out.lsb.insert(out.lsb.end(), l.begin(), l.end());
    ++out.rows;
  }
  return out;
}

Planes streamed(const std::vector<uint8_t>& src, int sw, int sh, int pw, int ph,
                reader::CoverFit fit) {
  reader::CoverFitter f;
  REQUIRE(f.begin(sw, sh, pw, ph, fit));
  Planes out;
  out.bytes = (pw + 7) / 8;
  for (int y = 0; y < sh; ++y) {
    REQUIRE(f.addRow(src.data() + static_cast<size_t>(y) * sw));
    while (f.nextRow()) {
      out.msb.insert(out.msb.end(), f.msbRow(), f.msbRow() + out.bytes);
      out.lsb.insert(out.lsb.end(), f.lsbRow(), f.lsbRow() + out.bytes);
      CHECK(f.lastEmittedRow() == out.rows);
      ++out.rows;
    }
  }
  CHECK(f.rowsEmitted() == out.rows);
  CHECK(f.rowsEmitted() == f.box().dstH);
  return out;
}


// Read one destination row's levels back out of the two planes, exactly as
// png.cpp's composeGray does it: an INKED plane counts 1, and the level is
// (msb << 1) | lsb.
void readLevels(const reader::CoverFitter& f, std::vector<int>& out) {
  const reader::FitBox& b = f.box();
  out.clear();
  for (int x = 0; x < b.dstW; ++x) {
    const int px = b.dstX + x;
    const uint8_t bit = static_cast<uint8_t>(0x80u >> (px & 7));
    const int m = (f.msbRow()[px >> 3] & bit) ? 0 : 1;
    const int l = (f.lsbRow()[px >> 3] & bit) ? 0 : 1;
    out.push_back((m << 1) | l);
  }
}
}  // namespace

TEST_CASE("CoverFitter streams to exactly what the whole-image reference produces") {
  // Both panels, both fits, and source shapes that are NOT integer multiples of
  // the destination -- an exact multiple would hide every rounding bug in the
  // box filter.
  //
  // The first four shapes are chosen to reach both crop directions: 601x1000 is
  // 0.601, narrower than the X3's 0.667, so Fill crops its HEIGHT there and its
  // WIDTH on the X4; 877x973 and 1400x2100 crop width or nothing.
  //
  // THE LAST FOUR ARE ENLARGEMENTS, and they are what puts the inverse map in
  // this sweep. 400x662 is the smallest cover in the corpus (x1.21 on the X4,
  // x1.32 on the X3); 301x501 is mid-range at x1.60/x1.75; 265x401 is x1.995 on
  // the X4 and x1.992 on the X3, which was the top of the admitted range while
  // kMaxCoverUpscalePercent was 200 and is kept because that range is still
  // inside the current one and it is where the two panels come closest to
  // disagreeing.
  //
  // 214x321 IS THE TOP OF THE ADMITTED RANGE AT 250, and the sweep has to reach
  // it or it stops covering the band the cap was raised to allow -- which is the
  // whole of what this change does. It is as close to the cap as a shape can be
  // and still clear it at BOTH panels and BOTH fits: x2.4922 (X4 Fill), x2.2430
  // (X4 Whole), x2.4673 (X3, both fits). It is exactly 2:3 -- 214 = 2 * 107 and
  // 321 = 3 * 107 -- which is the corpus's median aspect and the X3's own, so on
  // that panel Fill crops nothing and this is the 71%-of-books case at the
  // steepest ratio the firmware will draw it at. None of the four is an integer
  // multiple of either panel, for the reason the downscale shapes are not: an
  // exact multiple hides every rounding bug in the map.
  const int panels[2][2] = {{480, 800}, {528, 792}};
  const int sources[8][2] = {{1400, 2100}, {877, 973}, {601, 1000}, {1600, 2400},
                             {400, 662},   {301, 501}, {265, 401}, {214, 321}};

  for (const auto& p : panels) {
    for (const auto& s : sources) {
      for (const reader::CoverFit fit : {reader::CoverFit::Fill, reader::CoverFit::Whole}) {
        CAPTURE(p[0]); CAPTURE(p[1]); CAPTURE(s[0]); CAPTURE(s[1]);
        CAPTURE(fit == reader::CoverFit::Fill);
        const std::vector<uint8_t> px = ramp(s[0], s[1]);
        const Planes want = reference(px, s[0], s[1], p[0], p[1], fit);
        const Planes got = streamed(px, s[0], s[1], p[0], p[1], fit);
        REQUIRE(got.rows == want.rows);
        REQUIRE(got.rows > 0);
        CHECK(got.msb == want.msb);
        CHECK(got.lsb == want.lsb);
      }
    }
  }
}

TEST_CASE("fitCover crops for Fill and letterboxes for Whole") {
  // THE ARITHMETIC, once, because the plan's own version of this case asserted
  // the transpose of it and the numbers are not a matter of taste.
  //
  // A 1400x2100 cover is 0.6667 wide-over-tall. The X3 is 528/792 = 0.6667 and
  // the X4 is 480/800 = 0.6000. The cover is therefore RELATIVELY WIDER than
  // the X4's panel, so covering that panel (Fill) means scaling until the
  // HEIGHT fits -- 800/2100 -- and throwing away the width that no longer fits:
  // 2100 * 480/800 = 1260 of 1400 columns, a 10.0% loss. Containing it (Whole)
  // means scaling until the WIDTH fits -- 480/1400 -- which leaves 2100 * 0.3429
  // = 720 rows in an 800-row panel: bands ABOVE and BELOW.
  const reader::FitBox x3 = reader::fitCover(1400, 2100, 528, 792, reader::CoverFit::Fill);
  CHECK(x3.dstW == 528);
  CHECK(x3.dstH == 792);
  CHECK(x3.srcW == 1400);
  CHECK(x3.srcH == 2100);
  CHECK(x3.srcX == 0);
  CHECK(x3.srcY == 0);

  const reader::FitBox fill = reader::fitCover(1400, 2100, 480, 800, reader::CoverFit::Fill);
  CHECK(fill.dstW == 480);
  CHECK(fill.dstH == 800);
  CHECK(fill.srcH == 2100);  // the height is what fits; nothing is cropped there
  CHECK(fill.srcW == 1260);  // 2100 * 480 / 800
  CHECK(fill.srcX == 70);    // centred horizontally: (1400 - 1260) / 2

  const reader::FitBox whole = reader::fitCover(1400, 2100, 480, 800, reader::CoverFit::Whole);
  CHECK(whole.srcH == 2100);
  CHECK(whole.srcW == 1400);
  CHECK(whole.dstW == 480);
  CHECK(whole.dstH == 720);  // 2100 * 480 / 1400
  CHECK(whole.dstX == 0);
  CHECK(whole.dstY == 40);  // centred, so there is a band above and below

  // The squarest cover in the corpus (see the census in reader/cover_fit.h),
  // on the X4: Fill cuts its title off at both edges. 973 * 480 / 800 = 583.8
  // -> 584 of 877 columns.
  const reader::FitBox sq = reader::fitCover(877, 973, 480, 800, reader::CoverFit::Fill);
  CHECK(sq.srcW == 584);
  CHECK(sq.srcH == 973);
  CHECK(sq.srcX == 146);
}

TEST_CASE("Fill centres a HEIGHT crop at 0.4, not at 0.5") {
  // The only case the 0.4 exists for, and the one the panels reach least often:
  // a source relatively TALLER than the panel, so Fill crops rows rather than
  // columns. How rare, per panel, is in the census in reader/cover_fit.h.
  //
  // 1000x2000 is 0.500 against the X4's 0.600, so the width binds and the crop
  // keeps 1000 * 800/480 = 1666.67 -> 1667 rows of 2000. 333 rows go, and 0.4
  // of them come off the TOP so the title band at the bottom survives:
  // round(333 * 0.4) = 133, where an even crop would take 166.
  const reader::FitBox tall = reader::fitCover(1000, 2000, 480, 800, reader::CoverFit::Fill);
  CHECK(tall.dstW == 480);
  CHECK(tall.dstH == 800);
  CHECK(tall.srcW == 1000);
  CHECK(tall.srcH == 1667);
  CHECK(tall.srcX == 0);
  CHECK(tall.srcY == 133);
}

TEST_CASE("a cover smaller than the panel is ENLARGED to fill it") {
  // #64: this case read "fitCover never upscales, and Fill degrades to
  // Whole-at-1:1", and it pinned the behaviour a reader reported as a defect --
  // design/SleepCover.dc.html says full-bleed and a small cover sat in the middle
  // of the glass. 400x662 is the smallest cover in the corpus (the census is in
  // reader/cover_fit.h), and on the X4 it asks for x1.21.
  const reader::FitBox small = reader::fitCover(400, 662, 480, 800, reader::CoverFit::Fill);
  CHECK_FALSE(small.tooSmall);
  CHECK(small.dstW == 480);   // the WHOLE panel, which is what Fill means
  CHECK(small.dstH == 800);
  CHECK(small.dstX == 0);     // and therefore no band on either axis
  CHECK(small.dstY == 0);
  // Fill still crops to the panel's aspect first: 662 * 480 / 800 = 397.2 -> 397
  // of 400 columns, centred.
  CHECK(small.srcW == 397);
  CHECK(small.srcH == 662);
  CHECK(small.srcX == 1);

  // Whole enlarges too, and still letterboxes: 400x662 is 0.604 against the X4's
  // 0.600, so the WIDTH binds and there are bands above and below.
  const reader::FitBox smallWhole = reader::fitCover(400, 662, 480, 800, reader::CoverFit::Whole);
  CHECK_FALSE(smallWhole.tooSmall);
  CHECK(smallWhole.srcW == 400);
  CHECK(smallWhole.srcH == 662);
  CHECK(smallWhole.dstW == 480);
  CHECK(smallWhole.dstH == 794);  // 662 * 480 / 400
  CHECK(smallWhole.dstX == 0);
  CHECK(smallWhole.dstY == 3);

  // And the fitter fills the whole box from a source with fewer rows in it,
  // which is the half the old one-source-row-to-one-destination-row streaming
  // could not do at all.
  const std::vector<uint8_t> px = ramp(400, 662);
  reader::CoverFitter f;
  REQUIRE(f.begin(400, 662, 480, 800, reader::CoverFit::Fill));
  for (int y = 0; y < 662; ++y) {
    REQUIRE(f.addRow(px.data() + static_cast<size_t>(y) * 400));
    while (f.nextRow()) { /* drained */ }
  }
  CHECK(f.rowsEmitted() == 800);
  CHECK(f.rowsEmitted() == f.box().dstH);
}

TEST_CASE("kMaxCoverUpscalePercent is a boundary, and exactly 250% is admitted") {
  // THE NUMBER'S DERIVATION IS IN imagefit.h -- as is the fact that 250 is an
  // OWNER OVERRIDE of what that derivation bounds. What is pinned here is not the
  // number but that the comparison is EXACT: a shape asking for exactly the cap
  // is admitted, and one rounding step past it is refused. That is the case a
  // floating-point scale would decide by rounding.
  //
  // 192x320 INTO THE X4 IS EXACTLY x2.50 ON BOTH AXES, which is why it is the
  // shape: 480 = 5 * 96 and 800 = 5 * 160, and 192/320 is 0.600, the X4's own
  // aspect, so Fill crops nothing and neither axis is decided by a rounding step.
  //
  // THE SHAPE IS LITERAL AND ITS STATUS AS THE BOUNDARY IS NOT. These two REQUIREs
  // say "this shape sits exactly ON the cap" in terms of the constant, so moving
  // the constant again fails HERE, immediately, at the place that explains what
  // the shape was for -- rather than leaving the case silently straddling nothing,
  // which is what raising the cap from 200 did to this test's previous shapes.
  REQUIRE(480 * 100 == 192 * reader::kMaxCoverUpscalePercent);
  REQUIRE(800 * 100 == 320 * reader::kMaxCoverUpscalePercent);

  const reader::FitBox atCap = reader::fitCover(192, 320, 480, 800, reader::CoverFit::Fill);
  CHECK_FALSE(atCap.tooSmall);
  CHECK(atCap.dstW == 480);
  CHECK(atCap.dstH == 800);
  // Whole answers the same box for this shape, the aspects being equal.
  CHECK_FALSE(reader::fitCover(192, 320, 480, 800, reader::CoverFit::Whole).tooSmall);

  // One row less of source and it is over the cap: 800 / 319 = x2.508.
  const reader::FitBox over = reader::fitCover(192, 319, 480, 800, reader::CoverFit::Fill);
  CHECK(over.tooSmall);
  // AND THE BOX IS THE 1:1 CENTRED ONE, which is exactly what this function
  // returned for such a cover before it could enlarge at all. The flag is the
  // only observable that moved, so a caller that ignores it gets the old geometry
  // rather than a surprise -- and CoverReport can report where the cover WOULD
  // have gone.
  //
  // 1:1 IS 1:1 WITH THE CROP RECTANGLE, NOT WITH THE FILE, which is easy to read
  // past: Fill crops to the panel's aspect BEFORE the cap is consulted, so
  // 192x319 has already lost a column -- 319 * 480 / 800 = 191.4 -> 191 -- and
  // the fallback box is that crop at 1:1. Asserting the file's 192 here failed,
  // and the failure was the test's.
  CHECK(over.srcW == 191);
  CHECK(over.dstW == 191);
  CHECK(over.dstH == 319);
  CHECK(over.dstX == 144);  // (480 - 191) / 2
  CHECK(over.dstY == 240);  // (800 - 319) / 2

  // THE BOUNDARY IS PER PANEL AND PER FIT, NOT A PROPERTY OF THE PICTURE, and the
  // same shape shows both answers at once on the OTHER panel: the X3 is 2:3 where
  // this cover is 3:5, so Fill crops its height and asks x2.75 -- refused -- while
  // Whole letterboxes and asks only x2.475, which the cap admits.
  CHECK(reader::fitCover(192, 320, 528, 792, reader::CoverFit::Fill).tooSmall);
  CHECK_FALSE(reader::fitCover(192, 320, 528, 792, reader::CoverFit::Whole).tooSmall);

  // THE BOOK THAT PRODUCED #64 IS NOW ADMITTED, AND THAT IS THE WHOLE POINT OF
  // THE RAISE. 260x346 asks x2.29 on the X3 (Fill), x2.03 there (Whole), x2.31 on
  // the X4 (Fill) and x1.85 (Whole) -- all four refused at 200 and all four served
  // at 250, so the sleep screen draws the picture its board draws instead of
  // falling back to the reading card. Whether x2.29 replication READS as a
  // photograph is the open question imagefit.h hands to the glass; this only pins
  // that the arithmetic lets it through.
  CHECK_FALSE(reader::fitCover(260, 346, 528, 792, reader::CoverFit::Fill).tooSmall);
  CHECK_FALSE(reader::fitCover(260, 346, 528, 792, reader::CoverFit::Whole).tooSmall);
  CHECK_FALSE(reader::fitCover(260, 346, 480, 800, reader::CoverFit::Fill).tooSmall);
  CHECK_FALSE(reader::fitCover(260, 346, 480, 800, reader::CoverFit::Whole).tooSmall);

  // NOTHING MAY DRAW A REFUSED COVER, which is what makes the flag worth having
  // rather than being advice. begin() is the one gate every caller goes through,
  // and it agrees with the flag on both sides of the boundary.
  reader::CoverFitter f;
  CHECK_FALSE(f.begin(192, 319, 480, 800, reader::CoverFit::Fill));
  CHECK(f.rowsEmitted() == 0);
  // And a source ONE row bigger on the binding axis is served: 800 / 320 == 2.5.
  CHECK(f.begin(192, 320, 480, 800, reader::CoverFit::Fill));
}

TEST_CASE("the two axes are asked separately, and a mixed box is served") {
  // fitCover keeps the two scales equal up to one rounding step, so an
  // enlargement is normally both axes at once -- but the rounding can land one
  // axis at exactly 1:1 while the other is over it, and CoverFitter asks each
  // axis its own question so that case needs no special handling.
  //
  // 100x40 into 101x50 with Whole: the source is relatively wider, so the width
  // binds -- dstW is 101 (an enlargement of one pixel) and dstH is
  // round(40 * 101 / 100) = 40, which is 1:1. A single "is this an enlargement"
  // flag would take the whole row path down the wrong branch.
  const reader::FitBox mixed = reader::fitCover(100, 40, 101, 50, reader::CoverFit::Whole);
  REQUIRE(mixed.dstW == 101);
  REQUIRE(mixed.dstH == 40);
  REQUIRE(mixed.srcW == 100);
  REQUIRE(mixed.srcH == 40);
  CHECK_FALSE(mixed.tooSmall);

  const std::vector<uint8_t> px = ramp(100, 40);
  const Planes want = reference(px, 100, 40, 101, 50, reader::CoverFit::Whole);
  const Planes got = streamed(px, 100, 40, 101, 50, reader::CoverFit::Whole);
  REQUIRE(got.rows == 40);
  CHECK(got.msb == want.msb);
  CHECK(got.lsb == want.lsb);
}

TEST_CASE("a replicated row is not a duplicated row") {
  // WHAT THE ENLARGEMENT ACTUALLY LOOKS LIKE, and the property that makes
  // nearest-neighbour replication acceptable at the cap. The accumulator is held
  // across the several destination rows one source row completes, but err_
  // advances per emitted row -- so the copies are the same TONE in DIFFERENT
  // dither patterns, and the vertical replication is broken up by the diffusion
  // instead of showing as pairs of identical rows.
  //
  // A flat mid-tone is the case that shows it: grey 128 is a level midpoint, the
  // tone four levels carry worst, so every row has to be a pattern.
  const int sw = 33, sh = 33;
  const std::vector<uint8_t> flat(static_cast<size_t>(sw) * sh, 128);
  reader::CoverFitter f;
  REQUIRE(f.begin(sw, sh, 64, 64, reader::CoverFit::Fill));
  REQUIRE(f.box().dstH == 64);

  std::vector<std::vector<int>> rows;
  std::vector<int> levels;
  for (int y = 0; y < sh; ++y) {
    REQUIRE(f.addRow(flat.data() + static_cast<size_t>(y) * sw));
    while (f.nextRow()) {
      readLevels(f, levels);
      rows.push_back(levels);
    }
  }
  REQUIRE(rows.size() == 64);
  // Some source rows completed TWO destination rows -- 64 out of 33 is where the
  // replication happens at all.
  int identicalNeighbours = 0;
  for (size_t i = 1; i < rows.size(); ++i)
    if (rows[i] == rows[i - 1]) ++identicalNeighbours;
  // Not a single pair of adjacent destination rows is identical. Zero rather than
  // "few": if the accumulator were emitted twice with err_ frozen, EVERY
  // replicated pair would match, and the diffusion is what makes none of them do.
  CHECK(identicalNeighbours == 0);
  // And the tone is right, which is the other half: a held accumulator that was
  // cleared too early would emit paper for the second copy.
  long long ink = 0;
  for (const auto& r : rows)
    for (int lv : r) ink += lv;
  const double mean = 255.0 - 85.0 * (static_cast<double>(ink) / (64.0 * 64.0));
  CHECK(mean > 118.0);
  CHECK(mean < 138.0);
}

TEST_CASE("an undrained push is refused rather than blended") {
  // THE ONE MISUSE THE DRAIN INTERFACE INTRODUCES. A caller that kept the old
  // `if (emitted)` shape would, on an enlargement, drop every second destination
  // row and add the next source row on top of an accumulator still holding the
  // last -- a picture with rows blended into each other, which is worse than a
  // missing one because it still looks like a picture.
  const int sw = 33, sh = 33;
  const std::vector<uint8_t> px = ramp(sw, sh);
  reader::CoverFitter f;
  REQUIRE(f.begin(sw, sh, 64, 64, reader::CoverFit::Fill));
  REQUIRE(f.addRow(px.data()));
  REQUIRE(f.nextRow());        // one taken
  CHECK(f.rowsEmitted() == 1);
  // 64 destination rows over 33 source rows means this row owes a second one, so
  // pushing again now is the misuse.
  CHECK_FALSE(f.addRow(px.data() + sw));
  // Drain it and the next push is accepted.
  REQUIRE(f.nextRow());
  CHECK_FALSE(f.nextRow());
  CHECK(f.addRow(px.data() + sw));

  // AND A DOWNSCALE CANNOT REACH IT, which is why no shipped caller had to
  // change more than an `if` into a `while`: nothing is ever pending on entry.
  const std::vector<uint8_t> big = ramp(64, 64);
  reader::CoverFitter g;
  REQUIRE(g.begin(64, 64, 33, 33, reader::CoverFit::Fill));
  for (int y = 0; y < 64; ++y) {
    REQUIRE(g.addRow(big.data() + static_cast<size_t>(y) * 64));
    int drained = 0;
    while (g.nextRow()) ++drained;
    CHECK(drained <= 1);
  }
}

namespace {


// How far the fitted picture's mean tone sits from the source's, in grey units
// of 255. A level L is grey 255 - 85L, so the two are directly comparable.
double toneDrift(const std::vector<uint8_t>& px, int sw, int sh, int pw, int ph) {
  reader::CoverFitter f;
  REQUIRE(f.begin(sw, sh, pw, ph, reader::CoverFit::Fill));
  const reader::FitBox b = f.box();
  std::vector<int> levels;
  long long ink = 0;
  for (int y = 0; y < sh; ++y) {
    REQUIRE(f.addRow(px.data() + static_cast<size_t>(y) * sw));
    while (f.nextRow()) {
      readLevels(f, levels);
      for (int lv : levels) ink += lv;
    }
  }
  long long srcSum = 0, srcN = 0;
  for (int y = b.srcY; y < b.srcY + b.srcH; ++y)
    for (int x = b.srcX; x < b.srcX + b.srcW; ++x) {
      srcSum += px[static_cast<size_t>(y) * sw + x];
      ++srcN;
    }
  const double outMean =
      255.0 - 85.0 * (static_cast<double>(ink) /
                      static_cast<double>(static_cast<long long>(b.dstW) * b.dstH));
  return outMean - static_cast<double>(srcSum) / static_cast<double>(srcN);
}

}  // namespace

TEST_CASE("the diffusion reaches all four levels") {
  // THIS CASE WAS NAMED "every emitted level is 0..3 and the two planes agree on
  // it" AND ASSERTED NEITHER -- it checked two row counts. The property in its
  // name also cannot fail: a level is read back out of two bits, so it is in
  // 0..3 by construction and one bit from each plane cannot disagree with
  // itself. That is the "reports on less than it claims" shape CLAUDE.md records
  // three times. What CAN fail is that the four levels are all USED: an
  // implementation that thresholded instead of diffusing, or that clamped an
  // index a step short, still draws a plausible gradient -- and is still
  // byte-identical to a reference that made the same mistake.
  const std::vector<uint8_t> px = ramp(1400, 2100);
  reader::CoverFitter f;
  REQUIRE(f.begin(1400, 2100, 480, 800, reader::CoverFit::Fill));
  long long seen[4] = {0, 0, 0, 0};
  std::vector<int> levels;
  int rows = 0;
  for (int y = 0; y < 2100; ++y) {
    REQUIRE(f.addRow(px.data() + static_cast<size_t>(y) * 1400));
    while (f.nextRow()) {
      ++rows;
      readLevels(f, levels);
      for (int lv : levels) ++seen[lv];
    }
  }
  CHECK(rows == 800);
  CHECK(f.rowsEmitted() == 800);
  for (int level = 0; level < 4; ++level) {
    CAPTURE(level);
    CHECK(seen[level] > 0);
  }
}

TEST_CASE("no flat grey drifts more than one twenty-eighth of a level") {
  // WHAT ERROR DIFFUSION IS FOR, and the one property that separates it from
  // thresholding: the fitted picture must carry the SAME MEAN TONE as the
  // source. A dropped term is a systematic bias, and a bias is invisible to
  // every other test here -- the whole-image reference shares the arithmetic, so
  // it agrees with a wrong implementation, and a picture with a bias still looks
  // like a picture.
  //
  // A RAMP DOES NOT DISCRIMINATE, WHICH IS WHY THIS SWEEPS. Written first
  // against the 1400x2100 ramp above, the drift was 0.14 correct and 0.18, -0.11
  // and -0.11 under three different dropped terms -- a symmetric gradient's
  // errors cancel whatever you do to them, so every mutation passed. A FLAT
  // field near an extreme is where a lost term becomes a one-sided bias, because
  // the quantiser clamps there and the error stops cancelling.
  //
  // AND THE BOUND IS SWEPT, NOT SAMPLED. Five hand-picked greys gave a worst of
  // 1.72 and would have set the tolerance at 2.0; the full 256 say the worst
  // legitimate drift is exactly 3.00, at grey 3, where every error term truncates
  // toward zero before it can accumulate. A tolerance of 2.0 would have been a
  // test that passed on its author's samples -- the trap CLAUDE.md records for
  // previewLinesThatFit, which is also why that one walks its whole space.
  //
  // Measured, worst |drift| over all 256 flat fields:
  //
  //     correct                     3.00      drop the 3/16 term    10.00
  //     drop the 1/16 term          5.00      drop the 5/16 term    15.00
  //     drop the 7/16 term         20.00      reset the error row   24.00
  //
  // So 4.0 separates them with a third of a level of margin either side, and it
  // is geometry-independent: 3.00 at grey 3 on 48x80, 96x160 and 480x800 alike.
  // The small geometry is what the suite can afford 256 times.
  //
  // The box filter's rounding is NOT in that table because it does not move this
  // number (3.00 either way) -- it is the reference comparison that catches it,
  // with 32 assertions.
  double worst = 0.0;
  int worstAt = 0;
  for (int grey = 0; grey <= 255; ++grey) {
    const std::vector<uint8_t> flat(static_cast<size_t>(160) * 240,
                                    static_cast<uint8_t>(grey));
    double d = toneDrift(flat, 160, 240, 48, 80);
    if (d < 0) d = -d;
    if (d > worst) {
      worst = d;
      worstAt = grey;
    }
  }
  CAPTURE(worstAt);
  CAPTURE(worst);
  CHECK(worst < 4.0);
}

TEST_CASE("a fit box is inside its panel and inside its source, for every shape") {
  // THE INVARIANT THE PACKER'S SAFETY RESTS ON, swept rather than argued.
  // emitRow() writes bit `0x80 >> ((dstX + c) & 7)` into byte `(dstX + c) >> 3`
  // of a (panelW + 7) / 8 array, so a negative dstX or a box running off the
  // right edge is a write outside the buffer -- not a wrong picture. The
  // arithmetic that keeps it in bounds is four branches deep in rounding, and
  // the awkward shapes are where rounding decides: 1x1, 7x9, a source one pixel
  // off the panel, a landscape panel.
  //
  // THE UPSCALE CAP IS SWEPT WITH IT, and the two halves are what the shapes are
  // for: a box may now be BIGGER than its source, so the bound that matters is
  // kMaxCoverUpscalePercent rather than the source rectangle, and 1x1 and 7x9 are
  // the shapes over it -- a 1x1 source asks for x480, which no cap admits. So the
  // sweep asserts the flag and begin() AGREE about every shape, in both
  // directions: a box within the cap must be servable, and one over it must be
  // refused. That equivalence is the whole of what makes the flag safe to rest a
  // packer's bounds on -- it is what says nothing can reach emitRow() with a
  // geometry fitCover marked.
  const int widths[7] = {1, 7, 100, 400, 877, 1400, 3000};
  const int heights[7] = {1, 9, 100, 662, 973, 2100, 4000};
  const int panels[3][2] = {{480, 800}, {528, 792}, {800, 480}};
  for (int sw : widths) {
    for (int sh : heights) {
      for (const auto& p : panels) {
        for (const reader::CoverFit fit :
             {reader::CoverFit::Fill, reader::CoverFit::Whole}) {
          CAPTURE(sw); CAPTURE(sh); CAPTURE(p[0]); CAPTURE(p[1]);
          CAPTURE(fit == reader::CoverFit::Fill);
          const reader::FitBox b = reader::fitCover(sw, sh, p[0], p[1], fit);
          CHECK(b.dstW > 0);
          CHECK(b.dstH > 0);
          CHECK(b.srcW > 0);
          CHECK(b.srcH > 0);
          CHECK(b.dstX >= 0);
          CHECK(b.dstY >= 0);
          CHECK(b.dstX + b.dstW <= p[0]);
          CHECK(b.dstY + b.dstH <= p[1]);
          CHECK(b.srcX >= 0);
          CHECK(b.srcY >= 0);
          CHECK(b.srcX + b.srcW <= sw);
          CHECK(b.srcY + b.srcH <= sh);
          // Within the cap in both axes, whichever direction the scale went.
          CHECK(static_cast<long long>(b.dstW) * 100 <=
                static_cast<long long>(b.srcW) * reader::kMaxCoverUpscalePercent);
          CHECK(static_cast<long long>(b.dstH) * 100 <=
                static_cast<long long>(b.srcH) * reader::kMaxCoverUpscalePercent);
          // A cover over the cap keeps its own size, which is the fallback
          // geometry, and is refused rather than drawn.
          if (b.tooSmall) {
            CHECK(b.dstW == b.srcW);
            CHECK(b.dstH == b.srcH);
          }
          reader::CoverFitter f;
          CHECK(f.begin(sw, sh, p[0], p[1], fit) == !b.tooSmall);
        }
      }
    }
  }
  // A non-positive dimension answers an all-zero box rather than nonsense.
  const reader::FitBox none = reader::fitCover(0, 100, 480, 800, reader::CoverFit::Fill);
  CHECK(none.dstW == 0);
  CHECK(none.dstH == 0);
}

TEST_CASE("no destination column is left without a source pixel") {
  // THE ONE PROPERTY THE REFERENCE CANNOT CHECK, because it shares the map:
  // both walk `j * dstW / srcW`, so a map that skipped a destination column
  // would skip it identically on both sides and the comparison above would
  // still be byte-identical. A BLACK source is what makes the gap visible --
  // a column nothing landed in falls back to paper, so it shows up as a white
  // stripe in a field of ink.
  //
  // The ratios are deliberately awkward: 1.0007 sources per destination row is
  // where a monotonic floor map is most likely to be written in a way that
  // skips.
  const int cases[5][4] = {
      {1400, 2100, 480, 800}, {601, 1000, 528, 792}, {1601, 801, 800, 400},
      {529, 793, 528, 792},   {877, 973, 480, 800},
  };
  for (const auto& c : cases) {
    CAPTURE(c[0]); CAPTURE(c[1]); CAPTURE(c[2]); CAPTURE(c[3]);
    const std::vector<uint8_t> black(static_cast<size_t>(c[0]) * c[1], 0);
    reader::CoverFitter f;
    REQUIRE(f.begin(c[0], c[1], c[2], c[3], reader::CoverFit::Fill));
    const reader::FitBox& b = f.box();
    long paperInsideTheBox = 0;
    for (int y = 0; y < c[1]; ++y) {
      REQUIRE(f.addRow(black.data() + static_cast<size_t>(y) * c[0]));
      while (f.nextRow()) {
        for (int x = 0; x < b.dstW; ++x) {
          const int px = b.dstX + x;
          const uint8_t bit = static_cast<uint8_t>(0x80u >> (px & 7));
          if (f.msbRow()[px >> 3] & bit) ++paperInsideTheBox;
          if (f.lsbRow()[px >> 3] & bit) ++paperInsideTheBox;
        }
      }
    }
    CHECK(f.rowsEmitted() == b.dstH);
    CHECK(paperInsideTheBox == 0);
  }
}

TEST_CASE("a cell that takes more than 65535 samples still averages correctly") {
  // WHY count_ IS uint32_t AND WAS uint16_t. A cell takes about
  // (srcW / dstW) * (srcH / dstH) samples. A JPEG cannot make that overflow 16
  // bits -- its dimensions are 16-bit -- but PNG's IHDR width is 31 bits and
  // pngd.cpp deliberately imposes no cap, so the wrap is reachable off a real
  // card.
  //
  // A 1x1 destination is the cheapest way to reach it: every source pixel lands
  // in one cell, so 300x300 is 90,000 samples where uint16_t holds 65,535. It
  // wrapped to 24,464, which made the mean 735 instead of 200 -- clamped to the
  // lightest level, so a mid-grey field came out as PAPER. That is the failure
  // this case pins, and it is a wrong PICTURE rather than a crash, which is why
  // no other test here noticed.
  const int side = 300;
  const std::vector<uint8_t> flat(static_cast<size_t>(side) * side, 200);
  reader::CoverFitter f;
  REQUIRE(f.begin(side, side, 1, 1, reader::CoverFit::Fill));
  REQUIRE(f.box().dstW == 1);
  REQUIRE(f.box().dstH == 1);
  REQUIRE(f.box().srcW == side);
  REQUIRE(f.box().srcH == side);

  for (int y = 0; y < side; ++y) {
    REQUIRE(f.addRow(flat.data() + static_cast<size_t>(y) * side));
    while (f.nextRow()) { /* the single row lands on the last push */ }
  }
  REQUIRE(f.rowsEmitted() == 1);
  // Grey 200 quantises to 170, which is level 1: lsb inked, msb paper.
  CHECK((f.msbRow()[0] & 0x80u) != 0);
  CHECK((f.lsbRow()[0] & 0x80u) == 0);
}

TEST_CASE("a ratio that could overflow the accumulator is refused, not wrapped") {
  // acc_ is uint32_t and a sample is at most 255, so a cell may take at most
  // (2^32 - 1) / 255 = 16,843,009 of them. begin() refuses beyond that rather
  // than summing modulo 2^32, which would be a wrong mean and therefore a wrong
  // picture. 5000x5000 into 1x1 is 25 M samples in the single cell.
  reader::CoverFitter f;
  CHECK_FALSE(f.begin(5000, 5000, 1, 1, reader::CoverFit::Fill));
  // Just inside is served: 4000x4000 is 16.0 M.
  CHECK(f.begin(4000, 4000, 1, 1, reader::CoverFit::Fill));
  // And nothing shaped like a cover is anywhere near -- 1400x2100 into 480x800
  // is 3 x 3 = 9 samples a cell.
  CHECK(f.begin(1400, 2100, 480, 800, reader::CoverFit::Fill));
}

TEST_CASE("CoverFitter refuses a geometry it cannot serve, and never aborts") {
  reader::CoverFitter f;
  // Never begun.
  CHECK_FALSE(f.addRow(nullptr));
  CHECK_FALSE(f.nextRow());
  CHECK(f.rowsEmitted() == 0);
  CHECK(f.lastEmittedRow() == -1);

  CHECK_FALSE(f.begin(0, 100, 480, 800, reader::CoverFit::Fill));
  CHECK_FALSE(f.begin(100, 0, 480, 800, reader::CoverFit::Fill));
  CHECK_FALSE(f.begin(100, 100, 0, 800, reader::CoverFit::Fill));
  CHECK_FALSE(f.begin(100, 100, 480, -1, reader::CoverFit::Fill));

  // A row past the source's declared height is misuse, not a silent no-op.
  const std::vector<uint8_t> px = ramp(64, 32);
  REQUIRE(f.begin(64, 32, 16, 8, reader::CoverFit::Fill));
  for (int y = 0; y < 32; ++y) {
    REQUIRE(f.addRow(px.data() + static_cast<size_t>(y) * 64));
    while (f.nextRow()) { /* drained */ }
  }
  CHECK(f.rowsEmitted() == 8);
  CHECK_FALSE(f.addRow(px.data()));
  CHECK_FALSE(f.nextRow());
  CHECK(f.rowsEmitted() == 8);
}

// --- The two cases that pin the packing to something OUTSIDE this file -------
//
// Everything above compares our packing against a reference that shares it, so
// a mirrored or inverted packing passes all of it. These read the bytes back
// through reader::Framebuffer, which is the authority on both questions:
// bitMask() is `0x80 >> (physX % 8)` and the class documents `true/1 = white`.

TEST_CASE("a packed plane row means to Framebuffer what it means here") {
  // One black column at x == 0 on an otherwise white field, fitted 1:1 so no
  // rounding and no diffusion can move it: 0 and 255 are both exactly on a
  // level, so every error term is zero and the picture is what was fed in.
  const int w = 16, h = 8;
  std::vector<uint8_t> px(static_cast<size_t>(w) * h, 255);
  for (int y = 0; y < h; ++y) px[static_cast<size_t>(y) * w] = 0;

  reader::CoverFitter f;
  REQUIRE(f.begin(w, h, w, h, reader::CoverFit::Fill));
  REQUIRE(f.box().dstW == w);
  REQUIRE(f.box().dstH == h);

  reader::Framebuffer msb(w, h), lsb(w, h);
  msb.clear(true);
  lsb.clear(true);
  int rows = 0;
  for (int y = 0; y < h; ++y) {
    REQUIRE(f.addRow(px.data() + static_cast<size_t>(y) * w));
    REQUIRE(f.nextRow());
    // The raw bytes first: black at the leftmost pixel of the byte is bit 7.
    CHECK(f.msbRow()[0] == 0x7F);
    CHECK(f.lsbRow()[0] == 0x7F);
    CHECK(f.msbRow()[1] == 0xFF);
    std::memcpy(msb.data() + static_cast<size_t>(y) * msb.physRowBytes(), f.msbRow(),
                static_cast<size_t>(f.box().dstW + 7) / 8);
    std::memcpy(lsb.data() + static_cast<size_t>(y) * lsb.physRowBytes(), f.lsbRow(),
                static_cast<size_t>(f.box().dstW + 7) / 8);
    ++rows;
  }
  REQUIRE(rows == h);

  for (int y = 0; y < h; ++y) {
    // getPixel is `bit != 0`, and false is ink. Column 0 is black in both
    // planes -- level 3 -- and every other column is paper in both.
    CHECK_FALSE(msb.getPixel(0, y));
    CHECK_FALSE(lsb.getPixel(0, y));
    for (int x = 1; x < w; ++x) {
      CHECK(msb.getPixel(x, y));
      CHECK(lsb.getPixel(x, y));
    }
  }
}

TEST_CASE("the two mid levels land in the plane the panel expects") {
  // 170 and 85 are exactly the two mid levels, so again nothing diffuses.
  // png.cpp's composeGray reads a level back as (msb << 1) | lsb with an INKED
  // plane counting 1, so:
  //   grey 170 -> level 1 -> lsb inked, msb paper
  //   grey  85 -> level 2 -> msb inked, lsb paper
  // Getting these two the other way round is a cover whose midtones are
  // swapped: it would still look like a picture, which is why it is asserted.
  const int w = 8, h = 2;
  std::vector<uint8_t> px(static_cast<size_t>(w) * h, 170);
  for (int x = 0; x < w; ++x) px[static_cast<size_t>(w) + x] = 85;

  reader::CoverFitter f;
  REQUIRE(f.begin(w, h, w, h, reader::CoverFit::Fill));

  REQUIRE(f.addRow(px.data()));
  REQUIRE(f.nextRow());
  CHECK(f.msbRow()[0] == 0xFF);  // paper
  CHECK(f.lsbRow()[0] == 0x00);  // ink

  REQUIRE(f.addRow(px.data() + w));
  REQUIRE(f.nextRow());
  CHECK(f.msbRow()[0] == 0x00);  // ink
  CHECK(f.lsbRow()[0] == 0xFF);  // paper
}

TEST_CASE("the letterbox bands come out paper, not ink") {
  // A Whole fit leaves columns outside the box, and they must not read as a
  // black frame around the cover -- the caller tints them from its board.
  const int w = 40, h = 20;  // 2.0, much wider than the 8x16 panel below
  const std::vector<uint8_t> px(static_cast<size_t>(w) * h, 0);  // all black

  reader::CoverFitter f;
  REQUIRE(f.begin(w, h, 8, 16, reader::CoverFit::Whole));
  REQUIRE(f.box().dstW == 8);
  REQUIRE(f.box().dstH == 4);  // 20 * 8 / 40
  REQUIRE(f.box().dstX == 0);

  // Now one that leaves side bands: a tall source into a wide panel.
  reader::CoverFitter g;
  REQUIRE(g.begin(20, 40, 16, 16, reader::CoverFit::Whole));
  REQUIRE(g.box().dstH == 16);
  REQUIRE(g.box().dstW == 8);
  REQUIRE(g.box().dstX == 4);
  const std::vector<uint8_t> tall(static_cast<size_t>(20) * 40, 0);
  bool emitted = false;
  for (int y = 0; y < 40 && !emitted; ++y) {
    REQUIRE(g.addRow(tall.data() + static_cast<size_t>(y) * 20));
    emitted = g.nextRow();
  }
  REQUIRE(emitted);
  // Columns 0..3 and 12..15 are band; 4..11 are a black cover.
  CHECK(g.msbRow()[0] == 0xF0);
  CHECK(g.lsbRow()[0] == 0xF0);
  CHECK(g.msbRow()[1] == 0x0F);
  CHECK(g.lsbRow()[1] == 0x0F);
}
