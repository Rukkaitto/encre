#include "reader/rotate.h"

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
}  // namespace

void rotate90CW(const Framebuffer& src, Framebuffer& dst) {
  if (!isTransposeOf(src, dst)) return;
  for (int y = 0; y < src.height(); ++y)
    for (int x = 0; x < src.width(); ++x)
      dst.setPixel(src.height() - 1 - y, x, src.getPixel(x, y));
}

void rotate90CCW(const Framebuffer& src, Framebuffer& dst) {
  if (!isTransposeOf(src, dst)) return;
  for (int y = 0; y < src.height(); ++y)
    for (int x = 0; x < src.width(); ++x)
      dst.setPixel(y, src.width() - 1 - x, src.getPixel(x, y));
}

}  // namespace reader
