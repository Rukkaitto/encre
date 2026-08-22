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
#include "reader/font_manifest.h"
#include "reader/fontset.h"

namespace ramp {

inline std::vector<uint8_t> slurpAsset(const std::string& name) {
  const std::string path = std::string(ASSETS_DIR) + "/built/" + name;
  std::ifstream f(path, std::ios::binary);
  REQUIRE_MESSAGE(f.good(), "cannot read " << path);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

// Which asset backs which role is the manifest's (reader/font_manifest.h): one
// list, expanded here over file loads exactly as the simulator does, and in the
// shell over the embedded arrays. The blob members are named blob_<Role>, so a
// test that deliberately mis-binds one (there is one, and it must keep failing
// to load) reads as obviously deliberate.
struct Ramp {
#define ENCRE_ROLE_BLOB(role, stem) \
  std::vector<uint8_t> blob_##role = slurpAsset(#stem ".rfnt");
  READER_FONT_RAMP(ENCRE_ROLE_BLOB)
#undef ENCRE_ROLE_BLOB
  reader::FontSet fonts;

  Ramp() {
    REQUIRE(load(fonts));
    REQUIRE(fonts.ready());
  }

  // Fills `set` with the whole ramp. Exposed so a test can build a second,
  // deliberately different set (a band whose label role is a bigger face, say)
  // without repeating the list.
  bool load(reader::FontSet& set) const {
    return true
#define ENCRE_LOAD_ROLE(role, stem) \
  && set.load(reader::Role::role, blob_##role.data(), blob_##role.size())
        READER_FONT_RAMP(ENCRE_LOAD_ROLE)
#undef ENCRE_LOAD_ROLE
        ;
  }
};

}  // namespace ramp
