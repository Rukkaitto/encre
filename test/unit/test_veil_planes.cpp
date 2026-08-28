// THE FIRST OVERLAY OVER A GRAYSCALE SCREEN, which is what issue #1 names as its
// risk. Every overlay today -- ItemActions, DeleteConfirm -- sits over the Library,
// which is Fidelity::Mono, so veilRect has only ever been applied ONCE. A peek sits
// over the Reader, so it is applied once per plane.
//
// IT SHOULD BE CONSISTENT: white in Plane::Bw is paper, and coverage 0 in Lsb/Msb is
// also paper, so the veil's whitening ought to mean the same thing in all three. This
// project's notes are pointed about the difference between "should be" and "was
// measured", so it is measured.
//
// AND IT RUNS UNDER Rotation::Ccw AS WELL AS Rotation::None, because veilRect is one
// of the four byte-wise primitives in core/ that has to know Rotation exists -- and
// the whole desktop is Rotation::None, so a transposed veil passes every golden and
// every simulator PNG and smears diagonally on glass.
#include <cstdint>
#include <utility>
#include <vector>

#include "doctest.h"
#include "reader/dither.h"
#include "reader/framebuffer.h"

namespace {

reader::Framebuffer veiled(int w, int h, reader::Rotation rot, bool ink) {
  reader::Framebuffer fb(w, h, rot);
  fb.clear(!ink);  // clear(true) is paper; clear(false) is solid ink
  reader::veilRect(fb, 0, 0, w, h);
  return fb;
}

bool bytesEqual(const reader::Framebuffer& a, const reader::Framebuffer& b) {
  REQUIRE(a.sizeBytes() == b.sizeBytes());
  REQUIRE(a.sizeBytes() > 0);
  for (int i = 0; i < a.sizeBytes(); ++i)
    if (a.data()[i] != b.data()[i]) return false;
  return true;
}

// Three contents standing for what the three passes of the grayscale sequence
// would actually have drawn into the frame before the veil goes over them. They
// have to be VISIBLY different from each other or the case below is back to
// being the tautology it started as. The diagonals are deliberately on a 3px
// step against the veil's own 3px pitch, which is where a phase that read the
// content instead of the coordinates would show up first.
enum class Content { Bw, Lsb, Msb };

void paint(reader::Framebuffer& fb, Content c) {
  fb.clear(true);
  for (int y = 0; y < fb.height(); ++y)
    for (int x = 0; x < fb.width(); ++x) {
      bool ink = false;
      switch (c) {
        case Content::Bw: ink = (x / 7 + y / 5) % 2 == 0; break;   // coarse blocks
        case Content::Lsb: ink = (x + y) % 3 == 0; break;          // diagonals
        case Content::Msb: ink = (x * y) % 5 == 0; break;          // irregular
      }
      if (ink) fb.setPixel(x, y, false);
    }
}

}  // namespace

TEST_CASE("the veil whitens the same cells whatever the plane had drawn in it") {
  // AN EARLIER FORM OF THIS CASE VEILED THREE IDENTICAL FRAMES AND COMPARED THEM,
  // which is a tautology: veilRect takes no plane argument, so there was nothing
  // for the three calls to differ on. It passed under every mutation -- the
  // forced-unrotated one and a veil rewritten to ink instead of whiten -- while
  // its two siblings caught both. A case that cannot fail is worse than no case,
  // because the file it sits in reports that the property is covered.
  //
  // THE PROPERTY IS THAT THE VEIL IS A CONTENT-INDEPENDENT MASK. veilRect only
  // ever SETS bits, so it can be written as one: `veiled(C) == C | mask`, with the
  // mask a function of geometry alone. That is what "consistent across planes"
  // means -- three passes draw DIFFERENT ink into the same frame shape, and a veil
  // that whitened different cells depending on what it found would leave the
  // composed four-level image disagreeing with itself, an Lsb pass carrying
  // coverage the renderer never computed.
  //
  // The mask is recovered rather than recomputed: ink is a 0 bit and the veil ORs,
  // so veiling an ALL-INK frame leaves exactly the mask standing in the bytes. No
  // second copy of the tile arithmetic, which is what test_dither.cpp's per-pixel
  // reference is for.
  for (const reader::Rotation rot : {reader::Rotation::None, reader::Rotation::Ccw}) {
    for (const auto wh : std::vector<std::pair<int, int>>{{480, 800}, {528, 792}}) {
      const int w = wh.first, h = wh.second;
      CAPTURE(w);
      CAPTURE(h);
      CAPTURE(rot == reader::Rotation::Ccw);
      const reader::Framebuffer mask = veiled(w, h, rot, /*ink=*/true);
      for (const Content c : {Content::Bw, Content::Lsb, Content::Msb}) {
        CAPTURE(static_cast<int>(c));
        reader::Framebuffer content(w, h, rot);
        paint(content, c);
        // The content must actually reach the frame, or this is the old tautology
        // wearing a pattern: an all-paper frame satisfies `veiled(C) == C | mask`
        // for any mask at all.
        reader::Framebuffer expected(w, h, rot);
        paint(expected, c);
        int inked = 0;
        for (int i = 0; i < expected.sizeBytes(); ++i)
          if (expected.data()[i] != 0xFF) ++inked;
        REQUIRE(inked > 0);
        for (int i = 0; i < expected.sizeBytes(); ++i)
          expected.data()[i] = static_cast<uint8_t>(expected.data()[i] | mask.data()[i]);
        reader::veilRect(content, 0, 0, w, h);
        CHECK(bytesEqual(content, expected));
      }
    }
  }
}

TEST_CASE("the veil leaves paper alone, so a plane that inked nothing is untouched") {
  // THE PROPERTY THAT MAKES THE PEEK'S VEIL SOUND. Lsb and Msb carry coverage bits,
  // and coverage 0 is paper exactly as white is paper in Bw -- so a region no glyph
  // reached must come out of the veil unchanged in every plane. If the veil INKED
  // anything, an Lsb pass would gain coverage the renderer never computed, and the
  // composed four-level image would show the veil as a grey wash rather than as
  // whitened ink.
  for (const reader::Rotation rot : {reader::Rotation::None, reader::Rotation::Ccw}) {
    for (const auto wh : std::vector<std::pair<int, int>>{{480, 800}, {528, 792}}) {
      const int w = wh.first, h = wh.second;
      CAPTURE(w);
      CAPTURE(h);
      reader::Framebuffer paper(w, h, rot);
      paper.clear(true);
      reader::Framebuffer reference(w, h, rot);
      reference.clear(true);
      reader::veilRect(paper, 0, 0, w, h);
      CHECK(bytesEqual(paper, reference));
    }
  }
}

TEST_CASE("the veil whitens ink identically at both geometries under both rotations") {
  // The counts, not just the equality: 4 of every 9 pixels of ink survive (one 2x2
  // block per 3x3 cell), which test_dither.cpp already pins on a 36x36 frame. Asserted
  // here at PANEL sizes, because 480, 800, 528 and 792 are none of them multiples of
  // 3 -- so the tile's phase runs off the end of the frame, which is the case a
  // 36x36 test cannot reach.
  for (const reader::Rotation rot : {reader::Rotation::None, reader::Rotation::Ccw}) {
    for (const auto wh : std::vector<std::pair<int, int>>{{480, 800}, {528, 792}}) {
      const int w = wh.first, h = wh.second;
      CAPTURE(w);
      CAPTURE(h);
      const reader::Framebuffer fb = veiled(w, h, rot, /*ink=*/true);
      int ink = 0;
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          if (!fb.getPixel(x, y)) ++ink;
      // Between a third and a half: the exact fraction depends on where the 3px
      // phase falls against a width that is not a multiple of 3, and pinning a
      // single number would be pinning that arithmetic rather than the veil.
      CHECK(ink * 100 / (w * h) >= 40);
      CHECK(ink * 100 / (w * h) <= 48);
    }
  }
}
