#pragma once
#include <cstdint>

#include "reader/text.h"  // Ink, Plane

namespace reader {
class Framebuffer;

// A UI mark, not a character. Generated from the design boards' own inline SVG
// by tools/iconc.py (`make icons`) rather than hand-authored: the previous
// hand-drawn bitmaps drifted from the design twice over, once in shape and once
// in size, and a generated asset cannot.
//
// Coverage is stored at `bpp` bits per pixel, MSB-first, rows packed to whole
// bytes -- the same layout as an .rfnt v2 glyph bitmap, so drawIcon can split it
// across the panel's grey planes exactly as drawText does. bpp is explicit
// rather than assumed to be 2: a mark that genuinely wants hard 1-bit edges (a
// hairline rule, a pixel-aligned box) is still expressible.
struct Icon {
  int w, h;
  int bpp;              // 1 = hard mask (a set bit is full ink), 2 = 0..3 coverage
  const uint8_t* rows;  // ((w * bpp + 7) / 8) * h bytes
};

// Ink coverage of one pixel, 0..3, whatever the icon's depth: a 1bpp set bit
// reports 3. Out-of-range coordinates are the caller's problem, as with Font.
uint8_t coverage(const Icon& icon, int col, int row);

// `plane` selects which bit-plane of the 2-bit level this pass emits, matching
// drawText: Bw paints coverage >= 2, Lsb bit 0, Msb bit 1. A 1bpp icon is
// coverage 0 or 3 and so identical in all three.
void drawIcon(Framebuffer& fb, const Icon& icon, int x, int y, Ink ink = Ink::Black,
              Plane plane = Plane::Bw);

// Sizes are each icon's own -- they are the design's, not a shared grid. See
// tools/iconc.py for the source board and viewBox behind each one.
namespace icons {
extern const Icon kBack;     // 23x23 arrow curving left
extern const Icon kDot;      // 23x23 filled circle: the Confirm button
extern const Icon kUp;       // 23x23 stem with a chevron head, pointing up
extern const Icon kDown;     // 23x23 the same, pointing down
extern const Icon kChevron;  // 23x23 right-pointing disclosure
extern const Icon kBook;     // 25x25 open book: the Read action
extern const Icon kFolder;   // 46x39 folder: a Library directory row
extern const Icon kBattery;  // 38x21 the header band's charge cell
}  // namespace icons

}  // namespace reader
