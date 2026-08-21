#pragma once
namespace reader {
class Framebuffer;

// Rotate a 1bpp framebuffer by a quarter turn. dst must be the transpose of
// src: dst.width() == src.height() and dst.height() == src.width(). Both must
// also be Rotation::None. If either does not hold, both functions leave dst
// untouched rather than writing a partial or sheared image.
//
//   rotate90CW:  (x, y) -> (src.height() - 1 - y, x)
//   rotate90CCW: (x, y) -> (y, src.width() - 1 - x)
//
// NEITHER IS ON THE PAINT PATH ANY MORE. The shell draws into a Rotation::Ccw
// framebuffer, which applies the CCW mapping per pixel as it draws instead of
// transposing the finished frame. These stay because rotate90CCW is the
// known-good reference the rotated framebuffer is checked against, byte for
// byte, and because its block-transpose tests are the specification for the
// mapping. See the note at rotate90CCW's definition.
//
// Measured on Xteink X3 hardware: CCW is the correct direction (CW renders the
// UI upside down). Not yet verified on X4. Note the two directions differ by
// 180 degrees, not by a flip, so swapping them is not the fix for a mirrored
// image. That fact now also lives on Rotation in framebuffer.h, which is where
// the device's orientation is actually chosen.
void rotate90CW(const Framebuffer& src, Framebuffer& dst);
void rotate90CCW(const Framebuffer& src, Framebuffer& dst);
}  // namespace reader
