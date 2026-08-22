#pragma once
#include "reader/text.h"

namespace reader {
class Framebuffer;
class FontSet;
struct HomeViewModel;
struct SdMissingViewModel;
struct LibraryViewModel;
struct ItemActionsViewModel;
struct DeleteConfirmViewModel;
struct BookDetailsViewModel;
struct SettingsViewModel;
struct SleepViewModel;

// Themes own the entire presentation, layout structure included (spec 3.3).
// The FontSet is supplied by the caller so device knowledge — which asset backs
// which role, any board uiScale — stays out of core/.
class Theme {
 public:
  virtual ~Theme() = default;
  // `plane` defaults to Plane::Bw so existing callers compile unchanged; the
  // caller is expected to invoke this three times, once per Plane, to produce
  // the base frame and the two grey bit-planes (see reader/text.h).
  virtual void renderHome(Framebuffer& fb, const FontSet& fonts, const HomeViewModel& vm,
                          Plane plane = Plane::Bw) = 0;
  // The no-card prompt. A product screen, so it gets its own typed method beside
  // renderHome rather than borrowing the placeholder surface below: it has no
  // header band, no rows and no battery reading, and nothing about it is a list.
  virtual void renderSdMissing(Framebuffer& fb, const FontSet& fonts,
                               const SdMissingViewModel& vm, Plane plane = Plane::Bw) = 0;
  // The Library (spec 4.1). Its own typed method, for the reason
  // renderSdMissing is its own: it is its own board, and a shared "titled list"
  // surface would have to be told which board it was drawing.
  virtual void renderLibrary(Framebuffer& fb, const FontSet& fonts, const LibraryViewModel& vm,
                             Plane plane = Plane::Bw) = 0;
  // The item actions overlay. It draws its own veil and then its panel, over a
  // parent App::render has already painted -- so this method must NOT clear the
  // framebuffer, which is the one way an overlay's render differs in kind from a
  // whole screen's.
  virtual void renderItemActions(Framebuffer& fb, const FontSet& fonts,
                                 const ItemActionsViewModel& vm, Plane plane = Plane::Bw) = 0;
  // The delete confirmation, also an overlay and also drawing over a parent
  // App::render has painted. Its panel is WIDER than the actions panel -- 380
  // against 340 -- which is each board's own number and not a shared one.
  virtual void renderDeleteConfirm(Framebuffer& fb, const FontSet& fonts,
                                   const DeleteConfirmViewModel& vm,
                                   Plane plane = Plane::Bw) = 0;

  // Book details, which is a whole screen and not an overlay -- so it clears the
  // framebuffer and draws its own hint bar like any other screen.
  virtual void renderBookDetails(Framebuffer& fb, const FontSet& fonts,
                                 const BookDetailsViewModel& vm, Plane plane = Plane::Bw) = 0;

  // How many Library rows fit on a panel `panelH` tall.
  //
  // A query rather than a draw, and on the THEME rather than on the screen,
  // because the answer is the board's box model -- the panel less the header band
  // and the hint bar, over a row's height -- and the theme is what owns layout
  // (spec 3.3). The Library needs it before it can paint anything: a scroll
  // window cannot decide whether a focus move scrolls without knowing how many
  // rows are on glass, and `onEvent` has no framebuffer to ask. So the caller
  // that knows the panel size asks this and tells the screen once.
  //
  // Not a constant, for the reason three defects in this project were: the two
  // geometries differ by 8px of height, the band's height depends on its type
  // role, and a row's depends on the faces its two lines are set in.
  virtual int libraryVisibleRows(int panelH, const FontSet& fonts) const = 0;

  // Settings' BOX MODEL, not its row count, and the split is deliberate.
  //
  // Library's items are all one height, so a theme can answer "how many fit"
  // outright. Settings interleaves 54px rows with taller section headers, so the
  // answer depends on WHICH items are in the window -- and the item table belongs
  // to SettingsScreen, not here. So the theme reports the three heights it owns
  // and the screen, which knows where its headers are, does the counting. Neither
  // side ends up holding a copy of the other's data.
  // design/Sleep.dc.html. No hint bar and no focus -- the device is asleep.
  virtual void renderSleep(Framebuffer& fb, const FontSet& fonts, const SleepViewModel& vm,
                           Plane plane) = 0;

  virtual void settingsMetrics(int panelH, const FontSet& fonts, int& listH, int& rowH,
                               int& headerH) const = 0;

  // The provisional Phase 2B surface. A virtual on Theme rather than a screen
  // drawing its own pixels, because "screens never draw pixels directly" holds
  // for scaffolding too -- a diagnostic that bypassed the theme would be the
  // precedent that erodes the rule.
  // design/Settings.dc.html. A scrolling list whose items include section
  // headers, with the same rail Library uses -- see SettingsViewModel.
  virtual void renderSettings(Framebuffer& fb, const FontSet& fonts,
                              const SettingsViewModel& vm, Plane plane) = 0;

};
}  // namespace reader
