#include <cstring>
#include <utility>

#include "doctest.h"
#include "reader/framebuffer.h"

using reader::Framebuffer;

TEST_CASE("framebuffer starts white and stores pixels MSB-first") {
  Framebuffer fb(16, 4);            // 16 px wide -> 2 bytes per row
  CHECK(fb.width() == 16);
  CHECK(fb.height() == 4);
  CHECK(fb.physRowBytes() == 2);
  CHECK(fb.data()[0] == 0xFF);      // all white

  fb.setPixel(0, 0, false);         // black at leftmost pixel
  CHECK(fb.data()[0] == 0x7F);      // bit 7 cleared -> MSB is leftmost
  CHECK_FALSE(fb.getPixel(0, 0));
  CHECK(fb.getPixel(1, 0));

  fb.setPixel(0, 0, true);
  CHECK(fb.data()[0] == 0xFF);
}

TEST_CASE("fillRect clips to bounds") {
  Framebuffer fb(16, 4);
  fb.fillRect(12, 2, 100, 100, false);  // overflows right and bottom
  CHECK_FALSE(fb.getPixel(12, 2));
  CHECK_FALSE(fb.getPixel(15, 3));
  CHECK(fb.getPixel(11, 2));
  CHECK(fb.getPixel(12, 1));
}

TEST_CASE("clear repaints everything") {
  Framebuffer fb(8, 1);
  fb.fillRect(0, 0, 8, 1, false);
  fb.clear(true);
  CHECK(fb.data()[0] == 0xFF);
}

TEST_CASE("a width that is not a multiple of 8 still has backing storage") {
  // The docs say width % 8 == 0, but nothing enforced it: storage was sized
  // width/8 (= 1 byte/row for width 12) while width_ kept the full 12, so
  // setPixel's bounds check passed for columns 8..11 with nothing behind them
  // (ASAN: heap-buffer-overflow). Round the stride up instead.
  Framebuffer fb(12, 4);
  CHECK(fb.width() == 12);
  CHECK(fb.physRowBytes() == 2);  // ceil(12 / 8)
  CHECK(fb.sizeBytes() == fb.physRowBytes() * fb.height());

  // Every in-bounds coordinate must be writable without leaving the buffer.
  for (int y = 0; y < fb.height(); ++y)
    for (int x = 0; x < fb.width(); ++x) fb.setPixel(x, y, false);
  for (int y = 0; y < fb.height(); ++y)
    for (int x = 0; x < fb.width(); ++x) CHECK_FALSE(fb.getPixel(x, y));

  // The last row's last pixel is the one that used to run off the end.
  fb.clear(true);
  fb.setPixel(11, 3, false);
  CHECK_FALSE(fb.getPixel(11, 3));
  CHECK(fb.getPixel(10, 3));
}

TEST_CASE("non-positive dimensions give an inert empty buffer") {
  for (auto [w, h] : {std::pair{0, 4}, std::pair{4, 0}, std::pair{-8, 4}, std::pair{8, -4}}) {
    Framebuffer fb(w, h);
    CHECK(fb.width() == 0);
    CHECK(fb.height() == 0);
    CHECK(fb.physRowBytes() == 0);
    CHECK(fb.sizeBytes() == 0);
    // Every operation must be a safe no-op rather than touching storage.
    fb.clear(false);
    fb.setPixel(0, 0, false);
    fb.fillRect(-4, -4, 100, 100, false);
    CHECK(fb.getPixel(0, 0));
  }
}

// --- Rotation ---------------------------------------------------------------
//
// The byte-level proof that Rotation::Ccw matches rotate90CCW exactly lives in
// test_rotate.cpp, where the reference implementation is. These pin the class's
// own contract: which dimensions are logical, which are physical, and that
// Rotation::None is unchanged.

TEST_CASE("Rotation::None is what the default has always been") {
  // The simulator, every golden and tools/compare-design.py all work in logical
  // portrait space, so the unrotated buffer must stay byte-identical to what it
  // was before rotation existed -- not merely equivalent.
  Framebuffer implicitDefault(12, 20);
  Framebuffer explicitNone(12, 20, reader::Rotation::None);
  CHECK(implicitDefault.rotation() == reader::Rotation::None);
  CHECK(explicitNone.sizeBytes() == implicitDefault.sizeBytes());
  CHECK(explicitNone.physRowBytes() == implicitDefault.physRowBytes());
  // Physical and logical coincide, so physWidth/physHeight are not a second
  // thing to keep in step here.
  CHECK(implicitDefault.physWidth() == 12);
  CHECK(implicitDefault.physHeight() == 20);
  CHECK(implicitDefault.physRowBytes() == 2);  // ceil(12 / 8)

  for (int y = 0; y < 20; ++y)
    for (int x = 0; x < 12; ++x) {
      const bool white = ((x * 7 + y * 3) % 5) != 0;
      implicitDefault.setPixel(x, y, white);
      explicitNone.setPixel(x, y, white);
    }
  CHECK(std::memcmp(implicitDefault.data(), explicitNone.data(),
                    static_cast<size_t>(implicitDefault.sizeBytes())) == 0);
  // The MSB-first byte layout is the driver's contract; assert it directly
  // rather than through a comparison that two identical bugs would satisfy.
  implicitDefault.clear(true);
  implicitDefault.setPixel(0, 0, false);
  CHECK(implicitDefault.data()[0] == 0x7F);
}

TEST_CASE("a rotated framebuffer's stride comes from the physical width") {
  // The bug this exists to catch: deriving the stride from the LOGICAL width
  // sizes the store correctly by total bytes and indexes it wrongly by rows,
  // which draws a diagonally sheared screen -- plausible enough to chase for
  // hours. 528 logical wide, 792 physical wide: 99 bytes per row, not 66.
  Framebuffer rot(528, 792, reader::Rotation::Ccw);
  CHECK(rot.physRowBytes() == 99);
  CHECK(rot.physRowBytes() != (528 + 7) / 8);
  CHECK(rot.sizeBytes() == 99 * 528);

  // A logical size that is not a multiple of 8 in either axis: the stride is
  // rounded up from the physical width (the logical HEIGHT), and the slack byte
  // is on the physical row, not the logical one.
  Framebuffer ragged(13, 21, reader::Rotation::Ccw);
  CHECK(ragged.width() == 13);
  CHECK(ragged.height() == 21);
  CHECK(ragged.physWidth() == 21);
  CHECK(ragged.physHeight() == 13);
  CHECK(ragged.physRowBytes() == 3);  // ceil(21 / 8), not ceil(13 / 8) == 2
  CHECK(ragged.sizeBytes() == 3 * 13);
}

TEST_CASE("every in-bounds logical coordinate of a rotated buffer has backing storage") {
  // Same failure mode the non-multiple-of-8 case above guards for the unrotated
  // buffer, but the transform makes it a different piece of arithmetic. Under
  // ASAN this is what catches an off-by-one in either physical axis.
  for (auto [w, h] : {std::pair{13, 21}, std::pair{21, 13}, std::pair{1, 1}, std::pair{7, 100}}) {
    Framebuffer fb(w, h, reader::Rotation::Ccw);
    fb.clear(true);
    for (int y = 0; y < fb.height(); ++y)
      for (int x = 0; x < fb.width(); ++x) fb.setPixel(x, y, false);
    for (int y = 0; y < fb.height(); ++y)
      for (int x = 0; x < fb.width(); ++x) REQUIRE_FALSE(fb.getPixel(x, y));
    // And a single pixel is a single pixel: writing one must not smear across
    // the transform onto a neighbour.
    fb.clear(true);
    fb.setPixel(w - 1, h - 1, false);
    int ink = 0;
    for (int y = 0; y < fb.height(); ++y)
      for (int x = 0; x < fb.width(); ++x)
        if (!fb.getPixel(x, y)) ++ink;
    CHECK(ink == 1);
  }
}

TEST_CASE("fillRect and clear work in logical coordinates through the rotation") {
  // fillRect routes through setPixel, so it follows the transform for free --
  // this pins that nothing bypasses it, and that clear() still covers the whole
  // physical store rather than a logical-sized prefix of it.
  Framebuffer fb(16, 32, reader::Rotation::Ccw);
  fb.clear(false);
  for (int i = 0; i < fb.sizeBytes(); ++i) REQUIRE(fb.data()[i] == 0x00);
  fb.clear(true);
  for (int i = 0; i < fb.sizeBytes(); ++i) REQUIRE(fb.data()[i] == 0xFF);

  fb.fillRect(2, 3, 5, 7, false);
  for (int y = 0; y < fb.height(); ++y)
    for (int x = 0; x < fb.width(); ++x) {
      const bool inRect = x >= 2 && x < 7 && y >= 3 && y < 10;
      REQUIRE(fb.getPixel(x, y) == !inRect);
    }
  // Clipping is on the LOGICAL bounds, same as unrotated.
  fb.clear(true);
  fb.fillRect(14, 30, 100, 100, false);
  CHECK_FALSE(fb.getPixel(15, 31));
  CHECK(fb.getPixel(13, 30));
}

TEST_CASE("a rotated non-positive dimension is still an inert empty buffer") {
  for (auto [w, h] : {std::pair{0, 4}, std::pair{4, 0}, std::pair{-8, 4}}) {
    Framebuffer fb(w, h, reader::Rotation::Ccw);
    CHECK(fb.width() == 0);
    CHECK(fb.height() == 0);
    CHECK(fb.physWidth() == 0);
    CHECK(fb.physHeight() == 0);
    CHECK(fb.physRowBytes() == 0);
    CHECK(fb.sizeBytes() == 0);
    fb.clear(false);
    fb.setPixel(0, 0, false);
    fb.fillRect(-4, -4, 100, 100, false);
    CHECK(fb.getPixel(0, 0));
  }
}
