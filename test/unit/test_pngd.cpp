// The PNG decoder, against stb_image over real covers.
//
// UNLIKE JPEG THIS ONE IS BYTE-EXACT, and the assertions say so. PNG is
// lossless, so the only arithmetic between the two decoders is the grey
// weighting -- and PngDecoder::greyOf is fixed to stb's own coefficients
// precisely so this can be an equality rather than a tolerance. A tolerance here
// would hide a whole class of defect: an off-by-one in the Average filter moves
// most pixels by one level, which any tolerance loose enough to survive JPEG's
// IDCT rounding would swallow whole.
//
// TWO GRAINS, and grain 1 is the load-bearing one, exactly as it is for the
// inflater underneath: a source that satisfies every read hides every resumption
// bug there is -- a chunk header split across two reads, an IDAT boundary inside
// a scanline, the zlib header arriving one byte at a time.
//
// AND THE ORACLE IS A DIFFERENT DECODER, not a golden of our own output. A
// golden would pin whatever this decoder does, right or wrong, on the day it was
// blessed.
#include <cstdint>
#include <string>
#include <vector>

#include "doctest.h"
#include "grained_source.h"
#include "image_fixtures.h"
#include "reader/pngd.h"

namespace {

// Accumulates every row. A TEST may hold the whole image; the FIRMWARE may not,
// which is exactly why the decoder pushes rows instead of returning a buffer.
struct CollectingSink : reader::ImageRowSink {
  int width = 0, height = 0, rows = 0, begins = 0;
  int stopAfter = -1;   // -1 never stops
  bool acceptBegin = true;
  std::vector<uint8_t> px;

  bool begin(int w, int h) override {
    width = w;
    height = h;
    ++begins;
    return acceptBegin;
  }
  bool row(const uint8_t* p) override {
    px.insert(px.end(), p, p + width);
    ++rows;
    return stopAfter < 0 || rows < stopAfter;
  }
};

// One fixture, decoded both ways, asserted equal byte for byte. Every colour
// type this decoder accepts goes through here, so the assertion is written once
// and a new type is one line rather than a fourth copy of it.
void checkAgainstStb(const char* fixture, size_t grain = 4096) {
  CAPTURE(fixture);
  const std::string bytes = imgfix::loadFixture(fixture);
  REQUIRE(!bytes.empty());

  const imgfix::Oracle want = imgfix::decodeWithStb(bytes);
  REQUIRE_MESSAGE(!want.pixels.empty(), "the oracle refused " << fixture);

  grainsrc::Grained src(bytes, grain);
  CollectingSink sink;
  reader::PngDecoder dec;
  REQUIRE(dec.decode(src, sink));
  CHECK(dec.reason() == nullptr);
  CHECK_FALSE(dec.aborted());
  CHECK(sink.begins == 1);
  CHECK(sink.width == want.width);
  CHECK(sink.height == want.height);
  CHECK(sink.rows == want.height);
  // ONE ASSERTION OVER THE WHOLE IMAGE, not one per pixel: doctest counts and
  // prints every CHECK, and 3.84 million of them would bury the suite's own
  // output in a passing run.
  CHECK(sink.px == want.pixels);
}

}  // namespace

TEST_CASE("PngDecoder matches stb_image byte for byte on a real cover") {
  checkAgainstStb("truecolour.png");
}

TEST_CASE("PngDecoder is unaffected by how the source chunks its bytes") {
  const std::string bytes = imgfix::loadFixture("truecolour.png");
  REQUIRE(!bytes.empty());

  grainsrc::Grained big(bytes, 4096);
  grainsrc::Grained one(bytes, 1);
  CollectingSink a, b;
  reader::PngDecoder d1, d2;
  REQUIRE(d1.decode(big, a));
  REQUIRE(d2.decode(one, b));
  CHECK(a.rows == b.rows);
  CHECK(a.px == b.px);
}

// EVERY COLOUR TYPE THIS DECODER ACCEPTS HAS A FIXTURE, and that is a decision
// rather than a coincidence -- see fixtures/images/README.md. The corpus is 39
// of 39 colour type 2, so 0, 4 and 6 are accepted on the argument that they
// "come free with the same unfilter". Accepting a type that has never once been
// decoded is a claim, not a tested behaviour, so the three of them were
// synthesised from the same public-domain cover and are decoded here.
TEST_CASE("every colour type this decoder accepts is decoded, not merely accepted") {
  SUBCASE("colour type 0, grey") { checkAgainstStb("grey8.png"); }
  SUBCASE("colour type 4, grey and alpha") { checkAgainstStb("greyalpha8.png"); }
  SUBCASE("colour type 6, RGBA") { checkAgainstStb("rgba8.png"); }
}

// AND THE SAME HOLE EXISTED IN THE FILTERS. truecolour.png's 2400 rows use
// filters 1, 2 and 4 only -- there is not one None row and not one Average row
// in it -- so the byte-exactness assertion above never reached two of the five
// branches. grey8.png uses all five (None 3, Sub 15, Up 197, Average 4,
// Paeth 81), which is what makes the Average branch testable at all.
TEST_CASE("a fixture that uses all five row filters, including Average") {
  checkAgainstStb("grey8.png", 1);
}

// A REAL PNG SPLITS ITS IDAT, and this fixture is the one that says so: five
// IDAT chunks with ancillary chunks either side of them. The concatenation is
// invisible to the inflater by design -- it pulls through a ByteSource that
// walks chunks on demand -- so a decoder that got it wrong would report a
// corrupt stream rather than a wrong picture, which is why this asserts equality
// with the unsplit file's own pixels.
TEST_CASE("IDAT split across chunks, with ancillary chunks around it") {
  const std::string split = imgfix::loadFixture("split_idat.png");
  const std::string whole = imgfix::loadFixture("grey8.png");
  const imgfix::Oracle want = imgfix::decodeWithStb(whole);
  REQUIRE(!want.pixels.empty());

  grainsrc::Grained src(split, 1);
  CollectingSink sink;
  reader::PngDecoder dec;
  REQUIRE(dec.decode(src, sink));
  CHECK(sink.width == want.width);
  CHECK(sink.height == want.height);
  CHECK(sink.px == want.pixels);
}

TEST_CASE("a sink that says stop aborts the decode") {
  const std::string bytes = imgfix::loadFixture("truecolour.png");
  grainsrc::Grained src(bytes, 4096);
  CollectingSink sink;
  sink.stopAfter = 20;
  reader::PngDecoder dec;
  CHECK_FALSE(dec.decode(src, sink));
  // An abort is not a refusal, and the caller has to be able to tell them
  // apart: a refusal is permanent for this book, an abort is not.
  CHECK(dec.aborted());
  CHECK(dec.reason() == nullptr);
  CHECK(sink.rows == 20);
  CHECK(sink.rows < sink.height);
}

TEST_CASE("a sink that refuses to begin stops before any row") {
  const std::string bytes = imgfix::loadFixture("truecolour.png");
  grainsrc::Grained src(bytes, 4096);
  CollectingSink sink;
  sink.acceptBegin = false;
  reader::PngDecoder dec;
  CHECK_FALSE(dec.decode(src, sink));
  CHECK(dec.aborted());
  CHECK(sink.begins == 1);
  CHECK(sink.rows == 0);
}

// THE IHDR REFUSALS SHARE A FIXTURE AND A METHOD: one IHDR byte is changed and
// the chunk's CRC is deliberately NOT repaired, which is what proves the refusal
// is structural -- this decoder does not verify CRCs (a checksum failure on a
// cover should cost the cover, not the book), so the only thing that can be
// declining these files is the field itself.
namespace {

// `bytes` with one byte of the IHDR payload replaced. The signature is 8 bytes,
// then a 4-byte length and a 4-byte type, so IHDR's payload starts at 16.
std::string withIhdrByte(size_t offsetInPayload, uint8_t value) {
  std::string b = imgfix::loadFixture("truecolour.png");
  REQUIRE(b.size() > 16 + offsetInPayload);
  b[16 + offsetInPayload] = static_cast<char>(value);
  return b;
}

// `mentions` IS NOT DECORATION, and a mutation is what proved it. pngd.h states
// that each refusal names the thing rather than saying "unsupported", because
// the log line is the only way a user learns why a cover did not appear -- so
// the sentence is part of the contract. Two clauses in readHeader() decline a
// colour type: the explicit `colour == 3` and the generic `channels == 0` that
// catches 1, 5 and 7. DELETING THE PALETTE CLAUSE FAILED NOTHING while this
// helper asked only `reason() != nullptr`, because the generic one refused the
// file anyway with a worse sentence. Naming a word from the message is what
// makes the difference between the two testable at all.
void checkRefused(const std::string& bytes, const char* mentions) {
  grainsrc::Grained src(bytes, 4096);
  CollectingSink sink;
  reader::PngDecoder dec;
  CHECK_FALSE(dec.decode(src, sink));
  // Not an abort: the sink never asked for anything.
  CHECK_FALSE(dec.aborted());
  CHECK(sink.rows == 0);
  CHECK(sink.begins == 0);
  REQUIRE(dec.reason() != nullptr);
  const std::string why = dec.reason();
  CAPTURE(why);
  CHECK(why.find(mentions) != std::string::npos);
}

// THE OTHER SHAPE OF REFUSAL, and the difference is the sink's. Everything
// checkRefused covers is decided from IHDR or the zlib header, before the sink
// is told anything -- so it hears nothing at all. A row that cannot be decoded
// is decided AFTER begin(), because the dimensions were fine and only the
// pixels were not. A caller has to be able to expect both, which is why they
// are two named helpers rather than one with a loosened assertion.
void checkRefusedAfterHeader(const std::string& bytes, const char* mentions) {
  grainsrc::Grained src(bytes, 4096);
  CollectingSink sink;
  reader::PngDecoder dec;
  CHECK_FALSE(dec.decode(src, sink));
  CHECK_FALSE(dec.aborted());
  CHECK(sink.begins == 1);
  CHECK(sink.rows < sink.height);
  REQUIRE(dec.reason() != nullptr);
  const std::string why = dec.reason();
  CAPTURE(why);
  CHECK(why.find(mentions) != std::string::npos);
}

}  // namespace

TEST_CASE("PngDecoder refuses an interlaced PNG rather than showing a seventh of it") {
  // 0 of 225 corpus covers are interlaced, and a decoder that read an Adam7
  // stream as if it were progressive scanlines would put a plausible-looking
  // eighth-scale mosaic on the glass for hours.
  checkRefused(withIhdrByte(12, 1), "interlaced");  // interlace method -> Adam7
}

TEST_CASE("a palette PNG is refused, not rendered as noise") {
  checkRefused(withIhdrByte(9, 3), "palette");  // colour type -> palette
}

// THE OTHER CLAUSE, and it exists because colour type is a number rather than an
// enumeration: 1, 5 and 7 are in no PNG the spec allows, and a file off a card
// can hold any of them. It must refuse them and must NOT claim they are a
// palette -- which is the assertion that keeps the two clauses from collapsing
// into one message that is wrong for one of its inputs.
TEST_CASE("a colour type that is not in the spec at all is refused separately") {
  const std::string bytes = withIhdrByte(9, 5);
  checkRefused(bytes, "colour type");
  grainsrc::Grained src(bytes, 4096);
  CollectingSink sink;
  reader::PngDecoder dec;
  CHECK_FALSE(dec.decode(src, sink));
  CHECK(std::string(dec.reason()).find("palette") == std::string::npos);
}

TEST_CASE("a bit depth other than 8 is refused") {
  // 16 is the one that is legal PNG for this colour type and would otherwise
  // decode as noise at half the width; 1, 2 and 4 are not legal for colour
  // type 2 at all, and are refused by the same clause.
  SUBCASE("16") { checkRefused(withIhdrByte(8, 16), "8 bits a channel"); }
  SUBCASE("4") { checkRefused(withIhdrByte(8, 4), "8 bits a channel"); }
}

TEST_CASE("a PNG with no pixels is refused") {
  std::string b = imgfix::loadFixture("truecolour.png");
  REQUIRE(b.size() > 20);
  b[16] = b[17] = b[18] = b[19] = 0;  // width -> 0
  checkRefused(b, "no pixels");
}

TEST_CASE("PngDecoder refuses bytes that are not a PNG at all") {
  // A real JPEG, which is the confusion that can actually happen: cover.cpp
  // sniffs the first bytes and could hand this decoder the wrong file.
  checkRefused(imgfix::loadFixture("baseline.jpg"), "not a PNG");
}

TEST_CASE("PngDecoder refuses a truncated PNG rather than reporting success") {
  std::string bytes = imgfix::loadFixture("truecolour.png");
  REQUIRE(bytes.size() > 20000);
  bytes.resize(20000);

  grainsrc::Grained src(bytes, 4096);
  CollectingSink sink;
  reader::PngDecoder dec;
  CHECK_FALSE(dec.decode(src, sink));
  CHECK_FALSE(dec.aborted());
  REQUIRE(dec.reason() != nullptr);
  // WHAT THE SINK KEEPS WHEN THIS ANSWERS FALSE: every row it was already
  // given, and no more. The same contract JpegDecoder states, and the reason
  // both state it is that a false means "this picture is not finished", never
  // "undo what you were told".
  CHECK(sink.begins == 1);
  CHECK(sink.rows > 0);
  CHECK(sink.rows < sink.height);
}

TEST_CASE("a PNG whose compressed data is corrupt is refused, not half-drawn as success") {
  std::string bytes = imgfix::loadFixture("truecolour.png");
  REQUIRE(bytes.size() > 30000);
  // Well past the zlib header and the first scanlines, so the failure is a
  // malformed Huffman code mid-stream rather than a rejected header.
  for (size_t i = 25000; i < 25064; ++i) bytes[i] = static_cast<char>(0x5A);

  grainsrc::Grained src(bytes, 4096);
  CollectingSink sink;
  reader::PngDecoder dec;
  CHECK_FALSE(dec.decode(src, sink));
  CHECK_FALSE(dec.aborted());
  REQUIRE(dec.reason() != nullptr);
  CHECK(sink.rows < sink.height);
}

TEST_CASE("a PngDecoder can be used again, and keeps nothing from the last time") {
  const std::string good = imgfix::loadFixture("grey8.png");
  const std::string bad = imgfix::loadFixture("baseline.jpg");
  const imgfix::Oracle want = imgfix::decodeWithStb(good);
  REQUIRE(!want.pixels.empty());

  reader::PngDecoder dec;

  grainsrc::Grained s1(bad, 4096);
  CollectingSink k1;
  CHECK_FALSE(dec.decode(s1, k1));
  CHECK(dec.reason() != nullptr);

  // The refusal above must not survive into the decode below -- a stale reason
  // reads to a caller as a picture that failed and produced rows anyway.
  grainsrc::Grained s2(good, 4096);
  CollectingSink k2;
  REQUIRE(dec.decode(s2, k2));
  CHECK(dec.reason() == nullptr);
  CHECK_FALSE(dec.aborted());
  CHECK(k2.px == want.pixels);

  // ...and an abort must not survive either.
  grainsrc::Grained s3(good, 4096);
  CollectingSink k3;
  k3.stopAfter = 5;
  CHECK_FALSE(dec.decode(s3, k3));
  CHECK(dec.aborted());

  grainsrc::Grained s4(good, 4096);
  CollectingSink k4;
  REQUIRE(dec.decode(s4, k4));
  CHECK_FALSE(dec.aborted());
  CHECK(k4.px == want.pixels);
}

TEST_CASE("PngDecoder reports the heap a decode holds") {
  const std::string bytes = imgfix::loadFixture("truecolour.png");
  grainsrc::Grained src(bytes, 4096);
  CollectingSink sink;
  reader::PngDecoder dec;
  CHECK(dec.workspaceBytes() == 0);  // nothing decoded yet
  REQUIRE(dec.decode(src, sink));

  // The inflate window and its tables, plus the row block. 1600 wide x 3
  // channels is 4,800 bytes a row, two of those for the unfilter's previous
  // row, plus 1,600 for the grey the sink is handed.
  const size_t rows = 2u * 1600 * 3 + 1600;
  CHECK(dec.workspaceBytes() == reader::Inflater::kHeapBytes + rows);
  // The figure the spec's memory budget quotes. Written out so a change to
  // either term is a failing test rather than a budget that has drifted.
  CHECK(dec.workspaceBytes() == 48256);
}

// A PNG BUILT HERE, BECAUSE THREE OF THIS DECODER'S REFUSALS HAVE NO FIXTURE
// AND CANNOT HAVE ONE: a filter byte above 4, a malformed zlib header and a
// preset-dictionary request are all things no encoder will produce, so a
// committed file would have to be hand-corrupted anyway. pngd.h claims all three
// are refused structurally; a claim with nothing exercising it is what this
// project keeps finding at the bottom of a defect.
//
// IT NEEDS NO COMPRESSOR. DEFLATE has a stored-block mode, so a valid zlib
// stream is a two-byte header, a framed copy of the bytes and a checksum -- the
// same trick test_reader_restream.cpp uses to get a real method-8 zip entry
// without one. What that leaves is real, whole-file PNG bytes that stb_image
// accepts, which is what the first test below proves before any of the others
// trusts the builder.
namespace {

uint32_t crc32Of(const uint8_t* p, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) {
    c ^= p[i];
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (~(c & 1u) + 1u));
  }
  return ~c;
}

void putBe32(std::string& s, uint32_t v) {
  s.push_back(static_cast<char>(v >> 24));
  s.push_back(static_cast<char>(v >> 16));
  s.push_back(static_cast<char>(v >> 8));
  s.push_back(static_cast<char>(v));
}

void putChunk(std::string& s, const char* type, const std::string& payload) {
  putBe32(s, static_cast<uint32_t>(payload.size()));
  std::string body(type, 4);
  body += payload;
  s += body;
  putBe32(s, crc32Of(reinterpret_cast<const uint8_t*>(body.data()), body.size()));
}

// `raw` is h rows of (1 filter byte + w grey bytes). `zlibHeader` is the two
// bytes of RFC 1950 wrapper, so a test can hand over a broken one.
std::string tinyGreyPng(int w, int h, const std::string& raw,
                        uint16_t zlibHeader = 0x7801) {
  std::string z;
  z.push_back(static_cast<char>(zlibHeader >> 8));
  z.push_back(static_cast<char>(zlibHeader & 0xFF));
  // One final stored block. LEN and its complement are little-endian.
  z.push_back(static_cast<char>(0x01));
  const uint16_t len = static_cast<uint16_t>(raw.size());
  z.push_back(static_cast<char>(len & 0xFF));
  z.push_back(static_cast<char>(len >> 8));
  z.push_back(static_cast<char>(~len & 0xFF));
  z.push_back(static_cast<char>((~len >> 8) & 0xFF));
  z += raw;
  uint32_t a = 1, b = 0;
  for (unsigned char c : raw) {
    a = (a + c) % 65521;
    b = (b + a) % 65521;
  }
  putBe32(z, (b << 16) | a);

  std::string ihdr;
  putBe32(ihdr, static_cast<uint32_t>(w));
  putBe32(ihdr, static_cast<uint32_t>(h));
  const char tail[5] = {8, 0, 0, 0, 0};  // depth, colour 0, comp, filter, interlace
  ihdr.append(tail, 5);

  std::string out("\x89PNG\r\n\x1a\n", 8);
  putChunk(out, "IHDR", ihdr);
  putChunk(out, "IDAT", z);
  putChunk(out, "IEND", std::string());
  return out;
}

// Eight by five, a two-axis ramp, every row filtered None -- so the bytes are
// the picture and a wrong answer is legible rather than merely different.
std::string rampRaw(int w, int h, uint8_t firstFilter = 0) {
  std::string raw;
  for (int y = 0; y < h; ++y) {
    raw.push_back(static_cast<char>(y == 0 ? firstFilter : 0));
    for (int x = 0; x < w; ++x) raw.push_back(static_cast<char>(x * 8 + y * 40));
  }
  return raw;
}

}  // namespace

// THE BUILDER IS PROVED BEFORE ANYTHING RESTS ON IT. A mutation that fails
// nothing tells you about your INPUT before it tells you about your test, and a
// hand-built file that no decoder accepts would make every refusal below pass
// for the wrong reason.
TEST_CASE("the PNG this file builds is a real one, accepted by stb_image too") {
  const std::string bytes = tinyGreyPng(8, 5, rampRaw(8, 5));
  const imgfix::Oracle want = imgfix::decodeWithStb(bytes);
  REQUIRE_MESSAGE(!want.pixels.empty(), "stb_image refused the file this test builds");
  CHECK(want.width == 8);
  CHECK(want.height == 5);

  grainsrc::Grained src(bytes, 1);
  CollectingSink sink;
  reader::PngDecoder dec;
  REQUIRE(dec.decode(src, sink));
  CHECK(sink.rows == 5);
  CHECK(sink.px == want.pixels);
}

// EVERY FIXTURE'S FIRST ROW IS SUB-FILTERED -- measured, all five of them -- and
// Sub never reads the row above. So the "virtual row of zeroes above the image"
// that Up, Average and Paeth read on row 0 was reached by NOTHING, and dropping
// the row block's value-initialisation failed no test at all. An encoder will
// not fix this: filtering row 0 against a zero row is wasteful, so Sub or None
// is what every one of them picks.
//
// WHAT THIS CAN AND CANNOT PROVE, measured by mutation rather than argued.
// POISONING the row block with 0xAA fails this test and NOTHING ELSE -- so it
// reaches the row-0 `prev` read, no other test does, and a `prev` pointing at
// the wrong end of the block or a row-0 case that drops the term is caught
// outright. Dropping the `()` from the allocation, though, still fails nothing:
// this allocator hands back zeroed pages, and uninitialised memory is not
// observable from a portable test at all. A sanitizer is where that half of the
// coverage lives. Written down rather than left to be rediscovered -- and the
// all-ink first decode stays, because on an allocator that recycles the block
// it turns the unobservable half into a real failure for free.
TEST_CASE("the row above the first row is zero, which a first-row Up or Paeth reads") {
  uint8_t first = 0;
  SUBCASE("Up") { first = 2; }
  SUBCASE("Average") { first = 3; }
  SUBCASE("Paeth") { first = 4; }

  reader::PngDecoder dec;

  // Same dimensions, so the second decode's row block is the size the first one
  // just gave back -- and full of ink rather than zeroes.
  std::string inkRaw;
  for (int y = 0; y < 5; ++y) {
    inkRaw.push_back(0);
    for (int x = 0; x < 8; ++x) inkRaw.push_back(static_cast<char>(0xF7));
  }
  const std::string ink = tinyGreyPng(8, 5, inkRaw);
  grainsrc::Grained s1(ink, 4096);
  CollectingSink k1;
  REQUIRE(dec.decode(s1, k1));

  const std::string bytes = tinyGreyPng(8, 5, rampRaw(8, 5, first));
  const imgfix::Oracle want = imgfix::decodeWithStb(bytes);
  REQUIRE(!want.pixels.empty());
  grainsrc::Grained s2(bytes, 4096);
  CollectingSink k2;
  REQUIRE(dec.decode(s2, k2));
  CHECK(k2.rows == 5);
  CHECK(k2.px == want.pixels);
}

TEST_CASE("a row filter the spec does not define is refused, not treated as Paeth") {
  // unfilter()'s last arm is Paeth and takes everything that reaches it, so
  // without this refusal a 7 would silently produce a wrong picture rather than
  // a message. 5 is the first undefined value; 255 is what a run of corrupt
  // bytes looks like.
  SUBCASE("5") {
    checkRefusedAfterHeader(tinyGreyPng(8, 5, rampRaw(8, 5, 5)), "row filter");
  }
  SUBCASE("255") {
    checkRefusedAfterHeader(tinyGreyPng(8, 5, rampRaw(8, 5, 255)), "row filter");
  }
}

TEST_CASE("a malformed zlib header is refused before a byte is inflated") {
  // The check digit: RFC 1950 requires (CMF << 8 | FLG) % 31 == 0, and 0x7800
  // is 0x7801 with it wrong.
  SUBCASE("the check digit") {
    checkRefused(tinyGreyPng(8, 5, rampRaw(8, 5), 0x7800), "malformed header");
  }
  SUBCASE("a compression method that is not deflate") {
    // CM 7 rather than 8, with a check digit that still adds up.
    checkRefused(tinyGreyPng(8, 5, rampRaw(8, 5), 0x771A), "deflate");
  }
  SUBCASE("a preset dictionary, which PNG forbids and this decoder was never given") {
    checkRefused(tinyGreyPng(8, 5, rampRaw(8, 5), 0x78BB), "preset dictionary");
  }
}

TEST_CASE("greyOf is stb_image's own weighting, which is what makes the equality assertions legal") {
  CHECK(reader::PngDecoder::greyOf(0, 0, 0) == 0);
  CHECK(reader::PngDecoder::greyOf(255, 255, 255) == 255);
  // (255*77 + 0 + 0) >> 8 == 76
  CHECK(reader::PngDecoder::greyOf(255, 0, 0) == 76);
  CHECK(reader::PngDecoder::greyOf(0, 255, 0) == 149);
  CHECK(reader::PngDecoder::greyOf(0, 0, 255) == 28);
}
