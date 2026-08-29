#include "image_fixtures.h"

#include <fstream>
#include <sstream>

#include "doctest.h"
// png.cpp already defines STB_IMAGE_IMPLEMENTATION for the desktop build, so this
// translation unit takes the header only -- two implementations would be a
// duplicate-symbol link error.
#include "stb_image.h"

namespace imgfix {

Oracle decodeWithStb(const std::string& bytes) {
  Oracle out;
  int w = 0, h = 0, comp = 0;
  unsigned char* px = stbi_load_from_memory(
      reinterpret_cast<const unsigned char*>(bytes.data()),
      static_cast<int>(bytes.size()), &w, &h, &comp, 1);
  if (px == nullptr) return out;
  out.width = w;
  out.height = h;
  out.pixels.assign(px, px + static_cast<size_t>(w) * h);
  stbi_image_free(px);
  return out;
}

std::string loadFixture(const char* name) {
  std::string path = std::string(TEST_FIXTURE_DIR) + "/images/" + name;
  std::ifstream in(path, std::ios::binary);
  // golden::slurp (golden.h) fails exactly this loudly for exactly this reason: a
  // missing fixture must not read back as an empty image, which is indistinguishable
  // from every decoder under test agreeing to refuse it.
  REQUIRE_MESSAGE(in.good(), "cannot read " << path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace imgfix
