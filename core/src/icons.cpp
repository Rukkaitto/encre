#include "reader/dither.h"
#include "reader/icons.h"

#include "icons_data.h"
#include "reader/framebuffer.h"
#include "reader/profile.h"

namespace reader {

uint8_t coverage(const Icon& icon, int col, int row) {
  const int rowBytes = (icon.w * icon.bpp + 7) / 8;
  const uint8_t* r = icon.rows + static_cast<size_t>(row) * rowBytes;
  if (icon.bpp == 1) return ((r[col / 8] >> (7 - col % 8)) & 1) ? 3 : 0;
  // 2bpp, MSB-first: two bits per pixel, four pixels per byte. Font::coverage
  // in core/src/font.cpp reads the identical layout.
  const int shift = 6 - 2 * (col % 4);
  return static_cast<uint8_t>((r[col / 4] >> shift) & 0x3);
}

void drawIcon(Framebuffer& fb, const Icon& icon, int x, int y, Ink ink, Plane plane) {
  PhaseSpan sp(Phase::Icon);
  const bool white = (ink == Ink::White);
  for (int row = 0; row < icon.h; ++row)
    for (int col = 0; col < icon.w; ++col) {
      const uint8_t cov = coverage(icon, col, row);
      bool emit = false;
      switch (plane) {
        case Plane::Bw:
          emit = cov >= 2;
          break;
        case Plane::Lsb:
          emit = (cov & 1) != 0;
          break;
        case Plane::Msb:
          emit = (cov & 2) != 0;
          break;
        case Plane::BwDithered:
          // Same stipple as glyph edges (see drawText), indexed by panel
          // coordinates so a mark and the label beside it share one pattern.
          emit = (cov * 16) / 3 > bayer4(x + col, y + row);
          break;
      }
      if (emit) fb.setPixel(x + col, y + row, white);
    }
}

// The bitmaps live in the generated icons_data.h; this file only names them.
// Nothing here is hand-authored, so nothing here can disagree with the boards:
// to change an icon, edit its entry in tools/iconc.py and run `make icons`.
namespace icons {
const Icon kBack{data::kBackW, data::kBackH, 2, data::kBackBits};
const Icon kForward{data::kForwardW, data::kForwardH, 2, data::kForwardBits};
const Icon kDot{data::kDotW, data::kDotH, 2, data::kDotBits};
const Icon kHold{data::kHoldW, data::kHoldH, 2, data::kHoldBits};
const Icon kUp{data::kUpW, data::kUpH, 2, data::kUpBits};
const Icon kDown{data::kDownW, data::kDownH, 2, data::kDownBits};
const Icon kChevron{data::kChevronW, data::kChevronH, 2, data::kChevronBits};
const Icon kBook{data::kBookW, data::kBookH, 2, data::kBookBits};
const Icon kBookLarge{data::kBookLargeW, data::kBookLargeH, 2, data::kBookLargeBits};
const Icon kBookRow{data::kBookRowW, data::kBookRowH, 2, data::kBookRowBits};
const Icon kFolder{data::kFolderW, data::kFolderH, 2, data::kFolderBits};
const Icon kBattery{data::kBatteryW, data::kBatteryH, 2, data::kBatteryBits};
const Icon kBatteryCharging{data::kBatteryChargingW, data::kBatteryChargingH, 2, data::kBatteryChargingBits};
const Icon kBatteryLarge{data::kBatteryLargeW, data::kBatteryLargeH, 2, data::kBatteryLargeBits};
const Icon kSdCard{data::kSdCardW, data::kSdCardH, 2, data::kSdCardBits};
const Icon kCheck{data::kCheckW, data::kCheckH, 2, data::kCheckBits};
const Icon kWarning{data::kWarningW, data::kWarningH, 2, data::kWarningBits};
}  // namespace icons

}  // namespace reader
