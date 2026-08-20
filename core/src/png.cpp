#include "reader/png.h"

#ifdef READER_DESKTOP
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <vector>

#include "reader/framebuffer.h"

namespace reader {

static std::vector<unsigned char> toGray(const Framebuffer& fb) {
  std::vector<unsigned char> px(static_cast<size_t>(fb.width()) * fb.height());
  for (int y = 0; y < fb.height(); ++y)
    for (int x = 0; x < fb.width(); ++x)
      px[static_cast<size_t>(y) * fb.width() + x] = fb.getPixel(x, y) ? 0xFF : 0x00;
  return px;
}

bool writePng(const Framebuffer& fb, const char* path) {
  auto px = toGray(fb);
  return stbi_write_png(path, fb.width(), fb.height(), 1, px.data(), fb.width()) != 0;
}

bool comparePng(const Framebuffer& fb, const char* path, int* wOut, int* hOut) {
  int w = 0, h = 0, n = 0;
  unsigned char* img = stbi_load(path, &w, &h, &n, 1);
  if (!img) return false;
  if (wOut) *wOut = w;
  if (hOut) *hOut = h;
  bool same = (w == fb.width() && h == fb.height());
  if (same) {
    auto px = toGray(fb);
    for (size_t i = 0; i < px.size() && same; ++i) same = (px[i] == img[i]);
  }
  stbi_image_free(img);
  return same;
}

}  // namespace reader
#endif  // READER_DESKTOP
