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

    // THE FITTER FIRST, THE SINK SECOND, and the order is deliberate: the fitter
    // is the allocation that can fail on device, and a sink that opens a file
    // should not have been asked to do it for a cover that was never going to be
    // fitted. It is the mirror of pngd.cpp asking its sink before taking its
    // window, for the same reason -- put the refusal that costs nothing first.
    if (!fitter_.begin(width, height, panelW_, panelH_, fit_)) {
      oom_ = true;
      return false;
    }
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
    return pushPaper(fitter_.box().dstY);
  }

  bool row(const uint8_t* px) override {
    if (stop_ != nullptr && (srcRows_ % kStopEveryRows) == 0 && stop_(ctx_)) {
      stopped_ = true;
      return false;
    }
    ++srcRows_;
    bool emitted = false;
    if (!fitter_.addRow(px, emitted)) {
      // UNREACHABLE while a decoder pushes exactly the height it declared, which
      // both of ours do -- pngd's loop runs `height` times and jpegd refuses a
      // band count that disagrees with its own. Kept because what it guards is a
      // plane taller than the sink was promised, and because the alternative to a
      // refusal here is silence.
      fitFailed_ = true;
      return false;
    }
    if (!emitted) return true;
    if (!out_->row(fitter_.msbRow(), fitter_.lsbRow())) {
      sinkRefused_ = true;
      return false;
    }
    ++written_;
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
  const FitBox& box() const { return fitter_.box(); }
  bool stopped() const { return stopped_; }
  bool outOfMemory() const { return oom_; }
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
  std::unique_ptr<uint8_t[]> paper_;
  int panelW_ = 0, panelH_ = 0, planeBytes_ = 0;
  CoverFit fit_ = CoverFit::Fill;
  CoverStopFn stop_ = nullptr;
  void* ctx_ = nullptr;
  int width_ = 0, height_ = 0;   // as handed over, which for a JPEG is after scaling
  int srcRows_ = 0;   // source rows offered, which is what the stop predicate paces on
  int written_ = 0;   // rows pushed to the sink, letterbox included
  bool declared_ = false, stopped_ = false, oom_ = false;
  bool sinkRefused_ = false, fitFailed_ = false;
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

  // A caller-side error rather than anything about the book. The six results have
  // no name for one, so it borrows the nearest and the reason carries the truth;
  // what matters is refusing HERE, before the card is touched and before
  // CoverFitter is handed a geometry it would report as OutOfMemory.
  if (panelW <= 0 || panelH <= 0)
    return refuse(CoverResult::Unsupported, "the panel has no pixels");

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
  if (adapter.outOfMemory())
    return refuse(CoverResult::OutOfMemory, "no memory to fit this cover to the panel");
  if (adapter.sinkRefused())
    return refuse(CoverResult::ReadFailed, "the cover's plane rows could not be written");
  if (adapter.fitFailed())
    return refuse(CoverResult::Unsupported, "the cover decoded to more rows than it declares");
  // A decoder that failed with nothing to say is one whose own construction could
  // not allocate -- both are pimpls that answer a null reason in that state.
  if (decoderReason == nullptr)
    return refuse(CoverResult::OutOfMemory, "no memory for the cover decoder");
  // AND THE LAST SPLIT IS "DID THE PICTURE EVER DECLARE ITSELF". A refusal before
  // begin() is a format we do not read; one after it is a fault in bytes that
  // parsed. See cover.h for the case this gets wrong and why the reason is handed
  // back rather than the enum being widened.
  return refuse(adapter.declared() ? CoverResult::ReadFailed : CoverResult::Unsupported,
                decoderReason);
}

}  // namespace reader
