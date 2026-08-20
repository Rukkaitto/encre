#pragma once
namespace reader {
class Framebuffer;

// Rotate a 1bpp framebuffer by a quarter turn. dst must be the transpose of
// src: dst.width() == src.height() and dst.height() == src.width(). If it is
// not, both functions leave dst untouched rather than writing a partial image.
//
//   rotate90CW:  (x, y) -> (src.height() - 1 - y, x)
//   rotate90CCW: (x, y) -> (y, src.width() - 1 - x)
//
// If the panel image comes out rotated the wrong way on hardware, switch
// main.cpp to the other function -- both mappings are provided so bring-up is a
// one-line change. Note this is a 180-degree difference, not a flip, so it is
// not the fix for a mirrored image.
void rotate90CW(const Framebuffer& src, Framebuffer& dst);
void rotate90CCW(const Framebuffer& src, Framebuffer& dst);
}  // namespace reader
