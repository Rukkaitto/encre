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
