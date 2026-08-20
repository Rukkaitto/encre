#pragma once
#include <cstdint>
#include <vector>

namespace reader {

// 1bpp, row-major, MSB-first (bit 7 = leftmost pixel of the byte).
// true/1 = white, 0 = black (freeink-sdk convention). Width % 8 == 0.
class Framebuffer {
 public:
  Framebuffer(int width, int height);

  int width() const { return width_; }
  int height() const { return height_; }
  int rowBytes() const { return width_ / 8; }
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
  std::vector<uint8_t> bytes_;
};

}  // namespace reader
