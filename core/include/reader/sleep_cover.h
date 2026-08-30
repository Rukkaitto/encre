#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace reader {

inline constexpr const char* kSleepCoverPath = "/.reader/sleep.cover";
inline constexpr uint32_t kSleepCoverMagic = 0x56435245;  // "ERCV"
inline constexpr int kSleepCoverVersion = 1;

// THE CACHED COVER'S HEADER, then plane 0 (MSB) then plane 1 (LSB), each
// `planeBytes` long.
//
// THE PLANES ARE LOGICAL RASTER ROWS, NOT THE PHYSICAL STORE. This said "physical
// store layout" first, which was right on the desktop and wrong on the device --
// the worst way to be wrong. The shell binds Rotation::Ccw, under which byteIndex
// maps logical (x, y) to physical (physX = y, physY = width - 1 - x), so one
// logical ROW is one physical COLUMN. A streaming row-major downscale can only
// emit logical rows (imagefit.h says so at length), so a file of physical rows
// cannot be produced at all. Getting a row onto the frame is therefore a strided
// scatter and not a memcpy -- Framebuffer::writePackedRow, which is the one
// function in this feature that knows Rotation exists.
//
// `rotation` is stored anyway, and is not decoration: it is what the raster was
// packed FOR. Under Rotation::None the file's rows and the store's rows coincide,
// so a cache written by the simulator and one written by the device are different
// files of the same size, and only this field tells them apart.
//
// TWO PLANES SERVE THREE PASSES: Plane::Bw inks where coverage >= 2, which is
// exactly "MSB set", so the Bw base pass and the Msb pass read the same plane.
//
// ONE FILE, NOT ONE PER BOOK, scoped exactly like last.json because the sleep
// screen only ever shows the last-read book. That makes cache eviction -- its own
// Phase 5 card -- a non-problem by construction. The cost is stated: alternating
// between two books re-decodes on each sleep.
//
// `complete` IS WRITTEN LAST, by seeking back. FileSystem has no rename (and must
// not grow one for this -- appendToCard is a shell free function for exactly that
// reason), so atomicity is a flag the writer sets only when every plane row is
// down. A half-written file -- an abandoned decode, a power loss -- is never
// mistaken for a good one.
struct SleepCoverHeader {
  uint32_t magic = kSleepCoverMagic;
  int32_t version = kSleepCoverVersion;
  int32_t panelW = 0;
  int32_t panelH = 0;
  int32_t rotation = 0;    // the Rotation the planes were packed for
  int32_t planeBytes = 0;  // Framebuffer::sizeBytes() -- ONE plane
  uint32_t bookBytes = 0;  // the EPUB's size, the identity check
  int32_t complete = 0;    // written last; 0 means do not trust what follows
  // NUL-PADDED, AND ALWAYS TERMINATED -- an invariant sleepCoverUsable rests on,
  // because it compares a std::string against this as a C string. Every route in
  // maintains it: the default above zeroes all 128, setSleepCoverBookPath refuses
  // anything that would not leave room for the terminator, and decode forces the
  // last byte to NUL whatever the card said. A longer path stores EMPTY, and empty
  // matches nothing.
  char bookPath[128] = {};
};

// Fixed-size little-endian encoding, so the file does not depend on the compiler's
// padding. 160 bytes: eight 32-bit fields, then the path.
inline constexpr size_t kSleepCoverHeaderBytes =
    8 * sizeof(uint32_t) + sizeof(SleepCoverHeader::bookPath);

// TWO TRIPWIRES, AND THE FIRST ONE ALONE WAS NOT ENOUGH -- which was found by
// mutation rather than by argument. Writing the constant as an expression over the
// fields LOOKS derived and is only half of it: adding `int32_t extra` to the struct
// changes neither `sizeof(bookPath)` nor the hand-written `8`, so the encoder would
// silently keep writing 160 bytes of a 164-byte struct and the new field would
// never reach the card. The mutation that proved that failed nothing at all.
//
// So the guard that bites is on the STRUCT's own size. It is deliberately not a
// claim that the wire format equals the compiler's layout -- the whole point of
// encoding byte at a time is that it does not. It is a claim that nobody adds,
// removes or resizes a field without being made to look at encodeSleepCoverHeader,
// kSleepCoverVersion and this line. Every member is a 32-bit scalar or a char
// array, so 160 holds on any ABI this firmware is built for; an exotic one would
// fail loudly here rather than write a subtly wrong file.
static_assert(sizeof(SleepCoverHeader) == 160,
              "a field changed: update encodeSleepCoverHeader, decodeSleepCoverHeader and "
              "kSleepCoverHeaderBytes together, and bump kSleepCoverVersion");
static_assert(kSleepCoverHeaderBytes == 160, "the wire format is 160 bytes and files exist");

void encodeSleepCoverHeader(const SleepCoverHeader& h, uint8_t* out);

// False -- with `out` untouched -- for fewer than kSleepCoverHeaderBytes bytes or
// a null buffer. It does NOT judge the contents: a decoded header with the wrong
// magic is a successfully decoded header that sleepCoverUsable will refuse. Those
// are two questions and this project has paid for spelling one of them twice.
//
// `out.bookPath` is always NUL-terminated, whatever the 128 bytes on the card say.
// A file whose path field has no terminator is a corrupt file, not a licence to
// read past the struct.
bool decodeSleepCoverHeader(const uint8_t* in, size_t bytes, SleepCoverHeader& out);

// Store a book's path in the header, or refuse.
//
// TRUNCATING WOULD BE WORSE THAN REFUSING, which is the whole reason this is a
// function rather than a strncpy at the writer's call site: two books whose paths
// share their first 127 bytes would each accept the other's cover, and a cover is
// a picture of a book -- a wrong one is not a subtle defect, it is the wrong book
// on the glass for hours. A path that does not fit stores EMPTY, and
// sleepCoverUsable never matches empty, so the cache is simply not used for that
// book. Returns whether it fit.
bool setSleepCoverBookPath(SleepCoverHeader& h, const std::string& path);

// Whether this header describes a cover that may be painted NOW: complete, the
// right magic and version, the right panel and rotation, and the same book.
//
// ONE PREDICATE, asked in one place. Two spellings of "is the cache good" would be
// two chances to disagree, and this project has shipped a dead button twice from
// exactly that shape.
bool sleepCoverUsable(const SleepCoverHeader& h, const std::string& bookPath,
                      uint32_t bookBytes, int panelW, int panelH, int rotation,
                      int planeBytes);

}  // namespace reader
