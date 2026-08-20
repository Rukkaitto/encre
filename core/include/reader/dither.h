#pragma once
namespace reader {
class Framebuffer;

// Ordered (Bayer 4x4) dither fill. `level` is 0 (white) to 4 (solid black);
// the panel has no real greys, so apparent tone comes from a stipple the eye
// integrates at reading distance. Used for cover placeholders, sleep-screen
// fields and the veil behind overlays.
void ditherRect(Framebuffer& fb, int x, int y, int w, int h, int level);
}  // namespace reader
