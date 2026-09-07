// The two cover sleep screens, at four grey levels, at both panel geometries.
//
// NOT checkGoldenGray's FIRST CALLER, whatever this comment said before: the Reader
// golden took that on 2026-08-22 and the peek's on 2026-08-29, and CLAUDE.md's
// "nothing uses it today" outlived all three. `git log -S"checkGoldenGray(lsb, msb"`
// settles it in one command. What IS true is the rest of this paragraph, and it is
// the part worth keeping. It composes the Lsb and Msb
// planes into one level per pixel exactly as the panel combines them, which is the
// only way a four-level screen can be pinned at all -- and it is also the only
// thing in this repo that pins imagefit.cpp's PACKING. test_imagefit.cpp says so
// itself: everything in it compares our packing against a reference that shares
// it, so a consistently mirrored or inverted bit order satisfies all of it. Here
// the bytes go through a real Framebuffer and out through the PNG writer, and a
// mirrored packing scallops every curve in the picture into 8-pixel steps.
#include <cstring>
#include <string>
#include <vector>

#include "doctest.h"
#include "golden.h"
#include "ramp.h"
#include "reader/components.h"  // kStatusWaking
#include "reader/framebuffer.h"
#include "reader/imagefit.h"
#include "reader/screen_sleep.h"
#include "reader/theme_quiet.h"
#include "reader/viewmodel.h"

namespace {

// --- The picture ---------------------------------------------------------------
//
// SYNTHETIC, NOT design/assets/sleep-cover-480x800.png, and that is a rule rather
// than a convenience. A golden must not depend on a PNG decoder in the test
// harness -- the decoder is itself under test in this feature -- and a source
// generated from arithmetic is reproducible on any machine and readable straight
// through. What it costs is that this golden is not the board's picture, so it
// cannot be compared against the board; `make compare` is where that happens.
//
// EVERY ELEMENT BELOW IS A DETECTOR, and the geometry was chosen for that:
//
//   * a diagonal RAMP, so Floyd-Steinberg has real tone to diffuse. A flat field
//     would make every defect this file exists to catch invisible.
//   * a DISC and a DIAGONAL BAND, because a curved or slanted edge is what shows
//     an 8-pixel byte mirror. A straight vertical edge would not: mirroring
//     inside a byte moves it by at most seven pixels and leaves it straight.
//   * a solid BAND low down, so the badge has something other than paper to be
//     legible over -- paper over paper would prove nothing about the card.
std::vector<uint8_t> syntheticCover(int w, int h) {
  std::vector<uint8_t> px(static_cast<size_t>(w) * static_cast<size_t>(h));
  const long cx = w * 45 / 100, cy = h * 34 / 100, r = w * 30 / 100;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      // Light at the top left, near-black at the foot: an inverted plane packing
      // reads as an upside-down cover rather than as a subtle shift.
      int v = 240 - (x * 60 / (w - 1)) - (y * 170 / (h - 1));
      const long dx = x - cx, dy = y - cy;
      if (dx * dx + dy * dy <= r * r) v = 255 - v;
      const long d = static_cast<long>(x) * 2 + y - w;
      if (d >= 0 && d < w / 12) v = 25;
      if (y >= h * 78 / 100 && y < h * 81 / 100) v = 0;
      if (v < 0) v = 0;
      if (v > 255) v = 255;
      px[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] =
          static_cast<uint8_t>(v);
    }
  }
  return px;
}

// --- The source ------------------------------------------------------------------
//
// A CoverSource over two plane rasters held in memory. The device reads them off
// the card a row at a time; a test may hold both.
struct FakeCover : reader::CoverSource {
  int panelW = 0, panelH = 0, rowBytes = 0;
  std::vector<uint8_t> msb, lsb;  // panelH rows of rowBytes, top row first
  bool ok = true;
  int loads = 0;

  bool loadPlane(reader::Plane plane, reader::Framebuffer& fb) override {
    ++loads;
    if (!ok) return false;
    if (fb.width() != panelW || fb.height() != panelH) return false;
    // Bw inks where coverage >= 2, which is exactly "MSB set" -- so the Bw base
    // pass and the Msb pass read the SAME plane. That identity is the whole
    // reason the cache holds two planes and not three, and honouring it here is
    // what makes this fake the same shape as the shell's real source.
    const std::vector<uint8_t>& src = (plane == reader::Plane::Lsb) ? lsb : msb;
    if (src.size() != static_cast<size_t>(rowBytes) * static_cast<size_t>(panelH)) return false;
    // NOT a memcpy into fb.data(). The cache holds LOGICAL raster rows and the
    // shell binds Rotation::Ccw, under which one logical row is a physical
    // column -- see reader/sleep_cover.h. writePackedRow is the one function in
    // this feature that knows that, and using it here means the test drives the
    // path the device drives.
    for (int y = 0; y < panelH; ++y)
      fb.writePackedRow(y, src.data() + static_cast<size_t>(y) * static_cast<size_t>(rowBytes));
    return true;
  }
};

// Fills both planes by running the synthetic picture through the real
// CoverFitter, so the goldens hold the output of the shipped downscale and
// diffusion rather than a second implementation of them.
FakeCover coverFor(int w, int h) {
  FakeCover c;
  c.panelW = w;
  c.panelH = h;
  c.rowBytes = (w + 7) / 8;
  const size_t plane = static_cast<size_t>(c.rowBytes) * static_cast<size_t>(h);
  c.msb.assign(plane, 0xFFu);
  c.lsb.assign(plane, 0xFFu);

  // 2:3, which is 160 of the 225 corpus covers (reader/cover_fit.h) and bigger
  // than either panel in both axes, so Fill crops rather than degrading to
  // Whole-at-1:1 and the box is the whole frame.
  const int sw = 700, sh = 1050;
  const std::vector<uint8_t> px = syntheticCover(sw, sh);

  reader::CoverFitter f;
  REQUIRE(f.begin(sw, sh, w, h, reader::CoverFit::Fill));
  REQUIRE(f.box().dstW == w);
  REQUIRE(f.box().dstH == h);
  REQUIRE(f.box().dstX == 0);
  REQUIRE(f.box().dstY == 0);
  for (int y = 0; y < sh; ++y) {
    REQUIRE(f.addRow(px.data() + static_cast<size_t>(y) * static_cast<size_t>(sw)));
    while (f.nextRow()) {
      const size_t at = static_cast<size_t>(f.box().dstY + f.lastEmittedRow()) *
                        static_cast<size_t>(c.rowBytes);
      std::memcpy(c.msb.data() + at, f.msbRow(), static_cast<size_t>(c.rowBytes));
      std::memcpy(c.lsb.data() + at, f.lsbRow(), static_cast<size_t>(c.rowBytes));
    }
  }
  REQUIRE(f.rowsEmitted() == h);
  return c;
}

// design/SleepCoverDetails.dc.html's card is design/Sleep.dc.html's, byte for
// byte -- but a DIFFERENT book from this file's other copy on purpose, so a
// golden that accidentally rendered the plain Sleep view model is obvious.
reader::SleepViewModel sampleWithCover(reader::SleepShows shows) {
  reader::SleepViewModel vm;
  vm.label = "NOW READING";
  vm.title = "Gullible's Travels";
  vm.author = "Ring Lardner";
  vm.progressPercent = 34;
  vm.progress = "34% \xC2\xB7 CH. 07";
  vm.note = "ASLEEP \xC2\xB7 HOLD POWER TO WAKE";
  vm.shows = shows;
  return vm;
}

// The frame the cover alone makes, with no theme involved -- the reference the
// COVER mode has to reproduce exactly.
reader::Framebuffer bareCover(FakeCover& cov, int w, int h) {
  reader::Framebuffer fb(w, h);
  fb.clear(true);
  REQUIRE(cov.loadPlane(reader::Plane::Msb, fb));
  return fb;
}

reader::Framebuffer renderSleepTo(int w, int h, reader::SleepShows shows, FakeCover* cov,
                                  reader::Plane plane, const ramp::Ramp& ramp,
                                  reader::QuietTheme& theme) {
  reader::Framebuffer fb(w, h);
  reader::SleepScreen scr(sampleWithCover(shows), cov);
  scr.render(fb, ramp.fonts, theme, plane);
  return fb;
}

}  // namespace

TEST_CASE("a cover sleep screen declares Grayscale and a plain one does not") {
  // Dynamic fidelity is what keeps DETAILS at today's single ~825 ms waveform.
  // Pinned here rather than assumed, exactly as the Sleep golden pins Mono.
  FakeCover cov = coverFor(480, 800);
  CHECK(reader::SleepScreen(sampleWithCover(reader::SleepShows::CoverAndDetails), &cov)
            .fidelity() == reader::Fidelity::Grayscale);
  CHECK(reader::SleepScreen(sampleWithCover(reader::SleepShows::Cover), &cov).fidelity() ==
        reader::Fidelity::Grayscale);
  CHECK(reader::SleepScreen(sampleWithCover(reader::SleepShows::Details), &cov).fidelity() ==
        reader::Fidelity::Mono);
  // No cover source at all: Mono, whatever the mode asked for. This is the
  // shipped screen, and it is what every existing sleep golden renders.
  CHECK(reader::SleepScreen(sampleWithCover(reader::SleepShows::Cover), nullptr).fidelity() ==
        reader::Fidelity::Mono);
  CHECK(reader::SleepScreen(sampleWithCover(reader::SleepShows::CoverAndDetails), nullptr)
            .fidelity() == reader::Fidelity::Mono);
}

TEST_CASE("DETAILS never asks the source for a plane") {
  // The mode that must cost nothing. Asserted on the CALL rather than on the
  // pixels, because a source that was asked and whose answer was then thrown
  // away would render identically and still have paid a card read per pass.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  FakeCover cov = coverFor(480, 800);
  reader::Framebuffer fb(480, 800);
  reader::SleepScreen scr(sampleWithCover(reader::SleepShows::Details), &cov);
  scr.render(fb, ramp.fonts, theme, reader::Plane::Bw);
  CHECK(cov.loads == 0);
}

TEST_CASE("QuietTheme renders both cover sleep screens to golden at both geometries") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;

  auto renderOne = [&](int w, int h, reader::SleepShows shows, const std::string& name) {
    FakeCover cov = coverFor(w, h);
    reader::SleepScreen scr(sampleWithCover(shows), &cov);
    REQUIRE(scr.fidelity() == reader::Fidelity::Grayscale);
    reader::Framebuffer lsb(w, h), msb(w, h);
    scr.render(lsb, ramp.fonts, theme, reader::Plane::Lsb);
    scr.render(msb, ramp.fonts, theme, reader::Plane::Msb);
    golden::checkGoldenGray(lsb, msb, name);
  };

  renderOne(480, 800, reader::SleepShows::CoverAndDetails, "sleep_cover_details");
  renderOne(528, 792, reader::SleepShows::CoverAndDetails, "sleep_cover_details_x3");
  renderOne(480, 800, reader::SleepShows::Cover, "sleep_cover");
  renderOne(528, 792, reader::SleepShows::Cover, "sleep_cover_x3");
}

TEST_CASE("the grayscale BASE pass is the Msb pass, which is what the panel assumes") {
  // Two planes serve three passes. If Bw and Msb ever diverged, the base the
  // refinement builds on would not be the plane it refines -- and nothing else in
  // the suite renders a screen through both.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  FakeCover a = coverFor(480, 800), b = coverFor(480, 800);
  const reader::Framebuffer bw =
      renderSleepTo(480, 800, reader::SleepShows::CoverAndDetails, &a, reader::Plane::Bw, ramp,
                    theme);
  const reader::Framebuffer msb =
      renderSleepTo(480, 800, reader::SleepShows::CoverAndDetails, &b, reader::Plane::Msb, ramp,
                    theme);
  CHECK(golden::identical(bw, msb));
}

TEST_CASE("COVER draws the cover and NOTHING else; every fallback puts the badge back") {
  // ASSERTED AGAINST THE COVER ITSELF, not by counting ink in a band. An ink
  // count reads the badge as ADDING ink, and the badge is a white box with a
  // 1px outline -- over a dark cover it REMOVES far more than it adds, so the
  // obvious inequality is the wrong way round for exactly the picture this
  // feature exists to draw. Frame identity has no such direction.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  const int w = 480, h = 800;

  FakeCover ref = coverFor(w, h);
  const reader::Framebuffer plain = bareCover(ref, w, h);

  FakeCover c1 = coverFor(w, h);
  const reader::Framebuffer coverOnly =
      renderSleepTo(w, h, reader::SleepShows::Cover, &c1, reader::Plane::Msb, ramp, theme);
  CHECK(golden::identical(coverOnly, plain));

  // ...and COVER + DETAILS is the same cover with the card and the badge on it.
  FakeCover c2 = coverFor(w, h);
  const reader::Framebuffer withCard =
      renderSleepTo(w, h, reader::SleepShows::CoverAndDetails, &c2, reader::Plane::Msb, ramp,
                    theme);
  CHECK_FALSE(golden::identical(withCard, plain));
  // The badge's own band, so "the card differs" cannot stand in for "the badge is
  // there". design/Sleep.dc.html puts it 34px off the bottom and it is one line of
  // --t-meta in an 8/18 padded box with a 1px border, so it lives inside the
  // bottom 90 rows and clear of the last 20.
  CHECK_FALSE(golden::rowsIdentical(withCard, plain, h - 90, h - 20));
  // The card is centred, and COVER must not be drawing it faintly somewhere.
  CHECK(golden::rowsIdentical(coverOnly, plain, h - 90, h - 20));

  // THE FALLBACK IS THE POINT: a COVER whose cover will not load must draw the
  // field, the card AND the badge, because the reason the badge may be hidden --
  // that a full-bleed cover is unmistakable -- is false when there is no cover.
  FakeCover dead = coverFor(w, h);
  dead.ok = false;
  FakeCover dead2 = coverFor(w, h);
  dead2.ok = false;
  const reader::Framebuffer refused =
      renderSleepTo(w, h, reader::SleepShows::Cover, &dead, reader::Plane::Bw, ramp, theme);
  const reader::Framebuffer details =
      renderSleepTo(w, h, reader::SleepShows::Details, &dead2, reader::Plane::Bw, ramp, theme);
  CHECK(golden::identical(refused, details));
  // And it is not the cover with the badge stuck on: a refused load falls all the
  // way back to the dithered field.
  CHECK_FALSE(golden::identical(refused, plain));

  // No source at all is the same fallback by a different route.
  const reader::Framebuffer noSource =
      renderSleepTo(w, h, reader::SleepShows::Cover, nullptr, reader::Plane::Bw, ramp, theme);
  CHECK(golden::identical(noSource, details));
}

TEST_CASE("a WAKING cover screen keeps the badge and still drops the card") {
  // THE WAKE'S OWN RULE, AND IT IS THE ONE EXCEPTION TO `coverOnly`. A sleeping
  // COVER screen may drop the badge because a full-bleed cover is not a screen
  // this device can otherwise be in -- the picture says "asleep" unaided. A
  // WAKING screen is making a different claim, and the cover is byte-identical
  // in both states, so without the badge a COVER-mode wake would paint something
  // indistinguishable from the sleep it is waking from.
  //
  // ASSERTED BY FRAME IDENTITY, not by counting ink, for this file's own reason:
  // the badge is a white box with a 1px outline, so over a dark cover it REMOVES
  // far more ink than it adds and the obvious inequality points the wrong way.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  const int w = 480, h = 800;

  FakeCover ref = coverFor(w, h);
  const reader::Framebuffer plain = bareCover(ref, w, h);

  auto renderWaking = [&](reader::SleepShows shows, FakeCover* cov) {
    reader::Framebuffer fb(w, h);
    reader::SleepViewModel vm = sampleWithCover(shows);
    vm.note = reader::kStatusWaking;
    vm.waking = true;
    reader::SleepScreen(vm, cov).render(fb, ramp.fonts, theme, reader::Plane::Msb);
    return fb;
  };

  // The badge's own band -- design/Sleep.dc.html puts it 34px off the bottom and
  // it is one line of --t-meta in an 8/18 padded box with a 1px border, so it is
  // inside the bottom 90 rows and clear of the last 20. The SAME band the
  // sleeping case above asserts is untouched.
  FakeCover c1 = coverFor(w, h);
  const reader::Framebuffer waking = renderWaking(reader::SleepShows::Cover, &c1);
  CHECK_FALSE(golden::rowsIdentical(waking, plain, h - 90, h - 20));
  CHECK_FALSE(golden::identical(waking, plain));

  // ...and the CARD is still gone, which is the half `waking` must NOT reach: a
  // waking COVER screen is the cover and the words, never the cover and the
  // reading card. The card is centred, so rows around the middle are the cover's.
  CHECK(golden::rowsIdentical(waking, plain, h / 2 - 120, h / 2 + 120));

  // The sleeping screen it differs from, so this case cannot pass by the badge
  // having been drawn all along. Same source, same mode, `waking` the only
  // difference -- and dropping `&& !vm.waking` from theme_quiet.cpp makes these
  // two frames equal, which is what fails here.
  FakeCover c2 = coverFor(w, h);
  const reader::Framebuffer asleep =
      renderSleepTo(w, h, reader::SleepShows::Cover, &c2, reader::Plane::Msb, ramp, theme);
  CHECK(golden::identical(asleep, plain));
  CHECK_FALSE(golden::identical(waking, asleep));

  // COVER + DETAILS is unaffected by the flag: it never suppressed anything, so a
  // waking one is the card and the badge over the cover exactly as a sleeping one
  // is, with only the note's words different.
  FakeCover c3 = coverFor(w, h);
  FakeCover c4 = coverFor(w, h);
  reader::Framebuffer bothWaking = renderWaking(reader::SleepShows::CoverAndDetails, &c3);
  reader::Framebuffer bothAsleep(w, h);
  {
    reader::SleepViewModel vm = sampleWithCover(reader::SleepShows::CoverAndDetails);
    vm.note = reader::kStatusWaking;  // same words, so only `waking` can differ
    reader::SleepScreen(vm, &c4).render(bothAsleep, ramp.fonts, theme, reader::Plane::Msb);
  }
  CHECK(golden::identical(bothWaking, bothAsleep));

  // AND WITH NO COVER THE FLAG DOES NOTHING AT ALL, which is what keeps the
  // sleep_waking goldens where they are: `coverOnly` is already false, so the
  // badge was never at risk and there is nothing for `waking` to put back.
  reader::Framebuffer noCoverWaking = renderWaking(reader::SleepShows::Cover, nullptr);
  reader::Framebuffer noCoverAsleep(w, h);
  {
    reader::SleepViewModel vm = sampleWithCover(reader::SleepShows::Cover);
    vm.note = reader::kStatusWaking;
    reader::SleepScreen(vm, nullptr).render(noCoverAsleep, ramp.fonts, theme,
                                           reader::Plane::Msb);
  }
  CHECK(golden::identical(noCoverWaking, noCoverAsleep));
}

TEST_CASE("COVER + DETAILS is the plain Sleep screen with its BACKGROUND replaced") {
  // design/SleepCoverDetails.dc.html's own claim, in as many words: "anything that
  // differs here from Sleep.dc.html is a defect rather than a design decision".
  // The goldens show it and only a human reading them can say so; this says it
  // per pixel, and with no geometry -- naming the card's box here would be a
  // second copy of an arithmetic the theme already does.
  //
  // Every pixel is one of two things. Either it is BACKGROUND, and each render
  // agrees with its own backdrop; or it is CHROME, and the two renders agree with
  // EACH OTHER because the card and the badge are opaque and identically placed.
  // A card that moved, resized or restyled over a cover fails the second clause.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  const int w = 480, h = 800;

  FakeCover ref = coverFor(w, h);
  const reader::Framebuffer plainCover = bareCover(ref, w, h);
  reader::Framebuffer plainField(w, h);
  {
    reader::SleepViewModel bare;
    bare.nothingToContinue = true;
    bare.shows = reader::SleepShows::Details;  // ...and no note, so no badge either
    theme.renderSleep(plainField, ramp.fonts, bare, reader::Plane::Msb, nullptr);
  }

  FakeCover cov = coverFor(w, h);
  const reader::Framebuffer overCover =
      renderSleepTo(w, h, reader::SleepShows::CoverAndDetails, &cov, reader::Plane::Msb, ramp,
                    theme);
  const reader::Framebuffer overField =
      renderSleepTo(w, h, reader::SleepShows::CoverAndDetails, nullptr, reader::Plane::Msb,
                    ramp, theme);

  int bad = 0;
  for (int y = 0; y < h && bad == 0; ++y) {
    for (int x = 0; x < w; ++x) {
      const bool background = overCover.getPixel(x, y) == plainCover.getPixel(x, y) &&
                              overField.getPixel(x, y) == plainField.getPixel(x, y);
      const bool chrome = overCover.getPixel(x, y) == overField.getPixel(x, y);
      if (!background && !chrome) {
        CHECK_MESSAGE(false, "the card differs over a cover at (" << x << ", " << y << ")");
        ++bad;
        break;
      }
    }
  }
  CHECK(bad == 0);
}

TEST_CASE("a refused load never leaves half a cover on the frame") {
  // loadPlane does not promise it left the frame alone -- a streaming source
  // finds out the card is gone half way down the picture. What makes that safe is
  // renderSleep clearing, and this is the assertion that keeps the clear where it
  // is: a source that writes the top half and THEN fails must be invisible.
  struct HalfCover : reader::CoverSource {
    FakeCover full;
    bool loadPlane(reader::Plane plane, reader::Framebuffer& fb) override {
      for (int y = 0; y < fb.height() / 2; ++y)
        fb.writePackedRow(y, (plane == reader::Plane::Lsb ? full.lsb : full.msb).data() +
                                 static_cast<size_t>(y) * static_cast<size_t>(full.rowBytes));
      return false;
    }
  };
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  HalfCover half;
  half.full = coverFor(480, 800);

  reader::Framebuffer got(480, 800), want(480, 800);
  reader::SleepScreen(sampleWithCover(reader::SleepShows::CoverAndDetails), &half)
      .render(got, ramp.fonts, theme, reader::Plane::Bw);
  reader::SleepScreen(sampleWithCover(reader::SleepShows::Details), nullptr)
      .render(want, ramp.fonts, theme, reader::Plane::Bw);
  CHECK(golden::identical(got, want));
}
