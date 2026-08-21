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

// THE SHAPE THE FAST PATH BELOW ENCODES: a whole row of the tile at cy == 1, and
// a single column of it at cx == 1. Everything from here to veilRect is derived
// from that, so it is asserted rather than commented -- a change to kVeilDot that
// left the derivation alone would otherwise draw a different veil in silence,
// and the symptom (a veil of the wrong density) reads as a design regression
// rather than as a broken optimisation.
//
// Note also that the shape is symmetric under transposition, kVeilDot[a][b] ==
// kVeilDot[b][a], which is what lets the rotated branch of veilRect swap the
// axes and keep the same two cases.
constexpr bool veilIsRowPlusColumn() {
  for (int cy = 0; cy < 3; ++cy)
    for (int cx = 0; cx < 3; ++cx)
      if (kVeilDot[cy][cx] != (cy == 1 || cx == 1)) return false;
  return true;
}
static_assert(veilIsRowPlusColumn(),
              "veilRect's byte masks assume a full row at cy==1 and one column at cx==1");

// One byte of the stripe -- the tile's cx == 1 column -- for each phase a byte
// can start on. A byte covers eight consecutive columns, so which of its bits
// are on the stripe depends on the first column's cell index, and 8 % 3 == 2
// means that index advances by two per byte and repeats every three.
//
// Derived from kVeilDot rather than written out, so the masks cannot drift from
// the pattern they are meant to be.
constexpr uint8_t stripeByte(int phase) {
  uint8_t m = 0;
  for (int k = 0; k < 8; ++k)
    if (kVeilDot[0][(phase + k) % 3]) m = static_cast<uint8_t>(m | (0x80u >> k));
  return m;
}
constexpr uint8_t kStripeByte[3] = {stripeByte(0), stripeByte(1), stripeByte(2)};
// Three columns of every eight-column byte, except where the phase puts only two
// in it: 3 + 3 + 2 == 8 bits across 24 columns, which is the 1-in-3 the stripe
// is. Spelled out so the derivation above has something to be checked against.
static_assert(kStripeByte[0] == 0x49 && kStripeByte[1] == 0x92 && kStripeByte[2] == 0x24,
              "the stripe's byte masks are not 1-in-3");

// Whiten one PHYSICAL row's columns [pxLo, pxHi), eight at a time.
//
// `full` is the tile's cy == 1 case, where every column of the run goes white;
// otherwise only the columns on the stripe do. Both are a mask OR, because the
// veil only ever SETS white -- which is what makes it idempotent, and what lets
// this touch a byte once instead of eight times.
//
// The two edge bytes are masked down to the run rather than special-cased, so a
// run whose origin or width is not a multiple of eight writes no pixel outside
// itself. That is the case a byte-wise path gets wrong, and the panel geometries
// are both multiples of 8, so nothing on the device would have caught it.
void veilPhysRun(uint8_t* row, int pxLo, int pxHi, bool full) {
  const int b0 = pxLo >> 3, b1 = (pxHi - 1) >> 3;
  int phase = (2 * b0) % 3;  // (8 * b) % 3, and b0 is never negative
  for (int b = b0; b <= b1; ++b) {
    uint8_t m = full ? 0xFFu : kStripeByte[phase];
    if (b == b0) m = static_cast<uint8_t>(m & (0xFFu >> (pxLo & 7)));
    if (b == b1 && (pxHi & 7) != 0) m = static_cast<uint8_t>(m & ~(0xFFu >> (pxHi & 7)));
    row[b] = static_cast<uint8_t>(row[b] | m);
    phase += 2;
    if (phase >= 3) phase -= 3;
  }
}
}  // namespace

int bayer4(int x, int y) { return kBayer[y & 3][x & 3]; }

void ditherRect(Framebuffer& fb, int x, int y, int w, int h, int level, Ink ink) {
  if (level <= 0) return;
  if (level > 4) level = 4;
  // level 1..4 -> threshold 4, 8, 12, 16 out of 16 cells inked.
  const int threshold = level * 4;
  // The dot's colour, in the framebuffer's convention (true = paper). The CELLS
  // chosen are the same either way: the board's inverted tint is the same dot on
  // the same grid drawn in the other colour, so a focused row's cover and an
  // unfocused one's cannot drift out of phase with each other.
  const bool dot = (ink == Ink::White);
  // Keyed on absolute framebuffer coordinates, not on the rect's own origin, so
  // two adjoining dithered areas share one continuous grid instead of showing a
  // seam where their phases disagree.
  for (int yy = y; yy < y + h; ++yy)
    for (int xx = x; xx < x + w; ++xx)
      if (kClustered[yy & 3][xx & 3] < threshold) fb.setPixel(xx, yy, dot);
}

void veilRect(Framebuffer& fb, int x, int y, int w, int h) {
  // Keyed on absolute framebuffer coordinates, exactly as ditherRect is: an
  // overlay veils the band above its panel and the band below it, and a pattern
  // phased on each band's own origin would show a seam where the two meet.
  //
  // WHAT THIS USED TO BE, and why it is worth the bytes below: a per-pixel loop
  // calling setPixel, with the cell index computed as ((v % 3) + 3) % 3 for BOTH
  // axes on EVERY pixel -- four integer divisions and a bit-addressed
  // read-modify-write per pixel. Measured on the desktop at 528x792 it cost
  // 2.33 ms against ditherRect's 1.12 ms for the same shape of work, and this
  // project's desktop-to-device ratio is about 65x, so it was on the order of
  // 150 ms of every overlay repaint. It also ran on all 418k pixels of the frame
  // whichever pixel had changed, which is the same shape of waste the full-frame
  // rotate90CCW was.
  //
  // Nothing about the OUTPUT changes here; test_dither.cpp keeps the per-pixel
  // form as its reference and asserts this produces the identical framebuffer at
  // both geometries, under both rotations, and for runs that start and end
  // mid-byte.
  if (w <= 0 || h <= 0) return;
  const int fw = fb.width(), fh = fb.height();
  if (fw <= 0 || fh <= 0) return;
  // CLIP FIRST, which is what setPixel's own bounds check used to do one pixel at
  // a time, and it is also what retires the old cell3(): a clipped coordinate is
  // never negative, so `% 3` wraps correctly without the extra division that was
  // there to rescue a negative one. The PHASE is still the absolute coordinate's,
  // so clipping cannot move the pattern -- a rect with a negative origin (overlay
  // geometry is derived by subtraction from a centred panel, so it happens on the
  // narrow geometry) keeps the grid it would have had.
  const int xLo = x > 0 ? x : 0, xHi = (x + w) < fw ? (x + w) : fw;
  const int yLo = y > 0 ? y : 0, yHi = (y + h) < fh ? (y + h) : fh;
  if (xLo >= xHi || yLo >= yHi) return;

  uint8_t* const base = fb.data();
  const int stride = fb.physRowBytes();
  if (fb.rotation() == Rotation::Ccw) {
    // UNDER ROTATION A LOGICAL ROW IS A PHYSICAL COLUMN, so walking a logical row
    // byte-wise would smear the pattern diagonally across the frame -- and the
    // device is the rotated case, so that mistake would look right on the desktop
    // and on every golden and wrong only on glass.
    //
    // A logical COLUMN is a physical row: physX = logY, physY = width - 1 - logX
    // (framebuffer.cpp's byteIndex, and rotate90CCW before it). So the outer loop
    // is the logical x, each value of which is one physical row whose columns are
    // the logical y range. The tile is symmetric under transposition, so the two
    // cases are the same ones with the axes swapped: a logical x on the tile's
    // centre column fills its whole physical row, and every other one gets the
    // stripe -- which now runs in logical y, and lands on exactly the pixels the
    // per-pixel form's kVeilDot[cell3(yy)][cell3(xx)] chose.
    for (int lx = xLo; lx < xHi; ++lx)
      veilPhysRun(base + static_cast<size_t>(fw - 1 - lx) * static_cast<size_t>(stride), yLo, yHi,
                  lx % 3 == 1);
  } else {
    // Unrotated: a logical row IS a physical row, so this is the plain case.
    for (int ly = yLo; ly < yHi; ++ly)
      veilPhysRun(base + static_cast<size_t>(ly) * static_cast<size_t>(stride), xLo, xHi,
                  ly % 3 == 1);
  }
}

}  // namespace reader
