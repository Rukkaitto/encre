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

// WHAT A DECODE SAW, for a log line and for a corpus probe.
//
// THE REASON IS HERE BECAUSE THE SIX RESULTS CANNOT CARRY THE WHOLE TRUTH. The
// result is what a caller branches on and what tools/covers.py counts; the reason
// is the sentence from whichever layer refused -- which of the four PNG colour
// types, which axis of the IHDR, where the entry ran out.
//
// A SHORTFALL IS REPORTED AS ONE WHEREVER IT HAPPENS, and it took a change in
// both decoders to make that true. This paragraph used to say the opposite -- that
// an allocation failure inside a decoder "is not distinguished from a bad file" --
// and the consequence was precise and wrong: pngd asks its sink BEFORE taking its
// 37 KB window, so the deflated PNG that genuinely does not fit was DECLARED when
// it failed and came back ReadFailed, a card fault for the one corpus shape whose
// only problem is that this device is too small. TJpgDec's row band failed the
// other side of the same line and came back Unsupported. Both decoders now carry
// `outOfMemory()` beside `aborted()`, set at their nothrow sites and nowhere else,
// and decodeCover asks it before it asks anything about the file.
//
// WHAT IS LEFT OF THE OLD SPLIT is honest: a decode that failed for a reason that
// is neither a shortfall nor a button splits on whether the picture ever declared
// its dimensions -- Unsupported if it did not, ReadFailed if it did. That is a
// question about the file, asked only where the answer is about the file.
//
// AND THE SCALE IS HERE BECAUSE NOTHING ELSE CAN SEE IT. `scaleDivisor` is the
// one lever this pipeline has that changes no output geometry at all -- a cover
// decoded at 1/1 and at 1/2 both fill the same box with slightly different greys
// -- so a request that quietly asked for too little would be invisible to a test
// that looked only at the planes. It is reported for the corpus probe and pinned
// by a test for that reason.
struct CoverReport {
  // Null when there is nothing to say: an Ok decode, and an abandoned one -- the
  // file was fine and the user pressed a button.
  const char* reason = nullptr;
  // What the FILE said, before any scaling. 0 if it never got that far, which is
  // every refusal before the headers parsed.
  int sourceWidth = 0, sourceHeight = 0;
  // 1, 2, 4 or 8 -- the divisor the JPEG's IDCT scaling chose. Always 1 for a
  // PNG, which has no such lever (pngd.h states the absence as the point).
  int scaleDivisor = 1;
  // WHERE THE COVER LANDED on the panel, and therefore how much of it is band.
  // Zero until the source's dimensions are known. Four ints rather than a FitBox
  // so this header stays clear of imagefit.h and its <vector>, for the reason
  // reader/cover_fit.h exists.
  int dstX = 0, dstY = 0, dstW = 0, dstH = 0;
};

// DECODE `book`'s COVER INTO `sink`, STREAMING, HOLDING NEITHER IMAGE NOR PLANE.
//
// PEAK HEAP, MEASURED RATHER THAN ADDED UP. A probe that counts live bytes across
// one call, over a 740x1000 JPEG and a 1600x2400 PNG, both fits, both panels:
//
//   stored JPEG      26.6 KB   largest block 11,840 (TJpgDec's MCU band)
//   DEFLATED JPEG    63.6 KB   largest block 36,956 (the zip's inflate window)
//   stored PNG       58.7 KB   largest block 36,956 (the PNG's own window)
//   deflated PNG     95.7 KB   two windows, one after the other
//
// Nothing is held after the call returns -- the probe reports zero live bytes at
// every one of them, which is the property the decoders' own headers promise and
// the only one this layer could break by holding a decoder past its picture.
//
// AGAINST ~87 KB FREE AT SLEEP, once the reader's chapter is released, that makes
// the deflated JPEG -- 59% of the corpus's JPEG covers -- the worst case that
// FITS, at about three quarters of the budget, and the deflated PNG the one shape
// that does NOT. That is 1 of 225 corpus books, it answers OutOfMemory, and the
// caller falls back to the reading card. The spec estimated 54-62 KB for the
// deflated JPEG and the measurement is 63.6, so the shape of the estimate was
// right and its total was a little low.
//
// THE BAND IS THE PART THAT SURPRISES, and it is why JpegDecoder reports its own
// workspace rather than this comment asserting a number: TJpgDec emits MCU
// RECTANGLES, so a row is not complete until its whole band has arrived and one
// band must be held. Ask JpegDecoder::workspaceBytes() rather than trusting this.
// It is BOUNDED rather than unbounded because of the `atLeast` request -- but not
// by the figure this line first quoted. The band is `mcuHeight * outputWidth`, and
// the scale search requires BOTH axes to clear the request (jpegd.cpp), so when
// HEIGHT is the binding axis the output width is bounded by
// `2 * panelH * (srcW / srcH)` and not by `2 * panelW`. Height usually IS binding
// here: reader/cover_fit.h's census says a cover is almost always relatively wider
// than the X4, so the aspect is what holds the real figures down, and the squarest
// corpus cover already reaches 1.83x panelW. Ask workspaceBytes().
//
// THE FIGURES ARE THE DESKTOP'S ALLOCATOR, so the ESP32's per-block overhead is
// on top and the std::string/std::vector sizes are not exactly the firmware's.
// What transfers is the sizeable blocks, which are the same objects.
//
// THE SPAN IS NOT ASSUMED TO BE AN IMAGE. Task 5 deliberately applied no
// media-type filter to the cover the OPF names -- the same call Epub::open makes
// about `unique-identifier`, where a check cost four real books to catch a
// mispointer that costs nothing -- so what decides which decoder runs is the
// BYTES: a JPEG SOI or the eight-byte PNG signature, and neither is Unsupported.
//
// `report` is optional and its fields are filled as far as the decode got.
CoverResult decodeCover(FileSystem& fs, const OpenedBook& book, int panelW, int panelH,
                        CoverFit fit, CoverPlaneSink& sink,
                        CoverStopFn stop = nullptr, void* stopCtx = nullptr,
                        CoverReport* report = nullptr);

}  // namespace reader
