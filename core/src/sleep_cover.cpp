#include "reader/sleep_cover.h"

#include <cstring>

namespace reader {
namespace {

// FIXED-WIDTH LITTLE-ENDIAN, BYTE AT A TIME, and neither half of that is
// ceremony. Byte at a time because a memcpy of the struct would ship the
// compiler's padding and its alignment into a file, and this project builds the
// same source for a RISC-V device and an arm64 desktop -- the simulator will one
// day write one of these and the device will read it. Little-endian because both
// of those parts are, so the loop below compiles to the store it looks like and
// the file is still readable with xxd on either.
void put32(uint8_t*& p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
  p += 4;
}

uint32_t get32(const uint8_t*& p) {
  const uint32_t v = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                     (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
  p += 4;
  return v;
}

// The signed fields go through the unsigned pair above rather than getting their
// own: every one of them is a geometry or a flag and none is ever negative, so
// the round trip is exact and there is one encoding to be wrong about instead of
// two. (Two's complement is mandated by C++20, so the cast is well defined even
// if one ever were.)
void put32s(uint8_t*& p, int32_t v) { put32(p, static_cast<uint32_t>(v)); }
int32_t get32s(const uint8_t*& p) { return static_cast<int32_t>(get32(p)); }

}  // namespace

void encodeSleepCoverHeader(const SleepCoverHeader& h, uint8_t* out) {
  if (out == nullptr) return;
  uint8_t* p = out;
  put32(p, h.magic);
  put32s(p, h.version);
  put32s(p, h.panelW);
  put32s(p, h.panelH);
  put32s(p, h.rotation);
  put32s(p, h.planeBytes);
  put32(p, h.bookBytes);
  put32s(p, h.complete);
  // The path is copied WHOLE, padding included, so two headers naming the same
  // book encode to identical bytes whatever was in the struct's tail. A writer
  // that stopped at the terminator would leave the caller's uninitialised stack
  // in the file -- which is a slow leak of whatever the shell had lying about,
  // and it would make the same header encode differently on two calls.
  std::memcpy(p, h.bookPath, sizeof(h.bookPath));
}

bool decodeSleepCoverHeader(const uint8_t* in, size_t bytes, SleepCoverHeader& out) {
  // REFUSE AND LEAVE `out` ALONE, rather than filling in what fits. A caller who
  // ignores the bool then has the struct's defaults -- complete = 0 -- which
  // sleepCoverUsable refuses, so the two failure paths agree.
  if (in == nullptr || bytes < kSleepCoverHeaderBytes) return false;
  const uint8_t* p = in;
  out.magic = get32(p);
  out.version = get32s(p);
  out.panelW = get32s(p);
  out.panelH = get32s(p);
  out.rotation = get32s(p);
  out.planeBytes = get32s(p);
  out.bookBytes = get32(p);
  out.complete = get32s(p);
  std::memcpy(out.bookPath, p, sizeof(out.bookPath));
  // THESE BYTES CAME OFF A CARD. A corrupt or truncated write can leave all 128
  // of them non-zero, and std::string(h.bookPath) would then read past the end of
  // the struct -- comparing whatever the caller's stack holds next against a book
  // path. Terminating is enough: an over-long field is not a path this firmware
  // could have written (setSleepCoverBookPath refuses one), so the comparison in
  // sleepCoverUsable fails and the cover is simply not used. Refusing the header
  // here instead would be a second spelling of "is this good", which that one
  // predicate owns.
  out.bookPath[sizeof(out.bookPath) - 1] = '\0';
  return true;
}

bool setSleepCoverBookPath(SleepCoverHeader& h, const std::string& path) {
  std::memset(h.bookPath, 0, sizeof(h.bookPath));
  // `>=`, not `>`: 128 bytes of path plus a terminator does not fit in 128, and
  // this boundary is the one a strncpy gets wrong -- strncpy(dst, src, 128) with
  // a 128-byte source copies the lot and terminates nothing.
  if (path.size() >= sizeof(h.bookPath)) return false;
  std::memcpy(h.bookPath, path.data(), path.size());
  return true;
}

bool sleepCoverUsable(const SleepCoverHeader& h, const std::string& bookPath, uint32_t bookBytes,
                      int panelW, int panelH, int rotation, int planeBytes) {
  if (h.magic != kSleepCoverMagic) return false;
  if (h.version != kSleepCoverVersion) return false;
  // WRITTEN LAST BY THE WRITER, SO IT IS THE COMMIT RECORD. Everything above it
  // in the file can be perfectly formed and describe planes that were never
  // finished being streamed.
  if (h.complete == 0) return false;
  if (h.panelW != panelW || h.panelH != panelH) return false;
  if (h.rotation != rotation) return false;
  // The planes are read straight into Framebuffer::data(), so a plane size that
  // disagrees with the geometry is the one mismatch here that could write past
  // the end of a frame rather than merely draw the wrong picture. Both are
  // checked because the pair is redundant BY DESIGN -- panelW and panelH imply
  // planeBytes only once you also know the rotation and the stride rounding, and
  // recomputing that here would be a second copy of Framebuffer's arithmetic.
  if (h.planeBytes != planeBytes) return false;
  if (h.bookBytes != bookBytes) return false;
  // AN EMPTY STORED PATH MATCHES NOTHING, INCLUDING AN EMPTY ONE. Empty is what
  // setSleepCoverBookPath stores when it refuses, so it means "there is no path
  // here" -- and a caller with nothing to offer must not be handed a cover on the
  // strength of two blanks agreeing. Checked before the comparison rather than
  // relying on it, because the comparison would say yes.
  if (h.bookPath[0] == '\0' || bookPath.empty()) return false;
  // Whole strings, not a bounded prefix compare: the field is NUL-padded, so
  // `/books/Le Fleau` and `/books/Le Fleau.epub` must not agree.
  return bookPath == h.bookPath;
}

}  // namespace reader
