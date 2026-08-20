#include "reader/icons.h"

#include "reader/framebuffer.h"

namespace reader {

void drawIcon(Framebuffer& fb, const Icon& icon, int x, int y, Ink ink) {
  const bool white = (ink == Ink::White);
  const int rowBytes = (icon.w + 7) / 8;
  for (int row = 0; row < icon.h; ++row) {
    const uint8_t* src = icon.rows + row * rowBytes;
    for (int col = 0; col < icon.w; ++col)
      if ((src[col / 8] >> (7 - col % 8)) & 1) fb.setPixel(x + col, y + row, white);
  }
}

namespace icons {
namespace {
// 13x13 marks, one byte per row (13 bits rounded to 2 bytes). Authored as
// binary literals so the shape is readable and editable in place.
//
// The arrows and the book are solid masses rather than strokes: rendered and
// reviewed at size, hollow outlines did not survive 13x13 on a 1-bit panel --
// the book read as the letters "OC" and the arrows as a chevron with a
// detached square. Solid heads with a connected stem, and two page blocks
// split by a 1px spine, do read.
#define R2(a, b) 0b##a, 0b##b

const uint8_t kBackBits[] = {
    R2(00000000, 00000000), R2(00000100, 00000000), R2(00001100, 00000000),
    R2(00011100, 00000000), R2(00111111, 11100000), R2(01111111, 11110000),
    R2(11100000, 00111000), R2(01110000, 00111000), R2(00111000, 00111000),
    R2(00011100, 00111000), R2(00000000, 00111000), R2(00000000, 00000000),
    R2(00000000, 00000000),
};
const uint8_t kDotBits[] = {
    R2(00000000, 00000000), R2(00000000, 00000000), R2(00001110, 00000000),
    R2(00111111, 10000000), R2(01111111, 11000000), R2(01111111, 11000000),
    R2(11111111, 11100000), R2(01111111, 11000000), R2(01111111, 11000000),
    R2(00111111, 10000000), R2(00001110, 00000000), R2(00000000, 00000000),
    R2(00000000, 00000000),
};
const uint8_t kUpBits[] = {
    R2(00000000, 00000000),
    R2(00000010, 00000000),
    R2(00000111, 00000000),
    R2(00001111, 10000000),
    R2(00011111, 11000000),
    R2(00111111, 11100000),
    R2(00000111, 00000000),
    R2(00000111, 00000000),
    R2(00000111, 00000000),
    R2(00000111, 00000000),
    R2(00000111, 00000000),
    R2(00000000, 00000000),
    R2(00000000, 00000000),
};
const uint8_t kDownBits[] = {
    R2(00000000, 00000000),
    R2(00000111, 00000000),
    R2(00000111, 00000000),
    R2(00000111, 00000000),
    R2(00000111, 00000000),
    R2(00000111, 00000000),
    R2(00111111, 11100000),
    R2(00011111, 11000000),
    R2(00001111, 10000000),
    R2(00000111, 00000000),
    R2(00000010, 00000000),
    R2(00000000, 00000000),
    R2(00000000, 00000000),
};
const uint8_t kChevronBits[] = {
    R2(00000000, 00000000), R2(00110000, 00000000), R2(00111000, 00000000),
    R2(00011100, 00000000), R2(00001110, 00000000), R2(00000111, 00000000),
    R2(00000011, 10000000), R2(00000111, 00000000), R2(00001110, 00000000),
    R2(00011100, 00000000), R2(00111000, 00000000), R2(00110000, 00000000),
    R2(00000000, 00000000),
};
const uint8_t kBookBits[] = {
    R2(00000000, 00000000),
    R2(00111101, 11100000),
    R2(01111101, 11110000),
    R2(11111101, 11111000),
    R2(11111101, 11111000),
    R2(11111101, 11111000),
    R2(11111101, 11111000),
    R2(11111101, 11111000),
    R2(11111101, 11111000),
    R2(01111101, 11110000),
    R2(00111101, 11100000),
    R2(00000000, 00000000),
    R2(00000000, 00000000),
};
const uint8_t kFolderBits[] = {
    R2(00000000, 00000000), R2(11111000, 00000000), R2(11111100, 00000000),
    R2(11111111, 11111000), R2(10000000, 00001000), R2(10000000, 00001000),
    R2(10000000, 00001000), R2(10000000, 00001000), R2(10000000, 00001000),
    R2(11111111, 11111000), R2(00000000, 00000000), R2(00000000, 00000000),
    R2(00000000, 00000000),
};
#undef R2
}  // namespace

const Icon kBack{13, 13, kBackBits};
const Icon kDot{13, 13, kDotBits};
const Icon kUp{13, 13, kUpBits};
const Icon kDown{13, 13, kDownBits};
const Icon kChevron{13, 13, kChevronBits};
const Icon kBook{13, 13, kBookBits};
const Icon kFolder{13, 13, kFolderBits};
}  // namespace icons

}  // namespace reader
