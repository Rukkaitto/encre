#include "reader/framebuffer.h"

#include "reader/profile.h"

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

// The view. Same geometry arithmetic as above -- it has to be, or an owning and
// a viewing frame of the same dimensions would describe different bytes and the
// driver's memcpy contract would be stated twice, differently.
//
// The store is NOT cleared: these bytes belong to someone else who may already
// have put something in them (the driver memsets its own framebuffer to white in
// begin(), and the paint path clears before every full render anyway). An owning
// frame starts white because it is fresh memory with nothing in it to respect.
//
// REFUSE, DO NOT CLAMP, when the memory cannot hold the frame. See the header
// for why. Checked in the body rather than in the initialiser list because it
// needs rowBytes_ and physHeight(), which are what the list is computing.
Framebuffer::Framebuffer(uint8_t* data, size_t bytes, int width, int height, Rotation rot)
    : width_(width > 0 && height > 0 ? width : 0),
      height_(width > 0 && height > 0 ? height : 0),
      rot_(rot),
      rowBytes_((physWidth() + 7) / 8),
      viewing_(true),
      view_(data) {
  const size_t need = static_cast<size_t>(rowBytes_) * static_cast<size_t>(physHeight());
  if (data == nullptr || bytes < need) {
    width_ = height_ = 0;
    rowBytes_ = 0;
    view_ = nullptr;
  }
}

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

// sizeBytes() rather than bytes_.size(): an inert buffer -- a non-positive
// geometry or a refused view -- reports zero either way, and a viewing frame has
// no vector to ask.
void Framebuffer::clear(bool white) {
  if (sizeBytes() == 0) return;
  std::memset(data(), white ? 0xFF : 0x00, static_cast<size_t>(sizeBytes()));
}

void Framebuffer::setPixel(int x, int y, bool white) {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
  uint8_t& b = data()[byteIndex(x, y)];
  const uint8_t mask = bitMask(x, y);
  if (white) b |= mask; else b &= static_cast<uint8_t>(~mask);
}

bool Framebuffer::getPixel(int x, int y) const {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) return true;
  return (data()[byteIndex(x, y)] & bitMask(x, y)) != 0;
}

void Framebuffer::fillRect(int x, int y, int w, int h, bool white) {
  // THE PRIMITIVE MOST LIKELY TO BE THE ANSWER, which is why it is measured
  // rather than assumed. It is a per-pixel setPixel loop -- a bounds check, a
  // byteIndex (a division under rotation), a bitMask (a modulo) and a
  // read-modify-write, for every pixel -- and that is the exact shape veilRect
  // was rewritten byte-wise to escape. An overlay panel is ~340x420 and a
  // full-bleed focused row is ~300x72, so one reader-menu frame asks for on the
  // order of 200,000 of them.
  PhaseSpan sp(Phase::Fill);
  for (int yy = y; yy < y + h; ++yy)
    for (int xx = x; xx < x + w; ++xx)
      setPixel(xx, yy, white);
}

}  // namespace reader
