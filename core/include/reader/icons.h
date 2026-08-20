#pragma once
#include <cstdint>

#include "reader/text.h"  // Ink

namespace reader {
class Framebuffer;

// A UI mark, not a character: 1bpp rows, MSB-first, a set bit meaning ink.
// Hand-authored at the sizes the design uses so they stay crisp on a 1-bit
// panel — scaled bitmaps would not.
struct Icon {
  int w, h;
  const uint8_t* rows;  // ((w + 7) / 8) * h bytes
};

void drawIcon(Framebuffer& fb, const Icon& icon, int x, int y, Ink ink = Ink::Black);

namespace icons {
extern const Icon kBack;     // arrow curving left
extern const Icon kDot;      // filled circle: the Confirm button
extern const Icon kUp;       // chevron up in a circle-less form
extern const Icon kDown;     // chevron down
extern const Icon kChevron;  // right-pointing disclosure
extern const Icon kBook;     // open book: the Read action
extern const Icon kFolder;   // folder: a Library directory row
extern const Icon kBattery;  // 22x12, not 13x13: the header band's charge cell
}  // namespace icons

}  // namespace reader
