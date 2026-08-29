// decodeCover: a book on the card to two 1-bit plane rows at a time.
//
// THIS IS THE ONLY TEST THAT SEES THE WHOLE CHAIN -- the zip, the optional
// inflate, the sniff, a real decoder, the fitter, the plane sink. Each layer
// below has its own file and its own oracle; what can only be wrong HERE is the
// joining: the wrong decoder for the bytes, a fit box computed from the wrong
// dimensions, a padding row missing, an abandoned decode reported as a card
// fault.
//
// THE COVERS ARE REAL IMAGES, not synthesised ones, for the reason the decoder
// tests give: a fixture drawn to be easy to decode is a fixture that cannot fail
// the way a book on a card fails.
#include <cstdint>
#include <string>
#include <vector>

#include "doctest.h"
#include "epub_builder.h"
#include "fake_fs.h"
#include "image_fixtures.h"
#include "reader/cover.h"

namespace {

// Accumulates every plane row. A TEST may hold the whole panel; the FIRMWARE may
// not, which is exactly why decodeCover pushes rows instead of returning planes.
struct VectorSink : reader::CoverPlaneSink {
  int w = 0, h = 0, bytes = 0, rows = 0, declaredRows = 0, begins = 0;
  bool finished = false, finishedOk = false;
  int finishes = 0;
  bool acceptBegin = true;
  int refuseRowAt = -1;  // -1 never refuses
  std::vector<uint8_t> msb, lsb;

  bool begin(int panelW, int panelH, int planeRowBytes, int r) override {
    w = panelW;
    h = panelH;
    bytes = planeRowBytes;
    declaredRows = r;
    ++begins;
    return acceptBegin;
  }
  bool row(const uint8_t* m, const uint8_t* l) override {
    msb.insert(msb.end(), m, m + bytes);
    lsb.insert(lsb.end(), l, l + bytes);
    ++rows;
    return refuseRowAt < 0 || rows < refuseRowAt;
  }
  bool finish(bool ok) override {
    finished = true;
    finishedOk = ok;
    ++finishes;
    return true;
  }

  // A row of the accumulated MSB plane. Empty if that row was never pushed.
  const uint8_t* msbRow(int y) const {
    return static_cast<size_t>((y + 1) * bytes) <= msb.size()
               ? msb.data() + static_cast<size_t>(y) * bytes
               : nullptr;
  }
};

// PAPER IS 0xFF IN BOTH PLANES and ink is a CLEARED bit -- framebuffer.h's
// convention is "true/1 = white". So "did anything decode" is "is any byte not
// 0xFF"; asking for a non-zero byte instead answers yes for a blank panel and no
// for an all-black cover, which is backwards in both directions.
bool anyInk(const std::vector<uint8_t>& plane) {
  for (uint8_t b : plane)
    if (b != 0xFFu) return true;
  return false;
}

bool rowIsPaper(const VectorSink& s, int y) {
  const uint8_t* r = s.msbRow(y);
  if (r == nullptr) return false;
  for (int i = 0; i < s.bytes; ++i)
    if (r[i] != 0xFFu) return false;
  return true;
}

void putBook(FakeFileSystem& fs, const std::string& bytes) {
  REQUIRE(fs.writeAll("/books/book.epub", bytes));
}

reader::OpenedBook openIt(FakeFileSystem& fs) {
  reader::OpenedBook book;
  const char* why = "";
  REQUIRE_MESSAGE(reader::openBook(fs, "/books/book.epub", book, &why), std::string(why));
  return book;
}

}  // namespace

TEST_CASE("decodeCover turns a real JPEG cover into a full panel of plane rows") {
  FakeFileSystem fs;
  putBook(fs, epubbuild::withCoverImage(imgfix::loadFixture("baseline.jpg")));
  const reader::OpenedBook book = openIt(fs);

  VectorSink sink;
  reader::CoverReport rep;
  const reader::CoverResult r =
      reader::decodeCover(fs, book, 480, 800, reader::CoverFit::Fill, sink, nullptr,
                          nullptr, &rep);
  CHECK(r == reader::CoverResult::Ok);
  CHECK(rep.reason == nullptr);
  CHECK(rep.sourceWidth == 740);
  CHECK(rep.sourceHeight == 1000);
  // NOT SCALED, and that is the request being honoured rather than overshot:
  // 740x1000 halved is 370x500, which is under the 480x800 asked for.
  CHECK(rep.scaleDivisor == 1);
  // A Fill of a cover bigger than the panel leaves NO band on either axis.
  CHECK(rep.dstX == 0);
  CHECK(rep.dstY == 0);
  CHECK(rep.dstW == 480);
  CHECK(rep.dstH == 800);
  CHECK(sink.begins == 1);
  CHECK(sink.w == 480);
  CHECK(sink.h == 800);
  CHECK(sink.bytes == 60);
  CHECK(sink.rows == 800);
  CHECK(sink.declaredRows == 800);
  CHECK(sink.finishes == 1);
  CHECK(sink.finishedOk);
  // The planes are a physical framebuffer store, byte for byte: one plane is
  // exactly what Framebuffer::sizeBytes() reports for this panel.
  CHECK(sink.msb.size() == 60u * 800u);
  CHECK(sink.lsb.size() == 60u * 800u);
  // Not a blank plane: a cover that decoded to nothing would satisfy every count
  // above and put white on the glass.
  CHECK(anyInk(sink.msb));
  CHECK(anyInk(sink.lsb));
}

TEST_CASE("the same cover DEFLATED inside the zip decodes identically") {
  // 59% of the corpus's JPEG covers are deflated, so this is the majority path
  // and the one that puts the zip's inflate window UNDER the image decoder.
  const std::string jpeg = imgfix::loadFixture("baseline.jpg");

  FakeFileSystem storedFs;
  putBook(storedFs, epubbuild::withCoverImage(jpeg));
  VectorSink stored;
  REQUIRE(reader::decodeCover(storedFs, openIt(storedFs), 480, 800, reader::CoverFit::Fill,
                              stored) == reader::CoverResult::Ok);

  FakeFileSystem deflatedFs;
  putBook(deflatedFs, epubbuild::withDeflatedCoverImage(jpeg));
  const reader::OpenedBook book = openIt(deflatedFs);
  // The fixture really is compressed, or this test is the stored one twice.
  REQUIRE(book.cover.deflated);

  VectorSink deflated;
  reader::CoverReport rep;
  CHECK(reader::decodeCover(deflatedFs, book, 480, 800, reader::CoverFit::Fill, deflated,
                            nullptr, nullptr, &rep) == reader::CoverResult::Ok);
  CHECK(rep.reason == nullptr);
  CHECK(deflated.rows == 800);
  // ONE ASSERTION OVER THE WHOLE PANEL, not one per row: doctest prints every
  // CHECK and 1,600 of them would bury a passing run.
  CHECK(deflated.msb == stored.msb);
  CHECK(deflated.lsb == stored.lsb);
}

TEST_CASE("a PNG cover goes down the other decoder and fills the same panel") {
  FakeFileSystem fs;
  putBook(fs, epubbuild::withCoverImage(imgfix::loadFixture("truecolour.png")));
  const reader::OpenedBook book = openIt(fs);

  VectorSink sink;
  reader::CoverReport rep;
  CHECK(reader::decodeCover(fs, book, 480, 800, reader::CoverFit::Fill, sink, nullptr,
                            nullptr, &rep) == reader::CoverResult::Ok);
  CHECK(rep.reason == nullptr);
  CHECK(sink.rows == 800);
  CHECK(anyInk(sink.msb));
  // PNG HAS NO IDCT LEVER, so a 1600x2400 cover is inflated and unfiltered whole
  // whatever the panel wants of it, and the dimensions are the file's own.
  CHECK(rep.sourceWidth == 1600);
  CHECK(rep.sourceHeight == 2400);
  CHECK(rep.scaleDivisor == 1);
}

TEST_CASE("the cover's BYTES pick the decoder, not the manifest's media type") {
  // Every cover fixture declares `media-type="image/jpeg"`. This one holds a PNG,
  // and Task 5 deliberately applied no media-type filter -- so what has to be
  // right is the sniff.
  FakeFileSystem fs;
  putBook(fs, epubbuild::withCoverImage(imgfix::loadFixture("grey8.png")));
  VectorSink sink;
  CHECK(reader::decodeCover(fs, openIt(fs), 480, 800, reader::CoverFit::Whole, sink) ==
        reader::CoverResult::Ok);
  CHECK(anyInk(sink.msb));
}

TEST_CASE("a span that is not an image at all is Unsupported, with a reason") {
  // The OPF's cover pointer can name anything -- an XHTML cover PAGE is the
  // common real mistake. epubbuild's default cover bytes are prose.
  FakeFileSystem fs;
  putBook(fs, epubbuild::withCoverMetaTag());
  VectorSink sink;
  reader::CoverReport rep;
  CHECK(reader::decodeCover(fs, openIt(fs), 480, 800, reader::CoverFit::Fill, sink,
                            nullptr, nullptr, &rep) == reader::CoverResult::Unsupported);
  CHECK(rep.reason != nullptr);
  CHECK(rep.sourceWidth == 0);   // nothing ever said what the picture was
  CHECK(sink.begins == 0);   // no file is opened for something that is not a picture
  CHECK(sink.finishes == 1);
  CHECK_FALSE(sink.finishedOk);
}

TEST_CASE("a book with no cover is NoCover, and the sink is never begun") {
  FakeFileSystem fs;
  putBook(fs, epubbuild::minimalEpub());
  const reader::OpenedBook book = openIt(fs);

  VectorSink sink;
  reader::CoverReport rep;
  CHECK(reader::decodeCover(fs, book, 480, 800, reader::CoverFit::Fill, sink, nullptr,
                            nullptr, &rep) == reader::CoverResult::NoCover);
  CHECK(sink.rows == 0);
  CHECK(sink.begins == 0);
  CHECK(sink.w == 0);
  CHECK(rep.reason != nullptr);
  // finish() IS still called, and that is the contract: it is the one call a sink
  // is guaranteed, so a stale file from another book can be dropped on the way
  // past rather than needing a caller to remember.
  CHECK(sink.finishes == 1);
  CHECK_FALSE(sink.finishedOk);
}

TEST_CASE("a cover the manifest declares and the archive does not hold is NoCover") {
  FakeFileSystem fs;
  putBook(fs, epubbuild::withCoverDeclaredButAbsent());
  VectorSink sink;
  CHECK(reader::decodeCover(fs, openIt(fs), 480, 800, reader::CoverFit::Fill, sink) ==
        reader::CoverResult::NoCover);
  CHECK(sink.begins == 0);
}

TEST_CASE("a progressive JPEG is Unsupported, not garbage") {
  FakeFileSystem fs;
  putBook(fs, epubbuild::withCoverImage(imgfix::loadFixture("progressive.jpg")));
  const reader::OpenedBook book = openIt(fs);

  VectorSink sink;
  reader::CoverReport rep;
  CHECK(reader::decodeCover(fs, book, 480, 800, reader::CoverFit::Fill, sink, nullptr,
                            nullptr, &rep) == reader::CoverResult::Unsupported);
  CHECK(rep.reason != nullptr);
  // REFUSED BEFORE THE HEADERS, which is exactly what makes it Unsupported rather
  // than ReadFailed: jd_prepare answers FMT3 for a progressive stream, so nothing
  // ever said what the picture was.
  CHECK(rep.sourceWidth == 0);
  CHECK(sink.begins == 0);
  CHECK(sink.finished);
  CHECK_FALSE(sink.finishedOk);  // the sink must be told, so it can refuse to leave a file
}

TEST_CASE("a truncated cover entry is ReadFailed, not Unsupported") {
  // The picture DECLARED itself and then ran out, which is a fault in the bytes
  // rather than a format we do not read -- and the two want different words in a
  // log line. The split is "was the sink ever begun", which is exactly "did the
  // headers parse".
  const std::string jpeg = imgfix::loadFixture("baseline.jpg");
  FakeFileSystem fs;
  putBook(fs, epubbuild::withCoverImage(jpeg.substr(0, jpeg.size() / 4)));
  VectorSink sink;
  reader::CoverReport rep;
  CHECK(reader::decodeCover(fs, openIt(fs), 480, 800, reader::CoverFit::Fill, sink,
                            nullptr, nullptr, &rep) == reader::CoverResult::ReadFailed);
  CHECK(rep.reason != nullptr);
  CHECK(rep.sourceWidth == 740);   // the headers parsed; the data ran out
  CHECK(sink.begins == 1);
  CHECK(sink.rows < 800);
  CHECK_FALSE(sink.finishedOk);
}

TEST_CASE("Whole letterboxes with PAPER ROWS, and the panel is still filled") {
  FakeFileSystem fs;
  putBook(fs, epubbuild::withCoverImage(imgfix::loadFixture("baseline.jpg")));

  VectorSink sink;
  reader::CoverReport rep;
  REQUIRE(reader::decodeCover(fs, openIt(fs), 480, 800, reader::CoverFit::Whole, sink,
                              nullptr, nullptr, &rep) == reader::CoverResult::Ok);
  // 740x1000 contained in 480x800 is 480x649, centred at y=75. Stated as numbers
  // rather than re-derived with fitCover, which would be the same arithmetic
  // agreeing with itself.
  REQUIRE(rep.dstH == 649);
  REQUIRE(rep.dstY == 75);
  REQUIRE(rep.dstW == 480);
  CHECK(sink.rows == 800);       // the bands are PUSHED, not skipped

  int paperAbove = 0, paperBelow = 0;
  for (int y = 0; y < rep.dstY; ++y) paperAbove += rowIsPaper(sink, y) ? 1 : 0;
  for (int y = rep.dstY + rep.dstH; y < 800; ++y) paperBelow += rowIsPaper(sink, y) ? 1 : 0;
  CHECK(paperAbove == rep.dstY);
  CHECK(paperBelow == 800 - rep.dstY - rep.dstH);

  // And the middle is not paper, or the two counts above would be satisfied by a
  // blank panel.
  int inked = 0;
  for (int y = rep.dstY; y < rep.dstY + rep.dstH; ++y) inked += rowIsPaper(sink, y) ? 0 : 1;
  CHECK(inked > rep.dstH / 2);
}

TEST_CASE("a cover smaller than the panel is centred, never enlarged") {
  // tiny_444.jpg is 33x9. fitCover refuses to upscale, so Whole degrades to 1:1
  // and almost the whole panel is band -- which is the case that proves the
  // padding is computed from the BOX and not from a Fill-shaped assumption.
  FakeFileSystem fs;
  putBook(fs, epubbuild::withCoverImage(imgfix::loadFixture("tiny_444.jpg")));

  VectorSink sink;
  reader::CoverReport rep;
  REQUIRE(reader::decodeCover(fs, openIt(fs), 480, 800, reader::CoverFit::Whole, sink,
                              nullptr, nullptr, &rep) == reader::CoverResult::Ok);
  CHECK(rep.sourceWidth == 33);
  CHECK(rep.sourceHeight == 9);
  REQUIRE(rep.dstW == 33);
  REQUIRE(rep.dstH == 9);
  REQUIRE(rep.dstX == 223);
  REQUIRE(rep.dstY == 395);
  CHECK(sink.rows == 800);
  CHECK(rowIsPaper(sink, rep.dstY - 1));
  CHECK(rowIsPaper(sink, rep.dstY + rep.dstH));
  CHECK(rowIsPaper(sink, 0));
  CHECK(rowIsPaper(sink, 799));
  CHECK_FALSE(rowIsPaper(sink, rep.dstY + rep.dstH / 2));
}

TEST_CASE("a cover much larger than the panel is decoded at a smaller scale") {
  // THE ONE LEVER THAT CHANGES NO OUTPUT GEOMETRY. TJpgDec halves out of the IDCT
  // for free, so asking it for the panel rather than for everything is a quarter
  // of the work per halving -- and a request that asked for too little, or for
  // nothing at all, would produce a panel of exactly the same shape. Only the
  // divisor can see it.
  FakeFileSystem fs;
  putBook(fs, epubbuild::withCoverImage(imgfix::loadFixture("baseline.jpg")));
  const reader::OpenedBook book = openIt(fs);

  VectorSink sink;
  reader::CoverReport rep;
  // 740x1000 for a 200x300 panel: 1/2 is 370x500 and clears it, 1/4 is 185x250
  // and does not. So the answer is 2, and it is 2 in BOTH directions -- asking
  // for nothing would give 1 and asking for half the panel would give 4.
  REQUIRE(reader::decodeCover(fs, book, 200, 300, reader::CoverFit::Fill, sink, nullptr,
                              nullptr, &rep) == reader::CoverResult::Ok);
  CHECK(rep.scaleDivisor == 2);
  CHECK(rep.sourceWidth == 740);      // what the FILE said, not what was decoded
  CHECK(rep.sourceHeight == 1000);
  // AND THE SCALED SOURCE IS STILL BIG ENOUGH TO FILL THE PANEL, which is the
  // property the request exists to guarantee: a divisor one step too far would
  // letterbox a Fill.
  CHECK(rep.dstW == 200);
  CHECK(rep.dstH == 300);
  CHECK(sink.rows == 300);
  CHECK(anyInk(sink.msb));
}

TEST_CASE("a panel width that is not a multiple of eight still packs whole bytes") {
  FakeFileSystem fs;
  putBook(fs, epubbuild::withCoverImage(imgfix::loadFixture("baseline.jpg")));
  VectorSink sink;
  REQUIRE(reader::decodeCover(fs, openIt(fs), 530, 300, reader::CoverFit::Fill, sink) ==
          reader::CoverResult::Ok);
  CHECK(sink.bytes == 67);
  CHECK(sink.rows == 300);
  CHECK(sink.msb.size() == 67u * 300u);
}

TEST_CASE("the stop predicate abandons the decode and the sink is told") {
  FakeFileSystem fs;
  putBook(fs, epubbuild::withCoverImage(imgfix::loadFixture("baseline.jpg")));
  const reader::OpenedBook book = openIt(fs);

  int calls = 0;
  VectorSink sink;
  reader::CoverReport rep;
  const reader::CoverResult r = reader::decodeCover(
      fs, book, 480, 800, reader::CoverFit::Fill, sink,
      [](void* ctx) { return ++*static_cast<int*>(ctx) > 5; }, &calls, &rep);
  CHECK(r == reader::CoverResult::Abandoned);
  CHECK(calls == 6);
  CHECK(sink.rows < 800);
  CHECK(sink.finishes == 1);
  CHECK_FALSE(sink.finishedOk);
  // AN ABANDONED DECODE HAS NOTHING TO SAY, and reporting one as a card fault is
  // what would put a wrong reason in the log.
  CHECK(rep.reason == nullptr);
}

TEST_CASE("a stop predicate that never says stop costs the decode nothing") {
  FakeFileSystem fs;
  putBook(fs, epubbuild::withCoverImage(imgfix::loadFixture("baseline.jpg")));

  int calls = 0;
  VectorSink sink;
  CHECK(reader::decodeCover(
            fs, openIt(fs), 480, 800, reader::CoverFit::Fill, sink,
            [](void* ctx) {
              ++*static_cast<int*>(ctx);
              return false;
            },
            &calls) == reader::CoverResult::Ok);
  CHECK(sink.rows == 800);
  // ASKED EVERY 8 SOURCE ROWS, over the 1000 rows of this cover -- not per row,
  // which is 8x the calls for latency nobody can feel, and not per band, which
  // would depend on the JPEG's sampling factors.
  CHECK(calls == 125);
}

TEST_CASE("a sink that refuses at begin stops before a row is decoded") {
  FakeFileSystem fs;
  putBook(fs, epubbuild::withCoverImage(imgfix::loadFixture("baseline.jpg")));
  VectorSink sink;
  sink.acceptBegin = false;
  CHECK(reader::decodeCover(fs, openIt(fs), 480, 800, reader::CoverFit::Fill, sink) !=
        reader::CoverResult::Ok);
  CHECK(sink.begins == 1);
  CHECK(sink.rows == 0);
  CHECK(sink.finishes == 1);
  CHECK_FALSE(sink.finishedOk);
}

TEST_CASE("a sink that refuses a row part-way through stops the decode") {
  FakeFileSystem fs;
  putBook(fs, epubbuild::withCoverImage(imgfix::loadFixture("baseline.jpg")));
  VectorSink sink;
  sink.refuseRowAt = 100;
  CHECK(reader::decodeCover(fs, openIt(fs), 480, 800, reader::CoverFit::Fill, sink) !=
        reader::CoverResult::Ok);
  CHECK(sink.rows == 100);
  CHECK_FALSE(sink.finishedOk);
}

TEST_CASE("a card that will not open the book is ReadFailed") {
  FakeFileSystem fs;
  putBook(fs, epubbuild::withCoverImage(imgfix::loadFixture("baseline.jpg")));
  const reader::OpenedBook book = openIt(fs);
  fs.setMounted(false);   // the card left the slot between the open and the sleep

  VectorSink sink;
  reader::CoverReport rep;
  CHECK(reader::decodeCover(fs, book, 480, 800, reader::CoverFit::Fill, sink, nullptr,
                            nullptr, &rep) == reader::CoverResult::ReadFailed);
  CHECK(rep.reason != nullptr);
  CHECK(sink.begins == 0);
  CHECK(sink.finishes == 1);
}

TEST_CASE("a nonsense panel is refused before the card is touched") {
  // A caller-side error rather than anything about the book, and the six results
  // have no name for one -- so it borrows the nearest and the reason carries the
  // truth. What matters is that it refuses HERE: reaching CoverFitter with a zero
  // panel would report OutOfMemory, which is a wrong word in a log line, and
  // reaching the packing arithmetic with one would be worse than a wrong word.
  FakeFileSystem fs;
  putBook(fs, epubbuild::withCoverImage(imgfix::loadFixture("baseline.jpg")));
  VectorSink sink;
  reader::CoverReport rep;
  CHECK(reader::decodeCover(fs, openIt(fs), 0, 800, reader::CoverFit::Fill, sink, nullptr,
                            nullptr, &rep) == reader::CoverResult::Unsupported);
  CHECK(rep.reason != nullptr);
  CHECK(sink.begins == 0);
  CHECK(sink.finishes == 1);
}

TEST_CASE("every result has a name") {
  using reader::CoverResult;
  const CoverResult all[] = {CoverResult::Ok,         CoverResult::NoCover,
                             CoverResult::Unsupported, CoverResult::ReadFailed,
                             CoverResult::OutOfMemory, CoverResult::Abandoned};
  for (CoverResult r : all) {
    CAPTURE(static_cast<int>(r));
    const char* n = reader::coverResultName(r);
    REQUIRE(n != nullptr);
    CHECK(n[0] != '\0');
  }
  CHECK(std::string(reader::coverResultName(CoverResult::Ok)) == "Ok");
  CHECK(std::string(reader::coverResultName(CoverResult::Abandoned)) == "Abandoned");
}
