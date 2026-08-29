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
    // crop rectangle carries the panel's aspect, which is what makes the
    // "never upscale" test below a single comparison -- if it is smaller than
    // the panel in one axis it is smaller in both.
    b.dstW = panelW;
    b.dstH = panelH;
    if (srcSpread > panelSpread) {
      // The source is relatively WIDER, so the height fits and the width is cut.
      // This is 223 of 225 corpus covers on the X4 and 207 on the X3.
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
    // cover -- 160 of 225 books, where this whole setting is a no-op.
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

  // NEVER UPSCALE, and the header says why it is a hard property rather than a
  // preference: a box filter cannot enlarge, and CoverFitter's one-source-row-
  // to-one-destination-row streaming cannot either. Measured, 3 of 225 corpus
  // covers reach this on the X4 and 4 on the X3; the smallest is 400x662.
  if (b.dstW > b.srcW || b.dstH > b.srcH) {
    b.dstW = b.srcW;
    b.dstH = b.srcH;
  }
  // Centred last, so it is stated once for every branch above. A Fill that was
  // not clamped lands at (0, 0) by construction, since dstW == panelW there.
  b.dstX = (panelW - b.dstW) / 2;
  b.dstY = (panelH - b.dstH) / 2;
  return b;
}

bool CoverFitter::begin(int srcW, int srcH, int panelW, int panelH, CoverFit fit) {
  box_ = FitBox{};
  planeBytes_ = 0;
  srcH_ = 0;
  srcRow_ = dstRow_ = 0;
  acc_.clear();
  count_.clear();
  err_.clear();
  msb_.clear();
  lsb_.clear();

  if (srcW <= 0 || srcH <= 0 || panelW <= 0 || panelH <= 0) return false;
  const FitBox b = fitCover(srcW, srcH, panelW, panelH, fit);
  if (b.dstW <= 0 || b.dstH <= 0 || b.srcW <= 0 || b.srcH <= 0) return false;
  // fitCover guarantees this; asserting it here is what lets addRow below say
  // "a source row completes at most one destination row" without a loop.
  if (b.dstW > b.srcW || b.dstH > b.srcH) return false;

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
  const size_t need = sizeof(uint32_t) * static_cast<size_t>(b.dstW) +
                      sizeof(uint16_t) * static_cast<size_t>(b.dstW) +
                      sizeof(int16_t) * static_cast<size_t>(b.dstW) +
                      2u * static_cast<size_t>(planeBytes);
  {
    std::unique_ptr<uint8_t[]> probe(new (std::nothrow) uint8_t[need]);
    if (!probe) return false;
  }

  box_ = b;
  planeBytes_ = planeBytes;
  srcH_ = srcH;
  acc_.assign(static_cast<size_t>(b.dstW), 0u);
  count_.assign(static_cast<size_t>(b.dstW), 0u);
  err_.assign(static_cast<size_t>(b.dstW), 0);
  msb_.assign(static_cast<size_t>(planeBytes), 0xFFu);
  lsb_.assign(static_cast<size_t>(planeBytes), 0xFFu);
  return true;
}

bool CoverFitter::addRow(const uint8_t* src, bool& emitted) {
  emitted = false;
  if (planeBytes_ == 0) return false;   // never begun, or begin() refused
  if (src == nullptr) return false;
  if (srcRow_ >= srcH_) return false;   // more rows than the source declared

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
  // The box filter: every source pixel is added to the one destination cell it
  // lands in. `dstW <= srcW` (fitCover's clamp) makes the map onto 0..dstW-1
  // surjective, so no destination column can end up with an empty accumulator.
  for (int j = 0; j < box_.srcW; ++j) {
    const int dc = static_cast<int>(static_cast<long long>(j) * box_.dstW / box_.srcW);
    acc_[static_cast<size_t>(dc)] += row[j];
    count_[static_cast<size_t>(dc)] += 1u;
  }

  // A destination row is finished when the NEXT source row would land past it,
  // which includes the source running out.
  const int nextDr = (i + 1 >= box_.srcH)
                         ? box_.dstH
                         : static_cast<int>(static_cast<long long>(i + 1) * box_.dstH / box_.srcH);
  if (nextDr == dstRow_) return true;
  emitRow();
  emitted = true;
  return true;
}

void CoverFitter::emitRow() {
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

    acc_[at] = 0u;
    count_[at] = 0u;
  }
  ++dstRow_;
}

}  // namespace reader
