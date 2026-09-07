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

// THE LARGEST ENLARGEMENT A COVER MAY BE DRAWN AT, as a percentage of the source
// rectangle.
//
// 250 IS AN OWNER OVERRIDE OF A DERIVED BOUND, NOT A CORRECTION OF IT. The two
// measurements below derived 200, they are unchanged, nothing has falsified
// either, and 250 EXCEEDS WHAT THEY BOUND. Both halves are stated because
// restating the derivation under the larger constant -- letting the argument for
// 200 stand as though it had produced 250 -- would be this project's most
// expensive recurring defect, the one book.cpp committed about Epub::open's
// behaviour and the one a static_assert committed about its own tie to the enum.
// A future reader needs the figures to move this BACK with evidence, so they are
// kept whole rather than adjusted to fit.
//
// WHAT THE TWO MEASUREMENTS BOUND, AND THEY BOUND 200. Nearest-neighbour
// replication at scale k introduces structure of period k pixels -- a source
// pixel's footprint -- and both of these are about the SMALLEST STRUCTURE THIS
// GLASS CARRIES, which is 2 px:
//
//   * THE PIPELINE'S OWN GRAIN, measured off the shipped CoverFitter. A flat
//     field at each of the three level midpoints -- grey 42/43, 127/128, 212/213,
//     the tones four levels carry worst and therefore the patterns with the most
//     contrast in them -- comes out of emitRow() as a run length of exactly ONE,
//     which is a period-TWO alternation. Over all 234 greys that need a pattern
//     at all the mean run is 3.20 px, so 2 px is the floor of that distribution
//     and its highest-contrast end. That picture has been confirmed on the X3
//     (CLAUDE.md, 2026-08-29) to read as a photograph rather than as noise. So
//     2 px is structure this panel is KNOWN to accept.
//   * THE PROJECT'S OWN LEGIBILITY FLOOR, already in the repo. "Below ~10pt is
//     not legible on this glass, measured"; 10 pt at 150 DPI is a 21 px ppem,
//     whose stem is ~2 px, and the whole Mono argument is about "a 2px stem fully
//     inked". 2 px is the smallest structure this project has measured as
//     carrying meaning here.
//
// Two independent measurements landing on one number is what made 200 a
// derivation rather than a taste. At k <= 2 the introduced structure is no
// coarser than the dither grain already on the glass beside it, and it is
// absorbed into the diffusion. AT k = 2.5 IT IS NOT: a source pixel becomes a run
// of 2 or 3 destination pixels, mean 2.5, so it is coarser than the only floor
// this project has evidence for and it becomes the picture's own structure. That
// is the cost of this constant, stated rather than argued away -- past 200 the
// sufficiency of nearest-neighbour is no longer measured, it is assumed.
//
// WHY IT WAS RAISED ANYWAY, WHICH IS A DECISION AND NOT A FINDING. The book that
// produced #64 -- Walden ou la vie dans les bois, 260x346 -- asks x2.29 on the X3
// and x2.31 on the X4, so 200 REFUSED it and the sleep screen showed its reading
// card instead. Both of those are boarded states, and the owner's call is that a
// soft full-bleed cover beats a card: design/SleepCover.dc.html draws a picture,
// and the card is what the screen falls back to when there is NONE. The refusal
// was not a wrong answer, it was the derived answer, and it has been overruled on
// a judgement no measurement in this repo can make.
//
// WHAT IS NOW UNPROVEN IS EXACTLY WHAT THE CAP EXISTED TO PREVENT: a 260 px
// picture replicated across 528 px at x2.29 may read as BLOCKS rather than as a
// photograph. Nothing on the desktop can answer it -- the simulator and the
// goldens run this same arithmetic, so they agree with it by construction, and
// this project has been wrong about this panel from desktop evidence three times.
// THE GLASS SETTLES IT, and it settles it in both directions: if a x2.3 cover
// reads as mush on an X3, 200 is the number the measurements above support and
// this is a one-line change back.
//
// WHAT THE CORPUS SAYS AND WHAT IT CANNOT: over the 225 books in
// ~/.cache/encre-corpus, run through the real decodeCover at both panels, 223
// declare a cover whose dimensions parse and the worst enlargement any of them
// asks for is x1.32 (400x662 on the X3) -- so 200 ALREADY admitted every one of
// them and RAISING IT MOVES NO CORPUS COVER AT ALL (tools/covers.py: 223 Ok and 0
// TooSmall at both caps, and all 892 cover renders byte-identical across the
// change). The corpus therefore has nothing to say for or against this raise,
// which is the point rather than a gap: the case it is for is the one the corpus
// does not contain. #64's book is far smaller than anything in it, the corpus
// under-counts this the way it under-counted #35, and both of the books #35 made
// openable are small-cover cases.
inline constexpr int kMaxCoverUpscalePercent = 250;

// The rectangle the cover occupies inside the panel, and which source rectangle
// maps onto it. Whole leaves bands; Fill leaves none and crops the source.
struct FitBox {
  int dstX = 0, dstY = 0, dstW = 0, dstH = 0;
  int srcX = 0, srcY = 0, srcW = 0, srcH = 0;
  // THE SOURCE CANNOT REACH THE PANEL WITHIN kMaxCoverUpscalePercent, so the box
  // above is the 1:1 centred one -- exactly what this function returned for such
  // a cover before it could enlarge at all.
  //
  // A FLAG RATHER THAN AN EMPTY BOX, and rather than a second function: whether a
  // cover was clamped by the cap is a fact only fitCover can know (recovering it
  // means recomputing the scale, which is a second copy of the arithmetic that
  // decided it), CoverFitter::begin refuses on it so nothing can draw a tiny
  // centred picture, and decodeCover reports it as CoverResult::TooSmall so the
  // log says which of the six answers it took. Leaving the box valid is what
  // keeps fitCover TOTAL: a caller that ignores the flag gets today's geometry
  // rather than a surprise.
  bool tooSmall = false;
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
// IT UPSCALES, UP TO kMaxCoverUpscalePercent, AND SETS `tooSmall` BEYOND IT.
// This paragraph read "IT NEVER UPSCALES, and that is a property CoverFitter
// depends on rather than a taste" for two phases, and every word of it was true
// of the code: a box filter cannot enlarge, and one-source-row-to-one-
// destination-row streaming cannot either. What was written down is what the
// reader saw as a defect (#64) -- design/SleepCover.dc.html says full-bleed and a
// 260x346 cover sat on the X3's 528x792 as a small picture covering 22% of the
// glass, for hours, which is not a state any board draws.
//
// BOTH HALVES OF THE OLD PROPERTY HAD TO GIVE, and each gave differently:
//
//   * THE FILTER. A box filter's support is the destination pixel's footprint,
//     which when enlarging is SMALLER than a source pixel -- so area-averaging an
//     enlargement is nearest-neighbour replication however it is spelled, and
//     pre-replicating the source and box-filtering that back down does not help
//     (at an integer ratio the grids align and it IS replication again). Anything
//     smoother needs a reconstruction filter wider than the destination pixel,
//     which is real interpolation and a new hot loop. So this replicates, and
//     kMaxCoverUpscalePercent above is what bounds how coarse that is allowed to
//     get. At the 200 that constant's two measurements derived, the bound held
//     the introduced structure at the grain the glass already carries; at the 250
//     it now holds, it does not, and whether replication is still sufficient
//     there is the open question recorded with the constant. Real interpolation
//     is the answer if the glass says it is not.
//   * THE INTERFACE. One source row can now complete SEVERAL destination rows,
//     so addRow() below no longer answers "did one come out" -- the caller drains
//     with nextRow(). That is the honest shape for any ratio rather than a
//     contract that leaks the cap by promising "at most two".
//
// A source rectangle that cannot reach the panel within the cap keeps its own
// size, centred, `tooSmall` set -- and CoverFitter::begin then refuses it, so the
// small centred picture is not drawn at all. The census in reader/cover_fit.h has
// the aspects; the incidence of the enlargement is with the constant above.
FitBox fitCover(int srcW, int srcH, int panelW, int panelH, CoverFit fit);

// TURNS SOURCE ROWS INTO TWO 1-BIT PLANE ROWS, IN ORDER, HOLDING NEITHER IMAGE.
//
// Box-filter down (accumulate whole source rows into the destination row they
// land in) then Floyd-Steinberg to four levels, diffusing into a single carried
// error row. That is the whole reason this streams: FS needs the NEXT row's
// error and nothing more, so one row of state serves an image of any height.
//
// AND UP, BY REPLICATION, WHICH IS A SECOND ACCUMULATE PATH AND NOT A SECOND
// COPY OF ONE. A downscale is a forward SCATTER -- walk the source, add each
// pixel to the one destination cell it lands in -- and an upscale is an inverse
// GATHER -- walk the destination, read the one source pixel it sits on. They are
// different operations over the same accumulator, and everything after the
// accumulator (the mean, the diffusion, the packing, the guards) is shared: the
// branch is ~10 lines inside addRow and emitRow is untouched. The forward form is
// kept verbatim rather than generalised because a gather with the same boundaries
// would round its cell edges the other way (floor where the scatter's are
// effectively ceil), which moves a byte of every cover the device has ever drawn
// -- 219 of 223 corpus covers, every golden and the whole comparison sheet go
// through it, and this change is asserted to move none of them.
//
// A REPLICATED ROW IS NOT A DOUBLED ROW, which is worth knowing before predicting
// what the enlargement looks like. The accumulator is held across the several
// destination rows one source row completes, but err_ advances per emitted row --
// so each copy is diffused against a different carried error and comes out a
// DIFFERENT pattern of the same tone. The vertical replication is broken up by
// the dither rather than showing as pairs of identical rows.
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
  // PUSH A ROW, THEN DRAIN WITH nextRow() UNTIL IT IS FALSE:
  //
  //     if (!f.addRow(px)) fail();
  //     while (f.nextRow()) sink(f.msbRow(), f.lsbRow());
  //
  // Returns false only on misuse: before begin(), on a null row, on a row past
  // the source's declared height, or WITH ROWS STILL PENDING from the previous
  // push -- an undrained accumulator would take the next source row on top of
  // the one it is still holding, which is a picture with rows blended into each
  // other rather than an error a caller would notice.
  //
  // THIS WAS `addRow(src, bool& emitted)`, AND THE OUT-PARAM WAS THE INTERFACE
  // THE OLD "never upscales" PROPERTY EXISTED TO PROTECT. A bool can say "zero or
  // one"; an enlargement completes several destination rows from one source row,
  // and a contract promising "at most two" would only be true while the cap
  // happens to be 200%. Zero-or-more, drained, is honest at any ratio.
  bool addRow(const uint8_t* src);

  // True when msbRow()/lsbRow() hold a destination row the caller has not seen.
  // False when this source row has nothing more to give -- which for every
  // downscale is after at most one true, so the loop above reduces to the old
  // `if (emitted)`.
  bool nextRow();

  // Valid after nextRow() answered true. (panelW + 7) / 8 bytes each. Columns
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
  // `keepAccumulator` is the replication: the several destination rows one source
  // row completes all draw from the same accumulated cells, so only the LAST of
  // them may clear them. It is false for every downscale, where the count is
  // always one, which is what keeps that path byte-identical.
  void emitRow(bool keepAccumulator);
  // How many destination rows the source row just accumulated has completed.
  // ONE FUNCTION rather than the expression inlined at the end of each accumulate
  // path: the row boundaries are the same question whichever direction the
  // COLUMNS went, and two copies of them is two chances to put a picture's rows a
  // step out of place. Always true; it returns addRow's own answer so each path
  // can end in `return markReady(i)`.
  bool markReady(int i);

  FitBox box_;
  int planeBytes_ = 0;
  int srcH_ = 0;
  int srcRow_ = 0, dstRow_ = 0;
  // HOW MANY DESTINATION ROWS ARE COMPLETE, which is what nextRow() drains
  // against. Equal to dstRow_ when there is nothing pending, so the two together
  // are the whole of the push/pull state.
  int readyRow_ = 0;
  // WHICH DIRECTION EACH AXIS GOES, decided once in begin() rather than compared
  // per row. They are independent on purpose: fitCover's two branches keep the
  // two scales equal up to one rounding step, so a source a hair under the panel
  // can land at 1:1 on one axis and just over it on the other, and asking each
  // axis its own question costs a bool and cannot get that case wrong.
  bool upCols_ = false, upRows_ = false;
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
