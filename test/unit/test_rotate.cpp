#include "doctest.h"
#include "reader/framebuffer.h"
#include "reader/rotate.h"

TEST_CASE("rotate90CW maps portrait to landscape") {
  reader::Framebuffer portrait(8, 16);   // w=8, h=16
  reader::Framebuffer landscape(16, 8);  // w=16, h=8
  portrait.setPixel(0, 0, false);        // top-left of portrait
  reader::rotate90CW(portrait, landscape);
  // Portrait top-left lands at landscape top-right.
  CHECK_FALSE(landscape.getPixel(15, 0));
  CHECK(landscape.getPixel(0, 0));
}
