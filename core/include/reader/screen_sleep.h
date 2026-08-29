#pragma once
#include "reader/app.h"
#include "reader/viewmodel.h"

namespace reader {

// WHERE THE CACHED COVER'S PLANES COME FROM.
//
// An interface rather than a buffer, and this is what makes four grey levels
// affordable at the 42,152-byte reading floor: each grayscale pass reads ONE
// plane straight onto the frame, so painting a cover costs no extra RAM at all.
// Holding a 2 bpp image of a 480x800 panel would be 96 KB.
//
// Same shape as SettingsSink, for the same reason -- core/ never learns what a
// filesystem is. The shell implements it over the card, the simulator over
// HostFileSystem, the tests over a buffer.
//
// TWO PLANES SERVE THREE PASSES. Plane::Bw inks where coverage >= 2, which is
// exactly "MSB set", so the Bw base pass and the Msb pass ask for the SAME
// plane. That identity is why the cache is two files' worth of bytes and not
// three (reader/sleep_cover.h says the same thing from the file's side), and an
// implementation that answered Bw with something other than its Msb plane would
// give a base pass that disagrees with the refinement over it.
//
// IT IS NOT A memcpy INTO data(), AND THAT IS THE HALF THAT IS ONLY WRONG ON
// THE DEVICE. The cache holds LOGICAL raster rows -- a streaming row-major
// downscale can emit nothing else (imagefit.h) -- and the shell binds
// Rotation::Ccw, under which one logical row is a physical COLUMN. So an
// implementation blits row by row through Framebuffer::writePackedRow, which is
// the one function in this feature that knows Rotation exists. Under
// Rotation::None, which is every golden and the whole simulator, that reduces to
// the memcpy and the difference is invisible.
class CoverSource {
 public:
  virtual ~CoverSource() = default;

  // Put `plane` on `fb`, covering the whole frame. False means it is not there.
  //
  // A FALSE DOES NOT PROMISE `fb` IS UNTOUCHED, and pretending otherwise would
  // be a contract no streaming implementation can keep -- it finds out the card
  // is gone half way down the picture. What makes that safe is the CALLER:
  // renderSleep clears and draws the field whenever this answers false, so a
  // partial write is overwritten rather than shown.
  //
  // THE PASSES CAN STILL DISAGREE, and it is worth naming rather than implying
  // it away. Fidelity is decided once and the three passes each ask separately,
  // so a source that succeeds for Msb and fails for Lsb composes a frame with
  // the cover in one plane and the field in the other. What keeps that
  // theoretical is where the decision is made: the shell asks sleepCoverUsable
  // BEFORE constructing the screen and passes null when the answer is no, so a
  // non-null source is one whose header has already been validated and the only
  // remaining failure is the card physically leaving mid-paint -- at which point
  // the sleep screen has lost more than its cover.
  virtual bool loadPlane(Plane plane, Framebuffer& fb) = 0;
};

// design/Sleep.dc.html: what the panel holds while the device is asleep.
//
// IT TAKES NO INPUT, and that is not an omission. The shell paints this and then
// calls deep sleep, so there is nobody left to press anything -- the only way out
// is the power button, which is a hardware wake and not an event this screen could
// see. onEvent therefore answers none() for everything, including Back: a screen
// that could be dismissed would imply a running device.
//
// It also draws NO HINT BAR, for the same reason. The bar is a contract about what
// the four front buttons do, and while the device is asleep they do nothing.
//
// E-ink is why this screen exists at all: the glass holds its last image with no
// power, so whatever is painted before sleeping is what the user sees for as long
// as the device is off. Leaving the previous screen there would show a Library or a
// half-read page and give no clue the device is asleep rather than frozen -- which
// this project has confused itself once already (CLAUDE.md: "a frozen screen does
// not mean the firmware ran").
class SleepScreen : public Screen {
 public:
  // `cover` may be null and IS null everywhere but the shell: it is not owned,
  // and it must outlive the screen. Null means there is no cached cover to
  // paint, which is the shipped behaviour and every golden this screen already
  // had -- see fidelity() below.
  explicit SleepScreen(SleepViewModel vm, CoverSource* cover = nullptr);

  ScreenId id() const override { return ScreenId::Sleep; }

  // GRAYSCALE ONLY WHEN THERE IS ACTUALLY A COVER TO PAINT, and this is what
  // keeps SleepShows::Details on today's single ~825 ms waveform. A cover is a
  // photograph: it is the one thing on this device that needs four levels, and
  // three waveforms is what four levels cost (CLAUDE.md, Rendering model). A
  // card, a badge and a dither field are chrome and want Mono.
  //
  // IT IS DECIDED HERE AND NOT FROM THE LOAD, because the shell has to know
  // which sequence to paint before any pass runs. That is also why the shell
  // hands over a null source rather than a source that will refuse: a screen
  // that declared Grayscale and then fell back would spend three waveforms
  // drawing a one-waveform screen.
  Fidelity fidelity() const override {
    return (cover_ != nullptr && vm_.shows != SleepShows::Details) ? Fidelity::Grayscale
                                                                  : Fidelity::Mono;
  }

  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const SleepViewModel& vm() const { return vm_; }

 private:
  SleepViewModel vm_;
  CoverSource* cover_ = nullptr;
};

}  // namespace reader
