#pragma once
namespace reader {
class Framebuffer;

// Ordered (Bayer 4x4) dither fill. `level` is 0 (white) to 4 (solid black);
// the panel has no real greys, so apparent tone comes from a stipple the eye
// integrates at reading distance. Used for cover placeholders, sleep-screen
// fields and the veil behind overlays.
void ditherRect(Framebuffer& fb, int x, int y, int w, int h, int level);

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
