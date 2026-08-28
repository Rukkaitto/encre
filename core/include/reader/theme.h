#pragma once
#include "reader/layout.h"
#include "reader/settings.h"
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
struct ReaderViewModel;
struct ReaderMenuViewModel;
struct ContentsViewModel;

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

  // The reader's menu overlay, and the chapter list it opens.
  virtual void renderReaderMenu(Framebuffer& fb, const FontSet& fonts,
                                const ReaderMenuViewModel& vm, Plane plane) = 0;
  virtual void renderContents(Framebuffer& fb, const FontSet& fonts,
                              const ContentsViewModel& vm, Plane plane) = 0;

  // HOW MANY CONTENTS ROWS FIT, which the screen needs before it can window its list.
  //
  // A ROW COUNT, unlike settingsMetrics' box model, and the difference is real: a
  // Settings list interleaves two heights and only the screen knows which items are
  // headers, where a Contents list is also mixed but its section headers come from the
  // BOOK -- so neither side can count without the other's data. The conservative
  // answer is what a caller can actually use: how many of the SHORTER box fit, so a
  // window sized by it never overflows when some of its rows turn out to be taller.
  virtual int contentsVisibleRows(int panelH, const FontSet& fonts) = 0;

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

  // Reader's COLUMN, the same split as settingsMetrics: the theme owns the box
  // model, the screen owns what goes in it. The theme knows the header band's and
  // the footer's heights because it draws them; only the screen can paginate,
  // because only it holds the chapter.
  //
  // Takes the body face as well as the ramp: the column's height is a whole
  // number of the BODY face's line boxes, and the body face is a ScalableFont
  // rasterised at a runtime size, not one of FontSet's eleven fixed roles.
  //
  // AND TAKES THE SETTINGS, because three of them are box-model numbers now --
  // margins, line spacing and alignment (design/Typography.dc.html). The struct
  // rather than three ints: three loose ints at a call site are three chances to
  // pass them in the wrong order, and both the shell and the Typography screen
  // already hold this struct.
  //
  // NO DEFAULT ARGUMENT, deliberately. A defaulted Settings would let a caller
  // that should have been updated compile and silently lay the page out at the
  // defaults -- which on this device is a book that ignores the reader's own
  // settings, and looks like the settings not being saved.
  virtual void readerMetrics(int panelW, int panelH, const FontSet& fonts,
                             const GlyphSource& body, const Settings& settings,
                             PageMetrics& out) const = 0;

  // design/Reader.dc.html.
  //
  // The ONE theme method that takes two content arguments, and the extra one is
  // not a convenience: `page` is already positioned, in framebuffer coordinates,
  // by reader/layout.h -- which is where justification and pagination live and
  // where the assertion that measuring does not rasterise is made. A theme that
  // took only a view model would have to lay the page out itself, and then the
  // screen could not know how many pages there are or which one it is on.
  virtual void renderReader(Framebuffer& fb, const FontSet& fonts, const GlyphSource& body,
                            // THE ITALIC, AND NULL IS A SUPPORTED STATE: emphasis is then
                            // drawn roman, which is what the firmware did before the second
                            // asset existed and what any caller with one face gets.
                            const GlyphSource* italic, const ReaderViewModel& vm,
                            const Page& page, Plane plane) = 0;

};
}  // namespace reader
