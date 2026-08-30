#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "doctest.h"
#include "reader/framebuffer.h"

using reader::Framebuffer;

TEST_CASE("framebuffer starts white and stores pixels MSB-first") {
  Framebuffer fb(16, 4);            // 16 px wide -> 2 bytes per row
  CHECK(fb.width() == 16);
  CHECK(fb.height() == 4);
  CHECK(fb.physRowBytes() == 2);
  CHECK(fb.data()[0] == 0xFF);      // all white

  fb.setPixel(0, 0, false);         // black at leftmost pixel
  CHECK(fb.data()[0] == 0x7F);      // bit 7 cleared -> MSB is leftmost
  CHECK_FALSE(fb.getPixel(0, 0));
  CHECK(fb.getPixel(1, 0));

  fb.setPixel(0, 0, true);
  CHECK(fb.data()[0] == 0xFF);
}

TEST_CASE("fillRect clips to bounds") {
  Framebuffer fb(16, 4);
  fb.fillRect(12, 2, 100, 100, false);  // overflows right and bottom
  CHECK_FALSE(fb.getPixel(12, 2));
  CHECK_FALSE(fb.getPixel(15, 3));
  CHECK(fb.getPixel(11, 2));
  CHECK(fb.getPixel(12, 1));
}

TEST_CASE("clear repaints everything") {
  Framebuffer fb(8, 1);
  fb.fillRect(0, 0, 8, 1, false);
  fb.clear(true);
  CHECK(fb.data()[0] == 0xFF);
}

TEST_CASE("a width that is not a multiple of 8 still has backing storage") {
  // The docs say width % 8 == 0, but nothing enforced it: storage was sized
  // width/8 (= 1 byte/row for width 12) while width_ kept the full 12, so
  // setPixel's bounds check passed for columns 8..11 with nothing behind them
  // (ASAN: heap-buffer-overflow). Round the stride up instead.
  Framebuffer fb(12, 4);
  CHECK(fb.width() == 12);
  CHECK(fb.physRowBytes() == 2);  // ceil(12 / 8)
  CHECK(fb.sizeBytes() == fb.physRowBytes() * fb.height());

  // Every in-bounds coordinate must be writable without leaving the buffer.
  for (int y = 0; y < fb.height(); ++y)
    for (int x = 0; x < fb.width(); ++x) fb.setPixel(x, y, false);
  for (int y = 0; y < fb.height(); ++y)
    for (int x = 0; x < fb.width(); ++x) CHECK_FALSE(fb.getPixel(x, y));

  // The last row's last pixel is the one that used to run off the end.
  fb.clear(true);
  fb.setPixel(11, 3, false);
  CHECK_FALSE(fb.getPixel(11, 3));
  CHECK(fb.getPixel(10, 3));
}

TEST_CASE("non-positive dimensions give an inert empty buffer") {
  for (auto [w, h] : {std::pair{0, 4}, std::pair{4, 0}, std::pair{-8, 4}, std::pair{8, -4}}) {
    Framebuffer fb(w, h);
    CHECK(fb.width() == 0);
    CHECK(fb.height() == 0);
    CHECK(fb.physRowBytes() == 0);
    CHECK(fb.sizeBytes() == 0);
    // Every operation must be a safe no-op rather than touching storage.
    fb.clear(false);
    fb.setPixel(0, 0, false);
    fb.fillRect(-4, -4, 100, 100, false);
    CHECK(fb.getPixel(0, 0));
  }
}

// --- Rotation ---------------------------------------------------------------
//
// The byte-level proof that Rotation::Ccw matches rotate90CCW exactly lives in
// test_rotate.cpp, where the reference implementation is. These pin the class's
// own contract: which dimensions are logical, which are physical, and that
// Rotation::None is unchanged.

TEST_CASE("Rotation::None is what the default has always been") {
  // The simulator, every golden and tools/compare-design.py all work in logical
  // portrait space, so the unrotated buffer must stay byte-identical to what it
  // was before rotation existed -- not merely equivalent.
  Framebuffer implicitDefault(12, 20);
  Framebuffer explicitNone(12, 20, reader::Rotation::None);
  CHECK(implicitDefault.rotation() == reader::Rotation::None);
  CHECK(explicitNone.sizeBytes() == implicitDefault.sizeBytes());
  CHECK(explicitNone.physRowBytes() == implicitDefault.physRowBytes());
  // Physical and logical coincide, so physWidth/physHeight are not a second
  // thing to keep in step here.
  CHECK(implicitDefault.physWidth() == 12);
  CHECK(implicitDefault.physHeight() == 20);
  CHECK(implicitDefault.physRowBytes() == 2);  // ceil(12 / 8)

  for (int y = 0; y < 20; ++y)
    for (int x = 0; x < 12; ++x) {
      const bool white = ((x * 7 + y * 3) % 5) != 0;
      implicitDefault.setPixel(x, y, white);
      explicitNone.setPixel(x, y, white);
    }
  CHECK(std::memcmp(implicitDefault.data(), explicitNone.data(),
                    static_cast<size_t>(implicitDefault.sizeBytes())) == 0);
  // The MSB-first byte layout is the driver's contract; assert it directly
  // rather than through a comparison that two identical bugs would satisfy.
  implicitDefault.clear(true);
  implicitDefault.setPixel(0, 0, false);
  CHECK(implicitDefault.data()[0] == 0x7F);
}

TEST_CASE("a rotated framebuffer's stride comes from the physical width") {
  // The bug this exists to catch: deriving the stride from the LOGICAL width
  // sizes the store correctly by total bytes and indexes it wrongly by rows,
  // which draws a diagonally sheared screen -- plausible enough to chase for
  // hours. 528 logical wide, 792 physical wide: 99 bytes per row, not 66.
  Framebuffer rot(528, 792, reader::Rotation::Ccw);
  CHECK(rot.physRowBytes() == 99);
  CHECK(rot.physRowBytes() != (528 + 7) / 8);
  CHECK(rot.sizeBytes() == 99 * 528);

  // A logical size that is not a multiple of 8 in either axis: the stride is
  // rounded up from the physical width (the logical HEIGHT), and the slack byte
  // is on the physical row, not the logical one.
  Framebuffer ragged(13, 21, reader::Rotation::Ccw);
  CHECK(ragged.width() == 13);
  CHECK(ragged.height() == 21);
  CHECK(ragged.physWidth() == 21);
  CHECK(ragged.physHeight() == 13);
  CHECK(ragged.physRowBytes() == 3);  // ceil(21 / 8), not ceil(13 / 8) == 2
  CHECK(ragged.sizeBytes() == 3 * 13);
}

TEST_CASE("every in-bounds logical coordinate of a rotated buffer has backing storage") {
  // Same failure mode the non-multiple-of-8 case above guards for the unrotated
  // buffer, but the transform makes it a different piece of arithmetic. Under
  // ASAN this is what catches an off-by-one in either physical axis.
  for (auto [w, h] : {std::pair{13, 21}, std::pair{21, 13}, std::pair{1, 1}, std::pair{7, 100}}) {
    Framebuffer fb(w, h, reader::Rotation::Ccw);
    fb.clear(true);
    for (int y = 0; y < fb.height(); ++y)
      for (int x = 0; x < fb.width(); ++x) fb.setPixel(x, y, false);
    for (int y = 0; y < fb.height(); ++y)
      for (int x = 0; x < fb.width(); ++x) REQUIRE_FALSE(fb.getPixel(x, y));
    // And a single pixel is a single pixel: writing one must not smear across
    // the transform onto a neighbour.
    fb.clear(true);
    fb.setPixel(w - 1, h - 1, false);
    int ink = 0;
    for (int y = 0; y < fb.height(); ++y)
      for (int x = 0; x < fb.width(); ++x)
        if (!fb.getPixel(x, y)) ++ink;
    CHECK(ink == 1);
  }
}

TEST_CASE("fillRect and clear work in logical coordinates through the rotation") {
  // fillRect routes through setPixel, so it follows the transform for free --
  // this pins that nothing bypasses it, and that clear() still covers the whole
  // physical store rather than a logical-sized prefix of it.
  Framebuffer fb(16, 32, reader::Rotation::Ccw);
  fb.clear(false);
  for (int i = 0; i < fb.sizeBytes(); ++i) REQUIRE(fb.data()[i] == 0x00);
  fb.clear(true);
  for (int i = 0; i < fb.sizeBytes(); ++i) REQUIRE(fb.data()[i] == 0xFF);

  fb.fillRect(2, 3, 5, 7, false);
  for (int y = 0; y < fb.height(); ++y)
    for (int x = 0; x < fb.width(); ++x) {
      const bool inRect = x >= 2 && x < 7 && y >= 3 && y < 10;
      REQUIRE(fb.getPixel(x, y) == !inRect);
    }
  // Clipping is on the LOGICAL bounds, same as unrotated.
  fb.clear(true);
  fb.fillRect(14, 30, 100, 100, false);
  CHECK_FALSE(fb.getPixel(15, 31));
  CHECK(fb.getPixel(13, 30));
}

TEST_CASE("a rotated non-positive dimension is still an inert empty buffer") {
  for (auto [w, h] : {std::pair{0, 4}, std::pair{4, 0}, std::pair{-8, 4}}) {
    Framebuffer fb(w, h, reader::Rotation::Ccw);
    CHECK(fb.width() == 0);
    CHECK(fb.height() == 0);
    CHECK(fb.physWidth() == 0);
    CHECK(fb.physHeight() == 0);
    CHECK(fb.physRowBytes() == 0);
    CHECK(fb.sizeBytes() == 0);
    fb.clear(false);
    fb.setPixel(0, 0, false);
    fb.fillRect(-4, -4, 100, 100, false);
    CHECK(fb.getPixel(0, 0));
  }
}

// --- Viewing storage somebody else owns -------------------------------------
//
// The shell draws straight into the panel driver's own framebuffer, so this
// constructor is on the device's paint path and nothing else is. It is also the
// one place in this class where a wrong answer writes memory OUTSIDE the buffer
// rather than drawing the wrong picture: every bounds check is against
// width_/height_, which come from the caller, not from the allocation.

TEST_CASE("a viewing framebuffer draws byte-identically to an owning one") {
  // THE EQUIVALENCE THAT MATTERS. The goldens and the simulator all render into
  // owning frames; the device renders into a viewed one. If the two ever
  // disagree by a byte, every desktop test passes and the panel is wrong -- the
  // same shape of defect as a rotation applied to the coordinates but not to the
  // store.
  for (auto rot : {reader::Rotation::None, reader::Rotation::Ccw}) {
    for (auto [w, h] : {std::pair{16, 4}, std::pair{528, 792}, std::pair{13, 21}}) {
      Framebuffer owned(w, h, rot);
      std::vector<uint8_t> storage(static_cast<size_t>(owned.sizeBytes()), 0x00);
      Framebuffer viewed(storage.data(), storage.size(), w, h, rot);

      // The layout each one describes must match before a single pixel is set:
      // this is the driver's memcpy contract, stated in these three numbers.
      REQUIRE(viewed.sizeBytes() == owned.sizeBytes());
      REQUIRE(viewed.physRowBytes() == owned.physRowBytes());
      REQUIRE(viewed.physWidth() == owned.physWidth());
      REQUIRE(viewed.physHeight() == owned.physHeight());
      REQUIRE(viewed.width() == w);
      REQUIRE(viewed.height() == h);
      REQUIRE(viewed.data() == storage.data());  // no copy: it IS the caller's memory
      REQUIRE_FALSE(viewed.ownsStorage());
      REQUIRE(owned.ownsStorage());

      // A view does not clear on construction (the bytes are someone else's),
      // so the comparison starts from an explicit clear on both.
      owned.clear(true);
      viewed.clear(true);
      REQUIRE(std::memcmp(owned.data(), viewed.data(),
                          static_cast<size_t>(owned.sizeBytes())) == 0);

      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          const bool white = ((x * 7 + y * 3) % 5) != 0;
          owned.setPixel(x, y, white);
          viewed.setPixel(x, y, white);
        }
      owned.fillRect(w - 3, h - 5, 100, 100, false);   // clipped on both edges
      viewed.fillRect(w - 3, h - 5, 100, 100, false);
      CHECK(std::memcmp(owned.data(), viewed.data(),
                        static_cast<size_t>(owned.sizeBytes())) == 0);
      // ...and read back through the logical transform the same way. Only on the
      // small geometries: the byte comparison above already covers the panel
      // sizes, and 418k assertions per case make the fast loop slow for nothing.
      if (w * h <= 4096)
        for (int y = 0; y < h; ++y)
          for (int x = 0; x < w; ++x) REQUIRE(owned.getPixel(x, y) == viewed.getPixel(x, y));
    }
  }
}

TEST_CASE("a view over a too-small buffer is REFUSED, not clamped") {
  // The decision, pinned. A clamped view would report the geometry it was asked
  // for and write past the end of the memory it was given, which is the one
  // failure in this class that corrupts something else instead of looking wrong
  // -- and on the device that something else is the heap next to the panel
  // driver's framebuffer. So a view that does not fit is the same inert empty
  // buffer a non-positive dimension gives, which the shell can see (sizeBytes()
  // == 0 for a geometry that should not be empty) and refuse to boot on.
  //
  // 16x4 unrotated needs 2 bytes per row x 4 rows = 8.
  std::vector<uint8_t> storage(8, 0xFF);
  {
    Framebuffer exact(storage.data(), 8, 16, 4);
    REQUIRE(exact.sizeBytes() == 8);       // exactly enough is enough
    REQUIRE(exact.data() == storage.data());
  }
  {
    Framebuffer spare(storage.data(), 64, 16, 4);
    REQUIRE(spare.sizeBytes() == 8);       // more than enough is fine, and unused
  }
  // One byte short, and every way of being short. Each buffer is ALLOCATED at
  // the short length rather than merely described as short, so ASAN has
  // something real to catch: a clamping implementation writes eight bytes into
  // this block and reports a heap-buffer-overflow. (Verified by temporarily
  // clamping: with the refusal removed, ASAN reports the overflow and the
  // untouched-bytes check below fails.) `new uint8_t[0]` is a valid, non-null,
  // zero-length allocation, which is what keeps the bytes == 0 case a genuine
  // pointer rather than passing the refusal for the wrong reason.
  for (size_t bytes : {size_t{0}, size_t{1}, size_t{7}}) {
    std::unique_ptr<uint8_t[]> sh(new uint8_t[bytes]);
    for (size_t i = 0; i < bytes; ++i) sh[i] = 0xFF;
    Framebuffer fb(sh.get(), bytes, 16, 4);
    CAPTURE(bytes);
    CHECK(fb.width() == 0);
    CHECK(fb.height() == 0);
    CHECK(fb.physRowBytes() == 0);
    CHECK(fb.sizeBytes() == 0);
    CHECK(fb.data() == nullptr);
    CHECK_FALSE(fb.ownsStorage());  // it is still a view; it is just an empty one
    // Inert: every operation a no-op, and NOTHING written through the pointer it
    // was handed.
    fb.clear(false);
    fb.setPixel(0, 0, false);
    fb.fillRect(-4, -4, 1000, 1000, false);
    CHECK(fb.getPixel(0, 0));
    for (size_t i = 0; i < bytes; ++i) CHECK(sh[i] == 0xFF);
  }
  for (uint8_t b : storage) CHECK(b == 0xFF);  // and the exact-fit block above is untouched
  // A null pointer is refused the same way, however generous the length claim.
  Framebuffer nul(nullptr, 1u << 20, 16, 4);
  CHECK(nul.sizeBytes() == 0);
  CHECK(nul.data() == nullptr);
  nul.clear(false);
  nul.setPixel(0, 0, false);
  CHECK(nul.getPixel(0, 0));
}

TEST_CASE("a rotated view is sized by the PHYSICAL geometry, not the logical one") {
  // The X3's real numbers, and the trap: 528x792 logical portrait needs 99 bytes
  // per row x 528 rows. Sizing the check from the logical width (66 bytes per
  // row) would accept a buffer two thirds the size it needs and then write off
  // the end of it -- and the total, 66 * 792, is the SAME 52272, so a check
  // written against the byte count alone cannot tell the two apart. This is the
  // rotated form of the stride bug test_rotate.cpp pins for the owning frame.
  const size_t need = 99u * 528u;
  REQUIRE(need == 66u * 792u);  // the coincidence that makes this worth a test
  std::vector<uint8_t> storage(need, 0xFF);

  Framebuffer fb(storage.data(), need, 528, 792, reader::Rotation::Ccw);
  REQUIRE(fb.physRowBytes() == 99);
  REQUIRE(fb.sizeBytes() == static_cast<int>(need));
  // Every in-bounds logical coordinate must land inside the storage. ASAN is the
  // real assertion here; the byte check is what catches a mapping that stays in
  // bounds but overlaps itself -- every byte of the store must have been
  // reached, so not one of them may still be white.
  fb.clear(true);
  for (int y = 0; y < 792; ++y)
    for (int x = 0; x < 528; ++x) fb.setPixel(x, y, false);
  size_t untouched = 0;
  for (int i = 0; i < fb.sizeBytes(); ++i)
    if (fb.data()[i] != 0x00) ++untouched;
  CHECK(untouched == 0);

  // ...and one byte short of the rotated requirement is refused, even though it
  // is far more than an unrotated 528-wide frame would need.
  Framebuffer short_(storage.data(), need - 1, 528, 792, reader::Rotation::Ccw);
  CHECK(short_.sizeBytes() == 0);
}

TEST_CASE("a view is not cleared on construction and never frees its storage") {
  // Two facts the shell depends on. The driver memsets its framebuffer to white
  // in begin() and the paint path clears before every full render, so a view
  // that cleared on construction would be doing someone else's work with
  // someone else's memory -- and on the grayscale path, between two passes, it
  // would be destroying a plane.
  std::vector<uint8_t> storage(8, 0xA5);
  {
    Framebuffer fb(storage.data(), storage.size(), 16, 4);
    REQUIRE(fb.data()[0] == 0xA5);
    fb.setPixel(0, 0, false);
    CHECK(storage[0] == 0x25);  // wrote through to the caller's memory
  }
  // The Framebuffer is gone; the storage is not. (ASAN would report a
  // double-free or a free of non-heap memory here if the view had taken
  // ownership.)
  CHECK(storage[0] == 0x25);
  CHECK(storage[7] == 0xA5);
}

// --- fillRect: the byte-wise path against the per-pixel one it replaced -------
//
// fillRect used to be a per-pixel loop over setPixel -- a bounds check, a
// byteIndex division, a bitMask modulo and a read-modify-write per pixel -- and
// it now writes the rectangle eight columns at a time straight into the physical
// store. That is PURE OPTIMISATION: every golden in the suite, and every screen
// test, pins the output it must keep producing.
//
// The reference below is that per-pixel loop, kept here for the same reason
// test_dither.cpp keeps veilRectReference and test_rotate.cpp keeps
// rotate90CCW_reference: it IS the specification, because it is the code whose
// output the goldens were blessed against. Any disagreement means the fast path
// is wrong, never the reference.
//
// The comparison is over data() rather than over pixels, because the fast path
// writes bytes: a mapping that is right per pixel but lays the bytes out
// differently is still a wrong frame from the panel driver's point of view. And
// it runs under Rotation::Ccw as well as Rotation::None, because CCW is how the
// device paints -- a byte-wise path that assumed a logical row is a physical row
// would pass every desktop test and every golden and smear on glass.

namespace {

void fillRectReference(Framebuffer& fb, int x, int y, int w, int h, bool white) {
  for (int yy = y; yy < y + h; ++yy)
    for (int xx = x; xx < x + w; ++xx) fb.setPixel(xx, yy, white);
}

// A deterministic ground with no byte-level symmetry, so a fast path that is off
// by a bit, a byte or a row cannot coincidentally match. A plain white or plain
// black ground would hide a mask that is too WIDE whenever the fill colour
// happened to match the ground.
void fillPseudoRandom(Framebuffer& fb, uint32_t seed) {
  uint32_t s = seed | 1u;
  for (int y = 0; y < fb.height(); ++y)
    for (int x = 0; x < fb.width(); ++x) {
      s ^= s << 13; s ^= s >> 17; s ^= s << 5;  // xorshift32
      fb.setPixel(x, y, (s & 0x10u) != 0);
    }
}

struct FillCase {
  int fw, fh, x, y, w, h;
  const char* what;
};

void checkFillMatchesReference(const FillCase& c, reader::Rotation rot, bool white) {
  Framebuffer fast(c.fw, c.fh, rot), ref(c.fw, c.fh, rot);
  const uint32_t seed = static_cast<uint32_t>(c.fw * 7919 + c.fh * 104729 + c.x * 31 + c.w);
  fillPseudoRandom(fast, seed);
  fillPseudoRandom(ref, seed);
  fast.fillRect(c.x, c.y, c.w, c.h, white);
  fillRectReference(ref, c.x, c.y, c.w, c.h, white);

  REQUIRE(fast.sizeBytes() == ref.sizeBytes());
  int diffs = 0, first = -1;
  for (int i = 0; i < ref.sizeBytes(); ++i)
    if (fast.data()[i] != ref.data()[i]) {
      if (diffs == 0) first = i;
      ++diffs;
    }
  const char* rn = rot == reader::Rotation::Ccw ? "Ccw" : "None";
  CHECK_MESSAGE(diffs == 0, c.what << " rot=" << rn << " white=" << white << ": " << diffs
                                   << " of " << ref.sizeBytes()
                                   << " bytes differ, first at offset " << first);
}

}  // namespace

TEST_CASE("the byte-wise fillRect is byte-identical to the per-pixel one") {
  const FillCase cases[] = {
      // The two panels, full frame: what a clear-shaped fill asks for.
      {528, 792, 0, 0, 528, 792, "X3 full frame"},
      {480, 800, 0, 0, 480, 800, "X4 full frame"},
      // The shapes an actual paint asks for: the actions panel, a full-bleed
      // focused row, a header band, and a 1px rule.
      {528, 792, 94, 212, 340, 368, "the actions panel"},
      {528, 792, 0, 300, 528, 72, "a full-bleed row"},
      {528, 792, 0, 0, 528, 96, "the header band"},
      {528, 792, 24, 100, 480, 1, "a hairline rule"},
      {528, 792, 24, 100, 1, 480, "a hairline rule, vertical"},
      // Origins and widths that are not multiples of 8, which is where a
      // byte-wise path's edge masks are the whole of the correctness. Both panel
      // widths are multiples of 8, so nothing on the device exercises this --
      // but overlay geometry is derived by subtraction from a centred panel.
      {528, 792, 1, 0, 526, 792, "inset by one pixel"},
      {528, 792, 7, 3, 513, 785, "ragged origin and width"},
      {528, 792, 3, 0, 5, 792, "a five-pixel column inside one byte"},
      {528, 792, 6, 0, 4, 10, "a run straddling one byte boundary"},
      {528, 792, 8, 0, 8, 10, "a run that is exactly one byte"},
      {528, 792, 0, 0, 8, 10, "a run that is exactly the first byte"},
      {528, 792, 520, 0, 8, 10, "a run that is exactly the last byte"},
      {64, 64, 0, 0, 64, 64, "aligned 64x64"},
      {64, 64, 5, 5, 54, 54, "inset 64x64"},
      // Every start and end phase within a byte, so no edge mask is missed.
      {64, 64, 1, 0, 1, 64, "single column at phase 1"},
      {64, 64, 2, 0, 3, 64, "phase 2, three wide"},
      {64, 64, 5, 0, 2, 64, "phase 5, two wide"},
      {64, 64, 7, 0, 9, 64, "phase 7, crossing two boundaries"},
      // Negative origins: overlay geometry is derived by subtraction from a
      // centred panel, so a panel wider than the narrow geometry produces one.
      {36, 36, -4, -4, 8, 8, "negative origin"},
      {36, 36, -7, -5, 20, 20, "negative origin, odd offsets"},
      {36, 36, -10, -10, 100, 100, "negative origin, overhanging both ends"},
      // Off the far edge, so the clip has to shorten the run rather than write
      // past the row.
      {36, 36, 30, 30, 20, 20, "overhanging the far edge"},
      // Degenerate: nothing drawn, nothing walked off.
      {36, 36, 4, 4, 0, 8, "zero width"},
      {36, 36, 4, 4, 8, 0, "zero height"},
      {36, 36, 4, 4, -5, -5, "negative extent"},
      {36, 36, 100, 100, 8, 8, "wholly off-screen"},
      {36, 36, -50, -50, 8, 8, "wholly off-screen, negative"},
      // Frames whose own dimensions are not multiples of 8, in both axes, which
      // under rotation is where the stride comes from the other one.
      {13, 21, 0, 0, 13, 21, "ragged frame 13x21"},
      {21, 13, 0, 0, 21, 13, "ragged frame 21x13"},
      {13, 21, 2, 3, 9, 15, "ragged frame, ragged rect"},
      {1, 1, 0, 0, 1, 1, "single pixel"},
      {1, 800, 0, 0, 1, 800, "single column"},
      {800, 1, 0, 0, 800, 1, "single row"},
  };
  for (const FillCase& c : cases)
    for (const reader::Rotation rot : {reader::Rotation::None, reader::Rotation::Ccw})
      for (const bool white : {false, true}) checkFillMatchesReference(c, rot, white);
}

TEST_CASE("the byte-wise fillRect writes nothing outside the rect it was given") {
  // The fast path indexes raw bytes, so an off-by-one in an edge mask corrupts a
  // neighbour rather than failing a pattern check. Fill a rect inset by one pixel
  // on every side and require the border ring is untouched -- one pixel is inside
  // the first and last byte of every row, so the masks are doing the work rather
  // than the byte arithmetic. Both colours, because a mask that is too wide is
  // invisible when the fill colour matches the ground.
  for (const reader::Rotation rot : {reader::Rotation::None, reader::Rotation::Ccw})
    for (const bool white : {false, true}) {
      Framebuffer fb(64, 48, rot);
      fb.clear(!white);  // the ground is the OTHER colour, so any leak shows
      fb.fillRect(1, 1, 62, 46, white);
      for (int x = 0; x < 64; ++x) {
        CHECK(fb.getPixel(x, 0) == !white);
        CHECK(fb.getPixel(x, 47) == !white);
      }
      for (int y = 0; y < 48; ++y) {
        CHECK(fb.getPixel(0, y) == !white);
        CHECK(fb.getPixel(63, y) == !white);
      }
      // ...and it did draw something, so the check above is not passing on a no-op.
      CHECK(fb.getPixel(1, 1) == white);
      CHECK(fb.getPixel(62, 46) == white);
    }
}

TEST_CASE("a degenerate fillRect draws nothing and does not walk off the buffer") {
  // The byte path computes b0/b1 from the run, and (pxHi - 1) >> 3 on an empty
  // run would name a byte before the start of it. Every one of these must return
  // before that arithmetic happens. ASAN is the real assertion here; the frame
  // being untouched is the visible one.
  for (const reader::Rotation rot : {reader::Rotation::None, reader::Rotation::Ccw}) {
    Framebuffer fb(24, 16, rot);
    fb.clear(true);
    fb.fillRect(4, 4, 0, 8, false);
    fb.fillRect(4, 4, 8, 0, false);
    fb.fillRect(4, 4, -5, -5, false);
    fb.fillRect(100, 100, 8, 8, false);
    fb.fillRect(-100, -100, 8, 8, false);
    fb.fillRect(-8, 4, 8, 8, false);  // ends exactly at the left edge
    fb.fillRect(24, 4, 8, 8, false);  // starts exactly at the right edge
    fb.fillRect(4, -8, 8, 8, false);  // ends exactly at the top edge
    fb.fillRect(4, 16, 8, 8, false);  // starts exactly at the bottom edge
    int inked = 0;
    for (int i = 0; i < fb.sizeBytes(); ++i)
      if (fb.data()[i] != 0xFF) ++inked;
    CHECK(inked == 0);
  }
  // An inert buffer has no store at all, so every one of these must be a no-op
  // before data() is dereferenced.
  Framebuffer empty(0, 0);
  empty.fillRect(0, 0, 10, 10, false);
  empty.fillRect(-5, -5, 100, 100, true);
  CHECK(empty.sizeBytes() == 0);
}

TEST_CASE("fillRect over the whole frame is exactly what clear writes") {
  // The two ways to paint a frame have to agree byte for byte, because the paint
  // path uses one and screens use the other over the same pixels. Under rotation
  // this also pins that a full-frame fill covers the whole store rather than the
  // logical rows it names.
  for (const reader::Rotation rot : {reader::Rotation::None, reader::Rotation::Ccw})
    for (const bool white : {false, true}) {
      Framebuffer a(528, 792, rot), b(528, 792, rot);
      a.clear(white);
      b.clear(!white);
      b.fillRect(0, 0, 528, 792, white);
      REQUIRE(a.sizeBytes() == b.sizeBytes());
      int diffs = 0;
      for (int i = 0; i < a.sizeBytes(); ++i)
        if (a.data()[i] != b.data()[i]) ++diffs;
      CHECK(diffs == 0);
    }
}

TEST_CASE("fillRect draws the same bytes into a viewing frame as into an owning one") {
  // The fast path writes through data(), which branches on whether the store is
  // owned or borrowed. A view is what the shell paints into on the device, so it
  // is the case that matters and the one no golden covers.
  for (const reader::Rotation rot : {reader::Rotation::None, reader::Rotation::Ccw}) {
    Framebuffer owned(64, 48, rot);
    const size_t need = static_cast<size_t>(owned.sizeBytes());
    std::vector<uint8_t> storage(need, 0xFF);
    Framebuffer viewed(storage.data(), storage.size(), 64, 48, rot);
    REQUIRE(viewed.sizeBytes() == owned.sizeBytes());
    owned.fillRect(3, 5, 37, 29, false);
    viewed.fillRect(3, 5, 37, 29, false);
    int diffs = 0;
    for (int i = 0; i < owned.sizeBytes(); ++i)
      if (owned.data()[i] != viewed.data()[i]) ++diffs;
    CHECK(diffs == 0);
  }
}

// --- writePackedRow: one logical row of packed bits, and where the rotation is --
//
// THE CACHE HOLDS LOGICAL RASTER ROWS AND THE DEVICE'S FRAME IS Rotation::Ccw, so
// getting one onto the frame is a strided SCATTER and not a memcpy: byteIndex maps
// logical (x, y) to physical (physX = y, physY = width - 1 - x), so all `width`
// pixels of logical row y land at ONE bit position, 0x80 >> (y % 8), in `width`
// different bytes strided by physRowBytes().
//
// THE DESKTOP CANNOT CATCH A WRONG ONE ON ITS OWN. The simulator, every golden and
// tools/compare-design.py are all Rotation::None, where the two branches agree --
// so a version that always memcpy'd passes the entire suite and smears diagonally
// on glass, which is precisely what CLAUDE.md records happening to the veil,
// fillRect, the glyph blit and ditherRect. The Ccw cases below are the only thing
// standing between this function and the panel, and they were proved by MUTATION:
// forcing the unrotated branch for both rotations fails nothing under None and
// fails loudly under Ccw.
//
// The reference is the per-pixel setPixel loop, for the reason fillRectReference
// above is: setPixel IS the specification of what a logical coordinate means, and
// any disagreement means the fast path is wrong. The comparison is over data()
// rather than over pixels, because a mapping that is right per pixel and lays the
// bytes out differently is still a wrong frame from the driver's point of view.

namespace {

void writePackedRowReference(Framebuffer& fb, int y, const uint8_t* row) {
  for (int x = 0; x < fb.width(); ++x)
    fb.setPixel(x, y, (row[x >> 3] & static_cast<uint8_t>(0x80u >> (x & 7))) != 0);
}

// A packed row whose SLACK BITS ARE GARBAGE. The last byte of a row covers eight
// columns and a width that is not a multiple of eight uses only some of them; the
// rest must not reach the frame. Filling them with the same pseudo-random stream
// is what makes a missing edge mask visible -- zeroing them would hide it.
std::vector<uint8_t> packedRow(int w, uint32_t& s) {
  std::vector<uint8_t> row(static_cast<size_t>((w + 7) / 8));
  for (uint8_t& b : row) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;  // xorshift32
    b = static_cast<uint8_t>(s);
  }
  return row;
}

void checkWritePackedRowMatchesReference(int w, int h, reader::Rotation rot) {
  Framebuffer fast(w, h, rot), ref(w, h, rot);
  const uint32_t seed = static_cast<uint32_t>(w * 7919 + h * 104729) | 1u;
  // The ground has no byte-level symmetry, so a scatter that is off by a bit, a
  // byte or a row cannot coincidentally match -- and it is NOT plain white, which
  // would hide every bit the fast path failed to write.
  fillPseudoRandom(fast, seed);
  fillPseudoRandom(ref, seed);

  uint32_t s = seed;
  for (int y = 0; y < h; ++y) {
    const std::vector<uint8_t> row = packedRow(w, s);
    fast.writePackedRow(y, row.data());
    writePackedRowReference(ref, y, row.data());
  }

  REQUIRE(fast.sizeBytes() == ref.sizeBytes());
  int diffs = 0, first = -1;
  for (int i = 0; i < ref.sizeBytes(); ++i)
    if (fast.data()[i] != ref.data()[i]) {
      if (diffs == 0) first = i;
      ++diffs;
    }
  const char* rn = rot == reader::Rotation::Ccw ? "Ccw" : "None";
  CHECK_MESSAGE(diffs == 0, w << "x" << h << " rot=" << rn << ": " << diffs << " of "
                              << ref.sizeBytes() << " bytes differ, first at offset " << first);
}

}  // namespace

TEST_CASE("writePackedRow is byte-identical to a per-pixel setPixel of the same bits") {
  struct Geom { int w, h; const char* what; };
  const Geom cases[] = {
      // The two real panels. Both widths are multiples of 8, which is exactly why
      // the ragged cases below have to exist: nothing on the device reaches the
      // partial last byte and only a test can.
      {528, 792, "X3"},
      {480, 800, "X4"},
      // Neither dimension a multiple of 8, in both orders. Under Ccw the WIDTH is
      // the number of physical rows and the HEIGHT is what the stride is rounded
      // up from, so the two axes fail differently and both have to be ragged.
      {13, 21, "ragged, taller than wide"},
      {21, 13, "ragged, wider than tall"},
      {7, 9, "ragged and small"},
      {9, 7, "ragged and small, transposed"},
      // Height not a multiple of 8 with width that is: under Ccw the row's bit
      // position is 0x80 >> (y % 8), so the last byte COLUMN of the store is the
      // partial one and it is the height that decides how much of it is used.
      {16, 20, "width aligned, height not"},
      {20, 16, "height aligned, width not"},
      // Degenerate shapes, where a loop bound off by one has nowhere to hide.
      {1, 1, "single pixel"},
      {8, 1, "one byte, one row"},
      {1, 800, "single column"},
      {800, 1, "single row"},
      {8, 8, "exactly one byte column"},
      {9, 9, "one bit past a byte in both axes"},
  };
  for (const Geom& g : cases)
    for (const reader::Rotation rot : {reader::Rotation::None, reader::Rotation::Ccw})
      checkWritePackedRowMatchesReference(g.w, g.h, rot);
}

TEST_CASE("writePackedRow puts a known pattern at the logical coordinates it names") {
  // The case above compares against a reference; this one compares against the
  // ARITHMETIC, so a reference and a fast path that shared a bug would still be
  // caught. One black pixel per row, walking diagonally, read back through
  // getPixel -- which is the contract every screen in this firmware draws against.
  for (const reader::Rotation rot : {reader::Rotation::None, reader::Rotation::Ccw}) {
    const int w = 13, h = 21;  // ragged in both axes, on purpose
    Framebuffer fb(w, h, rot);
    fb.clear(true);
    for (int y = 0; y < h; ++y) {
      std::vector<uint8_t> row(static_cast<size_t>((w + 7) / 8), 0xFF);
      const int black = y % w;
      row[static_cast<size_t>(black >> 3)] &= static_cast<uint8_t>(~(0x80u >> (black & 7)));
      fb.writePackedRow(y, row.data());
    }
    int ink = 0;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const bool expectInk = x == y % w;
        REQUIRE(fb.getPixel(x, y) == !expectInk);
        if (expectInk) ++ink;
      }
    CHECK(ink == h);  // and the walk really did ink something on every row
  }
}

TEST_CASE("writePackedRow writes ONE row and leaves the rest of the frame alone") {
  // The scatter walks `width` bytes strided by physRowBytes(), so an off-by-one in
  // the stride or the mirror term lands on a NEIGHBOURING row rather than failing a
  // pattern check -- and under Ccw a wrong bit position corrupts a different row of
  // the same byte column, which is eight rows' worth of blast radius.
  for (const reader::Rotation rot : {reader::Rotation::None, reader::Rotation::Ccw}) {
    const int w = 21, h = 19;
    Framebuffer fb(w, h, rot), untouched(w, h, rot);
    fillPseudoRandom(fb, 0x5EEDu);
    fillPseudoRandom(untouched, 0x5EEDu);

    const int target = 9;  // mid-byte in both axes
    const std::vector<uint8_t> row(static_cast<size_t>((w + 7) / 8), 0x00);  // all ink
    fb.writePackedRow(target, row.data());

    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        CAPTURE(x); CAPTURE(y);
        REQUIRE(fb.getPixel(x, y) == (y == target ? false : untouched.getPixel(x, y)));
      }
  }
}

TEST_CASE("writePackedRow refuses a row that is not on the frame, and a null one") {
  // The scatter indexes raw bytes with no per-pixel bounds check -- that is the
  // whole point of it -- so the ONE check it does have is the whole of its safety.
  // ASAN is the real assertion here; the frame being untouched is the visible one.
  for (const reader::Rotation rot : {reader::Rotation::None, reader::Rotation::Ccw}) {
    Framebuffer fb(24, 16, rot);
    fb.clear(true);
    const std::vector<uint8_t> row(3, 0x00);  // all ink, so any leak shows
    fb.writePackedRow(-1, row.data());
    fb.writePackedRow(16, row.data());
    fb.writePackedRow(1000, row.data());
    fb.writePackedRow(-1000, row.data());
    fb.writePackedRow(0, nullptr);
    fb.writePackedRow(8, nullptr);
    int inked = 0;
    for (int i = 0; i < fb.sizeBytes(); ++i)
      if (fb.data()[i] != 0xFF) ++inked;
    CHECK(inked == 0);
    // ...and the check above is not passing on a function that does nothing.
    fb.writePackedRow(8, row.data());
    for (int x = 0; x < 24; ++x) CHECK_FALSE(fb.getPixel(x, 8));
  }
  // An inert buffer has no store at all, so this must return before data() is
  // dereferenced -- and a refused VIEW is inert while still holding a pointer.
  const std::vector<uint8_t> row(64, 0x00);
  Framebuffer empty(0, 0);
  empty.writePackedRow(0, row.data());
  CHECK(empty.sizeBytes() == 0);
  std::vector<uint8_t> tooSmall(4, 0xFF);
  Framebuffer refused(tooSmall.data(), tooSmall.size(), 16, 4);
  REQUIRE(refused.sizeBytes() == 0);
  refused.writePackedRow(0, row.data());
  for (uint8_t b : tooSmall) CHECK(b == 0xFF);
}

TEST_CASE("writePackedRow draws the same bytes into a viewing frame as into an owning one") {
  // A view is what the shell paints into on the device -- the panel driver's own
  // framebuffer -- and it is the case no golden covers. Same equivalence fillRect
  // is held to, for the same reason.
  for (const reader::Rotation rot : {reader::Rotation::None, reader::Rotation::Ccw}) {
    const int w = 29, h = 37;
    Framebuffer owned(w, h, rot);
    std::vector<uint8_t> storage(static_cast<size_t>(owned.sizeBytes()), 0x00);
    Framebuffer viewed(storage.data(), storage.size(), w, h, rot);
    REQUIRE(viewed.sizeBytes() == owned.sizeBytes());
    owned.clear(true);
    viewed.clear(true);
    uint32_t s = 0xC0FFEEu;
    for (int y = 0; y < h; ++y) {
      const std::vector<uint8_t> row = packedRow(w, s);
      owned.writePackedRow(y, row.data());
      viewed.writePackedRow(y, row.data());
    }
    int diffs = 0;
    for (int i = 0; i < owned.sizeBytes(); ++i)
      if (owned.data()[i] != viewed.data()[i]) ++diffs;
    CHECK(diffs == 0);
  }
}
