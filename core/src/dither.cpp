#include "reader/dither.h"

#include "reader/framebuffer.h"

namespace reader {

namespace {
// A clustered-dot 4x4 halftone, not a Bayer matrix.
//
// The design's tint is one shape repeated on a grid:
//
//   radial-gradient(circle, #000 1.1px, transparent 1.3px); background-size: 4px 4px
//
// -- a single round dot, ~2.4px across, centred in every 4px cell. A Bayer
// matrix reaches the same *nominal* coverage by scattering the inked cells as
// far apart as it can, which is the right answer for photographic dithering and
// the wrong one here: at a quarter coverage Bayer inks four isolated single
// pixels per 4x4 tile where the board inks one 2x2 blob. Isolated pixels read
// heavier than a clustered dot of the same area (each one is surrounded by
// contrast on all four sides), so the placeholder cover came out visibly denser
// and grainier than the board's, at identical arithmetic coverage.
//
// The cells are therefore ranked by distance from the tile's centre instead of
// dispersed away from it, so ink grows outward as one dot:
//
//     level 1  . . . .      level 2  . # . .      level 3  . # # .
//              . # # .               # # # .               # # # #
//              . # # .               . # # #               # # # #
//              . . . .               . . # .               . # # .
//
// Level 1 is the design's case: a 2x2 dot on a 4px pitch, which is what the
// board's 2.4px circle covers once rasterised. Within each distance ring the
// ranks are handed out in opposite pairs, so a partly-filled ring stays
// balanced about the centre rather than growing to one side.
//
// Density is still strictly monotonic in `level` -- 4, 8, 12 then 16 cells of
// every 16 -- because the ranks are a permutation of 0..15, exactly as Bayer's
// were. That is the property callers and tests rely on; the arrangement within a
// tile is what changed.
// The dispersed counterpart, for glyph and icon edge coverage. The classic
// recursive Bayer 4x4: every rank is as far from its neighbours as the tile
// allows, which is what makes a partial tone read as tone rather than texture.
constexpr int kBayer[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

constexpr int kClustered[4][4] = {
    {12, 6, 11, 13},
    {4, 0, 1, 9},
    {8, 3, 2, 5},
    {14, 10, 7, 15},
};
}  // namespace

int bayer4(int x, int y) { return kBayer[y & 3][x & 3]; }

void ditherRect(Framebuffer& fb, int x, int y, int w, int h, int level) {
  if (level <= 0) return;
  if (level > 4) level = 4;
  // level 1..4 -> threshold 4, 8, 12, 16 out of 16 cells inked.
  const int threshold = level * 4;
  // Keyed on absolute framebuffer coordinates, not on the rect's own origin, so
  // two adjoining dithered areas share one continuous grid instead of showing a
  // seam where their phases disagree.
  for (int yy = y; yy < y + h; ++yy)
    for (int xx = x; xx < x + w; ++xx)
      if (kClustered[yy & 3][xx & 3] < threshold) fb.setPixel(xx, yy, false);
}

}  // namespace reader
