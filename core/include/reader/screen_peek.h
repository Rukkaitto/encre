#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "reader/app.h"
#include "reader/book.h"
#include "reader/layout.h"
#include "reader/screen_reader.h"
#include "reader/toc.h"
#include "reader/viewmodel.h"

namespace reader {
class GlyphSource;

// design/Peek.dc.html: a page of the book over the veiled page you are on.
//
// Opened by picking a chapter in the table of contents, and it is the answer to the
// question a contents list cannot ask: "is this the chapter I meant?". `GO HERE`
// commits the jump; `CLOSE` leaves the reader's page exactly where it was.
//
// --- IT OWNS A HEADLESS ReaderScreen -----------------------------------------
//
// The panel is INSET -- 412 wide with a 2px border and 20px of padding either side --
// so its measure is ~368px against the reading page's 444. Narrower column, different
// wrap, different pagination. That single fact drives the whole design:
//
//   * IT IS WHY THE PANEL HAS NO PAGE NUMBER. "Page 53" of a re-wrapped column is not
//     page 53 of the book, so the band says chapter and percent, which are true at any
//     width. See PeekViewModel.
//   * IT IS WHY THE READER'S ALREADY-LAID `page_` CANNOT BE BORROWED. Those lines were
//     measured against the reading column; drawn into this one they would look almost
//     right and would overflow the panel.
//
// So the peek needs its own pagination over the peeked chapter, and the cheapest
// correct way to have one is to hold a ReaderScreen built at Theme::peekMetrics and
// forward page gestures into it. THREE ALTERNATIVES WERE WEIGHED:
//
//   * A BESPOKE MINIMAL PAGER. A second copy of open / advance / seek -- the three
//     routines this project has spent the most effort getting right, each carrying
//     rules (a hit leaves no live builder; a refused chapter turn must restore the
//     previous one; the index is pages KNOWN and not pages total) that a copy would
//     have to re-earn. The second copy is the extraction point, not the place to start.
//   * EXTRACTING A `ChapterPager`. The right shape in the abstract, and a large
//     refactor of the most performance-critical code in the firmware for the benefit of
//     one screen that wants a fraction of it.
//   * THIS. It costs ~2-3 KB -- the book's 12-bytes-a-spine-entry spans duplicated, and
//     a second copy of the chapter names -- plus a `Screen` used as a model, which is
//     an odd thing for a screen to be.
//
// WHAT IT BUYS IS THE ONE PROPERTY THAT MATTERS: THE BLOCK THE PEEK COMMITS IS BY
// CONSTRUCTION THE ONE THE READER RESTORES. Both sides are `ReaderScreen::currentCursor`
// over the same document, so there is no second spelling of a BLOCK free to disagree
// with the first -- which is precisely how a "go here" lands in the wrong paragraph.
//
// AND IT SAID `THE CURSOR` HERE, WHICH WAS TRUE OF THE BLOCK AND FALSE OF THE LINE
// (#48). A line is a line WITHIN A BLOCK AT ONE COLUMN WIDTH, and these are two column
// widths -- so what crosses is the block, and `chosenCursor` is where the line is
// dropped. See its definition in screen_peek.cpp, which prices both halves.
//
// --- THE MEMORY DANCE --------------------------------------------------------
//
// A live chapter peaks at 69,884 bytes with a 36,956-byte single allocation, against a
// measured 45,840-byte heap floor, so TWO live chapters do not fit -- and a peek is a
// second live chapter. The Reader beneath therefore RELEASES its stream while the panel
// is up (ReaderScreen::releaseChapter) and takes it back on the way out
// (reacquireChapter, which costs no seekTo). What survives the release is everything
// the paint reads, so App::render draws the veiled page underneath with no decode at
// all. That sequencing is the shell's; this screen only assumes it has the heap.
//
// --- TWO PROPERTIES THAT FALL OUT RATHER THAN BEING ARRANGED -----------------
//
//   * PAGING OFF EITHER END CROSSES CHAPTERS, because openChapterAt already does that
//     -- the peek is a reader, so reading past the end of what you peeked at continues
//     into the next chapter exactly as it would on the page.
//   * THE INNER READER IS INVISIBLE TO THE QUIET-WINDOW JOBS. The shell drives the page
//     count, the refinement, the ring warm and the restream through the App's stack,
//     and this ReaderScreen is not on it. Nothing has to be told to leave it alone.
//
// --- AND ITS ANCHOR IS DISCARDED WITH IT -------------------------------------
//
// The inner reader keeps a ReturnAnchor of its own, and paging inside a peek raises and
// spends it there. That is right and it is inert: an anchor is a way back to a page the
// reader can return to, and nothing here is a page they were reading. The OUTER
// Reader's anchor is touched by exactly one thing -- the shell acting on the commit,
// where goToPosition raises it on the position being LEFT. So the peek's own anchor
// dies with the screen and no caller ever reads it.
class PeekScreen : public Screen {
 public:
  // A BOOK, for the device: the peeked chapter is a spine entry of the book the reader
  // has open, and `fs` and `book` must outlive the screen exactly as they must for a
  // ReaderScreen.
  //
  // NO `percent` PARAMETER, AND IT USED TO TAKE ONE. The caller computed the book-wide
  // percentage at the peeked chapter and this screen held it for its whole life -- so
  // the band's number stayed on the chapter the panel was OPENED at while its label
  // followed the reader across a chapter boundary. Paging off either end crosses into
  // the next spine entry (see above, where it is listed as a designed property), so the
  // two halves of one composed run described different chapters. The book is right here
  // in the inner reader, so the number is derived from the chapter on screen instead --
  // see PeekScreen::percentHere.
  //
  // `at` IS WHERE IN THE CHAPTER TO LAND, and it is what Mentions needs. Contents
  // passes the default and gets page one of the spine entry, which is what it has
  // always got; a sighting passes the block it sits in.
  //
  // THE MECHANISM ALREADY EXISTED AND ONLY THE PARAMETER DID NOT.
  // `ReaderScreen::restoreAt` is public and must be called BEFORE `setMetrics` --
  // it arms `startAt_`, which `walkToChapter` consumes on the first candidate -- so
  // this constructor is the only place that ordering has to be right.
  //
  // THE CURSOR IS `(block, 0)` BY CONSTRUCTION. `fitOf` grades `line` as the field
  // that survives neither a re-layout nor a re-bind, and a landing only needs the
  // paragraph; `chosenCursor` already zeroes it on the way out for the same reason.
  PeekScreen(FileSystem& fs, OpenedBook book, int spine, const GlyphSource* body,
             Cursor at = Cursor{});

  // A single chapter already in memory, for the simulator and the goldens, which have
  // no card -- the same pair of constructors ReaderScreen has and for the same reason.
  // There is no book behind it, so paging past either end simply stops.
  //
  // THIS ONE KEEPS `percent`, and that is the whole reason the field survives: with no
  // book there are no chapter byte spans to sum, so the board's 4% cannot be derived
  // from anything and has to be stated.
  PeekScreen(std::string_view xhtml, std::string chapter, int percent,
             const GlyphSource* body);
  ~PeekScreen() override;

  ScreenId id() const override { return ScreenId::Peek; }

  // A PANEL OVER THE PAGE, not a screen instead of it. The reader has not left the
  // book; they have asked it a question. App::render paints the Reader underneath and
  // this draws its veil, its panel and its own hint bar over it.
  bool isOverlay() const override { return true; }

  // BODY TEXT AT READING SIZE, so the Reader's reason applies verbatim: this is the one
  // thing on the device drawn from a runtime-rasterised face, and Mono would
  // hard-threshold every stem of a serif face at 32px. The peek is the one overlay that
  // does NOT declare Mono -- the reader menu and the actions panel are chrome, and
  // chrome is small type where a stipple reads as noise; this is the same prose the
  // page underneath is set in, and it is the whole content of the panel.
  //
  // IT COSTS THE PARTIAL REPAINT, and that is the trade rather than an oversight.
  // App::canRenderTopOnly refuses grayscale outright: the sequence renders three planes
  // plus a rebase, so the frame between passes holds a DIFFERENT plane and the
  // precondition ("the frame is the one my last paint left") is false for every pass but
  // the first. What that would have bought is nothing here anyway -- a peek has no focus
  // to move, and every press that changes the panel changes all of its text.
  Fidelity fidelity() const override { return Fidelity::Grayscale; }

  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  // THE PANEL'S COLUMN, from Theme::peekMetrics -- not the reading page's. Same contract
  // as ReaderScreen::setMetrics: the screen is not renderable until it has been called,
  // because a chapter cannot be paginated without a column height.
  //
  // AND THE LEAD DOES NOT TRAVEL WITH IT, where it used to. While the panel's HEIGHT
  // was a result of a fixed line count, renderPeek had to compute its box from the same
  // line height peekMetrics did, so PeekViewModel carried the lead across for it. The
  // box is fixed now (theme.h's kPeekPanelH) and neither function reads a lead at all,
  // so the two spellings this project's first invariant warns about no longer exist to
  // be reconciled. The lead reaches PageBuilder through `m` and stops there.
  void setMetrics(const PageMetrics& m);

  void setItalic(const GlyphSource* italic);

  // The book's chapter names, so the band can say `LIVRE I` instead of `CH. 08`. A
  // COPY, as ReaderScreen's is, and this is the ~1.2 KB the class comment prices.
  void setChapterNames(std::vector<TocEntry> toc);

  const PeekViewModel& vm() const { return vm_; }
  const Page& page() const;

  // --- WHAT THE SHELL READS OFF A CLOSING PEEK --------------------------------
  //
  // BOTH BUTTONS ANSWER `Pop`, and this flag is the whole difference between them. The
  // peek cannot move the Reader, because the Reader is on the stack UNDERNEATH it: a
  // screen returns one Action, and "pop me, then jump the screen I was covering" is not
  // one. Contents answers popTo(Reader) for the identical reason and the shell does the
  // moving there too.
  //
  // SO THE SHELL READS THESE WHILE THIS SCREEN IS STILL ON TOP -- the dispatch pops it,
  // and after that there is no screen left to ask. That ordering is the same one
  // `chosenSpine()` on Contents already lives under.
  bool committed() const { return committed_; }

  // WHERE `GO HERE` GOES: the spine entry, and the BLOCK the page the panel was showing
  // began in. A cursor and not a page number, for reading_position.h's reason -- a page
  // index is a position at one column width, and this column is not the reader's.
  //
  // The reader may have paged several pages into the peek before committing, so page one
  // of the chapter is the wrong landing and ReaderScreen::goToPosition is what takes
  // both halves. The page NUMBER is then computed on arrival by counting boundaries,
  // which is what lets the peek be honest about not having one.
  //
  // ITS `line` IS ALWAYS ZERO, and that is the fix for #48 rather than an omission --
  // exactly the field reading_position.h zeroes for a `Relaid` fit and relayout() drops
  // one layer up, for exactly the same reason. The definition in screen_peek.cpp carries
  // the measurements and the two alternatives that were refused.
  int chosenSpine() const;
  Cursor chosenCursor() const;

 private:
  void syncVm();
  // The band's number: the book-wide percentage at the chapter the panel is SHOWING,
  // which is not necessarily the one it was opened at. Falls back to the constructor's
  // figure when there is no book -- see the in-memory constructor.
  int percentHere() const;

  std::unique_ptr<ReaderScreen> inner_;
  PeekViewModel vm_{};
  // ONLY THE IN-MEMORY CONSTRUCTOR SETS THIS. Card-backed peeks derive the number.
  int percent_ = 0;
  bool committed_ = false;
};

}  // namespace reader
