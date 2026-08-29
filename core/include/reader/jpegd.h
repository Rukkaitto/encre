#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

#include "reader/inflate_stream.h"  // ByteSource

namespace reader {

// WHERE DECODED IMAGE ROWS GO.
//
// A sink, because the decoder cannot hand rows back on request: TJpgDec's
// jd_decomp() decodes the whole image in ONE call and pushes MCU rectangles
// through a callback, so there is no point at which a caller could ask for the
// next row. Everything downstream (CoverFitter, the plane sink) is push for the
// same reason, and the whole pipeline is one direction from the archive to the
// glass.
class ImageRowSink {
 public:
  virtual ~ImageRowSink() = default;
  // Once, before any row, with the OUTPUT dimensions after any scaling. False
  // stops the decode before a single row is produced, which is the cheapest
  // refusal a sink that cannot use these dimensions can make.
  virtual bool begin(int width, int height) = 0;
  // One row of `width` bytes of grey, 0 = black, in top-to-bottom order. Return
  // false to stop the decode -- this is the interruption path, and for JPEG it is
  // TJpgDec's own (outfunc returning 0 aborts with JDR_INTR).
  //
  // The pointer is borrowed until the call returns: it is into the decoder's own
  // MCU band buffer, which the next band overwrites.
  virtual bool row(const uint8_t* px) = 0;
};

// BASELINE JPEG, PUSHED ONE ROW AT A TIME.
//
// A cover is 2.94 MP at the corpus median, which is 2.9 MB decoded to 8-bit grey
// against a 42 KB reading floor and no PSRAM -- so the whole picture is never in
// memory at any instant.
//
// WRAPS TJpgDec R0.03, VENDORED. The reasons this project wrote its own DEFLATE --
// stb's one-shot API and its 6,608-byte single stack frame -- do not apply:
// TJpgDec already has the memory model wanted here. What this class adds is a
// ByteSource front end, rows instead of MCU rectangles, and a refusal that carries
// a reason.
//
// ROWS INSTEAD OF RECTANGLES IS THE WHOLE OF THE WORK, and it is not free.
// TJpgDec emits one MCU at a time -- 16x16 for 4:2:0, which is 177 of the 185
// corpus JPEG covers -- left to right across a band, then the next band down. So
// a row is not finished until every MCU of its band has arrived, and ONE BAND
// must be held: mcuHeight * outputWidth, 8.4-16.9 KB at the scales this uses.
// workspaceBytes() reports it because it is large enough to matter and invisible
// enough to be forgotten.
//
// PROGRESSIVE JPEG IS REFUSED, not approximated. Measured: 2 of 225 corpus books,
// but 2 of the user's own 16. A refusal falls back to the reading card and logs
// why; a mis-decode would put garbage on the glass for hours.
class JpegDecoder {
 public:
  JpegDecoder();
  ~JpegDecoder();
  JpegDecoder(const JpegDecoder&) = delete;
  JpegDecoder& operator=(const JpegDecoder&) = delete;

  // Decode `src` into `sink`. False for a refusal (reason() says why) OR for an
  // abort the sink asked for -- ask aborted() which, because they mean different
  // things to the caller: a refusal is permanent for this book, an abort is not.
  //
  // `atLeastW`/`atLeastH` are the smallest output the caller can use; TJpgDec
  // halves out of the IDCT for free, so a 1400x2100 cover asked for 480x800 is
  // decoded at 1/2 -- a quarter of the work. Never scales BELOW the request. 0 for
  // both means full scale.
  //
  // Nothing is held after this returns: the pool and the band are given back, and
  // only the reported figures survive.
  //
  // WHAT THE SINK KEEPS WHEN THIS ANSWERS FALSE: every row it was already given.
  // A refusal part-way through -- a truncated file is the one that happens --
  // leaves the complete bands that were pushed before it pushed, and discards the
  // band that was in hand, which was never whole. So a false means "this picture
  // is not finished", never "undo what you were told". The sink decides what a
  // partial frame is worth; for a cover it is worth nothing and the caller should
  // draw the fallback.
  bool decode(ByteSource& src, ImageRowSink& sink, int atLeastW = 0, int atLeastH = 0);

  // What the file said, before scaling. Valid once decode() has read the headers,
  // including on a refusal that happened after them -- a refusal BEFORE them (a
  // progressive stream, bytes that are not a JPEG) leaves both 0, because nothing
  // ever said what the picture was.
  int sourceWidth() const;
  int sourceHeight() const;
  // 1, 2, 4 or 8 -- the divisor decode() chose.
  int scaleDivisor() const;

  // Whether the last decode() stopped because the SINK said so, as opposed to
  // failing. Never both, and an abort leaves reason() null: the file was fine.
  bool aborted() const;
  // Null until something fails. A sentence, for a log line.
  const char* reason() const;

  // Heap held during a decode: TJpgDec's pool plus the MCU band buffer.
  //
  // REPORTED RATHER THAN DOCUMENTED, so the figure in the spec's budget cannot
  // drift from the object. Valid once decode() has read the headers and chosen a
  // scale; 0 before, and 0 after a refusal that never got that far.
  size_t workspaceBytes() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace reader
