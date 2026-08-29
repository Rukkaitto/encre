#pragma once
#include <cstdint>

namespace reader {

// WHERE DECODED IMAGE ROWS GO, FOR EVERY IMAGE DECODER IN THIS PROJECT.
//
// A sink, because the JPEG decoder cannot hand rows back on request: TJpgDec's
// jd_decomp() decodes the whole image in ONE call and pushes MCU rectangles
// through a callback, so there is no point at which a caller could ask for the
// next row. Everything downstream (CoverFitter, the plane sink) is push for the
// same reason, and the whole pipeline is one direction from the archive to the
// glass.
//
// PNG DOES NOT NEED THE PUSH SHAPE AND TAKES IT ANYWAY -- its scanlines come out
// in order and a pull form would be natural. It is push so cover.cpp drives both
// formats through ONE path: two shapes would be two drivers feeding the fitter,
// and two chances to get that feeding wrong.
//
// IT LIVES HERE RATHER THAN IN jpegd.h BECAUSE THERE ARE TWO DECODERS NOW.
// Task 2 correctly left it in jpegd.h with one consumer -- a shared home for a
// single caller is a header edge bought for nothing. pngd.h is the second, and
// the second copy is the extraction point, not the fifth. The alternative was
// pngd.h including jpegd.h for a base class, coupling the two decoders through
// nothing but an accident of which was written first.
class ImageRowSink {
 public:
  virtual ~ImageRowSink() = default;
  // Once, before any row, with the OUTPUT dimensions after any scaling. False
  // stops the decode before a single row is produced, which is the cheapest
  // refusal a sink that cannot use these dimensions can make.
  virtual bool begin(int width, int height) = 0;
  // One row of `width` bytes of grey, 0 = black, in top-to-bottom order. Return
  // false to stop the decode -- this is the interruption path, and there is no
  // second one. For JPEG it is TJpgDec's own (outfunc returning 0 aborts with
  // JDR_INTR); for PNG it ends the scanline loop.
  //
  // The pointer is borrowed until the call returns: it is into the decoder's own
  // scratch -- the JPEG's MCU band, the PNG's current scanline -- which the next
  // band or row overwrites.
  virtual bool row(const uint8_t* px) = 0;
};

}  // namespace reader
