#pragma once
#include <cstdint>

namespace reader {

// ONE PHYSICAL ROW'S COLUMNS [pxLo, pxHi), AS BYTES: which byte the run starts
// and ends in, and which bits of those two are inside it. The bytes between them
// are wholly inside, which is what lets a caller treat the middle as a straight
// run.
//
// WHY THIS IS A HEADER AND NOT A THIRD COPY. Every area primitive in core/ has
// now been converted from a per-pixel setPixel loop to a byte-wise write into
// the physical store -- Framebuffer::fillRect, veilRect and ditherRect -- and
// each of them needs exactly this arithmetic at the two ends of every run. It
// was written twice before this file existed, and the second copy is the
// extraction point rather than the fifth: MSB-first edge masks are the shape
// that produces a one-pixel bug on one path only, and a bug that appears in the
// tint but not the fill is a bug nobody attributes to a shared idea.
//
// The panel geometries are both multiples of 8, so NOTHING ON THE DEVICE
// exercises these masks. Overlay geometry is derived by subtraction from a
// centred panel, so arbitrary origins and widths do occur, and it is the tests
// that have to catch a bad mask rather than the glass.
struct PhysRun {
  int b0, b1;         // first and last byte of the run, inclusive
  uint8_t firstMask;  // the bits of b0 that are inside the run
  uint8_t lastMask;   // the bits of b1 that are inside the run
};

// Requires pxLo >= 0 and pxHi > pxLo -- every caller clips first, which is what
// makes that true and also what retires any negative-coordinate handling here.
inline PhysRun physRunFor(int pxLo, int pxHi) {
  PhysRun r;
  r.b0 = pxLo >> 3;
  r.b1 = (pxHi - 1) >> 3;
  // pxLo's bit and everything to its right; pxHi's bit and everything to its
  // left. MSB-first, so "right" is the low bits. A run that ends on a byte
  // boundary has (pxHi & 7) == 0, where `0xFF >> 0` would keep the whole NEXT
  // byte instead of the whole last one -- the off-by-one an edge mask is for.
  r.firstMask = static_cast<uint8_t>(0xFFu >> (pxLo & 7));
  r.lastMask = (pxHi & 7) != 0 ? static_cast<uint8_t>(~(0xFFu >> (pxHi & 7))) : 0xFFu;
  return r;
}

}  // namespace reader
