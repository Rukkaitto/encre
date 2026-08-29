#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

#include "reader/image_sink.h"      // ImageRowSink -- one sink shape for both formats
#include "reader/inflate_stream.h"  // ByteSource, Inflater

namespace reader {

// PNG, PUSHED ONE SCANLINE AT A TIME, over this project's own Inflater.
//
// WHY OURS AND NOT A SECOND VENDORED DECODER: a PNG is DEFLATE plus per-row
// unfiltering, and inflate_stream.h is already the hard half -- bounded memory, a
// 32 KB window, no giant stack frame. Vendoring a second decoder to get ~200
// lines of unfiltering would be the worse trade, which is the mirror of jpegd.h's
// argument for vendoring THERE.
//
// THE PUSH INTERFACE IS NOT FORCED HERE THE WAY IT IS FOR JPEG -- scanlines come
// out in order and a pull form would be natural. It is push anyway so cover.cpp
// drives both formats through ONE path: two shapes would be two drivers feeding
// the fitter, and two chances to get that feeding wrong.
//
// WHAT IT SUPPORTS, AND THE MEASUREMENT BEHIND IT: colour types 0/2/4/6 at bit
// depth 8, non-interlaced. All 39 PNG covers across 225 corpus books are colour
// type 2, bit depth 8, non-interlaced -- 0 interlaced, 0 palette. Types 0/4/6
// come free with the same unfilter and are accepted; PALETTE (3), bit depths
// other than 8, and INTERLACED are REFUSED with a reason rather than
// approximated. Accepting a type nothing has ever decoded would be a claim
// rather than a behaviour, so all three have fixtures and test_pngd.cpp decodes
// each of them byte for byte against stb_image.
//
// IDAT IS ZLIB, NOT RAW DEFLATE, and that is the one place this differs from
// every other Inflater caller in the project: the zip path hands over RFC 1951
// bytes, PNG wraps them in RFC 1950. The two-byte header is read and checked
// here and the four-byte Adler-32 trailer is never reached, because the decode
// stops after the last scanline.
//
// CRCs ARE NOT VERIFIED, deliberately. A checksum failure on a cover should cost
// the COVER, not the book, and everything a corrupt file can do to this decoder
// is already refused structurally -- a bad IHDR field, a filter byte above 4, a
// stream that ends early, a Huffman code that does not decode. The
// mutated-IHDR tests exercise exactly that: they change a byte and leave the CRC
// wrong, so what declines those files can only be the field itself.
//
// A NOTE ON HEAP, because it decides where this may run: an Inflater is 37,056
// bytes (Inflater::kHeapBytes) and it is taken for the duration of a decode and
// given back at the end. 38 of the 39 corpus PNGs are STORED inside the zip
// (method 0), so the common case needs exactly this one window. The single
// deflated PNG needs the zip's window too and may refuse at the reading floor --
// a stated limit, not a defect.
class PngDecoder {
 public:
  PngDecoder();
  ~PngDecoder();
  PngDecoder(const PngDecoder&) = delete;
  PngDecoder& operator=(const PngDecoder&) = delete;

  // False for a refusal (reason() says why) OR for an abort the sink asked for --
  // ask aborted() which. Same contract as JpegDecoder::decode, deliberately.
  //
  // Nothing is held after this returns: the inflate window and the row block are
  // given back, and only the reported figures survive.
  //
  // WHAT THE SINK KEEPS WHEN THIS ANSWERS FALSE: every row it was already given,
  // and no more. A truncated or corrupt file is the case that happens, and it
  // leaves the complete scanlines that were pushed before it. So a false means
  // "this picture is not finished", never "undo what you were told". The sink
  // decides what a partial frame is worth; for a cover it is worth nothing and
  // the caller should draw the fallback.
  //
  // THERE IS NO atLeastW/atLeastH PAIR, unlike JpegDecoder, and the absence is
  // the statement: TJpgDec halves out of the IDCT for free, so asking it for
  // less is a real saving. PNG has no such lever -- every scanline must be
  // inflated and unfiltered whatever the caller wants of it, because the next
  // row's unfilter reads this one. Offering the parameter and ignoring it would
  // be a knob that does nothing.
  bool decode(ByteSource& src, ImageRowSink& sink);

  // Whether the last decode() stopped because the SINK said so, as opposed to
  // failing. Never both, and an abort leaves reason() null: the file was fine.
  bool aborted() const;
  // Null until something fails, and null again after a decode that succeeds. A
  // sentence, for a log line.
  const char* reason() const;

  // The inflate window and tables, plus the row block -- two filter rows for the
  // unfilter and one grey row for the sink, in one allocation.
  //
  // The inflater's half is Inflater::kHeapBytes, which is that class's own
  // PUBLISHED cost and 100 bytes above the block it really takes -- the real
  // size is private to it, and a budget wants the number its header guarantees
  // rather than one this file would have to keep in step by hand.
  //
  // REPORTED RATHER THAN DOCUMENTED, so the figure in the spec's budget cannot
  // drift from the object. Valid once decode() has read IHDR and sized the rows;
  // 0 before, and 0 after a refusal that never got that far.
  size_t workspaceBytes() const;

  // THE PICTURE'S DIMENSIONS ARE NOT REPORTED HERE, and that is deliberate:
  // there is no scaling, so they are exactly what ImageRowSink::begin was handed
  // and a getter would be a second spelling of one fact. JpegDecoder has
  // sourceWidth()/sourceHeight() because a scaled decode makes source and output
  // genuinely different numbers.

  // Grey from RGB with stb_image's own coefficients, so test_pngd.cpp can assert
  // EQUALITY against the oracle rather than a tolerance. Stated here because the
  // choice is load-bearing for the test, not because the number is interesting.
  static uint8_t greyOf(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
  }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace reader
