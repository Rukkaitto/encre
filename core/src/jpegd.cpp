#include "reader/jpegd.h"

#include <cstring>
#include <new>

extern "C" {
#include "tjpgd.h"
}

namespace reader {
namespace {

// TJpgDec's whole working set, which it sub-allocates from and never grows.
// DERIVED rather than copied from upstream's docs, which quote "up to 3,092
// bytes" without saying which configuration that is:
//
//   input buffer      JD_SZBUF                                              512
//   Huffman, 2 table ids x DC and AC:
//     AC  16 bits + 256 codes x 2 + 256 data                       2 x 784 = 1568
//     DC  16 bits +  16 codes x 2 +  16 data                       2 x  64 =  128
//   quantiser tables  4 ids x 64 x int32_t                                 1024
//   workbuf           msx*msy * 64 * 2 + 64, msx*msy <= 4                    576
//   mcubuf            (msx*msy + 2) * 64 * sizeof(jd_yuv_t), which is 1 byte
//                     at JD_FASTDECODE 0 and 2 at 1                         384
//                                                              ---------------
//                                                                         4,192
//
// Every one of those is the worst case the library will ACCEPT rather than what
// a cover uses. 4,608 leaves ~400 bytes over it, which is there because
// alloc_pool never reuses a block: a file that redefines a quantiser table
// costs its 256 bytes twice. Past that the answer is JDR_MEM1 -- a refusal with
// a sentence, not a wrong picture.
//
// It is small beside the band buffer either way (8-17 KB), which is why this is
// stated once and not tuned.
constexpr size_t kPoolBytes = 4608;

// The nullptr-buffer form of the input callback discards through this. 64 bytes
// of frame, on a device whose loop task has 16 KB and where a 6,608-byte frame
// has already panicked it once.
constexpr size_t kSkipChunk = 64;

}  // namespace

// THE BAND IS THE WHOLE OF THIS CLASS.
//
// TJpgDec's jd_decomp walks MCUs left to right across a band and only then moves
// down (its own loop order, tjpgd.c), and hands each one to outfunc as a
// rectangle. So no row is finished until its band's last rectangle has arrived,
// and this object holds exactly one band -- filled rectangle by rectangle, then
// emitted a row at a time when the FIRST rectangle of the next band shows up.
struct JpegDecoder::Impl {
  JDEC jd{};
  uint8_t* pool = nullptr;
  uint8_t* band = nullptr;

  ByteSource* src = nullptr;
  ImageRowSink* sink = nullptr;

  int srcW = 0, srcH = 0;
  int outW = 0, outH = 0;
  uint8_t shift = 0;      // TJpgDec's own 0..3; the divisor is 1 << shift
  int bandRows = 0;       // output rows one full band holds
  int bandTop = -1;       // output y of the band in hand; -1 for none
  int bandFilled = 0;     // rows of it a rectangle has actually written
  int rowsEmitted = 0;

  size_t workspace = 0;
  bool sinkStopped = false;
  bool aborted = false;
  const char* reason = nullptr;

  // The decode proper. decode() owns the allocations and frees them whichever way
  // this answers, which is why the two are separate: every refusal below is an
  // early return, and a single exit that also had to free would be the shape this
  // project keeps finding stale guards in.
  bool run(int atLeastW, int atLeastH);

  void reset() {
    std::memset(&jd, 0, sizeof jd);
    srcW = srcH = outW = outH = 0;
    shift = 0;
    bandRows = 0;
    bandTop = -1;
    bandFilled = 0;
    rowsEmitted = 0;
    workspace = 0;
    sinkStopped = false;
    aborted = false;
    reason = nullptr;
  }

  bool fail(const char* why) {
    if (reason == nullptr) reason = why;
    return false;
  }

  // Push the band in hand to the sink. False means the sink asked to stop, or a
  // rectangle wrote somewhere it should not have -- `sinkStopped` is what tells
  // decode() which, because only one of the two is the caller's own doing.
  bool flushBand() {
    if (bandTop < 0) return true;
    for (int y = 0; y < bandFilled; ++y) {
      if (!sink->row(band + static_cast<size_t>(y) * outW)) {
        sinkStopped = true;
        return false;
      }
      ++rowsEmitted;
    }
    bandTop = -1;
    bandFilled = 0;
    return true;
  }

  // JPEG bytes from the ByteSource.
  //
  // A SHORT READ IS NOT AN ERROR HERE and a partial one is not an answer:
  // jd_prepare tests `infunc(...) != len` for every segment it loads, so a
  // ByteSource handing back one byte at a time -- which is the case the tests
  // run -- must be drained in a loop rather than reported as the stream ending.
  static size_t feed(JDEC* jd, uint8_t* buff, size_t nbyte) {
    auto* im = static_cast<Impl*>(jd->device);
    size_t got = 0;
    if (buff != nullptr) {
      while (got < nbyte) {
        const size_t n = im->src->read(buff + got, nbyte - got);
        if (n == 0) break;
        got += n;
      }
      return got;
    }
    // A NULL BUFFER MEANS SKIP, NOT READ. TJpgDec is discarding a segment it has
    // no use for (a comment, an Exif block), and it still counts the bytes:
    // returning 0 here tells it the stream ended in the middle of the headers.
    uint8_t drop[kSkipChunk];
    while (got < nbyte) {
      size_t want = nbyte - got;
      if (want > sizeof drop) want = sizeof drop;
      const size_t n = im->src->read(drop, want);
      if (n == 0) break;
      got += n;
    }
    return got;
  }

  // One decoded MCU, in OUTPUT coordinates -- mcu_output has already applied the
  // scale to both the origin and the size, so nothing here shifts anything.
  static int emit(JDEC* jd, void* bitmap, JRECT* rect) {
    auto* im = static_cast<Impl*>(jd->device);
    const int left = static_cast<int>(rect->left);
    const int top = static_cast<int>(rect->top);
    const int w = static_cast<int>(rect->right) - left + 1;
    const int h = static_cast<int>(rect->bottom) - top + 1;

    if (top != im->bandTop) {
      // The first rectangle of a new band, which is the only signal there is that
      // the last one is complete.
      if (!im->flushBand()) return 0;
      im->bandTop = top;
      im->bandFilled = 0;
    }
    const int rowOff = top - im->bandTop;

    // These are bytes off somebody's card by way of a library this project did
    // not write, and the destination is a heap buffer: a rectangle that does not
    // fit the band is refused rather than trusted.
    if (w <= 0 || h <= 0 || left < 0 || rowOff < 0 || left + w > im->outW ||
        rowOff + h > im->bandRows) {
      im->fail("the JPEG decoder emitted a block outside the image");
      return 0;
    }

    const auto* s = static_cast<const uint8_t*>(bitmap);
    for (int y = 0; y < h; ++y) {
      std::memcpy(im->band + static_cast<size_t>(rowOff + y) * im->outW + left,
                  s + static_cast<size_t>(y) * w, static_cast<size_t>(w));
    }
    // HOW MANY ROWS THE BAND HAS IS THE DATA'S ANSWER, NOT ARITHMETIC. The bottom
    // band is short -- TJpgDec clips at the image edge -- and at a scale that
    // rounds its remainder to nothing it is dropped altogether, so deriving the
    // count from outH would be a second spelling of a rule the library already
    // applies. rowsEmitted is checked against outH once, at the end.
    if (rowOff + h > im->bandFilled) im->bandFilled = rowOff + h;
    return 1;
  }
};

bool JpegDecoder::Impl::run(int atLeastW, int atLeastH) {
  JRESULT rc = jd_prepare(&jd, &Impl::feed, pool, kPoolBytes, this);
  if (rc != JDR_OK) {
    switch (rc) {
      case JDR_INP:
        return fail("the cover is not a JPEG, or ends before its header does");
      case JDR_MEM1:
        return fail("the JPEG's tables do not fit the decoder's work pool");
      case JDR_MEM2:
        return fail("the JPEG has a header segment larger than the decoder accepts");
      case JDR_FMT1:
        return fail("the JPEG's header is malformed");
      case JDR_FMT3:
        // The one refusal that is about a whole CLASS of file rather than a
        // broken one, and so the one worth naming exactly: jd_prepare answers
        // FMT3 for a progressive or arithmetic-coded stream, for a component
        // count that is neither grey nor YCbCr, and for chroma sampling outside
        // 4:4:4 / 4:2:2 / 4:2:0.
        return fail(
            "this JPEG is progressive or otherwise not baseline, which this decoder does not read");
      default:
        return fail("the JPEG's header could not be read");
    }
  }

  srcW = jd.width;
  srcH = jd.height;

  // THE LARGEST DIVISOR THAT STILL CLEARS THE REQUEST. Both dimensions shrink
  // monotonically, so the first shift that fails ends the search. 0 on an axis is
  // "no requirement there", and 0 on both is full scale -- otherwise every cover
  // would come back at 1/8, since everything is at least 0 wide.
  if (atLeastW > 0 || atLeastH > 0) {
    for (uint8_t s = 1; s <= 3; ++s) {
      const int w = srcW >> s, h = srcH >> s;
      if (w < 1 || h < 1 || w < atLeastW || h < atLeastH) break;
      shift = s;
    }
  }
  outW = srcW >> shift;
  outH = srcH >> shift;
  if (outW < 1 || outH < 1) return fail("the JPEG has no pixels");

  // The band cannot be sized before now: it needs the sampling factors, which
  // only the SOF says, and the scale, which is chosen from them.
  bandRows = (jd.msy * 8) >> shift;
  if (bandRows < 1) return fail("the JPEG's sampling factors make no sense");
  const size_t bandBytes = static_cast<size_t>(bandRows) * outW;
  // Value-initialised, so a band with a hole in it -- which the geometry says
  // cannot happen -- reads as a black stripe every time rather than as whatever
  // the heap last held.
  band = new (std::nothrow) uint8_t[bandBytes]();
  if (band == nullptr) return fail("no memory for a row band of this cover");
  workspace = kPoolBytes + bandBytes;

  if (!sink->begin(outW, outH)) {
    // The same outcome as a sink that stops mid-picture, and it has to be: a
    // caller cannot be asked to tell "you refused" from "the file is bad" by
    // which of two ways it was refused.
    sinkStopped = aborted = true;
    return false;
  }

  rc = jd_decomp(&jd, &Impl::emit, shift);
  if (rc == JDR_OK && !flushBand()) rc = JDR_INTR;  // the last band
  switch (rc) {
    case JDR_OK:
      // The band accounting and the dimensions the sink was given have to agree:
      // begin() promised outH rows, and a band phase off by one delivers a
      // different number and still looks like a picture.
      //
      // NO TEST REACHES THIS BRANCH, and that is written down rather than left to
      // be rediscovered: deleting it fails nothing, because the geometry makes it
      // unreachable for every sampling factor and scale the library accepts.
      // What it is for is the DEVICE, where there is no oracle to compare
      // against -- a mutation that miscounts a band is caught here as a refusal
      // with a sentence instead of as a cover that is subtly wrong. (Proved by
      // mutation: the band-count mutation this defends against is ALSO caught by
      // the row-count assertions in test_jpegd.cpp, so the two are independent
      // rather than one covering for the other.)
      if (rowsEmitted != outH) {
        return fail("the JPEG decoded to fewer rows than its header declares");
      }
      return true;
    case JDR_INTR:
      // Either the sink said stop, or emit() refused a rectangle -- and only the
      // first is an abort. The second has already left a reason.
      aborted = sinkStopped;
      if (!aborted) return fail("the JPEG decoder was interrupted");
      return false;
    case JDR_INP:
      return fail("the JPEG's image data ends before the picture does");
    case JDR_FMT1:
      return fail("the JPEG's image data is malformed");
    default:
      return fail("the JPEG's image data could not be decoded");
  }
}

JpegDecoder::JpegDecoder() : impl_(new (std::nothrow) Impl()) {}
JpegDecoder::~JpegDecoder() = default;

bool JpegDecoder::decode(ByteSource& src, ImageRowSink& sink, int atLeastW, int atLeastH) {
  if (impl_ == nullptr) return false;  // the constructor's allocation failed
  Impl& im = *impl_;
  im.reset();
  im.src = &src;
  im.sink = &sink;

  // -fno-exceptions makes a throwing new an abort() with no diagnostic, which
  // this project has twice had reported to it as "opening a book goes back to
  // Home". Every allocation on this path is nothrow and answered.
  im.pool = new (std::nothrow) uint8_t[kPoolBytes];
  const bool ok = im.pool != nullptr ? im.run(atLeastW, atLeastH)
                                     : im.fail("no memory for the JPEG decoder's tables");

  // NOTHING IS HELD BETWEEN DECODES. The band is the largest single allocation in
  // this class and a JpegDecoder outlives the picture it decoded by as long as
  // the screen is up. The reported figures survive, which is all workspaceBytes()
  // ever needed.
  delete[] im.band;
  im.band = nullptr;
  delete[] im.pool;
  im.pool = nullptr;
  im.src = nullptr;
  im.sink = nullptr;
  return ok;
}

int JpegDecoder::sourceWidth() const { return impl_ ? impl_->srcW : 0; }
int JpegDecoder::sourceHeight() const { return impl_ ? impl_->srcH : 0; }
int JpegDecoder::scaleDivisor() const { return impl_ ? (1 << impl_->shift) : 1; }
bool JpegDecoder::aborted() const { return impl_ && impl_->aborted; }
const char* JpegDecoder::reason() const { return impl_ ? impl_->reason : "no memory for a JPEG decoder"; }
size_t JpegDecoder::workspaceBytes() const { return impl_ ? impl_->workspace : 0; }

}  // namespace reader
