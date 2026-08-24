#include "reader/framebuffer.h"

#include <cstring>

namespace reader {
namespace {

// One PHYSICAL row's columns [pxLo, pxHi), as bytes: which byte the run starts
// and ends in, and which bits of those two are inside it. The bytes between them
// are wholly inside, which is what lets the middle be a memset.
//
// Hoisted out of the row loop because a fill's run is the SAME for every row it
// touches -- a rectangle is a rectangle in whichever space it is walked -- so the
// masks are computed once per fillRect rather than once per row. veilRect cannot
// do that (its mask depends on the row's phase in the 3px tile) and so recomputes
// per row; the shapes look alike and this one is deliberately cheaper.
struct PhysRun {
  int b0, b1;         // first and last byte of the run, inclusive
  uint8_t firstMask;  // the bits of b0 that are inside the run
  uint8_t lastMask;   // the bits of b1 that are inside the run
};

PhysRun physRunFor(int pxLo, int pxHi) {
  PhysRun r;
  r.b0 = pxLo >> 3;
  r.b1 = (pxHi - 1) >> 3;
  // pxLo's bit and everything to its right; pxHi's bit and everything to its
  // left. MSB-first, so "right" is the low bits. A run that ends on a byte
  // boundary has (pxHi & 7) == 0, where `0xFF >> 0` would keep the whole NEXT
  // byte instead of the whole last one -- the off-by-one an edge mask is for.
  r.firstMask = static_cast<uint8_t>(0xFFu >> (pxLo & 7));
  r.lastMask = (pxHi & 7) != 0 ? static_cast<uint8_t>(~(0xFFu >> (pxHi & 7))) : 0xFFu;
  return r;
}

// The two edge bytes are masked down to the run rather than special-cased, so a
// run whose origin or width is not a multiple of eight writes no pixel outside
// itself. Both panel widths are multiples of 8, so nothing on the device
// exercises that -- but overlay geometry is derived by subtraction from a centred
// panel, so arbitrary origins and widths do occur, and it is the tests that have
// to catch a bad mask rather than the glass.
void fillPhysRun(uint8_t* row, const PhysRun& r, bool white) {
  const uint8_t fillByte = white ? 0xFFu : 0x00u;
  if (r.b0 == r.b1) {
    const uint8_t m = static_cast<uint8_t>(r.firstMask & r.lastMask);
    row[r.b0] = static_cast<uint8_t>((row[r.b0] & ~m) | (fillByte & m));
    return;
  }
  row[r.b0] = static_cast<uint8_t>((row[r.b0] & ~r.firstMask) | (fillByte & r.firstMask));
  if (r.b1 - r.b0 > 1)
    std::memset(row + r.b0 + 1, fillByte, static_cast<size_t>(r.b1 - r.b0 - 1));
  row[r.b1] = static_cast<uint8_t>((row[r.b1] & ~r.lastMask) | (fillByte & r.lastMask));
}

}  // namespace

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

// WHAT THIS USED TO BE, and why it is worth the bytes above: a per-pixel loop
// calling setPixel, which per pixel did a bounds check, byteIndex (a division,
// and under rotation a subtraction and a multiply as well), bitMask (a modulo)
// and a bit-addressed read-modify-write. Measured on the DEVICE it moved about
// 200,000 pixels in 158 ms -- 790 ns a pixel -- and it was 62% of an
// item-actions render and 53% of the reader menu's, the two most expensive
// renders on the panel. veilRect, already byte-wise, covers the whole 418,176
// pixel frame at 24 ns a pixel: the same class of work, 33x apart, and the
// distance is entirely the per-pixel arithmetic.
//
// Nothing about the OUTPUT changes. test_framebuffer.cpp keeps the per-pixel
// form as its reference and asserts this produces the identical framebuffer at
// both panel geometries, under both rotations, and for runs that start and end
// mid-byte -- the same shape of proof test_dither.cpp gives the veil, and for
// the same reason: every golden in the suite was blessed against the loop this
// replaces, so a disagreement means the fast path is wrong.
void Framebuffer::fillRect(int x, int y, int w, int h, bool white) {
  if (w <= 0 || h <= 0) return;
  if (width_ <= 0 || height_ <= 0) return;  // inert buffer: no store to write into
  // CLIP FIRST, which is what setPixel's own bounds check used to do one pixel at
  // a time. A fill has no phase -- unlike the veil and the dither, it is keyed on
  // nothing -- so clipping can never move what it draws; it only has to be exact
  // at the edges. In 64-bit because x + w is the caller's arithmetic and a
  // pathological w would otherwise overflow the addition before the comparison
  // could reject it, which is undefined rather than merely wrong.
  const long long x0 = x, y0 = y;
  const long long x1 = x0 + w, y1 = y0 + h;
  const int xLo = static_cast<int>(x0 > 0 ? x0 : 0);
  const int xHi = static_cast<int>(x1 < width_ ? x1 : width_);
  const int yLo = static_cast<int>(y0 > 0 ? y0 : 0);
  const int yHi = static_cast<int>(y1 < height_ ? y1 : height_);
  if (xLo >= xHi || yLo >= yHi) return;

  uint8_t* const base = data();
  const int stride = rowBytes_;
  if (rot_ == Rotation::Ccw) {
    // UNDER ROTATION A LOGICAL ROW IS A PHYSICAL COLUMN, so walking a logical row
    // byte-wise would smear the rectangle diagonally across the frame -- and the
    // device is the rotated case, so that mistake would look right on the desktop
    // and on every golden and wrong only on glass. This is veilRect's structure
    // for veilRect's reason.
    //
    // A logical COLUMN is a physical row: physX = logY, physY = width - 1 - logX
    // (byteIndex above, and rotate90CCW before it). So the outer loop is the
    // logical x, each value of which is one physical row whose columns are the
    // logical y range -- and that range is the same for every column of the rect,
    // which is why the run is computed once.
    const PhysRun run = physRunFor(yLo, yHi);
    for (int lx = xLo; lx < xHi; ++lx)
      fillPhysRun(base + static_cast<size_t>(width_ - 1 - lx) * static_cast<size_t>(stride), run,
                  white);
  } else {
    // Unrotated: a logical row IS a physical row, so this is the plain case.
    const PhysRun run = physRunFor(xLo, xHi);
    for (int ly = yLo; ly < yHi; ++ly)
      fillPhysRun(base + static_cast<size_t>(ly) * static_cast<size_t>(stride), run, white);
  }
}

}  // namespace reader
