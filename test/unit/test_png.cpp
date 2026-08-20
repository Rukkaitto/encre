#include <cstdio>
#include <string>

#include "doctest.h"
#include "reader/framebuffer.h"
#include "reader/png.h"

TEST_CASE("writePng emits a decodable 1-bit image") {
  reader::Framebuffer fb(16, 8);
  fb.fillRect(0, 0, 8, 8, false);  // left half black
  const std::string path = std::string(BUILD_DIR) + "/test_png_out.png";
  REQUIRE(reader::writePng(fb, path.c_str()));

  int w = 0, h = 0;
  REQUIRE(reader::comparePng(fb, path.c_str(), &w, &h));
  CHECK(w == 16);
  CHECK(h == 8);
  std::remove(path.c_str());
}
