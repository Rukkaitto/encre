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

PngDiff diffPng(const Framebuffer& fb, const char* path) {
  PngDiff d;
  int w = 0, h = 0, n = 0;
  unsigned char* img = stbi_load(path, &w, &h, &n, 1);
  if (!img) {
    d.status = PngDiff::Status::kDecodeFailed;
    return d;
  }
  d.width = w;
  d.height = h;
  if (w != fb.width() || h != fb.height()) {
    d.status = PngDiff::Status::kSizeMismatch;
    stbi_image_free(img);
    return d;
  }
  const auto px = toGray(fb);
  for (size_t i = 0; i < px.size(); ++i) {
    if (px[i] == img[i]) continue;
    ++d.diffPixels;
    if (d.firstDiffX < 0) {
      d.firstDiffX = static_cast<int>(i % static_cast<size_t>(w));
      d.firstDiffY = static_cast<int>(i / static_cast<size_t>(w));
    }
  }
  d.status = d.diffPixels == 0 ? PngDiff::Status::kMatch : PngDiff::Status::kPixelMismatch;
  stbi_image_free(img);
  return d;
}

const char* pngDiffStatusName(PngDiff::Status status) {
  switch (status) {
    case PngDiff::Status::kMatch: return "match";
    case PngDiff::Status::kDecodeFailed: return "decode failed (missing or unreadable)";
    case PngDiff::Status::kSizeMismatch: return "size mismatch";
    case PngDiff::Status::kPixelMismatch: return "pixel mismatch";
  }
  return "unknown";
}

bool comparePng(const Framebuffer& fb, const char* path, int* wOut, int* hOut) {
  const PngDiff d = diffPng(fb, path);
  if (wOut) *wOut = d.width;
  if (hOut) *hOut = d.height;
  return d.ok();
}

}  // namespace reader
#endif  // READER_DESKTOP
