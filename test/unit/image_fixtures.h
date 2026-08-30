#pragma once
// The stb_image oracle the decoder tests compare against, and the fixture images
// on disk it and they both read.
//
// THE ORACLE IS stb_image, ALREADY VENDORED AND ALREADY DESKTOP-ONLY. This is the
// move that validated inflate_stream against the one-shot decoder -- 216 entry
// passes, zero disagreements -- and it is available here for the same reason: a
// decoder is only trustworthy against a decoder nobody in this repo wrote.
//
// A grain-limited ByteSource to feed the decoders under test is grained_source.h,
// not here: it is not image-specific, and three of its four callers -- the
// DEFLATE stream, the document builder, the XML tokenizer -- have nothing to do
// with images.
#include <cstdint>
#include <string>
#include <vector>

namespace imgfix {

// Decoded by stb_image to 8-bit grey. Empty `pixels` means stb refused it.
struct Oracle {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> pixels;  // width*height, row-major, 0 = black
};

Oracle decodeWithStb(const std::string& bytes);

// Reads a file from test/unit/fixtures/images/. REQUIREs the read succeeded --
// a typo'd fixture name must fail loudly here, not read back as an empty image
// that then reads as both the oracle and the decoder under test having agreed
// to refuse it.
std::string loadFixture(const char* name);

}  // namespace imgfix
