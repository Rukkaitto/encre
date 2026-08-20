#include "reader/dither.h"

#include "reader/framebuffer.h"

namespace reader {

namespace {
// Classic Bayer 4x4 threshold matrix, values 0..15.
constexpr int kBayer[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};
}  // namespace

void ditherRect(Framebuffer& fb, int x, int y, int w, int h, int level) {
  if (level <= 0) return;
  if (level > 4) level = 4;
  // level 1..4 -> threshold 4, 8, 12, 16 out of 16 cells inked.
  const int threshold = level * 4;
  for (int yy = y; yy < y + h; ++yy)
    for (int xx = x; xx < x + w; ++xx)
      if (kBayer[yy & 3][xx & 3] < threshold) fb.setPixel(xx, yy, false);
}

}  // namespace reader
