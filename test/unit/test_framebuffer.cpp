#include <utility>

#include "doctest.h"
#include "reader/framebuffer.h"

using reader::Framebuffer;

TEST_CASE("framebuffer starts white and stores pixels MSB-first") {
  Framebuffer fb(16, 4);            // 16 px wide -> 2 bytes per row
  CHECK(fb.width() == 16);
  CHECK(fb.height() == 4);
  CHECK(fb.rowBytes() == 2);
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
  CHECK(fb.rowBytes() == 2);  // ceil(12 / 8)
  CHECK(fb.sizeBytes() == fb.rowBytes() * fb.height());

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
    CHECK(fb.rowBytes() == 0);
    CHECK(fb.sizeBytes() == 0);
    // Every operation must be a safe no-op rather than touching storage.
    fb.clear(false);
    fb.setPixel(0, 0, false);
    fb.fillRect(-4, -4, 100, 100, false);
    CHECK(fb.getPixel(0, 0));
  }
}
