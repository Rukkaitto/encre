// NOT core/src/png.cpp, WHICH IS ONE LETTER AWAY AND THE OPPOSITE BUILD.
// png.cpp is the desktop-only golden writer and differ (stb, <filesystem>-free
// but READER_DESKTOP-guarded); THIS file is the device decoder and ships in the
// firmware. core/library.json excludes `png.cpp` by exact name -- PlatformIO's
// srcFilter is fnmatch and the pattern carries no wildcard, so `pngd.cpp` is not
// caught by it. Confirmed by building for the ESP32-C3.
#include "reader/pngd.h"

#include <cstring>
#include <new>

namespace reader {
namespace {

constexpr uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

// A chunk length is a 31-bit number by the spec, so the high bit set is a
// malformed file rather than a very large chunk. It bounds an image's width and
// height too, which is what keeps `width * channels` inside 64 bits below.
constexpr uint32_t kMaxChunkLen = 0x7FFFFFFFu;

// The nullptr-buffer form of skipping. 64 bytes of frame, on a device whose loop
// task has 16 KB and where a 6,608-byte frame has already panicked it once.
constexpr size_t kSkipChunk = 64;

// FILL `n` BYTES OR SAY THE SOURCE ENDED, over any ByteSource.
//
// A SHORT READ IS NOT THE END OF THE INPUT: the contract says 0 means ended and
// anything else is a partial answer, so every fixed-size read has to loop. The
// grain-1 tests are what prove it and every real card produces short reads at a
// sector boundary. This was three copies -- the chunk walk over the file, the
// scanline reads over the inflater, and the zlib wrapper over the IDAT stream --
// TWO OF WHICH CARRIED A PARAGRAPH STATING THIS SAME RULE, which is this
// project's own signature for a primitive not yet extracted.
//
// It reaches all three because they are all ByteSources: `src` is the file,
// `InflateSource` is the decompressed stream, and `IdatSource` is the
// concatenated IDAT payloads. Note InflateSource happens to fill a request
// completely today; this rests on the interface rather than on that.
bool fillFrom(ByteSource& s, void* dst, size_t n) {
  auto* p = static_cast<uint8_t*>(dst);
  size_t got = 0;
  while (got < n) {
    const size_t k = s.read(p + got, n - got);
    if (k == 0) return false;
    got += k;
  }
  return true;
}

uint32_t be32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

bool typeIs(const uint8_t* p, const char* four) {
  return std::memcmp(p, four, 4) == 0;
}

// Bytes per pixel at depth 8, which is also the filter's `bpp` offset.
int channelsFor(uint8_t colourType) {
  switch (colourType) {
    case 0: return 1;  // grey
    case 2: return 3;  // RGB
    case 4: return 2;  // grey + alpha
    case 6: return 4;  // RGBA
    default: return 0;  // 3 is palette, which this decoder refuses
  }
}

// PNG's own predictor (RFC 2083 6.6). Written with ints rather than the spec's
// pseudocode types because every term is a byte and the differences are not.
uint8_t paeth(uint8_t a, uint8_t b, uint8_t c) {
  const int p = static_cast<int>(a) + static_cast<int>(b) - static_cast<int>(c);
  int pa = p - static_cast<int>(a);
  int pb = p - static_cast<int>(b);
  int pc = p - static_cast<int>(c);
  if (pa < 0) pa = -pa;
  if (pb < 0) pb = -pb;
  if (pc < 0) pc = -pc;
  if (pa <= pb && pa <= pc) return a;
  return pb <= pc ? b : c;
}

}  // namespace

// THE CHUNK WALK IS DRIVEN BY THE INFLATER'S APPETITE, not by a pass over the
// file. `IdatSource` is a ByteSource that hands out the concatenated IDAT
// payloads and nothing else: when one chunk runs out it steps over that chunk's
// CRC, skips whatever ancillary chunks come next by their own length, and
// continues at the following IDAT. IEND or the end of input answers 0, which is
// how the inflater learns the stream is over.
//
// That is what makes multi-IDAT free. A decoder that concatenated the payloads
// into a buffer first would need the whole compressed stream resident -- 50 KB
// for a small cover and unbounded in general -- for no gain over reading them in
// place.
struct PngDecoder::Impl {
  // The IDAT payloads, concatenated, as something the Inflater can pull from.
  // A MEMBER rather than a local in run(), so it outlives every call the
  // Inflater could make on it -- Inflater::begin's own contract asks for that.
  class IdatSource : public ByteSource {
   public:
    explicit IdatSource(Impl& im) : im_(&im) {}
    size_t read(void* dst, size_t bytes) override { return im_->readIdat(dst, bytes); }

   private:
    Impl* im_;
  };

  ByteSource* src = nullptr;
  ImageRowSink* sink = nullptr;

  IdatSource idat{*this};
  Inflater inf;
  InflateSource zs{inf};

  uint8_t* rows = nullptr;  // one block: cur, prev, grey
  uint8_t* cur = nullptr;
  uint8_t* prev = nullptr;
  uint8_t* grey = nullptr;

  int width = 0, height = 0;
  int channels = 0;
  size_t stride = 0;

  uint32_t idatLeft = 0;   // bytes left in the IDAT chunk being read
  bool inChunk = false;    // ...and whether its CRC is still owed
  bool idatDone = false;   // IEND seen, or the input ended

  size_t workspace = 0;
  // NO `sinkStopped` FLAG, unlike jpegd.cpp. There it tells two causes of one
  // JDR_INTR apart; here the sink's false return is the only thing that sets
  // `aborted`, so a second flag would have no reader.
  bool aborted = false;
  const char* reason = nullptr;

  void reset() {
    zs.reset();
    inf.release();
    width = height = channels = 0;
    stride = 0;
    idatLeft = 0;
    inChunk = false;
    idatDone = false;
    workspace = 0;
    aborted = false;
    reason = nullptr;
  }

  // decode() frees the row block on every path, so this is not a live leak --
  // it is what stops the invariant resting on ONE function. reset() does not
  // clear the pointer either, so `~PngDecoder() = default` would leak silently
  // the day decode() grows a path that does not reach its own cleanup.
  ~Impl() { delete[] rows; }

  bool fail(const char* why) {
    if (reason == nullptr) reason = why;
    return false;
  }

  bool skipBytes(uint32_t n) {
    uint8_t drop[kSkipChunk];
    while (n > 0) {
      const size_t want = n < kSkipChunk ? n : kSkipChunk;
      const size_t k = src->read(drop, want);
      if (k == 0) return false;
      n -= static_cast<uint32_t>(k);
    }
    return true;
  }

  // Step to the next IDAT chunk WITH BYTES IN IT. False for IEND, for the end of
  // the input, or for a length the spec does not allow.
  //
  // "WITH BYTES IN IT" IS THE POSTCONDITION AND readIdat RESTS ON IT. A
  // zero-length IDAT is legal PNG and real encoders emit them, so the `continue`
  // below is what makes this true. readIdat used to ALSO loop on the same
  // condition -- two guards for one thing, each making the other unnecessary,
  // which is why deleting either failed no test. One of them is the contract and
  // the other was a second spelling of it.
  bool advanceToIdat() {
    for (;;) {
      if (inChunk) {
        if (!skipBytes(4)) return false;  // the CRC of the chunk just finished
        inChunk = false;
      }
      uint8_t hdr[8];
      if (!fillFrom(*src, hdr, sizeof hdr)) return false;
      const uint32_t len = be32(hdr);
      if (len > kMaxChunkLen) return false;
      if (typeIs(hdr + 4, "IEND")) return false;
      if (typeIs(hdr + 4, "IDAT")) {
        idatLeft = len;
        inChunk = true;
        if (len > 0) return true;
        continue;  // a zero-length IDAT is legal, and has nothing in it
      }
      if (!skipBytes(len) || !skipBytes(4)) return false;
    }
  }

  size_t readIdat(void* dst, size_t want) {
    if (want == 0) return 0;
    // ONE `if`, NOT A LOOP: advanceToIdat answers true only with bytes in hand,
    // so there is nothing to go round for. See its postcondition.
    if (idatLeft == 0) {
      if (idatDone || !advanceToIdat()) {
        idatDone = true;
        return 0;
      }
    }
    const size_t n = want < idatLeft ? want : idatLeft;
    const size_t got = src->read(dst, n);
    idatLeft -= static_cast<uint32_t>(got);
    // The input ended inside a chunk. DELETING THIS LATCH FAILS NO TEST and
    // cannot: without it the next call finds idatLeft still non-zero, reads 0
    // again and answers 0 again, so the outcome is identical and only one read
    // per call is saved. It is kept because that read is a poke at a card on the
    // panel's own SPI bus; it is written down because a clause no test reaches
    // should say so rather than look like coverage.
    if (got == 0) idatDone = true;
    return got;
  }

  // TWO REASONS, TOLD APART BY THE INFLATER. A stream that ENDED is a truncated
  // file; a stream that FAILED is a corrupt one. They ask different things of a
  // user and the log line is where they learn which -- so this is a function
  // rather than the two copies it was, one at the filter byte and one at the row
  // beside it. Two spellings could disagree about which case is which.
  const char* streamEndReason() const {
    const char* e = inf.error();
    return e != nullptr && e[0] != '\0'
               ? "the PNG's compressed image data is corrupt"
               : "the PNG's image data ends before the picture does";
  }

  bool run();
  bool readHeader();
  bool readZlibHeader();
  void unfilter(uint8_t filter);
  void toGrey();
};

// IHDR, and every field of it that decides whether this decoder can read the
// file. Each refusal names the thing rather than saying "unsupported": the log
// line is the only way a user ever learns why a cover did not appear.
bool PngDecoder::Impl::readHeader() {
  uint8_t sig[8];
  if (!fillFrom(*src, sig, sizeof sig) || std::memcmp(sig, kSignature, sizeof sig) != 0) {
    return fail("the cover is not a PNG");
  }
  uint8_t hdr[8];
  if (!fillFrom(*src, hdr, sizeof hdr)) return fail("the PNG ends before its header does");
  if (be32(hdr) != 13 || !typeIs(hdr + 4, "IHDR")) {
    return fail("the PNG does not begin with an image header");
  }
  uint8_t ih[13];
  if (!fillFrom(*src, ih, sizeof ih)) return fail("the PNG ends before its header does");
  if (!skipBytes(4)) return fail("the PNG ends before its header does");  // IHDR's CRC

  const uint32_t w = be32(ih), h = be32(ih + 4);
  const uint8_t depth = ih[8], colour = ih[9], comp = ih[10], filt = ih[11], inter = ih[12];

  if (w == 0 || h == 0 || w > kMaxChunkLen || h > kMaxChunkLen) {
    return fail("the PNG's header declares an image with no pixels");
  }
  if (comp != 0) return fail("the PNG is compressed by a method this decoder does not read");
  if (filt != 0) return fail("the PNG uses a filter method this decoder does not read");
  if (inter != 0) {
    // 0 of 225 corpus covers are interlaced. Reading an Adam7 stream as if it
    // were progressive scanlines produces a plausible-looking mosaic, which is
    // worse than nothing on a screen that stays up for hours.
    return fail("this PNG is interlaced, which this decoder does not read");
  }
  if (colour == 3) {
    return fail("this PNG uses a colour palette, which this decoder does not read");
  }
  channels = channelsFor(colour);
  if (channels == 0) return fail("this PNG's colour type is not one this decoder reads");
  if (depth != 8) {
    return fail("this PNG is not 8 bits a channel, which is all this decoder reads");
  }

  width = static_cast<int>(w);
  height = static_cast<int>(h);

  // WIDTH IS A 31-BIT NUMBER OFF SOMEBODY'S CARD and size_t is 32 bits on the
  // device, so the row block is sized in 64 bits and the cast back is checked.
  // THERE IS NO ARBITRARY CAP: what refuses an image too wide to hold is the
  // allocation failing, which is the honest answer and the one that also
  // accounts for the heap actually free at the time.
  //
  // NO TEST REACHES THE REFUSAL BELOW, and that is written down rather than left
  // to be rediscovered -- the same note jpegd.cpp carries for its band-count
  // check. On the desktop `size_t` IS `uint64_t`, so the round trip is exact by
  // construction and the branch cannot be taken at all; it exists for the 32-bit
  // C3, where 2^31 * 4 does not fit a size_t and the multiply would otherwise
  // wrap to a small allocation and a heap overrun. Deleting it fails nothing on
  // the host, by construction rather than for want of a fixture.
  const uint64_t strideU = static_cast<uint64_t>(w) * static_cast<uint64_t>(channels);
  const uint64_t needU = 2u * strideU + static_cast<uint64_t>(w);
  const size_t need = static_cast<size_t>(needU);
  if (static_cast<uint64_t>(need) != needU) {
    return fail("the PNG's rows are larger than this device can address");
  }
  stride = static_cast<size_t>(strideU);

  // ONE ALLOCATION, THREE ROWS, and the third is not optional: `cur` becomes
  // `prev` for the next row's unfilter, so the grey the sink is handed cannot be
  // written over the filtered bytes it was made from. Value-initialised because
  // `prev` must be zero for the first row -- the spec's virtual row above the
  // image -- and a run over uninitialised heap there is a picture that differs
  // between boots.
  //
  // DROPPING THE `()` FAILS NO TEST, and that is written down rather than left
  // to be rediscovered: uninitialised memory is not observable from a portable
  // test, and this allocator hands back zeroed pages. What IS covered is the
  // path -- test_pngd.cpp builds an image whose FIRST row is Up-, Average- and
  // Paeth-filtered, because every real fixture's first row is Sub and Sub never
  // reads the row above, so before that test nothing reached this at all.
  // Poisoning the block with 0xAA fails that test and nothing else.
  rows = new (std::nothrow) uint8_t[need]();
  if (rows == nullptr) return fail("no memory for the rows of a cover this wide");
  cur = rows;
  prev = rows + stride;
  grey = rows + 2 * stride;
  // THE ROW BLOCK ONLY. pngd.h promises 0 after a refusal that never sized the
  // rows, and the window's term is added by run() once inf.begin() has actually
  // taken it -- a figure that counted 37 KB the decoder had not allocated would
  // be exactly the drift workspaceBytes() exists to prevent.
  workspace = need;
  return true;
}

// IDAT IS ZLIB (RFC 1950) AND THE INFLATER IS RAW DEFLATE (RFC 1951), because
// its other caller is a zip, which stores raw. So the two-byte wrapper is read
// and checked here. The Adler-32 trailer is never reached: the decode stops
// after the last scanline, several bytes short of it.
bool PngDecoder::Impl::readZlibHeader() {
  uint8_t z[2];
  if (!fillFrom(idat, z, sizeof z)) return fail("the PNG has no image data");
  const unsigned cmf = z[0], flg = z[1];
  if ((cmf & 0x0F) != 8) return fail("the PNG's image data is not deflate-compressed");
  if ((cmf >> 4) > 7) return fail("the PNG's image data asks for a window this decoder does not have");
  if (((cmf << 8) | flg) % 31 != 0) return fail("the PNG's image data has a malformed header");
  // A preset dictionary would mean bytes this decoder was never given. PNG
  // forbids it outright, so a file that asks is malformed rather than exotic.
  if ((flg & 0x20) != 0) return fail("the PNG's image data asks for a preset dictionary");
  return true;
}

// RFC 2083 6.3, on the raw channel bytes and BEFORE any grey conversion -- the
// filter's `a` and `c` are the pixel to the left and the pixel above-left, so
// they are only correct at the source's own pixel width.
void PngDecoder::Impl::unfilter(uint8_t filter) {
  const size_t bpp = static_cast<size_t>(channels);
  switch (filter) {
    case 0:  // None
      break;
    case 1:  // Sub
      for (size_t i = bpp; i < stride; ++i) cur[i] = static_cast<uint8_t>(cur[i] + cur[i - bpp]);
      break;
    case 2:  // Up
      for (size_t i = 0; i < stride; ++i) cur[i] = static_cast<uint8_t>(cur[i] + prev[i]);
      break;
    case 3:  // Average
      for (size_t i = 0; i < stride; ++i) {
        const unsigned a = i >= bpp ? cur[i - bpp] : 0u;
        // The spec's floor((a + b) / 2), computed at full width: a and b are
        // bytes and their sum is not.
        cur[i] = static_cast<uint8_t>(cur[i] + ((a + prev[i]) >> 1));
      }
      break;
    default:  // 4, Paeth -- the caller has already refused anything above it
      for (size_t i = 0; i < stride; ++i) {
        const uint8_t a = i >= bpp ? cur[i - bpp] : 0;
        const uint8_t c = i >= bpp ? prev[i - bpp] : 0;
        cur[i] = static_cast<uint8_t>(cur[i] + paeth(a, prev[i], c));
      }
      break;
  }
}

// STB'S OWN CONVERSION, matched exactly, which is what lets test_pngd.cpp assert
// equality: it takes the grey byte for 1- and 2-channel images and weights RGB
// for 3- and 4-channel ones, and it DROPS alpha rather than compositing. A cover
// is drawn over paper-white anyway, so compositing would be the same picture at
// a cost, and disagreeing with the oracle for it would cost the byte-exactness.
void PngDecoder::Impl::toGrey() {
  if (channels <= 2) {
    for (int x = 0; x < width; ++x) grey[x] = cur[static_cast<size_t>(x) * channels];
    return;
  }
  for (int x = 0; x < width; ++x) {
    const uint8_t* p = cur + static_cast<size_t>(x) * channels;
    grey[x] = PngDecoder::greyOf(p[0], p[1], p[2]);
  }
}

bool PngDecoder::Impl::run() {
  if (!readHeader()) return false;
  if (!readZlibHeader()) return false;

  // THE SINK IS ASKED BEFORE THE 37 KB WINDOW IS TAKEN, and the order is
  // load-bearing rather than incidental. image_sink.h advertises a false from
  // begin() as "the cheapest refusal a sink that cannot use these dimensions can
  // make" -- and the realistic reason CoverFitter::begin says false on device is
  // that it CANNOT ALLOCATE, which is precisely the moment 37,056 bytes must not
  // have just been taken out from under it. Nothing forces the other order here,
  // unlike jpegd.cpp where the band cannot be sized before the SOF is read.
  //
  // The row block IS already taken (~11 KB): moving the ask above it would put
  // the zlib-header refusals after begin() too, and those legitimately report
  // "the sink heard nothing". A tenth of the saving for a worse contract.
  if (!sink->begin(width, height)) {
    // The same outcome as a sink that stops mid-picture, and it has to be: a
    // caller cannot be asked to tell "you refused" from "the file is bad" by
    // which of two ways it was refused.
    aborted = true;
    return false;
  }

  if (!inf.begin(idat)) return fail("no memory for the PNG's inflate window");
  workspace += Inflater::kHeapBytes;

  for (int y = 0; y < height; ++y) {
    uint8_t filter = 0;
    if (!fillFrom(zs, &filter, 1)) {
      return fail(streamEndReason());
    }
    // CHECKED BEFORE THE ROW IS READ. `unfilter`'s default arm is Paeth and
    // relies on this, so the two are one clause read apart -- and a file that
    // names a filter the spec does not define is refused without pulling
    // another `stride` bytes through the inflater first.
    if (filter > 4) return fail("the PNG uses a row filter that does not exist");
    if (!fillFrom(zs, cur, stride)) {
      return fail(streamEndReason());
    }
    unfilter(filter);
    toGrey();
    if (!sink->row(grey)) {
      aborted = true;
      return false;
    }
    // The row just unfiltered is the next row's `prev`. Swapping is what makes
    // the pair a pair -- with one buffer, Up, Average and Paeth would each read
    // the row they are in the middle of writing.
    uint8_t* t = cur;
    cur = prev;
    prev = t;
  }
  return true;
}

PngDecoder::PngDecoder() : impl_(new (std::nothrow) Impl()) {}
PngDecoder::~PngDecoder() = default;

bool PngDecoder::decode(ByteSource& src, ImageRowSink& sink) {
  if (impl_ == nullptr) return false;  // the constructor's allocation failed
  Impl& im = *impl_;
  im.reset();
  im.src = &src;
  im.sink = &sink;

  const bool ok = im.run();

  // NOTHING IS HELD BETWEEN DECODES. The inflate window is 37,056 bytes and a
  // PngDecoder outlives the picture it decoded by as long as the screen is up --
  // which on the sleep screen is hours. release() is the Inflater's own answer
  // to exactly this, and it is why that method exists.
  im.inf.release();
  im.zs.reset();
  delete[] im.rows;
  im.rows = im.cur = im.prev = im.grey = nullptr;
  im.src = nullptr;
  im.sink = nullptr;
  return ok;
}

bool PngDecoder::aborted() const { return impl_ && impl_->aborted; }
const char* PngDecoder::reason() const {
  return impl_ ? impl_->reason : "no memory for a PNG decoder";
}
size_t PngDecoder::workspaceBytes() const { return impl_ ? impl_->workspace : 0; }

}  // namespace reader
