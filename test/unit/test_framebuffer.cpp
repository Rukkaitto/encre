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
