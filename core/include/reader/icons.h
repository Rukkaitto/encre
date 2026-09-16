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

// Sizes are each icon's own -- they are the design's, not a shared grid, and
// they are read off the board by the generator rather than declared anywhere in
// core/. No size is repeated here for that reason: a comment stating 23x23 is
// the same stale transcript the generator used to keep, and it outlived two
// design changes. Ask the Icon. tools/iconc.py names the source board for each.
namespace icons {
extern const Icon kBack;     // arrow curving left: the Back button
extern const Icon kForward;  // long arrow: an action block's proceed mark
extern const Icon kDot;      // filled circle: the Confirm button
extern const Icon kHold;     // hollow ring: this button has a long-press action
extern const Icon kUp;       // stem with a chevron head, pointing up
extern const Icon kDown;     // the same, pointing down
extern const Icon kChevron;  // right-pointing disclosure
extern const Icon kBook;     // open book: the Read action
// The SAME drawing at 112px, and a separate asset because these are pre-rendered
// bitmaps -- there is no scaling a 25px mark up. HomeEmpty's mark.
extern const Icon kBookLarge;
// The same drawing a THIRD time, at 44px: a Library book row's mark, and a third
// asset for kBookLarge's reason. It is not kBook resized, because kBook at 25px
// is still the `READ` hint's mark on three Home boards -- so the two sizes have
// two callers and are two bitmaps. Its stroke is matched to kFolder's, which is
// the mark it sits under in the same list.
extern const Icon kBookRow;
extern const Icon kFolder;   // folder: a Library directory row
extern const Icon kBattery;  // the header band's charge cell
extern const Icon kBatteryCharging;  // ...with a bolt knocked out: charging
// The same cell nearly empty and drawn large, and a separate asset for
// kBookLarge's reason: there is no scaling a header-band mark up. BatteryEmpty's.
extern const Icon kBatteryLarge;
extern const Icon kSdCard;   // an SD card: the subject of the no-card prompt
extern const Icon kCheck;    // a tick: the end-of-book confirmation mark
extern const Icon kWarning;  // a warning triangle: the corrupt-book dialog and
                             // low-battery banner's mark. ONE bitmap: all three
                             // boards' svgs differ only in colour, and colour is
                             // Ink's business -- see iconc.py's `warning` entry.
// --- The V1.1 connect flow ---------------------------------------------
// THREE MARKS, NOT SIX. The picker's signal meter is three axis-aligned
// rectangles differing only in which are filled, and it is drawSignalBars in
// components.h rather than kSignal1..3 here -- see that function for why. What
// is generated is the three that are genuinely icon-shaped.
extern const Icon kLock;    // a closed padlock: a scan row needing a passphrase
extern const Icon kRescan;  // a circular arrow: the picker's last row
extern const Icon kWifi;    // three arcs over a dot: the connecting dialog
// An arrow into a tray: the sync dialog's FETCHING stage, where kWifi is its
// connecting one. The two stages of one screen differ by this mark, because the
// first stage IS the radio and the second is bytes landing on the card.
extern const Icon kDownload;
// The Articles flow's one new mark: a stack of articles over a tray, 98x91, for
// the not-set-up prompt. The sync row draws kRescan and the sync-done tick draws
// kCheck -- both already generated from paths their own boards share with this
// flow's, which iconc's `source` is what disambiguates.
extern const Icon kArticlesLarge;
}  // namespace icons

}  // namespace reader
