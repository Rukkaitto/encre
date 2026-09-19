#include <pthread.h>

// <cstdint> for uintptr_t. libc++ satisfies this transitively and libstdc++ does
// not, so it compiled on macOS and failed on the first Linux build.
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "doctest.h"
#include "stack_ceiling.h"
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


// --- How much STACK the inflate wants -----------------------------------------
//
// This test exists because the device found it first, and nothing on the desktop
// could have. Opening any book was a stack-protection fault in loopTask:
// stb_image's inflate wants 6,608 bytes in ONE FRAME -- the compiler inlines
// stbi__parse_zlib, stbi__compute_huffman_codes and stbi__zbuild_huffman together,
// so all three stbi__zhuffman tables share a frame -- against Arduino's default
// 8,184-byte loop stack with ~1.5 KB already spent above the call.
//
// A desktop main thread has 8 MB, so every test above passed. The project's answer
// to "shell/ has no test harness" has been to move logic into core/ where a fake
// can reach it; a stack budget cannot be moved, so it has to be MEASURED instead.
//
// The technique is FreeRTOS's own: give the thread a stack we own, fill it with a
// pattern, and see how much of the pattern survives. Conservative in the safe
// direction -- if the inflate happens to write the pattern's own bytes, this
// under-reports usage, so the assertion has room rather than being tight.
namespace {

constexpr size_t kProbeStack = 512 * 1024;  // generous; the point is to measure
constexpr unsigned char kFill = 0xA5;

struct Probe {
  bool ok = false;
  std::string out;
};

void* runInflate(void* arg) {
  Probe* p = static_cast<Probe*>(arg);
  p->out.assign(std::strlen(kSentenceText), '\0');
  p->ok = reader::inflateRaw(bytes(kSentence, sizeof(kSentence)), p->out);
  return nullptr;
}

}  // namespace

TEST_CASE("INFLATE FITS THE DEVICE'S STACK, measured rather than assumed") {
  std::vector<unsigned char> stack(kProbeStack, kFill);

  pthread_attr_t attr;
  REQUIRE(pthread_attr_init(&attr) == 0);
  // Page-align the base: pthread_attr_setstack requires it on both hosts, and a
  // misaligned base is an EINVAL that would look like the API not working.
  const size_t page = 16u * 1024u;
  unsigned char* base = stack.data();
  const size_t shift = (page - (reinterpret_cast<uintptr_t>(base) % page)) % page;
  base += shift;
  const size_t usable = (kProbeStack - shift) & ~(page - 1);
  REQUIRE(pthread_attr_setstack(&attr, base, usable) == 0);

  Probe probe;
  pthread_t tid{};
  REQUIRE(pthread_create(&tid, &attr, runInflate, &probe) == 0);
  REQUIRE(pthread_join(tid, nullptr) == 0);
  pthread_attr_destroy(&attr);

  REQUIRE(probe.ok);
  // std::string on both sides: doctest stringifies a const char* as a POINTER
  // (CLAUDE.md records this for fs_contract.h), so a failure here would print an
  // address instead of the text that differed.
  CHECK(probe.out == std::string(kSentenceText));

  // The stack grew DOWN from the top, so what is still the fill pattern at the
  // bottom is what was never touched.
  size_t untouched = 0;
  while (untouched < usable && base[untouched] == kFill) ++untouched;
  const size_t used = usable - untouched;
  CAPTURE(used);

  // The frame the panic reported is 6,608 bytes, plus the chain into it. Asserted
  // as a CEILING, so this fails if a vendored-library bump or a compiler change
  // grows the appetite -- which is the thing that would panic the device again.
  //
  // The device's loopTask is 16 KB (SET_LOOP_TASK_STACK_SIZE in shell/src/main.cpp)
  // and spends ~1.5 KB above this call, so 10 KB is the budget this may not exceed
  // while leaving room for the layers above it.
  //
  // Per HOST compiler: clang measures 7,348 here and x86-64 gcc 12,212, for the
  // same code, and an AddressSanitizer build of that same clang 9,808. See
  // stack_ceiling.h for why that is three numbers and not one.
  CHECK(used > 4096);   // the measurement is real, not a pattern-scan artifact
  CHECK(used <= stackceil::pick(/*clang=*/10240, /*gcc=*/16384, /*asan=*/13312));
}
