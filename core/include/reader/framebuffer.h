#pragma once
#include <cstdint>
#include <vector>

namespace reader {

// 1bpp, row-major, MSB-first (bit 7 = leftmost pixel of the byte).
// true/1 = white, 0 = black (freeink-sdk convention).
//
// Panel widths are multiples of 8, but the class does not rely on it: the row
// stride is rounded up to ceil(width / 8) so storage always covers width()
// and no in-bounds coordinate can address memory outside the buffer. A
// non-multiple-of-8 width simply leaves the last byte of each row partly
// unused. Non-positive dimensions collapse to an empty, inert buffer (all
// operations are no-ops; getPixel reports white) rather than throwing, because
// the firmware is built with -fno-exceptions.
class Framebuffer {
 public:
  Framebuffer(int width, int height);

  int width() const { return width_; }
  int height() const { return height_; }
  int rowBytes() const { return rowBytes_; }
  const uint8_t* data() const { return bytes_.data(); }
  uint8_t* data() { return bytes_.data(); }
  int sizeBytes() const { return static_cast<int>(bytes_.size()); }

  void clear(bool white = true);
  void setPixel(int x, int y, bool white);
  bool getPixel(int x, int y) const;
  void fillRect(int x, int y, int w, int h, bool white);

 private:
  int width_;
  int height_;
  int rowBytes_;
  std::vector<uint8_t> bytes_;
};

}  // namespace reader
