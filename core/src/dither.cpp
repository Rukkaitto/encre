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
constexpr int kClustered[4][4] = {
    {12, 6, 11, 13},
    {4, 0, 1, 9},
    {8, 3, 2, 5},
    {14, 10, 7, 15},
};

// The dispersed counterpart, for glyph and icon edge coverage. The classic
// recursive Bayer 4x4: every rank is as far from its neighbours as the tile
// allows, which is what makes a partial tone read as tone rather than texture.
constexpr int kBayer[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

// The veil behind an overlay: a clustered WHITE dot on a THREE-pixel grid, which
// is a third pattern for a third job and not either of the two above.
//
// The overlay boards declare:
//
//   radial-gradient(circle, #ffffff 1.3px, transparent 1.5px);
//   background-size: 3px 3px
//
// so: a white dot ~2.6px across, centred in every 3px cell. Rasterising that on
// the 3x3 cell is not a judgement call -- measure from the cell's centre. The
// centre pixel is at distance 0, its four edge-adjacent neighbours at 1, its four
// diagonal ones at 1.414; the dot's radius is 1.3, so the first five are inside
// it and the corners are outside:
//
//     . o .        `o` white (the dot)
//     o o o        `.` the parent's ink, surviving
//     . o .
//
// Five cells of nine whitened, four left inked -- 44%, against the 41% the
// board's 5.31 px^2 circle covers arithmetically. ditherRect's 4px grid used for
// the same job would whiten 4 of 16 and leave 75% standing: about half as dense a
// veil, which is the whole reason this is its own pattern.
//
// It is clustered for the same reason kClustered is, and the trap is the same
// one. The ink that survives here is the four corners of each cell, and corners
// of adjoining cells touch, so what is left is a 2x2 block on a 3px pitch -- a
// receding shape. A dispersed pattern of identical coverage would leave those
// four pixels scattered as singletons, and a lone black pixel on white carries
// contrast on all four sides and reads heavier than its area. That is what made
// the dispersed tint come out denser and grainier than the board's at identical
// arithmetic coverage, and a veil is the same measurement with the colours
// swapped.
constexpr bool kVeilDot[3][3] = {
    {false, true, false},
    {true, true, true},
    {false, true, false},
};

// Cell index for an absolute coordinate. ditherRect can use `& 3` because its
// grid is a power of two; a 3px grid cannot, and `%` alone yields a negative
// result for a negative coordinate -- which would index outside kVeilDot rather
// than wrapping. Overlay geometry is derived by subtraction from a centred
// panel, so a negative origin is a real possibility on the narrow geometry.
constexpr int cell3(int v) { return ((v % 3) + 3) % 3; }
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

void veilRect(Framebuffer& fb, int x, int y, int w, int h) {
  // Keyed on absolute framebuffer coordinates, exactly as ditherRect is: an
  // overlay veils the band above its panel and the band below it, and a pattern
  // phased on each band's own origin would show a seam where the two meet.
  for (int yy = y; yy < y + h; ++yy)
    for (int xx = x; xx < x + w; ++xx)
      // Setting white only, never black. The parent has already been drawn, so
      // this subtracts from its ink -- and it means a second overlay's veil over
      // a first one's changes nothing further rather than bleaching it away.
      if (kVeilDot[cell3(yy)][cell3(xx)]) fb.setPixel(xx, yy, true);
}

}  // namespace reader
