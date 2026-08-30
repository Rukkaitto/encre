#include <cstring>
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/sleep_cover.h"

namespace {

reader::SleepCoverHeader sample() {
  reader::SleepCoverHeader h;
  h.panelW = 528; h.panelH = 792; h.rotation = 1;
  h.planeBytes = 52272; h.bookBytes = 1234567; h.complete = 1;
  std::strcpy(h.bookPath, "/books/Le Fleau.epub");
  return h;
}

}  // namespace

TEST_CASE("the header round-trips through its fixed-size encoding") {
  std::vector<uint8_t> buf(reader::kSleepCoverHeaderBytes);
  const reader::SleepCoverHeader in = sample();
  reader::encodeSleepCoverHeader(in, buf.data());

  reader::SleepCoverHeader out;
  REQUIRE(reader::decodeSleepCoverHeader(buf.data(), buf.size(), out));
  CHECK(out.magic == in.magic);
  CHECK(out.version == in.version);
  CHECK(out.panelW == 528);
  CHECK(out.panelH == 792);
  CHECK(out.rotation == 1);
  CHECK(out.planeBytes == 52272);
  CHECK(out.bookBytes == 1234567u);
  CHECK(out.complete == 1);
  CHECK(std::string(out.bookPath) == "/books/Le Fleau.epub");
}

TEST_CASE("the encoding is little-endian and exactly the size it claims") {
  // The file is read by a device and written by a device, but it is also read by
  // a human with xxd when a cover comes out wrong, and it must not depend on the
  // compiler's padding. Assert the byte positions, not merely the round trip: an
  // encoder and a decoder that share a wrong layout round-trip perfectly.
  std::vector<uint8_t> buf(reader::kSleepCoverHeaderBytes, 0xAA);
  reader::SleepCoverHeader h;
  h.panelW = 0x01020304; h.panelH = 792; h.rotation = 1;
  h.planeBytes = 52272; h.bookBytes = 0x0A0B0C0D; h.complete = 1;
  reader::encodeSleepCoverHeader(h, buf.data());

  // magic "ERCV" == 0x56435245, little-endian: 45 52 43 56.
  CHECK(buf[0] == 0x45);
  CHECK(buf[1] == 0x52);
  CHECK(buf[2] == 0x43);
  CHECK(buf[3] == 0x56);
  CHECK(buf[4] == 1);  // version, and its three high bytes are zero
  CHECK(buf[5] == 0);
  CHECK(buf[6] == 0);
  CHECK(buf[7] == 0);
  // panelW at offset 8, low byte first.
  CHECK(buf[8] == 0x04);
  CHECK(buf[9] == 0x03);
  CHECK(buf[10] == 0x02);
  CHECK(buf[11] == 0x01);
  // bookBytes at offset 24.
  CHECK(buf[24] == 0x0D);
  CHECK(buf[27] == 0x0A);
  // The path field starts at 32 and the whole thing is 160 bytes, so an unset
  // path leaves 128 NULs and nothing beyond them is touched.
  for (size_t i = 32; i < reader::kSleepCoverHeaderBytes; ++i) CHECK(buf[i] == 0);
  CHECK(reader::kSleepCoverHeaderBytes == 160u);
}

TEST_CASE("a short buffer is refused rather than read past") {
  std::vector<uint8_t> buf(reader::kSleepCoverHeaderBytes);
  reader::encodeSleepCoverHeader(sample(), buf.data());
  reader::SleepCoverHeader out;
  CHECK_FALSE(reader::decodeSleepCoverHeader(buf.data(), 12, out));
  // ...and `out` is untouched, so a caller that ignores the bool gets defaults
  // rather than half a header. Defaults are complete = 0, which nothing trusts.
  CHECK(out.complete == 0);
  CHECK(out.panelW == 0);
  // One byte short of the whole thing is still short.
  CHECK_FALSE(reader::decodeSleepCoverHeader(buf.data(), reader::kSleepCoverHeaderBytes - 1, out));
  CHECK_FALSE(reader::decodeSleepCoverHeader(nullptr, reader::kSleepCoverHeaderBytes, out));
  // Exactly enough is enough, and more than enough is fine.
  CHECK(reader::decodeSleepCoverHeader(buf.data(), reader::kSleepCoverHeaderBytes, out));
  std::vector<uint8_t> spare(reader::kSleepCoverHeaderBytes + 4096, 0);
  reader::encodeSleepCoverHeader(sample(), spare.data());
  CHECK(reader::decodeSleepCoverHeader(spare.data(), spare.size(), out));
}

TEST_CASE("a path field with no terminator on the card is still NUL-terminated in RAM") {
  // The 128 bytes come off an SD card. A corrupt or truncated write can leave
  // them all non-zero, and std::string(out.bookPath) would then run off the end
  // of the struct -- reading whatever the caller's stack holds next and comparing
  // it against a book path. Refusing the header outright would be a second
  // spelling of "is this good", which sleepCoverUsable already owns, so decode
  // terminates it instead and the comparison simply fails.
  std::vector<uint8_t> buf(reader::kSleepCoverHeaderBytes);
  reader::encodeSleepCoverHeader(sample(), buf.data());
  for (size_t i = 32; i < reader::kSleepCoverHeaderBytes; ++i) buf[i] = 'x';

  reader::SleepCoverHeader out;
  REQUIRE(reader::decodeSleepCoverHeader(buf.data(), buf.size(), out));
  CHECK(std::strlen(out.bookPath) == sizeof(out.bookPath) - 1);
  CHECK(out.bookPath[sizeof(out.bookPath) - 1] == '\0');
}

TEST_CASE("sleepCoverUsable refuses every way the cache can be stale") {
  const reader::SleepCoverHeader good = sample();
  const std::string path = "/books/Le Fleau.epub";

  CHECK(reader::sleepCoverUsable(good, path, 1234567, 528, 792, 1, 52272));

  // Each of these has actually happened to a cache somewhere, and each must cost
  // the cover rather than putting the WRONG book's cover on the glass for hours.
  SUBCASE("incomplete") {
    reader::SleepCoverHeader h = good; h.complete = 0;
    CHECK_FALSE(reader::sleepCoverUsable(h, path, 1234567, 528, 792, 1, 52272));
  }
  SUBCASE("a different book at the same path") {
    CHECK_FALSE(reader::sleepCoverUsable(good, path, 999, 528, 792, 1, 52272));
  }
  SUBCASE("a different book") {
    CHECK_FALSE(reader::sleepCoverUsable(good, "/books/Other.epub", 1234567, 528, 792, 1, 52272));
  }
  SUBCASE("a path that is a PREFIX of the stored one") {
    // A comparison written with strncmp against the field's length, or against
    // the caller's length, accepts this. The stored path is NUL-padded, so the
    // honest comparison is of whole strings.
    CHECK_FALSE(reader::sleepCoverUsable(good, "/books/Le Fleau", 1234567, 528, 792, 1, 52272));
    CHECK_FALSE(reader::sleepCoverUsable(good, "/books/Le Fleau.epub.bak", 1234567, 528, 792, 1,
                                         52272));
  }
  SUBCASE("the other panel") {
    CHECK_FALSE(reader::sleepCoverUsable(good, path, 1234567, 480, 800, 1, 60000));
  }
  SUBCASE("the same width on the other panel's height") {
    // Both dimensions are checked, not just the one that changes between the two
    // shipped panels.
    CHECK_FALSE(reader::sleepCoverUsable(good, path, 1234567, 528, 800, 1, 52272));
    CHECK_FALSE(reader::sleepCoverUsable(good, path, 1234567, 480, 792, 1, 52272));
  }
  SUBCASE("the other rotation") {
    CHECK_FALSE(reader::sleepCoverUsable(good, path, 1234567, 528, 792, 0, 52272));
  }
  SUBCASE("a plane size that disagrees with the geometry") {
    CHECK_FALSE(reader::sleepCoverUsable(good, path, 1234567, 528, 792, 1, 52273));
  }
  SUBCASE("a version from another firmware") {
    reader::SleepCoverHeader h = good; h.version = 99;
    CHECK_FALSE(reader::sleepCoverUsable(h, path, 1234567, 528, 792, 1, 52272));
    h.version = 0;
    CHECK_FALSE(reader::sleepCoverUsable(h, path, 1234567, 528, 792, 1, 52272));
  }
  SUBCASE("not our file at all") {
    reader::SleepCoverHeader h = good; h.magic = 0;
    CHECK_FALSE(reader::sleepCoverUsable(h, path, 1234567, 528, 792, 1, 52272));
    h.magic = 0x45524356;  // the magic byte-swapped: a big-endian writer
    CHECK_FALSE(reader::sleepCoverUsable(h, path, 1234567, 528, 792, 1, 52272));
  }
}

TEST_CASE("a path too long for the field stores empty and therefore never matches") {
  // Truncating would be worse than refusing: two books whose paths share their
  // first 127 bytes would each accept the other's cover.
  const std::string longPath(200, 'x');
  reader::SleepCoverHeader made;
  made.panelW = 528; made.panelH = 792; made.rotation = 1;
  made.planeBytes = 52272; made.bookBytes = 1; made.complete = 1;

  CHECK_FALSE(reader::setSleepCoverBookPath(made, longPath));
  CHECK(made.bookPath[0] == '\0');
  CHECK_FALSE(reader::sleepCoverUsable(made, longPath, 1, 528, 792, 1, 52272));

  // AN EMPTY STORED PATH MATCHES NOTHING, INCLUDING AN EMPTY ONE. Empty means
  // "the writer refused to store a path", so a caller that also has nothing to
  // offer must not be handed a cover on the strength of two blanks agreeing.
  CHECK_FALSE(reader::sleepCoverUsable(made, "", 1, 528, 792, 1, 52272));
}

TEST_CASE("setSleepCoverBookPath stores exactly what fits and NUL-pads the rest") {
  reader::SleepCoverHeader h;
  // 127 bytes is the longest that fits: the field is 128 and the terminator is
  // one of them. This is the boundary a strncpy gets wrong in both directions.
  const std::string fits(127, 'a');
  CHECK(reader::setSleepCoverBookPath(h, fits));
  CHECK(std::string(h.bookPath) == fits);
  CHECK(h.bookPath[127] == '\0');

  const std::string overByOne(128, 'a');
  CHECK_FALSE(reader::setSleepCoverBookPath(h, overByOne));
  CHECK(h.bookPath[0] == '\0');  // and it cleared the one that DID fit

  // The tail past a short path is NULs, so the encoded bytes of two headers with
  // the same path are identical whatever was in the struct before.
  reader::SleepCoverHeader a, b;
  std::memset(b.bookPath, 'z', sizeof(b.bookPath));
  REQUIRE(reader::setSleepCoverBookPath(a, "/books/x.epub"));
  REQUIRE(reader::setSleepCoverBookPath(b, "/books/x.epub"));
  CHECK(std::memcmp(a.bookPath, b.bookPath, sizeof(a.bookPath)) == 0);
}

TEST_CASE("a header written by setSleepCoverBookPath survives the encoding") {
  // The two halves of the writer's job, joined: the path goes in through the
  // one function that owns the length rule, and comes back out of the file the
  // predicate will be asked about.
  reader::SleepCoverHeader h;
  h.panelW = 480; h.panelH = 800; h.rotation = 0;
  h.planeBytes = 48000; h.bookBytes = 42; h.complete = 1;
  const std::string path = "/books/Un titre avec des accents ecrases.epub";
  REQUIRE(reader::setSleepCoverBookPath(h, path));

  std::vector<uint8_t> buf(reader::kSleepCoverHeaderBytes);
  reader::encodeSleepCoverHeader(h, buf.data());
  reader::SleepCoverHeader out;
  REQUIRE(reader::decodeSleepCoverHeader(buf.data(), buf.size(), out));
  CHECK(reader::sleepCoverUsable(out, path, 42, 480, 800, 0, 48000));
}
