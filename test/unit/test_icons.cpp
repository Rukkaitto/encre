#include <vector>

#include "doctest.h"
#include "reader/framebuffer.h"
#include "reader/icons.h"

namespace {
// Named so a failure says which icon, not just "one of nine".
struct Named {
  const char* name;
  const reader::Icon* icon;
};
const Named kAll[] = {{"kBack", &reader::icons::kBack},
                      {"kForward", &reader::icons::kForward},
                      {"kDot", &reader::icons::kDot},
                      {"kHold", &reader::icons::kHold},
                      {"kUp", &reader::icons::kUp},
                      {"kDown", &reader::icons::kDown},
                      {"kChevron", &reader::icons::kChevron},
                      {"kBook", &reader::icons::kBook},
                      {"kBookRow", &reader::icons::kBookRow},
                      {"kFolder", &reader::icons::kFolder},
                      {"kBattery", &reader::icons::kBattery},
                      {"kBatteryCharging", &reader::icons::kBatteryCharging}};

// Big enough for the largest icon in kAll (kBookRow, 44x44) plus the 8px offset
// every case draws at, with room left over to catch a stray pixel on every side.
constexpr int kCanvas = 80;
constexpr int kAt = 8;
}  // namespace

TEST_CASE("every icon draws something inside its own box and nothing outside") {
  // The sizes are each icon's own -- the button marks share a box but the folder
  // and the battery do not -- so the box is read off the icon, never a shared
  // constant. A uniform box is exactly the assumption that let the hand-drawn
  // set stay at 13x13 after the design's icons grew.
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
  // Each value is the width/height its design board renders that SVG at, which
  // is the authority -- tools/iconc.py reads both the geometry and the size out
  // of the board at generation time, so these follow the design rather than
  // leading it. When a board resizes a mark this test is *expected* to fail;
  // re-read the board and update it, do not adjust the generator.
  CHECK(reader::icons::kBack.w == 25);
  CHECK(reader::icons::kBack.h == 25);
  CHECK(reader::icons::kDot.w == 25);
  CHECK(reader::icons::kHold.w == 25);
  CHECK(reader::icons::kHold.h == 25);
  CHECK(reader::icons::kUp.w == 25);
  CHECK(reader::icons::kDown.w == 25);
  CHECK(reader::icons::kChevron.w == 25);
  CHECK(reader::icons::kBook.w == 25);
  CHECK(reader::icons::kBook.h == 25);
  // 44, not 46: the Library board's folder mark used to declare `width="46"`
  // inside a `width: 44px` thumbnail box, so Chrome shrank the <svg> to 44 on
  // the main axis and the board's own render was 44 wide while the generator
  // read 46 off the declaration. The mark the firmware shipped was therefore
  // 4.5% larger than the mark the board draws. The board now declares the size
  // it renders at; nothing about the board's pixels changed.
  CHECK(reader::icons::kFolder.w == 44);
  CHECK(reader::icons::kFolder.h == 39);
  // The third size of the book, and it fills the row slot's 44px width exactly --
  // which is the point of it: the folder above it in the same list is 44 wide and
  // a 25px book read lighter beside it. Square, because the drawing's viewBox is.
  CHECK(reader::icons::kBookRow.w == 44);
  CHECK(reader::icons::kBookRow.h == 44);
  CHECK(reader::icons::kBattery.w == 38);
  CHECK(reader::icons::kBattery.h == 21);
  // The action block's mark is the one non-square mark in the button set: the
  // board draws it 32x25 (design/Main.dc.html:63), wider than the 25x25 menu-row
  // chevron it is not.
  CHECK(reader::icons::kForward.w == 32);
  CHECK(reader::icons::kForward.h == 25);
}

TEST_CASE("the action block's mark is a shafted arrow, not a disclosure chevron") {
  // The firmware drew kChevron in the CONTINUE block for want of this icon, and
  // no size or centring assertion notices a mark that is simply the wrong mark.
  // What separates the two is the shaft: `M1 7h15` runs almost the full width of
  // the arrow's viewBox, so the arrow's middle row is inked nearly edge to edge
  // where the chevron's carries only its vertex. Measured on the bitmap, since
  // that is what the panel shows.
  auto midRowInk = [](const reader::Icon& icon) {
    int n = 0;
    for (int x = 0; x < icon.w; ++x)
      if (reader::coverage(icon, x, icon.h / 2) > 0) ++n;
    return n;
  };
  const reader::Icon& arrow = reader::icons::kForward;
  const reader::Icon& chevron = reader::icons::kChevron;
  CHECK(midRowInk(arrow) >= arrow.w * 3 / 4);
  CHECK(midRowInk(chevron) <= chevron.w / 4);
  // And it is wider than it is tall, which the chevron and every hint mark is
  // not -- so a caller that right-aligns it cannot reuse a square mark's width.
  CHECK(arrow.w > arrow.h);
}

TEST_CASE("every mark that can appear in a hint bar shares one box") {
  // A design rule, not a coincidence: the boards normalised every hint-bar mark
  // to a single box so the four slots sit on one optical line. The bar used to
  // mix 19/23/26px marks and read as ragged. This is the assertion that catches
  // a board reverting one of them in isolation -- the sizes above would still
  // pass individually while the bar went crooked again.
  const reader::Icon* bar[] = {&reader::icons::kBack, &reader::icons::kDot,
                               &reader::icons::kHold, &reader::icons::kUp,
                               &reader::icons::kDown, &reader::icons::kBook,
                               &reader::icons::kChevron};
  for (const reader::Icon* i : bar) {
    CHECK(i->w == bar[0]->w);
    CHECK(i->h == bar[0]->h);
    CHECK(i->w == i->h);  // and each is square
  }
}

TEST_CASE("the two marks a Library row can draw are matched in STROKE, not in box") {
  // THE DESIGN CLAIM THAT EARNED kBookRow, and neither a size nor a placement
  // assertion can see it. A folder row's mark fills the slot's 44px width and a
  // book row's used to be kBook at 25px, so a book read distinctly lighter than
  // the folder one row above it in the same column -- an asymmetry that was never
  // designed, only which assets happened to exist.
  //
  // MATCHED BY STROKE RATHER THAN BY BOX, because these two marks have different
  // aspects (44x39 against 44x44) and different viewBoxes (26 units against 16),
  // so "the same weight" can only mean the stroke. The boards say
  // `stroke-width: 1.8` over 26 units and `1.1` over 16, which at 44px is 3.05 and
  // 3.03 device px.
  //
  // MEASURED ON THE BITMAPS, WHICH IS WHAT THE PANEL SHOWS, and in coverage rather
  // than in inked cells: a 3.03px stroke that is not pixel-aligned lands on FOUR
  // cells at partial coverage, so counting cells reports 4 for one mark and 3 for
  // another that are really 0.02px apart. The mass of the leading inked run on a
  // row -- summed coverage, in thirds of a whole ink pixel -- is the stroke's own
  // width whatever phase it landed on. Taken over the middle half of the rows, so
  // every sampled row crosses one vertical stroke and neither the tab on the
  // folder nor the splay at the foot of the book is in it.
  auto strokeThirds = [](const reader::Icon& i) {
    const int y0 = i.h / 4, y1 = i.h - i.h / 4;
    int total = 0;
    for (int y = y0; y < y1; ++y) {
      int x = 0;
      while (x < i.w && reader::coverage(i, x, y) == 0) ++x;
      while (x < i.w && reader::coverage(i, x, y) > 0) total += reader::coverage(i, x, y), ++x;
    }
    REQUIRE(y1 > y0);
    return total / (y1 - y0);
  };
  const int folder = strokeThirds(reader::icons::kFolder);
  const int bookRow = strokeThirds(reader::icons::kBookRow);
  const int book = strokeThirds(reader::icons::kBook);
  CAPTURE(folder);
  CAPTURE(bookRow);
  CAPTURE(book);
  // Both are 9 thirds -- 3.0px -- on this instrument. The tolerance is one third of
  // a pixel rather than zero, because the two strokes are 0.02px apart in the SVG
  // and which cell phase they quantise onto is not something a board declares.
  CHECK(bookRow - folder <= 1);
  CHECK(folder - bookRow <= 1);
  // AND THE 25px ASSET IS THE NEGATIVE CONTROL, so this test says something the
  // tolerance above could otherwise be satisfied by accident: kBook's stroke is
  // 2.19 device px, which is 7 thirds, so it misses the folder by more than the
  // tolerance in the direction the reader could see. This is the assertion that
  // fails if the row goes back to drawing the smaller asset -- and it is stated
  // here, on the assets, because the row's own tests measure a mark's box and its
  // centring, and kBook passes both of those.
  CHECK(folder - book >= 2);
}

TEST_CASE("the hold ring is the dot drawn hollow, not the dot") {
  // The two marks are the same circle on the same viewBox at the same size and
  // differ only in `fill` -- so the generator's `match` for one of them could
  // silently pick up the other, and no size or placement assertion would notice.
  // What separates them is the interior: the ring's centre is paper, the dot's is
  // ink. Measured on the bitmaps, which is what the panel shows.
  const reader::Icon& ring = reader::icons::kHold;
  const reader::Icon& dot = reader::icons::kDot;
  CHECK(reader::coverage(dot, dot.w / 2, dot.h / 2) == 3);
  CHECK(reader::coverage(ring, ring.w / 2, ring.h / 2) == 0);
  // And the ring is a ring, not an arc: its middle row crosses two strokes, so
  // the row has ink on both sides of that empty centre.
  int left = -1, right = -1;
  for (int x = 0; x < ring.w; ++x)
    if (reader::coverage(ring, x, ring.h / 2) > 0) {
      if (left < 0) left = x;
      right = x;
    }
  REQUIRE(left >= 0);
  CHECK(right > left);
  CHECK(left < ring.w / 2);
  CHECK(right > ring.w / 2);
  // Being hollow, it carries markedly less ink than the disc of the same radius
  // -- it is a lighter mark on the bar, which is the design intent. Not "half":
  // the board strokes it 1 unit wide on a 10-unit viewBox, so at 25px that is a
  // 2.5px stroke and the ring keeps about three fifths of the disc's ink.
  auto ink = [](const reader::Icon& i) {
    int n = 0;
    for (int y = 0; y < i.h; ++y)
      for (int x = 0; x < i.w; ++x) n += reader::coverage(i, x, y);
    return n;
  };
  CHECK(ink(ring) < ink(dot) * 3 / 4);
}

TEST_CASE("every icon's ink is centred in its own box") {
  // The shared placement helpers centre an icon's *box* on its label. That only
  // puts the mark where the eye expects it if the mark is itself centred in its
  // box, so the two halves of the guarantee are asserted separately -- here, and
  // in test_components.cpp against real labels. A pixel of slack is allowed
  // because several of these marks are an odd number of ink rows in an odd-sized
  // box; more than that means a board has drawn a mark off-centre in its viewBox
  // and no amount of correct placement will look right.
  for (const Named& n : kAll) {
    CAPTURE(n.name);
    int top = n.icon->h, bottom = -1;
    for (int y = 0; y < n.icon->h; ++y)
      for (int x = 0; x < n.icon->w; ++x)
        if (reader::coverage(*n.icon, x, y) > 0) {
          if (y < top) top = y;
          if (y > bottom) bottom = y;
        }
    REQUIRE(bottom >= 0);
    // Doubled, so a mark with an even ink height in an odd box needs no fudge.
    const int off = (top + bottom) - (n.icon->h - 1);
    CHECK(off >= -2);
    CHECK(off <= 2);
  }
}

TEST_CASE("the up and down arrows fill their box as fully as the chevron") {
  // The defect this pins was in the *design*: the arrows spanned 37% of their
  // viewBox where the chevron spanned 62%, so they read as undersized however
  // faithfully they were rasterised. Measured on the bitmap rather than on the
  // path data, because the bitmap is what the panel shows.
  auto inkExtent = [](const reader::Icon& icon) {
    int top = icon.h, bottom = -1;
    for (int y = 0; y < icon.h; ++y)
      for (int x = 0; x < icon.w; ++x)
        if (reader::coverage(icon, x, y) > 0) {
          if (y < top) top = y;
          if (y > bottom) bottom = y;
        }
    return bottom - top + 1;
  };
  const int chevron = inkExtent(reader::icons::kChevron);
  for (const reader::Icon* a : {&reader::icons::kUp, &reader::icons::kDown}) {
    CHECK(inkExtent(*a) >= chevron - 1);
  }
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
