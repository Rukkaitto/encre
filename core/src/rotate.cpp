#include "reader/rotate.h"

#include <cstddef>

#include "reader/framebuffer.h"

namespace reader {

namespace {
// setPixel already clips, so a mismatched destination would silently produce a
// cropped or offset image -- the hardest kind of bring-up bug to spot. Bail out
// with dst untouched instead, so the symptom is a blank screen rather than a
// plausible-looking wrong one. (No exceptions: the firmware is -fno-exceptions.)
bool isTransposeOf(const Framebuffer& src, const Framebuffer& dst) {
  return src.width() > 0 && src.height() > 0 && dst.width() == src.height() &&
         dst.height() == src.width();
}

// Rotate one 8x8 pixel block, whole bytes in and whole bytes out.
//
// Why this exists: the per-pixel form below costs a bounds check, two divisions
// and a read-modify-write per pixel, and the shell runs it FOUR times per paint
// (one per plane, plus the grayscale cleanup rebase). Measured on the desktop at
// the X3's geometry it was 3.86 ms against 0.57 ms for all of Home's actual
// drawing -- 87% of a render pass spent moving bits that were already correct.
// On device that is roughly 190 ms of every 220 ms pass. It is also
// content-independent, which is why a focus move on Home and a push into a
// nearly empty placeholder measured within 4% of each other.
//
// The mapping, derived once so nobody has to re-derive it: rotate90CCW sends
// src(x, y) to dst(y, w-1-x). Take an 8-aligned source block at (bx, by). Its
// eight source bytes s[r] hold row by+r, columns bx..bx+7, MSB leftmost. Its
// destination is the 8-aligned block at dst byte column by/8, dst rows
// w-8-bx .. w-1-bx. Writing j = r and k = 7-c through both coordinate maps
// leaves:
//
//     bit (7-r) of d[k]  =  bit k of s[r]
//
// which is a bit-matrix transpose with one axis reversed. Strides are in bytes.
inline void rotateBlockCCW(const uint8_t* s, int srcStride, uint8_t* d, int dstStride) {
  uint8_t in[8];
  for (int r = 0; r < 8; ++r) in[r] = s[static_cast<size_t>(r) * srcStride];
  for (int k = 0; k < 8; ++k) {
    unsigned out = 0;
    for (int r = 0; r < 8; ++r) out |= ((static_cast<unsigned>(in[r]) >> k) & 1u) << (7 - r);
    d[static_cast<size_t>(k) * dstStride] = static_cast<uint8_t>(out);
  }
}
}  // namespace

// Not on the hot path -- the shell rotates CCW, measured (CW renders the screen
// 180 degrees out). Left per-pixel on purpose: an unused fast path is an unused
// place for a bug. If a board ever needs CW, give it the same block treatment
// and the same reference test.
void rotate90CW(const Framebuffer& src, Framebuffer& dst) {
  if (!isTransposeOf(src, dst)) return;
  for (int y = 0; y < src.height(); ++y)
    for (int x = 0; x < src.width(); ++x)
      dst.setPixel(src.height() - 1 - y, x, src.getPixel(x, y));
}

void rotate90CCW(const Framebuffer& src, Framebuffer& dst) {
  if (!isTransposeOf(src, dst)) return;
  const int w = src.width(), h = src.height();
  const int srcStride = src.rowBytes(), dstStride = dst.rowBytes();
  const uint8_t* sp = src.data();
  uint8_t* dp = dst.data();

  // Whole blocks. Both panel geometries are multiples of 8 in both axes
  // (528x792 and 480x800), so on the device this covers everything and the
  // per-pixel remainder below never runs -- but the primitive must stay correct
  // for any size, because the tests use odd ones and a future panel might.
  const int fullW = (w / 8) * 8, fullH = (h / 8) * 8;
  for (int by = 0; by < fullH; by += 8)
    for (int bx = 0; bx < fullW; bx += 8)
      rotateBlockCCW(sp + static_cast<size_t>(by) * srcStride + bx / 8, srcStride,
                     dp + static_cast<size_t>(w - 8 - bx) * dstStride + by / 8, dstStride);

  if (fullW == w && fullH == h) return;
  // The two edge strips the block loop could not cover, per pixel.
  for (int y = 0; y < h; ++y)
    for (int x = fullW; x < w; ++x) dst.setPixel(y, w - 1 - x, src.getPixel(x, y));
  for (int y = fullH; y < h; ++y)
    for (int x = 0; x < fullW; ++x) dst.setPixel(y, w - 1 - x, src.getPixel(x, y));
}

}  // namespace reader
