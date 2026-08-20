#include "reader/framebuffer.h"

#include <cstring>

namespace reader {

// Round the stride up so ceil(width / 8) bytes back every row: sizing storage
// by width / 8 while width_ kept the full width let setPixel's bounds check
// pass for columns with no backing byte. A non-positive dimension collapses
// both dimensions to zero, giving an empty buffer whose every operation is a
// no-op -- the firmware has no exceptions to throw instead.
Framebuffer::Framebuffer(int width, int height)
    : width_(width > 0 && height > 0 ? width : 0),
      height_(width > 0 && height > 0 ? height : 0),
      rowBytes_((width_ + 7) / 8),
      bytes_(static_cast<size_t>(rowBytes_) * static_cast<size_t>(height_), 0xFF) {}

void Framebuffer::clear(bool white) {
  if (bytes_.empty()) return;
  std::memset(bytes_.data(), white ? 0xFF : 0x00, bytes_.size());
}

void Framebuffer::setPixel(int x, int y, bool white) {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
  uint8_t& b = bytes_[static_cast<size_t>(y) * rowBytes_ + x / 8];
  const uint8_t mask = static_cast<uint8_t>(0x80u >> (x % 8));
  if (white) b |= mask; else b &= static_cast<uint8_t>(~mask);
}

bool Framebuffer::getPixel(int x, int y) const {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) return true;
  const uint8_t b = bytes_[static_cast<size_t>(y) * rowBytes_ + x / 8];
  return (b >> (7 - x % 8)) & 1;
}

void Framebuffer::fillRect(int x, int y, int w, int h, bool white) {
  for (int yy = y; yy < y + h; ++yy)
    for (int xx = x; xx < x + w; ++xx)
      setPixel(xx, yy, white);
}

}  // namespace reader
