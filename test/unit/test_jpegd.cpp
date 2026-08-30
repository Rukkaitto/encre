// The JPEG decoder, against stb_image over a real cover.
//
// TWO GRAINS, and grain 1 is the load-bearing one: TJpgDec pulls through our
// ByteSource, so a source that satisfies every read hides every refill bug there
// is. test_inflate_stream.cpp found exactly that class of defect this way.
//
// AND THE ORACLE IS A DIFFERENT DECODER, not a golden of our own output. A
// golden here would pin whatever this decoder does, right or wrong, on the day
// it was blessed; stb_image is the move that validated inflate_stream (216 entry
// passes, zero disagreements) and it is available for the same reason.
#include <cstdlib>
#include <string>
#include <vector>

#include "doctest.h"
#include "grained_source.h"
#include "image_fixtures.h"
#include "reader/jpegd.h"

namespace {

// Accumulates every row. A TEST may hold the whole image; the FIRMWARE may not,
// which is exactly why the decoder pushes rows instead of returning a buffer.
struct CollectingSink : reader::ImageRowSink {
  int width = 0, height = 0, rows = 0, begins = 0;
  int stopAfter = -1;  // -1 never stops
  std::vector<uint8_t> px;

  bool begin(int w, int h) override {
    width = w;
    height = h;
    ++begins;
    return true;
  }
  bool row(const uint8_t* p) override {
    px.insert(px.end(), p, p + width);
    ++rows;
    return stopAfter < 0 || rows < stopAfter;
  }
};

// Mean and worst absolute difference between two same-sized grey images.
struct Diff {
  long worst = 0;
  double mean = 0;
};
Diff compare(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
  REQUIRE(a.size() == b.size());
  REQUIRE(!a.empty());
  Diff d;
  long sum = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    const long delta = std::labs(static_cast<long>(a[i]) - static_cast<long>(b[i]));
    if (delta > d.worst) d.worst = delta;
    sum += delta;
  }
  d.mean = static_cast<double>(sum) / static_cast<double>(a.size());
  return d;
}

// The oracle reduced by an n x n box average, which is what TJpgDec's descaling
// does to an MCU (mcu_output averages a 2^scale square per output pixel) -- so a
// scaled decode has an oracle of its own, and the scaled paths are checked on
// their PIXELS rather than only on their dimensions. Partial squares at the
// right and bottom edge are dropped, exactly as the decoder's own truncation
// does.
std::vector<uint8_t> boxAverage(const imgfix::Oracle& src, int n, int w, int h) {
  // w/h ARE THE DECODER'S OWN REPORTED SIZE, so they are the thing under test and
  // must not be trusted to index the oracle. Without this, a decoder that reports
  // a size too large reads off the end of `src.pixels` and compares garbage --
  // the test then fails by luck rather than by design, and under a sanitizer it
  // fails as a harness crash instead of as a named assertion.
  REQUIRE(w > 0);
  REQUIRE(h > 0);
  REQUIRE(w * n <= src.width);
  REQUIRE(h * n <= src.height);
  std::vector<uint8_t> out(static_cast<size_t>(w) * h);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      unsigned sum = 0;
      for (int dy = 0; dy < n; ++dy) {
        for (int dx = 0; dx < n; ++dx) {
          sum += src.pixels[static_cast<size_t>(y * n + dy) * src.width + (x * n + dx)];
        }
      }
      out[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(sum / (n * n));
    }
  }
  return out;
}

}  // namespace

TEST_CASE("JpegDecoder matches stb_image on a real baseline cover") {
  const std::string bytes = imgfix::loadFixture("baseline.jpg");
  REQUIRE(!bytes.empty());

  const imgfix::Oracle want = imgfix::decodeWithStb(bytes);
  REQUIRE(!want.pixels.empty());

  grainsrc::Grained src(bytes, 4096);
  CollectingSink sink;
  reader::JpegDecoder dec;
  REQUIRE(dec.decode(src, sink));  // full scale: no atLeast given
  CHECK(dec.scaleDivisor() == 1);
  CHECK(dec.sourceWidth() == want.width);
  CHECK(dec.sourceHeight() == want.height);
  CHECK(sink.begins == 1);
  CHECK(sink.width == want.width);
  CHECK(sink.height == want.height);
  CHECK(sink.rows == want.height);
  REQUIRE(sink.px.size() == want.pixels.size());

  // NOT byte-identical, and it must not be asserted as such: TJpgDec and stb use
  // different IDCT rounding and different YCbCr->grey coefficients. What is
  // asserted is that no pixel is far off, which catches a wrong upsample, a
  // transposed block or an off-by-one row -- the defects that actually happen --
  // while tolerating arithmetic that is legitimately not bit-equal.
  const Diff d = compare(sink.px, want.pixels);
  CHECK(d.worst <= 24);
  CHECK(d.mean <= 2.0);
}

TEST_CASE("JpegDecoder is unaffected by how the source chunks its bytes") {
  const std::string bytes = imgfix::loadFixture("baseline.jpg");
  REQUIRE(!bytes.empty());

  grainsrc::Grained big(bytes, 4096);
  grainsrc::Grained one(bytes, 1);
  CollectingSink a, b;
  reader::JpegDecoder d1, d2;
  REQUIRE(d1.decode(big, a));
  REQUIRE(d2.decode(one, b));
  CHECK(a.width == b.width);
  CHECK(a.rows == b.rows);
  CHECK(a.px == b.px);
}

TEST_CASE("JpegDecoder scales down but never below what the caller asked for") {
  // The free IDCT scaling is what makes a 2.94 MP cover affordable. This fixture
  // is 740x1000 -- SMALLER than the corpus median cover, and small enough that a
  // 480x800 panel cannot be served by a halved decode (370 < 480), so the panel
  // request is the case that must NOT scale. The smaller requests below are what
  // exercise the divisors themselves.
  const std::string bytes = imgfix::loadFixture("baseline.jpg");
  REQUIRE(!bytes.empty());
  const imgfix::Oracle full = imgfix::decodeWithStb(bytes);
  REQUIRE(!full.pixels.empty());

  SUBCASE("a panel-sized request this fixture cannot halve stays at full scale") {
    grainsrc::Grained src(bytes, 4096);
    CollectingSink sink;
    reader::JpegDecoder dec;
    REQUIRE(dec.decode(src, sink, 480, 800));
    CHECK(dec.scaleDivisor() == 1);
    CHECK(sink.width >= 480);
    CHECK(sink.height >= 800);
    CHECK(sink.width == full.width);
  }

  SUBCASE("half scale, and the pixels are the oracle's own 2x2 averages") {
    grainsrc::Grained src(bytes, 4096);
    CollectingSink sink;
    reader::JpegDecoder dec;
    REQUIRE(dec.decode(src, sink, 300, 400));
    CHECK(dec.scaleDivisor() == 2);
    CHECK(sink.width == 370);
    CHECK(sink.height == 500);
    CHECK(sink.rows == 500);
    CHECK(sink.width >= 300);
    CHECK(sink.height >= 400);
    CHECK(sink.width < full.width);
    const Diff d = compare(sink.px, boxAverage(full, 2, sink.width, sink.height));
    CHECK(d.worst <= 24);
    CHECK(d.mean <= 2.0);
  }

  SUBCASE("eighth scale, which is a different branch of the library") {
    // At 1/8 TJpgDec skips the IDCT altogether and fills each block with its DC
    // value, so this is not the same code as 1/2 or 1/4 and a band bug can hide
    // in one and not the other. The DC term IS the block mean, so the 8x8 box
    // average is still the oracle -- and it holds to the SAME bound as the other
    // two, which was worth checking rather than assuming: a quantised mean could
    // have needed a looser one and does not.
    grainsrc::Grained src(bytes, 4096);
    CollectingSink sink;
    reader::JpegDecoder dec;
    REQUIRE(dec.decode(src, sink, 90, 120));
    CHECK(dec.scaleDivisor() == 8);
    CHECK(sink.width == 92);
    CHECK(sink.height == 125);
    CHECK(sink.rows == 125);
    const Diff d = compare(sink.px, boxAverage(full, 8, sink.width, sink.height));
    CHECK(d.worst <= 24);
    CHECK(d.mean <= 2.0);
  }

  SUBCASE("a request no divisor can serve is decoded at full scale") {
    grainsrc::Grained src(bytes, 4096);
    CollectingSink sink;
    reader::JpegDecoder dec;
    REQUIRE(dec.decode(src, sink, 4000, 6000));
    CHECK(dec.scaleDivisor() == 1);
    CHECK(sink.width == full.width);
  }
}

TEST_CASE("JpegDecoder handles every MCU geometry, not just the one a cover happens to have") {
  // THE BAND IS THE WHOLE OF jpegd.cpp, AND baseline.jpg EXERCISES ONE SHAPE OF
  // IT. That cover is 4:2:0, so msx == msy == 2 and the band is always 16 rows;
  // every other sampling factor gives a band of 8, a different spacing between
  // consecutive band tops, and a different trigger point for the flush. Nothing
  // on the desktop would have caught a regression in those, and the device has
  // no oracle to notice on its behalf.
  //
  // Three synthetic fixtures, each a few hundred bytes, chosen for the cases the
  // real cover cannot reach -- see fixtures/images/README.md for how they were
  // made. All three also end in a SHORT final band (one row), which is the part
  // of the flush that arithmetic rather than the rectangles would get wrong.
  // THE BAND'S SHAPE IS INVISIBLE FROM OUT HERE except through workspaceBytes(),
  // so without this the band could be sized for the wrong geometry and every
  // assertion below would still pass: `bandFilled` comes from the rectangles, so
  // a band buffer that is too TALL is not a wrong picture, only wasted heap --
  // and heap is the whole reason this class exists. Proved by mutation: hardcoding
  // a 16-row band (i.e. assuming 4:2:0 everywhere) failed nothing at all until
  // this check existed.
  //
  // The pool is recovered from a fixture whose geometry is known rather than
  // transcribed as a literal, so re-deriving kPoolBytes does not falsely fail
  // this test -- only a change to the BAND does.
  size_t pool = 0;
  {
    const std::string cover = imgfix::loadFixture("baseline.jpg");
    grainsrc::Grained src(cover, 4096);
    CollectingSink sink;
    reader::JpegDecoder dec;
    REQUIRE(dec.decode(src, sink));
    const size_t band = 16u * 740u;  // 4:2:0 at full scale, 740 wide
    REQUIRE(dec.workspaceBytes() > band);
    pool = dec.workspaceBytes() - band;
  }

  struct Case {
    const char* file;
    int w, h;
    int bandRows;  // msy * 8, at full scale
    const char* what;
  };
  const Case cases[] = {
      // msx=1 msy=1, band 8 rows, 5 MCU columns of which the last is 1px wide.
      {"tiny_444.jpg", 33, 9, 8, "4:4:4, a short final band and a short final column"},
      // msx=2 msy=1 -- the mixed factors, and NARROWER THAN ONE MCU (9 < 16), so
      // the only rectangle in each band is clipped on both axes.
      {"tiny_422.jpg", 9, 17, 8, "4:2:2, narrower than a single MCU"},
      // msx=2 msy=2 as the cover, but 17x17: one full band plus a band of one row.
      {"tiny_420.jpg", 17, 17, 16, "4:2:0, one full band and a one-row band"},
  };

  for (const Case& c : cases) {
    CAPTURE(c.file);
    CAPTURE(c.what);
    const std::string bytes = imgfix::loadFixture(c.file);
    REQUIRE(!bytes.empty());
    const imgfix::Oracle want = imgfix::decodeWithStb(bytes);
    REQUIRE(want.width == c.w);
    REQUIRE(want.height == c.h);

    // Grain 1 as well as a satisfying grain, for the reason the cover is read
    // both ways: these files are small enough that a single read would satisfy
    // TJpgDec outright and hide every refill.
    for (size_t grain : {size_t{4096}, size_t{1}}) {
      CAPTURE(grain);
      grainsrc::Grained src(bytes, grain);
      CollectingSink sink;
      reader::JpegDecoder dec;
      REQUIRE(dec.decode(src, sink));
      CHECK(dec.scaleDivisor() == 1);
      CHECK(sink.width == c.w);
      CHECK(sink.height == c.h);
      CHECK(sink.rows == c.h);
      REQUIRE(sink.px.size() == want.pixels.size());
      const Diff d = compare(sink.px, want.pixels);
      CHECK(d.worst <= 24);
      CHECK(d.mean <= 2.0);
      CHECK(dec.workspaceBytes() ==
            pool + static_cast<size_t>(c.bandRows) * static_cast<size_t>(c.w));
    }
  }
}

TEST_CASE("a scaled decode of an odd geometry still fills every row it promises") {
  // Halving moves the band as well as the picture -- an 8-row band becomes 4 --
  // so the short final band and the dropped final column both land at different
  // places than they do at full scale. 33x9 at 1/2 is 16x4: the 1px-wide last
  // MCU column rounds away entirely, which is the case where deriving the output
  // width from anything but the rectangles goes wrong.
  const std::string bytes = imgfix::loadFixture("tiny_444.jpg");
  const imgfix::Oracle full = imgfix::decodeWithStb(bytes);
  REQUIRE(full.width == 33);

  grainsrc::Grained src(bytes, 1);
  CollectingSink sink;
  reader::JpegDecoder dec;
  REQUIRE(dec.decode(src, sink, 16, 4));
  CHECK(dec.scaleDivisor() == 2);
  CHECK(sink.width == 16);
  CHECK(sink.height == 4);
  CHECK(sink.rows == 4);
  const Diff d = compare(sink.px, boxAverage(full, 2, sink.width, sink.height));
  CHECK(d.worst <= 24);
  CHECK(d.mean <= 2.0);
}

TEST_CASE("a sink that says stop aborts the decode") {
  // This is the interruption path the shell uses to get out of the way of a button
  // press, and it is TJpgDec's own: outfunc returning 0 aborts with JDR_INTR.
  const std::string bytes = imgfix::loadFixture("baseline.jpg");
  REQUIRE(!bytes.empty());

  grainsrc::Grained src(bytes, 4096);
  CollectingSink sink;
  sink.stopAfter = 40;  // mid-band: the MCU is 16 rows tall
  reader::JpegDecoder dec;
  CHECK_FALSE(dec.decode(src, sink));
  CHECK(dec.aborted());
  CHECK_FALSE(dec.outOfMemory());
  CHECK(sink.rows == 40);
  CHECK(sink.rows < sink.height);
  // An abort is not a failure of the file, so it must not read as one: it is the
  // one false return that leaves reason() null.
  CHECK(dec.reason() == nullptr);
}

TEST_CASE("a sink that refuses to begin stops before any row") {
  struct RefusingSink : CollectingSink {
    bool begin(int, int) override { return false; }
  } sink;
  const std::string bytes = imgfix::loadFixture("baseline.jpg");
  grainsrc::Grained src(bytes, 4096);
  reader::JpegDecoder dec;
  CHECK_FALSE(dec.decode(src, sink));
  CHECK(dec.aborted());
  CHECK_FALSE(dec.outOfMemory());
  CHECK(sink.rows == 0);
}

TEST_CASE("JpegDecoder refuses a progressive JPEG rather than mis-decoding it") {
  const std::string bytes = imgfix::loadFixture("progressive.jpg");
  REQUIRE(!bytes.empty());
  grainsrc::Grained src(bytes, 4096);
  CollectingSink sink;
  reader::JpegDecoder dec;
  CHECK_FALSE(dec.decode(src, sink));
  CHECK_FALSE(dec.aborted());  // refused, not interrupted -- a different outcome
  CHECK_FALSE(dec.outOfMemory());
  CHECK(sink.rows == 0);
  CHECK(sink.begins == 0);
  // A refusal must SAY something -- every refusal on this path ends up in a log
  // line the user's card can be diagnosed from.
  REQUIRE(dec.reason() != nullptr);
  CHECK(std::string(dec.reason()).size() > 0);
}

TEST_CASE("JpegDecoder refuses bytes that are not a JPEG at all") {
  // Everything here is bytes off somebody's card, so the not-an-image case is a
  // real one rather than a hypothetical: a cover the OPF names may be anything.
  const std::string bytes(4000, '\0');
  grainsrc::Grained src(bytes, 4096);
  CollectingSink sink;
  reader::JpegDecoder dec;
  CHECK_FALSE(dec.decode(src, sink));
  CHECK_FALSE(dec.aborted());
  CHECK_FALSE(dec.outOfMemory());
  CHECK(sink.rows == 0);
  REQUIRE(dec.reason() != nullptr);
}

TEST_CASE("JpegDecoder refuses a truncated JPEG rather than reporting success") {
  const std::string whole = imgfix::loadFixture("baseline.jpg");
  REQUIRE(whole.size() > 20000);
  const std::string bytes = whole.substr(0, whole.size() / 2);
  grainsrc::Grained src(bytes, 4096);
  CollectingSink sink;
  reader::JpegDecoder dec;
  CHECK_FALSE(dec.decode(src, sink));
  CHECK_FALSE(dec.aborted());
  CHECK_FALSE(dec.outOfMemory());
  REQUIRE(dec.reason() != nullptr);
  // The rows it DID produce were real -- a truncation is a short read, not a
  // corrupt one -- so the caller gets a partial picture and a false, and it is
  // the false that decides.
  CHECK(sink.rows > 0);
  CHECK(sink.rows < sink.height);
}

TEST_CASE("a JpegDecoder can be used again, and keeps nothing from the last time") {
  // Task 6 decodes a cover and may come back for another; a decoder that carried
  // its last refusal forward would report a good cover as bad.
  const std::string good = imgfix::loadFixture("baseline.jpg");
  const std::string bad = imgfix::loadFixture("progressive.jpg");
  reader::JpegDecoder dec;

  grainsrc::Grained s1(bad, 4096);
  CollectingSink r1;
  CHECK_FALSE(dec.decode(s1, r1));
  REQUIRE(dec.reason() != nullptr);

  grainsrc::Grained s2(good, 4096);
  CollectingSink r2;
  REQUIRE(dec.decode(s2, r2));
  CHECK(dec.reason() == nullptr);
  CHECK_FALSE(dec.aborted());
  CHECK_FALSE(dec.outOfMemory());
  CHECK(r2.rows == 1000);

  // ...and the other way round, which is the half that catches a `reason` only
  // ever assigned and never cleared.
  grainsrc::Grained s3(bad, 4096);
  CollectingSink r3;
  CHECK_FALSE(dec.decode(s3, r3));
  CHECK(dec.reason() != nullptr);
  CHECK(r3.rows == 0);
}

TEST_CASE("outOfMemory is false for every refusal that is about the FILE") {
  // THE FLAG IS ACTED ON, so what it must not do is fire for a bad file: cover.cpp
  // asks it before it asks anything else about a refusal, and a decoder that set it
  // on an ordinary failure would report a progressive JPEG as a device too small.
  // The other direction -- a real shortfall -- cannot be provoked from a test here,
  // and test_cover.cpp says why at length.
  CollectingSink sink;
  reader::JpegDecoder dec;

  SUBCASE("progressive") {
    const std::string bytes = imgfix::loadFixture("progressive.jpg");
    grainsrc::Grained src(bytes, 4096);
    CHECK_FALSE(dec.decode(src, sink));
    CHECK_FALSE(dec.outOfMemory());
    CHECK(dec.reason() != nullptr);
  }
  SUBCASE("not a JPEG at all") {
    const std::string bytes = "certainly not a JPEG";
    grainsrc::Grained src(bytes, 4096);
    CHECK_FALSE(dec.decode(src, sink));
    CHECK_FALSE(dec.outOfMemory());
  }
  SUBCASE("truncated") {
    std::string bytes = imgfix::loadFixture("baseline.jpg");
    bytes.resize(bytes.size() / 4);
    grainsrc::Grained src(bytes, 4096);
    CHECK_FALSE(dec.decode(src, sink));
    CHECK_FALSE(dec.outOfMemory());
  }
  SUBCASE("a decode that succeeds") {
    const std::string bytes = imgfix::loadFixture("tiny_444.jpg");
    grainsrc::Grained src(bytes, 4096);
    CHECK(dec.decode(src, sink));
    CHECK_FALSE(dec.outOfMemory());
  }
}

TEST_CASE("JpegDecoder reports the heap a decode holds") {
  // REPORTED RATHER THAN DOCUMENTED, so the figure in the spec's memory budget
  // cannot drift from the object. The band is the part that surprises.
  const std::string bytes = imgfix::loadFixture("baseline.jpg");
  reader::JpegDecoder dec;
  CHECK(dec.workspaceBytes() == 0);  // nothing read yet

  grainsrc::Grained src(bytes, 4096);
  CollectingSink sink;
  REQUIRE(dec.decode(src, sink));
  // 4:2:0, so the MCU is 16 rows; one band of a 740-wide image is 11,840 bytes,
  // and the rest is TJpgDec's own pool.
  CHECK(dec.workspaceBytes() > 11840);
  CHECK(dec.workspaceBytes() < 20u * 1024u);

  // Scaling shrinks BOTH terms -- the band is fewer rows of fewer pixels -- which
  // is why a scaled decode is affordable in a way a full-scale one is not.
  grainsrc::Grained half(bytes, 4096);
  CollectingSink halfSink;
  reader::JpegDecoder halfDec;
  REQUIRE(halfDec.decode(half, halfSink, 300, 400));
  CHECK(halfDec.workspaceBytes() < dec.workspaceBytes() / 2);
}
