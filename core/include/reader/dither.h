#pragma once
namespace reader {
class Framebuffer;

// Ordered dither fill, on the boards' 4px tint grid: a CLUSTERED dot, not a
// Bayer matrix -- see the comment in dither.cpp for the measurement that settled
// it. `level` is 0 (white) to 4 (solid black); the panel has no real greys, so
// apparent tone comes from a stipple the eye integrates at reading distance.
// Used for cover placeholders and sleep-screen fields.
//
// NOT for the veil behind an overlay, which is a different board declaration and
// a different pattern -- see veilRect.
void ditherRect(Framebuffer& fb, int x, int y, int w, int h, int level);

// Knocks an overlay's parent back with the boards' dim veil: a white dot on a
// 3px grid, leaving 4 of every 9 pixels of the parent's ink standing.
//
// From the overlay boards' own declaration, which is NOT ditherRect's:
//
//   .dim-veil { background-image: radial-gradient(circle, #ffffff 1.3px,
//                                                 transparent 1.5px);
//               background-size: 3px 3px; }
//
// A WHITE dot ~2.6px across on a THREE-pixel grid, where the tint above is a
// BLACK dot on a four-pixel one. Reusing the 4px pattern -- the tempting move,
// since the matrix is right there -- whitens 4 cells of 16 against this one's 5
// of 9, so it veils about half as densely and reads as a screen that is merely
// smudged rather than one deliberately behind something.
//
// The pattern is clustered for the same reason ditherRect's is: the board draws
// one round dot per cell, so the ink has to recede as a single shape. Dispersing
// it would leave the surviving ink as isolated pixels, and a lone black pixel on
// white reads heavier than its area -- so a dispersed veil at the board's
// arithmetic density would come out darker than the board's, which is exactly
// the trap the tint fell into once already.
//
// Keyed on absolute framebuffer coordinates, as ditherRect is, so an overlay's
// veil above and below its panel is one continuous grid instead of two with a
// seam between them. Sets white only, so it is idempotent and stacked overlays
// do not bleach each other's veils away.
void veilRect(Framebuffer& fb, int x, int y, int w, int h);

// Dispersed (true Bayer 4x4) threshold for the pixel at panel coordinates
// (x, y), 0..15. Deliberately NOT the clustered-dot matrix ditherRect uses.
//
// Those two want opposite things. A tint is one shape repeated on a grid, so its
// ink must grow outward as a single dot -- see the comment in dither.cpp, which
// measured clustered as the only way to match the board's density. A glyph EDGE
// is the reverse: it is a partial tone the eye should integrate into a smooth
// contour, and clustering the ink there puts a 2x2 blob on the edge of a stem,
// which reads as the stroke getting thicker rather than smoother.
//
// Indexed by absolute panel coordinates, not by position within the glyph, so
// the pattern is stable across the screen and two identical letters at
// different x do not dither differently.
int bayer4(int x, int y);
}  // namespace reader
