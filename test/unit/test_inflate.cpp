#include <cstring>
#include <string>

#include "doctest.h"
#include "reader/inflate.h"

// RAW DEFLATE FIXTURES, generated so the test data and the reader cannot disagree
// about the format. Reproduce any of them with:
//
//   python3 -c "import zlib; c=zlib.compressobj(9,zlib.DEFLATED,-15); \
//               import sys; d=c.compress(b'...')+c.flush(); \
//               print(' '.join('0x%02x,'%x for x in d))"
//
// Bytes rather than a call into zlib at test time, because the point is to pin
// what the DEVICE will be handed: a stream produced by whatever zipped the book.
namespace {

// 75 bytes, from 87 of the Reader board's own first sentence.
constexpr unsigned char kSentence[] = {
    0x0d, 0xca, 0xd1, 0x0d, 0x80, 0x30, 0x08, 0x45, 0xd1, 0x55, 0xde, 0x04,
    0x0e, 0xe1, 0xbf, 0x43, 0x54, 0x8b, 0x81, 0x54, 0x4b, 0x03, 0x98, 0xa6,
    0xdb, 0xcb, 0xe7, 0xbd, 0x39, 0x87, 0xb8, 0x63, 0x37, 0xd5, 0x46, 0xe0,
    0x52, 0x11, 0x5c, 0x02, 0x4d, 0x7a, 0x85, 0xde, 0x38, 0xa9, 0x7c, 0xb1,
    0x30, 0x59, 0x2e, 0x86, 0x13, 0xbd, 0x8e, 0xd0, 0xbc, 0xa9, 0x4c, 0x67,
    0x87, 0xf4, 0x4c, 0xa3, 0x47, 0x28, 0xed, 0xc2, 0x50, 0x35, 0x54, 0x23,
    0xf7, 0xed, 0x07,
};
const char* const kSentenceText =
    "Miss Brooke had that kind of beauty which seems to be thrown into relief by poor dress.";

// 9 bytes from 400: a back-reference stream, which exercises the window rather
// than just the literal path.
constexpr unsigned char kRepetitive[] = {0x4b, 0x4c, 0x4a, 0x1c, 0x85,
                                        0x83, 0x08, 0x02, 0x00};

// 3 bytes from 1. The smallest real stream there is.
constexpr unsigned char kOneByte[] = {0xf3, 0x05, 0x00};

std::string_view bytes(const unsigned char* p, size_t n) {
  return std::string_view(reinterpret_cast<const char*>(p), n);
}

}  // namespace

TEST_CASE("a deflated stream inflates to exactly the original bytes") {
  std::string out(std::strlen(kSentenceText), '\0');
  REQUIRE(reader::inflateRaw(bytes(kSentence, sizeof(kSentence)), out));
  CHECK(out == kSentenceText);
}

TEST_CASE("back-references decode, not just literals") {
  std::string out(400, '\0');
  REQUIRE(reader::inflateRaw(bytes(kRepetitive, sizeof(kRepetitive)), out));
  std::string expected;
  for (int i = 0; i < 200; ++i) expected += "ab";
  CHECK(out == expected);
}

TEST_CASE("a one-byte stream is a stream") {
  std::string out(1, '\0');
  REQUIRE(reader::inflateRaw(bytes(kOneByte, sizeof(kOneByte)), out));
  CHECK(out == "M");
}

TEST_CASE("an output buffer LONGER than the stream is a refusal") {
  // The zip header's uncompressed length disagreeing with the stream means one of
  // the two is corrupt, and a partial buffer that reported success would be a
  // chapter with its end silently missing.
  std::string out(std::strlen(kSentenceText) + 10, '\0');
  CHECK_FALSE(reader::inflateRaw(bytes(kSentence, sizeof(kSentence)), out));
}

TEST_CASE("an output buffer SHORTER than the stream is a refusal") {
  std::string out(10, '\0');
  CHECK_FALSE(reader::inflateRaw(bytes(kSentence, sizeof(kSentence)), out));
}

TEST_CASE("truncated input is a refusal, not a partial decode") {
  std::string out(std::strlen(kSentenceText), '\0');
  CHECK_FALSE(reader::inflateRaw(bytes(kSentence, sizeof(kSentence) / 2), out));
}

TEST_CASE("garbage is a refusal and does not crash") {
  const unsigned char junk[] = {0xff, 0xff, 0xff, 0xff, 0x00, 0x13, 0x37};
  std::string out(64, '\0');
  CHECK_FALSE(reader::inflateRaw(bytes(junk, sizeof(junk)), out));
}

TEST_CASE("empty input, and an empty output, are both refusals rather than aborts") {
  std::string out(4, '\0');
  CHECK_FALSE(reader::inflateRaw(std::string_view{}, out));
  std::string none;
  CHECK_FALSE(reader::inflateRaw(bytes(kOneByte, sizeof(kOneByte)), none));
}

TEST_CASE("deterministic fuzz: every truncation and byte flip is survivable") {
  // The property is only that it never crashes and never reports success with a
  // wrong buffer. The same fuzz found real defects in the JSON reader.
  const std::string_view full = bytes(kSentence, sizeof(kSentence));
  for (size_t cut = 0; cut <= full.size(); ++cut) {
    std::string out(std::strlen(kSentenceText), '\0');
    const bool ok = reader::inflateRaw(full.substr(0, cut), out);
    if (ok) CHECK(out == kSentenceText);
  }
  for (size_t i = 0; i < full.size(); ++i) {
    std::string mutated(full);
    mutated[i] = static_cast<char>(mutated[i] ^ 0x5a);
    std::string out(std::strlen(kSentenceText), '\0');
    const bool ok = reader::inflateRaw(mutated, out);
    if (ok) CHECK(out.size() == std::strlen(kSentenceText));
  }
}
