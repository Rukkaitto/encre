#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/font.h"
#include "reader/framebuffer.h"
#include "reader/png.h"
#include "reader/text.h"

static std::vector<uint8_t> slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  REQUIRE(f.good());
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

TEST_CASE("text render matches golden") {
  auto bytes = slurp(std::string(ASSETS_DIR) + "/built/literata_18.rfnt");
  reader::Font font;
  REQUIRE(font.load(bytes.data(), bytes.size()));

  reader::Framebuffer fb(480, 64);
  reader::drawText(fb, font, 12, 40, "Middlemarch — 6% · page 53");

  const std::string golden = std::string(GOLDEN_DIR) + "/text_sample.png";
  if (!std::ifstream(golden).good()) {
    // First run: write the candidate for human review, then fail loudly.
    reader::writePng(fb, (std::string(BUILD_DIR) + "/text_sample_candidate.png").c_str());
    FAIL("golden missing - inspect build/text_sample_candidate.png, then copy to test/golden/text_sample.png");
  }
  CHECK(reader::comparePng(fb, golden.c_str()));
}
