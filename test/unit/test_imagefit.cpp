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

  // 1. Box filter the crop rectangle down to dstW x dstH, rounding to nearest.
  std::vector<long> sum(static_cast<size_t>(b.dstW) * b.dstH, 0);
  std::vector<long> num(static_cast<size_t>(b.dstW) * b.dstH, 0);
  for (int i = 0; i < b.srcH; ++i) {
    const int dy = static_cast<int>(static_cast<long long>(i) * b.dstH / b.srcH);
    const uint8_t* row = src.data() + static_cast<size_t>(b.srcY + i) * sw + b.srcX;
    for (int j = 0; j < b.srcW; ++j) {
      const int dx = static_cast<int>(static_cast<long long>(j) * b.dstW / b.srcW);
      sum[static_cast<size_t>(dy) * b.dstW + dx] += row[j];
      num[static_cast<size_t>(dy) * b.dstW + dx] += 1;
    }
  }
  std::vector<int> grey(static_cast<size_t>(b.dstW) * b.dstH, 255);
  for (size_t i = 0; i < grey.size(); ++i)
    if (num[i] > 0) grey[i] = static_cast<int>((sum[i] + num[i] / 2) / num[i]);

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
    bool emitted = false;
    REQUIRE(f.addRow(src.data() + static_cast<size_t>(y) * sw, emitted));
    if (!emitted) continue;
    out.msb.insert(out.msb.end(), f.msbRow(), f.msbRow() + out.bytes);
    out.lsb.insert(out.lsb.end(), f.lsbRow(), f.lsbRow() + out.bytes);
    CHECK(f.emittedRow() == out.rows);
    ++out.rows;
  }
  CHECK(f.rowsEmitted() == out.rows);
  CHECK(f.rowsEmitted() == f.box().dstH);
  return out;
}

}  // namespace

TEST_CASE("CoverFitter streams to exactly what the whole-image reference produces") {
  // Both panels, both fits, and source shapes that are NOT integer multiples of
  // the destination -- an exact multiple would hide every rounding bug in the
  // box filter.
  //
  // The four shapes are chosen to reach both crop directions: 601x1000 is
  // 0.601, narrower than the X3's 0.667, so Fill crops its HEIGHT there and its
  // WIDTH on the X4; 877x973 and 1400x2100 crop width or nothing.
  const int panels[2][2] = {{480, 800}, {528, 792}};
  const int sources[4][2] = {{1400, 2100}, {877, 973}, {601, 1000}, {1600, 2400}};

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

  // The squarest corpus cover, on the X4: Fill cuts its title off at both
  // edges. 973 * 480 / 800 = 583.8 -> 584 of 877 columns, a 33.4% loss.
  const reader::FitBox sq = reader::fitCover(877, 973, 480, 800, reader::CoverFit::Fill);
  CHECK(sq.srcW == 584);
  CHECK(sq.srcH == 973);
  CHECK(sq.srcX == 146);
}

TEST_CASE("Fill centres a HEIGHT crop at 0.4, not at 0.5") {
  // The only case the 0.4 exists for, and the one the panels reach least often:
  // a source relatively TALLER than the panel, so Fill crops rows rather than
  // columns. 2 of 225 corpus covers on the X4, 18 of 225 on the X3.
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

TEST_CASE("fitCover never upscales, and Fill degrades to Whole-at-1:1") {
  // 3 of 225 corpus covers are smaller than the X4 panel in some axis; the
  // smallest is 400x662. A box filter cannot enlarge, and CoverFitter's one
  // -source-row-to-one-destination-row streaming cannot either, so the box is
  // clamped to the source rectangle and centred.
  const reader::FitBox small = reader::fitCover(400, 662, 480, 800, reader::CoverFit::Fill);
  CHECK(small.dstW <= 480);
  CHECK(small.dstH <= 800);
  CHECK(small.dstW <= small.srcW);
  CHECK(small.dstH <= small.srcH);
  CHECK(small.dstX > 0);
  CHECK(small.dstY > 0);

  const reader::FitBox smallWhole = reader::fitCover(400, 662, 480, 800, reader::CoverFit::Whole);
  CHECK(smallWhole.dstW == 400);
  CHECK(smallWhole.dstH == 662);
  CHECK(smallWhole.srcW == 400);
  CHECK(smallWhole.srcH == 662);
  CHECK(smallWhole.dstX == 40);
  CHECK(smallWhole.dstY == 69);

  // And the fitter serves it rather than refusing: a small cover is 1.3% of the
  // corpus and a refusal there is a book with no cover at all.
  const std::vector<uint8_t> px = ramp(400, 662);
  reader::CoverFitter f;
  REQUIRE(f.begin(400, 662, 480, 800, reader::CoverFit::Whole));
  for (int y = 0; y < 662; ++y) {
    bool emitted = false;
    REQUIRE(f.addRow(px.data() + static_cast<size_t>(y) * 400, emitted));
  }
  CHECK(f.rowsEmitted() == 662);
}

TEST_CASE("every emitted level is 0..3 and the two planes agree on it") {
  const std::vector<uint8_t> px = ramp(1400, 2100);
  reader::CoverFitter f;
  REQUIRE(f.begin(1400, 2100, 480, 800, reader::CoverFit::Fill));
  int rows = 0;
  for (int y = 0; y < 2100; ++y) {
    bool emitted = false;
    REQUIRE(f.addRow(px.data() + static_cast<size_t>(y) * 1400, emitted));
    if (emitted) ++rows;
  }
  CHECK(rows == 800);
  CHECK(f.rowsEmitted() == 800);
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
  // `dstW <= srcW` is in here too, because it is not a nicety: CoverFitter's
  // whole one-source-row-to-one-destination-row streaming depends on it, and
  // begin() refuses without it.
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
          CHECK(b.dstW <= b.srcW);
          CHECK(b.dstH <= b.srcH);
          reader::CoverFitter f;
          CHECK(f.begin(sw, sh, p[0], p[1], fit));
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
      bool emitted = false;
      REQUIRE(f.addRow(black.data() + static_cast<size_t>(y) * c[0], emitted));
      if (!emitted) continue;
      for (int x = 0; x < b.dstW; ++x) {
        const int px = b.dstX + x;
        const uint8_t bit = static_cast<uint8_t>(0x80u >> (px & 7));
        if (f.msbRow()[px >> 3] & bit) ++paperInsideTheBox;
        if (f.lsbRow()[px >> 3] & bit) ++paperInsideTheBox;
      }
    }
    CHECK(f.rowsEmitted() == b.dstH);
    CHECK(paperInsideTheBox == 0);
  }
}

TEST_CASE("CoverFitter refuses a geometry it cannot serve, and never aborts") {
  reader::CoverFitter f;
  bool emitted = true;
  // Never begun.
  CHECK_FALSE(f.addRow(nullptr, emitted));
  CHECK_FALSE(emitted);
  CHECK(f.rowsEmitted() == 0);
  CHECK(f.emittedRow() == -1);

  CHECK_FALSE(f.begin(0, 100, 480, 800, reader::CoverFit::Fill));
  CHECK_FALSE(f.begin(100, 0, 480, 800, reader::CoverFit::Fill));
  CHECK_FALSE(f.begin(100, 100, 0, 800, reader::CoverFit::Fill));
  CHECK_FALSE(f.begin(100, 100, 480, -1, reader::CoverFit::Fill));

  // A row past the source's declared height is misuse, not a silent no-op.
  const std::vector<uint8_t> px = ramp(64, 32);
  REQUIRE(f.begin(64, 32, 16, 8, reader::CoverFit::Fill));
  for (int y = 0; y < 32; ++y) {
    bool e = false;
    REQUIRE(f.addRow(px.data() + static_cast<size_t>(y) * 64, e));
  }
  CHECK(f.rowsEmitted() == 8);
  bool extra = true;
  CHECK_FALSE(f.addRow(px.data(), extra));
  CHECK_FALSE(extra);
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
    bool emitted = false;
    REQUIRE(f.addRow(px.data() + static_cast<size_t>(y) * w, emitted));
    REQUIRE(emitted);
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

  bool emitted = false;
  REQUIRE(f.addRow(px.data(), emitted));
  REQUIRE(emitted);
  CHECK(f.msbRow()[0] == 0xFF);  // paper
  CHECK(f.lsbRow()[0] == 0x00);  // ink

  REQUIRE(f.addRow(px.data() + w, emitted));
  REQUIRE(emitted);
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
  for (int y = 0; y < 40 && !emitted; ++y)
    REQUIRE(g.addRow(tall.data() + static_cast<size_t>(y) * 20, emitted));
  REQUIRE(emitted);
  // Columns 0..3 and 12..15 are band; 4..11 are a black cover.
  CHECK(g.msbRow()[0] == 0xF0);
  CHECK(g.lsbRow()[0] == 0xF0);
  CHECK(g.msbRow()[1] == 0x0F);
  CHECK(g.lsbRow()[1] == 0x0F);
}
