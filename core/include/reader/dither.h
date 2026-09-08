#pragma once
#include "reader/text.h"  // Ink

namespace reader {
class Framebuffer;

// Ordered dither fill, on the boards' 4px tint grid: a CLUSTERED dot, not a
// Bayer matrix -- see the comment in dither.cpp for the measurement that settled
// it. `level` is 0 (white) to 4 (solid black); the panel has no real greys, so
// apparent tone comes from a stipple the eye integrates at reading distance.
//
// ONE PRODUCTION CALLER: renderSleep's full-panel field. This line said "cover
// placeholders and sleep-screen fields" and the placeholders are all three gone
// -- Home's, the Library row's and Book details' -- so the plural was the last of
// them outliving the sentence. Naming the caller is deliberate: it goes stale
// loudly where a category does not.
//
// `ink` IS TESTED CAPABILITY AND NOT WORKING BEHAVIOUR, which is the honest
// version of what this comment used to claim. The boards declare the tint twice,
// once each way:
//
//   .dither-dots     { background-color: #ffffff; ... circle, #000000 1.1px ... }
//   .dither-dots-inv { background-color: #000000; ... circle, #ffffff 1.1px ... }
//
// Same dot, same 4px grid, colours swapped -- and Ink::White SETS PAPER where
// Ink::Black sets ink, on identical cells, so a tint on a filled ground and one
// on paper are the same stipple seen against opposite grounds rather than two
// patterns to keep in step. The instance was a FOCUSED Library row's placeholder
// cover, reversed out of the row's black fill, and that placeholder is gone: the
// one surviving caller passes black. Kept the way ListRow::trackingEm1000 is
// kept -- test_dither.cpp drives both inks across 30 rectangles x both rotations
// x all four levels, and Bookmarks.dc.html is a board that asks for a reversed
// tint again -- and stated rather than assumed, because a reader with no producer
// is the shape this project has been bitten by from two directions.
//
// NOT for the veil behind an overlay, which is a different board declaration and
// a different pattern -- see veilRect. (A white-inked ditherRect is not that
// veil either: the grid is 4px against the veil's 3px, which is the whole
// measurement the comment below turns on.)
//
// Written eight columns at a time straight into the framebuffer's physical
// store, so this is one of the four routines in core/ that know the rotation
// exists -- with fillRect, the glyph blit and veilRect. It reads like the veil
// (a tile with a phase) and is built like a FILL: a byte is eight columns and
// this tile is four wide, so every byte of a row carries the same mask and the
// run hoists out of the loop. The veil's tile is three wide, which is why its
// mask cannot. See dither.cpp for the measurement -- Sleep tints the whole panel,
// which was 91% of that screen's render -- and for what the rotated case has to
// do differently. It is pure optimisation: test_dither.cpp keeps the per-pixel
// form as its reference and asserts byte-identity under both rotations, at every
// level, in both inks.
void ditherRect(Framebuffer& fb, int x, int y, int w, int h, int level,
                Ink ink = Ink::Black);

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
//
// Written eight columns at a time straight into the framebuffer's physical
// store, which is why this was the FIRST drawing routine in core/ to know the
// rotation exists -- there are four now: an overlay veils the WHOLE frame, so it
// was the most expensive thing on an overlay repaint by a wide margin. See dither.cpp for the
// measurement and for what the rotated case has to do differently. It is pure
// optimisation -- test_dither.cpp keeps the per-pixel form as its reference and
// asserts byte-identity under both rotations.
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
