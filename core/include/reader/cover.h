#pragma once
#include <cstdint>

#include "reader/book.h"
// CoverFit COMES FROM THE LEAF, NOT FROM imagefit.h. That header needs <vector>
// -- 72,845 preprocessed lines on its own -- and nothing in this file's interface
// is a FitBox or a CoverFitter, so reaching through it for one enum would undo
// exactly the decision reader/cover_fit.h exists to record.
#include "reader/cover_fit.h"
#include "reader/filesystem.h"

namespace reader {

// WHERE A DECODED COVER GOES.
//
// A sink rather than a buffer, because the whole point is that the planes are
// never all in memory: two rows at a time come out of CoverFitter and go straight
// to the card. The shell implements this over SdFat, the tests over a vector.
//
// Same shape as SettingsSink, for the same reason -- core/ does not learn what a
// filesystem is, and the desktop can drive the real code with a fake.
//
// WHAT IT IS PROMISED, AND IT IS THE WHOLE PANEL. `begin` is followed by exactly
// `rows` calls to `row` unless something refuses, and `rows` is the panel's
// height: the letterbox bands -- the rows above and below a Whole fit, and every
// row of a cover too small to fill the panel -- are PUSHED, as paper. That is
// what makes the two planes a physical framebuffer store byte for byte, which is
// what lets a paint be one file read into Framebuffer::data() with no extra RAM
// at all. CoverFitter already does the same thing across a row (its letterbox
// COLUMNS come out paper); doing it down the panel as well is the same rule on
// the other axis, and a raster cropped on one axis and not the other would be a
// trap for whoever placed it.
//
// So `rows` is `panelH`, always, and the parameter is not a second fact -- it is
// the count of `row` calls, stated where a sink that wants to size a file can use
// it (`planeRowBytes * rows` is one plane) rather than derived at each sink.
class CoverPlaneSink {
 public:
  virtual ~CoverPlaneSink() = default;
  // Called once, before any row, with the plane geometry.
  virtual bool begin(int panelW, int panelH, int planeRowBytes, int rows) = 0;
  // One destination row, both planes. `rowBytes` each.
  //
  // A PAPER ROW PASSES THE SAME POINTER TWICE, because paper is 0xFF in both
  // planes and holding two identical buffers to say so would be a second copy of
  // one row. A sink must read them, not compare them.
  virtual bool row(const uint8_t* msb, const uint8_t* lsb) = 0;
  // Called once at the end. `ok` is false if decoding was abandoned or failed, and
  // an implementation MUST NOT leave a file that a reader would accept.
  //
  // ALWAYS CALLED, AND EVEN WHEN `begin` WAS NOT -- a book that declares no cover,
  // a span that is not an image, a card that will not open. It is the one call a
  // sink is guaranteed, so cleanup has one home rather than one per refusal, and a
  // stale file from another book can be dropped on the way past.
  virtual bool finish(bool ok) = 0;
};

// Answered every few source rows. True abandons the decode -- the shell answers it
// from rawSamplesPending(), so a button press gets the device out of the way.
//
// A FUNCTION POINTER, NOT std::function: this is -fno-exceptions embedded code and
// completeIndex's stop predicate already set the precedent.
using CoverStopFn = bool (*)(void*);

enum class CoverResult {
  Ok,
  NoCover,        // the book declares none
  Unsupported,    // progressive JPEG, interlaced/palette PNG, not an image we read
  ReadFailed,     // the card, the zip, or a truncated entry
  OutOfMemory,    // a window or a buffer could not be allocated
  Abandoned,      // the stop predicate said so
};

const char* coverResultName(CoverResult r);

// DECODE `book`'s COVER INTO `sink`, STREAMING, HOLDING NEITHER IMAGE NOR PLANE.
//
// Peak heap, worst realistic case (a deflated JPEG -- 59% of corpus JPEG covers
// are deflated inside the zip): the zip inflater's 36,956 bytes, TJpgDec's ~3,500,
// ITS MCU BAND BUFFER at 8.4-16.9 KB, a destination accumulator and an error row
// at ~2,112 each, and two plane rows. About 54-62 KB, against ~87 KB free at sleep
// once the reader's chapter is released.
//
// THE BAND IS THE PART THAT SURPRISES, and it is why JpegDecoder reports its own
// workspace rather than this comment asserting a number: TJpgDec emits MCU
// RECTANGLES, so a row is not complete until its whole band has arrived and one
// band must be held. Ask JpegDecoder::workspaceBytes() rather than trusting this.
//
// A DEFLATED PNG NEEDS TWO WINDOWS and may run out of memory. That is 1 of 225
// corpus books, and the caller falls back to the reading card.
//
// THE SPAN IS NOT ASSUMED TO BE AN IMAGE. Task 5 deliberately applied no
// media-type filter to the cover the OPF names -- the same call Epub::open makes
// about `unique-identifier`, where a check cost four real books to catch a
// mispointer that costs nothing -- so what decides which decoder runs is the
// BYTES: a JPEG SOI or the eight-byte PNG signature, and neither is Unsupported.
//
// WHY THE RESULT AND THE REASON ARE BOTH HANDED BACK. The result is what the
// caller branches on and what tools/covers.py counts; `*reason` is a sentence for
// a log line, from whichever layer refused, and it exists because the six results
// cannot carry the whole truth. In particular an allocation failure INSIDE a
// decoder -- TJpgDec's band, the PNG's own inflate window -- is not distinguished
// from a bad file here: this function reports OutOfMemory for the allocations it
// makes itself and otherwise splits on whether the picture ever declared its
// dimensions (Unsupported if it did not, ReadFailed if it did). The alternatives
// were matching on the decoders' reason strings, which is one spelling of a
// sentence in two files, and probing workspaceBytes(), which is arithmetic on
// another header's bookkeeping that a wide-enough PNG defeats. The reason string
// carries what the enum cannot, and every non-Ok answer falls back the same way.
//
// Null `reason` is allowed and null `*reason` is possible -- an abandoned decode
// has nothing to say, because nothing was wrong.
CoverResult decodeCover(FileSystem& fs, const OpenedBook& book, int panelW, int panelH,
                        CoverFit fit, CoverPlaneSink& sink,
                        CoverStopFn stop = nullptr, void* stopCtx = nullptr,
                        const char** reason = nullptr);

}  // namespace reader
