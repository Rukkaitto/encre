#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/fontset.h"

static std::vector<uint8_t> slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  REQUIRE(f.good());
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

TEST_CASE("FontSet exposes one loaded face per role and reports readiness") {
  const std::string dir = std::string(ASSETS_DIR) + "/built/";
  auto meta  = slurp(dir + "spacegrotesk_500_12.rfnt");
  auto label = slurp(dir + "spacegrotesk_500_13.rfnt");
  auto value = slurp(dir + "spacegrotesk_700_14.rfnt");
  auto body  = slurp(dir + "spacegrotesk_500_17.rfnt");
  auto title = slurp(dir + "spacegrotesk_700_24.rfnt");
  auto display = slurp(dir + "spacegrotesk_700_44.rfnt");

  reader::FontSet fonts;
  CHECK_FALSE(fonts.ready());
  REQUIRE(fonts.load(reader::Role::Meta,  meta.data(),  meta.size()));
  REQUIRE(fonts.load(reader::Role::Label, label.data(), label.size()));
  REQUIRE(fonts.load(reader::Role::Value, value.data(), value.size()));
  REQUIRE(fonts.load(reader::Role::Body,  body.data(),  body.size()));
  REQUIRE(fonts.load(reader::Role::Title, title.data(), title.size()));
  REQUIRE(fonts.load(reader::Role::Display, display.data(), display.size()));
  CHECK(fonts.ready());

  // The ramp must actually be a ramp, or the design's hierarchy is lost. Display
  // tops it: the board's percentage is the dominant number on the screen, so it
  // has to out-measure the title rather than tie with it.
  CHECK(fonts[reader::Role::Display].ascent() > fonts[reader::Role::Title].ascent());
  CHECK(fonts[reader::Role::Title].ascent() > fonts[reader::Role::Body].ascent());
  CHECK(fonts[reader::Role::Body].ascent()  > fonts[reader::Role::Value].ascent());
  CHECK(fonts[reader::Role::Label].ascent() >= fonts[reader::Role::Meta].ascent());
}

TEST_CASE("a rejected blob leaves the set not ready") {
  reader::FontSet fonts;
  const uint8_t junk[16] = {0};
  CHECK_FALSE(fonts.load(reader::Role::Body, junk, sizeof junk));
  CHECK_FALSE(fonts.ready());
}
