#include <string>

#include "doctest.h"
#include "golden.h"
#include "reader/font.h"
#include "reader/framebuffer.h"
#include "reader/text.h"

TEST_CASE("text render matches golden") {
  auto bytes = golden::slurp(std::string(ASSETS_DIR) + "/built/literata_18.rfnt");
  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));

  reader::Framebuffer fb(480, 64);
  reader::drawText(fb, font, 12, 40, "Middlemarch — 6% · page 53");

  golden::checkGolden(fb, "text_sample");
}
