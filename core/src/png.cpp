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

// The 4-level ramp, indexed by (msb << 1) | lsb: no ink in either plane is
// paper, ink in both is full black, one plane each gives the two mid greys.
static const unsigned char kRamp[4] = {0xFF, 0xAA, 0x55, 0x00};

static std::vector<unsigned char> composeGray(const Framebuffer& lsb, const Framebuffer& msb) {
  std::vector<unsigned char> px(static_cast<size_t>(lsb.width()) * lsb.height());
  for (int y = 0; y < lsb.height(); ++y)
    for (int x = 0; x < lsb.width(); ++x) {
      const int l = lsb.getPixel(x, y) ? 0 : 1;  // ink (false == black) == level bit set
      const int m = msb.getPixel(x, y) ? 0 : 1;
      px[static_cast<size_t>(y) * lsb.width() + x] = kRamp[(m << 1) | l];
    }
  return px;
}

// Compares an already-composed greyscale raster against the image on disk.
static PngDiff diffRaster(const std::vector<unsigned char>& px, int w, int h, const char* path) {
  PngDiff d;
  int fw = 0, fh = 0, n = 0;
  unsigned char* img = stbi_load(path, &fw, &fh, &n, 1);
  if (!img) {
    d.status = PngDiff::Status::kDecodeFailed;
    return d;
  }
  d.width = fw;
  d.height = fh;
  if (fw != w || fh != h) {
    d.status = PngDiff::Status::kSizeMismatch;
    stbi_image_free(img);
    return d;
  }
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

bool writePng(const Framebuffer& fb, const char* path) {
  auto px = toGray(fb);
  return stbi_write_png(path, fb.width(), fb.height(), 1, px.data(), fb.width()) != 0;
}

bool writeGrayPng(const Framebuffer& lsb, const Framebuffer& msb, const char* path) {
  if (lsb.width() != msb.width() || lsb.height() != msb.height()) return false;
  const auto px = composeGray(lsb, msb);
  if (px.empty()) return false;
  return stbi_write_png(path, lsb.width(), lsb.height(), 1, px.data(), lsb.width()) != 0;
}

PngDiff diffPng(const Framebuffer& fb, const char* path) {
  return diffRaster(toGray(fb), fb.width(), fb.height(), path);
}

PngDiff diffGrayPng(const Framebuffer& lsb, const Framebuffer& msb, const char* path) {
  PngDiff d;
  if (lsb.width() != msb.width() || lsb.height() != msb.height()) {
    // Not a decode problem, but the caller's inputs are unusable; reporting a
    // size mismatch is the closest honest answer.
    d.status = PngDiff::Status::kSizeMismatch;
    return d;
  }
  return diffRaster(composeGray(lsb, msb), lsb.width(), lsb.height(), path);
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
