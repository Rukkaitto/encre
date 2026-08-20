#include <vector>

#include "doctest.h"
#include "reader/framebuffer.h"
#include "reader/icons.h"

namespace {
// Named so a failure says which icon, not just "one of eight".
struct Named {
  const char* name;
  const reader::Icon* icon;
};
const Named kAll[] = {{"kBack", &reader::icons::kBack},
                      {"kDot", &reader::icons::kDot},
                      {"kUp", &reader::icons::kUp},
                      {"kDown", &reader::icons::kDown},
                      {"kChevron", &reader::icons::kChevron},
                      {"kBook", &reader::icons::kBook},
                      {"kFolder", &reader::icons::kFolder},
                      {"kBattery", &reader::icons::kBattery}};

// Big enough for the largest icon (kFolder, 46x39) plus the 8px offset every
// case draws at, with room left over to catch a stray pixel on every side.
constexpr int kCanvas = 80;
constexpr int kAt = 8;
}  // namespace

TEST_CASE("every icon draws something inside its own box and nothing outside") {
  // The sizes are each icon's own -- 23x23 for the button marks, 25x25 for the
  // book, 46x39 for the folder, 38x21 for the battery -- so the box is read off
  // the icon, never a shared constant. A uniform box is exactly the assumption
  // that let the hand-drawn set stay at 13x13 after the design's icons grew.
  for (const Named& n : kAll) {
    CAPTURE(n.name);
    reader::Framebuffer fb(kCanvas, kCanvas);
    REQUIRE(n.icon->w > 0);
    REQUIRE(n.icon->h > 0);
    REQUIRE(kAt + n.icon->w <= kCanvas);
    REQUIRE(kAt + n.icon->h <= kCanvas);
    reader::drawIcon(fb, *n.icon, kAt, kAt, reader::Ink::Black);
    int inked = 0, outside = 0;
    for (int y = 0; y < kCanvas; ++y)
      for (int x = 0; x < kCanvas; ++x)
        if (!fb.getPixel(x, y)) {
          ++inked;
          const bool inBox =
              x >= kAt && y >= kAt && x < kAt + n.icon->w && y < kAt + n.icon->h;
          if (!inBox) ++outside;
        }
    CHECK(inked > 0);
    CHECK(outside == 0);
  }
}

TEST_CASE("icons are the sizes the design boards draw them at") {
  // Pinned deliberately: the previous failure was not a wrong shape but a right
  // shape at the wrong scale, which no shape-agnostic assertion notices.
  CHECK(reader::icons::kBack.w == 23);
  CHECK(reader::icons::kBack.h == 23);
  CHECK(reader::icons::kDot.w == 23);
  CHECK(reader::icons::kUp.w == 23);
  CHECK(reader::icons::kDown.w == 23);
  CHECK(reader::icons::kChevron.w == 23);
  CHECK(reader::icons::kBook.w == 25);
  CHECK(reader::icons::kBook.h == 25);
  CHECK(reader::icons::kFolder.w == 46);
  CHECK(reader::icons::kFolder.h == 39);
  CHECK(reader::icons::kBattery.w == 38);
  CHECK(reader::icons::kBattery.h == 21);
}

TEST_CASE("every icon carries anti-aliased coverage, not a hard mask") {
  // These are stroke marks with curves and diagonals; if the generator ever
  // reverts to thresholding, every one of them loses its intermediate levels
  // and goes back to the staircase that made the 1-bit chrome illegible.
  for (const Named& n : kAll) {
    CAPTURE(n.name);
    CHECK(n.icon->bpp == 2);
    int levels[4] = {};
    for (int y = 0; y < n.icon->h; ++y)
      for (int x = 0; x < n.icon->w; ++x) {
        const uint8_t c = reader::coverage(*n.icon, x, y);
        REQUIRE(c <= 3);
        ++levels[c];
      }
    CHECK(levels[0] > 0);                  // an icon that fills its box is a blob
    CHECK(levels[3] > 0);                  // and one with no solid ink is a ghost
    CHECK(levels[1] + levels[2] > 0);      // partial coverage survived the pack
  }
}

TEST_CASE("each plane emits ink, and Bw is a subset of the Lsb/Msb union") {
  // The glyph test's counterpart. An anti-aliased mark does NOT render alike in
  // all three planes -- that only holds for fully opaque drawing -- so the
  // invariant is a containment, plus the fact that Lsb and Msb genuinely
  // disagree. That last check is the one that bites: it can only pass if some
  // pixel has coverage 1 or 2, i.e. if the anti-aliasing reached the panel.
  for (const Named& n : kAll) {
    CAPTURE(n.name);
    const reader::Plane planes[3] = {reader::Plane::Bw, reader::Plane::Lsb, reader::Plane::Msb};
    std::vector<std::vector<bool>> ink(3, std::vector<bool>(kCanvas * kCanvas, false));
    int count[3] = {};
    for (int p = 0; p < 3; ++p) {
      reader::Framebuffer fb(kCanvas, kCanvas);
      reader::drawIcon(fb, *n.icon, kAt, kAt, reader::Ink::Black, planes[p]);
      for (int y = 0; y < kCanvas; ++y)
        for (int x = 0; x < kCanvas; ++x)
          if (!fb.getPixel(x, y)) {
            ink[p][y * kCanvas + x] = true;
            ++count[p];
          }
    }
    CHECK(count[0] > 0);
    CHECK(count[1] > 0);
    CHECK(count[2] > 0);

    int bwOutsideUnion = 0, planesDiffer = 0;
    for (int i = 0; i < kCanvas * kCanvas; ++i) {
      if (ink[0][i] && !(ink[1][i] || ink[2][i])) ++bwOutsideUnion;
      if (ink[1][i] != ink[2][i]) ++planesDiffer;
    }
    CHECK(bwOutsideUnion == 0);
    CHECK(planesDiffer > 0);
  }
}

TEST_CASE("icons can draw in white for inverted rows") {
  reader::Framebuffer fb(kCanvas, kCanvas);
  fb.fillRect(0, 0, kCanvas, kCanvas, false);
  reader::drawIcon(fb, reader::icons::kDot, kAt, kAt, reader::Ink::White);
  bool anyWhite = false;
  for (int y = 0; y < kCanvas && !anyWhite; ++y)
    for (int x = 0; x < kCanvas && !anyWhite; ++x)
      if (fb.getPixel(x, y)) anyWhite = true;
  CHECK(anyWhite);
}
