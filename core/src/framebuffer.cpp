#include "reader/framebuffer.h"

#include <cstring>

namespace reader {

// Round the stride up so ceil(physWidth / 8) bytes back every physical row:
// sizing storage by width / 8 while width_ kept the full width let setPixel's
// bounds check pass for columns with no backing byte. A non-positive dimension
// collapses both dimensions to zero, giving an empty buffer whose every
// operation is a no-op -- the firmware has no exceptions to throw instead.
//
// The stride derives from the PHYSICAL width, which under Rotation::Ccw is the
// logical height. Deriving it from the logical width instead would size the
// store correctly by total bytes and index it wrongly by rows, which reads as a
// diagonally sheared screen -- plausible enough to waste an afternoon on.
Framebuffer::Framebuffer(int width, int height, Rotation rot)
    : width_(width > 0 && height > 0 ? width : 0),
      height_(width > 0 && height > 0 ? height : 0),
      rot_(rot),
      rowBytes_((physWidth() + 7) / 8),
      bytes_(static_cast<size_t>(rowBytes_) * static_cast<size_t>(physHeight()), 0xFF) {}

// Logical to physical, and it must match rotate90CCW exactly: that function
// writes dst.setPixel(srcY, srcW - 1 - srcX), so physX = logY and
// physY = logicalWidth - 1 - logX. The mirror term is not optional -- dropping
// it gives a plain transpose, which is a rotation composed with a flip, and
// test_rotate.cpp's byte-identity case is what catches that.
size_t Framebuffer::byteIndex(int x, int y) const {
  if (rot_ == Rotation::Ccw) {
    const int physX = y, physY = width_ - 1 - x;
    return static_cast<size_t>(physY) * rowBytes_ + physX / 8;
  }
  return static_cast<size_t>(y) * rowBytes_ + x / 8;
}

uint8_t Framebuffer::bitMask(int x, int y) const {
  const int physX = rot_ == Rotation::Ccw ? y : x;
  return static_cast<uint8_t>(0x80u >> (physX % 8));
}

void Framebuffer::clear(bool white) {
  if (bytes_.empty()) return;
  std::memset(bytes_.data(), white ? 0xFF : 0x00, bytes_.size());
}

void Framebuffer::setPixel(int x, int y, bool white) {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
  uint8_t& b = bytes_[byteIndex(x, y)];
  const uint8_t mask = bitMask(x, y);
  if (white) b |= mask; else b &= static_cast<uint8_t>(~mask);
}

bool Framebuffer::getPixel(int x, int y) const {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) return true;
  return (bytes_[byteIndex(x, y)] & bitMask(x, y)) != 0;
}

void Framebuffer::fillRect(int x, int y, int w, int h, bool white) {
  for (int yy = y; yy < y + h; ++yy)
    for (int xx = x; xx < x + w; ++xx)
      setPixel(xx, yy, white);
}

}  // namespace reader
