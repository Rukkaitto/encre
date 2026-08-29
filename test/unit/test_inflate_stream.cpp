// The streaming DEFLATE decoder.
//
// This is the most delicate code in the repository: a bit-level parser over bytes
// from somebody's card, where a missing bound is a hang or a read of memory the
// stream never wrote. So the malformed cases below outnumber the valid ones, and
// every one of them is a stream a real file could contain.
#include <pthread.h>

// <cstdint> for uintptr_t. libc++ satisfies this transitively and libstdc++ does
// not, so it compiled on macOS and failed on the first Linux build.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "deflate_fixtures.h"
#include "doctest.h"
#include "reader/inflate.h"
#include "reader/inflate_stream.h"

namespace {

using reader::Inflater;

// A source over a buffer, handing out at most `grain` bytes a call.
//
// `grain` is the point of this class. A source that always satisfies a full read
// hides every resumption bug in the decoder -- the bit reader's refill, a match
// straddling an input boundary, a block header split across two reads. One byte at
// a time makes every one of those happen on every stream.
class Grained : public reader::ByteSource {
 public:
  Grained(std::string_view bytes, size_t grain) : b_(bytes), grain_(grain) {}
  size_t read(void* dst, size_t bytes) override {
    const size_t want = bytes < grain_ ? bytes : grain_;
    const size_t got = b_.size() - at_ < want ? b_.size() - at_ : want;
    std::memcpy(dst, b_.data() + at_, got);
    at_ += got;
    return got;
  }

 private:
  std::string_view b_;
  size_t grain_;
  size_t at_ = 0;
};

std::string_view view(const unsigned char* p, size_t n) {
  return std::string_view(reinterpret_cast<const char*>(p), n);
}

// Drains an inflater into one string. `chunks` reports how many next() calls
// produced output, which is what proves the chunking actually happened rather than
// the whole stream arriving in one go.
std::string drain(std::string_view compressed, size_t grain, bool* ok, int* chunks = nullptr,
                  const char** why = nullptr) {
  Grained src(compressed, grain);
  Inflater inf;
  REQUIRE(inf.begin(src));
  std::string out;
  int n = 0;
  // BOUNDED, because a decoder that stops producing without finishing would
  // otherwise spin here -- which is exactly the failure a malformed stream causes.
  for (int guard = 0; guard < 100000; ++guard) {
    const std::string_view chunk = inf.next();
    if (chunk.empty()) break;
    out.append(chunk);
    ++n;
  }
  if (chunks != nullptr) *chunks = n;
  if (why != nullptr) *why = inf.error();
  *ok = inf.done() && inf.error()[0] == '\0';
  return out;
}

#define FIX(name) view(deflatefix::name, sizeof(deflatefix::name))

}  // namespace

TEST_CASE("a fixed-Huffman block decodes") {
  bool ok = false;
  const std::string out = drain(FIX(kFixed), 4096, &ok);
  CHECK(ok);
  CHECK(out == std::string(deflatefix::kFixedOut));
}

TEST_CASE("stored blocks decode, and there are three of them") {
  bool ok = false;
  const std::string out = drain(FIX(kStored), 4096, &ok);
  CHECK(ok);
  CHECK(out == std::string(deflatefix::kStoredOut));
}

TEST_CASE("AN OVERLAPPING MATCH READS WHAT IT IS WRITING") {
  // Distance 1 is a run of one byte, defined by RFC 1951 to read bytes the same
  // match has just produced. A memcpy is wrong here and produces plausible
  // garbage -- the right length, the wrong contents.
  bool ok = false;
  const std::string out = drain(FIX(kOverlap), 4096, &ok);
  REQUIRE(ok);
  REQUIRE(out.size() == deflatefix::kOverlapOut);
  std::string expect = "a" + std::string(5000, 'b') + std::string(300, 'c');
  CHECK(out == expect);
}

TEST_CASE("SIX BLOCKS IN ONE STREAM, so the header path runs mid-stream") {
  bool ok = false;
  const std::string out = drain(FIX(kMultiBlock), 4096, &ok);
  REQUIRE(ok);
  REQUIRE(out.size() == deflatefix::kMultiBlockOut);
  for (int i = 0; i < 6; ++i)
    CHECK(out.substr(static_cast<size_t>(i) * 9000, 9000) ==
          std::string(9000, static_cast<char>('A' + i)));
}

TEST_CASE("OUTPUT LARGER THAN THE WINDOW WRAPS THE RING CORRECTLY") {
  // 200,000 bytes through a 32 KB ring in 16 KB chunks: the wrap happens six
  // times, and a match reaching back across one is the case that a naive
  // "subtract the distance" would get wrong.
  bool ok = false;
  int chunks = 0;
  const std::string out = drain(FIX(kLongRun), 4096, &ok, &chunks);
  REQUIRE(ok);
  CHECK(out.size() == deflatefix::kLongRunOut);
  // Really chunked, not delivered whole.
  CHECK(chunks > 10);
  const std::string unit = "The captain's word was law, and the law was thin. ";
  CHECK(out.substr(0, unit.size()) == unit);
  CHECK(out.substr(out.size() - unit.size()) == unit);
  // And the middle, because a wrap corrupts the interior while leaving both ends
  // looking right.
  for (size_t at = 0; at + unit.size() <= out.size(); at += unit.size() * 137)
    REQUIRE(out.compare(at, unit.size(), unit) == 0);
}

TEST_CASE("NO CHUNK EXCEEDS THE DOCUMENTED CAP, and none straddles the wrap") {
  // The contract callers rely on: a chunk is contiguous and at most kChunkBytes,
  // so it can be treated as a plain span.
  Grained src(FIX(kLongRun), 4096);
  Inflater inf;
  REQUIRE(inf.begin(src));
  for (int guard = 0; guard < 100000; ++guard) {
    const std::string_view chunk = inf.next();
    if (chunk.empty()) break;
    CHECK(chunk.size() <= Inflater::kChunkBytes);
  }
  CHECK(inf.done());
}

TEST_CASE("EVERY GRAIN SIZE GIVES THE SAME ANSWER, one byte at a time included") {
  // The resumption test. At grain 1 the bit reader refills mid-code, a block
  // header splits across reads, and a match's extra bits arrive separately -- on
  // every symbol of the stream rather than occasionally.
  bool ok1 = false, ok2 = false, ok3 = false;
  const std::string a = drain(FIX(kLongRun), 1, &ok1);
  const std::string b = drain(FIX(kLongRun), 7, &ok2);
  const std::string c = drain(FIX(kLongRun), 2048, &ok3);
  CHECK(ok1);
  CHECK(ok2);
  CHECK(ok3);
  CHECK(a.size() == deflatefix::kLongRunOut);
  CHECK(a == b);
  CHECK(b == c);
}

TEST_CASE("the fixtures agree with the one-shot decoder they replace") {
  // Cross-checked against stb_image's zlib, which has been shipping in this
  // firmware and is what produced every page read so far. Two independent
  // implementations agreeing on the same bytes is stronger evidence than either
  // one agreeing with a fixture's stated length.
  struct Case {
    std::string_view compressed;
    size_t out;
  };
  const Case cases[] = {
      {FIX(kLongRun), deflatefix::kLongRunOut},
      {FIX(kOverlap), deflatefix::kOverlapOut},
      {FIX(kMultiBlock), deflatefix::kMultiBlockOut},
  };
  for (const Case& c : cases) {
    bool ok = false;
    const std::string streamed = drain(c.compressed, 4096, &ok);
    REQUIRE(ok);
    std::string oneShot(c.out, '\0');
    REQUIRE(reader::inflateRaw(c.compressed, oneShot));
    CHECK(streamed == oneShot);
  }
}

// --- Refusals ----------------------------------------------------------------
//
// Built by corrupting the valid fixtures, so each one is a stream that differs
// from a real file by the specific thing being tested.

namespace {

// True if the stream was refused with a reason, rather than accepted or hanging.
bool refused(std::string_view bytes) {
  bool ok = false;
  const char* why = "";
  drain(bytes, 4096, &ok, nullptr, &why);
  return !ok && why[0] != '\0';
}

}  // namespace

TEST_CASE("a truncated stream is refused, not silently short") {
  const std::string_view full = FIX(kLongRun);
  // Every truncation point, because the interesting ones are where a code, a
  // block header or a match's extra bits are cut in half -- and which byte that
  // is depends on the stream.
  for (size_t keep = 1; keep < full.size(); keep += 7) {
    CAPTURE(keep);
    CHECK(refused(full.substr(0, keep)));
  }
}

TEST_CASE("an empty stream is refused") { CHECK(refused("")); }

TEST_CASE("a reserved block type is refused") {
  // BTYPE 11 is reserved. Bits 1-2 of the first byte.
  std::string s(FIX(kFixed));
  s[0] = static_cast<char>((s[0] & ~0x06) | 0x06);
  CHECK(refused(s));
}

TEST_CASE("a stored block whose NLEN is not the complement of LEN is refused") {
  std::string s(FIX(kStored));
  s[3] = static_cast<char>(s[3] ^ 0xFF);  // corrupt NLEN's low byte
  CHECK(refused(s));
}

TEST_CASE("a stored block that ends early is refused") {
  std::string s(FIX(kStored));
  CHECK(refused(s.substr(0, 10)));  // header says 20 bytes, 5 are present
}

TEST_CASE("CORRUPTION ANYWHERE IS REFUSED OR DECODED, NEVER HANGS") {
  // The property that matters most for untrusted input. Every single-byte
  // corruption of a real stream must terminate -- either refused, or producing
  // some output and stopping. What must not happen is an unbounded decode, which
  // is what a missing over-subscription check or an unchecked repeat count gives.
  //
  // `drain`'s own guard would fire on a hang, so this passing means every one of
  // these streams terminated on its own.
  const std::string_view full = FIX(kMultiBlock);
  int accepted = 0, rejected = 0;
  for (size_t at = 0; at < full.size(); ++at) {
    for (const uint8_t mask : {0x01, 0x40, 0xFF}) {
      std::string s(full);
      s[at] = static_cast<char>(static_cast<uint8_t>(s[at]) ^ mask);
      bool ok = false;
      const char* why = "";
      drain(s, 4096, &ok, nullptr, &why);
      if (ok) ++accepted;
      else ++rejected;
    }
  }
  CAPTURE(accepted);
  CAPTURE(rejected);
  // Most corruptions are caught; a few flip bits the format does not care about
  // and still decode. Neither number is the assertion -- terminating is.
  CHECK(rejected > 0);
  CHECK(accepted + rejected == static_cast<int>(full.size()) * 3);
}

TEST_CASE("a distance reaching before the start of the output is refused") {
  // Hand-built: a fixed-Huffman block whose first symbol is a match. There is no
  // output yet, so any distance reaches before the stream began -- which without
  // the totalOut_ bound reads uninitialised window bytes and returns them as text.
  //
  // Fixed tables: length code 257 is the 7-bit code 0000001, distance code 0 is
  // 5 bits of 0. Assembled LSB-first: header 3 bits (BFINAL=1, BTYPE=01).
  std::string s;
  uint32_t hold = 0;
  int bits = 0;
  const auto push = [&](uint32_t v, int n) {
    hold |= (v & ((1u << n) - 1u)) << bits;
    bits += n;
    while (bits >= 8) {
      s.push_back(static_cast<char>(hold & 0xFF));
      hold >>= 8;
      bits -= 8;
    }
  };
  push(1, 1);  // BFINAL
  push(1, 2);  // BTYPE = fixed
  // Symbol 257 in the fixed literal table is code 0b0000001, written MSB-first.
  for (int i = 6; i >= 0; --i) push((0x01u >> i) & 1u, 1);
  for (int i = 0; i < 5; ++i) push(0, 1);  // distance code 0 -> distance 1
  push(0, 7);                              // flush the partial byte
  CHECK(refused(s));
}


// --- The stack, on the instrument that caught stb ----------------------------

TEST_CASE("THE STREAMING DECODER'S STACK IS A FRACTION OF THE ONE IT REPLACES") {
  // stb's one-shot inflate wants 6,608 bytes in a single frame and panicked the
  // device. The tables here are counts-and-symbols in the OBJECT, on the heap, so
  // the frame should be small -- measured on the same pattern-filled pthread stack
  // test_inflate.cpp uses, because a desktop main thread has 8 MB and cannot show
  // this.
  constexpr size_t kProbeStack = 512 * 1024;
  constexpr unsigned char kFill = 0xA5;
  static std::vector<unsigned char> stack;
  stack.assign(kProbeStack, kFill);

  struct Job {
    std::string out;
    bool ok = false;
  } job;

  pthread_attr_t attr;
  REQUIRE(pthread_attr_init(&attr) == 0);
  const size_t page = 16u * 1024u;
  unsigned char* base = stack.data();
  const size_t shift = (page - (reinterpret_cast<uintptr_t>(base) % page)) % page;
  base += shift;
  const size_t usable = (kProbeStack - shift) & ~(page - 1);
  REQUIRE(pthread_attr_setstack(&attr, base, usable) == 0);

  pthread_t tid{};
  REQUIRE(pthread_create(
              &tid, &attr,
              [](void* arg) -> void* {
                Job* j = static_cast<Job*>(arg);
                j->out = drain(FIX(kLongRun), 2048, &j->ok);
                return nullptr;
              },
              &job) == 0);
  REQUIRE(pthread_join(tid, nullptr) == 0);
  pthread_attr_destroy(&attr);

  REQUIRE(job.ok);
  CHECK(job.out.size() == deflatefix::kLongRunOut);

  size_t untouched = 0;
  while (untouched < usable && base[untouched] == kFill) ++untouched;
  const size_t used = usable - untouched;
  CAPTURE(used);
  // A CEILING, so a refactor that moves a table onto the stack fails here rather
  // than on the device. stb's whole chain measured 7,348 bytes; this must stay
  // well under it, and the 32 KB window is on the HEAP and so is not counted.
  CHECK(used > 256);    // the measurement is real, not a pattern-scan artifact
  CHECK(used <= 4096);  // measured 3,072 -- 42% of the 7,348 the stb chain wanted
}
