#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace reader {

// Which way the backing store is turned relative to the coordinates callers
// draw against.
//
//   None -- physical == logical. What the simulator and every golden use.
//   Ccw  -- the store is the logical frame rotated a quarter turn
//           counter-clockwise, so a portrait canvas lands in a landscape buffer.
//
// CCW is the direction the Xteink panel needs, measured on X3 hardware: CW
// renders the whole screen 180 degrees out (the two differ by exactly half a
// turn, so swapping them is not the fix for a mirrored image). Unverified on X4
// -- if an X4 comes out upside down, this enum is the line. The mapping is
// pinned byte-for-byte against rotate90CCW in test_rotate.cpp.
enum class Rotation { None, Ccw };

// 1bpp, row-major, MSB-first (bit 7 = leftmost pixel of the byte).
// true/1 = white, 0 = black (freeink-sdk convention).
//
// Two coordinate spaces, and the split is the whole point of the class:
//
//   LOGICAL  -- what width()/height() report and what every drawing routine
//               works in. The constructor's arguments are logical.
//   PHYSICAL -- what data()/sizeBytes()/physRowBytes() describe: the bytes the
//               panel driver is handed. Under Rotation::Ccw the store is
//               allocated transposed, so the stride derives from the PHYSICAL
//               width and setPixel/getPixel map logical to physical as they go.
//
// That transform replaces a full-frame transpose on the paint path: rotating
// 418k pixels cost 37 ms of a 521 ms repaint whether one pixel changed or all
// of them did, and a text screen inks about 8%. The reference firmware does the
// same thing -- its primitives take an orientation and map coordinates as they
// draw, straight into the driver's framebuffer.
//
// Panel widths are multiples of 8, but the class does not rely on it: the row
// stride is rounded up to ceil(physicalWidth / 8) so storage always covers the
// frame and no in-bounds coordinate can address memory outside the buffer. A
// non-multiple-of-8 width simply leaves the last byte of each row partly
// unused. Non-positive dimensions collapse to an empty, inert buffer (all
// operations are no-ops; getPixel reports white) rather than throwing, because
// the firmware is built with -fno-exceptions.
class Framebuffer {
 public:
  Framebuffer(int width, int height, Rotation rot = Rotation::None);

  // Logical: the space callers draw in. Unaffected by the rotation.
  int width() const { return width_; }
  int height() const { return height_; }

  Rotation rotation() const { return rot_; }

  // Physical: the shape of the bytes behind data(). Equal to the logical pair
  // under Rotation::None, transposed under Rotation::Ccw.
  int physWidth() const { return rot_ == Rotation::Ccw ? height_ : width_; }
  int physHeight() const { return rot_ == Rotation::Ccw ? width_ : height_; }
  // Stride of a PHYSICAL row -- ceil(physWidth() / 8), the step between the
  // rows of data(). Deliberately not called rowBytes(): next to width() that
  // name reads as "bytes per row of width() pixels", which stops being true
  // the moment the buffer is rotated. Every caller wants the store's stride,
  // because every caller uses it to walk data().
  int physRowBytes() const { return rowBytes_; }

  const uint8_t* data() const { return bytes_.data(); }
  uint8_t* data() { return bytes_.data(); }
  int sizeBytes() const { return static_cast<int>(bytes_.size()); }

  void clear(bool white = true);
  void setPixel(int x, int y, bool white);
  bool getPixel(int x, int y) const;
  void fillRect(int x, int y, int w, int h, bool white);

 private:
  // Logical (x, y) -> index into bytes_ and the bit mask within it. Only valid
  // for an in-bounds coordinate; callers bounds-check first.
  size_t byteIndex(int x, int y) const;
  uint8_t bitMask(int x, int y) const;

  int width_;
  int height_;
  Rotation rot_;
  int rowBytes_;
  std::vector<uint8_t> bytes_;
};

}  // namespace reader
