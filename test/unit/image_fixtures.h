#pragma once
// A ByteSource over a std::string, and the stb_image oracle the decoder tests
// compare against.
//
// THE ORACLE IS stb_image, ALREADY VENDORED AND ALREADY DESKTOP-ONLY. This is the
// move that validated inflate_stream against the one-shot decoder -- 216 entry
// passes, zero disagreements -- and it is available here for the same reason: a
// decoder is only trustworthy against a decoder nobody in this repo wrote.
#include <cstdint>
#include <string>
#include <vector>

#include "reader/inflate_stream.h"

namespace imgfix {

// Serves `bytes` in chunks of at most `grain`. GRAIN 1 IS THE LOAD-BEARING CASE:
// a source that satisfies every read hides every resumption bug there is, which
// is exactly what test_inflate_stream.cpp found.
class StringSource : public reader::ByteSource {
 public:
  StringSource(std::string bytes, size_t grain = 4096)
      : bytes_(std::move(bytes)), grain_(grain == 0 ? 1 : grain) {}

  size_t read(void* dst, size_t want) override {
    if (want > grain_) want = grain_;
    const size_t left = bytes_.size() - at_;
    const size_t n = want < left ? want : left;
    if (n != 0) __builtin_memcpy(dst, bytes_.data() + at_, n);
    at_ += n;
    return n;
  }

  void rewind() { at_ = 0; }

 private:
  std::string bytes_;
  size_t grain_;
  size_t at_ = 0;
};

// Decoded by stb_image to 8-bit grey. Empty `pixels` means stb refused it.
struct Oracle {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> pixels;  // width*height, row-major, 0 = black
};

Oracle decodeWithStb(const std::string& bytes);

// Reads a file from test/unit/fixtures/images/. Empty on failure.
std::string loadFixture(const char* name);

}  // namespace imgfix
