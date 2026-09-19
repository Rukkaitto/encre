#pragma once
#include "reader/layout.h"
#include "reader/settings.h"
#include "reader/text.h"

namespace reader {
class Framebuffer;
class FontSet;
class CoverSource;
struct HomeViewModel;
struct SdMissingViewModel;
struct BookEndViewModel;
struct BatteryEmptyViewModel;
struct LibraryViewModel;
struct ItemActionsViewModel;
struct DeleteConfirmViewModel;
struct BookErrorViewModel;
struct BookDetailsViewModel;
struct SettingsViewModel;
struct WifiSettingsViewModel;
struct WifiPickerViewModel;
struct TextEntryViewModel;
struct WifiConnectViewModel;
struct WifiErrorViewModel;
struct WifiNetworkActionsViewModel;
struct SleepViewModel;
struct ReaderViewModel;
struct ReaderMenuViewModel;
struct ContentsViewModel;
struct NamesViewModel;
struct TypographyViewModel;
struct PeekViewModel;
struct ArticlesViewModel;
struct ArticleActionsViewModel;
struct ArticleEndViewModel;
struct WallabagAccountViewModel;
struct WallabagConnectingViewModel;
struct WallabagErrorViewModel;

// THE PEEK PANEL'S HEIGHT. The BOX is the constant and the LINE COUNT is the result,
// and this is the inversion of what shipped -- there was a `kPeekLines = 8` here and
// the height was derived from it.
//
// design/Peek.dc.html carried the argument for the old shape and it does not hold. It
// ran: a pinned height "cut the last line in half lengthwise", therefore the panel
// must be sized by its text. The premise is true; the conclusion needs a step that is
// missing. A pinned height only cuts a line in half if the COUNT is not floored, and
// PageBuilder floors it already -- it lays out whole lines and fits
// `pxToF26(columnH) / leadF26` of them (rowsThatFit, layout.h). Pin the box, floor
// the count, and every line is whole AND the panel is one size.
//
// TWO THINGS THE OLD SHAPE COST, BOTH MEASURED:
//
//   * ON GLASS THE PANEL WAS "A LOT SHORTER" THAN THE SIMULATOR SHOWS. Reported by a
//     reader running a smaller ppem and a tighter lead -- 17 lines in their reading
//     column where the default fits 12. `lineBox` shrinks with both, so eight of THEIR
//     boxes is ~310px against 546: a small box adrift in a lot of veil, on a screen
//     whose whole job is to read as a modal rather than as a bordered full screen. No
//     board and no golden could show it, because nothing renders the reader at
//     non-default typography (#40).
//   * AT THE TOP OF THE SETTINGS RAMP THE PANEL WAS TALLER THAN THE GLASS. The widest
//     line box either ramp can ask for is kBodyPpemSteps' 46 at kLineSpacingSteps'
//     2000 -- 92px -- so eight of them was a 736px column and an 846px panel, against
//     800 on the X4 and 792 on the X3. centreIn then yields a NEGATIVE origin and the
//     panel runs off both edges. A fixed box cannot do that, and 546 fits both panels
//     with 127px (X4) / 123px (X3) of veil above it.
//
// 546 IS WHAT THE OLD DERIVATION PRODUCED AT THE DEFAULT SETTINGS, to the pixel, which
// is what makes this a re-derivation rather than a redesign: 4px of border, a 70px
// band, 16px above the text and 20px below leaves 436px of column, and 436 holds
// exactly eight 54.4px line boxes. Neither peek golden moves.
//
// WHAT IS LEFT OVER IS SLACK at the foot of the panel, which is the precedent
// design/Typography.dc.html's preview box already set: "the box's height is DERIVED
// and fixed with respect to the settings, so the five rows never move and there is
// visible slack at large sizes".
//
// The line count is now a QUERY -- Theme::peekVisibleLines -- for the reason
// libraryVisibleRows and contentsVisibleRows are: it depends on the type ramp and on
// the reader's settings, so it is not a number anything can pin.
inline constexpr int kPeekPanelH = 546;

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
  // design/BookEnd.dc.html. Its own board and its own method, for the reason
  // renderSdMissing has one: a shared "prompt with two slabs" abstraction would have
  // to carry a header band that SdMissing does not draw and a bottom-anchored note
  // that nothing else does.
  virtual void renderBookEnd(Framebuffer& fb, const FontSet& fonts,
                             const BookEndViewModel& vm, Plane plane = Plane::Bw) = 0;
  // design/BatteryEmpty.dc.html, the critical-shutdown screen. Its own method for
  // renderSdMissing's reason again: it shares that board's centred column and shares
  // none of its furniture -- no action slab, no hint bar -- and it ends in the badge
  // that only Sleep draws.
  virtual void renderBatteryEmpty(Framebuffer& fb, const FontSet& fonts,
                                  const BatteryEmptyViewModel& vm, Plane plane = Plane::Bw) = 0;
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

  // The corrupt-book dialog, an overlay like the confirmation and drawing over a
  // parent App::render has painted -- so this must NOT clear the framebuffer.
  //
  // Its own typed method rather than a shared "panel with a paragraph and two
  // slabs" surface: it draws a MARK the confirmation does not, and a shared
  // abstraction would have to be told which board it was drawing. That is the same
  // reasoning renderSdMissing and renderBookEnd each carry.
  virtual void renderBookError(Framebuffer& fb, const FontSet& fonts,
                               const BookErrorViewModel& vm, Plane plane = Plane::Bw) = 0;

  // --- The V1.1 connect flow -------------------------------------------
  //
  // Six methods rather than one "a list, a panel and some slabs" surface, for
  // renderBookError's stated reason: a shared abstraction would have to be told
  // which board it was drawing, which is a caller list wearing a parameter's
  // clothes.

  // design/WifiSettings.dc.html, and design/WifiSettingsEmpty.dc.html as the
  // same method -- `nothingSaved` is the whole branch, because two render paths
  // would be two ways to spell one layout.
  virtual void renderWifiSettings(Framebuffer& fb, const FontSet& fonts,
                                  const WifiSettingsViewModel& vm, Plane plane = Plane::Bw) = 0;

  // design/WifiPicker.dc.html and its scrolled and empty variants. The second
  // scrolling list in the firmware, and the second user of the rail.
  virtual void renderWifiPicker(Framebuffer& fb, const FontSet& fonts,
                                const WifiPickerViewModel& vm, Plane plane = Plane::Bw) = 0;

  // design/WifiPassword.dc.html -- a 10x4 character grid over a function row of
  // six, and the only screen here that draws something components.h has no
  // primitive for.
  //
  // NAMED FOR THE MECHANISM AND NOT FOR THE BOARD, because the screen behind it
  // was extracted before its second caller (#126) and this draws what the
  // view-model holds: a band, a field, a counter row, a grid and a note. There
  // is one board today and it is Wi-Fi's.
  virtual void renderTextEntry(Framebuffer& fb, const FontSet& fonts,
                               const TextEntryViewModel& vm, Plane plane = Plane::Bw) = 0;

  // design/WifiConnect.dc.html -- an overlay, and the caption is the whole
  // indicator: the board's eight-cell ticker is gone.
  virtual void renderWifiConnect(Framebuffer& fb, const FontSet& fonts,
                                 const WifiConnectViewModel& vm, Plane plane = Plane::Bw) = 0;

  // design/WifiError.dc.html and its two siblings -- one method, three copy
  // shapes, and `offersEdit` decides whether the first slab is drawn AT ALL.
  virtual void renderWifiError(Framebuffer& fb, const FontSet& fonts,
                               const WifiErrorViewModel& vm, Plane plane = Plane::Bw) = 0;

  // design/WifiNetworkActions.dc.html -- the actions panel's box with one row.
  virtual void renderWifiNetworkActions(Framebuffer& fb, const FontSet& fonts,
                                        const WifiNetworkActionsViewModel& vm,
                                        Plane plane = Plane::Bw) = 0;

  // --- Articles over wallabag (V1.1) -----------------------------------
  //
  // Six methods for the connect flow's stated reason, which is renderBookError's:
  // a shared "a list, a panel and some slabs" surface would have to be told which
  // board it was drawing, and a parameter naming a board is a caller list wearing
  // a parameter's clothes.
  //
  // ASSEMBLED FROM SHIPPED PRIMITIVES, NOT NEW GEOMETRY. Every one of these six
  // is a board this firmware already draws with different content in it, which is
  // why the flow could be built screens-first on fixture data at all.

  // design/Articles.dc.html, with design/ArticlesSetup.dc.html and
  // design/SyncDone.dc.html as the SAME method -- `notSetUp` and a non-empty
  // `statusLine` are the two branches. Three render paths would be three ways to
  // spell one layout, which is renderWifiSettings' own rule one flow over.
  virtual void renderArticles(Framebuffer& fb, const FontSet& fonts,
                              const ArticlesViewModel& vm, Plane plane = Plane::Bw) = 0;

  // design/ArticleActions.dc.html -- an overlay, so this must NOT clear the
  // framebuffer. renderItemActions' panel with a caption that has no value slot.
  virtual void renderArticleActions(Framebuffer& fb, const FontSet& fonts,
                                    const ArticleActionsViewModel& vm,
                                    Plane plane = Plane::Bw) = 0;

  // design/ArticleEnd.dc.html -- renderBookEnd's screen with a band VALUE and a
  // slab list whose LENGTH is the shape.
  virtual void renderArticleEnd(Framebuffer& fb, const FontSet& fonts,
                                const ArticleEndViewModel& vm, Plane plane = Plane::Bw) = 0;

  // design/WallabagAccount.dc.html -- renderSettings' rows and section header,
  // plus a paragraph under the last row.
  virtual void renderWallabagAccount(Framebuffer& fb, const FontSet& fonts,
                                     const WallabagAccountViewModel& vm,
                                     Plane plane = Plane::Bw) = 0;

  // design/WallabagConnecting.dc.html AND design/WallabagFetching.dc.html -- one
  // method, two stages, and the stage lives entirely in the strings. An overlay.
  virtual void renderWallabagConnecting(Framebuffer& fb, const FontSet& fonts,
                                        const WallabagConnectingViewModel& vm,
                                        Plane plane = Plane::Bw) = 0;

  // design/WallabagError.dc.html and its two siblings -- one method, three copy
  // shapes, and the slab LIST is the shape (there is no offersRetry flag; see the
  // view-model). An overlay.
  virtual void renderWallabagError(Framebuffer& fb, const FontSet& fonts,
                                   const WallabagErrorViewModel& vm,
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
  // How many ARTICLE rows fit, which is libraryVisibleRows' question over a
  // different row height and one row fewer of chrome -- the sync row sits between
  // the band and the list and is not one of these.
  //
  // Its own query rather than libraryVisibleRows reused, for that query's own
  // reason: the count depends on the row's box and this board's rows are not the
  // Library's. A shared answer would be right by coincidence and would stop being
  // right the first time either board moved.
  //
  // `statusLine` AND `panelW` ARE REQUIRED, NOT DEFAULTED, and that is
  // sectionHeaderHeight's rule for its own `rule`: the one caller that had to
  // answer this got it wrong by not being asked. design/SyncDone.dc.html draws a
  // status block between the band and the sync row, so that variant has one row
  // fewer -- and the first golden of it drew the last row's meta line straight
  // through the hint bar. A default would have hidden exactly that.
  virtual int articlesVisibleRows(int panelH, int panelW, const FontSet& fonts,
                                  std::string_view statusLine) const = 0;

  // Settings' BOX MODEL, not its row count, and the split is deliberate.
  //
  // Library's items are all one height, so a theme can answer "how many fit"
  // outright. Settings interleaves 54px rows with taller section headers, so the
  // answer depends on WHICH items are in the window -- and the item table belongs
  // to SettingsScreen, not here. So the theme reports the three heights it owns
  // and the screen, which knows where its headers are, does the counting. Neither
  // side ends up holding a copy of the other's data.
  // design/Sleep.dc.html, SleepCoverDetails.dc.html, SleepCover.dc.html. No hint
  // bar and no focus -- the device is asleep.
  //
  // `cover` MAY BE NULL AND IS NOT DEFAULTED, deliberately. A default argument on
  // a virtual is resolved statically, so an override that spelled a different one
  // would give two behaviours for one call depending on the static type of the
  // reference -- and every caller here goes through `Theme&`. Explicit nullptr at
  // the three call sites that have no cover is one word and cannot do that.
  virtual void renderSleep(Framebuffer& fb, const FontSet& fonts, const SleepViewModel& vm,
                           Plane plane, CoverSource* cover) = 0;

  // The reader's menu overlay, and the chapter list it opens.
  virtual void renderReaderMenu(Framebuffer& fb, const FontSet& fonts,
                                const ReaderMenuViewModel& vm, Plane plane) = 0;
  virtual void renderContents(Framebuffer& fb, const FontSet& fonts,
                              const ContentsViewModel& vm, Plane plane) = 0;

  // design/NamesEmpty.dc.html, and design/Names.dc.html once there are names to
  // list. ONE METHOD FOR BOTH BOARDS, because they are one screen with a variant --
  // renderHome's `nothingToContinue` branch is the same shape for the same reason.
  //
  // Today it only ever draws the empty arm, since `NamesViewModel` carries no rows
  // (see the view model for why). The list arm arrives with the stacked-row
  // primitive that would draw it.
  virtual void renderNames(Framebuffer& fb, const FontSet& fonts,
                           const NamesViewModel& vm, Plane plane) = 0;

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

  // design/Typography.dc.html.
  //
  // Takes the body face for the same reason renderReader does: the preview is set
  // in a ScalableFont rasterised at a runtime size, not in one of FontSet's twelve
  // fixed roles, and the whole point of the box is to show that size.
  //
  // A POINTER, AND NULL IS A SUPPORTED STATE -- exactly as renderReader's italic
  // is, and for the same reason: a caller with no body face gets an empty preview
  // box rather than no screen. A reference would force every such caller to invent
  // a null face, which is a class nothing needs.
  //
  // THE LEAD COMES FROM THE VIEW MODEL, not from this face and not from a constant
  // here: a face is pinned to a ppem by init() and carries no leading, so a theme
  // that resolved 1.7 itself would draw a preview contradicting the `Line spacing`
  // row directly beneath it on four of that row's five steps. The SIZE needs no
  // such field, because it has already arrived as `body` -- the same asymmetry
  // readerMetrics states about reading three typography fields and not four.
  virtual void renderTypography(Framebuffer& fb, const FontSet& fonts,
                                const GlyphSource* body, const TypographyViewModel& vm,
                                Plane plane) = 0;

  // Reader's COLUMN, the same split as settingsMetrics: the theme owns the box
  // model, the screen owns what goes in it. The theme knows the header band's and
  // the footer's heights because it draws them; only the screen can paginate,
  // because only it holds the chapter.
  //
  // Takes the body face as well as the ramp: the column's height is a whole
  // number of the BODY face's line boxes, and the body face is a ScalableFont
  // rasterised at a runtime size, not one of FontSet's eleven fixed roles.
  //
  // AND TAKES THE SETTINGS, because three of them reach the column
  // (design/Typography.dc.html) -- but not as three of the same thing, and the
  // distinction is this codebase's own `1-7 OF 12` rule about two units in one
  // expression. `margins` and `lineSpacing` are BOX MODEL: they set where the
  // column is and how far apart its baselines are. `justify` is not box model at
  // all -- it is how a FINISHED line is set, moving no break and no box (see
  // PageMetrics::justify) -- and it travels here only because it travels in this
  // struct.
  //
  // The struct rather than three ints: three loose ints at a call site are three
  // chances to pass them in the wrong order, and both the shell and the Typography
  // screen already hold this struct.
  //
  // THE FOURTH TYPOGRAPHY FIELD IS ABSENT ON PURPOSE. `bodyPpem` is not read here
  // because it has already arrived, as `body` -- a ScalableFont is pinned to a
  // pixel size by init(), so the face this is handed IS the chosen ppem and
  // reading the field as well would be a second spelling of it, free to disagree.
  // Four typography fields against three reads is a decision, not a gap.
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

  // THE PEEK'S COLUMN, which is the reading column's sibling and not a variant of it.
  //
  // design/Peek.dc.html: an inset panel, 2px border, 20px padding, over the veiled
  // page. The measure is therefore the PANEL's box rather than the page's, which is
  // the whole reason the panel cannot show a page number -- a narrower column
  // re-wraps, and re-wrapped text paginates differently.
  //
  // TAKES THE SETTINGS FOR TWO OF FOUR FIELDS, and `margins` is one it does NOT read:
  // a margin is the reading page's box model and the panel's box is its own, so there
  // is nothing for it to apply to. `bodyPpem` is absent for readerMetrics' reason --
  // it has already arrived as `body`. Four fields, two reads.
  virtual void peekMetrics(int panelW, int panelH, const FontSet& fonts,
                           const GlyphSource& body, const Settings& settings,
                           PageMetrics& out) const = 0;

  // HOW MANY WHOLE LINES OF BOOK TEXT THE PEEK SHOWS -- the derived half of the
  // inversion kPeekPanelH describes, and a query for libraryVisibleRows' reason: the
  // band's height depends on its type roles and the line box on the reader's own
  // ppem and lead, so this is not a number anything can hold.
  //
  // IT TAKES NO PANEL SIZE, and the absence is the statement: the box is fixed, so
  // the count cannot depend on which glass it is drawn on. libraryVisibleRows takes a
  // panelH precisely because ITS box is the screen.
  //
  // AT LEAST 1, always. See the implementation for why that clamp is dead code on the
  // shipped ramps and what it would mean if it ever fired.
  virtual int peekVisibleLines(const FontSet& fonts, const GlyphSource& body,
                               const Settings& settings) const = 0;

  // design/Peek.dc.html. Takes the page for renderReader's reason: it is already
  // positioned, in framebuffer coordinates, by reader/layout.h.
  virtual void renderPeek(Framebuffer& fb, const FontSet& fonts, const GlyphSource& body,
                          const GlyphSource* italic, const PeekViewModel& vm,
                          const Page& page, Plane plane) = 0;
};
}  // namespace reader
