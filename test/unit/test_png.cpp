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

TEST_CASE("diffPng distinguishes the ways a comparison can fail") {
  using Status = reader::PngDiff::Status;
  const std::string path = std::string(BUILD_DIR) + "/test_diff_png.png";

  reader::Framebuffer fb(16, 8);
  fb.fillRect(0, 0, 8, 8, false);
  REQUIRE(reader::writePng(fb, path.c_str()));

  SUBCASE("match") {
    const auto d = reader::diffPng(fb, path.c_str());
    CHECK(d.status == Status::kMatch);
    CHECK(d.ok());
    CHECK(d.diffPixels == 0);
    CHECK(d.width == 16);
    CHECK(d.height == 8);
  }

  SUBCASE("missing file is not confused with a mismatch") {
    const auto d = reader::diffPng(fb, (path + ".nope").c_str());
    CHECK(d.status == Status::kDecodeFailed);
    CHECK(d.width == 0);
  }

  SUBCASE("size mismatch reports the decoded dimensions") {
    reader::Framebuffer other(16, 16);
    const auto d = reader::diffPng(other, path.c_str());
    CHECK(d.status == Status::kSizeMismatch);
    CHECK(d.width == 16);
    CHECK(d.height == 8);
  }

  SUBCASE("pixel mismatch counts and locates the difference") {
    reader::Framebuffer changed(16, 8);
    changed.fillRect(0, 0, 8, 8, false);
    changed.setPixel(3, 2, true);
    changed.setPixel(9, 5, false);
    const auto d = reader::diffPng(changed, path.c_str());
    CHECK(d.status == Status::kPixelMismatch);
    CHECK(d.diffPixels == 2);
    CHECK(d.firstDiffX == 3);  // raster order: (3,2) before (9,5)
    CHECK(d.firstDiffY == 2);
  }

  std::remove(path.c_str());
}
