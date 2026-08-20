#pragma once
namespace reader {
class Framebuffer;
// dst(w=src.h, h=src.w): dst[x][y] = src rotated 90 deg clockwise.
// If the panel image appears upside down on hardware, switch main.cpp to
// rotate90CCW (both mappings provided so bring-up is a one-line change).
void rotate90CW(const Framebuffer& src, Framebuffer& dst);
void rotate90CCW(const Framebuffer& src, Framebuffer& dst);
}  // namespace reader
