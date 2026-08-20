#include "reader/rotate.h"

#include "reader/framebuffer.h"

namespace reader {

void rotate90CW(const Framebuffer& src, Framebuffer& dst) {
  for (int y = 0; y < src.height(); ++y)
    for (int x = 0; x < src.width(); ++x)
      dst.setPixel(src.height() - 1 - y, x, src.getPixel(x, y));
}

void rotate90CCW(const Framebuffer& src, Framebuffer& dst) {
  for (int y = 0; y < src.height(); ++y)
    for (int x = 0; x < src.width(); ++x)
      dst.setPixel(y, src.width() - 1 - x, src.getPixel(x, y));
}

}  // namespace reader
