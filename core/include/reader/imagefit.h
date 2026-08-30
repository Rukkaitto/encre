#pragma once
#include <cstdint>
#include <vector>

// CoverFit LIVES IN ITS OWN LEAF, and that header says why at length: the
// Settings screen has to name a fit, and settings.h is a leaf that already
// refuses layout.h over exactly this cost. `<vector>` below is 72,845
// preprocessed lines on its own, so a settings header reaching through this one
// for an enum would undo that.
#include "reader/cover_fit.h"

namespace reader {

// The rectangle the cover occupies inside the panel, and which source rectangle
// maps onto it. Whole leaves bands; Fill leaves none and crops the source.
struct FitBox {
  int dstX = 0, dstY = 0, dstW = 0, dstH = 0;
  int srcX = 0, srcY = 0, srcW = 0, srcH = 0;
};

// Pure arithmetic, so it is testable without an image.
//
// VERTICAL CENTRING FOR Fill IS AT 0.4 RATHER THAN 0.5, because a cover's title
// band sits low and an evenly centred crop eats it from the bottom. It applies
// only when Fill crops the HEIGHT, which is to say only when the source is
// relatively TALLER than the panel -- a minority of the corpus, counted in the
// census in reader/cover_fit.h. Kept because it is one subtraction and because
// those are exactly the covers a symmetric crop hurts.
//
// IT NEVER UPSCALES, and that is a property CoverFitter depends on rather than
// a taste. A box filter cannot enlarge -- it would be nearest-neighbour
// replication -- and a streaming row-major fitter maps one source row to one
// destination row, so a destination taller than its source would need a single
// addRow() to complete several rows, which the interface below cannot express.
// So a source rectangle smaller than the panel keeps its own size, centred, and
// Fill degrades to Whole-at-1:1. How rare that is, and how small the smallest
// corpus cover is, are in the census in reader/cover_fit.h.
FitBox fitCover(int srcW, int srcH, int panelW, int panelH, CoverFit fit);

// TURNS SOURCE ROWS INTO TWO 1-BIT PLANE ROWS, IN ORDER, HOLDING NEITHER IMAGE.
//
// Box-filter down (accumulate whole source rows into the destination row they
// land in) then Floyd-Steinberg to four levels, diffusing into a single carried
// error row. That is the whole reason this streams: FS needs the NEXT row's
// error and nothing more, so one row of state serves an image of any height.
//
// TWO PLANES, NOT A 2 BPP IMAGE, and the reason is what makes four levels
// affordable at all: Plane::Bw inks where coverage >= 2, which is exactly "MSB
// set", so the Bw base pass and the Msb pass read the SAME plane. Two planes
// serve three passes, and each pass is one file read into Framebuffer::data().
//
// LEVELS ARE COVERAGE 0..3, mapped to (msb, lsb) as the framebuffer's planes
// are: level 0 is paper, level 3 is full ink.
//
// THE BIT ORDER IS Framebuffer's, EXACTLY, and both halves of that matter:
//
//   * MSB-first. Framebuffer::bitMask is `0x80 >> (physX % 8)`, so bit 7 of a
//     byte is the LEFTMOST of its eight pixels. Packing the other way is a
//     cover mirrored in 8-pixel groups -- instantly visible on glass and
//     invisible to a test that compares this packing against a reference that
//     shares it.
//   * A SET BIT IS PAPER. framebuffer.h states it ("true/1 = white, 0 =
//     black"), text.cpp's blit reaches it by clearing a bit for black ink, and
//     png.cpp's composeGray reads it back as `getPixel ? 0 : 1` per plane. So a
//     level's bits are stored COMPLEMENTED: level 0 leaves both planes' bits
//     set, level 3 clears both.
//
// ROTATION IS NOT THIS CLASS'S PROBLEM, AND IT CANNOT BE. Four byte-wise
// routines in core/ have had to learn Rotation exists (fillRect, veilRect,
// ditherRect, the glyph blit), each because it walks a LOGICAL rectangle into a
// PHYSICAL store and under Ccw a logical row is a physical column. This one
// walks no logical rectangle: it produces a raster, `panelW` pixels a row and
// `panelH` rows, and the caller decides what frame that raster is the store of.
// Under Rotation::None it IS the physical store, byte for byte, which is what
// the goldens and the simulator want.
//
// Under Rotation::Ccw it is not, and no rearrangement inside here could make it
// so: a physical row of a Ccw store is a logical COLUMN, and a streaming
// row-major downscale maps a source ROW to a destination ROW. Producing rotated
// physical rows needs the whole image. So the transpose belongs to whoever
// packs the cache file, and the cache header carries a `rotation` field for
// exactly that reason.
class CoverFitter {
 public:
  // `panelW` is the row width of the raster to produce, in pixels; plane rows
  // come out (panelW + 7) / 8 bytes. Under Rotation::None that is the panel's
  // physical row width -- see the rotation note above.
  //
  // False if the geometry is non-positive or a buffer could not be allocated --
  // never an abort. Safe to call again; it resets everything.
  bool begin(int srcW, int srcH, int panelW, int panelH, CoverFit fit);

  const FitBox& box() const { return box_; }

  // Feed source rows IN ORDER, every row of the source, exactly once. Rows
  // outside box().srcY..srcY+srcH are consumed and discarded, which is what
  // lets the caller stream the whole JPEG without knowing about the crop.
  //
  // When a destination row completes, `msb` and `lsb` are filled and true is
  // returned in `emitted`. A source row can complete at most one destination
  // row, because fitCover never lets the destination be taller than the source.
  //
  // Returns false only on misuse: before begin(), on a null row, or on a row
  // past the source's declared height. `emitted` is always written.
  bool addRow(const uint8_t* src, bool& emitted);

  // Valid after addRow set `emitted`. (panelW + 7) / 8 bytes each. Columns
  // outside the fit box -- the letterbox bands -- come out PAPER; tinting them
  // is the caller's, because it is the caller that has a design board.
  const uint8_t* msbRow() const { return msb_.empty() ? nullptr : msb_.data(); }
  const uint8_t* lsbRow() const { return lsb_.empty() ? nullptr : lsb_.data(); }
  // Which destination row was just emitted, RELATIVE to box().dstY: 0 is the
  // first row of the cover, not the first row of the panel. -1 before any.
  //
  // NOT `emittedRow()`, which is what this was called: beside `rowsEmitted()`
  // the two differed only in word order, one is an INDEX and the other a COUNT,
  // and they are always exactly one apart -- so transposing them at a call site
  // is an off-by-one the compiler cannot see. The names carry the distinction
  // now.
  int lastEmittedRow() const { return dstRow_ - 1; }

  // Destination rows emitted so far. Equal to box().dstH when the source is
  // spent.
  int rowsEmitted() const { return dstRow_; }

 private:
  void emitRow();

  FitBox box_;
  int planeBytes_ = 0;
  int srcH_ = 0;
  int srcRow_ = 0, dstRow_ = 0;
  // PER DESTINATION COLUMN: the summed grey, and how many source pixels went
  // into it. The widths are bounded rather than assumed, which err_'s note
  // below already was and these two were not.
  //
  // `count_` HOLDS ROUGHLY (srcW / dstW) * (srcH / dstH), and it was uint16_t.
  // A JPEG cannot overflow that -- its dimensions are 16-bit, so the worst is
  // about 6.6 K -- but PNG's IHDR width is 31 bits and pngd.cpp caps it
  // DELIBERATELY NOWHERE ("there is no arbitrary cap: what refuses an image too
  // wide to hold is the allocation failing"). So a 2,000,000 x 100 PNG fitted
  // Whole gives a 480 x 1 box and 416,600 samples a cell: a silent wrap, and
  // then a wrong mean or a white stripe. uint32_t holds 4.29e9 of them.
  //
  // `acc_` is then bounded by count_ * 255, and begin() REFUSES a geometry
  // whose worst cell could exceed that -- see the guard there, which is what
  // keeps this a stated bound instead of a hope.
  std::vector<uint32_t> acc_;
  std::vector<uint32_t> count_;
  std::vector<int16_t> err_;     // the carried Floyd-Steinberg error row
  std::vector<uint8_t> msb_, lsb_;
};

}  // namespace reader
