#include "reader/imagefit.h"

#include <memory>
#include <new>

namespace reader {
namespace {

// round(a * b / c) for non-negative arguments. In 64-bit because the products
// are pixel counts times pixel counts -- 1600 * 2400 is nothing, but a decoder
// that reported a nonsense size would overflow 32 bits before the guards in
// fitCover could look at the answer.
int mulDivRound(long long a, long long b, long long c) {
  if (c <= 0) return 0;
  return static_cast<int>((a * b + c / 2) / c);
}

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// THE FOUR GREY LEVELS THE PANEL HAS, in the grey the decoders speak: 0 is
// black, 255 is paper, and the two mid steps are 85 and 170. `+42` is half a
// step, so this is round-to-nearest; the division truncates toward zero, which
// for a value below -42 would give 0 rather than -1 and is why the clamp is
// separate rather than folded in.
//
// The INDEX runs light-to-dark's opposite -- 3 is paper -- because grey does.
// The caller turns it into a coverage level with `3 - idx`, which is the one
// place this file crosses from "how bright" to "how much ink".
int quantiseIndex(int v) {
  int idx = (v + 42) / 85;
  return clampInt(idx, 0, 3);
}

}  // namespace

FitBox fitCover(int srcW, int srcH, int panelW, int panelH, CoverFit fit) {
  FitBox b;
  if (srcW <= 0 || srcH <= 0 || panelW <= 0 || panelH <= 0) return b;

  b.srcW = srcW;
  b.srcH = srcH;
  const long long sw = srcW, sh = srcH, pw = panelW, ph = panelH;
  // Cross-multiplied rather than divided, so the comparison is exact: two
  // aspects that differ in the fourth decimal place decide which axis binds,
  // and a double would be deciding it by rounding.
  const long long srcSpread = sw * ph, panelSpread = pw * sh;

  if (fit == CoverFit::Fill) {
    // COVER: the destination is the whole panel and the SOURCE gives way. The
    // crop rectangle carries the panel's aspect, so the two scales are equal up
    // to one rounding step -- which is why the upscale-cap test below would
    // almost always answer the same from either axis, and why it asks both
    // anyway: "almost" is not a property to rest a bound on.
    b.dstW = panelW;
    b.dstH = panelH;
    if (srcSpread > panelSpread) {
      // The source is relatively WIDER, so the height fits and the width is cut.
      // This is the overwhelmingly common case on both panels; the census in
      // reader/cover_fit.h has the split.
      b.srcW = clampInt(mulDivRound(sh, pw, ph), 1, srcW);
      b.srcX = (srcW - b.srcW) / 2;
    } else if (srcSpread < panelSpread) {
      // The source is relatively TALLER, so rows go. 0.4 of the loss comes off
      // the TOP rather than half of it: a cover's title band sits low, and an
      // evenly centred crop eats it from the bottom. Round half up, in integers:
      // floor(0.4d + 0.5) == (4d + 5) / 10.
      b.srcH = clampInt(mulDivRound(sw, ph, pw), 1, srcH);
      const long long lost = srcH - b.srcH;
      b.srcY = static_cast<int>((lost * 4 + 5) / 10);
    }
    // An exactly-matching aspect crops neither, which is the X3 with a 2:3
    // cover -- most of the corpus, where this whole setting is a no-op. The
    // count is in the census in reader/cover_fit.h.
  } else {
    // CONTAIN: the source is whole and the DESTINATION gives way, leaving bands
    // for the caller to tint.
    if (srcSpread > panelSpread) {
      b.dstW = panelW;
      b.dstH = clampInt(mulDivRound(sh, pw, sw), 1, panelH);
    } else {
      b.dstH = panelH;
      b.dstW = clampInt(mulDivRound(sw, ph, sh), 1, panelW);
    }
  }

  // UPSCALE UP TO kMaxCoverUpscalePercent, AND FALL BACK TO 1:1 BEYOND IT. The
  // header carries the derivation of the cap and the arc of the property this
  // replaced; what matters here is that the fallback geometry is EXACTLY what
  // this function used to return for such a cover, so `tooSmall` is the only
  // observable that moved for a caller that does not enlarge.
  //
  // BOTH AXES, OR-ed, WHICH IS THE STRICT READING. Fill's crop carries the
  // panel's aspect and Whole's box does too, so the two scales are equal up to
  // one rounding step and either axis would almost always answer the same -- the
  // `||` is what makes "almost" not matter.
  //
  // The comparison is cross-multiplied rather than divided for fitCover's own
  // reason two dozen lines up: a cover asking for exactly the cap must be
  // ADMITTED, and a double deciding that by rounding is not a boundary anybody
  // can reason about. In long long because dst * 250 on a nonsense panel would
  // otherwise overflow before the guards could look at it.
  const long long cap = kMaxCoverUpscalePercent;
  if (static_cast<long long>(b.dstW) * 100 > static_cast<long long>(b.srcW) * cap ||
      static_cast<long long>(b.dstH) * 100 > static_cast<long long>(b.srcH) * cap) {
    b.tooSmall = true;
    b.dstW = b.srcW;
    b.dstH = b.srcH;
  }
  // Centred last, so it is stated once for every branch above. A Fill that was
  // not clamped lands at (0, 0) by construction, since dstW == panelW there.
  //
  // THIS ROUNDS DOWN AND text.h's centreIn ROUNDS UP, deliberately, and the
  // next person to place something against box().dstX needs to know before they
  // reach for the shared one. centreIn is `(slack + 1) / 2` because the boards
  // are rasterised by Chrome and Chrome rounds a half-pixel offset up; matching
  // it is what keeps a centred label on the pixel the board put it on. This is
  // not chrome -- it is where a photograph lands -- so there is no board to
  // agree with, and plain halving is the unsurprising answer. They differ by a
  // pixel whenever the slack is odd, which is about two combinations in five,
  // so a caption centred with centreIn over a box placed with this will sit a
  // pixel off unless it is centred on box().dstX + box().dstW / 2 instead.
  b.dstX = (panelW - b.dstW) / 2;
  b.dstY = (panelH - b.dstH) / 2;
  return b;
}

bool CoverFitter::begin(int srcW, int srcH, int panelW, int panelH, CoverFit fit) {
  box_ = FitBox{};
  planeBytes_ = 0;
  srcH_ = 0;
  srcRow_ = dstRow_ = readyRow_ = 0;
  upCols_ = upRows_ = false;
  acc_.clear();
  count_.clear();
  err_.clear();
  msb_.clear();
  lsb_.clear();

  if (srcW <= 0 || srcH <= 0 || panelW <= 0 || panelH <= 0) return false;
  const FitBox b = fitCover(srcW, srcH, panelW, panelH, fit);
  if (b.dstW <= 0 || b.dstH <= 0 || b.srcW <= 0 || b.srcH <= 0) return false;
  // A COVER TOO SMALL TO REACH THE PANEL IS REFUSED HERE, WHICH IS WHAT MAKES THE
  // FLAG WORTH HAVING. fitCover still hands back the 1:1 centred box for such a
  // cover -- it is a total function and that geometry is informative for a log
  // line -- but nothing may DRAW it: a small picture in the middle of the glass
  // for hours is #64, and this is the one place that can make it unreachable for
  // every caller at once. decodeCover asks fitCover itself so it can report
  // CoverResult::TooSmall rather than the OutOfMemory this false would otherwise
  // read as.
  if (b.tooSmall) return false;
  // AND THIS ONE IS MEMORY SAFETY, NOT CORRECTNESS, which is why it is a guard
  // and not a comment. emitRow() writes bit `0x80 >> ((dstX + c) & 7)` into
  // byte `(dstX + c) >> 3` of a (panelW + 7) / 8 buffer, so what keeps it in
  // bounds is exactly this -- and until now that was argued four rounding
  // branches deep inside fitCover and swept by a test, which is evidence about
  // today's fitCover rather than a guarantee about tomorrow's.
  //
  // DELETING IT FAILS NOTHING, and that is written down rather than left to be
  // rediscovered -- the THIRD such guard in this file, after `: 255` in
  // emitRow() and the `dstRow_ >= dstH` early-out in addRow(). It cannot fire
  // while fitCover clamps dstW to at most panelW and centres with a
  // non-negative slack, which the 294-shape sweep in test_imagefit.cpp checks
  // directly. There is no way to make it bite without injecting a bad FitBox,
  // and begin() computes its own. It is here so that a rounding slip in
  // fitCover becomes a refused cover instead of a write past the end of a
  // plane row.
  if (b.dstX < 0 || b.dstX + b.dstW > panelW) return false;
  // THE ACCUMULATOR'S RANGE, refused rather than wrapped. A cell takes at most
  // ceil(srcW / dstW) * ceil(srcH / dstH) samples of at most 255 each, and acc_
  // is uint32_t, so a geometry whose worst cell could exceed that is refused
  // here instead of silently summing modulo 2^32. Nothing shaped like a book
  // cover comes near it -- 1400x2100 into 480x800 is 9 samples a cell -- and
  // PNG's 31-bit dimensions with no cap upstream are what make it reachable at
  // all. See the members' note in the header for the JPEG/PNG asymmetry.
  //
  // The ceilings are `(a - 1) / b + 1` rather than `(a + b - 1) / b` so both
  // stay in 32 bits: every value here is at least 1, so no addition can
  // overflow, and RV32IMC HAS a 32-bit divider. The obvious spelling promoted
  // to long long and put two `__divdi3` calls into begin() -- once per cover
  // rather than per pixel, so it cost nothing measurable, but adding them back
  // in the change that removes them from addRow is not a thing to do by
  // accident. Only the PRODUCT needs 64 bits.
  //
  // AN ENLARGEMENT MAKES BOTH CEILINGS 1 and cannot come near the bound -- with
  // dstW > srcW, `(srcW - 1) / dstW` is 0. So this guard is still exactly the
  // downscale's, which is the direction that can pile millions of samples into
  // one cell.
  {
    const int perCol = (b.srcW - 1) / b.dstW + 1;
    const int perRow = (b.srcH - 1) / b.dstH + 1;
    if (static_cast<long long>(perCol) * perRow > 16843009LL) return false;  // (2^32 - 1) / 255
  }

  const int planeBytes = (panelW + 7) / 8;
  // A NOTHROW PRE-FLIGHT, because the members are std::vector and a resize that
  // cannot allocate is an abort() with no diagnostic under -fno-exceptions --
  // which is exactly what begin()'s contract promises not to do. One probe for
  // the sum, taken and released immediately before the resizes, is not a proof
  // (an allocator could fail the second request having served the first) but it
  // turns a certain abort into a vanishingly unlikely one at a cost of ~4.3 KB
  // held for the length of this function.
  //
  // THE VECTORS STAY BECAUSE NOTHING DEPENDS ON THIS HEADER BEING CHEAP -- the
  // one thing that would have, the Settings screen, reaches CoverFit through
  // reader/cover_fit.h instead. An earlier version of this comment justified
  // them by saying raw arrays would cost the header <memory>; measured, that is
  // backwards. `clang++ -std=c++20 -E`: <memory> is 38,447 preprocessed lines
  // and <vector> is 72,845, so the alternative is HALF the weight, not more.
  // The probe is here for the -fno-exceptions reason above and for no other.
  //
  // THE SIZES ARE DERIVED FROM THE CONTAINERS, NOT RESTATED. This asked for a
  // uint16_t's worth of `count_` for one commit after that vector was widened to
  // uint32_t -- so it under-asked by 2 * dstW bytes, in the one direction a probe
  // must never be wrong. Spelling the element types by hand is a second copy of a
  // fact the declarations already carry, which is exactly what drifted.
  const size_t need = sizeof(decltype(acc_)::value_type) * static_cast<size_t>(b.dstW) +
                      sizeof(decltype(count_)::value_type) * static_cast<size_t>(b.dstW) +
                      sizeof(decltype(err_)::value_type) * static_cast<size_t>(b.dstW) +
                      2u * static_cast<size_t>(planeBytes);
  {
    std::unique_ptr<uint8_t[]> probe(new (std::nothrow) uint8_t[need]);
    if (!probe) return false;
  }

  box_ = b;
  planeBytes_ = planeBytes;
  srcH_ = srcH;
  upCols_ = b.dstW > b.srcW;
  upRows_ = b.dstH > b.srcH;
  acc_.assign(static_cast<size_t>(b.dstW), 0u);
  count_.assign(static_cast<size_t>(b.dstW), 0u);
  err_.assign(static_cast<size_t>(b.dstW), 0);
  msb_.assign(static_cast<size_t>(planeBytes), 0xFFu);
  lsb_.assign(static_cast<size_t>(planeBytes), 0xFFu);
  return true;
}

bool CoverFitter::addRow(const uint8_t* src) {
  if (planeBytes_ == 0) return false;   // never begun, or begin() refused
  if (src == nullptr) return false;
  if (srcRow_ >= srcH_) return false;   // more rows than the source declared
  // AN UNDRAINED PUSH IS MISUSE, AND IT IS THE ONE THE NEW INTERFACE INTRODUCES.
  // Under a downscale nothing is ever pending on entry, so this cannot fire for
  // any geometry that shipped; under an enlargement a caller that kept the old
  // `if (emitted)` shape would drop every second destination row AND add the next
  // source row on top of an accumulator still holding the last one. That is a
  // blended picture rather than a missing one, so it must not be silent.
  if (readyRow_ != dstRow_) return false;

  const int sr = srcRow_++;
  // ROWS OUTSIDE THE CROP ARE CONSUMED AND DISCARDED. That is what lets the
  // caller push a whole JPEG through without knowing there is a crop at all --
  // the decoder has to emit them anyway, and refusing them would stop the
  // decode at the top of the picture.
  if (sr < box_.srcY || sr >= box_.srcY + box_.srcH) return true;
  // UNREACHABLE, like the `: 255` below and for the same reason: the row map is
  // monotonic and lands every crop row in 0..dstH-1, so the box cannot fill
  // before the crop is spent. Kept because what it guards is emitRow()
  // incrementing dstRow_ past dstH, which the caller would read as a plane
  // taller than the box it was told about.
  if (dstRow_ >= box_.dstH) return true;

  const int i = sr - box_.srcY;
  const uint8_t* const row = src + box_.srcX;
  if (upCols_) {
    // THE INVERSE MAP, FOR AN ENLARGEMENT: walk the DESTINATION and read the one
    // source pixel each cell sits on. That is nearest-neighbour, and the header
    // says why nothing else is available to a box filter and why the upscale cap
    // is what makes it sufficient.
    //
    // STEPPED FOR THE FORWARD MAP'S REASON, WHICH IS RV32IMC'S MISSING 64-BIT
    // DIVIDER. `sj` is `floor(c * srcW / dstW)` exactly: it advances by
    // `srcW / dstW`, which with `dstW > srcW` is below one, so carrying the
    // remainder reproduces the floor -- `rem` is `(c * srcW) % dstW` by
    // construction and the advance happens AFTER the read, so c == 0 reads
    // source pixel 0. Cross-compiled and read: no `call` of any kind in this
    // loop body. `while` rather than `if` because the invariant that bounds the
    // carry to one step is `dstW > srcW`, which is upCols_'s own condition -- a
    // relaxation of it would make an `if` drop pixels silently.
    //
    // `sj` CANNOT LEAVE THE ROW: its largest value is
    // floor((dstW - 1) * srcW / dstW), which is at most srcW - 1.
    int sj = 0, rem = 0;
    for (int c = 0; c < box_.dstW; ++c) {
      acc_[static_cast<size_t>(c)] += row[sj];
      count_[static_cast<size_t>(c)] += 1u;
      rem += box_.srcW;
      while (rem >= box_.dstW) {
        rem -= box_.dstW;
        ++sj;
      }
    }
    return markReady(i);
  }
  // The box filter: every source pixel is added to the one destination cell it
  // lands in. `dstW <= srcW` here (upCols_ took the branch above) makes the map
  // onto 0..dstW-1 surjective, so no destination column can end up with an empty
  // accumulator.
  //
  // THE COLUMN IS STEPPED, NOT DIVIDED, AND THAT IS A DEVICE FACT NO DESKTOP
  // MEASUREMENT CAN SEE. The obvious spelling of this map is
  //
  //     const int dc = static_cast<int>(static_cast<long long>(j) * dstW / srcW);
  //
  // and on x86-64 it is one hardware `idiv`, benchmarked at 3.19 ms a cover
  // against 3.19 for the form below -- no difference at all. RV32IMC HAS NO
  // 64-BIT DIVIDER. Compiled with the project's own toolchain
  // (riscv32-esp-elf-g++ -Os) that line emitted `mulh`/`mul` and a
  // `call __divdi3` INSIDE the per-pixel loop body -- a libgcc shift-subtract
  // routine, ~100-200 cycles, run once for every source pixel. A median cover
  // cropped to the X4 is ~2.65 M source pixels, so 1.7-3.3 s at 160 MHz in that
  // one call.
  //
  // This is the project's desktop-to-device ratio trap in a new place, and the
  // sharpest version of it yet: the ratio here is not 37x or 135x, it is
  // infinite, because the desktop cost is zero. `kEagerCountBytes` has the
  // same note for the same reason.
  //
  // The step is bit-identical rather than approximate. `dstW <= srcW` means
  // `j * dstW / srcW` advances by 0 or 1 per pixel, so carrying the remainder
  // reproduces the floor exactly -- `rem` is `(j * dstW) % srcW` by
  // construction. Verified over the whole test suite: not one output bit moved.
  //
  // The row map below keeps the divide. It runs once per source ROW.
  int dc = 0, rem = 0;
  for (int j = 0; j < box_.srcW; ++j) {
    acc_[static_cast<size_t>(dc)] += row[j];
    count_[static_cast<size_t>(dc)] += 1u;
    rem += box_.dstW;
    if (rem >= box_.srcW) {
      rem -= box_.srcW;
      ++dc;
    }
  }

  return markReady(i);
}

bool CoverFitter::markReady(int i) {
  // WHICH DESTINATION ROWS ARE NOW FINISHED, and this is the one place the two
  // directions need DIFFERENT arithmetic rather than the same expression read
  // twice.
  //
  // THE ROW MAP MUST BE THE COLUMN MAP'S INVERSE ON THE SAME AXIS CONVENTION, and
  // getting that wrong is a real defect rather than a taste -- it was caught here
  // by the reference disagreeing, and it would have shipped as a picture whose
  // rows are sampled a step out of phase with its columns. That is a cover
  // sheared by one source pixel down its whole height: still a picture, so
  // nothing but a reference comparison could see it.
  //
  //   DOWNSCALING, the columns SCATTER: source pixel j lands in cell
  //   floor(j * dstW / srcW), so cell c holds the j that map to it, and a
  //   destination row is finished when the NEXT source row lands past it --
  //   floor((i + 1) * dstH / srcH), which is the expression this function's
  //   predecessor inlined. Identical boundaries, so nothing downscaling moved.
  //
  //   ENLARGING, the columns GATHER: cell c reads source pixel
  //   floor(c * srcW / dstW). The rows have to say the same thing, so source row
  //   i owes the destination rows r with floor(r * srcH / dstH) == i, which is
  //   r < CEIL((i + 1) * dstH / srcH). A floor there would have handed
  //   destination row 1 of a 33-to-64 enlargement to source row 1 where its
  //   COLUMNS were reading source row 0.
  //
  // THE ONLY OTHER THING THAT CHANGED IS THAT THIS IS A COUNT RATHER THAN A
  // YES/NO. Downscaling, `readyRow_` is at most one past `dstRow_`, so nextRow()
  // answers true once and the caller's drain loop is the old `if (emitted)`.
  //
  // The row map keeps its divide: it runs once per source ROW, not per pixel, so
  // the `__divdi3` the column map exists to avoid costs nothing here.
  if (i + 1 >= box_.srcH) {
    readyRow_ = box_.dstH;
  } else {
    const long long num = static_cast<long long>(i + 1) * box_.dstH;
    readyRow_ = static_cast<int>(upRows_ ? (num + box_.srcH - 1) / box_.srcH
                                         : num / box_.srcH);
  }
  // UNREACHABLE with a monotonic map, and it is the mirror of the `dstRow_ >=
  // dstH` early-out above: what it guards is emitRow() being invited past the box
  // the caller was told about.
  if (readyRow_ > box_.dstH) readyRow_ = box_.dstH;
  return true;
}

bool CoverFitter::nextRow() {
  if (dstRow_ >= readyRow_) return false;
  // KEEP THE ACCUMULATOR WHILE MORE ROWS ARE OWED FROM IT, which is the whole of
  // the vertical replication: an enlargement's several destination rows all mean
  // the same source row. err_ still advances per emitted row, so they are the
  // same tone in DIFFERENT dither patterns rather than duplicate rows.
  emitRow(dstRow_ + 1 < readyRow_);
  return true;
}

void CoverFitter::emitRow(bool keepAccumulator) {
  // THE BANDS ARE PAPER. A Whole fit leaves columns outside the box and every
  // row leaves them; starting from paper rather than from whatever the last row
  // left is also what keeps a plane row a complete statement rather than a
  // delta on its predecessor.
  for (size_t i = 0; i < msb_.size(); ++i) {
    msb_[i] = 0xFFu;
    lsb_[i] = 0xFFu;
  }

  // FLOYD-STEINBERG WITH ONE CARRIED ROW, which is the whole reason this
  // streams. On entry err_[c] holds what the row ABOVE diffused into column c;
  // it is consumed and immediately reused as the accumulator for the row below.
  //
  // Two scalars carry what cannot go in the array yet:
  //   carry7 -- the 7/16 this pixel sends RIGHT, into the pixel about to be
  //             read, so it must not be in err_ (which is now the next row's).
  //   below1 -- the 1/16 this pixel sends to the next row's RIGHT neighbour.
  //             That cell's err_ entry is still holding the CURRENT row's
  //             inflow and must not be touched until it has been read, so the
  //             term waits one iteration.
  // The 3/16 goes to err_[c-1], which was reset one iteration ago and is
  // therefore already the next row's -- no delay needed.
  //
  // AT BOTH ENDS OF A ROW THE OVERHANGING TERMS ARE DROPPED, and that is the
  // right answer rather than an omission. Both scalars are re-initialised per
  // row, so the last column's 7/16 and 1/16 fall off the right edge; the
  // `c > 0` guard eats the first column's 3/16 off the left. Folding them back
  // instead -- adding them to the last or first column of the same row -- would
  // put a whole row's accumulated slack into one edge column, which reads as a
  // seam running down the side of the cover. The reference implementation in
  // test_imagefit.cpp drops them with the identical `x + 1 < dstW` and `x > 0`
  // guards, so the two agree by construction and not by coincidence.
  //
  // int16_t IS ENOUGH, and it is a fixed point rather than an estimate. Let M
  // bound |e| over a row. Only 15/16 of an error leaves a pixel, split 9/16
  // downward and 7/16 rightward, so |err_| <= 9M/16 and |carry7| <= 7M/16, and
  // therefore v = mean + err_ + carry7 lies in [-M, 255 + M]. Quantising clamps
  // to [0, 255], so |e| <= max(42, M) -- 42 being half a level. M = 42 solves
  // that, so |err_| never exceeds 24 whatever the picture: instrumented over
  // the test shapes it peaked at 15. Diffusion damps; it does not accumulate.
  int carry7 = 0, below1 = 0;
  for (int c = 0; c < box_.dstW; ++c) {
    const size_t at = static_cast<size_t>(c);
    const int n = count_[at];
    // Round the mean to NEAREST. Truncating instead biases every cell of every
    // cover dark by up to half a grey level, which on four levels is a visible
    // shift and not a rounding detail.
    //
    // `: 255` IS UNREACHABLE, AND A MUTATION OF IT FAILS NOTHING -- written
    // down rather than left to be rediscovered, exactly as
    // previewLinesThatFit's non-property was. `dc = j * dstW / srcW` is
    // monotonic and steps by at most one whenever dstW <= srcW, so it hits
    // every value in 0..dstW-1 and no column can end a row with an empty
    // accumulator. fitCover's clamp is what makes dstW <= srcW true and
    // begin() refuses the geometry again if it somehow is not. The branch stays
    // because the alternative is a division by zero the day something relaxes
    // that clamp, and paper is the right answer for a cell nothing landed in.
    // test_imagefit.cpp's flat-field case is the observable half: it is a BLACK
    // source, so a column that did fall through here would show as paper.
    const int mean = n > 0 ? static_cast<int>((acc_[at] + n / 2) / n) : 255;
    const int v = mean + err_[at] + carry7;
    const int idx = quantiseIndex(v);
    const int e = v - idx * 85;

    err_[at] = static_cast<int16_t>(below1 + e * 5 / 16);
    if (c > 0) err_[at - 1] = static_cast<int16_t>(err_[at - 1] + e * 3 / 16);
    below1 = e * 1 / 16;
    carry7 = e * 7 / 16;

    // Grey is 0 = black and a coverage level is 0 = paper, so the two run
    // opposite ways and this subtraction is the one place they meet.
    const int level = 3 - idx;
    // PACK AS Framebuffer PACKS. bitMask() is `0x80 >> (physX % 8)`, so bit 7
    // is the leftmost pixel of its byte; and a SET bit is paper, so a level's
    // bits are CLEARED into a row that started 0xFF. Both halves are invisible
    // to a test that shares this packing -- see test_imagefit.cpp's last three
    // cases, which read a row back through a real Framebuffer, and Task 13's
    // grayscale golden, which is what finally pins it.
    const int px = box_.dstX + c;
    const uint8_t bit = static_cast<uint8_t>(0x80u >> (px & 7));
    const size_t byte = static_cast<size_t>(px >> 3);
    if (level & 2) msb_[byte] = static_cast<uint8_t>(msb_[byte] & ~bit);
    if (level & 1) lsb_[byte] = static_cast<uint8_t>(lsb_[byte] & ~bit);

    if (!keepAccumulator) {
      acc_[at] = 0u;
      count_[at] = 0u;
    }
  }
  ++dstRow_;
}

}  // namespace reader
