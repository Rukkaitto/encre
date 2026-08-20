#pragma once
// The real type ramp, loaded from the built assets, for the tests that need a
// FontSet rather than a synthetic face. Three test files used to keep their own
// copy of this list under single-letter names, so adding a role meant editing
// the same seven lines in three places and the names said nothing about which
// face was which. The blobs are named for their role here on purpose: a test
// that deliberately mis-binds one (there is one, and it must keep failing to
// load) should read as obviously deliberate.
#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/fontset.h"

namespace ramp {

inline std::vector<uint8_t> slurpAsset(const std::string& name) {
  const std::string path = std::string(ASSETS_DIR) + "/built/" + name;
  std::ifstream f(path, std::ios::binary);
  REQUIRE_MESSAGE(f.good(), "cannot read " << path);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

struct Ramp {
  std::vector<uint8_t> meta400 = slurpAsset("spacegrotesk_400_10pt.rfnt");
  std::vector<uint8_t> label500 = slurpAsset("spacegrotesk_500_11pt.rfnt");
  std::vector<uint8_t> value700 = slurpAsset("spacegrotesk_700_12pt.rfnt");
  std::vector<uint8_t> body400 = slurpAsset("spacegrotesk_400_14pt.rfnt");
  std::vector<uint8_t> body500 = slurpAsset("spacegrotesk_500_14pt.rfnt");
  std::vector<uint8_t> title700 = slurpAsset("spacegrotesk_700_20pt.rfnt");
  std::vector<uint8_t> display700 = slurpAsset("spacegrotesk_700_32pt.rfnt");
  reader::FontSet fonts;

  Ramp() {
    REQUIRE(load(fonts));
    REQUIRE(fonts.ready());
  }

  // Fills `set` with the whole ramp. Exposed so a test can build a second,
  // deliberately different set (a band whose label role is a bigger face, say)
  // without repeating the list.
  bool load(reader::FontSet& set) const {
    return set.load(reader::Role::Meta400, meta400.data(), meta400.size()) &&
           set.load(reader::Role::Label500, label500.data(), label500.size()) &&
           set.load(reader::Role::Value700, value700.data(), value700.size()) &&
           set.load(reader::Role::Body400, body400.data(), body400.size()) &&
           set.load(reader::Role::Body500, body500.data(), body500.size()) &&
           set.load(reader::Role::Title700, title700.data(), title700.size()) &&
           set.load(reader::Role::Display700, display700.data(), display700.size());
  }
};

}  // namespace ramp
