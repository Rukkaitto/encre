#include "reader/cover.h"

#include <cstring>
#include <memory>
#include <new>

#include "reader/imagefit.h"
#include "reader/inflate_stream.h"
#include "reader/jpegd.h"
#include "reader/pngd.h"
#include "reader/zip.h"

namespace reader {
namespace {

// HOW OFTEN THE STOP PREDICATE IS ASKED, in SOURCE rows. Per row is eight times
// the calls for latency nobody can feel; completeIndex checks per block for the
// same trade at a comparable granularity. A source row of a 2.94 MP cover is
// worth roughly a block of XHTML.
//
// It is asked on SOURCE rows only, never while the letterbox bands are being
// pushed: those are a memset and a sink call, and abandoning inside them would
// leave a part-written file for work that was nearly free.
constexpr int kStopEveryRows = 8;

// THE LARGEST PANEL THIS WILL ENTERTAIN. The X4 is 480 wide and the X3 528, so
// this is four orders of magnitude of headroom -- it exists to keep
// `(panelW + 7) / 8` inside a signed int, not to express a policy.
constexpr int kMaxPanel = 1 << 20;

constexpr size_t kSniffBytes = 8;
constexpr uint8_t kPngSignature[kSniffBytes] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

enum class Format { None, Jpeg, Png };

// WHAT THE BYTES SAY THEY ARE. The OPF's cover pointer is not filtered by media
// type -- see cover.h -- so this is the only thing that decides which decoder
// runs, and a span that is neither is refused before a decoder is built.
//
// THE PNG SIGNATURE IS CHECKED WHOLE AND ONLY THE FIRST FOUR BYTES DECIDE
// ANYTHING, which is worth writing down rather than rediscovering: pngd.cpp reads
// and checks the same eight bytes itself (`kSignature`, pngd.cpp:15), so
// shortening this to `\x89PNG` fails no test and cannot -- a file that clears
// four and not eight is refused one layer down instead, with a better sentence
// and the same Unsupported. It is the whole signature because that is what the
// signature is; the last four bytes are the format's own CRLF-mangling detector
// and truncating them would be an arbitrary line to draw.
Format sniff(const uint8_t* p, size_t n) {
  if (n >= 2 && p[0] == 0xFFu && p[1] == 0xD8u) return Format::Jpeg;
  if (n >= kSniffBytes && std::memcmp(p, kPngSignature, kSniffBytes) == 0) return Format::Png;
  return Format::None;
}

// THE BYTES THE SNIFF ATE, PUT BACK. A ByteSource has no pushback and neither
// decoder will start part-way into its own header, so the prefix already read is
// served first and everything after it comes from the source below.
//
// IT FILLS A READ ACROSS THE SEAM, AND THAT IS A COURTESY RATHER THAN A FIX --
// written down because the first version of this comment claimed otherwise, and a
// mutation returning short at the seam failed nothing. `ByteSource::read` says
// "up to `bytes`", and BOTH decoders drain in a loop for it (jpegd's `feed` and
// pngd's `fillFrom`, each with a comment saying a short read is not the stream
// ending). So a short read here would be handled. What filling buys is that this
// source behaves like the EntrySource and InflateSource it stands in front of,
// rather than being the one source in the chain with a hiccup eight bytes in.
class PrefixSource : public ByteSource {
 public:
  PrefixSource(const uint8_t* prefix, size_t n, ByteSource& rest)
      : prefix_(prefix), left_(n), rest_(&rest) {}

  size_t read(void* dst, size_t bytes) override {
    uint8_t* out = static_cast<uint8_t*>(dst);
    size_t got = 0;
    if (left_ > 0) {
      const size_t take = left_ < bytes ? left_ : bytes;
      std::memcpy(out, prefix_ + at_, take);
      at_ += take;
      left_ -= take;
      got = take;
    }
    if (got < bytes) got += rest_->read(out + got, bytes - got);
    return got;
  }

 private:
  const uint8_t* prefix_;
  size_t at_ = 0;
  size_t left_ = 0;
  ByteSource* rest_;
};

// THE ADAPTER BETWEEN THE TWO PUSH INTERFACES, and the whole of this file's work.
//
// A decoder pushes source rows and knows nothing about a panel; a CoverPlaneSink
// takes destination plane rows and knows nothing about a picture. CoverFitter is
// what joins them, and the two things that cannot live anywhere else are here:
//
//   * THE FIT BOX NEEDS THE SOURCE'S DIMENSIONS, which nothing knows until the
//     decoder has read its headers. So fitCover and CoverFitter::begin happen in
//     begin(), not before the decode starts -- and what decodeCover passes as the
//     decoder's `atLeast` pair is the PANEL, which it does know.
//   * THE LETTERBOX ROWS ARE PUSHED, as paper. cover.h says why at length: the
//     planes are a physical framebuffer store byte for byte, so the rows above
//     and below the box belong to it. The leading band goes out here, the
//     trailing one from pad().
class PlaneAdapter : public ImageRowSink {
 public:
  PlaneAdapter(CoverPlaneSink& out, int panelW, int panelH, CoverFit fit, CoverStopFn stop,
               void* stopCtx)
      : out_(&out), panelW_(panelW), panelH_(panelH), fit_(fit), stop_(stop), ctx_(stopCtx) {}

  bool begin(int width, int height) override {
    // THE PICTURE DECLARED ITSELF, which is the one bit decodeCover needs to tell
    // "this is a format we do not read" from "this file is broken". A decoder
    // reaches here only after its headers parsed.
    declared_ = true;
    width_ = width;
    height_ = height;

    // THE GEOMETRY BEFORE THE FITTER, BECAUSE ONE `false` CANNOT CARRY TWO
    // ANSWERS. CoverFitter::begin refuses a cover too small to enlarge with the
    // same false it uses for a block it could not take, and reporting the first
    // as OutOfMemory is a log line that blames the device for a small picture.
    // fitCover is pure arithmetic on two pairs of integers, so asking it here
    // costs nothing and cannot disagree with the box begin() derives from the
    // same four arguments a line below.
    planned_ = fitCover(width, height, panelW_, panelH_, fit_);
    if (planned_.tooSmall) {
      tooSmall_ = true;
      return false;
    }
    // THE FITTER FIRST, THE SINK SECOND, and the order is deliberate: the fitter
    // is the allocation that can fail on device, and a sink that opens a file
    // should not have been asked to do it for a cover that was never going to be
    // fitted. It is the mirror of pngd.cpp asking its sink before taking its
    // window, for the same reason -- put the refusal that costs nothing first.
    if (!fitter_.begin(width, height, panelW_, panelH_, fit_)) {
      oom_ = true;
      return false;
    }
    // NOT AN INDEPENDENT DERIVATION -- imagefit.h STATES it ("plane rows come out
    // (panelW + 7) / 8 bytes"), and the paper row has to be exactly as long as the
    // fitter's or a sink reading `planeRowBytes` from one of them runs off the end
    // of the other. There is no accessor to ask for instead; the contract is the
    // shared fact, and the two are pinned together by a test.
    planeBytes_ = (panelW_ + 7) / 8;
    paper_.reset(new (std::nothrow) uint8_t[static_cast<size_t>(planeBytes_)]);
    if (paper_ == nullptr) {
      oom_ = true;
      return false;
    }
    std::memset(paper_.get(), 0xFFu, static_cast<size_t>(planeBytes_));

    if (!out_->begin(panelW_, panelH_, planeBytes_, panelH_)) {
      sinkRefused_ = true;
      return false;
    }
    return pushPaper(planned_.dstY);
  }

  bool row(const uint8_t* px) override {
    if (stop_ != nullptr && (srcRows_ % kStopEveryRows) == 0 && stop_(ctx_)) {
      stopped_ = true;
      return false;
    }
    ++srcRows_;
    if (!fitter_.addRow(px)) {
      // UNREACHABLE while a decoder pushes exactly the height it declared, which
      // both of ours do -- pngd's loop runs `height` times and jpegd refuses a
      // band count that disagrees with its own. Kept because what it guards is a
      // plane taller than the sink was promised, and because the alternative to a
      // refusal here is silence.
      fitFailed_ = true;
      return false;
    }
    // DRAINED, NOT TESTED ONCE. One source row completes several destination rows
    // when the cover is being enlarged (imagefit.h), and this is the whole of
    // what that costs the adapter -- a `while` where the old interface's
    // `if (emitted)` stood.
    while (fitter_.nextRow()) {
      if (!out_->row(fitter_.msbRow(), fitter_.lsbRow())) {
        sinkRefused_ = true;
        return false;
      }
      ++written_;
    }
    return true;
  }

  // The band below the cover. Called once, after a decode that succeeded, so the
  // sink has had exactly `panelH` rows -- which is what begin() promised it.
  bool pad() { return pushPaper(panelH_ - written_); }

  bool declared() const { return declared_; }
  // WHAT THE DECODER HANDED OVER, which for a JPEG is already scaled -- the file's
  // own dimensions come from JpegDecoder, which is the only layer that knows them.
  int width() const { return width_; }
  int height() const { return height_; }
  // WHERE THE COVER LANDED, OR WOULD HAVE. `planned_` rather than `fitter_.box()`
  // because the fitter resets its box on a refusal, and a TooSmall refusal is
  // exactly the case where the box is the interesting half of the diagnosis: the
  // reason is a fixed sentence, so `dst=260x346+134+223` in the log line is what
  // says by how much the cover missed. The two are the same box whenever begin()
  // succeeded -- one pure function, the same four arguments.
  const FitBox& box() const { return planned_; }
  bool stopped() const { return stopped_; }
  bool outOfMemory() const { return oom_; }
  bool tooSmall() const { return tooSmall_; }
  bool sinkRefused() const { return sinkRefused_; }
  bool fitFailed() const { return fitFailed_; }

 private:
  // BOTH PLANES FROM ONE BUFFER. Paper is 0xFF in each -- imagefit.h's "a set bit
  // is paper" -- so two buffers would be two copies of one row.
  bool pushPaper(int rows) {
    for (int i = 0; i < rows; ++i) {
      if (!out_->row(paper_.get(), paper_.get())) {
        sinkRefused_ = true;
        return false;
      }
      ++written_;
    }
    return true;
  }

  CoverPlaneSink* out_;
  CoverFitter fitter_;
  FitBox planned_;
  std::unique_ptr<uint8_t[]> paper_;
  int panelW_ = 0, panelH_ = 0, planeBytes_ = 0;
  CoverFit fit_ = CoverFit::Fill;
  CoverStopFn stop_ = nullptr;
  void* ctx_ = nullptr;
  int width_ = 0, height_ = 0;   // as handed over, which for a JPEG is after scaling
  int srcRows_ = 0;   // source rows offered, which is what the stop predicate paces on
  int written_ = 0;   // rows pushed to the sink, letterbox included
  bool declared_ = false, stopped_ = false, oom_ = false;
  bool sinkRefused_ = false, fitFailed_ = false, tooSmall_ = false;
};

}  // namespace

const char* coverResultName(CoverResult r) {
  switch (r) {
    case CoverResult::Ok: return "Ok";
    case CoverResult::NoCover: return "NoCover";
    case CoverResult::Unsupported: return "Unsupported";
    case CoverResult::ReadFailed: return "ReadFailed";
    case CoverResult::OutOfMemory: return "OutOfMemory";
    case CoverResult::Abandoned: return "Abandoned";
    case CoverResult::TooSmall: return "TooSmall";
  }
  return "?";
}

CoverResult decodeCover(FileSystem& fs, const OpenedBook& book, int panelW, int panelH,
                        CoverFit fit, CoverPlaneSink& sink, CoverStopFn stop, void* stopCtx,
                        CoverReport* report) {
  // A LOCAL REPORT, FILLED AS THE DECODE GOES AND HANDED OVER IN ONE PLACE. Every
  // branch below is an early return, so a `report != nullptr` test at each of them
  // would be a dozen chances to leave a caller with nothing to log.
  CoverReport rep;
  auto deliver = [&](CoverResult r, const char* why) -> CoverResult {
    rep.reason = why;
    if (report != nullptr) *report = rep;
    return r;
  };
  // AND EVERY REFUSAL GOES THROUGH ONE PLACE TOO, so `finish` cannot be forgotten
  // on one of them -- it is called even when `begin` was not, which cover.h states
  // as the contract: it is the sink's one guaranteed call, so a stale file from
  // another book is dropped on the way past.
  auto refuse = [&](CoverResult r, const char* why) -> CoverResult {
    sink.finish(false);
    return deliver(r, why);
  };

  // A caller-side error rather than anything about the book. The seven results have
  // no name for one, so it borrows the nearest and the reason carries the truth;
  // what matters is refusing HERE, before the card is touched and before
  // CoverFitter is handed a geometry it would report as OutOfMemory.
  //
  // AND THE UPPER BOUND IS NOT TIDINESS: `(panelW + 7) / 8` below is signed
  // arithmetic, so a panel near INT_MAX is undefined behaviour before it is
  // anything else. kMaxPanel is four orders of magnitude above the widest panel
  // this firmware drives (800), so nothing real is near it and the overflow is
  // unreachable rather than merely unlikely.
  if (panelW <= 0 || panelH <= 0 || panelW > kMaxPanel || panelH > kMaxPanel)
    return refuse(CoverResult::Unsupported, "the panel is not a panel this can draw");

  const ChapterLocation where = book.locateCover();
  if (where.bookPath.empty() || where.compressedSize == 0)
    return refuse(CoverResult::NoCover, "this book declares no cover");

  std::unique_ptr<FileHandle> file = fs.openRead(where.bookPath);
  if (file == nullptr)
    return refuse(CoverResult::ReadFailed, "cannot open the book file");

  uint32_t dataOffset = 0;
  if (!Zip::locateData(*file, where.localHeaderOffset, where.compressedSize, dataOffset))
    return refuse(CoverResult::ReadFailed, "the cover's local header will not parse");

  // DECLARATION ORDER IS THE LIFETIME ORDER, innermost last: the inflater reads
  // through the entry and the source reads through the inflater, and locals are
  // destroyed in reverse.
  EntrySource entry;
  entry.reset(*file, dataOffset, where.compressedSize);
  Inflater inflater;
  InflateSource inflated(inflater);

  ByteSource* bytes = &entry;
  if (where.deflated) {
    // 59% of the corpus's JPEG covers are here, and this is the window that makes
    // a deflated PNG the one shape that can run out of memory: 36,956 bytes, and
    // the PNG decoder then wants its own.
    if (!inflater.begin(entry))
      return refuse(CoverResult::OutOfMemory, "no memory for the cover's inflate window");
    bytes = &inflated;
  }

  // Value-initialised: `sniff` reads only what `got` covers, and an entry shorter
  // than eight bytes is a real card, so leaving the tail as whatever the stack
  // held would make the refusal depend on it.
  uint8_t head[kSniffBytes] = {};
  size_t got = 0;
  while (got < kSniffBytes) {
    const size_t n = bytes->read(head + got, kSniffBytes - got);
    if (n == 0) break;
    got += n;
  }
  const Format format = sniff(head, got);
  if (format == Format::None)
    return refuse(CoverResult::Unsupported, "the cover is neither a JPEG nor a PNG");

  PrefixSource src(head, got, *bytes);
  PlaneAdapter adapter(sink, panelW, panelH, fit, stop, stopCtx);

  bool ok = false;
  bool decoderOom = false;
  const char* decoderReason = nullptr;
  if (format == Format::Jpeg) {
    JpegDecoder dec;
    // THE PANEL IS THE `atLeast` PAIR, and it is exactly sufficient rather than
    // merely safe. TJpgDec halves out of the IDCT for free but never below the
    // request, so the output is at least panelW x panelH -- and a Fill crop keeps
    // `srcH * panelW / panelH` columns, which is at least panelW precisely when
    // srcH is at least panelH. The other axis is the same statement transposed,
    // and Whole never asks for more than Fill. So no scale that clears this
    // request can make the fit box smaller than the panel, and asking for the
    // BOX instead -- which is not known until begin() -- would buy nothing.
    ok = dec.decode(src, adapter, panelW, panelH);
    decoderReason = dec.reason();
    decoderOom = dec.outOfMemory();
    // FROM THE DECODER, NOT FROM THE ADAPTER: the adapter is handed the SCALED
    // dimensions, and what a corpus probe wants to know is what the file said.
    rep.sourceWidth = dec.sourceWidth();
    rep.sourceHeight = dec.sourceHeight();
    rep.scaleDivisor = dec.scaleDivisor();
  } else {
    // PNG has no such lever: every scanline must be inflated and unfiltered
    // whatever the caller wants of it, because the next row's unfilter reads this
    // one. pngd.h states the absence as the point -- and says so by not offering
    // the dimensions either, since with no scaling they are exactly what the sink
    // was handed.
    PngDecoder dec;
    ok = dec.decode(src, adapter);
    decoderReason = dec.reason();
    decoderOom = dec.outOfMemory();
    rep.sourceWidth = adapter.width();
    rep.sourceHeight = adapter.height();
    rep.scaleDivisor = 1;
  }
  const FitBox& box = adapter.box();
  rep.dstX = box.dstX;
  rep.dstY = box.dstY;
  rep.dstW = box.dstW;
  rep.dstH = box.dstH;

  if (ok) {
    if (!adapter.pad())
      return refuse(CoverResult::ReadFailed, "the cover's plane rows could not be written");
    // NOT THROUGH `refuse`: finish has already been called, with true, and calling
    // it a second time would break the "exactly once" half of its contract to
    // report a failure the sink itself just declared.
    if (!sink.finish(true))
      return deliver(CoverResult::ReadFailed, "the cover could not be committed");
    return deliver(CoverResult::Ok, nullptr);
  }

  // WHY IT STOPPED, ASKED OF OURSELVES BEFORE THE DECODER. Both decoders report a
  // sink that said no as `aborted()`, so the decoder cannot tell a button press
  // from a fitter that could not allocate from a card that would not take a row --
  // only the adapter knows which of its own returns was the false.
  if (adapter.stopped())
    return refuse(CoverResult::Abandoned, nullptr);  // nothing was wrong; nothing to say
  // AND A PICTURE TOO SMALL TO ENLARGE IS ASKED BEFORE EVERY ALLOCATION QUESTION,
  // because it is not one: nothing failed, and the file is fine. It is above the
  // OutOfMemory line rather than below it because that is the false it used to
  // arrive as -- CoverFitter::begin refuses both with one bool, so the order here
  // is what keeps a small cover from being logged as a device that ran out of
  // room. `report.dst*` carries the 1:1 box the picture would have occupied, so
  // the line says by how much it missed.
  if (adapter.tooSmall())
    return refuse(CoverResult::TooSmall, "this cover is too small to fill the panel");
  // WHICH OF THIS FUNCTION'S FIVE OutOfMemory SITES A TEST CAN REACH, written down
  // rather than left to be rediscovered. They are: the zip's inflate window above,
  // CoverFitter::begin, the paper row, and each decoder's own (below). FOUR OF THE
  // FIVE ARE ALLOCATION FAILURES and none is reachable from a desktop test --
  // `FileSystem` has no heap injection and nothing else here is behind a seam, so
  // provoking one means asking the host for a block big enough to fail, which on a
  // 64-bit machine with overcommit is either served or fatal rather than refused.
  //
  // THE FIFTH IS REACHABLE, and it is this one: CoverFitter::begin answers the same
  // false for a geometry whose worst accumulator cell could exceed uint32 as it does
  // for a block it could not take, and that guard trips on declared dimensions
  // alone -- so a PNG whose IHDR says 5000x5000 fitted to a 1x1 panel reaches it for
  // the cost of a 15 KB row block. test_cover.cpp takes that route. Both causes are
  // a capacity refusal rather than a fault in the file, which is why one result
  // serves them; the reason below deliberately says "fit", not "allocate".
  if (adapter.outOfMemory())
    return refuse(CoverResult::OutOfMemory, "this cover cannot be fitted to this panel");
  if (adapter.sinkRefused())
    return refuse(CoverResult::ReadFailed, "the cover's plane rows could not be written");
  if (adapter.fitFailed())
    return refuse(CoverResult::Unsupported, "the cover decoded to more rows than it declares");
  // AND A SHORTFALL INSIDE THE DECODER IS STILL A SHORTFALL. This is asked before
  // the declared/undeclared split below, because that split answers a question
  // about the FILE and a decoder that ran out of memory is not telling you one:
  //
  //   * pngd asks its sink BEFORE taking its 37 KB window, deliberately (a Task 3
  //     decision, so that a sink which cannot allocate is refused first) -- so the
  //     deflated PNG that genuinely does not fit is DECLARED when it fails, and
  //     without this line it reported ReadFailed. A card fault, for the one shape
  //     in the corpus whose only problem is that this device is too small.
  //   * jpegd's row band fails the other way round, before begin(), and reported
  //     Unsupported -- "not an image we read", for an image we read perfectly well
  //     on a panel with room.
  //
  // One question asked once fixes both, and it is the same shape as the abandoned
  // check above: what stopped the decode is not always what is wrong with the file.
  //
  // THIS LINE IS NOT REACHED BY ANY TEST, and mutating it away fails nothing --
  // written down rather than left to be rediscovered. Both decoder-side sites are
  // real allocation failures, and provoking one means asking a 64-bit host for a
  // block big enough to be refused, which it either serves (overcommit) or dies
  // on. What IS pinned is the other half, in both decoders' own suites: every
  // refusal that is about the FILE leaves outOfMemory() false, so the flag cannot
  // start firing for a progressive JPEG or an interlaced PNG. Setting `oom` in
  // fail() instead of failOom() fails 8 assertions in test_jpegd and 20 in
  // test_pngd.
  if (decoderOom)
    return refuse(CoverResult::OutOfMemory, decoderReason);
  // AND THE LAST SPLIT IS "DID THE PICTURE EVER DECLARE ITSELF". A refusal before
  // begin() is a format we do not read; one after it is a fault in bytes that
  // parsed.
  return refuse(adapter.declared() ? CoverResult::ReadFailed : CoverResult::Unsupported,
                decoderReason);
}

}  // namespace reader
